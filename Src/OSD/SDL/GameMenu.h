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
 * GameMenu.h
 *
 * The in-game menu (UIMenu, F10 by default): the AspectRatio setting and its
 * companions, applied from the next frame on, and saving them in the settings
 * file in use or in the game's own file. The game is paused while it is open.
 */

#ifndef INCLUDED_GAMEMENU_H
#define INCLUDED_GAMEMENU_H

#include "SDLIncludes.h"
#include <string>

namespace Util
{
  namespace Config
  {
    class Node;
  }
}

namespace GameMenu
{
  enum class Action
  {
    None,
    Save,           // into the settings file in use
    SaveForGame     // into the game's own file (<game>.ini: a copy of the common file the first time)
  };

  bool Open(SDL_Window *window);  // false if it cannot be shown
  void Close();
  bool IsOpen();
  bool CloseRequested();          // its Close button or Escape: the host closes it

  // The window's events while it is open (the input system leaves them to it). False on quit
  bool ProcessEvents();

  // Draws it over the frame, before the buffers are swapped. The settings it changes are set in config
  void Draw(Util::Config::Node &config, const std::string &title, const std::string &settingsFile, bool gameFile);

  bool TakeChanged();             // a setting changed since the last call
  Action TakeAction();
  void SetStatus(const std::string &status);  // shown under the buttons

  void Shutdown();                // while the OpenGL context still exists
}

#endif  // INCLUDED_GAMEMENU_H
