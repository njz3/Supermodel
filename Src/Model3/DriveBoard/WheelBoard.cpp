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
 * WheelBoard.cpp
 *
 * Implementation of the CWheelBoard class: drive board (force feedback for wheel)
 * emulation.
 *
 * NOTE: Simulation does not yet work. Drive board ROMs are required.
 */

#include "WheelBoard.h"

#include "Supermodel.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <zlib.h>

// EPR-18261, the program of the Model 2 Indy 500 drive board. Le Mans 24 uses it with its cabinet type STANDARD
// (with the cabinet type LE MANS, it uses Scud Race's EPR-19338A). It drives a motor with two powers, one per way,
// instead of the registers of a servo board, and reads the wheel on ADC channel 1
static constexpr uint32_t EPR18261_CRC32 = 0x0C7FAC58;

Game::DriveBoardType CWheelBoard::GetType(void) const
{
  return Game::DRIVE_BOARD_WHEEL;
}

void CWheelBoard::Get7SegDisplays(UINT8 &seg1Digit1, UINT8 &seg1Digit2, UINT8 &seg2Digit1, UINT8 &seg2Digit2) const
{
  seg1Digit1 = m_seg1Digit1;
  seg1Digit2 = m_seg1Digit2;
  seg2Digit1 = m_seg2Digit1;
  seg2Digit2 = m_seg2Digit2;
}

void CWheelBoard::SaveState(CBlockFile *SaveState)
{
  CDriveBoard::SaveState(SaveState);

  SaveState->NewBlock("WheelBoard", __FILE__);
  SaveState->Write(m_simulated);
  {
    // Save DIP switches and digit displays (both modes)
    SaveState->Write(m_dip1);
    SaveState->Write(m_dip2);

    SaveState->Write(m_adcPortRead);
    SaveState->Write(m_adcPortBit);
    SaveState->Write(m_uncenterVal1);
    SaveState->Write(m_uncenterVal2);

    // In a block of its own, so that older save states still load
    SaveState->NewBlock("WheelBoardServo", __FILE__);
    SaveState->Write(m_servoGain);

    // The effects in force, played again when the state is loaded: a game may set its spring once (Sega Rally 2)
    SaveState->NewBlock("WheelBoardEffects", __FILE__);
    SaveState->Write(m_port42Out);
    SaveState->Write(m_port46Out);
    SaveState->Write(m_prev42Out);
    SaveState->Write(m_prev46Out);
    SaveState->Write(m_constForce);
    SaveState->Write(m_vibrate);
    SaveState->Write(m_lastSelfCenter);
    SaveState->Write(m_lastFriction);
    SaveState->Write(m_lastDamper);
  }

  // Simulated: the program's state
  if (m_simulated)
  {
    std::lock_guard<std::mutex> lock(m_indyMutex);
    if (m_servoSim)
    {
      m_servoSim->SaveState(SaveState);
      SaveState->NewBlock("WheelBoardSimTime", __FILE__);
      SaveState->Write(m_servoNmiAcc);
      SaveState->Write(m_servoNextNmi);
    }
    else
    {
      SaveState->NewBlock("WheelBoardIndySim", __FILE__);
      SaveState->Write(m_indy);
    }
  }
}

