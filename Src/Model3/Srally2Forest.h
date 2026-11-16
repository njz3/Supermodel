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
 * Srally2Forest.h
 *
 * Sega Rally 2's hidden fifth course, FOREST, in the practice's course
 * selection (a game patch: the in-game menu's Game page, the ForestCourse
 * setting of the game's section). See Srally2Forest.cpp.
 */

#ifndef INCLUDED_SRALLY2FOREST_H
#define INCLUDED_SRALLY2FOREST_H

#include "Model3/Real3D.h"
#include <cstdint>
#include <string>
#include <vector>

struct Game;

class CSrally2Forest
{
public:
  static constexpr const char *kSetting = "ForestCourse";

  // The game the patch was written for: srally2 (export, EPR-20632 to EPR-20635), its program as the patch expects it
  static bool Supports(const Game &game, const uint8_t *crom);

  // ram: the PowerPC's (32-bit words in the host's order), crom: the program and banked ROMs (the same, fixed 8 MB
  // first), vrom: the video ROM (the same), photoFile: a photo for the
  // panel instead of the one built in (256x128 BMP), if there
  CSrally2Forest(uint8_t *ram, uint8_t *crom, uint8_t *vrom, CReal3D &gpu, const std::string &photoFile);

  bool Enabled(void) const { return m_enabled; }
  void SetEnabled(bool enabled);

  // After each frame of the main board, with the PowerPC stopped: the program patched (or given back) in RAM when
  // the game is outside the course selection, the panel's textures shown during it, the rain's and the car's
  // reflections' during FOREST's races
  void Frame(void);

  // Save states without the panel's textures (texture RAM as the game has it): hidden before, shown again after
  void BeforeSaveState(void);
  void AfterSaveState(void);

private:
  uint32_t Ram32(uint32_t addr) const;
  void WriteRam32(uint32_t addr, uint32_t value);
  void WriteModels(void);
  void WriteRoute(bool forest);
  void ShowPanel(void);
  void ShowRace(void);
  bool Sheltered(void);
  void SpreadParticles(void);
  void WriteEnding(bool write);
  bool LoadPhoto(void);

  uint8_t *m_ram;
  uint8_t *m_crom;
  uint8_t *m_vrom;
  CReal3D &m_gpu;
  std::string m_photoFile;
  bool m_enabled = false;
  bool m_modelsWritten = false;
  enum class Shown { None, Panel, Race };
  Shown m_shown = Shown::None;      // the textures shown: the panel's (the carousel) or the race's (rain, reflections)
  bool m_hiddenForSave = false;
  std::vector<uint32_t> m_routeSaved;   // MOUNTAIN's route copies in FOREST's bank, as the ROMs have them
  std::vector<float> m_line;        // FOREST's line, x and z of each point (the rocks' shelter)
  std::vector<uint16_t> m_photo;    // 256x128, texture RAM's format, the bottom row first (as the panel maps it)
};

#endif  // INCLUDED_SRALLY2FOREST_H
