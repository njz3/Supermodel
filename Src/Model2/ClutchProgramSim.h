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
 * ClutchProgramSim.h
 *
 * The programs of the drive boards that drive a motor through a clutch
 * (CModel2DriveBoard's: Virtua Racing's EPR-15111, Daytona USA's EPR-16488A,
 * Sega Rally's EPR-17891) simulated (SimulateDriveBoard, or the ROM missing):
 * instead of running the program on the board's Z80, its logic, interrupt by
 * interrupt, giving the same motor byte (port 46) and the same reply to the
 * main board (port 23). CModel2DriveBoard turns the motor byte into force
 * feedback as it does the emulated program's.
 *
 * Written from the behavioural specs pkg/autopilot_data/tools_driveboard/
 * spec_15111.md, spec_16488a.md and spec_17891.md (the EPROMs' annotated
 * listings in BackForceFeeder's docs), with sm2-emu's DaytonaBoard and VrBoard
 * (their author's, njz3) as the starting point.
 */

#ifndef INCLUDED_CLUTCHPROGRAMSIM_H
#define INCLUDED_CLUTCHPROGRAMSIM_H

#include "Types.h"
#include "BlockFile.h"

#include <cstdint>
#include <memory>

class IClutchProgram
{
public:
  virtual ~IClutchProgram() = default;

  // As at power-up, with the DIP switch banks as the Z80 reads them (a switch ON reads 0): EPR-16488A and EPR-17891
  // read one bank, port 26 (dsw1; dsw2 unused); EPR-15111 two, DSW1 on port 25 and DSW2 on port 26
  virtual void Reset(UINT8 dsw1, UINT8 dsw2) = 0;

  // The Z80 cycles (4 MHz) from reset to the timer expiry before the program's first interrupt (the timer runs every
  // 4096 cycles from reset; the program takes its first interrupt at its EI, a little after): Interrupt() is called at
  // that expiry and every 4096 cycles after, as CModel2DriveBoard's emulated Z80 has them
  virtual double FirstInterruptCycles(void) const = 0;

  // One interrupt, with what the Z80 reads during it: the command on the link (port 27; EPR-15111's player 1) and the
  // wheel (ADC channel 0); then the main program's work until the next interrupt
  virtual void Interrupt(UINT8 command, UINT8 wheel) = 0;

  // The motor byte (port 46) and the reply (port 23) as they stand after the last Interrupt()
  virtual UINT8 Motor(void) const = 0;
  virtual UINT8 Reply(void) const = 0;

  virtual void SaveState(CBlockFile *saveState) = 0;
  virtual void LoadState(CBlockFile *saveState) = 0;
};

// The programs' CRC32 (their 64 KB ROM images)
static constexpr uint32_t EPR15111_CRC32 = 0x45C02763;
static constexpr uint32_t EPR16488A_CRC32 = 0x546C5D1A;
static constexpr uint32_t EPR17891_CRC32 = 0x9A33B437;

// The simulation of the program with this CRC32, or none (that program is emulated)
std::unique_ptr<IClutchProgram> MakeClutchProgram(uint32_t crc32);

#endif  // INCLUDED_CLUTCHPROGRAMSIM_H
