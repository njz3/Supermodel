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
 * Model1Outputs.cpp
 *
 * Model 1's lamps and game state (see Model1Outputs.h).
 *
 * The lamps: found in Supermodel's agent mode by recording the I/O board's
 * dual port RAM (0xC00000, the low byte of each word) every few frames. Apart
 * from the inputs (0-0A), the drive board command (11) and the board's
 * commands (20-21), the games write one byte, 0F, and its bits follow the
 * cabinet: bit 0 pulses at each coin (the coin counter), bit 2 blinks while
 * a credit waits for the start button (Virtua Fighter, Star Wars Arcade), bit
 * 3 is Virtua Fighter's second start button, and Virtua Racing lights bits 3
 * to 6 for its VR1 to VR4 view buttons (the selected view's, checked pressing
 * each in a race) and bit 7 in the attract mode. That is Model 2's lamp port
 * layout, so the same outputs are sent.
 *
 * The game state, found as Model 2's (Model2Outputs.cpp), in RAM A (0x400000)
 * and RAM B (0x500000): the credit counts, one more at each coin, less when a
 * game starts, as the screen showed. Virtua Racing starts as soon as a coin
 * makes a credit (no count to send); NetMerc's was not found.
 */

#include "Model1Outputs.h"

namespace
{
  const GameRAMOutputs kModel1Games[] =
  {
    // Virtua Fighter: 40BD00 (copies at 40FA18 and 40FC18)
    { "vf", { { OutputCredits, 0x40BD00, 1, false, "Credits" }, { OutputUnknown, 0, 0, false, nullptr } }, { 0, 0 } },
    // Star Wars Arcade: 40F0E0 (two taken when the mission starts)
    { "swa swaj", { { OutputCredits, 0x40F0E0, 1, false, "Credits" }, { OutputUnknown, 0, 0, false, nullptr } }, { 0, 0 } },
  };
}

const GameRAMOutputs *FindModel1RAMOutputs(const std::string &game)
{
  for (const GameRAMOutputs &table : kModel1Games)
  {
    // the game's name as one of the words
    std::string games = std::string(" ") + table.games + " ";
    if (!game.empty() && games.find(" " + game + " ") != std::string::npos)
      return &table;
  }
  return nullptr;
}

namespace Model1Outputs
{
  void Lamps(COutputs *outputs, uint8_t lamps)
  {
    if (!outputs)
      return;
    outputs->SetValue(OutputLampStart, !!(lamps & 0x04));
    outputs->SetValue(OutputLampView1, !!(lamps & 0x08));
    outputs->SetValue(OutputLampView2, !!(lamps & 0x10));
    outputs->SetValue(OutputLampView3, !!(lamps & 0x20));
    outputs->SetValue(OutputLampView4, !!(lamps & 0x40));
    outputs->SetValue(OutputLampLeader, !!(lamps & 0x80));
    outputs->SetValue(OutputRawLamps, lamps);
  }
}
