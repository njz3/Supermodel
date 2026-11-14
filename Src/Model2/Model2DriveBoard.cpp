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
 * Model2DriveBoard.cpp
 *
 * The Model 2 wheel drive board. See Model2DriveBoard.h.
 */

#include "Model2DriveBoard.h"

#include "Supermodel.h"
#include "Inputs/Inputs.h"

#include <algorithm>
#include <cmath>
#include <zlib.h>

CModel2DriveBoard::CModel2DriveBoard(const Util::Config::Node &config)
  : CDriveBoard(config)
{
  m_z80Clock = 4.0f;
  // normal operation, power-on calibration, the display shows the wheel's
  // position; the strength comes from the ForceFeedbackStrength setting
  m_dip1 = 0xF8;
  m_dip2 = 0xFF;
}

void CModel2DriveBoard::SimulateWithoutROM(uint32_t crc)
{
  m_romlessCrc = crc;
}

// The strength switches (2-0, inverted) add to the motor's power: 1-8 as
// Model 3's setting, 1 the weakest (EPR-15111: player 1's weight, DSW1)
unsigned CModel2DriveBoard::GetForceFeedbackStrength(void) const
{
  return ((~m_dip1) & 7) + 1;
}

void CModel2DriveBoard::SetForceFeedbackStrength(unsigned strength)
{
  strength = std::clamp(strength, 1u, 8u);
  m_dip1 = (m_dip1 & 0xF8) | ((~(strength - 1)) & 7);
}

void CModel2DriveBoard::SaveState(CBlockFile *SaveState)
{
  CDriveBoard::SaveState(SaveState);
  SaveState->NewBlock("Model2DriveBoard", __FILE__);
  SaveState->Write(m_motor);
  SaveState->Write(m_adcValue);
  SaveState->Write(m_adcBit);
  SaveState->Write(m_display, sizeof(m_display));
  SaveState->Write(m_timerCycles);
  if (m_sim)
  {
    SaveState->Write(m_simCycles);
    SaveState->Write(m_simNextInt);
    SaveState->Write(m_prevWheel);
    m_sim->SaveState(SaveState);
  }
}

void CModel2DriveBoard::LoadState(CBlockFile *SaveState)
{
  CDriveBoard::LoadState(SaveState);
  if (SaveState->FindBlock("Model2DriveBoard") != Result::OKAY)
  {
    Disable();
    return;
  }
  SaveState->Read(m_motor);
  SaveState->Read(m_adcValue);
  SaveState->Read(m_adcBit);
  SaveState->Read(m_display, sizeof(m_display));
  SaveState->Read(m_timerCycles);
  if (m_sim && !IsDisabled())
  {
    SaveState->Read(m_simCycles);
    SaveState->Read(m_simNextInt);
    SaveState->Read(m_prevWheel);
    m_sim->LoadState(SaveState);
  }
  m_lastWheel = -1;
  if (!IsDisabled())
  {
    StopForce();
    SendPanelSpring();
  }
}

void CModel2DriveBoard::Reset(void)
{
  // The program: its ROM's, or the one simulated without it
  uint32_t crc = m_romlessCrc != 0 ? m_romlessCrc : uint32_t(crc32(0, m_rom, 0x10000));
  m_vr = crc == EPR15111_CRC32;
  if (m_vr)
  {
    // DSW1: the game, switch 4 ON (no calibration), the weight from ForceFeedbackStrength (CDriveBoard::Reset); DSW2:
    // player 2's, switch 4 ON (mandatory with one seat), the display showing the wheel
    m_dip1 = 0xF7;
    m_dip2 = 0xF7;
  }
  CDriveBoard::Reset();
  bool simulate = m_romlessCrc != 0 || m_config["SimulateDriveBoard"].ValueAsDefault<bool>(false);
  m_sim.reset();
  if (simulate)
  {
    m_sim = MakeClutchProgram(crc);
    if (m_sim)
    {
      m_sim->Reset(m_dip1, m_dip2);
      m_simCycles = 0.0;
      m_simNextInt = m_sim->FirstInterruptCycles();
      m_prevWheel = 0x80;
      // When the program reads its inputs in its interrupt (SimulateFrame)
      m_cmdDelay = crc == EPR15111_CRC32 ? 150 : crc == EPR17891_CRC32 ? 1165 : 853;
      m_wheelDelay = crc == EPR15111_CRC32 ? 1190 : crc == EPR17891_CRC32 ? 155 : 275;
    }
    else
      InfoLog("Drive board: its program (CRC32 %08X) is not simulated, it is emulated.", crc);
  }
  m_simulated = m_sim != nullptr;
  m_shadow.reset();
  m_expiries = m_cmpCalls = m_cmpMotor = m_cmpMotorNext = m_cmpReply = 0;
  if (!m_sim && getenv("SUPERMODEL_DRIVEBOARD_COMPARE") != nullptr)
  {
    m_shadow = MakeClutchProgram(crc);
    if (m_shadow)
    {
      m_shadow->Reset(m_dip1, m_dip2);
      m_shadowFirst = int64_t(m_shadow->FirstInterruptCycles() / TIMER_CYCLES);
    }
  }
  m_motor = 0xFF;
  m_adcValue = 0x80;
  m_adcBit = 0;
  m_display[0] = m_display[1] = 0xFF;
  m_timerCycles = TIMER_CYCLES;
  m_lastWheel = -1;
  if (!m_config["ForceFeedback"].ValueAsDefault<bool>(false))
    Disable();
  if (!IsDisabled())
  {
    StopForce();
    SendPanelSpring();
  }
}

