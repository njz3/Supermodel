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
 * Model1Outputs.h
 *
 * Model 1's outputs: the cabinet's lamps, which the games write to the I/O
 * board's dual port RAM, and the game state some games keep in RAM (the table
 * in Model1Outputs.cpp, sent by Model2/Model2Outputs.h's CGameStateOutputs).
 */

#ifndef INCLUDED_MODEL1OUTPUTS_H
#define INCLUDED_MODEL1OUTPUTS_H

#include "Model2/Model2Outputs.h"
#include <cstdint>

namespace Model1Outputs
{
  // The dual port RAM byte the games write their lamps to
  const unsigned kLampByte = 0x0F;

  /*
   * Lamps(outputs, lamps):
   *
   * The lamp byte written: sent as RawLamps and one output per lamp, as Model
   * 2's lamp port (the same bits).
   */
  void Lamps(COutputs *outputs, uint8_t lamps);
}

#endif  // INCLUDED_MODEL1OUTPUTS_H
