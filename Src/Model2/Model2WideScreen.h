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
 * Model2WideScreen.h
 *
 * Wide screen on Model 2B and 2C: some of the geometry programs the games
 * upload to their coprocessor (2B: SHARC, 2C: TGPx4) tell the i960 whether
 * an object is on the 4:3 screen, testing its projection against the
 * screen's half-width (248 native pixels), and the game then leaves out what
 * is beyond. With WideScreen, those horizontal limits are moved out to the
 * wide picture's edges by rewriting the program (or the limits the program
 * keeps) in the coprocessor's memory. Without it, nothing is changed.
 */

#ifndef INCLUDED_MODEL2WIDESCREEN_H
#define INCLUDED_MODEL2WIDESCREEN_H

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CSHARC;

class CModel2WideScreen
{
public:
  void SetGame(const std::string &name) { m_game = name; }

  // The coprocessor reset (its memory cleared): nothing to patch
  void Reset();

  // A new program in the coprocessor (an upload, a state load): looks for the
  // off-screen test again. Nothing is changed until Update().
  void ProgramLoaded(CSHARC &sharc);
  void ProgramLoaded(uint64_t *tgpx4Program, size_t words);

  // Each frame: the test's horizontal limits for 'extension' native pixels
  // added on each side of the screen (0: the program's own)
  void Update(int extension);

  // Puts the program's own words back before a save state, so that states
  // hold the program as the game uploaded it, and patches it again after
  void Restore();
  void Reapply();

  // Debugging aid: with SUPERMODEL_MODEL2_COPRO_DUMP=<folder>, each program
  // uploaded to a coprocessor is written there, raw and disassembled (and the
  // SHARC's memory again at the frames SUPERMODEL_MODEL2_COPRO_DUMP_FRAMES
  // lists, e.g. 1000,5000)
  void DumpSHARC(CSHARC &sharc);
  void DumpTGP(const uint32_t *program, size_t words);
  void DumpTGPx4(const uint64_t *program, size_t words);

private:
  enum class Where
  {
    SHARCImmediate,           // the SHARC: a load instruction's immediate
    SHARCSetter,              // the SHARC: the command storing the limits the i960 sends
    TGPx4Immediate            // the TGPx4: a load instruction's immediate
  };

  struct Patch
  {
    Where  where;
    UINT32 addr;              // program address
    std::vector<UINT64> original;   // the program's instructions there
    float  limit;             // immediates: the 4:3 limit
    UINT32 limits;            // setter: the data address the limits are stored at
  };

  std::string m_game;
  CSHARC *m_sharc = nullptr;
  uint64_t *m_tgpx4 = nullptr;
  size_t m_tgpx4Words = 0;
  std::vector<Patch> m_patches;
  int m_applied = 0;          // extension the program is patched for
  int m_restored = 0;         // extension to patch again after a save state
  bool m_logged = false;
  unsigned m_dumps = 0;
  unsigned m_frames = 0;

  void FindSHARCImmediateTest();
  void FindSHARCSetterTest();
  void FindTGPx4Test();
  void Write(const Patch &patch, int extension);
  void Log(int extension);
  bool OpenDump(const char *cpu, std::string &base);
};

#endif  // INCLUDED_MODEL2WIDESCREEN_H