void CModel2DriveBoard::Disable(void)
{
  StopForce();
  CDriveBoard::Disable();
}

void CModel2DriveBoard::RunFrame(void)
{
  if (IsDisabled())
    return;

  // The next command the main board wrote, shown to the Z80 for this whole
  // frame (the program takes a command once it reads the same one twice)
  {
    std::lock_guard<std::mutex> lock(m_commandMutex);
    if (!m_commands.empty())
    {
      m_dataSent = m_commands.front();
      m_commands.pop_front();
    }
  }

  if (m_sim)
  {
    SimulateFrame();
    return;
  }

  // The frame's interrupts, the motor sampled at each
  int frameCycles = GetFramecycles();
  while (frameCycles > 0)
  {
    int cycles = m_z80.Run(std::min(frameCycles, m_timerCycles));
    frameCycles -= cycles;
    m_timerCycles -= cycles;
    if (m_timerCycles <= 0)
    {
      m_timerCycles += TIMER_CYCLES;
      ++m_nmiCount;
      m_z80.SetINT(true);
      SampleMotor();
      if (m_shadow)
        CompareShadow(frameCycles);
    }
  }
  SendFrameForces();
}

// At a timer expiry of the emulated Z80: the simulation, called with what the Z80 read since the last expiry, against
// the Z80's motor byte and reply now (and the motor byte at the next expiry: one interrupt late)
void CModel2DriveBoard::CompareShadow(int frameLeft)
{
  // The last interrupt of a frame, the next frame's command or wheel different: did the Z80 read it?
  UINT8 wheelNow = m_inputs && m_inputs->steering ? (UINT8) m_inputs->steering->value : 0x80;
  if (m_lastExpiryLeft < TIMER_CYCLES && m_expiries > m_shadowFirst)
  {
    if (m_dataSent != m_expiryCmd)
    {
      if (m_cmdRead == m_dataSent)
        m_cmdLo = std::max(m_cmdLo, m_lastExpiryLeft);
      else if (m_cmdRead == m_expiryCmd)
        m_cmdHi = std::min(m_cmdHi, m_lastExpiryLeft);
    }
    if (wheelNow != m_expiryWheel)
    {
      if (m_wheelRead == wheelNow)
        m_wheelLo = std::max(m_wheelLo, m_lastExpiryLeft);
      else if (m_wheelRead == m_expiryWheel)
        m_wheelHi = std::min(m_wheelHi, m_lastExpiryLeft);
    }
  }
  m_lastExpiryLeft = frameLeft;
  m_expiryCmd = m_dataSent;
  m_expiryWheel = wheelNow;
  ++m_expiries;
  if (m_expiries > m_shadowFirst)
  {
    if (m_cmpCalls > 0 && m_shadowPrevMotor != m_motor)
      ++m_cmpMotorNext;
    m_shadow->Interrupt(m_cmdRead, m_wheelRead);
    ++m_cmpCalls;
    m_cmpMotor += m_shadow->Motor() != m_motor;
    m_cmpReply += m_shadow->Reply() != m_dataReceived;
    m_shadowPrevMotor = m_shadow->Motor();
    if (m_cmpCalls % 20000 == 0)
      InfoLog("Drive board compare: %lld interrupts, motor differs %lld (%lld one interrupt late), reply differs %lld; "
              "command read %d-%d cycles after the expiry, wheel %d-%d.", (long long) m_cmpCalls, (long long) m_cmpMotor,
              (long long) m_cmpMotorNext, (long long) m_cmpReply, m_cmdLo, m_cmdHi, m_wheelLo, m_wheelHi);
  }
}

