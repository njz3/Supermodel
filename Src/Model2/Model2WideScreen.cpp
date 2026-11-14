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
 * Model2WideScreen.cpp
 *
 * See Model2WideScreen.h. The tests were found in the programs as the games
 * upload them (dumped with SUPERMODEL_MODEL2_COPRO_DUMP), in three shapes,
 * each looked for by its instructions rather than at fixed addresses:
 *
 * - SHARC, the limits loaded as immediates (Fighting Vipers, Sonic the
 *   Fighters): see FindSHARCImmediateTest().
 * - SHARC, the limits sent by the i960 (Rail Chase 2, Gunblade NY, Virtual
 *   On): see FindSHARCSetterTest().
 * - TGPx4, the half-width loaded as an immediate (The House of the Dead):
 *   see FindTGPx4Test().
 *
 * The other 2B and 2C programs have no such test (their games cull objects
 * themselves, or not at all), nor have the 2A (TGP) programs.
 */

#include "Model2/Model2WideScreen.h"

#include "Supermodel.h"
#include "CPU/SHARC/SHARC.h"
#include "CPU/MB86233/MB86233.h"
#include "CPU/MB86235/MB86235.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
  // The SHARC's program in its internal memory: 48-bit words at
  // 0x20000-0x24FFF (block 0) and 0x28000-0x2CFFF (block 1)
  const UINT32 SHARC_PROGRAM[2][2] = { { 0x20000, 0x25000 }, { 0x28000, 0x2D000 } };

  float AsFloat(UINT32 word)
  {
    float f;
    memcpy(&f, &word, sizeof(f));
    return f;
  }

  UINT32 AsWord(float f)
  {
    UINT32 word;
    memcpy(&word, &f, sizeof(word));
    return word;
  }

  // The 4:3 screen's half-width in native pixels, within a few pixels
  bool IsHalfWidth(float v)
  {
    return std::fabs(v) >= 240.0f && std::fabs(v) <= 256.0f;
  }

  // The half-height (192)
  bool IsHalfHeight(float v)
  {
    return std::fabs(v) >= 184.0f && std::fabs(v) <= 200.0f;
  }

  // A limit moved out by the extension
  float Widened(float limit, int extension)
  {
    return limit + (limit < 0.0f ? -float(extension) : float(extension));
  }

  // SHARC "Rn = <32-bit immediate>" (type 17, universal registers 0-15: R0-R15)
  bool IsLoadR(UINT64 word, unsigned &reg, UINT32 &imm)
  {
    if ((word >> 36) != 0x0F0)
      return false;
    reg = unsigned(word >> 32) & 0xF;
    imm = UINT32(word);
    return true;
  }

  // SHARC "COMP(Fx, Fy)" alone and unconditional (type 2, ALU operation 0x8A)
  bool IsFloatCompare(UINT64 word, unsigned &x, unsigned &y)
  {
    if ((word & 0xFFFFFFFFF000ULL) != 0x013E0008A000ULL)
      return false;
    x = unsigned(word >> 4) & 0xF;
    y = unsigned(word) & 0xF;
    return true;
  }

  // TGPx4 "MOV3 #<32-bit immediate>, <register>" (transfer type 3): the
  // immediate in bits 27-58
  bool IsMov3(UINT64 word, UINT32 &imm)
  {
    if ((word >> 61) != 7)
      return false;
    imm = UINT32(word >> 27);
    return true;
  }

  UINT64 WithMov3Immediate(UINT64 word, UINT32 imm)
  {
    return (word & ~(0xFFFFFFFFULL << 27)) | (UINT64(imm) << 27);
  }
}

/*
 * The SHARC test with the limits as immediates. A command takes a point,
 * projects it (F8 = screen x, F9 = screen y, F10 = depth) and returns to the
 * i960 an out-code built in R15: 0x1F if the point is too close (depth not
 * above 0.1), else bit 0 set if x is left of -248, bit 1 right of 248, bit 2
 * y below -192, bit 3 above 192:
 *
 *   R13 = 0xC3780000          (-248.0)
 *   COMP(F8, F13)
 *   IF GT, JUMP (+2)
 *   R15 = BSET R15 BY 0
 *   R13 = 0x43780000          (248.0)
 *   COMP(F8, F13)
 *   IF LT, JUMP (+2)
 *   R15 = BSET R15 BY 1
 *   R13 = 0xC3400000          (-192.0), then the same for y
 *
 * Looked for as two loads of opposite values near +-248 four instructions
 * apart, each compared at once with the same register. The two loads'
 * immediates are patched.
 */
