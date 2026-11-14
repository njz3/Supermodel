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
 * Model1Games.cpp
 *
 * The Model 1 racing games' decoders (MakeModel1Decoder): Virtua Racing and
 * Virtua Formula, where they keep their cars and their race in the V60's RAMs,
 * found by recording the RAM while an autopilot drove (in agent mode) and
 * checked against what the game shows (the speedometer, the rev counter, the
 * clocks, the position).
 */

#include "Telemetry.h"

#include "Supermodel.h"
#include "Game.h"
#include "Model1/Model1.h"

#include <algorithm>
#include <cstring>

namespace Telemetry
{
  namespace
  {
    // The V60's RAMs through its bus: little-endian 16-bit words, a 32-bit value low word first
    class CRam
    {
    public:
      CRam(CModel1 &model1)
        : m_model1(model1)
      {
      }

      uint8_t U8(uint32_t a) const { return m_model1.Read8(a); }
      uint16_t U16(uint32_t a) const { return m_model1.Read16(a); }
      int16_t S16(uint32_t a) const { return int16_t(m_model1.Read16(a)); }
      uint32_t U32(uint32_t a) const { return m_model1.Read16(a) | (uint32_t(m_model1.Read16(a + 2)) << 16); }
      int32_t S32(uint32_t a) const { return int32_t(U32(a)); }

      float F32(uint32_t a) const
      {
        uint32_t u = U32(a);
        float f;
        memcpy(&f, &u, 4);
        return f;
      }

    private:
      CModel1 &m_model1;
    };

    /*
     * Virtua Racing (vr), and Virtua Formula (vformula), which keeps everything at the same addresses.
     *
     * The game moves its cars once every two frames, and its clocks count 1/32 s at each move (the screen's
     * hundredths go up by 0.47 s every 30 frames, the "TOTAL TIME" 1'46"40 is 3405 of them): it takes a move for
     * 1/32 s where the machine takes 1/28.76 s. Its speedometer goes by the same clock: km/h = 113.8 x the metres
     * of a move, 11 % more than the speed the positions give in real time.
     *
     * 16 cars of 0x280 bytes from 0x400D00 (RAM A), the player's pointed at by 0x501320 (u32; the first, 0x400D00,
     * in a game for one): +0x14 u16 km/h (the speedometer); +0x1E, +0x22, +0x26 x, y, z (floats, metres: the wheels'
     * points at +0x72.. are 1.8 m apart across and 3.6 m along; y up, the world right-handed: steering right makes
     * atan2(dx, dz) grow); +0x2A, +0x2C, +0x2E its pitch (+ nose up, the slope of the road), minus its heading (the
     * heading of its motion when it does not slide) and its roll (+ leaning right: the camera's matrix at 0x501B00
     * leans the same way), s16, 65536 a turn; +0x44 the metres of its last move along its nose (float); +0x70 s16
     * its laps done (-1 on the grid behind the line); +0xA6 u16 its segment of the course (0 at the line; 338 a lap
     * on Big Forest, the course's count at 0x501954); +0x123 u8 its position (0 leading, as the screen's
     * "POSITION"); +0x140..+0x148 u16 what is under its middle and its wheels (front left, front right, rear left,
     * rear right; 2 the road, 0x100 the grey run-off, 4 grass, 0x20 against a wall). The player's car only:
     * +0x156 u16 its gear less one (an automatic gearbox of 7; +0x1B4 the same a few frames later), +0x15A u16 the
     * engine's rpm (+0x15E the rev counter's needle, which follows it; the red arc from 14000, the dial to 16000).
     *
     * The race (RAM B): 0x501122 u16 what the game is doing (below), 0x501168 u32 the race time (stopped at the
     * finish: "TOTAL TIME"), 0x50116C s32 the time left (-1 at time up), 0x501B80 u32 the player's laps' times, one
     * after the other (the lap in progress is the race time less them), all in 1/32 s (1'01"81 shown for 1978);
     * 0x5011A4 u16 the race's laps (5), 0x501954 u16 the course's segments a lap: 338 Big Forest (beginner), 418
     * Bay Bridge (medium, also the attract mode's course), 592 Acropolis (expert), which tells the course. The laps'
     * lengths, from the AI cars' paths: about 3500, 3350 and 4380 m.
     */
    class CVirtuaRacing : public IDecoder
    {
    public:
      explicit CVirtuaRacing(const char *game)
        : m_game(game)
      {
      }

      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram(static_cast<CModel1 &>(emulator));
        const uint32_t base = 0x400D00, stride = 0x280;
        const int cars = 16;
        const float clock = 32.0f;
        const float turn = 2 * 3.14159265f / 65536;
        uint32_t player = ram.U32(0x501320);
        if (player < base || player >= base + cars * stride || (player - base) % stride != 0)
          return false;
        out.game = m_game;
        out.frame = frame;
        out.state = DecodeState(ram.U16(0x501122));
        out.racing = out.state == State::Race;
        out.time_left = std::max(ram.S32(0x50116C), 0) / clock;
        out.race_time = ram.U32(0x501168) / clock;
        int laps = ram.U16(0x5011A4);
        out.laps = laps >= 1 && laps <= 99 ? laps : 0;
        int segments = ram.U16(0x501954);
        const Course *course = nullptr;
        for (const Course &c : kCourses)
          if (c.segments == segments)
            course = &c;
        if (course)
        {
          out.track = course->name;
          out.difficulty = course->difficulty;
          out.lapLength = course->length;
        }
        if (segments < 16 || segments > 4000)
          segments = 338;
        // The player's laps: their times one after the other, the lap in progress the race time less them
        int done = std::clamp(int(ram.S16(player + 0x70)), 0, 32);
        float laid = 0, last = NAN, best = NAN;
        for (int i = 0; i < done; i++)
        {
          float t = ram.U32(0x501B80 + 4 * i) / clock;
          laid += t;
          last = t;
          best = std::isnan(best) ? t : std::min(best, t);
        }
        out.lap_time = std::max(out.race_time - laid, 0.0f);
        out.last_lap = last;
        out.best_lap = best;
        out.finished = out.state == State::Over && out.laps > 0 && done >= out.laps;
        out.others.clear();
        for (int k = 0; k < cars; k++)
        {
          uint32_t c = base + k * stride;
          Vehicle v;
          v.x = ram.F32(c + 0x1E);
          v.y = ram.F32(c + 0x22);
          v.z = ram.F32(c + 0x26);
          if (c == player && (!std::isfinite(v.x) || !std::isfinite(v.z) || (v.x == 0 && v.z == 0)))
            return false;
          SetAngles(v, -ram.S16(c + 0x2C) * turn, ram.S16(c + 0x2A) * turn, ram.S16(c + 0x2E) * turn);
          v.speed = ram.U16(c + 0x14) / 3.6f;
          int lapsDone = ram.S16(c + 0x70);
          v.lap = std::max(lapsDone, 0) + 1;
          v.progress = lapsDone + std::min(int(ram.U16(c + 0xA6)), segments - 1) / float(segments);
          v.position = ram.U8(c + 0x123) + 1;
          for (int w = 0; w < 4; w++)
            v.wheels[w].surface = Surface(ram.U16(c + 0x142 + 2 * w));
          if (c == player)
          {
            v.gear = ram.U16(c + 0x156) + 1;
            v.gears = 7;
            v.rpm = ram.U16(c + 0x15A);
            v.rpm_max = 14000;
            out.player = v;
          }
          else
            out.others.push_back(v);
        }
        return true;
      }

