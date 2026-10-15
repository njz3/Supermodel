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
 * Telemetry.cpp
 *
 * The frames' derived values, their JSON and their Forza datagrams.
 */

#include "Telemetry.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Telemetry
{
  const char *StateName(State state)
  {
    switch (state)
    {
    case State::Attract:  return "attract";
    case State::Select:   return "select";
    case State::Race:     return "race";
    case State::Pause:    return "pause";
    case State::Over:     return "over";
    case State::Replay:   return "replay";
    default:              return "unknown";
    }
  }

  void SetOrientation(Vehicle &v, const float right[3], const float up[3], const float forward[3])
  {
    for (int r = 0; r < 3; r++)
    {
      v.rotation[r * 3 + 0] = right[r];
      v.rotation[r * 3 + 1] = up[r];
      v.rotation[r * 3 + 2] = forward[r];
    }
    v.heading = std::atan2(forward[0], forward[2]);
    v.pitch = std::asin(std::clamp(forward[1], -1.0f, 1.0f));
    v.roll = std::atan2(-right[1], up[1]);
  }

  /*
   * Derived values
   */

  static float Wrap(float a)
  {
    const float pi = 3.14159265f;
    while (a > pi)
      a -= 2 * pi;
    while (a < -pi)
      a += 2 * pi;
    return a;
  }

  void CDeriver::Derive(Frame &out)
  {
    const size_t n = 1 + out.others.size();
    if (m_last.size() < n)
      m_last.resize(n);
    for (size_t i = 0; i < n; i++)
    {
      Vehicle &v = i == 0 ? out.player : out.others[i - 1];
      Last &last = m_last[i];

      // A game that moves a car every other frame (30 Hz) leaves it where it was in between: a position the same to
      // the bit holds the last velocity and acceleration, up to 2 frames, and the next move is taken over the frames
      // since the last one (as sm2-emu does). Longer, the car is standing still
      bool samePlace = last.valid && v.x == last.x && v.y == last.y && v.z == last.z && out.frame > last.frame;
      if (samePlace && last.placeHeld < 2)
      {
        last.placeHeld++;
        if (!v.hasVelocity)
        {
          v.vx = last.vx; v.vy = last.vy; v.vz = last.vz;
        }
        v.ax = last.ax; v.ay = last.ay; v.az = last.az;
        v.impact = 0;
      }
      else
      {
        float dt = last.valid && out.frame > last.frame ? float((out.frame - last.frame) / kFps) : 0.0f;
        // Faster than 150 m/s: put elsewhere (a crash put back, a new race, a replay), the velocity kept, no
        // acceleration (as sm2-emu does)
        float moved = std::sqrt((v.x - last.x) * (v.x - last.x) + (v.y - last.y) * (v.y - last.y) + (v.z - last.z) * (v.z - last.z));
        bool teleported = dt > 0 && moved / dt > 150.0f;
        if (dt > 0 && dt < 0.5f && !teleported)
        {
          if (!v.hasVelocity)
          {
            v.vx = (v.x - last.x) / dt;
            v.vy = (v.y - last.y) / dt;
            v.vz = (v.z - last.z) / dt;
          }
          v.ax = (v.vx - last.vx) / dt;
          v.ay = (v.vy - last.vy) / dt;
          v.az = (v.vz - last.vz) / dt;
          // An impact: more than 8 g, and not a jump of the speed itself (a car put back on the track)
          float a = std::sqrt(v.ax * v.ax + v.ay * v.ay + v.az * v.az);
          float dv = a * dt;
          v.impact = a > 80.0f && dv < 50.0f ? dv : 0.0f;
        }
        else
        {
          if (!v.hasVelocity)
          {
            v.vx = teleported ? last.vx : 0.0f;
            v.vy = teleported ? last.vy : 0.0f;
            v.vz = teleported ? last.vz : 0.0f;
          }
          v.ax = v.ay = v.az = 0;
          v.impact = 0;
        }
        last.x = v.x; last.y = v.y; last.z = v.z;
        last.vx = v.vx; last.vy = v.vy; last.vz = v.vz;
        last.ax = v.ax; last.ay = v.ay; last.az = v.az;
        last.frame = out.frame;
        last.placeHeld = 0;
      }

      // The angles the same way: the same to the bit, the last rates held up to 2 frames
      bool sameAngles = last.valid && v.heading == last.heading && v.pitch == last.pitch && v.roll == last.roll &&
                        out.frame > last.angleFrame;
      if (sameAngles && last.anglesHeld < 2)
      {
        last.anglesHeld++;
        v.yaw_rate = last.yaw_rate; v.pitch_rate = last.pitch_rate; v.roll_rate = last.roll_rate;
      }
      else
      {
        float dt = last.valid && out.frame > last.angleFrame ? float((out.frame - last.angleFrame) / kFps) : 0.0f;
        if (dt > 0 && dt < 0.5f)
        {
          // An angle turning more than 0.5 rad a frame is not a rotation (a heading flipping at a standstill)
          float frames = float(dt * kFps);
          auto rate = [&](float now, float before) {
            float d = Wrap(now - before);
            return std::fabs(d) > 0.5f * frames ? 0.0f : d / dt;
          };
          v.yaw_rate = rate(v.heading, last.heading);
          v.pitch_rate = rate(v.pitch, last.pitch);
          v.roll_rate = rate(v.roll, last.roll);
        }
        else
          v.yaw_rate = v.pitch_rate = v.roll_rate = 0;
        last.heading = v.heading; last.pitch = v.pitch; last.roll = v.roll;
        last.yaw_rate = v.yaw_rate; last.pitch_rate = v.pitch_rate; last.roll_rate = v.roll_rate;
        last.angleFrame = out.frame;
        last.anglesHeld = 0;
      }
      last.valid = true;

      // In the car's frame: the rotation's columns are its right, up and forward
      const auto &R = v.rotation;
      v.alat = v.ax * R[0] + v.ay * R[3] + v.az * R[6];
      v.avert = v.ax * R[1] + v.ay * R[4] + v.az * R[7];
      v.along = v.ax * R[2] + v.ay * R[5] + v.az * R[8];
      if (!std::isnan(v.ground) && v.airborne == kUnknown)
        v.airborne = v.ground > 0.25f ? 1 : 0;
    }

    // The gaps along the track, from the laps driven
    if (!std::isnan(out.lapLength))
    {
      for (size_t i = 0; i < n; i++)
      {
        Vehicle &v = i == 0 ? out.player : out.others[i - 1];
        float ahead = NAN, behind = NAN;
        for (size_t j = 0; j < n; j++)
        {
          if (j == i)
            continue;
          const Vehicle &w = j == 0 ? out.player : out.others[j - 1];
          float d = (w.progress - v.progress) * out.lapLength;
          if (d >= 0 && (std::isnan(ahead) || d < ahead))
            ahead = d;
          if (d < 0 && (std::isnan(behind) || -d < behind))
            behind = -d;
        }
        float speed = std::max(std::isnan(v.speed) ? std::sqrt(v.vx * v.vx + v.vz * v.vz) : v.speed, 1.0f);
        v.dist_ahead = ahead;
        v.dist_behind = behind;
        v.gap_ahead = std::isnan(ahead) ? NAN : ahead / speed;
        v.gap_behind = std::isnan(behind) ? NAN : behind / speed;
      }
    }
  }

  /*
   * JSON
   */

  namespace
  {
    class CJson
    {
    public:
      CJson(const Selection &selection)
        : m_sel(selection)
      {
      }

      // The key at path is sent: everything is, or a field names it or one of its parents
      bool Wants(const std::string &path) const
      {
        if (m_sel.fields.empty())
          return true;
        for (auto &f : m_sel.fields)
        {
          if (f == path || (path.size() > f.size() && path.compare(0, f.size(), f) == 0 && path[f.size()] == '.'))
            return true;
        }
        return false;
      }

      // Something under path is sent
      bool WantsUnder(const std::string &path) const
      {
        if (Wants(path))
          return true;
        for (auto &f : m_sel.fields)
        {
          if (f.size() > path.size() && f.compare(0, path.size(), path) == 0 && f[path.size()] == '.')
            return true;
        }
        return false;
      }

      void Key(const char *key)
      {
        Separator();
        m_s += '"';
        m_s += key;
        m_s += "\":";
      }

      void Open(char c)
      {
        m_s += c;
        m_first.push_back(true);
      }

      void Close(char c)
      {
        m_s += c;
        m_first.pop_back();
      }

      void Separator()
      {
        if (!m_first.empty())
        {
          if (!m_first.back())
            m_s += ',';
          m_first.back() = false;
        }
      }

      void Float(float x)
      {
        if (!std::isfinite(x))
          m_s += "null";
        else
        {
          char buf[32];
          snprintf(buf, sizeof(buf), "%.7g", double(x));
          m_s += buf;
        }
      }

      void Int(int x)
      {
        if (x == kUnknown)
          m_s += "null";
        else
          m_s += std::to_string(x);
      }

      void String(const std::string &s)
      {
        m_s += '"';
        for (char c : s)
        {
          if (c == '"' || c == '\\')
            m_s += '\\';
          if (uint8_t(c) >= 0x20)
            m_s += c;
        }
        m_s += '"';
      }

      void Bool(bool b)
      {
        m_s += b ? "true" : "false";
      }

      // The leaf key at prefix + key, if sent
      template <typename F> void Leaf(const std::string &prefix, const char *key, F write)
      {
        if (Wants(prefix + key))
        {
          Key(key);
          write();
        }
      }

      std::string m_s;

    private:
      const Selection &m_sel;
      std::vector<bool> m_first;
    };

    void WriteMatrix(CJson &j, const std::array<float, 9> &m)
    {
      j.Open('[');
      for (float x : m)
      {
        j.Separator();
        j.Float(x);
      }
      j.Close(']');
    }

    void WriteVehicle(CJson &j, const Vehicle &v, const std::string &p)
    {
      j.Open('{');
      j.Leaf(p, "x", [&] { j.Float(v.x); });
      j.Leaf(p, "y", [&] { j.Float(v.y); });
      j.Leaf(p, "z", [&] { j.Float(v.z); });
      j.Leaf(p, "heading", [&] { j.Float(v.heading); });
      j.Leaf(p, "pitch", [&] { j.Float(v.pitch); });
      j.Leaf(p, "roll", [&] { j.Float(v.roll); });
      j.Leaf(p, "rotation", [&] { WriteMatrix(j, v.rotation); });
      j.Leaf(p, "speed", [&] { j.Float(v.speed); });
      j.Leaf(p, "vx", [&] { j.Float(v.vx); });
      j.Leaf(p, "vy", [&] { j.Float(v.vy); });
      j.Leaf(p, "vz", [&] { j.Float(v.vz); });
      j.Leaf(p, "ax", [&] { j.Float(v.ax); });
      j.Leaf(p, "ay", [&] { j.Float(v.ay); });
      j.Leaf(p, "az", [&] { j.Float(v.az); });
      j.Leaf(p, "along", [&] { j.Float(v.along); });
      j.Leaf(p, "alat", [&] { j.Float(v.alat); });
      j.Leaf(p, "avert", [&] { j.Float(v.avert); });
      j.Leaf(p, "yaw_rate", [&] { j.Float(v.yaw_rate); });
      j.Leaf(p, "pitch_rate", [&] { j.Float(v.pitch_rate); });
      j.Leaf(p, "roll_rate", [&] { j.Float(v.roll_rate); });
      j.Leaf(p, "ground", [&] { j.Float(v.ground); });
      j.Leaf(p, "airborne", [&] { if (v.airborne == kUnknown) j.m_s += "null"; else j.Bool(v.airborne != 0); });
      j.Leaf(p, "impact", [&] { j.Float(v.impact); });
      if (j.WantsUnder(p + "wheels"))
      {
        j.Key("wheels");
        j.Open('[');
        for (int w = 0; w < 4; w++)
        {
          std::string wp = p + "wheels." + std::to_string(w) + ".";
          const Wheel &wh = v.wheels[w];
          j.Separator();
          j.Open('{');
          j.Leaf(wp, "susp", [&] { j.Float(wh.susp); });
          j.Leaf(wp, "contact", [&] { if (wh.contact == kUnknown) j.m_s += "null"; else j.Bool(wh.contact != 0); });
          j.Leaf(wp, "surface", [&] { j.String(wh.surface.empty() ? "unknown" : wh.surface); });
          j.Leaf(wp, "slip", [&] { j.Float(wh.slip); });
          j.Leaf(wp, "spin", [&] { j.Float(wh.spin); });
          j.Close('}');
        }
        j.Close(']');
      }
      j.Leaf(p, "gear", [&] { j.Int(v.gear); });
      j.Leaf(p, "gears", [&] { j.Int(v.gears); });
      j.Leaf(p, "rpm", [&] { j.Float(v.rpm); });
      j.Leaf(p, "rpm_max", [&] { j.Float(v.rpm_max); });
      j.Leaf(p, "rpm_idle", [&] { j.Float(v.rpm_idle); });
      j.Leaf(p, "lap", [&] { j.Int(v.lap); });
      j.Leaf(p, "sector", [&] { j.Int(v.sector); });
      j.Leaf(p, "progress", [&] { j.Float(v.progress); });
      j.Leaf(p, "position", [&] { j.Int(v.position); });
      j.Leaf(p, "damage", [&] { j.Float(v.damage); });
      j.Leaf(p, "gap_ahead", [&] { j.Float(v.gap_ahead); });
      j.Leaf(p, "gap_behind", [&] { j.Float(v.gap_behind); });
      j.Leaf(p, "dist_ahead", [&] { j.Float(v.dist_ahead); });
      j.Leaf(p, "dist_behind", [&] { j.Float(v.dist_behind); });
      j.Leaf(p, "throttle", [&] { j.Float(v.throttle); });
      j.Leaf(p, "brake", [&] { j.Float(v.brake); });
      j.Leaf(p, "clutch", [&] { j.Float(v.clutch); });
      j.Leaf(p, "handbrake", [&] { j.Float(v.handbrake); });
      j.Leaf(p, "steer", [&] { j.Float(v.steer); });
      j.Close('}');
    }

    // The other cars, the nearest first (along the track, or in a straight line when the lap's length is unknown)
    std::vector<const Vehicle *> Nearest(const Frame &frame, int count)
    {
      std::vector<const Vehicle *> cars;
      for (auto &v : frame.others)
        cars.push_back(&v);
      if (count < 0 || size_t(count) >= cars.size())
        return cars;
      const Vehicle &p = frame.player;
      auto distance = [&](const Vehicle *v) {
        if (!std::isnan(frame.lapLength))
          return std::fabs(v->progress - p.progress) * frame.lapLength;
        return std::hypot(v->x - p.x, v->z - p.z);
      };
      std::stable_sort(cars.begin(), cars.end(), [&](const Vehicle *a, const Vehicle *b) { return distance(a) < distance(b); });
      cars.resize(size_t(count));
      return cars;
    }
  }

  std::string ToJson(const Frame &frame, const Selection &selection)
  {
    CJson j(selection);
    const std::string p;
    j.Open('{');
    j.Leaf(p, "frame", [&] { j.m_s += std::to_string(frame.frame); });
    j.Leaf(p, "game", [&] { j.String(frame.game); });
    j.Leaf(p, "track", [&] { j.String(frame.track); });
    j.Leaf(p, "sectors", [&] { j.Int(frame.sectors); });
    j.Leaf(p, "laps", [&] { j.Int(frame.laps); });
    j.Leaf(p, "transmission", [&] { j.String(frame.transmission); });
    j.Leaf(p, "racing", [&] { j.Bool(frame.racing); });
    j.Leaf(p, "finished", [&] { j.Bool(frame.finished); });
    j.Leaf(p, "state", [&] { j.String(StateName(frame.state)); });
    j.Leaf(p, "time_left", [&] { j.Float(frame.time_left); });
    j.Leaf(p, "lap_time", [&] { j.Float(frame.lap_time); });
    j.Leaf(p, "last_lap", [&] { j.Float(frame.last_lap); });
    j.Leaf(p, "best_lap", [&] { j.Float(frame.best_lap); });
    j.Leaf(p, "race_time", [&] { j.Float(frame.race_time); });
    j.Leaf(p, "checkpoint", [&] { j.Int(frame.checkpoint); });
    j.Leaf(p, "stage", [&] { j.Int(frame.stage); });
    j.Leaf(p, "car", [&] { j.String(frame.car); });
    j.Leaf(p, "difficulty", [&] { j.String(frame.difficulty); });
    j.Leaf(p, "camera", [&] { WriteMatrix(j, frame.camera); });
    if (j.WantsUnder("player"))
    {
      j.Key("player");
      WriteVehicle(j, frame.player, "player.");
    }
    if (selection.others != 0 && j.WantsUnder("others"))
    {
      j.Key("others");
      j.Open('[');
      for (const Vehicle *v : Nearest(frame, selection.others))
      {
        j.Separator();
        WriteVehicle(j, *v, "others.");
      }
      j.Close(']');
    }
    j.Close('}');
    return j.m_s;
  }

  std::string KnownKeys(const Frame &frame)
  {
    std::vector<std::string> keys = { "frame", "game", "racing", "finished", "state" };
    auto add = [&](const char *key, bool known) { if (known) keys.push_back(key); };
    add("track", !frame.track.empty());
    add("sectors", frame.sectors != 0);
    add("laps", frame.laps != 0);
    add("transmission", !frame.transmission.empty());
    add("time_left", !std::isnan(frame.time_left));
    add("lap_time", !std::isnan(frame.lap_time));
    add("last_lap", !std::isnan(frame.last_lap));
    add("best_lap", !std::isnan(frame.best_lap));
    add("race_time", !std::isnan(frame.race_time));
    add("checkpoint", frame.checkpoint != kUnknown);
    add("stage", frame.stage != kUnknown);
    add("car", !frame.car.empty());
    add("difficulty", !frame.difficulty.empty());
    const Vehicle &v = frame.player;
    for (const char *k : { "player.x", "player.y", "player.z", "player.heading", "player.pitch", "player.roll",
                           "player.rotation", "player.vx", "player.vy", "player.vz", "player.ax", "player.ay",
                           "player.az", "player.along", "player.alat", "player.avert", "player.yaw_rate",
                           "player.pitch_rate", "player.roll_rate", "player.impact", "player.lap",
                           "player.progress", "player.position" })
      keys.push_back(k);
    add("player.speed", !std::isnan(v.speed));
    add("player.ground", !std::isnan(v.ground));
    add("player.airborne", v.airborne != kUnknown);
    add("player.gear", v.gear != kUnknown);
    add("player.gears", v.gears != kUnknown);
    add("player.rpm", !std::isnan(v.rpm));
    add("player.rpm_max", !std::isnan(v.rpm_max));
    add("player.rpm_idle", !std::isnan(v.rpm_idle));
    add("player.damage", !std::isnan(v.damage));
    add("player.gap_ahead", !std::isnan(frame.lapLength));
    add("player.gap_behind", !std::isnan(frame.lapLength));
    add("player.dist_ahead", !std::isnan(frame.lapLength));
    add("player.dist_behind", !std::isnan(frame.lapLength));
    add("player.throttle", !std::isnan(v.throttle));
    add("player.brake", !std::isnan(v.brake));
    add("player.steer", !std::isnan(v.steer));
    add("player.wheels.0.susp", !std::isnan(v.wheels[0].susp));
    add("player.wheels.0.contact", v.wheels[0].contact != kUnknown);
    add("player.wheels.0.surface", !v.wheels[0].surface.empty());
    add("others", !frame.others.empty());
    std::string s = "[";
    for (size_t i = 0; i < keys.size(); i++)
      s += (i ? ",\"" : "\"") + keys[i] + "\"";
    return s + "]";
  }

  /*
   * Forza Motorsport 7's "Car Dash" datagram
   */

  namespace
  {
    class CPacket
    {
    public:
      void F32(float x)
      {
        if (!std::isfinite(x))
          x = 0;
        uint32_t u;
        memcpy(&u, &x, 4);
        U32(u);
      }

      void S32(int32_t x) { U32(uint32_t(x)); }

      void U32(uint32_t u)
      {
        for (int i = 0; i < 4; i++)
          m_data[m_pos++] = uint8_t(u >> (8 * i));
      }

      void U16(uint16_t u)
      {
        m_data[m_pos++] = uint8_t(u);
        m_data[m_pos++] = uint8_t(u >> 8);
      }

      void U8(uint8_t u) { m_data[m_pos++] = u; }

      std::array<uint8_t, kForzaDashSize> m_data{};
      size_t m_pos = 0;
    };

    uint8_t Byte01(float x)
    {
      return std::isfinite(x) ? uint8_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255)) : 0;
    }
  }

  std::array<uint8_t, kForzaDashSize> ForzaDash(const Frame &frame)
  {
    const Vehicle &v = frame.player;
    const auto &R = v.rotation;
    // In the car's frame: X its right, Y up, Z forward
    float bx = v.vx * R[0] + v.vy * R[3] + v.vz * R[6];
    float by = v.vx * R[1] + v.vy * R[4] + v.vz * R[7];
    float bz = v.vx * R[2] + v.vy * R[5] + v.vz * R[8];
    CPacket p;
    // The "Sled" part
    p.S32(frame.state == State::Race ? 1 : 0);
    p.U32(uint32_t(double(frame.frame) * 1000.0 / kFps));
    p.F32(v.rpm_max);
    p.F32(v.rpm_idle);
    p.F32(v.rpm);
    p.F32(v.alat); p.F32(v.avert); p.F32(v.along);
    p.F32(bx); p.F32(by); p.F32(bz);
    p.F32(v.pitch_rate); p.F32(v.yaw_rate); p.F32(v.roll_rate);
    p.F32(v.heading); p.F32(v.pitch); p.F32(v.roll);
    for (auto &w : v.wheels)                    // NormalizedSuspensionTravel: 0.5 at rest
      p.F32(std::isnan(w.susp) ? 0.0f : std::clamp(0.5f + w.susp / 0.2f, 0.0f, 1.0f));
    for (auto &w : v.wheels)                    // TireSlipRatio
      p.F32(w.slip);
    for (auto &w : v.wheels)                    // WheelRotationSpeed
      p.F32(w.spin);
    for (int i = 0; i < 4; i++)                 // WheelOnRumbleStrip
      p.S32(0);
    for (int i = 0; i < 4; i++)                 // WheelInPuddleDepth
      p.F32(0);
    for (int i = 0; i < 4; i++)                 // SurfaceRumble
      p.F32(0);
    for (int i = 0; i < 4; i++)                 // TireSlipAngle
      p.F32(0);
    for (auto &w : v.wheels)                    // TireCombinedSlip
      p.F32(w.slip);
    for (auto &w : v.wheels)                    // SuspensionTravelMeters
      p.F32(w.susp);
    for (int i = 0; i < 5; i++)                 // CarOrdinal, CarClass, CarPerformanceIndex, DrivetrainType, NumCylinders
      p.S32(0);
    // The "Dash" part
    p.F32(v.x); p.F32(v.y); p.F32(v.z);
    p.F32(std::isnan(v.speed) ? std::sqrt(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz) : v.speed);
    p.F32(0); p.F32(0);                         // Power, Torque
    for (int i = 0; i < 4; i++)                 // TireTemp
      p.F32(0);
    p.F32(0);                                   // Boost
    p.F32(1);                                   // Fuel
    p.F32(std::isnan(frame.lapLength) ? 0.0f : v.progress * frame.lapLength);
    p.F32(frame.best_lap);
    p.F32(frame.last_lap);
    p.F32(frame.lap_time);
    p.F32(frame.race_time);
    p.U16(uint16_t(std::max(v.lap - 1, 0)));
    p.U8(uint8_t(std::clamp(v.position, 0, 255)));
    p.U8(Byte01(v.throttle));
    p.U8(Byte01(v.brake));
    p.U8(Byte01(v.clutch));
    p.U8(Byte01(v.handbrake));
    p.U8(v.gear == -1 ? 0 : v.gear >= 1 && v.gear <= 10 ? uint8_t(v.gear) : 11);  // 0 reverse, 11 neutral or unknown
    p.U8(uint8_t(int8_t(std::isnan(v.steer) ? 0 : std::lround(-std::clamp(v.steer, -1.0f, 1.0f) * 127))));
    p.U8(0);                                    // NormalizedDrivingLine
    p.U8(0);                                    // NormalizedAIBrakeDifference
    return p.m_data;
  }
}
