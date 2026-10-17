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
 * JoyBoard.h
 *
 * Header for the CJoyBoard (force feedback emulation for joystick) class.
 */

#ifndef INCLUDED_JOYBOARD_H
#define INCLUDED_JOYBOARD_H

#include "DriveBoard.h"
#include "ServoProgramSim.h"
#include "Util/NewConfig.h"
#include "Game.h"

#include <memory>
#include <mutex>

/*
 * CJoyBoard
 */
class CJoyBoard : public CDriveBoard
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
   * CDriveBoard(config):
   * ~CDriveBoard():
   *
   * Constructor and destructor. Memory is freed by destructor.
   *
   * Paramters:
   *    config  Run-time configuration. The reference should be held because
   *            this changes at run-time.
   */
  CJoyBoard(const Util::Config::Node &config);
  ~CJoyBoard(void);

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

protected:
  void Disable(void);

private:

  UINT8 m_seg1Digit1;   // Current value of left digit on 7-segment display 1
  UINT8 m_seg1Digit2;   // Current value of right digit on 7-segment display 1
  UINT8 m_seg2Digit1;   // Current value of left digit on 7-segment display 2
  UINT8 m_seg2Digit2;     // Current value of right digit on 7-segment display 2

  UINT16 m_adcPortRead;   // ADC port currently reading from
  UINT8 m_adcPortBit;     // Bit number currently reading on ADC port

  // The two servo boards: board 1 drives the X axis (read on ADC channel 3), its value on Z80 I/O port 2A; board 2
  // the Y axis (ADC channel 4), its value on port 2D. Their registers share port 2E
  struct Servo
  {
    UINT8 value;          // Last value sent to its port, before its register
    UINT8 vibrateVal[2];  // Vibration bytes, assembled nibble by nibble (register FC, sub-registers 0-3)
    UINT8 gain;           // Torque gain, 0-0x7F (register FB)
    INT8 constForce;      // Torque (register FE), positive raising the axis
    UINT8 vibrate;        // Vibration strength
  };
  Servo m_servo[2];

  // Feedback state
  INT8 m_lastConstForce[2]; // Last constant force command sent, by axis
  UINT8 m_lastSelfCenter;   // Last self center command sent
  UINT8 m_lastFriction;     // Last friction command sent
  UINT8 m_lastVibrate[2];   // Last vibrate command sent, by axis

  // SimulateDriveBoard: the program's logic (ServoProgramSim) instead of its Z80, the Z80's cycles run and the next
  // NMI's, the last reply (for the agent's journal)
  std::unique_ptr<IJoystickProgram> m_joySim;
  std::mutex m_simMutex;
  double m_joyNmiAcc = 0;
  double m_joyNextNmi = 0;
  UINT8 m_simLastReply = 0;

  UINT8 SimulateRead(void);

  void SimulateFrame(void);

  void ResetServos(void);

  void ProcessServoRegister(int servoNum, UINT8 reg);

  void UpdateForceFeedback(void);

  void SendStopAll(void);

  void SendConstantForce(int axisNum, INT8 val);

  void SendSelfCenter(UINT8 val);

  void SendFriction(UINT8 val);

  void SendVibrate(int axisNum, UINT8 val);

  uint8_t ReadADCChannel1() const;

  uint8_t ReadADCChannel2() const;

  uint8_t ReadADCChannel3() const;

  uint8_t ReadADCChannel4() const;
};

#endif  // INCLUDED_JOYBOARD_H
