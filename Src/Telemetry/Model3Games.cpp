/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * Model3Games.cpp
 *
 * The racing games' decoders: where each keeps its cars and its race in the
 * main RAM, found by recording the RAM while an autopilot drove and checked
 * against what the game shows (the rev counter, the clocks, the position).
 * The addresses are those of the ROM set named; another version of a game may
 * keep them elsewhere, and gets no decoder until checked.
 */

#include "Telemetry.h"

#include "Supermodel.h"
#include "Game.h"
#include "Model3/Model3.h"

#include <algorithm>
#include <cstring>

namespace Telemetry
{
  namespace
  {
    // The main RAM as the PowerPC reads it: big-endian
    class CRam
    {
    public:
      CRam(CModel3 &model3)
        : m_model3(model3)
      {
      }

      uint8_t U8(uint32_t a) const { return m_model3.Read8(a); }
      uint16_t U16(uint32_t a) const { return m_model3.Read16(a); }
      int16_t S16(uint32_t a) const { return int16_t(m_model3.Read16(a)); }
      uint32_t U32(uint32_t a) const { return m_model3.Read32(a); }

      float F32(uint32_t a) const
      {
        uint32_t u = m_model3.Read32(a);
        float f;
        memcpy(&f, &u, 4);
        return f;
      }

      // A row of three floats
      void Vec3(uint32_t a, float v[3]) const
      {
        for (int i = 0; i < 3; i++)
          v[i] = F32(a + 4 * i);
      }

    private:
      CModel3 &m_model3;
    };

    /*
     * Daytona USA 2 - Battle on the Edge (daytona2). 40 cars of 0x1000 bytes from 0x501000, the player first; each:
     *   +0x09A u16 km/h, +0x09C/A0/A4 x, y, z (floats, m), +0x0D0 a 3x3 matrix (rows: the car's right, up, back),
     *   +0x0F8/FC/100 velocity (m a frame), +0x118 u16 laps done, +0x148 u16 position (0 leading),
     *   +0x194 s16 the track segment (192 a lap on the beginner course), +0x1D4 u32 gear (0 neutral, 1..4),
     *   +0x1DE u16 rpm.
     * The race: 0x105008 u16 0xFFFF while racing, 0x1083BE 15 at the game over, 0x10500C u32 the race's frames,
     * 0x105010 u32 the frames left, 0x1062F8 u32 a lap's frames (one a lap). Its clocks count 57 frames a second.
     * The game's world has its x the other way: mirrored here, so that the car's right is up x forward.
     */
    class CDaytona2 : public IDecoder
    {
    public:
      bool Decode(CModel3 &model3, uint64_t frame, Frame &out) override
      {
        CRam ram(model3);
        const uint32_t base = 0x501000, stride = 0x1000;
        const int cars = 40;
        const float clock = 57.0f;
        out.game = "daytona2";
        out.frame = frame;
        bool racing = ram.U16(0x105008) == 0xFFFF;
        out.racing = racing;
        out.state = racing ? State::Race : ram.U8(0x1083BE) == 15 ? State::Over : State::Unknown;
        out.time_left = ram.U32(0x105010) / clock;
        float raceTime = ram.U32(0x10500C) / clock;
        out.race_time = raceTime;
        int done = ram.U16(base + 0x118);
        float laps = 0, best = NAN, last = NAN;
        for (int i = 0; i < std::min(done, 16); i++)
        {
          float t = ram.U32(0x1062F8 + 4 * i) / clock;
          laps += t;
          last = t;
          best = std::isnan(best) ? t : std::min(best, t);
        }
        out.lap_time = raceTime - laps;
        out.last_lap = last;
        out.best_lap = best;
        out.others.clear();
        for (int k = 0; k < cars; k++)
        {
          uint32_t c = base + k * stride;
          Vehicle v;
          v.x = -ram.F32(c + 0x9C);
          v.y = ram.F32(c + 0xA0);
          v.z = ram.F32(c + 0xA4);
          // Every car keeps its place in `others` (the derived values follow each from frame to frame)
          if (k == 0 && (!std::isfinite(v.x) || !std::isfinite(v.z) || (v.x == 0 && v.z == 0)))
            return false;
          float rows[3][3];
          for (int r = 0; r < 3; r++)
          {
            ram.Vec3(c + 0xD0 + 12 * r, rows[r]);
            rows[r][0] = -rows[r][0];     // the mirror
          }
          float forward[3] = { -rows[2][0], -rows[2][1], -rows[2][2] };
          SetOrientation(v, rows[0], rows[1], forward);
          v.vx = -ram.F32(c + 0xF8) * float(kFps);
          v.vy = ram.F32(c + 0xFC) * float(kFps);
          v.vz = ram.F32(c + 0x100) * float(kFps);
          v.hasVelocity = true;
          v.speed = ram.U16(c + 0x9A) / 3.6f;
          v.gear = int(ram.U32(c + 0x1D4));
          v.gears = 4;
          v.rpm = ram.U16(c + 0x1DE);
          v.rpm_max = 8000;
          v.rpm_idle = 965;
          int lapsDone = ram.U16(c + 0x118);
          v.lap = lapsDone + 1;
          v.position = ram.U16(c + 0x148) + 1;
          v.progress = lapsDone + std::clamp(ram.S16(c + 0x194), int16_t(0), int16_t(191)) / 192.0f;
          if (k == 0)
            out.player = v;
          else
            out.others.push_back(v);
        }
        return true;
      }
    };

