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
 * The in-game menu (UIMenu, Tab by default), with the pages of linuxloader's
 * and windowsloader's: its Display page, the internal resolution, AspectRatio
 * and its companions, the display mode and the resolution, applied from the
 * next frame on; its Controls page, the devices, each with a page binding the
 * game's inputs to its own (as Flycast's), BackgroundInput and the force
 * feedback (the device that plays it, its strength). Both saved in
 * the settings file in use or in the game's own file. Its Game page: the game's
 * description and its notes from Config/GameNotes.txt (region change codes,
 * special modes, cabinet setup). Its Outputs page: what the game sends on the
 * outputs, with the values sent so far. The game is paused while it is open.
 */

#ifndef INCLUDED_GAMEMENU_H
#define INCLUDED_GAMEMENU_H

#include "SDLIncludes.h"
#include <string>
#include <vector>

namespace Util
{
  namespace Config
  {
    class Node;
  }
}

struct Game;
class COutputs;
class CInputs;

namespace GameMenu
{
  enum class Action
  {
    None,
    Save,           // into the settings file in use
    SaveForGame     // into the game's own file (<game>.ini: a copy of the common file the first time)
  };

  enum class Page
  {
    Display,
    Controls,
    Game,
    Outputs
  };

  // What the settings changed ask of the host (TakeChanges)
  enum Change : unsigned
  {
    ChangeLayout = 1,         // AspectRatio and its companions, InternalWidth and InternalHeight: the renderers set up again
    ChangeWindow = 2,         // FullScreen, BorderlessFullScreen, Stretch, XResolution and YResolution: the window too
    ChangeSwapInterval = 4,   // VSync
    ChangeInputSystem = 8,    // BackgroundInput: the input system's devices set up again
    ChangeForceFeedback = 16  // ForceFeedbackDevice, ForceFeedbackGain, ForceFeedbackInvert, the limits per effect:
                              // the effects stopped where they played, routed as now set and played again
  };

  // The game the Game page describes, and the file of the notes (read each time the menu opens: see GameNotes.txt
  // for its format)
  void SetGame(const Game &game, const std::string &notesFile);
  // The outputs the Outputs page shows the values of (nullptr: none configured)
  void SetOutputs(COutputs *outputs);
  // The inputs the Controls page binds (nullptr: none)
  void SetInputs(CInputs *inputs);
  // The size the renderers draw at now, shown by the Display page
  void SetRenderSize(unsigned width, unsigned height);
  // The outputs table, as linuxloader shows it: what the game sends on the outputs (the network's and the Windows
  // messages' names), with the values sent so far if outputs is given. inGame: the in-game menu's Outputs page, else
  // the front-end's Misc tab, where no game runs
  void DrawOutputsTable(const Util::Config::Node &config, const Game &game, COutputs *outputs, bool inGame);

  bool Open(SDL_Window *window);  // false if it cannot be shown
  void SelectPage(Page page);     // shown from the next frame
  void Close();
  bool IsOpen();
  bool CloseRequested();          // its Close button or Escape: the host closes it
  bool IsCapturing();             // the Controls page waits for an input to bind: UIMenu does not close it then

  // The window's events while it is open (the input system leaves them to it). False on quit
  bool ProcessEvents();

  // Draws it over the frame, before the buffers are swapped, the inputs polled. The settings it changes are set in
  // config
  void Draw(Util::Config::Node &config, const std::string &title, const std::string &settingsFile, bool gameFile);

  unsigned TakeChanges();         // the Change flags of the settings changed since the last call
  // The bindings changed in this session (their keys, InputStart1...), for the Save buttons
  const std::vector<std::string> &ChangedBindings();
  Action TakeAction();
  void SetStatus(const std::string &status);  // shown under the buttons

  void Shutdown();                // while the OpenGL context still exists
}

#endif  // INCLUDED_GAMEMENU_H