void CModel2WideScreen::FindSHARCImmediateTest()
{
  CSHARC &sharc = *m_sharc;
  for (const auto &range : SHARC_PROGRAM)
  {
    for (UINT32 addr = range[0]; addr + 5 < range[1]; addr++)
    {
      unsigned reg1, reg2, x1, y1, x2, y2;
      UINT32 imm1, imm2;
      if (!IsLoadR(sharc.ReadProgramWord(addr), reg1, imm1) || !IsHalfWidth(AsFloat(imm1)))
        continue;
      if (!IsFloatCompare(sharc.ReadProgramWord(addr + 1), x1, y1) || y1 != reg1)
        continue;
      if (!IsLoadR(sharc.ReadProgramWord(addr + 4), reg2, imm2) || imm2 != (imm1 ^ 0x80000000))
        continue;
      if (!IsFloatCompare(sharc.ReadProgramWord(addr + 5), x2, y2) || x2 != x1 || y2 != reg2)
        continue;
      for (UINT32 at : { addr, addr + 4 })
      {
        Patch patch;
        patch.where = Where::SHARCImmediate;
        patch.addr = at;
        patch.original.push_back(sharc.ReadProgramWord(at));
        patch.limit = AsFloat(UINT32(patch.original[0]));
        m_patches.push_back(patch);
      }
      addr += 5;
    }
  }
}

/*
 * The SHARC test with the limits sent by the i960. A command reads six floats
 * from the input FIFO and stores them at a fixed address: focal x and y, the
 * x right and left limits (248, -248), the y limits (192, -192; +-320 in
 * Gunblade NY):
 *
 *   IF FLAG0_IN, JUMP (+0)    (waits for the FIFO)
 *   R0 = DM(I0, M0)           (reads it)
 *   ...                       (the same for R1-R5)
 *   I6 = <address>
 *   DM(I6, M1) = R0
 *   DM(I6, M1) = R1
 *   DM(I6, M1) = R2           x right limit
 *   DM(I6, M1) = R3           x left limit
 *   RTS (DB)
 *   DM(I6, M1) = R4
 *   DM(I6, M1) = R5
 *
 * A sphere test loads the same address in I6 again and reads the x limits
 * with "R6 = DM(0x02, I6)" and "R6 = DM(0x03, I6)", each compared at once
 * (COMP(F2, F6)) with the sphere's projected edge. The i960 sends the limits
 * every frame, so they are widened as the command stores them: its 20
 * instructions are rewritten with absolute stores, which frees the slot of
 * the I6 load for the extension (each command sets I6 before using it):
 *
 *   IF FLAG0_IN, JUMP (+0)
 *   R0 = DM(I0, M0)
 *   IF FLAG0_IN, JUMP (+0)
 *   R1 = DM(I0, M0)
 *   DM(<address>) = R0
 *   R0 = <extension>
 *   IF FLAG0_IN, JUMP (+0)
 *   R2 = DM(I0, M0)
 *   IF FLAG0_IN, JUMP (+0)
 *   F2 = F2 + F0,  R3 = DM(I0, M0)
 *   IF FLAG0_IN, JUMP (+0)
 *   F3 = F3 - F0,  R4 = DM(I0, M0)
 *   IF FLAG0_IN, JUMP (+0)
 *   R5 = DM(I0, M0)
 *   DM(<address + 1>) = R1
 *   DM(<address + 2>) = R2
 *   DM(<address + 3>) = R3
 *   RTS (DB)
 *   DM(<address + 4>) = R4
 *   DM(<address + 5>) = R5
 */
namespace
{
  const unsigned SETTER_WORDS = 20;
  const UINT64 WAIT_FIFO_IN = 0x071200000000ULL;    // "IF FLAG0_IN, JUMP (+0)"
  const UINT64 READ_FIFO_IN = 0x503E00000000ULL;    // "R0 = DM(I0, M0)", Rn in bits 23-26
  const UINT64 STORE_I6 = 0x5C7E80000000ULL;        // "DM(I6, M1) = R0", Rn in bits 23-26
  const UINT64 STORE_ABSOLUTE = 0x110000000000ULL;  // "DM(<address>) = R0", Rn in bits 32-35
  const UINT64 LOAD_R0 = 0x0F0000000000ULL;         // "R0 = <immediate>"
  const UINT64 RTS_DELAYED = 0x0A3E04000000ULL;     // "RTS (DB)"
  const UINT64 F2_PLUS_F0 = 0x081220ULL;            // computations: "F2 = F2 + F0"
  const UINT64 F3_MINUS_F0 = 0x082330ULL;           // "F3 = F3 - F0"

