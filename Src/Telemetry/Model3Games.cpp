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
#include <string>
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
     * Scud Race's engine, shared by Scud Race and Daytona USA 2: a table of 40 cars, the player first; each with its
     * position (floats, m), a 3x3 matrix (rows: the car's right, up and back), its velocity (m a frame), km/h,
     * gear (0 neutral, 1..4), rpm, laps done, position (0 leading) and track segment. The race's clocks count
     * 57 frames a second. The game's world has its x the other way: mirrored here, so that the car's right is
     * up x forward.
     */
    struct ScudLayout
    {
      const char *game;
      uint32_t base, stride;      // the car table
      uint32_t x, matrix, velocity, kmh, gear, rpm, laps, position, segment;
      bool wideLaps;              // laps and position: u16 (else u8)
      int segments;               // segments a lap (the beginner course's)
      uint32_t timeLeft, raceTime, lapTable, stateWord;
      float rpmMax, rpmIdle;
    };

    class CScudEngine : public IDecoder
    {
    public:
      CScudEngine(const ScudLayout &layout)
        : m_l(layout)
      {
      }

      bool Decode(CModel3 &model3, uint64_t frame, Frame &out) override
      {
        CRam ram(model3);
        const ScudLayout &l = m_l;
        const int cars = 40;
        const float clock = 57.0f;
        out.game = l.game;
        out.frame = frame;
        out.state = DecodeState(ram);
        out.racing = out.state == State::Race;
        out.time_left = ram.U32(l.timeLeft) / clock;
        float raceTime = ram.U32(l.raceTime) / clock;
        out.race_time = raceTime;
        int done = Laps(ram, l.base);
        float laps = 0, best = NAN, last = NAN;
        for (int i = 0; i < std::min(done, 16); i++)
        {
          float t = ram.U32(l.lapTable + 4 * i) / clock;
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
          uint32_t c = l.base + k * l.stride;
          Vehicle v;
          v.x = -ram.F32(c + l.x);
          v.y = ram.F32(c + l.x + 4);
          v.z = ram.F32(c + l.x + 8);
          // Every car keeps its place in `others` (the derived values follow each from frame to frame)
          if (k == 0 && (!std::isfinite(v.x) || !std::isfinite(v.z) || (v.x == 0 && v.z == 0)))
            return false;
          float rows[3][3];
          for (int r = 0; r < 3; r++)
          {
            ram.Vec3(c + l.matrix + 12 * r, rows[r]);
            rows[r][0] = -rows[r][0];     // the mirror
          }
          float forward[3] = { -rows[2][0], -rows[2][1], -rows[2][2] };
          SetOrientation(v, rows[0], rows[1], forward);
          v.vx = -ram.F32(c + l.velocity) * float(kFps);
          v.vy = ram.F32(c + l.velocity + 4) * float(kFps);
          v.vz = ram.F32(c + l.velocity + 8) * float(kFps);
          v.hasVelocity = true;
          v.speed = ram.U16(c + l.kmh) / 3.6f;
          v.gear = int(ram.U32(c + l.gear));
          v.gears = 4;
          v.rpm = ram.U16(c + l.rpm);
          v.rpm_max = l.rpmMax;
          v.rpm_idle = l.rpmIdle;
          int lapsDone = Laps(ram, c);
          v.lap = lapsDone + 1;
          v.position = (l.wideLaps ? ram.U16(c + l.position) : ram.U8(c + l.position)) + 1;
          v.progress = lapsDone + std::clamp(int(ram.S16(c + l.segment)), 0, l.segments - 1) / float(l.segments);
          if (k == 0)
            out.player = v;
          else
            out.others.push_back(v);
        }
        return true;
      }

    private:
      int Laps(const CRam &ram, uint32_t car) const
      {
        return m_l.wideLaps ? ram.U16(car + m_l.laps) : ram.U8(car + m_l.laps);
      }

      State DecodeState(const CRam &ram) const
      {
        uint16_t w = ram.U16(m_l.stateWord);
        if (std::string(m_l.game).compare(0, 4, "scud") == 0)
        {
          // Scud Race: 0 attract, 0x4000 the menus, 0x2000 the choices made, 0x1000 the race's start (the rolling
          // start, driven by the game), 0x0400 racing, 0x0010 time up and game over
          if (w & 0x1400) return State::Race;
          if (w & 0x6000) return State::Select;
          if (w & 0x0010) return State::Over;
          return w == 0 ? State::Attract : State::Unknown;
        }
        // Daytona USA 2: 0x4000 racing, 0x0100 time up; 0 in the attract mode and the menus alike
        if (w & 0x4000) return State::Race;
        if (w & 0x0100) return State::Over;
        return State::Unknown;
      }

      ScudLayout m_l;
    };

    // Daytona USA 2 - Battle on the Edge: 40 cars of 0x1000 bytes from 0x501000; laps and position bytes (the byte
    // before each holds something else at times)
    const ScudLayout kDaytona2 = { "daytona2", 0x501000, 0x1000,
      0x09C, 0x0D0, 0x0F8, 0x09A, 0x1D4, 0x1DE, 0x119, 0x149, 0x194, false, 192,
      0x105010, 0x10500C, 0x1062F8, 0x105002, 8000, 965 };

    // Scud Race: 40 cars of 0x300 bytes from 0x181100; the segments a lap of its beginner course, about 1011
    const ScudLayout kScud = { "scud", 0x181100, 0x300,
      0x01C, 0x050, 0x078, 0x01A, 0x13C, 0x146, 0x098, 0x0D4, 0x096, false, 1011,
      0x104010, 0x10400C, 0x105284, 0x104002, 7000, NAN };

    /*
     * Sega Rally 2 (srally2). The player's car: 0x22449C/A0/A4 x, y, z, 0x2244A8 and 0x2244B0 the horizontal
     * velocity (m a frame), 0x224500 its speed (x 216.85 km/h), its 3x3 matrix at 0x224A10 (rows: right, up,
     * forward), 0x22F358 u32 gear (0 neutral, 1..4), 0x22F368 the rpm the rev counter shows (float).
     * The race: 0x22CF88 u32 the time left and 0x1B0EB0 u32 the total time (1/60 s, counting while racing),
     * 0x22D01A u16 position (0 leading), 0x22CFA7 the stage (0 Desert, 1 Mountain...; to be confirmed further).
     * The other cars from their wheels (below).
     */
    class CSegaRally2 : public IDecoder
    {
    public:
      bool Decode(CModel3 &model3, uint64_t frame, Frame &out) override
      {
        CRam ram(model3);
        out.game = "srally2";
        out.frame = frame;
        // Racing: the total time counting (it stops at a stage's end, at time up, and in the attract mode; 0x1B0C57,
        // once taken for the race's flag, follows the gear)
        uint32_t total = ram.U32(0x1B0EB0);
        if (total != m_lastTotal)
        {
          if (total > m_lastTotal && total - m_lastTotal < 30)
            m_lastCount = frame;
          m_lastTotal = total;
        }
        bool racing = frame - m_lastCount < 8;
        out.racing = racing;
        out.state = racing ? State::Race : State::Unknown;
        out.time_left = ram.U32(0x22CF88) / 60.0f;
        out.race_time = total / 60.0f;
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
        // The other cars: blocks of four wheels (x, y, z floats each: front left, front right, rear left, rear right)
        // every 0x198 bytes from 0x13AC68, the player's first; only the cars near the player are there (5 at a time
        // in the Championship), the other blocks zero. A car: its wheels' centre, its forward from the rear axle to
        // the front one, its right from the left wheels to the right ones
        out.others.clear();
        for (int k = 1; k < 16; k++)
        {
          float w[4][3];
          for (int j = 0; j < 4; j++)
            ram.Vec3(0x13AC68 + k * 0x198 + 12 * j, w[j]);
          Vehicle o;
          float c[3], f[3], r[3];
          for (int i = 0; i < 3; i++)
          {
            c[i] = (w[0][i] + w[1][i] + w[2][i] + w[3][i]) / 4;
            f[i] = (w[0][i] + w[1][i] - w[2][i] - w[3][i]) / 2;
            r[i] = (w[1][i] + w[3][i] - w[0][i] - w[2][i]) / 2;
          }
          float lf = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
          if (!std::isfinite(c[0]) || !std::isfinite(c[2]) || std::fabs(c[0]) + std::fabs(c[2]) < 2 || lf < 0.5f)
          {
            o.x = o.y = o.z = NAN;
            out.others.push_back(o);
            continue;
          }
          float fr = 0;
          for (int i = 0; i < 3; i++)
          {
            f[i] /= lf;
            fr += f[i] * r[i];
          }
          for (int i = 0; i < 3; i++)
            r[i] -= fr * f[i];
          float lr = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
          for (int i = 0; i < 3; i++)
            r[i] /= lr;
          float up[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };
          o.x = c[0];
          o.y = c[1];
          o.z = c[2];
          SetOrientation(o, r, up, f);
          out.others.push_back(o);
        }
        return true;
      }

    private:
      uint32_t m_lastTotal = 0;
      uint64_t m_lastCount = ~0ull >> 1;
    };

    /*
     * Dirt Devils (dirtdvls). The cars' own state was not found: their wheel tracks are, 8 cars of 10 records of
     * 0x38 bytes from 0x2230D8, the newest first, the player's first; a record: two points along the track (x, y, z
     * floats, about 0.0608 m a unit, its y downwards) and the ground's normal at +0x18. The player's car: the newest
     * record's middle, its forward the motion, its up the ground's normal. 0x25B2AA u16 km/h, 0x2230AB u8 the gear
     * in the manual gearbox (1..5; 0 in the automatic one), 0x25DA22 u16 rpm, 0x228EC2 u16 the time left (1/60 s),
     * 0x224A74 u8 the laps done.
     */
    class CDirtDevils : public IDecoder
    {
    public:
      bool Decode(CModel3 &model3, uint64_t frame, Frame &out) override
      {
        CRam ram(model3);
        const uint32_t base = 0x2230D8, carSize = 10 * 0x38;
        const float unit = 0.0608f;
        out.game = "dirtdvls";
        out.frame = frame;
        out.state = State::Unknown;
        out.time_left = ram.U16(0x228EC2) / 60.0f;
        out.others.clear();
        for (int k = 0; k < 8; k++)
        {
          uint32_t r = base + k * carSize;
          float a[3], b[3], n[3];
          ram.Vec3(r, a);
          ram.Vec3(r + 0x2C, b);
          ram.Vec3(r + 0x18, n);
          Vehicle v;
          v.x = (a[0] + b[0]) / 2 * unit;
          v.y = -(a[1] + b[1]) / 2 * unit;
          v.z = (a[2] + b[2]) / 2 * unit;
          if (k == 0 && (!std::isfinite(v.x) || !std::isfinite(v.z)))
            return false;
          if (k == 0)
          {
            // Up: the ground's normal, y flipped; forward: the motion since the last frame, on the ground
            float up[3] = { n[0], -n[1], n[2] };
            float d[3] = { v.x - m_last[0], v.y - m_last[1], v.z - m_last[2] };
            float along = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
            for (int i = 0; i < 3; i++)
              d[i] -= along * up[i];
            float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 0.02f && len < 5.0f)
              for (int i = 0; i < 3; i++)
                m_forward[i] = d[i] / len;
            float *f = m_forward;
            float right[3] = { up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0] };
            SetOrientation(v, right, up, f);
            m_last[0] = v.x; m_last[1] = v.y; m_last[2] = v.z;
            v.speed = ram.U16(0x25B2AA) / 3.6f;
            int gear = ram.U8(0x2230AB);
            v.gear = gear == 0 ? kUnknown : gear;
            v.gears = 5;
            v.rpm = ram.U16(0x25DA22);
            v.rpm_max = 8000;
            // The laps done, u8 0x224A74 (16 October: one more at each lap, 894 m apart on the Novice Canyon)
            v.lap = ram.U8(0x224A74) + 1;
            out.player = v;
          }
          else
            out.others.push_back(v);
        }
        return true;
      }

    private:
      float m_last[3] = { 0, 0, 0 };
      float m_forward[3] = { 0, 0, 1 };
    };

    /*
     * Emergency Call Ambulance (eca). The ambulance: 0x2AC438/3C/40 x, y, z (floats, in tenths of a metre: the
     * game's speed against the motion gives 0.1005 m), 0x2AC474/78/7C its velocity (units a frame), its 3x3 matrix
     * at 0x2AC258 (rows: left, up, forward), 0x3B3DEC its speed (x 21.614 km/h), 0x2AD790 u32 gear (1..4),
     * 0x2AD788 the rpm (float). Its world has its x the other way: mirrored.
     */
    class CEmergencyCallAmbulance : public IDecoder
    {
    public:
      bool Decode(CModel3 &model3, uint64_t frame, Frame &out) override
      {
        CRam ram(model3);
        out.game = "eca";
        out.frame = frame;
        out.state = State::Unknown;
        const float unit = 0.1f;
        Vehicle v;
        v.x = -ram.F32(0x2AC438) * unit;
        v.y = ram.F32(0x2AC43C) * unit;
        v.z = ram.F32(0x2AC440) * unit;
        if (!std::isfinite(v.x) || !std::isfinite(v.z))
          return false;
        float rows[3][3];
        for (int r = 0; r < 3; r++)
        {
          ram.Vec3(0x2AC258 + 12 * r, rows[r]);
          rows[r][0] = -rows[r][0];       // the mirror
        }
        float right[3] = { -rows[0][0], -rows[0][1], -rows[0][2] };
        SetOrientation(v, right, rows[1], rows[2]);
        v.vx = -ram.F32(0x2AC474) * unit * float(kFps);
        v.vy = ram.F32(0x2AC478) * unit * float(kFps);
        v.vz = ram.F32(0x2AC47C) * unit * float(kFps);
        v.hasVelocity = true;
        v.speed = ram.F32(0x3B3DEC) * 21.614f / 3.6f;
        v.gear = int(ram.U32(0x2AD790));
        v.gears = 4;
        v.rpm = ram.F32(0x2AD788);
        out.player = v;
        out.others.clear();
        return true;
      }
    };
  }

  std::unique_ptr<IDecoder> MakeDecoder(const Game &game)
  {
    // The clones checked to keep their parent's addresses (clone_check.py, Telemetry.md). Scud Race Deluxe and Plus
    // and the other Sega Rally 2 versions keep theirs elsewhere: no decoder yet
    static const char *const kSameAsParent[] = { "dayto2pe", "scudau" };
    for (const char *clone : kSameAsParent)
    {
      if (game.name == clone)
      {
        Game parent = game;
        parent.name = game.parent;
        parent.parent.clear();
        return MakeDecoder(parent);
      }
    }
    if (game.name == "daytona2")
      return std::make_unique<CScudEngine>(kDaytona2);
    if (game.name == "scud")
      return std::make_unique<CScudEngine>(kScud);
    if (game.name == "srally2")
      return std::make_unique<CSegaRally2>();
    if (game.name == "dirtdvls")
      return std::make_unique<CDirtDevils>();
    if (game.name == "eca")
      return std::make_unique<CEmergencyCallAmbulance>();
    return nullptr;
  }
}