    /*
     * Sega Rally 2 (srally2). The player's car: 0x22449C/A0/A4 x, y, z, 0x2244A8 and 0x2244B0 the horizontal
     * velocity (m a frame), 0x224500 its speed (x 216.85 km/h), its 3x3 matrix at 0x224A10 (rows: right, up,
     * forward), 0x22F358 u32 gear (0 neutral, 1..4), 0x22F368 the rpm the rev counter shows (float).
     * The race: 0x1B0C57 1 while racing, 0x22CF88 u32 the time left and 0x1B0EB0 u32 the total time (1/60 s),
     * 0x22D01A u16 position (0 leading), 0x22CFA7 the stage (0 Desert, 1 Mountain...; to be confirmed further).
     */
    class CSegaRally2 : public IDecoder
    {
    public:
      bool Decode(CModel3 &model3, uint64_t frame, Frame &out) override
      {
        CRam ram(model3);
        out.game = "srally2";
        out.frame = frame;
        bool racing = ram.U8(0x1B0C57) == 1;
        out.racing = racing;
        out.state = racing ? State::Race : State::Unknown;
        out.time_left = ram.U32(0x22CF88) / 60.0f;
        out.race_time = ram.U32(0x1B0EB0) / 60.0f;
        out.lap_time = out.race_time;
        out.stage = ram.U8(0x22CFA7);
        Vehicle v;
        v.x = ram.F32(0x22449C);
        v.y = ram.F32(0x2244A0);
        v.z = ram.F32(0x2244A4);
        if (!std::isfinite(v.x) || !std::isfinite(v.z))
          return false;
        float rows[3][3];
        for (int r = 0; r < 3; r++)
          ram.Vec3(0x224A10 + 12 * r, rows[r]);
        SetOrientation(v, rows[0], rows[1], rows[2]);
        v.vx = ram.F32(0x2244A8) * float(kFps);
        v.vy = 0;
        v.vz = ram.F32(0x2244B0) * float(kFps);
        v.hasVelocity = true;
        v.speed = ram.F32(0x224500) * 216.85f / 3.6f;
        v.gear = int(ram.U32(0x22F358));
        v.gears = 4;
        v.rpm = ram.F32(0x22F368);
        v.rpm_max = 8000;
        v.rpm_idle = 500;
        v.lap = 1;
        v.position = ram.U16(0x22D01A) + 1;
        out.player = v;
        out.others.clear();
        return true;
      }
    };
  }

  std::unique_ptr<IDecoder> MakeDecoder(const Game &game)
  {
    if (game.name == "daytona2")
      return std::make_unique<CDaytona2>();
    if (game.name == "srally2")
      return std::make_unique<CSegaRally2>();
    return nullptr;
  }
}
