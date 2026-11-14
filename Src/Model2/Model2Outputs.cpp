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
 * Model2Outputs.cpp
 *
 * The game state the Model 2 games keep in RAM, sent as outputs (see
 * Model2Outputs.h), and the code that sends it, shared with Model 1.
 *
 * All found in Supermodel's agent mode, by our own search of the RAM (work RAM
 * 0x500000-0x5FFFFF, RAM 0x200000, backup RAM 0x1D00000): snapshots taken
 * after each coin, at the start, after each shot and while the player got hit,
 * the values that moved accordingly, compared with the screen. Credits: the
 * count the game shows ("CREDITS 3"), whole credits. Most racing games
 * (Daytona USA, Sega Rally, Desert Tank, Manx TT...) start as soon as a coin
 * makes a credit, so they have no credit count worth sending; Indy 500 is an
 * exception.
 */

#include "Model2Outputs.h"

namespace
{
  // Frames a pulse lasts: the recoil at each shot (a solenoid's stroke) and the
  // hit
  const int kRecoilFrames = 3;
  const int kDamagedFrames = 10;

  // A game with only its credit count
  #define CREDITS_ONLY(games, address, size) { games, { { OutputCredits, address, size, false, "Credits" }, { OutputUnknown, 0, 0, false, nullptr } }, { 0, 0 } }

