/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2021 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
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
 * JoyBoard.cpp
 *
 * Implementation of the CJoyBoard class: drive board (force feedback emulation
 * for joystick) emulation.
 *
 * NOTE: The drive board of Star Wars Trilogy (EPR-21119) is the wheel boards'
 * (see CWheelBoard) with two of their servo boards, one per axis of the
 * joystick. Its program computes the spring itself, from the axes it reads on
 * ADC channels 3 and 4, and sends it to each servo board as a torque.
 *
 * NOTE: Simulation does not yet work. Drive board ROMs are required.
 */

#include "JoystickBoard.h"

#include "Supermodel.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

Game::DriveBoardType CJoyBoard::GetType(void) const
{
  return Game::DRIVE_BOARD_JOYSTICK;
}

void CJoyBoard::Get7SegDisplays(UINT8 &seg1Digit1, UINT8 &seg1Digit2, UINT8 &seg2Digit1, UINT8 &seg2Digit2) const
{
  seg1Digit1 = m_seg1Digit1;
  seg1Digit2 = m_seg1Digit2;
  seg2Digit1 = m_seg2Digit1;
  seg2Digit2 = m_seg2Digit2;
}

void CJoyBoard::SaveState(CBlockFile *SaveState)
{
  CDriveBoard::SaveState(SaveState);

  SaveState->NewBlock("JoystickBoard", __FILE__);
  SaveState->Write(m_simulated);
  if (m_simulated)
  {
    // TODO - save board simulation state
  }
  else
  {
    // Save DIP switches and digit displays
    SaveState->Write(m_dip1);
    SaveState->Write(m_dip2);

    SaveState->Write(m_adcPortRead);
    SaveState->Write(m_adcPortBit);
    SaveState->Write(m_servo[0].vibrateVal[0]);
    SaveState->Write(m_servo[0].vibrateVal[1]);

    // In a block of its own, so that older save states still load
    SaveState->NewBlock("JoystickBoardServos", __FILE__);
    for (const Servo &servo : m_servo)
    {
      SaveState->Write(servo.value);
      SaveState->Write(servo.vibrateVal[0]);
      SaveState->Write(servo.vibrateVal[1]);
      SaveState->Write(servo.gain);
      SaveState->Write(servo.constForce);
      SaveState->Write(servo.vibrate);
    }
  }
}

void CJoyBoard::LoadState(CBlockFile *SaveState)
{
  CDriveBoard::LoadState(SaveState);

  if (SaveState->FindBlock("JoystickBoard") != Result::OKAY)
  {
    ErrorLog("Unable to load joystick drive board state. Save state file is corrupt.");
    Disable();
    return;
  }

  bool wasSimulated;
  SaveState->Read(&wasSimulated, sizeof(wasSimulated));
  if (wasSimulated)
  {
    // Simulation has never existed
    ErrorLog("Save state contains unexpected data. Halting drive board emulation.");
    Disable();
    return;
  }
  else
  {
    // Load DIP switches and digit displays
    SaveState->Read(m_dip1);
    SaveState->Read(m_dip2);

    SaveState->Read(m_adcPortRead);
    SaveState->Read(m_adcPortBit);
    SaveState->Read(m_servo[0].vibrateVal[0]);
    SaveState->Read(m_servo[0].vibrateVal[1]);

    if (SaveState->FindBlock("JoystickBoardServos") == Result::OKAY)
    {
      for (Servo &servo : m_servo)
      {
        SaveState->Read(servo.value);
        SaveState->Read(servo.vibrateVal[0]);
        SaveState->Read(servo.vibrateVal[1]);
        SaveState->Read(servo.gain);
        SaveState->Read(servo.constForce);
        SaveState->Read(servo.vibrate);
      }
    }
    else
      ResetServos();
  }
}

void CJoyBoard::Disable(void)
{
  SendStopAll();
  CDriveBoard::Disable();
}

void CJoyBoard::Reset(void)
{
  CDriveBoard::Reset();

  m_seg1Digit1 = 0xFF;
  m_seg1Digit2 = 0xFF;
  m_seg2Digit1 = 0xFF;
  m_seg2Digit2 = 0xFF;

  m_adcPortRead = 0;
  m_adcPortBit = 0;

  ResetServos();

  m_lastConstForce[0] = 0;
  m_lastConstForce[1] = 0;
  m_lastSelfCenter = 0;
  m_lastFriction = 0;
  m_lastVibrate[0] = 0;
  m_lastVibrate[1] = 0;

  m_simulated = false;  //TODO: make this run-time configurable when simulation mode is supported

  if (!m_config["ForceFeedback"].ValueAsDefault<bool>(false))
    Disable();

  // Stop any effects that may still be playing
  if (!IsDisabled())
    SendStopAll();
}