  UINT64 ReadFIFO(unsigned n) { return READ_FIFO_IN | (UINT64(n) << 23); }
  UINT64 StoreI6(unsigned n) { return STORE_I6 | (UINT64(n) << 23); }
  UINT64 StoreAbsolute(unsigned n, UINT32 addr) { return STORE_ABSOLUTE | (UINT64(n) << 32) | addr; }

  // The command as the game has it, for the limits at 'limits'
  void OriginalSetter(UINT64 *words, UINT32 limits)
  {
    for (unsigned n = 0; n < 6; n++)
    {
      words[2 * n] = WAIT_FIFO_IN;
      words[2 * n + 1] = ReadFIFO(n);
    }
    words[12] = 0x0F1600000000ULL | limits;           // "I6 = <address>"
    for (unsigned n = 0; n < 4; n++)
      words[13 + n] = StoreI6(n);
    words[17] = RTS_DELAYED;
    words[18] = StoreI6(4);
    words[19] = StoreI6(5);
  }

  // The command widening the x limits by 'extension'
  void WideSetter(UINT64 *words, UINT32 limits, int extension)
  {
    const UINT64 rewritten[SETTER_WORDS] =
    {
      WAIT_FIFO_IN, ReadFIFO(0),
      WAIT_FIFO_IN, ReadFIFO(1),
      StoreAbsolute(0, limits),
      LOAD_R0 | AsWord(float(extension)),
      WAIT_FIFO_IN, ReadFIFO(2),
      WAIT_FIFO_IN, ReadFIFO(3) | F2_PLUS_F0,
      WAIT_FIFO_IN, ReadFIFO(4) | F3_MINUS_F0,
      WAIT_FIFO_IN, ReadFIFO(5),
      StoreAbsolute(1, limits + 1),
      StoreAbsolute(2, limits + 2),
      StoreAbsolute(3, limits + 3),
      RTS_DELAYED,
      StoreAbsolute(4, limits + 4),
      StoreAbsolute(5, limits + 5)
    };
    memcpy(words, rewritten, sizeof(rewritten));
  }
}

void CModel2WideScreen::FindSHARCSetterTest()
{
  CSHARC &sharc = *m_sharc;
  const UINT64 readXRight = 0x6C3E13000000ULL;   // "R6 = DM(0x02, I6)", with any computation
  const UINT64 readXLeft = 0x6C3E1B000000ULL;    // "R6 = DM(0x03, I6)"
  for (const auto &range : SHARC_PROGRAM)
  {
    for (UINT32 addr = range[0]; addr + SETTER_WORDS <= range[1]; addr++)
    {
      // The command, for limits in internal data memory
      const UINT64 load = sharc.ReadProgramWord(addr + 12);
      const UINT32 limits = UINT32(load);
      if (sharc.ReadProgramWord(addr) != WAIT_FIFO_IN || (load >> 32) != 0x0F16 || limits < 0x20000 || limits + 6 > 0x40000)
        continue;
      UINT64 expected[SETTER_WORDS];
      OriginalSetter(expected, limits);
      bool setter = true;
      for (unsigned i = 0; i < SETTER_WORDS && setter; i++)
        setter = sharc.ReadProgramWord(addr + i) == expected[i];
      if (!setter)
        continue;

      // The test reading the limits back
      bool reader = false;
      for (const auto &range2 : SHARC_PROGRAM)
      {
        for (UINT32 at = range2[0]; at + 32 < range2[1] && !reader; at++)
        {
          if (at == addr + 12 || sharc.ReadProgramWord(at) != load)
            continue;
          unsigned found = 0, x, y;
          for (UINT32 i = at + 1; i < at + 32; i++)
          {
            const UINT64 word = sharc.ReadProgramWord(i) & 0xFFFFFF800000ULL;
            if ((word == readXRight || word == readXLeft) && IsFloatCompare(sharc.ReadProgramWord(i + 1), x, y) && x == 2 && y == 6)
              found |= (word == readXRight) ? 1 : 2;
          }
          reader = found == 3;
        }
      }
      if (!reader)
        continue;

      Patch patch;
      patch.where = Where::SHARCSetter;
      patch.addr = addr;
      patch.original.assign(expected, expected + SETTER_WORDS);
      patch.limit = 248.0f;
      patch.limits = limits;
      m_patches.push_back(patch);
      addr += SETTER_WORDS - 1;
    }
  }
}