  const GameRAMOutputs kModel2Games[] =
  {
    // Virtua Cop: each player's record in work RAM, 32-bit words: ammo at
    // 50EE38/3C (6 down to 0, back to 6 when shooting off screen), life at
    // 50EE70/74 (the hearts shown, 5 at the start, more from the bonuses).
    // Found firing three shots (the count fell by one each) and letting the
    // enemies shoot (the hearts went one by one to 0, game over). Credits at
    // 559270. vcopa: the same addresses (credits, three shots, the hearts lost
    // checked again)
    { "vcop vcopa",
      { { OutputCredits, 0x559270, 1, false, "Credits" },
        { OutputP1Ammo, 0x50EE38, 1, false, "Player 1's bullets left in the gun (6 when full)" },
        { OutputP2Ammo, 0x50EE3C, 1, false, "Player 2's bullets left in the gun (6 when full)" },
        { OutputP1Life, 0x50EE70, 1, false, "Player 1's lives (the hearts)" },
        { OutputP2Life, 0x50EE74, 1, false, "Player 2's lives (the hearts)" },
        { OutputUnknown, 0, 0, false, nullptr } },
      { 0, 0 } },

    // Virtua Cop 2: ammo at 507260/64 (6 to 0), life at 502090/94 (the
    // hearts, 3 at the start on the default difficulty), credits at 5D05C0;
    // found as Virtua Cop's
    { "vcop2",
      { { OutputCredits, 0x5D05C0, 1, false, "Credits" },
        { OutputP1Ammo, 0x507260, 1, false, "Player 1's bullets left in the gun (6 when full)" },
        { OutputP2Ammo, 0x507264, 1, false, "Player 2's bullets left in the gun (6 when full)" },
        { OutputP1Life, 0x502090, 1, false, "Player 1's lives (the hearts)" },
        { OutputP2Life, 0x502094, 1, false, "Player 2's lives (the hearts)" },
        { OutputUnknown, 0, 0, false, nullptr } },
      { 0, 0 } },

    // The House of the Dead: a record per player at 51EC00 and 51ECCC, the
    // bullets in byte 41 (6 to 0), the torches (lives) in byte 10 (byte 14 is
    // the count shown, which follows a little later). Found firing and getting
    // hit, as above. Credits at 51E1E8 (two to start). hotdo: the same,
    // checked; hotdp not (its ROMs were not at hand)
    { "hotd hotdo",
      { { OutputCredits, 0x51E1E8, 1, false, "Credits (2 to start)" },
        { OutputP1Ammo, 0x51EC41, 1, false, "Player 1's bullets left in the gun (6 when full)" },
        { OutputP2Ammo, 0x51ED0D, 1, false, "Player 2's bullets left in the gun (6 when full)" },
        { OutputP1Life, 0x51EC10, 1, false, "Player 1's lives (the torches)" },
        { OutputP2Life, 0x51ECDC, 1, false, "Player 2's lives (the torches)" },
        { OutputUnknown, 0, 0, false, nullptr } },
      { 0, 0 } },

    // Rail Chase 2: machine guns, no ammo count. Life: each player's bar, its
    // length in pixels as a 16-bit signed word at 501E70/72 (208 full, below
    // 0 once dead), found by measuring the bar on the screen while the player
    // was hit and matching the values. Credits in backup RAM at 1D00022
    // (found with the coins: the work RAM has no copy). rchase2a: the same,
    // checked
    { "rchase2 rchase2a",
      { { OutputCredits, 0x1D00022, 1, false, "Credits" },
        { OutputP1Life, 0x501E70, 2, true, "Player 1's life bar, 208 (full) to 0" },
        { OutputP2Life, 0x501E72, 2, true, "Player 2's life bar, 208 (full) to 0" },
        { OutputUnknown, 0, 0, false, nullptr } },
      { 0, 0 } },

    // Gunblade NY: machine guns. Lives (the hearts, 3 at the start) at 53F9E8
    // and 53F9E9, found getting hit. Credits in backup RAM at 1D00022, as in
    // Rail Chase 2. The guns' motors are on the lamp port: bit 4 player 1,
    // bit 5 player 2, on while the trigger is held in the game (not in the
    // attract mode, nor for a player not in the game)
    { "gunblade",
      { { OutputCredits, 0x1D00022, 1, false, "Credits" },
        { OutputP1Life, 0x53F9E8, 1, false, "Player 1's lives (the hearts)" },
        { OutputP2Life, 0x53F9E9, 1, false, "Player 2's lives (the hearts)" },
        { OutputUnknown, 0, 0, false, nullptr } },
      { 0x10, 0x20 } },

    // Behind Enemy Lines: credits at 5A87F0; the guns' motors on the lamp port
    // (bit 4 player 1, bit 5 player 2), as Gunblade NY's. Its life gauges were
    // not found
    { "bel",
      { { OutputCredits, 0x5A87F0, 1, false, "Credits" },
        { OutputUnknown, 0, 0, false, nullptr } },
      { 0x10, 0x20 } },

    // The others' credit counts, found with the coins (each one more, one
    // less at the start, as the screen showed); each clone listed was checked
    // the same way. Where a clone keeps its work RAM copy elsewhere (Dead or
    // Alive's at 54FCC4, not 54FCE4), the backup RAM's, the same in all, is
    // used. Indy 500 and Dynamite Cop's set C take several coins a credit
    // (Indy 500 Deluxe three): the whole credits are sent
    CREDITS_ONLY("airwlkrs", 0x510560, 1),
    CREDITS_ONLY("doa doaa doaab doaae doab", 0x1D0000C, 1),
    CREDITS_ONLY("dynabb dynabb97", 0x1D00012, 1),
    CREDITS_ONLY("dynamcop dyndeka2 dynamcopb dyndeka2b dynamcopc", 0x1D0000A, 1),
    CREDITS_ONLY("fvipers fvipersb fvipersa", 0x515CC0, 1),
    CREDITS_ONLY("indy500 indy500d indy500to", 0x1D0000A, 1),
    CREDITS_ONLY("lastbrnx lastbrnxu lastbrnxj", 0x56CEB0, 1),
    CREDITS_ONLY("overrev overrevb overrevba", 0x592704, 1),
    CREDITS_ONLY("pltkids", 0x5092A0, 1),
    CREDITS_ONLY("schamp sfight", 0x51A5C0, 1),
    CREDITS_ONLY("sgt24h", 0x510DF0, 1),
    CREDITS_ONLY("skytargt", 0x517C48, 1),
    CREDITS_ONLY("topskatr topskatru topskatruo topskatrj", 0x5DCF62, 1),
    CREDITS_ONLY("vf2 vf2b vf2a vf2o", 0x515B54, 1),
    CREDITS_ONLY("vstriker vstrikero", 0x1D00128, 1),
    CREDITS_ONLY("zerogun zeroguna zerogunaj zerogunj", 0x502210, 1),
  };

  const GameRAMOutputs *FindIn(const GameRAMOutputs *table, size_t count, const std::string &game)
  {
    for (size_t i = 0; i < count; i++)
    {
      // the game's name as one of the words
      std::string games = std::string(" ") + table[i].games + " ";
      if (games.find(" " + game + " ") != std::string::npos)
        return &table[i];
    }
    return nullptr;
  }

