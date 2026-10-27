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
 * WheelBoard.h
 *
 * Header for the CWheelBoard (force feedback emulation for wheel) class.
 */

#ifndef INCLUDED_WHEELBOARD_H
#define INCLUDED_WHEELBOARD_H

#include "DriveBoard.h"
#include "ServoProgramSim.h"
#include "Util/NewConfig.h"
#include "Game.h"
#include <mutex>

/*
 * CWheelBoard
 */
class CWheelBoard : public CDriveBoard
{
public:
  /*
   * GetType(void):
   *
   * Returns:
   *    Drive board type.
   */
  Game::DriveBoardType GetType(void) const;

  /*
   * Get7SegDisplays(seg1Digit1, seg1Digit2, seg2Digit1, seg2Digit2):
   *
   * Reads the 7-segment displays.
   *
   * Parameters:
   *    seg1Digit1  Reference of variable to store digit 1 of the first 7-
   *          segment display to.
   *    seg1Digit2  First display, second digit.
   *    seg2Digit1  Second display, first digit.
   *    seg2Digit2  Second display, second digit.
   */
  void Get7SegDisplays(UINT8 &seg1Digit, UINT8 &seg1Digit2, UINT8 &seg2Digit1, UINT8 &seg2Digit2) const;


  /*
   * SaveState(SaveState):
   *
   * Saves the drive board state.
   *
   * Parameters:
   *    SaveState Block file to save state information to.
   */
  void SaveState(CBlockFile *SaveState);

  /*
   * LoadState(SaveState):
   *
   * Restores the drive board state.
   *
   * Parameters:
   *    SaveState Block file to load save state information from.
   */
  void LoadState(CBlockFile *SaveState);

  /*
   * Reset(void):
   *
   * Resets the drive board.
   */
  void Reset(void);

  /*
   * Read():
   *
   * Reads data from the drive board.
   *
   * Returns:
   *    Data read.
   */
  UINT8 Read(void);

  /*
   * Write(data):
   *
   * Writes data to the drive board.
   *
   * Parameters:
   *    data  Data to send.
   */
  void Write(UINT8 data);

  /*
   * RunFrame(void):
   *
   * Emulates a single frame's worth of time on the drive board.
   */
  void RunFrame(void);

  /*
   * CWheelBoard(config):
   * ~CWheelBoard():
   *
   * Constructor and destructor. Memory is freed by destructor.
   *
   * Paramters:
   *    config  Run-time configuration. The reference should be held because
   *            this changes at run-time.
   */
  CWheelBoard(const Util::Config::Node &config);
  ~CWheelBoard(void);

  /*
   * Read8(addr):
   * IORead8(portNum):
   *
   * Methods for reading from Z80's memory and IO space. Required by CBus.
   *
   * Parameters:
   *    addr    Address in memory (0-0xFFFF).
   *    portNum   Port address (0-255).
   *
   * Returns:
   *    A byte of data from the address or port.
   */
  UINT8 IORead8(UINT32 portNum);

  /*
   * Write8(addr, data):
   * IORead8(portNum, data):
   *
   * Methods for writing to Z80's memory and IO space. Required by CBus.
   *
   * Parameters:
   *    addr    Address in memory (0-0xFFFF).
   *    portNum   Port address (0-255).
   *    data    Byte to write.
   */
  void IOWrite8(UINT32 portNum, UINT8 data);

  /*
   * GetForceFeedbackStrength(void):
   * SetForceFeedbackStrength(strength):
   *
   * The strength DIP switches: bits 2-4 of DIP switches 1 on the boards
   * that drive a servo board, bits 5-7 on EPR-18261.
   */
  unsigned GetForceFeedbackStrength(void) const;
  void SetForceFeedbackStrength(unsigned strength);

  void ResendForceFeedback(void);

protected:
  void Disable(void);

private:

  UINT8 m_seg1Digit1;   // Current value of left digit on 7-segment display 1
  UINT8 m_seg1Digit2;   // Current value of right digit on 7-segment display 1
  UINT8 m_seg2Digit1;   // Current value of left digit on 7-segment display 2
  UINT8 m_seg2Digit2;     // Current value of right digit on 7-segment display 2

  UINT16 m_adcPortRead;   // ADC port currently reading from
  UINT8 m_adcPortBit;     // Bit number currently reading on ADC port

