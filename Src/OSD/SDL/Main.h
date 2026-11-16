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

#endif // _MAIN_H_