// The program simulated: its interrupts at the times the emulated Z80 takes them, with the frame's command and wheel
//
// The Z80 reads the wheel and the command a little after each timer expiry, during its interrupt routine (measured with
// SUPERMODEL_DRIVEBOARD_COMPARE: EPR-16488A the wheel 275 cycles in, the command 853; EPR-17891 155 and 1165;
// EPR-15111 1190 and 150): an interrupt at a frame's end reads the next frame's. So an interrupt is run once the frame
// its command read falls in has come, with the wheel of the frame its wheel read fell in. And each emulated frame runs
// a few cycles more than GetFramecycles() (its last instruction's overshoot, 4.1-4.5 measured): the interrupts drift
// by as much against the frames
void CModel2DriveBoard::SimulateFrame(void)
{
  constexpr double kOvershoot = 4.3;
  UINT8 wheel = m_inputs && m_inputs->steering ? (UINT8) m_inputs->steering->value : 0x80;
  double frameStart = m_simCycles;
  m_simCycles += GetFramecycles() + kOvershoot;
  while (m_simNextInt + m_cmdDelay <= m_simCycles)
  {
    UINT8 wheelRead = m_simNextInt + m_wheelDelay <= frameStart ? m_prevWheel : wheel;
    m_simNextInt += TIMER_CYCLES;
    ++m_nmiCount;
    m_sim->Interrupt(m_dataSent, wheelRead);
    m_motor = m_sim->Motor();
    m_dataReceived = m_sim->Reply();
    SampleMotor();
  }
  m_prevWheel = wheel;
  SendFrameForces();
}

// The motor byte (port 46, active low): bit 7 the way down (left), bit 6 up (right), bits 5-0 the clutch's power; no
// way, the clutch holds the wheel to the standing motor: a brake at that power
void CModel2DriveBoard::SampleMotor(void)
{
  int power = (~m_motor) & 0x3F;
  bool down = !(m_motor & 0x80), up = !(m_motor & 0x40);
  int torque = 0;
  if (down != up)
    torque = up ? power : -power;
  else
    m_sumBrake += power;
  // Reversals (a rise after a fall, or the other way), not changes: Daytona's power ramps one step an interrupt
  int last = m_samples > 0 ? m_torques[(m_samples - 1) & 63] : m_lastTorque;
  if (torque != last)
  {
    int way = torque > last ? 1 : -1;
    if (m_lastStep != 0 && way != m_lastStep)
      m_changes++;
    m_lastStep = way;
  }
  m_torques[m_samples & 63] = torque;
  m_sumTorque += torque;
  m_samples++;
}

void CModel2DriveBoard::SendFrameForces(void)
{
  int n = m_samples;
  if (n == 0)
    return;
  float torque = float(m_sumTorque) / n, brake = float(m_sumBrake) / n;
  // A torque chopped faster than the frames (several reversals in one): its mean pushes, its swing about the mean
  // vibrates (Sega Rally's 11-17: off half the time, a quarter stronger the other half)
  float swing = 0.0f;
  if (m_changes >= 2)
  {
    for (int i = 0; i < std::min(n, 64); i++)
      swing += std::abs(m_torques[i] - torque);
    swing /= std::min(n, 64);
  }
  m_lastTorque = m_torques[(n - 1) & 63];
  m_sumTorque = m_sumBrake = m_samples = m_changes = 0;

  // The brake: against the wheel's speed, full from 4 steps of 255 a frame (as EPR-18261's, WheelBoard)
  int wheel = m_inputs && m_inputs->steering ? int(m_inputs->steering->value) : -1;
  float speed = (wheel >= 0 && m_lastWheel >= 0) ? std::clamp((wheel - m_lastWheel) / 4.0f, -1.0f, 1.0f) : 0.0f;
  m_lastWheel = wheel;
  float force = torque - brake * speed;
  // 63 the full force: x 2 to the -128..127 of a constant force
  SendForce((INT8) std::clamp((int) std::lround(force * 2.0f), -128, 127));
  SendVibrate((UINT8) std::clamp((int) std::lround(swing * 4.0f), 0, 255));
}

UINT8 CModel2DriveBoard::Read8(UINT32 addr)
{
  if (addr < 0x8000)
    return m_rom[addr];
  if (addr >= 0xE000)
    return m_ram[addr - 0xE000];
  return 0xFF;
}