  UINT8 m_port42Out;      // Last value sent to Z80 I/O port 42 (encoder motor data)
  UINT8 m_port46Out;      // Last value sent to Z80 I/O port 46 (encoder motor control)

  UINT8 m_prev42Out;      // Previous value sent to Z80 I/O port 42
  UINT8 m_prev46Out;      // Previous value sent to Z80 I/O port 46

  UINT8 m_uncenterVal1;   // Vibration, first byte: servo board sub-registers FC 0 (low nibble) and 1 (high nibble)
  UINT8 m_uncenterVal2;   // Vibration, second byte: sub-registers FC 2 (low nibble) and 3 (high nibble)

  bool m_twoPowerOutputs; // EPR-18261: ports 2A and 2E are two motor powers, not a servo board register and value
  UINT8 m_servoGain;      // Servo board torque gain (register FB), 0-127

  // What the board asks for, sent to the wheel once a frame
  INT8 m_constForce;      // Torque, before the gain
  UINT8 m_vibrate;        // Vibration, before the gain
  // EPR-18261: its two powers (0-63), summed over the frame's NMIs (one sample at each write of "down")
  int m_powerUp, m_powerDown;
  int m_powerSumUp, m_powerSumDown, m_powerNmis;
  int m_lastWheel;        // The wheel's position at the last frame (0-255), -1 unknown: its speed brakes

  // Feedback state
  INT8 m_lastConstForce;  // Last constant force command sent
  UINT8 m_lastSelfCenter; // Last self center command sent
  UINT8 m_lastFriction;   // Last friction command sent
  UINT8 m_lastVibrate;    // Last vibrate command sent
  UINT8 m_lastDamper;     // Last damper command sent

  // EPR-18261 simulated (SimulateDriveBoard): its program's logic, once a frame instead of its Z80
  struct IndyBoard
  {
    UINT8 effect;         // The last command below 70
    UINT8 centre;         // 9x its high nibble, Cx its low
    int base;             // 7x: x / 2 + 2; 1 after an idle centring
    int vibPeriod;        // Ax & 7: the vibration's half period, in 16 NMIs; 0 none
    int vibAmp;           // Bx & 7; 0 none
    int up, down;         // The powers the effect asks for, kept when it leaves them be
    int speedDown, speedUp, speedRef, speedNmis;  // How far the wheel moved over the last 18 NMIs
    double nmiAcc;        // The NMIs of the frames, their fraction carried over
    UINT32 nmis;          // The NMI counter
    UINT8 replyKind;      // What the reply is: 0 a byte, 1 the wheel, 2 DIP switches 1, 3 the centre
    UINT8 replyByte;
  };
  IndyBoard m_indy;
  // The other programs simulated (the Model 3 drive boards, ServoProgramSim)
  std::unique_ptr<IServoProgram> m_servoSim;
  double m_servoNmiAcc;   // The Z80's cycles run (simulated), and the next NMI's
  double m_servoNextNmi;
  UINT8 m_simCommand, m_simLoggedCommand, m_simLastReply;  // For the agent's journal, as the Z80's
  std::mutex m_indyMutex; // The game's commands (its thread) against the frames (the drive board's)
  UINT8 m_sine[128];      // The program's sine table: 128 sin(pi i / 128), its integer part

  void IndyReset(void);
  void IndyCommand(UINT8 data);
  UINT8 IndyReply(void) const;
  int IndyStrength(void) const;
  void IndySpring(int position, int shift, bool dampOtherSide);
  void IndyRunEffect(int position);
  void IndyFrame(int position, int &up, int &down);

  UINT8 SimulateRead(void);

  void SimulateWrite(UINT8 data);

  void SimulateFrame(void);

  void ProcessEncoderCmd(void);

  void ProcessMotorPowers(void);

  void UpdateForceFeedback(void);

  void SendMotorPowers(void);

  void SendStopAll(void);

  void SendConstantForce(INT8 val);

  void SendSelfCenter(UINT8 val);

  void SendFriction(UINT8 val);

  void SendVibrate(UINT8 val);

  void SendDamper(UINT8 val);

  uint8_t ReadADCChannel1() const;

  uint8_t ReadADCChannel2() const;

  uint8_t ReadADCChannel3() const;

  uint8_t ReadADCChannel4() const;
};

#endif  // INCLUDED_WHEELBOARD_H
