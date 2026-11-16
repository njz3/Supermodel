/**
 ** Supermodel
 ** A Sega Model Series Arcade Emulator.
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

#ifndef _MAIN_H_
#define _MAIN_H_

#include <string>

Util::Config::Node DefaultConfig();

// Sets a setting of a game's section of an INI file (the game patches'), keeping all the rest (comments, layout): the
// section added at the end of the file when missing
bool SetINIGameSetting(const std::string &file, const std::string &game, const std::string &key, const std::string &value);

// ROM patches (RomPatches.h): a game's (Assets/Patches/<ROM set>), where its patched files are written
// (PatchedROMs/<ROM set>, in the write directory if any), and writing them (the ROMs loaded again from the zip file
// with the patches its settings turn on): false, and why in *message, when none could be written
std::string RomPatchFolder(const std::string &game);
std::string PatchedRomFolder(const std::string &game);
bool DumpPatchedROMs(const std::string &zip, const std::string &game, std::string *message);

#endif // _MAIN_H_
