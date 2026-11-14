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
 * Model2Games.cpp
 *
 * The Model 2 racing games' decoders (MakeModel2Decoder), from sm2-emu's
 * (Keisho; their author's, njz3): where each keeps its cars and its race in
 * the i960's RAMs (CModel2::WorkRAM at 0x500000, ScratchRAM at 0x200000, both
 * little-endian), found in sm2-emu's agent mode (its TELEMETRY.md tells how).
 * A clone reads as its parent unless it has its own addresses (the layouts
 * below, by the set's name).
 */

#include "Telemetry.h"

#include "Supermodel.h"
#include "Game.h"
#include "Model2/Model2.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace Telemetry
{
  namespace
  {
    const float kPi = 3.14159265f;

    // A turn of the games' 16-bit angles
    const float kAngle16 = 2 * kPi / 65536;

    // One of the i960's RAMs: its bytes and the address of the first, little-endian (0 outside it)
    class CRam
    {
    public:
      CRam(const std::vector<uint8_t> &bytes, uint32_t base)
        : m_bytes(bytes), m_base(base)
      {
      }

      // The RAM holds address to address + length
      bool Has(uint32_t address, uint32_t length) const
      {
        return address >= m_base && size_t(address - m_base) + length <= m_bytes.size();
      }

      uint8_t U8(uint32_t a) const { return Has(a, 1) ? m_bytes[a - m_base] : 0; }
      int8_t S8(uint32_t a) const { return int8_t(U8(a)); }
      uint16_t U16(uint32_t a) const { return uint16_t(U8(a) | (U8(a + 1) << 8)); }
      int16_t S16(uint32_t a) const { return int16_t(U16(a)); }
      uint32_t U32(uint32_t a) const { return uint32_t(U16(a)) | (uint32_t(U16(a + 2)) << 16); }
      int32_t S32(uint32_t a) const { return int32_t(U32(a)); }

      float F32(uint32_t a) const
      {
        uint32_t u = U32(a);
        float f;
        memcpy(&f, &u, 4);
        return f;
      }

    private:
      const std::vector<uint8_t> &m_bytes;
      uint32_t m_base;
    };

    CRam WorkRam(IEmulator &emulator)
    {
      return CRam(static_cast<CModel2 &>(emulator).WorkRAM(), 0x500000);
    }

    CRam ScratchRam(IEmulator &emulator)
    {
      return CRam(static_cast<CModel2 &>(emulator).ScratchRAM(), 0x200000);
    }

    // A 32-bit angle, 65536 a turn
    float Angle32(const CRam &ram, uint32_t a)
    {
      return float(ram.S32(a)) * kAngle16;
    }

    // An angle in -pi..pi
    float Wrap(float a)
    {
      a = std::fmod(a + kPi, 2 * kPi);
      return (a < 0 ? a + 2 * kPi : a) - kPi;
    }

    // A car's rotation from its heading, pitch and roll (+ turning right, + nose up, + leaning right), which it keeps
    // as the game gives them
    void SetAngles(Vehicle &v, float heading, float pitch, float roll)
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
      v.heading = heading;
      v.pitch = pitch;
      v.roll = roll;
    }

    /*
     * The player's controls as the game reads them: the analog channels its wheel (or handlebar), accelerator and
     * brake are on (CModel2::UpdateInputs), their range and their way. The steering's low end is the left: +1 left.
     */
    struct Control
    {
      int channel;              // -1: none
      uint8_t minimum, maximum;
      bool reverse;
    };

    struct Controls
    {
      Control steer, throttle, brake;
    };

    const Control kNone = { -1, 0, 0xFF, false };

    // Indy 500, Touring Car, Sega Rally, Daytona's I/O board but for its range: wheel, accelerator, brake on 0-2
    const Controls kWheelPedals = { { 0, 0, 0xFF, false }, { 1, 0, 0xFF, false }, { 2, 0, 0xFF, false } };
    // Daytona USA: the same, 0x20 to 0xE0
    const Controls kDaytonaControls = { { 0, 0x20, 0xE0, false }, { 1, 0x20, 0xE0, false }, { 2, 0x20, 0xE0, false } };
    // Super GT 24h, Over Rev: the pedals reversed
    const Controls kReversedPedals = { { 0, 0, 0xFF, false }, { 1, 0, 0xFF, true }, { 2, 0, 0xFF, true } };
    // The bikes (Manx TT, Motor Raid): throttle, brake, then the bank, reversed
    const Controls kBikeControls = { { 2, 0, 0xFF, true }, { 0, 0, 0xFF, false }, { 1, 0, 0xFF, false } };
    // Wave Runner: the handlebar on 0, the throttle (reversed) on 2, no brake
    const Controls kWaveRunnerControls = { { 0, 0, 0xFF, false }, { 2, 0, 0xFF, true }, kNone };
    // Desert Tank: wheel and accelerator; its channel 2 is a stick's (no brake)
    const Controls kDesertControls = { { 0, 0, 0xFF, false }, { 1, 0, 0xFF, false }, kNone };

    float ControlFraction(const CModel2 &model2, const Control &c)
    {
      if (c.channel < 0)
        return NAN;
      float span = float(std::max(1, c.maximum - c.minimum));
      float f = (float(model2.AnalogChannel(unsigned(c.channel))) - c.minimum) / span;
      return std::clamp(c.reverse ? 1.0f - f : f, 0.0f, 1.0f);
    }

    void SetControls(IEmulator &emulator, const Controls &controls, Vehicle &player)
    {
      const CModel2 &model2 = static_cast<CModel2 &>(emulator);
      player.throttle = ControlFraction(model2, controls.throttle);
      player.brake = ControlFraction(model2, controls.brake);
      float s = ControlFraction(model2, controls.steer);
      player.steer = std::isnan(s) ? NAN : 1.0f - 2.0f * s;
    }

    // What every decoder does last: the controls, and racing without a state of the game's own is the race
    void Finish(IEmulator &emulator, const Controls &controls, Frame &out)
    {
      SetControls(emulator, controls, out.player);
      if (out.state == State::Unknown && out.racing)
        out.state = State::Race;
    }

    /*
     * The speed over the ground of the cars whose own the game does not keep where it was found, from their positions
     * frame to frame, as sm2-emu has it: a car that did not move holds its last speed up to 2 frames (a game moving
     * its cars every other frame), a move faster than 150 m/s is a jump (a car put back on the track) and keeps the
     * last one, the first frame or one after a gap of 30 frames or more is 0.
     */
    class CGroundSpeeds
    {
    public:
      // Sets the speed of the cars `all` (else only the others), in out's order (the player first)
      void Fill(Frame &out, bool all = true)
      {
        const size_t n = 1 + out.others.size();
        if (m_last.size() != n)
          m_last.assign(n, Last());
        for (size_t i = 0; i < n; i++)
        {
          Vehicle &v = i == 0 ? out.player : out.others[i - 1];
          Last &l = m_last[i];
          bool still = l.valid && v.x == l.x && v.y == l.y && v.z == l.z && out.frame - l.frame <= 2;
          float speed = 0;
          if (l.valid && (out.frame == l.frame || still))
            speed = l.speed;
          else
          {
            if (l.valid && out.frame > l.frame && out.frame - l.frame < 30)
            {
              float dt = float(out.frame - l.frame) / float(kFps);
              float dx = (v.x - l.x) / dt, dy = (v.y - l.y) / dt, dz = (v.z - l.z) / dt;
              speed = std::sqrt(dx * dx + dy * dy + dz * dz) < 150.0f ? std::sqrt(dx * dx + dz * dz) : l.speed;
            }
            l.x = v.x; l.y = v.y; l.z = v.z;
            l.speed = speed;
            l.frame = out.frame;
            l.valid = true;
          }
          if (all || i > 0)
            v.speed = speed;
        }
      }

    private:
      struct Last
      {
        float x = 0, y = 0, z = 0, speed = 0;
        uint64_t frame = 0;
        bool valid = false;
      };
      std::vector<Last> m_last;
    };

    // The race's place of each car: one more for each car further along
    int PlaceOf(const Frame &out, const Vehicle &v)
    {
      int place = 1;
      if (&v != &out.player && out.player.progress > v.progress)
        place++;
      for (const Vehicle &o : out.others)
        if (&o != &v && o.progress > v.progress)
          place++;
      return place;
    }

    /*
     * Daytona USA (daytona), its work RAM. 40 entries of 0x300 bytes from 0x505100, the player's first, then the AI
     * cars: +98 the speedometer (u16, km/h), +9C/A0/A4 x, y, z (floats; a unit is about 1.11 m by the speedometer),
     * +A8 pitch and +AC roll (s16, 65536 a turn; + nose up, + right), +C8 heading (s32, 65536 a turn, negated), +EC
     * the place in the race (0 leading), +F0 laps done (s8: -1 on the grid, before the line), +1AC the progress along
     * the course (16.16, a segment every 8.3 units, 196 a lap of the beginner course), +1CC gear (0..3), +1D4 rpm
     * (u32). 0x5010A0 the game's state (1-3 attract, 17-21 coin and menus, 22 the race, 26 time up), 0x5010CC the
     * race time and 0x5010D0 the time left (frames, 64 to the clock's second), 0x5010E0 the player's laps done,
     * 0x5010EC the race's laps, 0x502A00 the player's lap times (u32 each, frames). The AI cars' speeds from their
     * moves. daytona93 keeps the player's entry at 0x504D80, from +1A8 on 4 bytes lower; its AI cars, place and laps
     * not found (the laps done are 0x5010E0's).
     */
    struct DaytonaLayout
    {
      uint32_t cars;
      int count;                // 1: the AI cars not found
      uint32_t progress, gear, rpm;
    };

    const DaytonaLayout kDaytona = { 0x505100, 40, 0x1AC, 0x1CC, 0x1D4 };
    const DaytonaLayout kDaytona93 = { 0x504D80, 1, 0x1A8, 0x1C8, 0x1D0 };

    class CDaytona : public IDecoder
    {
    public:
      CDaytona(const DaytonaLayout &layout)
        : m_l(layout)
      {
      }

      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const DaytonaLayout &l = m_l;
        const uint32_t carSize = 0x300, lapsDone = 0x5010E0, lapTimes = 0x502A00;
        const float clock = 64, segments = 196, lapLength = 1569.4f;
        if (!ram.Has(l.cars, carSize * l.count))
          return false;
        out.frame = frame;
        out.game = "daytona";
        out.others.resize(size_t(l.count - 1));
        for (int k = 0; k < l.count; k++)
        {
          uint32_t base = l.cars + k * carSize;
          Vehicle &v = k == 0 ? out.player : out.others[k - 1];
          v.x = ram.F32(base + 0x9C);
          v.y = ram.F32(base + 0xA0);
          v.z = ram.F32(base + 0xA4);
          SetAngles(v, Wrap(-Angle32(ram, base + 0xC8)), ram.S16(base + 0xA8) * kAngle16,
                    ram.S16(base + 0xAC) * kAngle16);
          int done = l.count > 1 ? ram.S8(base + 0xF0) : ram.S8(lapsDone);
          v.lap = done + 1;
          v.progress = done + float(ram.S32(base + l.progress)) / 65536.0f / segments;
          v.position = l.count > 1 ? ram.U8(base + 0xEC) + 1 : kUnknown;
        }
        Vehicle &p = out.player;
        p.gear = ram.U8(l.cars + l.gear) + 1;
        p.gears = 4;
        p.rpm = float(ram.U32(l.cars + l.rpm));
        p.rpm_max = 8000;     // the dial's red zone

        out.laps = ram.U8(0x5010EC);
        out.lapLength = lapLength;
        out.state = StateOf(ram.U8(0x5010A0));
        out.finished = out.laps > 0 && p.lap > out.laps;
        out.racing = out.state == State::Race && p.lap >= 1 && !out.finished;
        uint32_t race = ram.U32(0x5010CC);
        out.time_left = ram.U16(0x5010D0) / clock;
        out.race_time = race / clock;
        // The laps done are in the table; the current one is the rest of the race
        int done = std::clamp<int>(ram.S8(lapsDone), 0, 16);
        uint32_t sum = 0, best = 0;
        for (int i = 0; i < done; i++)
        {
          uint32_t t = ram.U32(lapTimes + 4 * i);
          sum += t;
          best = best == 0 ? t : std::min(best, t);
        }
        if (done > 0)
        {
          out.last_lap = ram.U32(lapTimes + 4 * (done - 1)) / clock;
          out.best_lap = best / clock;
        }
        if (race >= sum)
          out.lap_time = (race - sum) / clock;
        m_speeds.Fill(out, false);
        p.speed = ram.U16(l.cars + 0x98) / 3.6f;
        Finish(emulator, kDaytonaControls, out);
        return true;
      }

    private:
      static State StateOf(uint8_t s)
      {
        if (s >= 1 && s <= 3)
          return State::Attract;
        if (s >= 17 && s <= 21)
          return State::Select;
        if (s == 22)
          return State::Race;
        if (s == 26)
          return State::Over;
        return State::Unknown;
      }

      DaytonaLayout m_l;
      CGroundSpeeds m_speeds;
    };

    /*
     * Sega Rally Championship (srallyc). Its work RAM: 16 entries of 0x100 bytes from 0x500000, the player's, then
     * seven slots for the AI cars near the player: +14/18/1C x, y, z (floats, about metres). The player's: +38 pitch,
     * +3C heading (atan2(-dx, dz)), +40 roll (floats, radians; + nose up, + right), +78 gear (u32, 1..4), +7C rpm
     * (float), +80 speed (float, x 64.875 km/h), +84 progress (16.16, a segment every 4.8 units). An AI car's: +A4
     * heading (float, radians, atan2(dz, dx)), +88 progress (16.16, a segment every 10.3 units; 300 a lap of the
     * Desert). Its scratch RAM: 0x202098 the game's mode (2 attract, 3 from the coin to the game over), 0x2020B8 the
     * total time and 0x20B0B0 the time left (u16, 1/60 s). srallycc keeps its time left at 0x20B0D0; srallycdxa its
     * mode at 0x202088, its total time at 0x2020A4, its time left at 0x20C990. The AI cars' speeds from their moves;
     * the race's position not found.
     */
    struct SegaRallyLayout
    {
      uint32_t mode, totalTime, timeLeft;
    };

    const SegaRallyLayout kSegaRally = { 0x202098, 0x2020B8, 0x20B0B0 };
    const SegaRallyLayout kSegaRallyCC = { 0x202098, 0x2020B8, 0x20B0D0 };
    const SegaRallyLayout kSegaRallyDXA = { 0x202088, 0x2020A4, 0x20C990 };

    class CSegaRally : public IDecoder
    {
    public:
      CSegaRally(const SegaRallyLayout &layout)
        : m_l(layout)
      {
      }

      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const uint32_t entrySize = 0x100;
        const int cars = 8;
        if (!ram.Has(0x500000, entrySize * cars))
          return false;
        out.frame = frame;
        out.game = "srallyc";
        out.others.resize(cars - 1);
        for (int k = 0; k < cars; k++)
        {
          uint32_t base = 0x500000 + k * entrySize;
          Vehicle &v = k == 0 ? out.player : out.others[k - 1];
          v.x = ram.F32(base + 0x14);
          v.y = ram.F32(base + 0x18);
          v.z = ram.F32(base + 0x1C);
          if (k == 0)
          {
            SetAngles(v, -ram.F32(base + 0x3C), ram.F32(base + 0x38), ram.F32(base + 0x40));
            v.progress = ram.U32(base + 0x84) / 65536.0f;
          }
          else
          {
            float a = ram.F32(base + 0xA4);     // atan2(dz, dx)
            SetAngles(v, std::atan2(std::cos(a), std::sin(a)), 0, 0);
            v.progress = ram.U32(base + 0x88) / 65536.0f;
          }
          v.lap = 1;
        }
        Vehicle &p = out.player;
        p.gear = int(ram.U32(0x500078));
        p.gears = 4;
        p.rpm = ram.F32(0x50007C);
        p.rpm_max = 8200;     // the dial's red zone

        // The clocks and the game's mode, in the scratch RAM
        CRam scratch = ScratchRam(emulator);
        const SegaRallyLayout &l = m_l;
        if (scratch.Has(l.timeLeft, 2))
        {
          uint8_t mode = scratch.U8(l.mode);
          uint16_t total = scratch.U16(l.totalTime), left = scratch.U16(l.timeLeft);
          out.time_left = left / 60.0f;
          out.race_time = total / 60.0f;
          out.lap_time = out.race_time;
          if (mode == 2)
            out.state = State::Attract;
          else if (mode == 3)
            out.state = total == 0 ? State::Select : left == 0 ? State::Over : State::Race;
        }
        out.racing = out.state == State::Race;
        m_speeds.Fill(out, false);
        p.speed = ram.F32(0x500080) * 64.875f / 3.6f;
        Finish(emulator, kWheelPedals, out);
        return true;
      }

    private:
      SegaRallyLayout m_l;
      CGroundSpeeds m_speeds;
    };

    /*
     * Indy 500 (indy500), its work RAM. 33 cars of 0x1E4 bytes from 0x53C7AC, the player first (28 race on Bay Side
     * Street and Highland Raceway): +000/004/008 x, y, z (floats, metres), +00C roll (+ leaning right: negative in
     * the oval's banked left turns), +010 heading (negated), +014 pitch (+ nose up), i32, 65536 a turn; +030 speed
     * (float, metres a frame), +0E4 progress (lap << 20 | sector << 16 | the sector's fraction; lap 0 the grid and the
     * rolling start, laps + 1 done). 0x53C332 the time left (u16, frames), 0x53C350 the course (u8: 0 Bay Side
     * Street, 1 the oval, 2 Highland Raceway), 0x53C5B9 the gearbox (u8: 0 manual, 6 speeds; 1 automatic), 0x53C5BC
     * the player's gear (u8, 0 first), 0x53C700 its rpm (float), 0x5441E4 its time in the lap (u32, frames), 0x5449A0
     * its camera (3x3 floats, world from camera: a chase camera, late in the turns). indy500to keeps all this 0x10 to
     * 0x30 bytes lower, its gearbox four bytes before the gear; indy500d elsewhere, its cars 8 bytes longer (the angles
     * at +014, the progress at +0E8), its lap time not found. No frame outside the race (before the grid).
     */
    struct IndyCourse
    {
      const char *name;
      int laps, cars, sectors;
      float start[8];           // where each sector starts along the lap (a fraction of its length)
      float length;             // m, the autopilot's map of the racing line
    };

    const IndyCourse kIndyCourses[] =
    {
      { "Bay Side Street", 3, 28, 8, { 0.0f, 0.131f, 0.264f, 0.396f, 0.528f, 0.661f, 0.795f, 0.927f }, 4909.6f },
      { "Indy 500", 4, 33, 7, { 0.0f, 0.16f, 0.321f, 0.482f, 0.643f, 0.802f, 0.963f }, 4052.6f },
      { "Highland Raceway", 3, 28, 8, { 0.0f, 0.141f, 0.281f, 0.423f, 0.564f, 0.704f, 0.843f, 0.985f }, 4609.6f },
    };

    struct IndyLayout
    {
      uint32_t cars, carSize, angles, progress;
      uint32_t timeLeft, course, transmission, gear, rpm, camera;
      uint32_t lapTime;         // 0: not found
    };

    const IndyLayout kIndy500 = { 0x53C7AC, 0x1E4, 0x00C, 0x0E4,
      0x53C332, 0x53C350, 0x53C5B9, 0x53C5BC, 0x53C700, 0x5449A0, 0x5441E4 };
    const IndyLayout kIndy500To = { 0x53C77C, 0x1E4, 0x00C, 0x0E4,
      0x53C322, 0x53C340, 0x53C594, 0x53C598, 0x53C6E0, 0x544980, 0x5441C4 };
    const IndyLayout kIndy500D = { 0x54C714, 0x1EC, 0x014, 0x0E8,
      0x54C2D2, 0x54C2F0, 0x54C4A0, 0x54C4A4, 0x54C600, 0x554C80, 0 };

    class CIndy500 : public IDecoder
    {
    public:
      CIndy500(const IndyLayout &layout)
        : m_l(layout)
      {
      }

      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const IndyLayout &l = m_l;
        const float fps = 57.5f;
        if (!ram.Has(l.cars, 33 * l.carSize))
          return false;
        uint8_t courseId = ram.U8(l.course);
        const IndyCourse *course = courseId < 3 ? &kIndyCourses[courseId] : nullptr;
        out.frame = frame;
        out.game = "indy500";
        out.track = course ? course->name : "";
        out.laps = course ? course->laps : 0;
        out.lapLength = course ? course->length : NAN;
        out.transmission = ram.U8(l.transmission) == 0 ? "MT" : "AT";
        int cars = course ? course->cars : 33;
        out.others.resize(size_t(cars - 1));

        int maxSector = 0;
        for (int k = 0; k < cars; k++)
        {
          uint32_t base = l.cars + k * l.carSize;
          Vehicle &v = k == 0 ? out.player : out.others[k - 1];
          v.x = ram.F32(base + 0x000);
          v.y = ram.F32(base + 0x004);
          v.z = ram.F32(base + 0x008);
          SetAngles(v, Wrap(-Angle32(ram, base + l.angles + 4)), Angle32(ram, base + l.angles + 8),
                    Angle32(ram, base + l.angles));
          v.speed = ram.F32(base + 0x030) * fps;
          uint32_t e4 = ram.U32(base + l.progress);
          v.lap = int(e4 >> 20);
          v.sector = int((e4 >> 16) & 15);
          maxSector = std::max(maxSector, v.sector);
          v.progress = (e4 & 0xFFFF) / 65536.0f;     // the sector's fraction, for now
        }
        m_sectors = std::max(m_sectors, maxSector + 1);
        out.sectors = course ? course->sectors : m_sectors;
        for (int k = 0; k < cars; k++)
        {
          Vehicle &v = k == 0 ? out.player : out.others[k - 1];
          float f = v.progress, inLap;
          if (course && v.sector < course->sectors)
          {
            // The sectors' lengths differ: the lap's fraction by distance
            float a = course->start[v.sector];
            float b = v.sector + 1 < course->sectors ? course->start[v.sector + 1] : 1.0f;
            inLap = a + f * (b - a);
          }
          else
            inLap = (v.sector + f) / m_sectors;
          v.progress = (v.lap - 1) + inLap;
        }

        // The player: the camera, gear, rpm, clocks
        Vehicle &p = out.player;
        for (int i = 0; i < 9; i++)
          out.camera[i] = ram.F32(l.camera + 4 * i);
        p.gear = ram.U8(l.gear) + 1;
        p.gears = out.transmission == "MT" ? 6 : kUnknown;
        p.rpm = ram.F32(l.rpm);
        out.time_left = ram.U16(l.timeLeft) / fps;
        if (l.lapTime != 0)
          out.lap_time = ram.U32(l.lapTime) / fps;
        out.finished = out.laps > 0 && p.lap > out.laps;
        out.racing = p.lap >= 1 && !out.finished;
        p.position = PlaceOf(out, p);
        Finish(emulator, kWheelPedals, out);
        return p.lap >= 1;
      }

    private:
      IndyLayout m_l;
      int m_sectors = 1;        // on a course not in the table: the highest sector seen, plus one
    };

    /*
     * Sega Touring Car Championship (stcc), its scratch RAM (Model 2C). 8 cars of 0x58 bytes from 0x20A900, the
     * player first: +00/04/08 x, y, z, +0C/10/14 velocity (floats, units a frame), +1C heading (float, radians,
     * -atan2(dx, dz)), +50 the track segment the car is on (u16, 0 at the line), +52 the segments driven since the
     * line (s16, negative on the grid). 0x2022D0 the lap time (float, seconds as shown), 0x2022EC the time left (s32,
     * frames, 64 to the clock's second; below 0 at time up), 0x20462C the gear (u8, 1..6, 0 standing), 0x208914
     * probably the rpm (float, up to about 10000). Country Circuit has 300 segments a lap; on the other courses the
     * count is the highest segment seen, plus one. stcca, stccb and stcco keep the cars, the gear and the rpm 0x50 to
     * 0xD0 bytes lower, stcco its clocks 0x10 higher. No pitch nor roll found.
     */
    struct StccLayout
    {
      uint32_t cars, lapTime, timeLeft, gear, rpm;
    };

    const StccLayout kStcc = { 0x20A900, 0x2022D0, 0x2022EC, 0x20462C, 0x208914 };
    const StccLayout kStccAB = { 0x20A830, 0x2022D0, 0x2022EC, 0x2045BC, 0x208864 };
    const StccLayout kStccO = { 0x20A830, 0x2022E0, 0x2022FC, 0x2045BC, 0x208864 };

    class CTouringCar : public IDecoder
    {
    public:
      CTouringCar(const StccLayout &layout)
        : m_l(layout)
      {
      }

      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = ScratchRam(emulator);
        const StccLayout &l = m_l;
        const uint32_t carSize = 0x58;
        const int cars = 8;
        const float fps = 57.5f;
        if (!ram.Has(l.cars, carSize * cars))
          return false;
        out.frame = frame;
        out.game = "stcc";
        out.others.resize(cars - 1);
        for (int k = 0; k < cars; k++)
          m_segments = std::max(m_segments, int(ram.U16(l.cars + k * carSize + 0x50)) + 1);
        for (int k = 0; k < cars; k++)
        {
          uint32_t base = l.cars + k * carSize;
          Vehicle &v = k == 0 ? out.player : out.others[k - 1];
          v.x = ram.F32(base + 0x00);
          v.y = ram.F32(base + 0x04);
          v.z = ram.F32(base + 0x08);
          float vx = ram.F32(base + 0x0C), vy = ram.F32(base + 0x10), vz = ram.F32(base + 0x14);
          v.speed = std::sqrt(vx * vx + vy * vy + vz * vz) * fps;
          SetAngles(v, -ram.F32(base + 0x1C), 0, 0);
          int driven = ram.S16(base + 0x52);
          v.sector = ram.U16(base + 0x50);
          v.progress = float(driven) / m_segments;
          v.lap = int(std::floor(v.progress)) + 1;
        }
        out.sectors = m_segments;
        out.time_left = std::max<int32_t>(ram.S32(l.timeLeft), 0) / 64.0f;
        out.racing = out.time_left > 0;
        out.lapLength = 2047;     // Country Circuit, in units (the AI cars' whole laps)
        out.lap_time = ram.F32(l.lapTime);
        Vehicle &p = out.player;
        p.gear = ram.U8(l.gear);
        p.gears = 6;
        p.rpm = ram.F32(l.rpm);
        p.position = PlaceOf(out, p);
        Finish(emulator, kWheelPedals, out);
        return true;
      }

    private:
      StccLayout m_l;
      int m_segments = 300;     // segments a lap: Country Circuit's, or the highest seen plus one
    };

    /*
     * Over Rev (overrev), its work RAM; distances in half-metres, 60 frames a second on its clocks. 7 AI cars of
     * 0x2A8 bytes from 0x59E6A8: +050 laps done (u8), +0A4 heading (s32, 65536 a turn, pi/2 - atan2(dx, dz)), +0E0
     * speed (float, half-metres a frame), +100/104/108 x, y, z (floats, y up), +1F0 the distance from the start line
     * (float, negative on the grid). The player: 0x590DE4 its roll, negated (+ leaning right: out of a turn), its pitch
     * at +10 (+ nose up), its heading at +14, negated (s32, 65536 a turn); 0x595A5C its gear (u8, 1 first), 0x595A7C
     * its rpm (float), 0x595AA4 its distance into the lap (float), 0x595AB8 its speed (float, m/s), 0x595C04 its x, y,
     * z (floats), 0x595C9C its laps from the start line (s32, -1 on the grid, more than half a lap behind it). The
     * race: 0x595CAC the race time and 0x595CB0 the lap time (u16, frames), 0x595CE0 the seconds left (u8), 0x595D18
     * the race's laps (u8), 0x51D33C the lap's length (float). The game keeps no place but the one its display slides
     * to: counted from the progress, which gives the same. No frame in the attract mode and the menus.
     */
    class COverRev : public IDecoder
    {
    public:
      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const uint32_t cars = 0x59E6A8, carSize = 0x2A8, angles = 0x590DE4;
        const int aiCount = 7;
        const float metre = 0.5f;
        if (!ram.Has(cars, carSize * aiCount))
          return false;
        int raceTime = ram.U16(0x595CAC);
        int timeLeft = ram.U8(0x595CE0);
        if (raceTime == 0 && timeLeft == 0)
          return false;       // the attract mode and the menus
        float lap = ram.F32(0x51D33C);
        if (!(lap > 500.0f && lap < 50000.0f))
          lap = 2963.8f;      // round 1's course
        out.frame = frame;
        out.game = "overrev";
        out.laps = ram.U8(0x595D18);
        out.lapLength = lap * metre;
        out.race_time = raceTime / 60.0f;
        out.others.resize(aiCount);

        Vehicle &p = out.player;
        p.x = ram.F32(0x595C04) * metre;
        p.y = ram.F32(0x595C08) * metre;
        p.z = ram.F32(0x595C0C) * metre;
        SetAngles(p, Wrap(-Angle32(ram, angles + 0x14)), Wrap(Angle32(ram, angles + 0x10)),
                  Wrap(-Angle32(ram, angles)));
        p.speed = ram.F32(0x595AB8);
        p.gear = ram.U8(0x595A5C);
        p.rpm = ram.F32(0x595A7C);
        int32_t lapsDone = ram.S32(0x595C9C);
        p.progress = lapsDone + ram.F32(0x595AA4) / lap;
        p.lap = (lapsDone < 0 ? 0 : lapsDone) + 1;

        for (int k = 0; k < aiCount; k++)
        {
          uint32_t base = cars + k * carSize;
          Vehicle &v = out.others[k];
          v.x = ram.F32(base + 0x100) * metre;
          v.y = ram.F32(base + 0x104) * metre;
          v.z = ram.F32(base + 0x108) * metre;
          SetAngles(v, Wrap(kPi / 2 - Angle32(ram, base + 0xA4)), 0, 0);
          v.speed = ram.F32(base + 0xE0) * metre * 60.0f;
          v.progress = ram.F32(base + 0x1F0) / lap;
          v.lap = ram.U8(base + 0x50) + 1;
        }
        // The places from the progress, as the game's display has them
        p.position = PlaceOf(out, p);
        for (Vehicle &v : out.others)
          v.position = PlaceOf(out, v);

        out.time_left = float(timeLeft);
        out.lap_time = ram.U16(0x595CB0) / 60.0f;
        out.finished = out.laps > 0 && p.lap > out.laps;
        out.racing = raceTime > 0 && timeLeft > 0 && !out.finished;
        Finish(emulator, kReversedPedals, out);
        return true;
      }
    };

    /*
     * Super GT 24h (sgt24h), its work RAM; metres, 60 frames a second on its clocks. 14 cars of 0x1728 bytes from
     * 0x518424, the player last; from a car's base: -0C its place in the race (u8, 0 leading), -04 laps done (u8),
     * +00 the distance along the course (float, 3000 a lap; the grid is just past the line), +0C/10/14 x, y, z
     * (floats, y up), +34 heading (s32, 65536 a turn, -atan2(dx, dz)), +84C speed (float, metres a frame); the
     * player's only: +C8 gear (u8, 0 first), +220 rpm (u16), +764 its orientation (3x3 floats, world from car,
     * row-major; columns right, up, forward). 0x510948 the game's state (u32: 1 attract, 7 coin, 2 car select, 3 the
     * race and its countdown, 4 game over), 0x510F2C the lap time (u16, frames), 0x510F6C the race's laps (u8),
     * 0x510F74 the time left (s16, frames). Outside the race and the game over, the state alone (the cars not set up).
     */
    class CSuperGT24h : public IDecoder
    {
    public:
      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const uint32_t cars = 0x518424, carSize = 0x1728;
        const int count = 14;
        const uint32_t race = 3, over = 4;
        if (!ram.Has(cars, carSize * count))
          return false;
        uint32_t state = ram.U32(0x510948);
        out.frame = frame;
        out.game = "sgt24h";
        switch (state)
        {
        case 1: out.state = State::Attract; break;
        case 2: case 7: out.state = State::Select; break;
        case race: out.state = State::Race; break;
        case over: out.state = State::Over; break;
        default: break;
        }
        if (state != race && state != over)
        {
          Finish(emulator, kReversedPedals, out);
          return true;
        }
        out.lapLength = 3000;
        out.laps = ram.U8(0x510F6C);
        out.others.resize(count - 1);
        for (int k = 0; k < count; k++)
        {
          uint32_t base = cars + k * carSize;
          Vehicle &v = k == count - 1 ? out.player : out.others[k];
          v.x = ram.F32(base + 0x0C);
          v.y = ram.F32(base + 0x10);
          v.z = ram.F32(base + 0x14);
          SetAngles(v, Wrap(-Angle32(ram, base + 0x34)), 0, 0);
          v.speed = ram.F32(base + 0x84C) * 60.0f;
          v.progress = ram.F32(base) / 3000.0f;
          v.lap = ram.U8(base - 0x04) + 1;
          v.position = ram.U8(base - 0x0C) + 1;
        }

        // The player: its orientation (the matrix's columns), gear, rpm
        Vehicle &p = out.player;
        uint32_t base = cars + (count - 1) * carSize;
        float m[9], right[3], up[3], forward[3];
        for (int i = 0; i < 9; i++)
          m[i] = ram.F32(base + 0x764 + 4 * i);
        for (int r = 0; r < 3; r++)
        {
          right[r] = m[r * 3 + 0];
          up[r] = m[r * 3 + 1];
          forward[r] = m[r * 3 + 2];
        }
        SetOrientation(p, right, up, forward);
        p.gear = ram.U8(base + 0xC8) + 1;
        p.rpm = ram.U16(base + 0x220);

        int lapTime = ram.U16(0x510F2C);
        out.time_left = std::max<int>(ram.S16(0x510F74), 0) / 60.0f;
        out.lap_time = lapTime / 60.0f;
        out.finished = out.laps > 0 && p.lap > out.laps;
        out.racing = state == race && lapTime > 0 && out.time_left > 0 && !out.finished;
        if (state == race && !out.racing && !out.finished)
          out.state = lapTime == 0 ? State::Select : State::Over;   // the countdown, or time up
        Finish(emulator, kReversedPedals, out);
        return true;
      }
    };

    /*
     * Motor Raid (motoraid), its work RAM; 60 frames a second on its clocks. 10 riders of 0x71C bytes from 0x57B8E4,
     * the player first: +00/04/08 x, y, z (floats, y up), +0C pitch (s32, 65536 a turn, + nose up), +28 heading (s32,
     * 65536 a turn, -atan2(dx, dz)), +4C speed (float, units a frame; x 216 the speedometer's km/h), +284 lean (s32,
     * 65536 a turn, negated: the bike leans into a turn, + right; about 40 degrees at most). 0x517BD0 the time left
     * (u16, frames; 1200 in the menus and the attract mode), 0x517CB0 the time since the start's countdown (u16,
     * frames; still once the race is over): racing while it runs.
     */
    class CMotorRaid : public IDecoder
    {
    public:
      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const uint32_t riders = 0x57B8E4, riderSize = 0x71C;
        const int count = 10;
        if (!ram.Has(riders, riderSize * count))
          return false;
        out.frame = frame;
        out.game = "motoraid";
        out.others.resize(count - 1);
        for (int k = 0; k < count; k++)
        {
          uint32_t base = riders + k * riderSize;
          Vehicle &v = k == 0 ? out.player : out.others[k - 1];
          v.x = ram.F32(base + 0x00);
          v.y = ram.F32(base + 0x04);
          v.z = ram.F32(base + 0x08);
          SetAngles(v, Wrap(-Angle32(ram, base + 0x28)), Wrap(Angle32(ram, base + 0x0C)),
                    Wrap(-Angle32(ram, base + 0x284)));
          v.speed = ram.F32(base + 0x4C) * 60.0f;
        }
        uint16_t left = ram.U16(0x517BD0), race = ram.U16(0x517CB0);
        out.time_left = left / 60.0f;
        out.race_time = race / 60.0f;
        if (race != m_raceTime)
        {
          m_raceTime = race;
          m_raceFrame = frame;
        }
        out.racing = left > 0 && race > 0 && frame - m_raceFrame < 4;
        out.state = out.racing ? State::Race : race > 0 && left == 0 ? State::Over : State::Unknown;
        Finish(emulator, kBikeControls, out);
        return true;
      }

    private:
      uint16_t m_raceTime = 0;
      uint64_t m_raceFrame = 0;
    };

    /*
     * Wave Runner (waverunr), its work RAM: the player's jet ski only, its attitude on the waves not found (level).
     * 0x51DF1C: +00/04/08 x, y, z (floats, y up: the water near 0), +30 heading (s32, 65536 a turn, -atan2(dx, dz)),
     * +48 speed (float, units a frame; x 155.7 the speedometer's km/h). 0x5AD874 the race time (u16, 60 a second; 0
     * before the race, still once it is over): racing while it runs.
     */
    class CWaveRunner : public IDecoder
    {
    public:
      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const uint32_t player = 0x51DF1C, raceTime = 0x5AD874;
        if (!ram.Has(raceTime, 2))
          return false;
        out.frame = frame;
        out.game = "waverunr";
        Vehicle &p = out.player;
        p.x = ram.F32(player + 0x00);
        p.y = ram.F32(player + 0x04);
        p.z = ram.F32(player + 0x08);
        SetAngles(p, Wrap(-Angle32(ram, player + 0x30)), 0, 0);
        p.speed = ram.F32(player + 0x48) * 155.7f / 3.6f;
        uint16_t race = ram.U16(raceTime);
        if (race != m_raceTime)
        {
          m_raceTime = race;
          m_raceFrame = frame;
        }
        out.race_time = race / 60.0f;
        out.racing = race > 0 && frame - m_raceFrame < 4;
        out.state = out.racing ? State::Race : race > 0 ? State::Over : State::Unknown;
        Finish(emulator, kWaveRunnerControls, out);
        return true;
      }

    private:
      uint16_t m_raceTime = 0;
      uint64_t m_raceFrame = 0;
    };

    /*
     * Desert Tank (desert), its work RAM: the player's tank, moved every other frame. 0x5003D4 its centre (x, y, z
     * floats, y up), 0x500404 its four corners (x, y, z floats each: rear right, rear left, front right, front left),
     * whose heading, pitch and roll are the tank's (the ground under them is nearly flat: the dunes are drawn, not
     * driven); 0x521040 the mission timer (u16, 32 to the second shown). Its speed from its moves.
     */
    class CDesertTank : public IDecoder
    {
    public:
      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const uint32_t centre = 0x5003D4, corners = 0x500404, timer = 0x521040;
        if (!ram.Has(timer, 2))
          return false;
        out.frame = frame;
        out.game = "desert";
        Vehicle &p = out.player;
        p.x = ram.F32(centre);
        p.y = ram.F32(centre + 4);
        p.z = ram.F32(centre + 8);
        float c[4][3];          // rear right, rear left, front right, front left
        for (int j = 0; j < 4; j++)
          for (int i = 0; i < 3; i++)
            c[j][i] = ram.F32(corners + 12 * j + 4 * i);
        float d[3], s[3];       // forward, and towards the left side
        for (int i = 0; i < 3; i++)
        {
          d[i] = (c[2][i] + c[3][i] - c[0][i] - c[1][i]) / 2;
          s[i] = (c[3][i] + c[1][i] - c[2][i] - c[0][i]) / 2;
        }
        float flat = std::hypot(d[0], d[2]);
        // The left side higher: leaning right
        SetAngles(p, std::atan2(d[0], d[2]), std::atan2(d[1], flat), std::atan2(s[1], std::hypot(s[0], s[2])));
        uint16_t t = ram.U16(timer);
        out.time_left = t / 32.0f;
        out.racing = t > 0 && flat > 0.5f;
        m_speeds.Fill(out);
        Finish(emulator, kDesertControls, out);
        return true;
      }

    private:
      CGroundSpeeds m_speeds;
    };

    /*
     * Manx TT Superbike (manxtt), its work RAM: the player's bike. 0x578A20: +00/04/08 x, y, z (floats, decimetres,
     * y up), +0C pitch, +10 heading (negated), +14 lean (floats, radians; + nose up, + leaning right: into a right
     * turn); 0x578B64 the gear (u8, 1 first), 0x5009D0 the speedometer (u16, km/h), 0x5334F0 the time left (u16,
     * about 56 to the second shown; still on the menus' screens, counting during a race): racing while it runs. The DX
     * set (manxttdx) keeps them elsewhere: no decoder.
     */
    class CManxTT : public IDecoder
    {
    public:
      bool Decode(IEmulator &emulator, uint64_t frame, Frame &out) override
      {
        CRam ram = WorkRam(emulator);
        const uint32_t bike = 0x578A20, gear = 0x578B64;
        const float metre = 0.1f;
        if (!ram.Has(gear, 1))
          return false;
        out.frame = frame;
        out.game = "manxtt";
        Vehicle &p = out.player;
        p.x = ram.F32(bike + 0x00) * metre;
        p.y = ram.F32(bike + 0x04) * metre;
        p.z = ram.F32(bike + 0x08) * metre;
        SetAngles(p, Wrap(-ram.F32(bike + 0x10)), Wrap(ram.F32(bike + 0x0C)), Wrap(ram.F32(bike + 0x14)));
        p.gear = ram.U8(gear);
        p.speed = ram.U16(0x5009D0) / 3.6f;
        uint16_t left = ram.U16(0x5334F0);
        if (left != m_timeLeft)
        {
          m_timeLeft = left;
          m_timeFrame = frame;
        }
        out.time_left = left / 56.0f;
        out.racing = left > 0 && frame - m_timeFrame < 6;
        Finish(emulator, kBikeControls, out);
        return true;
      }

    private:
      uint16_t m_timeLeft = 0;
      uint64_t m_timeFrame = 0;
    };
  }

  std::unique_ptr<IDecoder> MakeModel2Decoder(const Game &game)
  {
    // By the game, its parent's for a clone; the clones with their own addresses by their name
    const std::string &name = game.name;
    const std::string &family = game.parent.empty() ? game.name : game.parent;
    if (family == "daytona")
      return std::make_unique<CDaytona>(name == "daytona93" ? kDaytona93 : kDaytona);
    if (family == "srallyc")
      return std::make_unique<CSegaRally>(name == "srallycc" ? kSegaRallyCC
                                          : name == "srallycdxa" ? kSegaRallyDXA : kSegaRally);
    if (family == "indy500")
      return std::make_unique<CIndy500>(name == "indy500to" ? kIndy500To : name == "indy500d" ? kIndy500D : kIndy500);
    if (family == "stcc")
      return std::make_unique<CTouringCar>(name == "stcco" ? kStccO
                                           : name == "stcca" || name == "stccb" ? kStccAB : kStcc);
    if (family == "overrev")
      return std::make_unique<COverRev>();
    if (family == "sgt24h")
      return std::make_unique<CSuperGT24h>();
    if (family == "motoraid")
      return std::make_unique<CMotorRaid>();
    if (family == "waverunr")
      return std::make_unique<CWaveRunner>();
    if (family == "desert")
      return std::make_unique<CDesertTank>();
    if (family == "manxtt" && name != "manxttdx")
      return std::make_unique<CManxTT>();
    return nullptr;
  }
}