void CWheelBoard::LoadState(CBlockFile *SaveState)
{
  if (SaveState->FindBlock("WheelBoard") != Result::OKAY)
  {
    return;
  }

  bool wasSimulated;
  bool hasEffects = false;
  SaveState->Read(&wasSimulated, sizeof(wasSimulated));
  if (wasSimulated && !m_simulated)
    // No Z80 state to resume from: CDriveBoard::LoadState halts the board
    ErrorLog("Save state from a simulated drive board, loaded with it emulated (SimulateDriveBoard): the drive board "
             "is halted.");
  {
    // Load DIP switches and digit displays
    SaveState->Read(m_dip1);
    SaveState->Read(m_dip2);

    SaveState->Read(m_adcPortRead);
    SaveState->Read(m_adcPortBit);
    SaveState->Read(m_uncenterVal1);
    SaveState->Read(m_uncenterVal2);

    if (SaveState->FindBlock("WheelBoardServo") == Result::OKAY)
      SaveState->Read(m_servoGain);
    else
      m_servoGain = 0x7F;

    // Older save states: the effects of before the load go on
    hasEffects = SaveState->FindBlock("WheelBoardEffects") == Result::OKAY;
    if (hasEffects)
    {
      SaveState->Read(m_port42Out);
      SaveState->Read(m_port46Out);
      SaveState->Read(m_prev42Out);
      SaveState->Read(m_prev46Out);
      SaveState->Read(m_constForce);
      SaveState->Read(m_vibrate);
      SaveState->Read(m_lastSelfCenter);
      SaveState->Read(m_lastFriction);
      SaveState->Read(m_lastDamper);
    }
  }

  if (m_simulated && m_servoSim)
  {
    // A Model 3 program simulated: its own state, or from an emulated board's save state its Z80's RAM (the base
    // board's state read as emulated, then the simulation going on from it)
    if (wasSimulated)
    {
      std::lock_guard<std::mutex> lock(m_indyMutex);
      m_servoSim->LoadState(SaveState);
      if (SaveState->FindBlock("WheelBoardSimTime") == Result::OKAY)
      {
        SaveState->Read(m_servoNmiAcc);
        SaveState->Read(m_servoNextNmi);
      }
    }
    else
    {
      m_simulated = false;
      CDriveBoard::LoadState(SaveState);
      m_simulated = true;
      std::lock_guard<std::mutex> lock(m_indyMutex);
      m_servoSim->ImportRam(m_ram);
      // The next NMI where the emulated board's timer had it (not the power-up's wait)
      m_servoNmiAcc = 0;
      m_servoNextNmi = m_allowNMIInterupts ? std::max(m_nmiTimerVal, 0) : m_servoSim->FirstNmiCycles();
    }
  }
  else if (m_simulated)
  {
    // EPR-18261 simulated: its own state; from an emulated drive board's save state, the simulation from power-up
    // (the games send their effect again at every frame), the base board's state (its Z80's) of no use
    std::lock_guard<std::mutex> lock(m_indyMutex);
    if (wasSimulated && SaveState->FindBlock("WheelBoardIndySim") == Result::OKAY)
      SaveState->Read(m_indy);
    else
    {
      InfoLog("Save state from an emulated drive board: its simulation starts again from power-up.");
      IndyReset();
    }
  }
  else
    CDriveBoard::LoadState(SaveState);

  if (hasEffects && !IsDisabled())
    ResendForceFeedback();
}

void CWheelBoard::Disable(void)
{
  SendStopAll();
  CDriveBoard::Disable();
}

void CWheelBoard::Reset(void)
{
  // Before CDriveBoard::Reset(), which sets the strength DIP switches
  m_twoPowerOutputs = crc32(0, m_rom, 0x10000) == EPR18261_CRC32;

  CDriveBoard::Reset();

  m_seg1Digit1 = 0xFF;
  m_seg1Digit2 = 0xFF;
  m_seg2Digit1 = 0xFF;
  m_seg2Digit2 = 0xFF;

  m_adcPortRead = 0;
  m_adcPortBit = 0;
  m_port42Out = 0;
  m_port46Out = 0;
  m_prev42Out = 0;
  m_prev46Out = 0;

  m_uncenterVal1 = 0;
  m_uncenterVal2 = 0;

  m_servoGain = 0x7F;
  m_constForce = 0;
  m_vibrate = 0;
  m_powerUp = m_powerDown = 0;
  m_powerSumUp = m_powerSumDown = m_powerNmis = 0;
  m_lastWheel = -1;

  m_lastConstForce = 0;
  m_lastSelfCenter = 0;
  m_lastFriction = 0;
  m_lastVibrate = 0;
  m_lastDamper = 0;

  // SimulateDriveBoard: the program's logic instead of its Z80, for the programs simulated (EPR-18261 here, the
  // Model 3 ones in ServoProgramSim); the others are emulated
  bool simulate = m_config["SimulateDriveBoard"].ValueAsDefault<bool>(false);
  uint32_t crc = crc32(0, m_rom, 0x10000);
  m_servoSim.reset();
  m_servoNmiAcc = 0;
  m_servoNextNmi = 1876648;
  m_simCommand = m_simLoggedCommand = m_simLastReply = 0;
  if (simulate && !m_twoPowerOutputs)
  {
    m_servoSim = MakeServoProgram(crc);
    if (m_servoSim)
    {
      m_servoSim->Reset(m_rom, m_dip1, m_dip2);
      m_servoNextNmi = m_servoSim->FirstNmiCycles();
    }
  }
  m_simulated = simulate && (m_twoPowerOutputs || m_servoSim);
  if (simulate && !m_simulated)
    InfoLog("Drive board: its program (CRC32 %08X) is not simulated yet, it is emulated.", crc);
  {
    std::lock_guard<std::mutex> lock(m_indyMutex);
    IndyReset();
  }

  if (!m_config["ForceFeedback"].ValueAsDefault<bool>(false))
    Disable();

  // Stop any effects that may still be playing
  if (!IsDisabled())
    SendStopAll();
}

UINT8 CWheelBoard::Read(void)
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

void CWheelBoard::Write(UINT8 data)
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
    if (data == 0xCB)
      m_initialized = false;
  }
}

