/**
 ** Supermodel
 ** A Sega Model Series Arcade Emulator.
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
 * Model2DriveBoard.h
 *
 * The Model 2 racing games' wheel drive board (MAME's sj25_0207_01: Daytona
 * USA's 838-10646, Sega Rally's): a Z80 at 4 MHz running the drive program
 * (Daytona: "HANDLE CONTROL PROGRAM", EPR-16488A), two 315-5296 I/O chips and
 * an MSM6253 ADC. Unlike the Model 3 board it drives the wheel's motor itself:
 *
 *   IN 27      the command from the main board (Daytona: the I/O board's
 *              shared RAM byte 11; the 2A games: I/O port E)
 *   OUT 23     the reply, on the main board's I/O port D (Daytona: the I/O
 *              board's IN2, shared RAM byte 10)
 *   IN 26      DIP switches: 7-6 equal for normal operation, 3 power-on
 *              calibration, 5-4 the display, 2-0 the strength (inverted)
 *   IN 28-2B, 48-4B  the I/O chips' "SEGA" ID, checked at power-on
 *   OUT/IN 80  the ADC (channel select, then the 8 bits serially, MSB first):
 *              the wheel's position
 *   OUT 42-43  the 7-segment display
 *   OUT 46     the motor: power in bits 5-0 (inverted), bit 7 cleared to turn
 *              the wheel left, bit 6 cleared to turn it right; neither, the
 *              motor stands and the clutch holds the wheel to it (a brake)
 *
 * The wheel gets the frame's mean torque (63 the full force), the clutch's
 * brake against its speed, and a vibration when the torque is chopped faster
 * than the frames (Sega Rally's 11-17); the panel's light spring (PanelSpring,
 * a percentage of Sega Rally's strongest torque) is a centring effect.
 *
 * The work is done in the maskable interrupt (IM 1), which the program's
 * timeouts put at about 1 kHz (976 ticks per calibration step, 1000 for the
 * motor test): taken as 4 MHz / 4096. At power-on the board turns the wheel
 * left and right to find its ends and centre, as the cabinet does; without a
 * force feedback wheel that moves, it gives up and drives no force.
 *
 * Virtua Racing's board (Model 1, EPR-15111) is the same hardware for two
 * seats: player 1 as above but its DIP bank DSW1 on port 25 (DSW2, player 2's,
 * on 26), player 2's command on port 20, reply 21, motor 45, ADC channel 1. One
 * seat is emulated: player 2's link reads 00, its wheel 80, its bank has switch
 * 4 ON (no calibration, which would hold player 1's for a minute), as has
 * player 1's (the panel's calibration is for a cabinet's motor: the powers then
 * come from the weight switches, the ForceFeedbackStrength setting).
 *
 * SimulateDriveBoard: the program's logic (ClutchProgramSim) instead of its Z80,
 * interrupt by interrupt. Without the ROM (MAME's sets lack EPR-15111) the
 * program is simulated if the game says which it is (SimulateWithoutROM), else
 * the game runs without the board.
 */

#ifndef INCLUDED_MODEL2DRIVEBOARD_H
#define INCLUDED_MODEL2DRIVEBOARD_H

#include "Model3/DriveBoard/DriveBoard.h"
#include "Model2/ClutchProgramSim.h"

#include <memory>

class CModel2DriveBoard : public CDriveBoard
{
public:
  CModel2DriveBoard(const Util::Config::Node &config);

  Game::DriveBoardType GetType(void) const override { return Game::DRIVE_BOARD_WHEEL; }
  unsigned GetForceFeedbackStrength(void) const override;
  void SetForceFeedbackStrength(unsigned strength) override;

  void SaveState(CBlockFile *SaveState) override;
  void LoadState(CBlockFile *SaveState) override;
  void Reset(void) override;
  void RunFrame(void) override;

  UINT8 Read8(UINT32 addr) override;
  UINT8 IORead8(UINT32 portNum) override;
  void IOWrite8(UINT32 portNum, UINT8 data) override;

  void ResendForceFeedback(void) override;

  // The board's program is known (its CRC32) but its ROM is missing: it is simulated, whatever SimulateDriveBoard
  // says. Call before Reset(); Init() still needs a ROM buffer (0x10000 bytes, any content)
  void SimulateWithoutROM(uint32_t crc);

protected:
  void Disable(void) override;

private:
  static constexpr int TIMER_CYCLES = 4096;   // Z80 cycles per interrupt

  void SampleMotor(void);
  void SendFrameForces(void);
  void SendForce(INT8 force);
  void SendVibrate(UINT8 vibrate);
  void SendPanelSpring(void);
  void StopForce(void);

  void SimulateFrame(void);

  bool  m_vr = false;           // EPR-15111 (Virtua Racing): two seats, DSW1 on port 25
  uint32_t m_romlessCrc = 0;    // SimulateWithoutROM's program, 0 the ROM is there
  // SimulateDriveBoard: the program's logic, its interrupts at the emulated Z80's times
  std::unique_ptr<IClutchProgram> m_sim;
  double m_simCycles = 0.0, m_simNextInt = 0.0;
  int   m_cmdDelay = 0, m_wheelDelay = 0;   // the cycles from a timer expiry to the program's reads (measured)
  UINT8 m_prevWheel = 0x80;                 // the last frame's wheel, for an interrupt that read it then
  // SUPERMODEL_DRIVEBOARD_COMPARE (a debugging aid): the simulation run beside the emulated Z80 on the inputs the Z80
  // read, its motor byte and reply compared at each timer expiry, the counts logged
  std::unique_ptr<IClutchProgram> m_shadow;
  int64_t m_expiries = 0, m_shadowFirst = 0, m_cmpCalls = 0, m_cmpMotor = 0, m_cmpMotorNext = 0, m_cmpReply = 0;
  UINT8 m_cmdRead = 0, m_wheelRead = 0x80, m_shadowPrevMotor = 0xFF;
  void CompareShadow(int frameLeft);
  // The cycles from a timer expiry to the Z80's reads of the command and the wheel, bracketed (lo, hi]: from the
  // interrupts at a frame's end that read the next frame's value or not
  int m_lastExpiryLeft = 1 << 30, m_cmdLo = 0, m_cmdHi = 1 << 30, m_wheelLo = 0, m_wheelHi = 1 << 30;
  UINT8 m_expiryCmd = 0, m_expiryWheel = 0x80;

  UINT8 m_motor = 0xFF;         // port 46
  UINT8 m_adcValue = 0x80;      // the value being shifted out
  UINT8 m_adcBit = 0;           // bits left to shift out
  UINT8 m_display[2] = { 0xFF, 0xFF };
  int   m_timerCycles = TIMER_CYCLES;

  // The motor at each interrupt of the frame: its torque (up, to the right, positive; 0-63 each way), what the clutch
  // brakes with no way set, how often it reversed
  int   m_sumTorque = 0, m_sumBrake = 0, m_samples = 0, m_changes = 0;
  int   m_torques[64] = {};
  int   m_lastTorque = 0, m_lastStep = 0;  // the last interrupt's torque and which way it last changed
  int   m_lastWheel = -1;       // the wheel at the last frame (0-255), -1 unknown: its speed is braked

  INT8  m_lastForce = 0;
  UINT8 m_lastVibrate = 0;
  UINT8 m_lastSpring = 0;
};

#endif  // INCLUDED_MODEL2DRIVEBOARD_H