/*
 * The TGPx4 test. A command projects an object's bounding sphere and tells
 * whether it is inside the screen, the half-width and half-height loaded
 * together, then scaled by the depth:
 *
 *   MOV3 #43780000, MB0       (248.0)
 *   MOV3 #43400000, MB1       (192.0)
 *   FMUL MB0, MA2, MA6
 *   ...
 *
 * Looked for as a load of a value near 248 followed at once by a load of a
 * value near 192 (another load of 248.0 in the same program, after one of
 * 248 squared, is the view setup, and stays). The first load's immediate is
 * patched.
 */
void CModel2WideScreen::FindTGPx4Test()
{
  for (size_t pc = 0; pc + 1 < m_tgpx4Words; pc++)
  {
    UINT32 imm1, imm2;
    if (!IsMov3(m_tgpx4[pc], imm1) || !IsHalfWidth(AsFloat(imm1)) || AsFloat(imm1) < 0.0f)
      continue;
    if (!IsMov3(m_tgpx4[pc + 1], imm2) || !IsHalfHeight(AsFloat(imm2)))
      continue;
    Patch patch;
    patch.where = Where::TGPx4Immediate;
    patch.addr = UINT32(pc);
    patch.original.push_back(m_tgpx4[pc]);
    patch.limit = AsFloat(imm1);
    m_patches.push_back(patch);
  }
}

void CModel2WideScreen::Reset()
{
  m_patches.clear();
  m_applied = 0;
  m_restored = 0;
  m_logged = false;
}

void CModel2WideScreen::ProgramLoaded(CSHARC &sharc)
{
  Reset();
  m_sharc = &sharc;
  DumpSHARC(sharc);
  FindSHARCImmediateTest();
  FindSHARCSetterTest();
}

void CModel2WideScreen::ProgramLoaded(uint64_t *tgpx4Program, size_t words)
{
  Reset();
  m_tgpx4 = tgpx4Program;
  m_tgpx4Words = words;
  DumpTGPx4(tgpx4Program, words);
  FindTGPx4Test();
}

void CModel2WideScreen::Write(const Patch &patch, int extension)
{
  switch (patch.where)
  {
  case Where::SHARCImmediate:
  {
    UINT64 word = (patch.original[0] & ~0xFFFFFFFFULL) | AsWord(Widened(patch.limit, extension));
    m_sharc->WriteProgramWord(patch.addr, extension ? word : patch.original[0]);
    break;
  }
  case Where::SHARCSetter:
  {
    UINT64 words[SETTER_WORDS];
    if (extension)
      WideSetter(words, patch.limits, extension);
    else
      std::copy(patch.original.begin(), patch.original.end(), words);
    for (unsigned i = 0; i < SETTER_WORDS; i++)
      m_sharc->WriteProgramWord(patch.addr + i, words[i]);
    break;
  }
  case Where::TGPx4Immediate:
    m_tgpx4[patch.addr] = extension ? WithMov3Immediate(patch.original[0], AsWord(Widened(patch.limit, extension))) : patch.original[0];
    break;
  }
}

void CModel2WideScreen::Update(int extension)
{
  extension = std::max(extension, 0);
  if (extension != m_applied)
  {
    for (const Patch &patch : m_patches)
      Write(patch, extension);
    if (extension)
      Log(extension);
    m_applied = extension;
  }

  // Debugging aid: the SHARC's memory dumped at the frames listed
  m_frames++;
  if (const char *frames = getenv("SUPERMODEL_MODEL2_COPRO_DUMP_FRAMES"))
    for (const char *p = frames; *p && m_sharc; p++)
      if ((p == frames || p[-1] == ',') && (unsigned) atoi(p) == m_frames)
        DumpSHARC(*m_sharc);
}

void CModel2WideScreen::Restore()
{
  if (m_applied)
    for (const Patch &patch : m_patches)
      Write(patch, 0);
  m_restored = m_applied;
  m_applied = 0;
}

void CModel2WideScreen::Reapply()
{
  if (m_restored)
    for (const Patch &patch : m_patches)
      Write(patch, m_restored);
  m_applied = m_restored;
  m_restored = 0;
}

