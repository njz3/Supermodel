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
 * GamePatches.h
 *
 * The game patches offered, by ROM set (see IEmulator::GamePatch): their
 * settings, check boxes and descriptions. The emulator offers one for the game
 * loaded only when its ROMs hold what the patch changes (GetGamePatches); the
 * front-end's Game Options window, where no game runs, shows them by ROM set.
 */

#ifndef INCLUDED_GAMEPATCHES_H
#define INCLUDED_GAMEPATCHES_H

#include "IEmulator.h"
#include <string>
#include <vector>

// The patches for this ROM set, all off (enabled: the caller's, from the settings or the emulator)
inline std::vector<IEmulator::GamePatch> GamePatchesFor(const std::string &romSet)
{
  std::vector<IEmulator::GamePatch> patches;
  if (romSet == "srally2")
  {
    IEmulator::GamePatch patch;
    patch.key = "ForestCourse";   // CSrally2Forest::kSetting
    patch.label = "FOREST, the hidden fifth course";
    patch.description = "A fifth panel in the practice's course selection: FOREST, a complete course of the game's that no menu "
                        "shows, with its rival on FOREST's own line. From the next course selection (the game's own program "
                        "is given back when turned off). Its weather falls as rain, the car's reflections as on the other courses, "
                        "its race's end plays (the game hung after CLEARED). Its title at the race's start and its music "
                        "stay MOUNTAIN's.";
    patches.push_back(patch);
  }
  return patches;
}

#endif  // INCLUDED_GAMEPATCHES_H
