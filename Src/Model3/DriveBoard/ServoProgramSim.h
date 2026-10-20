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
 * ServoProgramSim.h
 *
 * The Model 3 drive boards' programs simulated (SimulateDriveBoard): instead of
 * running a program on the drive board's Z80, its logic, NMI by NMI, giving the
 * same servo board register writes (a register on port 2E, its value on port
 * 2A, one pair an NMI) and the same replies to the main board. CWheelBoard
 * turns the pairs into force feedback as it does the emulated program's. The
 * programs' tables are read from their ROM, which is needed either way.
 */

#ifndef INCLUDED_SERVOPROGRAMSIM_H
#define INCLUDED_SERVOPROGRAMSIM_H

#include "Types.h"
#include "BlockFile.h"

#include <cstdint>
#include <memory>

class IServoProgram
{
public:
  virtual ~IServoProgram() = default;

  // As at power-up, with the program's ROM (64 KB) and the DIP switches (ON reads 0)
  virtual void Reset(const UINT8 *rom, UINT8 dip1, UINT8 dip2) = 0;

  // The byte the main board puts on the command port (28); the program samples it at every NMI
  virtual void SetCommand(UINT8 data) = 0;

  // The byte on the reply port (29)
  virtual UINT8 Reply(void) const = 0;

  // The Z80 cycles (8 MHz) from reset to the first NMI: the program's ROM checksum and RAM test, then its CTC. As
  // the emulated Z80 has it (measured): its NMI timer starts with the frame after the CTC's programming, the first
  // NMI 16003 cycles into it
  virtual double FirstNmiCycles(void) const = 0;

  // One NMI (500 Hz), the wheel at `wheel` (0 left .. FF right), the cockpit at `cockpit` (deluxe cabinets): true
  // and the pair written to the servo board (register, value) if the program writes one
  virtual bool Nmi(UINT8 wheel, UINT8 cockpit, UINT8 &mode, UINT8 &value) = 0;

  virtual void SaveState(CBlockFile *saveState) = 0;
  virtual void LoadState(CBlockFile *saveState) = 0;

  // A save state of the emulated board loaded: the program's state taken from its Z80's RAM (8 KB, E000-FFFF), the
  // game going on without starting the board again
  virtual void ImportRam(const UINT8 *ram) = 0;
};

// The simulation of the program with this CRC32, or none (that program is emulated)
std::unique_ptr<IServoProgram> MakeServoProgram(uint32_t crc32);

/*
 * A joystick board's program (Star Wars Trilogy's EPR-21119): two servo boards, their values on ports 2A and 2D,
 * their registers' nibbles on port 2E. An NMI in two halves: the outputs (with the reply), then the axes sampled,
 * which Supermodel shows centred until the board has replied CB (in that very NMI).
 */
class IJoystickProgram
{
public:
  virtual ~IJoystickProgram() = default;
  virtual void Reset(const UINT8 *rom, UINT8 dip1, UINT8 dip2) = 0;
  virtual void SetCommand(UINT8 data) = 0;
  virtual UINT8 Reply(void) const = 0;
  virtual double FirstNmiCycles(void) const = 0;

  // The NMI's first half: the command, the reply (Reply() after it), the values of ports 2A, 2D and 2E
  virtual void NmiOutputs(UINT8 &tq1, UINT8 &tq2, UINT8 &mode) = 0;

  // Its second half: the axes (ADC channels 3 and 4), the timers, then the main code until the next NMI
  virtual void NmiInputs(UINT8 axis1, UINT8 axis2) = 0;

  virtual void SaveState(CBlockFile *saveState) = 0;
  virtual void LoadState(CBlockFile *saveState) = 0;
  virtual void ImportRam(const UINT8 *ram) = 0;
};

std::unique_ptr<IJoystickProgram> MakeJoystickProgram(uint32_t crc32);

#endif  // INCLUDED_SERVOPROGRAMSIM_H