void CJoyBoard::ResetServos(void)
{
  for (Servo &servo : m_servo)
  {
    servo.value = 0x80;
    servo.vibrateVal[0] = 0;
    servo.vibrateVal[1] = 0;
    servo.gain = 0x7F;
    servo.constForce = 0;
    servo.vibrate = 0;
  }
}

UINT8 CJoyBoard::Read(void)
{
  if (IsDisabled())
  {
    return 0xFF;
  }

  // TODO - simulate initialization sequence even when emulating to get rid of long pause at boot up (drive board can
  // carry on booting whilst game starts)
  if (m_simulated)
    return SimulateRead();
  else
    return CDriveBoard::Read();
}

void CJoyBoard::Write(UINT8 data)
{
  if (IsDisabled())
  {
    return;
  }

  //if (data >= 0x01 && data <= 0x0F ||
  //  data >= 0x20 && data <= 0x2F ||
  //  data >= 0x30 && data <= 0x3F ||
  //  data >= 0x40 && data <= 0x4F ||
  //  data >= 0x70 && data <= 0x7F)
  //  DebugLog("DriveBoard.Write(%02X)\n", data);
  if (m_simulated)
    SimulateWrite(data);
  else
  {
    CDriveBoard::Write(data);
    // C9 starts the board again from its reply CE: the stick is shown centred to its calibration until it replies
    // CB again. C8 jumps from the start-up straight to the game
    if (data == 0xC9)
      m_initialized = false;
    else if (data == 0xC8)
      m_initialized = true;
  }
}

UINT8 CJoyBoard::SimulateRead(void)
{
  if (m_initialized)
  {
    switch (m_readMode)
    {
      case 0x0: return m_statusFlags;                      // Status flags
      case 0x1: return m_dip1;                             // DIP switch 1 value
      case 0x2: return m_dip2;                             // DIP switch 2 value
      case 0x3: return m_wheelCenter;                      // Wheel center
      case 0x4: return 0x80;                               // Cockpit banking center
      case 0x5: return (UINT8)m_inputs->analogJoyX->value; // Wheel position
      case 0x6: return 0x80;                               // Cockpit banking position
      case 0x7: return m_echoVal;                          // Init status/echo test
      default:  return 0xFF;
    }
  }
  else
  {
    switch (m_initState / 5)
    {
      case 0:  return 0xCF;  // Initiate start
      case 1:  return 0xCE;
      case 2:  return 0xCD;
      case 3:  return 0xCC;  // Centering wheel
      default:
        m_initialized = true;
        return 0x80;
    }
  }
}

void CJoyBoard::SimulateWrite(UINT8 cmd)
{
  // Following are commands for Scud Race.  Daytona 2 has a compatible command set while Sega Rally 2 is completely different
  // TODO - finish for Scud Race and Daytona 2
  // TODO - implement for Sega Rally 2
  UINT8 type = cmd>>4;
  UINT8 val = cmd&0xF;


    switch (type)
    {
    case 0: // 0x00-0F Play sequence
      /* TODO */
      break;
    case 1: // 0x10-1F Set centering strength
      if (val == 0)
        // Disable auto-centering
        // TODO - is 0x10 for disable?
        SendSelfCenter(0);
      else
        // Enable auto-centering (0x1 = weakest, 0xF = strongest)
        SendSelfCenter(val * 0x11);
      break;
    case 2: // 0x20-2F Friction strength
      if (val == 0)
        // Disable friction
        // TODO - is 0x20 for disable?
        SendFriction(0);
      else
        // Enable friction (0x1 = weakest, 0xF = strongest)
        SendFriction(val * 0x11);
      break;
    case 3: // 0x30-3F Uncentering (vibrate)
      if (val == 0)
        // Disable uncentering
        SendVibrate(0, 0);
      else
        // Enable uncentering (0x1 = weakest, 0xF = strongest)
        SendVibrate(0, val * 0x11);
      break;
    case 4: // 0x40-4F Play power-slide sequence
      /* TODO */
      break;
    case 5: // 0x50-5F Rotate wheel right
      SendConstantForce(0, (val + 1) * 0x5);
      break;
    case 6: // 0x60-6F Rotate wheel left
      SendConstantForce(0, -(val + 1) * 0x5);
      break;
    case 7: // 0x70-7F Set steering parameters
      /* TODO */
      break;
    case 8: // 0x80-8F Test Mode
      switch (val & 0x7)
      {
        case 0:  SendStopAll();                                      break;  // 0x80 Stop motor
        case 1:  SendConstantForce(0, 20);                           break;  // 0x81 Roll wheel right
        case 2:  SendConstantForce(0, -20);                          break;  // 0x82 Roll wheel left
        case 3:  /* Ignore - no clutch */                            break;  // 0x83 Clutch on
        case 4:  /* Ignore - no clutch */                            break;  // 0x84 Clutch off
        case 5:  m_wheelCenter = (UINT8)m_inputs->analogJoyX->value; break;  // 0x85 Set wheel center position
        case 6:  /* Ignore */                                        break;  // 0x86 Set cockpit banking position
        case 7:  /* Ignore */                                        break;  // 0x87 Lamp on/off
      }
      break;
    case 0x9: // 0x90-9F ??? Don't appear to have any effect with Scud Race ROM
      /* TODO */
      break;
    case 0xA: // 0xA0-AF ??? Don't appear to have any effect with Scud Race ROM
      /* TODO */
      break;
    case 0xB: // 0xB0-BF Invalid command (reserved for use by PPC to send cabinet type 0xB0 or 0xB1 during initialization)
      /* Ignore */
      break;
    case 0xC: // 0xC0-CF Set board mode (0xCB = reset board)
      SendStopAll();
      if (val >= 0xB)
      {
        // Reset board
        m_initialized = false;
        m_initState = 0;
      }
      else
        m_boardMode = val;
      break;
    case 0xD: // 0xD0-DF Set read mode
      m_readMode = val & 0x7;
      break;
    case 0xE: // 0xE0-EF Invalid command
      /* Ignore */
      break;
    case 0xF: // 0xF0-FF Echo test
      m_echoVal = val;
      break;
    }

}