UINT8 CWheelBoard::SimulateRead(void)
{
  if (m_servoSim)
  {
    std::lock_guard<std::mutex> lock(m_indyMutex);
    return m_servoSim->Reply();
  }
  if (m_twoPowerOutputs)
    return IndyReply();
  if (m_initialized)
  {
    switch (m_readMode)
    {
      case 0x0: return m_statusFlags;                    // Status flags
      case 0x1: return m_dip1;                           // DIP switch 1 value
      case 0x2: return m_dip2;                           // DIP switch 2 value
      case 0x3: return m_wheelCenter;                    // Wheel center
      case 0x4: return 0x80;                             // Cockpit banking center
      case 0x5: return (UINT8)m_inputs->steering->value; // Wheel position
      case 0x6: return 0x80;                             // Cockpit banking position
      case 0x7: return m_echoVal;                        // Init status/echo test
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

void CWheelBoard::SimulateWrite(UINT8 cmd)
{
  if (m_servoSim)
  {
    // Queued and shown to the program a frame each, as to the emulated Z80 (CDriveBoard::Write)
    CDriveBoard::Write(cmd);
    if (cmd == 0xCB)
      m_initialized = false;
    return;
  }
  if (m_twoPowerOutputs)
  {
    std::lock_guard<std::mutex> lock(m_indyMutex);
    IndyCommand(cmd);
    return;
  }

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
      SendVibrate(0);
    else
      // Enable uncentering (0x1 = weakest, 0xF = strongest)
      SendVibrate(val * 0x11);
    break;
  case 4: // 0x40-4F Play power-slide sequence
    /* TODO */
    break;
  case 5: // 0x50-5F Rotate wheel right
    SendConstantForce((val + 1) * 0x5);
    break;
  case 6: // 0x60-6F Rotate wheel left
    SendConstantForce(-(val + 1) * 0x5);
    break;
  case 7: // 0x70-7F Set steering parameters
    /* TODO */
    break;
  case 8: // 0x80-8F Test Mode
    switch (val & 0x7)
    {
    case 0:  SendStopAll();                                    break;  // 0x80 Stop motor
    case 1:  SendConstantForce(20);                            break;  // 0x81 Roll wheel right
    case 2:  SendConstantForce(-20);                           break;  // 0x82 Roll wheel left
    case 3:  /* Ignore - no clutch */                          break;  // 0x83 Clutch on
    case 4:  /* Ignore - no clutch */                          break;  // 0x84 Clutch off
    case 5:  m_wheelCenter = (UINT8)m_inputs->steering->value; break;  // 0x85 Set wheel center position
    case 6:  /* Ignore */                                      break;  // 0x86 Set cockpit banking position
    case 7:  /* Ignore */                                      break;  // 0x87 Lamp on/off
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

void CWheelBoard::RunFrame(void)
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

    // The frame's ~9 NMIs run in one go: the wheel only gets the last torque of the frame, so send that one only
    UpdateForceFeedback();
  }
}

void CWheelBoard::SimulateFrame(void)
{
  if (m_servoSim)
  {
    // The frame's NMIs, each perhaps a servo board register written, turned into force feedback as the emulated
    // program's writes are
    // The wheel as the emulated board reads it (80 until the program has answered CC)
    UINT8 wheel = ReadADCChannel2();
    bool newCommand = false;
    {
      std::lock_guard<std::mutex> lock(m_commandMutex);
      if (!m_commands.empty())
      {
        m_dataSent = m_commands.front();
        m_commands.pop_front();
        newCommand = true;
      }
    }
    {
      std::lock_guard<std::mutex> lock(m_indyMutex);
      if (newCommand)
      {
        m_servoSim->SetCommand(m_dataSent);
        ObserveIO(0x28, m_dataSent);
      }
      // The NMIs as the Z80's CTC raises them: every 16000 cycles of 8 MHz from the first, which comes after the
      // program's ROM checksum and RAM test (FirstNmiCycles), the frames 139072 cycles each
      m_servoNmiAcc += GetFramecycles();
      while (m_servoNextNmi <= m_servoNmiAcc)
      {
        m_servoNextNmi += 16000;
        ++m_nmiCount;
        UINT8 mode, value;
        if (m_servoSim->Nmi(wheel, ReadADCChannel3(), mode, value))
        {
          ObserveIO(0x2E, mode);
          ObserveIO(0x2A, value);
          m_port46Out = mode;
          m_port42Out = value;
          ProcessEncoderCmd();
        }
        UINT8 reply = m_servoSim->Reply();
        if (reply != m_simLastReply)
        {
          ObserveIO(0x29, reply);
          m_simLastReply = reply;
          if (reply == 0xCC)
            m_initialized = true;
        }
      }
    }
    UpdateForceFeedback();
    return;
  }
  if (m_twoPowerOutputs)
  {
    int up, down;
    {
      std::lock_guard<std::mutex> lock(m_indyMutex);
      IndyFrame(ReadADCChannel1(), up, down);
    }
    m_powerUp = up;
    m_powerDown = down;
    m_powerNmis = 0;
    UpdateForceFeedback();
    return;
  }
  if (!m_initialized)
    m_initState++;
  // TODO - update m_statusFlags and play preset scripts according to board mode
}

/*
 * EPR-18261 simulated: the logic of its program (Indy 500's drive board, Le Mans 24's cabinet TWIN-NORMAL), from
 * BFF's docs/EPR-18261-Indy500-DriveBoard and sm2-emu's IndyBoard (its author's), run once a frame over its ~8.7
 * NMIs. The program takes a command, works out two motor powers (0-63, "up" raising the wheel's position, to the
 * right) from it and the wheel's position, and answers the game.
 */

void CWheelBoard::IndyReset(void)
{
  // As at power-up: the centre at 80, the base at 2, the effect a stop, no vibration
  m_indy = IndyBoard{};
  m_indy.effect = 0x10;
  m_indy.centre = 0x80;
  m_indy.base = 2;
  m_indy.speedRef = 0x80;
}

void CWheelBoard::IndyCommand(UINT8 data)
{
  // The reply: the command itself (as the program answers), a question's answer, or as it was
  if (data < 0x80)
  {
    m_indy.replyKind = 0;
    m_indy.replyByte = data;
  }
  else if (data == 0x80 || data == 0x83)
    m_indy.replyKind = 1;
  else if (data == 0x81)
  {
    m_indy.replyKind = 0;
    m_indy.replyByte = 0xFF;
  }
  else if (data == 0x82)
    m_indy.replyKind = 2;
  else if (data == 0x84)
    m_indy.replyKind = 3;

  // From 70 up a setting or a question, the effect running on
  switch (data >> 4)
  {
  case 0x7: m_indy.base = (data & 0x0F) / 2 + 2; return;
  case 0x9: m_indy.centre = UINT8((m_indy.centre & 0x0F) | ((data & 0x0F) << 4)); return;
  case 0xA: m_indy.vibPeriod = data & 7; return;
  case 0xB: m_indy.vibAmp = data & 7; return;
  case 0xC: m_indy.centre = UINT8((m_indy.centre & 0xF0) | (data & 0x0F)); return;
  default: break;
  }
  if (data < 0x70)
    m_indy.effect = data;
}

UINT8 CWheelBoard::IndyReply(void) const
{
  switch (m_indy.replyKind)
  {
  case 1: return ReadADCChannel1();
  case 2: return m_dip1;
  case 3: return m_indy.centre;
  default: return m_indy.replyByte;
  }
}

int CWheelBoard::IndyStrength(void) const
{
  // (2n + base) x 2, and the DIP switches' 0 to 7 (measured on a board)
  return (2 * (m_indy.effect & 7) + m_indy.base) * 2 + int(GetForceFeedbackStrength()) - 1;
}

void CWheelBoard::IndySpring(int position, int shift, bool dampOtherSide)
{
  // Towards the centre, the sine table's curve capped at the strength, plus the damping; exactly on the centre the
  // powers stay as they were
  int s = IndyStrength();
  int centre = m_indy.centre;
  if (position < centre)
  {
    m_indy.up = std::min<int>(m_sine[std::min((centre - position) >> shift, 127)], s) + m_indy.speedDown;
    m_indy.down = dampOtherSide ? m_indy.speedUp : 0;
  }
  else if (position > centre)
  {
    m_indy.down = std::min<int>(m_sine[std::min((position - centre) >> shift, 127)], s) + m_indy.speedUp;
    m_indy.up = m_indy.speedDown;
  }
}

void CWheelBoard::IndyRunEffect(int position)
{
  int n = m_indy.effect & 0x0F;
  switch (m_indy.effect >> 4)
  {
  case 0x0:
    if (n == 1 || n >= 8)
    {
      // A reset: no force, base 2, no vibration
      m_indy.up = m_indy.down = 0;
      m_indy.base = 2;
      m_indy.vibPeriod = m_indy.vibAmp = 0;
    }
    else if (n < 6)
    {
      // An idle centring: base 1, the soft spring at strength 0
      m_indy.base = 1;
      UINT8 effect = m_indy.effect;
      m_indy.effect = 0x38;
      IndySpring(position, 2, false);
      m_indy.effect = effect;
    }
    break;    // 06, 07: the powers stay
  case 0x1:   // stop
    m_indy.up = m_indy.down = 0;
    m_indy.vibPeriod = m_indy.vibAmp = 0;
    break;
  case 0x2:   // both ways at the strength one NMI in two: the motor brakes (the frame's mean)
    m_indy.up = m_indy.down = IndyStrength() / 2;
    break;
  case 0x3:   // 30-37 the spring, 38-3F the soft spring
    if (n < 8)
      IndySpring(position, 1, true);
    else
      IndySpring(position, 2, false);
    break;
  case 0x4:   // away from the centre
    if (position < m_indy.centre)
    {
      m_indy.down = IndyStrength();
      m_indy.up = 0;
    }
    else
    {
      m_indy.up = IndyStrength();
      m_indy.down = 0;
    }
    break;
  case 0x5:   // push down (left)
    m_indy.down = IndyStrength();
    m_indy.up = 0;
    break;
  case 0x6:   // push up (right)
    m_indy.up = IndyStrength();
    m_indy.down = 0;
    break;
  default:
    break;
  }
}

void CWheelBoard::IndyFrame(int position, int &up, int &down)
{
  // The frame's NMIs (500 Hz)
  m_indy.nmiAcc += 500.0 / 57.524;
  int elapsed = int(m_indy.nmiAcc);
  m_indy.nmiAcc -= elapsed;
  m_indy.nmis += UINT32(elapsed);

  // Every 18 NMIs, how far the wheel went down or up (none with DIP switch 2-1 ON, bit 7 at 0)
  m_indy.speedNmis += elapsed;
  if (m_indy.speedNmis >= 18)
  {
    m_indy.speedNmis %= 18;
    bool damped = (m_dip2 & 0x80) != 0;
    m_indy.speedDown = damped && position < m_indy.speedRef ? m_indy.speedRef - position : 0;
    m_indy.speedUp = damped && position > m_indy.speedRef ? position - m_indy.speedRef : 0;
    m_indy.speedRef = position;
  }

  IndyRunEffect(position);

  // A vibration: an extra push one way then the other, each half 16 x its period NMIs, the damping the other way
  up = m_indy.up;
  down = m_indy.down;
  if (m_indy.vibPeriod != 0 && m_indy.vibAmp != 0)
  {
    int extra = m_indy.vibAmp + std::abs(int(m_indy.centre) - position) / 8 + 1;
    if (((m_indy.nmis >> 4) / UINT32(m_indy.vibPeriod)) % 2 != 0)
    {
      up += extra;
      down += m_indy.speedUp;
    }
    else
    {
      up += m_indy.speedDown;
      down += extra;
    }
  }
  up = std::min(up, 63);
  down = std::min(down, 63);
}

UINT8 CWheelBoard::IORead8(UINT32 portNum)
{
  UINT8 adcVal;

  switch (portNum)
  {
  case 0x20: // DIP 1 value
    return m_dip1;
  case 0x21: // DIP 2 value
    return m_dip2;
  case 0x24: // ADC channel 1 - Y analog axis for joystick, steering wheel position on EPR-18261
  case 0x25: // ADC channel 2 - steering wheel position (0x00 = full left, 0x80 = center, 0xFF = full right) and X analog axis for joystick
  case 0x26: // ADC channel 3 - cockpit bank position (deluxe cabinets) (0x00 = full left, 0x80 = center, 0xFF = full right)
  case 0x27: // ADC channel 4 - not connected
    if (portNum == m_adcPortRead && m_adcPortBit-- > 0)
    {
      switch (portNum)
      {
      case 0x24: // Y analog axis for joystick
        adcVal = ReadADCChannel1();
        break;
      case 0x25: // Steering wheel for twin racing cabinets - TODO - check actual range of steering, suspect it is not really 0x00-0xFF
        adcVal = ReadADCChannel2();
        {
          // The byte the program puts together from the 8 bits (agent mode's journal: a port read as 80|port)
          static UINT8 assembled;
          assembled = UINT8((assembled << 1) | ((adcVal >> m_adcPortBit) & 1));
          if (m_adcPortBit == 0)
            ObserveIO(0xA5, assembled);
        }
        break;
      case 0x26: // Cockpit bank position for deluxe racing cabinets
        adcVal = ReadADCChannel3();
        break;
      case 0x27: // Not connected
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
    ObserveIO(0xA8, m_dataSent);
    return m_dataSent;
  case 0x2c: // Encoder error reporting (kept at 0x00 for no error)
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

void CWheelBoard::IOWrite8(UINT32 portNum, UINT8 data)
{
  CDriveBoard::IOWrite8(portNum, data);

  switch (portNum)
  {
  case 0x1c: // Unsure? - two bytes 0xFF, 0xFF sent at initialization only
  case 0x1d: // Unsure? - two bytes 0x0F, 0x17 sent at initialization only
  case 0x1e: // Unsure? - same as port 28
  case 0x1f: // Unsure? - same as port 31
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
    if (data == 0xCC)
      m_initialized = true;
    return;
  case 0x2a: // Encoder motor data (x axis), or EPR-18261's motor power "up"
    m_port42Out = data;
    if (m_twoPowerOutputs)
      ProcessMotorPowers();
    else
      ProcessEncoderCmd();
    return;
  case 0x2d: // Clutch/lamp control (deluxe cabinets) ( or y axis)
    return;
  case 0x2e: // Encoder motor control, or EPR-18261's motor power "down"
    m_port46Out = data;
    if (m_twoPowerOutputs)
    {
      ProcessMotorPowers();
      // Written at every NMI, after "up": the NMI's sample
      m_powerSumUp += m_powerUp;
      m_powerSumDown += m_powerDown;
      m_powerNmis++;
    }
    return;
  default:
    DebugLog("Unhandled Z80 output on port %u (at PC = %04X)\n", portNum, m_z80.GetPC());
    return;
  }
}

void CWheelBoard::ProcessEncoderCmd(void)
{
  if (m_prev42Out != m_port42Out || m_prev46Out != m_port46Out)
  {
    //DebugLog("46 [%02X] / 42 [%02X]\n", m_port46Out, m_port42Out);
    switch (m_port46Out)
    {
      case 0xFB:
        // Torque gain, 0x80 (none) to 0xFF (full) [guess, from the programs]: set by the game's 2x commands within
        // the curve its 7x command picked (Scud Race, Daytona 2), half after a power slide, 0xFF in Sega Rally 2.
        // 0x05, written only at start-up, is not a gain
        if (m_port42Out & 0x80)
          m_servoGain = m_port42Out & 0x7F;
        break;

      case 0xFC:
        // Sub-register in bits 0-2
        if (m_port42Out&0x04)
        {
          // 4: centering
          // Bit 7 = on for disable, off for enable
          if (m_port42Out&0x80)
          {
            // Disable centering
            SendSelfCenter(0);
          }
          else
          {
            // Bits 3-6 = the servo board's spring gain, 1 to 15 (0C-7C). Its spring is a stiff position loop: on a
            // cabinet it is strong at the gains the games send (5 to 8 in Dirt Devils, with DIP row 4), while a
            // DirectInput spring grows with the angle over the wheel's whole travel. 47 % at 5, 75 % at 8, full from
            // 11: full from 8 made the wheel oscillate on a cabinet
            UINT8 strength = (UINT8)std::min(((m_port42Out&0x78)>>3) * 0x18, 0xFF);
            SendSelfCenter(strength);
          }
        }
        else
        {
          // 0-3: vibration (the game's 3x commands). Each sub-register holds a nibble, in bits 4-7: 0 and 1 the low
          // and high nibbles of a first byte (0x0E, 0x10, 0x12 for 31-35, 36-3A, 3B-3F), 2 and 3 those of a
          // second one (0x0A to 0x12 within each group); 30, and the mode changes (FC 0A, FC 0B), zero them. The
          // programs write 0 to 3 in turn: the vibration is set when 3 is written
          UINT8 subRegister = m_port42Out&0x03;
          UINT8 nibble = m_port42Out>>4;
          UINT8 &param = (subRegister & 2) ? m_uncenterVal2 : m_uncenterVal1;
          if (subRegister & 1)
            param = (param & 0x0F) | (nibble << 4);
          else
            param = (param & 0xF0) | nibble;
          if (subRegister == 3)
          {
            if (m_uncenterVal1 == 0 || m_uncenterVal2 == 0)
              m_vibrate = 0;
            else
            {
              // What the two bytes do on the servo board is unknown: 31-3F are mapped to 0x0F-0xEF, in 15 steps
              int strength = ((m_uncenterVal1>>1) - 7) * 0x50 + ((m_uncenterVal2>>1) - 5) * 0x10 + 0xF;
              m_vibrate = (UINT8)std::clamp(strength, 0, 0xFF);
            }
          }
        }
        break;

      case 0xFD:
        // [guess] A damping: with each spring the high nibble grows with its gain (11 to 51 with FC 4 0C to 7C on
        // Scud Race; 11 to 51 by the strength row on Sega Rally 2, 11 with each of its torques), 00 with Scud's
        // torques and always on Daytona 2, 66 at start-up. Without it the spring oscillated on a cabinet. Sent as a
        // friction, which a wheel feeder may play as a force against the wheel's speed: 1 = 12 %, 5 = 62 %
        SendFriction((UINT8)std::min((m_port42Out >> 4) * 0x20, 0xFF));
        break;

      case 0xFE:
        // Apply constant force to wheel
        // Value is: 0x80 = stop motor, 0x81-0xC0 = roll wheel left, 0x40-0x7F = roll wheel right, scale to range -0x80-0x7F
        // Note: seems to often output 0x7F or 0x81 for stop motor, so narrowing wheel ranges to 0x40-0x7E and 0x82-0xC0
        if (m_port42Out > 0x81)
        {
          if (m_port42Out <= 0xC0)
            m_constForce = 2 * (0x81 - m_port42Out);
          else
            m_constForce = -0x80;
        }
        else if (m_port42Out < 0x7F)
        {
          if (m_port42Out >= 0x40)
            m_constForce = 2 * (0x7F - m_port42Out);
          else
            m_constForce = 0x7F;
        }
        else
          m_constForce = 0;
        break;

      case 0xFF:
        // Motor idle, whatever the value: FF FF, and FF 80 after the attract mode's centring of the wheel
        SendStopAll();
        break;

      default:
        //DebugLog("Unknown = 46 [%02X] / 42 [%02X]\n", m_port46Out, m_port42Out);
        break;
    }

    m_prev42Out = m_port42Out;
    m_prev46Out = m_port46Out;
  }
}

void CWheelBoard::ProcessMotorPowers(void)
{
  // EPR-18261 writes each power, 0-63, with its bits reversed and complemented. "Up" raises the wheel position
  // (to the right), "down" lowers it; both at once brake the motor
  auto power = [](UINT8 port)
  {
    UINT8 bits = ~port;
    UINT8 value = 0;
    for (int i = 0; i < 8; i++)
      value |= ((bits >> i) & 1) << (7 - i);
    return value & 0x3F;
  };

  m_powerUp = power(m_port42Out);
  m_powerDown = power(m_port46Out);

  // 63 is the full force, as 0x80 +- 63 is on the servo boards
  m_constForce = 2 * (m_powerUp - m_powerDown);
}

void CWheelBoard::UpdateForceFeedback(void)
{
  // The servo board's gain (FB: the game's MOTOR POWER or STEERING FORCE, through its 7x and 2x commands) scales the
  // torque and the vibration, not the servo board's own spring. Dirt Devils vibrates half of a race at 50 to 81 %:
  // without the gain, its vibration was stronger than on a cabinet. Not the frame's strongest torque: Sega Rally 2's
  // road is its torque on for 8 NMIs, off for 8, which the last torque of each frame keeps (on one frame, off the
  // next) and the strongest filled in (on all the time)
  if (m_twoPowerOutputs)
    SendMotorPowers();
  else
    SendConstantForce(m_constForce * m_servoGain / 0x7F);
  SendVibrate(m_vibrate * m_servoGain / 0x7F);
}

void CWheelBoard::SendMotorPowers(void)
{
  // EPR-18261, as sm2-emu plays it (its author's description; the same board there, rewritten, and here its program):
  // the two powers averaged over the frame's NMIs (the last NMI's would fold the brake of 2x, both powers one NMI in
  // two, and the vibration into noise from frame to frame), up pushing to the right, and what both have in common
  // braking the motor: a force against the wheel's speed, full from 4 steps of 255 a frame (sm2-emu: 1024 of 65536 of
  // the device's axis, which Supermodel maps whole onto the 0-255 the board reads: the same speed, by 8-bit steps)
  float up = float(m_powerUp), down = float(m_powerDown);
  if (m_powerNmis > 0)
  {
    up = float(m_powerSumUp) / m_powerNmis;
    down = float(m_powerSumDown) / m_powerNmis;
  }
  m_powerSumUp = m_powerSumDown = m_powerNmis = 0;
  int wheel = m_inputs && m_inputs->steering ? int(m_inputs->steering->value) : -1;
  float speed = (wheel >= 0 && m_lastWheel >= 0) ? std::clamp((wheel - m_lastWheel) / 4.0f, -1.0f, 1.0f) : 0.0f;
  m_lastWheel = wheel;
  float force = (up - down) - std::min(up, down) * speed;
  // 63 the full force: x 2 to the -128..127 of a constant force
  int val = int(std::lround(force * 2.0f)) * m_servoGain / 0x7F;
  SendConstantForce(INT8(std::clamp(val, -128, 127)));
}

void CWheelBoard::ResendForceFeedback(void)
{
  if (IsDisabled() || !m_inputs || !m_inputs->steering)
    return;

  // A state loaded, or the joystick that plays the effects changed: the wheel still plays the effects of before, and a
  // game may set its spring once (Sega Rally 2), so the board's effects are played again from scratch
  UINT8 selfCenter = m_lastSelfCenter;
  UINT8 friction = m_lastFriction;
  UINT8 damper = m_lastDamper;
  INT8 constForce = m_constForce;
  UINT8 vibrate = m_vibrate;
  SendStopAll();
  m_constForce = constForce;
  m_vibrate = vibrate;
  SendSelfCenter(selfCenter);
  SendFriction(friction);
  SendDamper(damper);
  SendConstantForce(m_constForce * m_servoGain / 0x7F);
  SendVibrate(m_vibrate * m_servoGain / 0x7F);
}


void CWheelBoard::SendStopAll(void)
{
  //DebugLog(">> Stop All Effects\n");

  ForceFeedbackCmd ffCmd{};
  ffCmd.id = FFStop;

  if (m_inputs) {
      if (m_inputs->steering) {
          m_inputs->steering->SendForceFeedbackCmd(ffCmd);
      }
  }
  
  m_constForce     = 0;
  m_vibrate        = 0;
  m_powerSumUp = m_powerSumDown = m_powerNmis = 0;
  m_lastConstForce = 0;
  m_lastSelfCenter = 0;
  m_lastFriction   = 0;
  m_lastVibrate    = 0;
  m_lastDamper     = 0;
}

void CWheelBoard::SendConstantForce(INT8 val)
{
  if (val == m_lastConstForce)
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

  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastConstForce = val;
}

void CWheelBoard::SendSelfCenter(UINT8 val)
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

  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastSelfCenter = val;
}


void CWheelBoard::SendFriction(UINT8 val)
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
  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastFriction = val;
}

void CWheelBoard::SendVibrate(UINT8 val)
{
  if (val == m_lastVibrate)
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
  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastVibrate = val;
}

void CWheelBoard::SendDamper(UINT8 val)
{
  if (val == m_lastDamper)
    return;

  ForceFeedbackCmd ffCmd;
  ffCmd.id = FFDamper;
  ffCmd.force = (float)val / 255.0f;
  m_inputs->steering->SendForceFeedbackCmd(ffCmd);

  m_lastDamper = val;
}

uint8_t CWheelBoard::ReadADCChannel1() const
{
  if (m_twoPowerOutputs)
    return (UINT8)m_inputs->steering->value;
  return 0x00;
}

uint8_t CWheelBoard::ReadADCChannel2() const
{
  if (m_initialized)
    return (UINT8)m_inputs->steering->value;
  else
    return 0x80; // If not initialized, return 0x80 so that wheel centering test does not fail
}

uint8_t CWheelBoard::ReadADCChannel3() const
{
  return 0x80;
}

uint8_t CWheelBoard::ReadADCChannel4() const
{
  return 0x00;
}

unsigned CWheelBoard::GetForceFeedbackStrength() const
{
  // EPR-18261: bits 5-7 of DIP switches 1 add 0 to 7 to every strength (switches 1-1..1-3 on the board, switch n
  // being bit 8 - n, measured)
  if (m_twoPowerOutputs)
    return ((~(m_dip1 >> 5)) & 7) + 1;
  return CDriveBoard::GetForceFeedbackStrength();
}

void CWheelBoard::SetForceFeedbackStrength(unsigned strength)
{
  if (m_twoPowerOutputs)
    m_dip1 = (m_dip1 & 0x1F) | (((~(strength - 1)) & 7) << 5);
  else
    CDriveBoard::SetForceFeedbackStrength(strength);
}

CWheelBoard::CWheelBoard(const Util::Config::Node &config)
  : CDriveBoard(config)
{
  m_dip1 = 0xCF;
  m_dip2 = 0xFF;

  m_seg1Digit1 = 0;
  m_seg1Digit2 = 0;
  m_seg2Digit1 = 0;
  m_seg2Digit2 = 0;

  m_adcPortRead = 0;
  m_adcPortBit = 0;

  m_port42Out = 0;
  m_port46Out = 0;

  m_prev42Out = 0;
  m_prev46Out = 0;

  m_uncenterVal1 = 0;
  m_uncenterVal2 = 0;

  m_twoPowerOutputs = false;
  m_servoGain = 0x7F;
  m_constForce = 0;
  m_vibrate = 0;
  m_powerUp = m_powerDown = 0;
  m_powerSumUp = m_powerSumDown = m_powerNmis = 0;
  m_lastWheel = -1;

  // Feedback state
  m_lastConstForce = 0;
  m_lastSelfCenter = 0;
  m_lastFriction = 0;
  m_lastVibrate = 0;
  m_lastDamper = 0;

  // The program's sine table (its positive half, 80 at the peak): 128 sin(pi i / 128), its integer part, exactly
  for (int i = 0; i < 128; i++)
    m_sine[i] = UINT8(128.0 * std::sin(3.14159265358979323846 * i / 128.0) + 1e-9);
  IndyReset();

  DebugLog("Built Drive Board (wheel)\n");
}

CWheelBoard::~CWheelBoard(void)
{

}