  bool Has(const GameRAMOutputs &table, EOutputs output)
  {
    for (const GameRAMValue *v = table.values; v->output != OutputUnknown; v++)
    {
      if (v->output == output)
        return true;
    }
    return false;
  }
}

const GameRAMOutputs *FindModel2RAMOutputs(const std::string &game)
{
  if (game.empty())
    return nullptr;
  return FindIn(kModel2Games, sizeof(kModel2Games) / sizeof(kModel2Games[0]), game);
}

const char *RecoilMeaning(const GameRAMOutputs &table)
{
  if (table.recoilLampBits[0])
    return "1 while the gun fires (its motor, from the game's lamp port)";
  if (Has(table, OutputP1Ammo))
    return "1 for 3 frames at each shot (the ammo count dropping)";
  return nullptr;
}

const char *DamagedMeaning(const GameRAMOutputs &table)
{
  if (Has(table, OutputP1Life))
    return "1 for 10 frames when the player is hit (the life dropping)";
  return nullptr;
}

CGameStateOutputs::CGameStateOutputs(const GameRAMOutputs *(*find)(const std::string &game))
  : m_find(find)
{
}

const GameRAMOutputs *CGameStateOutputs::Table(const Game &game)
{
  if (game.name != m_game)
  {
    m_game = game.name;
    m_table = m_find(game.name);
    m_ammo[0] = m_ammo[1] = m_life[0] = m_life[1] = -1;
  }
  return m_table;
}

void CGameStateOutputs::Update(const Game &game, COutputs *outputs, const std::function<uint8_t(uint32_t)> &read8)
{
  const GameRAMOutputs *table = outputs ? Table(game) : nullptr;
  if (!table)
    return;

  for (const GameRAMValue *v = table->values; v->output != OutputUnknown; v++)
  {
    int value = read8(v->address);
    if (v->size == 2)
    {
      value |= read8(v->address + 1) << 8;
      if (v->isSigned)
        value = int16_t(value);
    }
    else if (v->isSigned)
      value = int8_t(value);
    if (value < 0)
      value = 0;
    if (value > 255)
      value = 255;
    outputs->SetValue(v->output, uint8_t(value));

    // A shot: the ammo one or two less (not a reset to empty, as the attract
    // mode does). A hit: the life less, by one or two for a count of lives
    // (not a reset), by any amount for a bar
    int player = (v->output == OutputP1Ammo || v->output == OutputP1Life) ? 0 : 1;
    if (v->output == OutputP1Ammo || v->output == OutputP2Ammo)
    {
      if (m_ammo[player] >= 0 && value < m_ammo[player] && m_ammo[player] - value <= 2)
        m_recoilFrames[player] = kRecoilFrames;
      m_ammo[player] = value;
    }
    else if (v->output == OutputP1Life || v->output == OutputP2Life)
    {
      if (m_life[player] >= 0 && value < m_life[player] && (m_life[player] > 10 || m_life[player] - value <= 2))
        m_damagedFrames[player] = kDamagedFrames;
      m_life[player] = value;
    }
  }

  // The pulses, for a game that has them
  const bool ammoRecoil = !table->recoilLampBits[0] && Has(*table, OutputP1Ammo);
  const bool damaged = Has(*table, OutputP1Life);
  for (int p = 0; p < 2; p++)
  {
    if (ammoRecoil)
    {
      outputs->SetValue(p == 0 ? OutputP1CtmRecoil : OutputP2CtmRecoil, m_recoilFrames[p] > 0);
      if (m_recoilFrames[p] > 0)
        m_recoilFrames[p]--;
    }
    if (damaged)
    {
      outputs->SetValue(p == 0 ? OutputP1Damaged : OutputP2Damaged, m_damagedFrames[p] > 0);
      if (m_damagedFrames[p] > 0)
        m_damagedFrames[p]--;
    }
  }
}

void CGameStateOutputs::Lamps(const Game &game, COutputs *outputs, uint8_t lamps)
{
  const GameRAMOutputs *table = outputs ? Table(game) : nullptr;
  if (!table || !table->recoilLampBits[0])
    return;
  outputs->SetValue(OutputP1CtmRecoil, (lamps & table->recoilLampBits[0]) != 0);
  outputs->SetValue(OutputP2CtmRecoil, (lamps & table->recoilLampBits[1]) != 0);
}
