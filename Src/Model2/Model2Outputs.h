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
 * Model2Outputs.h
 *
 * The game state some Model 1 and Model 2 games keep in their RAM, sent on the
 * outputs (-outputs=net or win) with the names arcade output tools use:
 * Credits, P1_Ammo, P1_Life..., read once a frame. Two more per player are
 * made from them: P1_CtmRecoil (1 for a few frames at each shot, when the
 * ammo count drops, or while the game's own gun output is on) and P1_Damaged
 * (1 for a few frames when the life drops).
 *
 * Every address was found in Supermodel's agent mode (-agent, smagent.py's
 * ram command): RAM recorded while inserting coins, firing, reloading and
 * getting hit, the counters that followed, checked against what the screen
 * shows. A clone has an entry only when it was checked to keep its parent's
 * addresses.
 */

#ifndef INCLUDED_MODEL2OUTPUTS_H
#define INCLUDED_MODEL2OUTPUTS_H

#include "Game.h"
#include "OSD/Outputs.h"
#include <cstdint>
#include <functional>
#include <string>

// One value a game keeps in RAM
struct GameRAMValue
{
  EOutputs output;        // OutputUnknown ends the list
  uint32_t address;       // in the main CPU's address space
  uint8_t size;           // 1 or 2 bytes, little-endian
  bool isSigned;          // a negative value is sent as 0
  const char *meaning;    // for the outputs list (in-game menu, launcher)
};

// What one game sends
struct GameRAMOutputs
{
  const char *games;            // the ROM sets, separated by spaces: the parent and the clones checked
  GameRAMValue values[8];
  uint8_t recoilLampBits[2];    // the lamp port bits that drive player 1's and 2's guns (0: none)
};

// The Model 2 games' table and the Model 1 games' (Model1/Model1Outputs.cpp): this game's entry, or nullptr
const GameRAMOutputs *FindModel2RAMOutputs(const std::string &game);
const GameRAMOutputs *FindModel1RAMOutputs(const std::string &game);

// The derived outputs: what P1_CtmRecoil and P1_Damaged mean for this game (nullptr: not sent)
const char *RecoilMeaning(const GameRAMOutputs &table);
const char *DamagedMeaning(const GameRAMOutputs &table);

class CGameStateOutputs
{
public:
  explicit CGameStateOutputs(const GameRAMOutputs *(*find)(const std::string &game));

  /*
   * Update(game, outputs, read8):
   *
   * Once a frame: reads this game's values with read8 (a byte through the
   * main CPU's bus) and sends those that changed.
   */
  void Update(const Game &game, COutputs *outputs, const std::function<uint8_t(uint32_t)> &read8);

  /*
   * Lamps(game, outputs, lamps):
   *
   * The lamp port written: for a game whose guns are driven from it, their
   * recoil.
   */
  void Lamps(const Game &game, COutputs *outputs, uint8_t lamps);

private:
  const GameRAMOutputs *Table(const Game &game);

  const GameRAMOutputs *(*m_find)(const std::string &game);
  std::string m_game;                 // the game m_table was looked up for
  const GameRAMOutputs *m_table = nullptr;
  int m_ammo[2] = { -1, -1 };         // last values read (-1: none yet)
  int m_life[2] = { -1, -1 };
  int m_recoilFrames[2] = { 0, 0 };   // frames left to send 1
  int m_damagedFrames[2] = { 0, 0 };
};

#endif  // INCLUDED_MODEL2OUTPUTS_H
