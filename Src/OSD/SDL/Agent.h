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
 * Agent.h
 *
 * Agent mode (-agent): Supermodel driven by another program over stdin and
 * stdout, one command a line, for an autopilot or for tests. The window is
 * hidden, the frames are rendered into a frame buffer that the agent can read,
 * and they run only on the agent's commands. The protocol is described at the
 * top of Agent.cpp and in Docs/README.txt.
 *
 * Everything lives here; Main.cpp only calls the functions below, each from
 * one place, and they do nothing unless -agent was given.
 */

#ifndef INCLUDED_AGENT_H
#define INCLUDED_AGENT_H

#include <cstdint>
#include <functional>
#include <string>

class IEmulator;
class CInputs;
class COutputs;
class SuperAA;
namespace Util { namespace Config { class Node; } }

namespace Agent
{
  // First thing in main(): takes -agent out of the command line and, if it
  // was there, stdout for the protocol (everything else printed goes to stderr)
  void Init(int &argc, char **argv);

  // Whether -agent was given
  bool Enabled();

  // Once the configuration is resolved: no full screen, no vertical sync (the
  // agent paces the frames), joysticks read in the background
  void AdjustConfig(Util::Config::Node &config);

  // The extra flags of the window: hidden in agent mode
  uint32_t WindowFlags();

  // Before the outputs are attached: in agent mode, outputs that record the
  // drive board bytes and pass everything on to the given ones (if any)
  COutputs *WrapOutputs(COutputs *outputs);

  // Serves the agent's commands until "quit" or the end of stdin, instead of
  // the main loop. menu: what the in-game menu does ("set KEY VALUE": a
  // setting changed and applied, "save", "savegame"), its reply
  void Run(IEmulator *emulator, CInputs *inputs, SuperAA *superAA, const Util::Config::Node &config,
           std::function<bool()> pollInputs,
           std::function<bool(const std::string &path)> saveState,
           std::function<bool(const std::string &path)> loadState,
           std::function<std::string(const std::string &request)> menu);
}

#endif  // INCLUDED_AGENT_H