UINT8 CModel2DriveBoard::IORead8(UINT32 portNum)
{
  portNum &= 0xFF;
  switch (portNum)
  {
  case 0x26:                        // DIP switches (EPR-15111: DSW2, player 2's)
    return m_vr ? m_dip2 : m_dip1;
  case 0x25:                        // EPR-15111: DSW1, player 1's
    return m_vr ? m_dip1 : 0xFF;
  case 0x20:                        // EPR-15111: player 2's command (no second seat)
    return m_vr ? 0x00 : 0xFF;
  case 0x27:                        // command
    m_cmdRead = m_dataSent;
    return m_dataSent;
  case 0x28: case 0x29: case 0x2A: case 0x2B:
  case 0x48: case 0x49: case 0x4A: case 0x4B:
    return "SEGA"[portNum & 3];     // the 315-5296s' ID
  case 0x80: case 0x81: case 0x82: case 0x83:
    // ADC: the selected channel's value, a bit at a time, MSB first
    if (m_adcBit == 0)
      return 0xFF;
    m_adcBit--;
    return (m_adcValue >> m_adcBit) & 1;
  default:
    return 0xFF;
  }
}

void CModel2DriveBoard::IOWrite8(UINT32 portNum, UINT8 data)
{
  portNum &= 0xFF;
  switch (portNum)
  {
  case 0x23:                        // reply
    m_dataReceived = data;
    break;
  case 0x42: case 0x43:             // 7-segment display
    m_display[portNum - 0x42] = data;
    break;
  case 0x46:                        // motor
    m_motor = data;
    break;
  case 0x80: case 0x81: case 0x82: case 0x83:
    // ADC channel select: channel 0 is the wheel
    m_adcValue = (portNum == 0x80 && m_inputs && m_inputs->steering) ? (UINT8) m_inputs->steering->value : 0x80;
    if (portNum == 0x80)
      m_wheelRead = m_adcValue;
    m_adcBit = 8;
    break;
  default:                          // 00: watchdog; the I/O chips' set-up
    break;
  }
}

void CModel2DriveBoard::SendForce(INT8 force)
{
  if (force == m_lastForce || !m_inputs || !m_inputs->steering)
    return;
  ForceFeedbackCmd ffCmd{};
  ffCmd.id = FFConstantForce;
  ffCmd.force = (float) force / (force >= 0 ? 127.0f : 128.0f);
  m_inputs->steering->SendForceFeedbackCmd(ffCmd);
  m_lastForce = force;
}

void CModel2DriveBoard::SendVibrate(UINT8 vibrate)
{
  if (vibrate == m_lastVibrate || !m_inputs || !m_inputs->steering)
    return;
  ForceFeedbackCmd ffCmd{};
  ffCmd.id = FFVibrate;
  ffCmd.force = (float) vibrate / 255.0f;
  m_inputs->steering->SendForceFeedbackCmd(ffCmd);
  m_lastVibrate = vibrate;
}

// The panel's light centring spring (Daytona's and Sega Rally's cabinets): PanelSpring percent of Sega Rally's
// strongest torque (32 of 63), 20 by default (sm2-emu's wheel_panel_spring, its author's)
void CModel2DriveBoard::SendPanelSpring(void)
{
  unsigned percent = std::min(m_config["PanelSpring"].ValueAsDefault<unsigned>(20), 100u);
  UINT8 spring = (UINT8) std::lround(255.0 * percent / 100.0 * 32.0 / 63.0);
  if (spring == m_lastSpring || !m_inputs || !m_inputs->steering)
    return;
  ForceFeedbackCmd ffCmd{};
  ffCmd.id = FFSelfCenter;
  ffCmd.force = (float) spring / 255.0f;
  m_inputs->steering->SendForceFeedbackCmd(ffCmd);
  m_lastSpring = spring;
}

void CModel2DriveBoard::StopForce(void)
{
  if (m_inputs && m_inputs->steering)
  {
    ForceFeedbackCmd ffCmd{};
    ffCmd.id = FFStop;
    m_inputs->steering->SendForceFeedbackCmd(ffCmd);
  }
  m_lastForce = 0;
  m_lastVibrate = 0;
  m_lastSpring = 0;
  m_sumTorque = m_sumBrake = m_samples = m_changes = 0;
}

// A state loaded, or the wheel's effects reset: the panel's spring and the frame's forces again from scratch
void CModel2DriveBoard::ResendForceFeedback(void)
{
  if (IsDisabled())
    return;
  INT8 force = m_lastForce;
  UINT8 vibrate = m_lastVibrate;
  StopForce();
  SendPanelSpring();
  SendForce(force);
  SendVibrate(vibrate);
}
