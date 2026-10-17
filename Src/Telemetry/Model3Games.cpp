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

    /*
     * Le Mans 24 (lemans24). Its unit: 9.26 a metre of its speedometer (the speed from the positions is then the one
     * the game shows). The player's car, in a block set once the race runs (cleared one frame in 256, garbage in a
     * few others): 0x1FDBB0/B4/B8 its angles (radians, 0..2 pi: the nose going down, pi less the heading, leaning
     * left; the roll's sign from the body leaning out of the bends), 0x1FDBC0/C4/C8 x, y, z (floats); else x and z
     * at 0x17E610/18, or the middle of its four points on the ground, x, y and z floats every 12 bytes from 0x190124.
     * 0x195474 and 0x195478 sin and cos of its heading (one frame behind the block), 0x195564 its speed (km/h,
     * float), 0x195488 u32 its track segment (2098 a lap of about 5932 m, the line at about 2082; numbered otherwise
     * in the pit lane), 0x1902B0 u32 the lap's percentage (from 1.24 % before the line; right in the pit lane too).
     * The dashboard: 0x1AD5DC u16 rpm, 0x1AD5E1 u8 the gear less one (the automatic gearbox's 6). The race: 0x17FD70
     * the time left (s, float), 0x17E509 u8 the laps done, 0x1799D4 u16 the position (1 leading) of 0x1799D6's cars
     * (40), 0x17E0D4 the lap time and 0x181E8C the race time (floats, ms of the game's clocks, 1000/60 a frame),
     * 0x17FD28 the 24 hours' clock (hours, float; not sent). No last or best lap kept: from the lap time's resets.
     * The other cars: entries of 0x1EC bytes from 0x17CED4 (x at +0, z at +8, the track segment u32 at +0x18; 6 on
     * the track, already spread around it as the player leaves the pits), their heading's sin and cos at +0x20/+0x24
     * of entries of 0x21C bytes from 0x1858D8 (with the next points of their path). No laps nor positions found for
     * them (the game ranks them from the player's): their progress is the player's, plus the shorter way round to
     * their segment. Its world is not mirrored.
     */
    class CLeMans24 : public IDecoder
    {
    public:
      bool Decode(CModel3 &model3, uint64_t frame, Frame &out) override
      {
        CRam ram(model3);
        const float unit = 1.0f / 9.26f;
        const float pi = 3.14159265f;
        out.game = "lemans24";
        out.frame = frame;
        out.lapLength = kLapLength;
        // Racing: the time left counting down
        out.time_left = ram.F32(0x17FD70);
        if (std::isfinite(out.time_left) && out.time_left != m_lastTimeLeft)
        {
          if (out.time_left < m_lastTimeLeft && m_lastTimeLeft - out.time_left < 0.5f)
            m_lastCount = frame;
          m_lastTimeLeft = out.time_left;
        }
        bool racing = frame - m_lastCount < 8;
        out.racing = racing;
        out.state = racing ? State::Race : State::Unknown;
        // The clocks; the last and best laps from the lap time going back to 0 while the race time goes on
        float lapTime = ram.F32(0x17E0D4) / 1000, raceTime = ram.F32(0x181E8C) / 1000;
        if (Clock(raceTime) && Clock(m_raceTime) && raceTime < m_raceTime - 1)
          m_lastLap = m_bestLap = NAN;
        else if (Clock(lapTime) && Clock(m_lapTime) && lapTime < m_lapTime - 1 && m_lapTime > 10)
        {
          // The frames since the last read went to the lap done, less what the new lap has counted (the lap time
          // reads 0 on the frame the line is crossed: the game shows the lap one frame longer than its last read)
          float since = frame > m_clockFrame && frame - m_clockFrame <= 2 ? float(frame - m_clockFrame) / 60 : 0;
          m_lastLap = m_lapTime + std::max(since - lapTime, 0.0f);
          m_bestLap = std::isnan(m_bestLap) ? m_lastLap : std::min(m_bestLap, m_lastLap);
        }
        m_lapTime = Clock(lapTime) ? lapTime : NAN;
        m_raceTime = Clock(raceTime) ? raceTime : NAN;
        m_clockFrame = frame;
        out.lap_time = m_lapTime;
        out.race_time = m_raceTime;
        out.last_lap = m_lastLap;
        out.best_lap = m_bestLap;
        Vehicle v;
        float a = ram.F32(0x1FDBB0), b = ram.F32(0x1FDBB4), c = ram.F32(0x1FDBB8);
        float x = ram.F32(0x1FDBC0), y = ram.F32(0x1FDBC4), z = ram.F32(0x1FDBC8);
        if (Angle(a) && Angle(b) && Angle(c) && Place(x) && Place(z))
        {
          m_x = x;
          m_y = y;
          m_z = z;
          m_heading = Wrap(pi - b);
          m_pitch = -Wrap(a);
          m_roll = -Wrap(c);
          m_blockFrame = frame;
        }
        else if (frame - m_blockFrame > 8)
        {
          // No block for a while (before the race): the copy at 0x17E610 (whose height is not the car's, 2 m above
          // it), else the four points on the ground; the heading alone
          m_x = ram.F32(0x17E610);
          m_z = ram.F32(0x17E618);
          m_y = ram.F32(0x190134);
          if (!Place(m_x) || !Place(m_z))
          {
            m_x = m_z = 0;
            for (int j = 0; j < 4; j++)
            {
              m_x += ram.F32(0x190124 + 12 * j) / 4;
              m_z += ram.F32(0x190124 + 12 * j + 8) / 4;
            }
          }
          m_heading = std::atan2(ram.F32(0x195474), ram.F32(0x195478));
          m_pitch = m_roll = 0;
        }
        // (else the block held: the derived values take it for a frame the game did not move the car)
        v.x = m_x * unit;
        v.y = m_y * unit;
        v.z = m_z * unit;
        if (!std::isfinite(v.x) || !std::isfinite(v.z) || (v.x == 0 && v.z == 0))
          return false;
        SetAngles(v, m_heading, m_pitch, m_roll);
        v.speed = ram.F32(0x195564) / 3.6f;
        v.gear = ram.U8(0x1AD5E1) + 1;
        v.gears = 6;
        v.rpm = ram.U16(0x1AD5DC);
        v.rpm_max = 10000;
        int cars = ram.U16(0x1799D6), position = ram.U16(0x1799D4);
        v.position = position >= 1 && position <= cars ? position : 0;
        int lapsDone = ram.U8(0x17E509);
        v.lap = lapsDone + 1;
        // Along the lap: the segment, or in the pit lane (where it is numbered otherwise) the percentage
        float along = Along(ram.U32(0x195488));
        uint32_t percent = ram.U32(0x1902B0);
        if (percent < 100)
        {
          float p = std::fmod(percent + 1.24f, 100.0f) / 100;
          if (std::fabs(Half(along - p)) > 0.05f)
            along = p;
        }
        // The laps counted a few segments away from the line: the nearer to the last frame's
        float progress = lapsDone + along;
        if (frame - m_progressFrame <= 2)
        {
          float d = progress - m_progress;
          if (d > 0.5f && d < 1.5f)
            progress -= 1;
          else if (d < -0.5f && d > -1.5f)
            progress += 1;
        }
        m_progress = progress;
        m_progressFrame = frame;
        v.progress = progress;
        out.player = v;
        out.others.clear();
        float up[3] = { 0, 1, 0 };
        for (int k = 0; k < 6; k++)
        {
          uint32_t e = 0x17CED4 + k * 0x1EC;
          Vehicle o;
          float ox = ram.F32(e), oz = ram.F32(e + 8);
          if (!Place(ox) || !Place(oz))
          {
            o.x = o.y = o.z = NAN;
            o.progress = NAN;
            out.others.push_back(o);
            continue;
          }
          // (their entries hold no height)
          o.x = ox * unit;
          o.y = 0;
          o.z = oz * unit;
          uint32_t p = 0x1858D8 + k * 0x21C;
          float os = ram.F32(p + 0x20), oc = ram.F32(p + 0x24);
          float of[3] = { os, 0, oc }, orr[3] = { oc, 0, -os };
          SetOrientation(o, orr, up, of);
          o.progress = progress + Half(Along(ram.U32(e + 0x18)) - along);
          out.others.push_back(o);
        }
        return true;
      }

    private:
      static constexpr int kSegments = 2098, kLine = 2082;
      static constexpr float kLapLength = 5932;

      // The lap's part from the line at a track segment
      static float Along(uint32_t segment)
      {
        return float((int(segment % kSegments) - kLine + kSegments) % kSegments) / kSegments;
      }

      // A part of a lap the shorter way round: -0.5..0.5
      static float Half(float d)
      {
        return d - std::round(d);
      }

      static float Wrap(float t)
      {
        const float pi = 3.14159265f;
        return t > pi ? t - 2 * pi : t;
      }

      static bool Angle(float t) { return std::isfinite(t) && t > -0.1f && t < 6.4f; }
      static bool Place(float t) { return std::isfinite(t) && std::fabs(t) > 1e-20f && std::fabs(t) < 1e6f; }
      static bool Clock(float t) { return std::isfinite(t) && t >= 0 && t < 1e6f; }

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

      float m_lastTimeLeft = 0;
      uint64_t m_lastCount = ~0ull >> 1;
      float m_lapTime = NAN, m_raceTime = NAN, m_lastLap = NAN, m_bestLap = NAN;
      uint64_t m_clockFrame = 0;
      float m_x = 0, m_y = 0, m_z = 0, m_heading = 0, m_pitch = 0, m_roll = 0;
      uint64_t m_blockFrame = ~0ull >> 1;
      float m_progress = 0;
      uint64_t m_progressFrame = ~0ull >> 1;
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
    if (game.name == "lemans24")
      return std::make_unique<CLeMans24>();
    return nullptr;
  }
}
