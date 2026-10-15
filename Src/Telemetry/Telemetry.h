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
 * Telemetry.h
 *
 * What a racing game knows about the cars and the race, read out of its RAM
 * each frame: for a motion simulator, a dashboard on the wheel, an autopilot.
 * Served as one JSON line a frame (the keys sm2-emu, the Model 2 emulator,
 * serves too) and as Forza Motorsport 7's "Data Out" datagrams, which motion
 * and dashboard software reads. Each game has its own decoder (Model3Games.cpp).
 *
 * Units: metres, seconds, radians, m/s, m/s^2. The world: y up, and the car's
 * right is up x forward (a decoder mirrors a game whose world is the other
 * way), so that the heading, atan2(dx, dz) of the car's nose, grows when it
 * turns right. A value the game does not give is NaN (or kUnknown): null in
 * the JSON.
 */

#ifndef INCLUDED_TELEMETRY_H
#define INCLUDED_TELEMETRY_H

#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CModel3;
struct Game;

namespace Telemetry
{
  // An integer the game does not give
  constexpr int kUnknown = INT_MIN;

  // Model 3's frame rate
  constexpr double kFps = 57.524;

  // One wheel: front left, front right, rear left, rear right
  struct Wheel
  {
    float susp = NAN;         // suspension travel, m, > 0 compressed
    int contact = kUnknown;   // 1 on the ground
    std::string surface;      // "asphalt", "dirt", "gravel", "grass", "snow", "mud"...; empty unknown
    float slip = NAN;         // 0 gripping, 1 sliding
    float spin = NAN;         // rad/s
  };

  // One car
  struct Vehicle
  {
    float x = 0, y = 0, z = 0;
    float heading = 0;        // atan2(dx, dz) of the nose, + turning right
    float pitch = 0;          // + nose up
    float roll = 0;           // + leaning right (its right side lower)
    std::array<float, 9> rotation{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };   // world from car, row-major: columns right, up, forward
    float speed = NAN;        // m/s, the game's own
    float vx = 0, vy = 0, vz = 0;
    float ax = 0, ay = 0, az = 0;
    float along = NAN, alat = NAN, avert = NAN;               // in the car's frame: speeding up, towards its right, up
    float yaw_rate = NAN, pitch_rate = NAN, roll_rate = NAN;  // turning right, nose rising, rolling right
    float ground = NAN;       // height above the ground
    int airborne = kUnknown;
    float impact = NAN;       // a collision's change of speed this frame, m/s; else 0
    std::array<Wheel, 4> wheels{};
    int gear = kUnknown;      // -1 reverse, 0 neutral, 1..N
    int gears = kUnknown;     // forward gears
    float rpm = NAN, rpm_max = NAN, rpm_idle = NAN;           // rpm_max: the red line
    int lap = 0;              // 1 on the first lap
    int sector = 0;
    float progress = 0;       // laps driven, fractional
    int position = 0;         // 1 leading
    float damage = NAN;       // 0..1
    float gap_ahead = NAN, gap_behind = NAN, dist_ahead = NAN, dist_behind = NAN;  // s, m along the track
    float throttle = NAN, brake = NAN, clutch = NAN, handbrake = NAN, steer = NAN; // 0..1; steer -1..1, +1 left

    bool hasVelocity = false; // the decoder read vx, vy, vz from the game (else from the positions); not sent
  };

  // What the game is doing: a motion platform parks unless it is Race
  enum class State { Unknown, Attract, Select, Race, Pause, Over, Replay };

  const char *StateName(State state);

  struct Frame
  {
    uint64_t frame = 0;
    std::string game;
    std::string track;
    int sectors = 0;
    int laps = 0;             // the race's laps; 0 unknown
    std::string transmission; // "AT", "MT", or empty
    bool racing = false;
    bool finished = false;
    State state = State::Unknown;
    // Clocks as the game shows them (its seconds may not be real ones)
    float time_left = NAN, lap_time = NAN, last_lap = NAN, best_lap = NAN, race_time = NAN;
    int checkpoint = kUnknown;
    int stage = kUnknown;
    std::string car, difficulty;
    float lapLength = NAN;    // m, for the distances between the cars; not sent
    Vehicle player;
    std::vector<Vehicle> others;
    std::array<float, 9> camera{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  };

  // Fills a car's rotation, heading, pitch and roll from its right, up and forward vectors (world, y up)
  void SetOrientation(Vehicle &v, const float right[3], const float up[3], const float forward[3]);

  // A game's decoder: reads one frame from the RAM, false when the game is in no state to say anything
  class IDecoder
  {
  public:
    virtual ~IDecoder() {}
    virtual bool Decode(CModel3 &model3, uint64_t frame, Frame &out) = 0;
  };

  // The decoder for this game, or nullptr for a game not decoded
  std::unique_ptr<IDecoder> MakeDecoder(const Game &game);

  // What is worked out from the decoded frames: velocities and accelerations from the positions (unless the game
  // gives them), the accelerations in the car's frame, the rates, the impacts, airborne, the gaps
  class CDeriver
  {
  public:
    void Derive(Frame &out);

  private:
    // A car's last move and last turn, the reference of the next (held while it does not move: a game at 30 Hz)
    struct Last
    {
      float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, ax = 0, ay = 0, az = 0;
      float heading = 0, pitch = 0, roll = 0, yaw_rate = 0, pitch_rate = 0, roll_rate = 0;
      uint64_t frame = 0, angleFrame = 0;
      int placeHeld = 0, anglesHeld = 0;
      bool valid = false;
    };
    std::vector<Last> m_last;
  };

  // What a client asks for: the keys (dotted paths, "player.speed", "others.x", "player.wheels.0.susp"; empty for
  // all) and the other cars (the nearest along the track, -1 all)
  struct Selection
  {
    std::vector<std::string> fields;
    int others = -1;
  };

  // One frame as a JSON object on one line, without the line feed
  std::string ToJson(const Frame &frame, const Selection &selection = Selection());

  // The keys this frame fills (their values known), as a JSON array
  std::string KnownKeys(const Frame &frame);

  // One frame as Forza Motorsport 7's "Car Dash" datagram, little-endian
  constexpr size_t kForzaDashSize = 311;
  std::array<uint8_t, kForzaDashSize> ForzaDash(const Frame &frame);
}

#endif  // INCLUDED_TELEMETRY_H