// Once a program: what is patched
void CModel2WideScreen::Log(int extension)
{
  if (m_logged || m_patches.empty())
    return;
  m_logged = true;
  for (const Patch &patch : m_patches)
  {
    if (patch.where == Where::SHARCSetter)
      InfoLog("Model 2: wide screen, the off-screen test's x limits (sent by the i960, stored by the SHARC program at %05X-%05X) moved out by %d.",
        patch.addr, patch.addr + SETTER_WORDS - 1, extension);
    else
      InfoLog("Model 2: wide screen, the off-screen test's x limit (%s program, load at %05X) moved from %g to %g.",
        patch.where == Where::SHARCImmediate ? "SHARC" : "TGPx4", patch.addr, patch.limit, Widened(patch.limit, extension));
  }
}

/******************************************************************************
 Debugging aid: the programs dumped
******************************************************************************/

bool CModel2WideScreen::OpenDump(const char *cpu, std::string &base)
{
  const char *folder = getenv("SUPERMODEL_MODEL2_COPRO_DUMP");
  if (!folder || !*folder)
    return false;
  char name[64];
  snprintf(name, sizeof(name), "_%s_%u", cpu, m_dumps++);
  base = std::string(folder) + "/" + (m_game.empty() ? "model2" : m_game) + name;
  return true;
}

void CModel2WideScreen::DumpSHARC(CSHARC &sharc)
{
  std::string base;
  if (!OpenDump("sharc", base))
    return;
  // Program memory: 48-bit words, block 0 at 0x20000, block 1 at 0x28000
  FILE *bin = fopen((base + ".pm.bin").c_str(), "wb");
  FILE *dis = fopen((base + ".dis").c_str(), "w");
  for (UINT32 block = 0; block < 2; block++)
  {
    for (UINT32 addr = 0x20000 + block * 0x8000; addr < 0x25000 + block * 0x8000; addr++)
    {
      UINT64 word = sharc.ReadProgramWord(addr);
      if (bin)
        fwrite(&word, sizeof(word), 1, bin);
      if (dis && word)
        fprintf(dis, "%05X: %012llX  %s\n", addr, (unsigned long long) word, CSHARC::Disassemble(addr, word).c_str());
    }
  }
  if (bin)
    fclose(bin);
  if (dis)
    fclose(dis);
  // The same memory as 32-bit data words
  if (FILE *dm = fopen((base + ".dm.bin").c_str(), "wb"))
  {
    for (UINT32 addr = 0x20000; addr < 0x30000; addr++)
    {
      UINT32 word = sharc.ReadDataWord(addr);
      fwrite(&word, sizeof(word), 1, dm);
    }
    fclose(dm);
  }
  InfoLog("Model 2: SHARC program dumped to %s.*", base.c_str());
}

void CModel2WideScreen::DumpTGP(const uint32_t *program, size_t words)
{
  std::string base;
  if (!OpenDump("tgp", base))
    return;
  if (FILE *bin = fopen((base + ".bin").c_str(), "wb"))
  {
    fwrite(program, sizeof(*program), words, bin);
    fclose(bin);
  }
  if (FILE *dis = fopen((base + ".dis").c_str(), "w"))
  {
    for (size_t pc = 0; pc < words; pc++)
      fprintf(dis, "%03X: %08X  %s\n", (unsigned) pc, program[pc], CMB86233::Disassemble(program[pc]).c_str());
    fclose(dis);
  }
  InfoLog("Model 2: TGP program dumped to %s.*", base.c_str());
}

void CModel2WideScreen::DumpTGPx4(const uint64_t *program, size_t words)
{
  std::string base;
  if (!OpenDump("tgpx4", base))
    return;
  if (FILE *bin = fopen((base + ".bin").c_str(), "wb"))
  {
    fwrite(program, sizeof(*program), words, bin);
    fclose(bin);
  }
  if (FILE *dis = fopen((base + ".dis").c_str(), "w"))
  {
    for (size_t pc = 0; pc < words; pc++)
      fprintf(dis, "%03X: %016llX  %s\n", (unsigned) pc, (unsigned long long) program[pc], CMB86235::Disassemble((UINT32) pc, program[pc]).c_str());
    fclose(dis);
  }
  InfoLog("Model 2: TGPx4 program dumped to %s.*", base.c_str());
}