    private:
      struct Course
      {
        int segments;
        const char *name, *difficulty;
        float length;
      };
      static constexpr Course kCourses[] = { { 338, "Big Forest", "beginner", 3500 },
                                             { 418, "Bay Bridge", "medium", 3350 },
                                             { 592, "Acropolis", "expert", 4380 } };

      // 0x501122 through a whole game (race_rec.py): 0 at power-on and in the attract mode (its demonstration race
      // too), 1 the course select, 2 the race's start, 4 the pit start and the count down (8 for a frame after it),
      // 0x10 racing, 0x400 the line crossed on the last lap ("GOAL", the car driving on by itself), 0x800 then 0x1000
      // time up and the result ("GAME OVER"; 0x20 for a frame after it), 0x4000 the name entry
      static State DecodeState(uint16_t w)
      {
        switch (w)
        {
        case 0x0000: return State::Attract;
        case 0x0001: return State::Select;
        case 0x0002:
        case 0x0004:
        case 0x0008:
        case 0x0010: return State::Race;
        case 0x0020:
        case 0x0400:
        case 0x0800:
        case 0x1000:
        case 0x4000: return State::Over;
        default: return State::Unknown;
        }
      }

      static const char *Surface(uint16_t w)
      {
        switch (w)
        {
        case 0x0002: return "asphalt";
        case 0x0004: return "grass";
        case 0x0100: return "runoff";
        default: return "";
        }
      }

      // A car's right, up and forward from its heading, pitch and roll (which SetOrientation reads back)
      static void SetAngles(Vehicle &v, float heading, float pitch, float roll)
      {
        float sh = std::sin(heading), ch = std::cos(heading), sp = std::sin(pitch), cp = std::cos(pitch);
        float sr = std::sin(roll), cr = std::cos(roll);
        float forward[3] = { sh * cp, sp, ch * cp };
        float right0[3] = { ch, 0, -sh }, up0[3] = { -sh * sp, cp, -ch * sp };
        float right[3], up[3];
        for (int i = 0; i < 3; i++)
        {
          right[i] = cr * right0[i] - sr * up0[i];
          up[i] = sr * right0[i] + cr * up0[i];
        }
        SetOrientation(v, right, up, forward);
      }

      const char *m_game;
    };
  }

  std::unique_ptr<IDecoder> MakeModel1Decoder(const Game &game)
  {
    if (game.name == "vr" || game.name == "vformula")
      return std::make_unique<CVirtuaRacing>(game.name == "vr" ? "vr" : "vformula");
    return nullptr;
  }
}