void CJoyBoard::RunFrame(void)
{
  if (IsDisabled())
  {
    return;
  }

  if (m_simulated)
    SimulateFrame();
  else
  {
    CDriveBoard::RunFrame();

    // The frame's ~9 NMIs run in one go: the stick only gets the last torques of the frame, so send those only
    UpdateForceFeedback();
  }
}

void CJoyBoard::SimulateFrame(void)
{
  if (!m_initialized)
    m_initState++;
  // TODO - update m_statusFlags and play preset scripts according to board mode
}

UINT8 CJoyBoard::IORead8(UINT32 portNum)
{
  UINT8 adcVal;

  switch (portNum)
  {
  case 0x20: // DIP 1 value
    return m_dip1;
  case 0x21: // DIP 2 value
    return m_dip2;
  case 0x24: // ADC channel 1 - only shown on the 7-segment displays
  case 0x25: // ADC channel 2 - only shown on the 7-segment displays
  case 0x26: // ADC channel 3 - joystick X axis (servo board 1) (0x00 = full left, 0x80 = center, 0xFF = full right)
  case 0x27: // ADC channel 4 - joystick Y axis (servo board 2)
    if (portNum == m_adcPortRead && m_adcPortBit-- > 0)
    {
      switch (portNum)
      {
      case 0x24:
        adcVal = ReadADCChannel1();
        break;
      case 0x25:
        adcVal = ReadADCChannel2();
        break;
      case 0x26: // X analog axis for joystick
        adcVal = ReadADCChannel3();
        break;
      case 0x27: // Y analog axis for joystick
        adcVal = ReadADCChannel4();
        break;
      default:
        DebugLog("Unhandled Z80 input on ADC port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
        return 0xFF;
      }
      return (adcVal >> m_adcPortBit) & 0x01;
      }
      else
      {
        DebugLog("Unhandled Z80 input on ADC port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
        return 0xFF;
      }
  case 0x28: // PPC command
    return m_dataSent;
  case 0x2c: // Encoder error reporting (kept at 0x00 for no error)
    // Bits 0-1 servo board 1, bits 2-3 servo board 2:
    // Bit 1 0
    //     0 0 = encoder okay, no error
    //     0 1 = encoder error 1 - overcurrent error
    //     1 0 = encoder error 2 - overheat error
    //     1 1 = encoder error 3 - encoder error, reinitializes board
    return 0x00;
  default:
    DebugLog("Unhandled Z80 input on port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
    return 0xFF;
  }
}

void CJoyBoard::IOWrite8(UINT32 portNum, UINT8 data)
{
  CDriveBoard::IOWrite8(portNum, data);

  switch (portNum)
  {
  case 0x1c: // Parallel (I/O) A port data
  case 0x1d: // Parallel (I/O) A port command
  case 0x1e: // Parallel (I/O) B port data
  case 0x1f: // Parallel (I/O) B port command
    return;
  case 0x20: // Left digit of 7-segment display 1
    m_seg1Digit1 = data;
    return;
  case 0x21: // Right digit of 7-segment display 1
    m_seg1Digit2 = data;
    return;
  case 0x22: // Left digit of 7-segment display 2
    m_seg2Digit1 = data;
    return;
  case 0x23: // Right digit of 7-segment display 2
    m_seg2Digit2 = data;
    return;
  case 0x24: // ADC channel 1 control
  case 0x25: // ADC channel 2 control
  case 0x26: // ADC channel 3 control
  case 0x27: // ADC channel 4 control
    m_adcPortRead = portNum;
    m_adcPortBit = 8;
    return;
  case 0x29: // Reply for PPC
    m_dataReceived = data;
    if (data == 0xCB)
      m_initialized = true;
    return;
  case 0x2a: // Servo board 1 value (x axis), written before the registers
    m_servo[0].value = data;
    return;
  case 0x2d: // Servo board 2 value (y axis), written before the registers
    m_servo[1].value = data;
    return;
  case 0x2e: // Servo boards' registers: board 1 in bits 0-3, board 2 in bits 4-7, each the register less its F
    ProcessServoRegister(0, 0xF0 | (data & 0x0F));
    ProcessServoRegister(1, 0xF0 | (data >> 4));
    return;
  default:
    DebugLog("Unhandled Z80 output on port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
    return;
  }
}

void CJoyBoard::ProcessServoRegister(int servoNum, UINT8 reg)
{
  // The wheel boards' servo board registers (see CWheelBoard::ProcessEncoderCmd). Bit 3 of each nibble, A-RST, is
  // active low: the program only writes 0xF8-0xFF
  Servo &servo = m_servo[servoNum];
  UINT8 value = servo.value;
  switch (reg)
  {
    case 0xFB:
      // Torque gain, 0x80 (none) to 0xFF (full) [guess]. 0x05, written only at start-up, is not a gain
      if (value & 0x80)
        servo.gain = value & 0x7F;
      break;

    case 0xFC:
      // Sub-register in bits 0-2. 4, the servo board's own spring, is never written: the program computes the spring
      // and sends it as a torque. 0-3: vibration (the game's 6x and 7x commands, Scud Race's bytes), a nibble each in
      // bits 4-7, low nibble first; set when 3 is written. The mode changes (FC 0A, FC 0B) zero it
      if (!(value & 0x04))
      {
        UINT8 subRegister = value & 0x03;
        UINT8 nibble = value >> 4;
        UINT8 &param = servo.vibrateVal[subRegister >> 1];
        if (subRegister & 1)
          param = (param & 0x0F) | (nibble << 4);
        else
          param = (param & 0xF0) | nibble;
        if (subRegister == 3)
        {
          if (servo.vibrateVal[0] == 0 || servo.vibrateVal[1] == 0)
            servo.vibrate = 0;
          else
          {
            // What the two bytes do on the servo board is unknown: the wheel boards' scale
            int strength = ((servo.vibrateVal[0]>>1) - 7) * 0x50 + ((servo.vibrateVal[1]>>1) - 5) * 0x10 + 0xF;
            servo.vibrate = (UINT8)std::clamp(strength, 0, 0xFF);
          }
        }
      }
      break;

    case 0xFE:
    {
      // Torque: 0x80 none, below 0x80 raises the axis on servo board 1 and lowers it on servo board 2, wired the other
      // way round. The spring rests at 0x7F on board 1 and 0x81 on board 2: these are no force, as on the wheels
      int force;
      if (value > 0x81)
        force = value <= 0xC0 ? 2 * (0x81 - value) : -0x80;
      else if (value < 0x7F)
        force = value >= 0x40 ? 2 * (0x7F - value) : 0x7F;
      else
        force = 0;
      servo.constForce = (INT8)std::clamp(servoNum == 0 ? force : -force, -0x80, 0x7F);
      break;
    }

    case 0xFF:
      // Motor idle, whatever the value
      servo.constForce = 0;
      servo.vibrate = 0;
      break;

    default:
      // 0xFD (unsure), 0xF8, 0xF9 (start-up settings): not translated
      break;
  }
}

void CJoyBoard::UpdateForceFeedback(void)
{
  // The servo board's gain scales the torque only, as on the wheel boards. The program never sets it
  for (int axisNum = 0; axisNum < 2; axisNum++)
  {
    const Servo &servo = m_servo[axisNum];
    SendConstantForce(axisNum, servo.constForce * servo.gain / 0x7F);
    SendVibrate(axisNum, servo.vibrate);
  }
}

void CJoyBoard::SendStopAll(void)
{
  //DebugLog(">> Stop All Effects\n");

  ForceFeedbackCmd ffCmd{};
  ffCmd.id = FFStop;

  m_inputs->analogJoyX->SendForceFeedbackCmd(ffCmd);
  m_inputs->analogJoyY->SendForceFeedbackCmd(ffCmd);

  m_lastConstForce[0] = 0;
  m_lastConstForce[1] = 0;
  m_lastSelfCenter = 0;
  m_lastFriction   = 0;
  m_lastVibrate[0] = 0;
  m_lastVibrate[1] = 0;
}

void CJoyBoard::SendConstantForce(int axisNum, INT8 val)
{
  if (val == m_lastConstForce[axisNum])
    return;
  /*
  if (val > 0)
  {
    DebugLog(">> Force Right %02X [%8s", val, "");
    for (unsigned i = 0; i < 8; i++)
      DebugLog(i == 0 || i <= (val + 1) / 16 ? ">" : " ");
    DebugLog("]\n");
  }
  else if (val < 0)
  {
    DebugLog(">> Force Left  %02X [", -val);
    for (unsigned i = 0; i < 8; i++)
      DebugLog(i == 7 || i >= (val + 128) / 16 ? "<" : " ");
    DebugLog("%8s]\n", "");
  }
  else
    DebugLog(">> Stop Force     [%16s]\n", "");
  */

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFConstantForce;
  ffCmd.force = (float)val / (val >= 0 ? 127.0f : 128.0f);

  if (axisNum == 0)
    m_inputs->analogJoyX->SendForceFeedbackCmd(ffCmd);
  else
    m_inputs->analogJoyY->SendForceFeedbackCmd(ffCmd);

  m_lastConstForce[axisNum] = val;
}

void CJoyBoard::SendSelfCenter(UINT8 val)
{
  if (val == m_lastSelfCenter)
    return;
  /*
  if (val == 0)
    DebugLog(">> Stop Self-Center\n");
  else
    DebugLog(">> Self-Center %02X\n", val);
  */

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFSelfCenter;
  ffCmd.force = (float)val / 255.0f;

  m_inputs->analogJoyX->SendForceFeedbackCmd(ffCmd);
  m_inputs->analogJoyY->SendForceFeedbackCmd(ffCmd);

  m_lastSelfCenter = val;
}


void CJoyBoard::SendFriction(UINT8 val)
{
  if (val == m_lastFriction)
    return;
  /*
  if (val == 0)
    DebugLog(">> Stop Friction\n");
  else
    DebugLog(">> Friction %02X\n", val);
  */

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFFriction;
  ffCmd.force = (float)val / 255.0f;
  m_inputs->analogJoyX->SendForceFeedbackCmd(ffCmd);

  m_lastFriction = val;
}

void CJoyBoard::SendVibrate(int axisNum, UINT8 val)
{
  if (val == m_lastVibrate[axisNum])
    return;
  /*
  if (val == 0)
    DebugLog(">> Stop Vibrate\n");
  else
    DebugLog(">> Vibrate %02X\n", val);
  */

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFVibrate;
  ffCmd.force = (float)val / 255.0f;

  if (axisNum == 0)
    m_inputs->analogJoyX->SendForceFeedbackCmd(ffCmd);
  else
    m_inputs->analogJoyY->SendForceFeedbackCmd(ffCmd);

  m_lastVibrate[axisNum] = val;
}

uint8_t CJoyBoard::ReadADCChannel1() const
{
  return 0x80;
}

uint8_t CJoyBoard::ReadADCChannel2() const
{
  return 0x80;
}

uint8_t CJoyBoard::ReadADCChannel3() const
{
  if (m_initialized)
    return (UINT8)m_inputs->analogJoyX->value;
  else
    return 0x80; // If not initialized, return 0x80 so that ffb centering test does not fail
}

uint8_t CJoyBoard::ReadADCChannel4() const
{
  if (m_initialized)
    return (UINT8)m_inputs->analogJoyY->value;
  else
    return 0x80; // If not initialized, return 0x80 so that ffb centering test does not fail
}

CJoyBoard::CJoyBoard(const Util::Config::Node &config)
  : CDriveBoard(config)
{
  m_dip1 = 0xCF;
  m_dip2 = 0xFF;

  ResetServos();

  DebugLog("Built Drive Board (Joystick)\n");
}

CJoyBoard::~CJoyBoard(void)
{

}
