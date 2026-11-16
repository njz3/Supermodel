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
 * Srally2Forest.cpp
 *
 * Sega Rally 2's fifth course. The program (srally2, EPR-20632 to EPR-20635)
 * has a table of eight courses: 0 DESERT_OLD, 1 FOREST, 2 MOUNTAIN, 3 DESERT,
 * 4 RIVIERA, 5 SWEDISH (Snowy), 6 and 7 INVALID. FOREST is complete (its
 * track, scenery and data, a bank of its own in the ROMs) but no menu leads to
 * it: the practice's course selection, a carousel of four panels, maps them to
 * 3 2 4 5, the championship plays 3 2 5 4.
 *
 * The patch makes the carousel one of five panels, the fifth FOREST:
 *
 * - The program, patched in RAM once the game has copied it from CROM (the
 *   ROMs are left as they are; the game's own checks see them unchanged), and
 *   given back when the patch is turned off, outside the course selection
 *   either way. The carousel turns by fifths of a turn instead of quarters
 *   (its step, 2pi/5, its spacing, 1/5, its index modulo 5, its intro showing
 *   five panels). Its tables move to a free place, the debug names RND_* that
 *   only a debug display reads: the panels' highlight flags (four words at
 *   0x47D424, followed by a vector in use), the index to the course (3 2 4 5,
 *   followed by a table of functions), the panels' models (normal and
 *   highlighted, one after the other); each gets a fifth entry: course 1, two
 *   new models.
 *
 * - The models, copies of MOUNTAIN's panel (normal and highlighted) at the
 *   end of VROM, where it is zero, their photo and title mapped to a texture
 *   the course selection does not show (the championship laurels of the game
 *   selection, page 1 at x 1024).
 *
 * - That texture, while the course selection is shown: a photo of the course
 *   (a frame of a race on it in Supermodel, Srally2ForestPhoto.h; or
 *   srally2_forest.bmp beside the settings, 256x128, if there) and the title
 *   FOREST, drawn here. The game's texels are given back when it ends
 *   (CReal3D's texture overlays).
 *
 * Linked cabinets (the battle) choose among five too: the program clears five
 * counters for the cabinets' choices but masked the index to 0-3.
 *
 * During FOREST's races: its weather (SNOWY's falling particles, which FOREST's
 * textures have no snowflake for) falls as rain, a drop of the patch's drawn
 * in a texture FOREST does not use, the particles sent there by a texture
 * offset (a routine of the patch's around the game's call) and falling
 * faster, but under the rocks of the gorge; the car's reflections (a texture FOREST gives them much more opaque
 * than the other courses' do) are made as faint as on the others.
 *
 * The race's end: the staff roll after CLEARED reads FOREST's shots from tables
 * of four courses (the game hanged there); five, FOREST's shots beside its
 * road and its replay following the car.
 *
 * The title shown when the race starts remains MOUNTAIN's.
 */

#include "Model3/Srally2Forest.h"
#include "Model3/Srally2ForestPhoto.h"
#include "Game.h"
#include "Supermodel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace
{
  // A word of the program: its address in RAM (its offset in CROM), the game's, the patch's. Variable: data the
  // game writes (the highlight flags), not compared
  struct ProgramWord
  {
    uint32_t addr;
    uint32_t original;
    uint32_t patched;
    bool variable;
  };

  constexpr ProgramWord kProgram[] =
  {
    // The carousel's intro: five panels shown (state 5 of the menus)
    { 0x047420, 0x2C800003, 0x2C800004, false },  // cmpwi cr1, r0, 3 -> 4
    { 0x047430, 0x38000004, 0x38000005, false },  // li r0, 4 -> 5
    // Its step, pi/2 -> 2pi/5, and its spacing, 1/4 -> 1/5 of a turn
    { 0x0E97EC, 0x3FC90FDB, 0x3FA0D97C, false },
    { 0x0E9814, 0x3E800000, 0x3E4CCCCD, false },
    // Its index modulo 5: >= 5 -> 0 (cmpwi 4 -> 5), -1 -> 4 (clrlwi r9,r9,30 -> rlwinm r9,r9,0,29,29)
    { 0x00599C, 0x2C8B0004, 0x2C8B0005, false },
    { 0x0059BC, 0x552907BE, 0x5529077A, false },
    { 0x005D44, 0x2C8B0004, 0x2C8B0005, false },
    { 0x005D64, 0x552907BE, 0x5529077A, false },
    { 0x005E20, 0x2C8B0004, 0x2C8B0005, false },
    { 0x005E44, 0x552907BE, 0x5529077A, false },
    { 0x005F28, 0x2C8A0004, 0x2C8A0005, false },
    { 0x005F50, 0x552907BE, 0x5529077A, false },
    // Its index from its angle, counted from 5 (li 4 -> 5), its angle from its index, (5 - index) steps
    { 0x005CE8, 0x39600004, 0x39600005, false },
    { 0x005DBC, 0x39600004, 0x39600005, false },
    { 0x005ECC, 0x39400004, 0x39400005, false },
    { 0x0056F8, 0x20000004, 0x20000005, false },
    { 0x005F70, 0x200A0004, 0x200A0005, false },
    // The highlight flags, 0x47D424 -> 0x0F24A0 (lis/addi pairs), five of them cleared or set
    { 0x0050E0, 0x3D200048, 0x3D20000F, false },
    { 0x0050E4, 0x38E9D424, 0x38E924A0, false },
    { 0x00537C, 0x3D200048, 0x3D20000F, false },
    { 0x005380, 0x3929D424, 0x392924A0, false },
    { 0x0058D4, 0x3D200048, 0x3D20000F, false },
    { 0x0058D8, 0x38E9D424, 0x38E924A0, false },
    { 0x00594C, 0x3D200048, 0x3D20000F, false },
    { 0x005950, 0x3929D424, 0x392924A0, false },
    { 0x0059C8, 0x3D200048, 0x3D20000F, false },
    { 0x0059CC, 0x3929D424, 0x392924A0, false },
    { 0x0059E4, 0x3D200048, 0x3D20000F, false },
    { 0x0059E8, 0x3929D424, 0x392924A0, false },
    { 0x00606C, 0x3D200048, 0x3D20000F, false },
    { 0x006070, 0x3B89D424, 0x3B8924A0, false },
    { 0x045DEC, 0x3D200048, 0x3D20000F, false },
    { 0x045DF0, 0x3B69D424, 0x3B6924A0, false },
    { 0x04622C, 0x3D200048, 0x3D20000F, false },
    { 0x046230, 0x38C9D424, 0x38C924A0, false },
    { 0x046288, 0x3D200048, 0x3D20000F, false },
    { 0x04628C, 0x3929D424, 0x392924A0, false },
    { 0x047564, 0x3D200048, 0x3D20000F, false },
    { 0x047568, 0x3929D424, 0x392924A0, false },
    { 0x0058E8, 0x39400003, 0x39400004, false },  // loops over the flags: 4 -> 5 words
    { 0x0058EC, 0x3960000C, 0x39600010, false },
    { 0x005958, 0x39400003, 0x39400004, false },
    { 0x00595C, 0x3800000C, 0x38000010, false },
    { 0x046240, 0x39400003, 0x39400004, false },
    { 0x046244, 0x3900000C, 0x39000010, false },
    // The index to the course, 0x0F1F20 -> 0x0F24B4
    { 0x046AE8, 0x3D20000F, 0x3D20000F, false },
    { 0x046AEC, 0x39291F20, 0x392924B4, false },
    { 0x048470, 0x3D20000F, 0x3D20000F, false },
    { 0x048474, 0x39291F20, 0x392924B4, false },
    // The opponents' route: 426 points a lap on FOREST (MOUNTAIN's 342 copied; see WriteRoute)
    { 0x0EE104, 0x00000156, 0x000001AA, false },
    // The panels' models, highlighted 0x0E9790 -> 0x0F24DC, normal 0x0E9780 -> 0x0F24C8 (the carousel's and the
    // panels' other drawing's)
    { 0x006074, 0x3D20000F, 0x3D20000F, false },
    { 0x006078, 0x3BA99790, 0x3BA924DC, false },
    { 0x00607C, 0x3D20000F, 0x3D20000F, false },
    { 0x006080, 0x3BC99780, 0x3BC924C8, false },
    { 0x045DF4, 0x3D20000F, 0x3D20000F, false },
    { 0x045DF8, 0x3B899790, 0x3B8924DC, false },
    { 0x045DFC, 0x3D20000F, 0x3D20000F, false },
    { 0x045E00, 0x3BA99780, 0x3BA924C8, false },
    // Linked cabinets (the battle): the cabinets' choices counted for five panels (the program clears five counters
    // but masks the index to 0-3, FOREST counted as DESERT), the most chosen among five (0x0E97DC: 0 1 2 3 -> 0x0F24F0)
    { 0x0055D0, 0x5529173A, 0x5529103A, false },  // rlwinm r9,r9,2,28,29 -> slwi r9,r9,2
    { 0x00561C, 0x3D20000F, 0x3D20000F, false },
    { 0x005620, 0x38C997DC, 0x38C924F0, false },
    { 0x005648, 0x2C8A0003, 0x2C8A0004, false },  // cmpwi cr1, r10, 3 -> 4
    { 0x0F24F0, 0x415F4445, 0x00000000, false },
    { 0x0F24F4, 0x53455254, 0x00000001, false },
    { 0x0F24F8, 0x20202020, 0x00000002, false },
    { 0x0F24FC, 0x00000000, 0x00000003, false },
    { 0x0F2500, 0x524E445F, 0x00000004, false },
    // The tables, in place of the debug names RND_*: the course of each index (DESERT, MOUNTAIN, RIVIERA, SNOWY,
    // FOREST)
    { 0x0F24B4, 0x5350494E, 0x00000003, false },
    { 0x0F24B8, 0x5F4D4F4E, 0x00000002, false },
    { 0x0F24BC, 0x41434F20, 0x00000004, false },
    { 0x0F24C0, 0x00000000, 0x00000005, false },
    { 0x0F24C4, 0x524E445F, 0x00000001, false },
    // The panels' models, normal: the four of the game's, FOREST's (model 0x4D97, its entry below)
    { 0x0F24C8, 0x415F5357, 0x000002C4, false },
    { 0x0F24CC, 0x45444953, 0x000002DF, false },
    { 0x0F24D0, 0x48202020, 0x00000322, false },
    { 0x0F24D4, 0x00000000, 0x00000329, false },
    { 0x0F24D8, 0x524E445F, 0x00004D97, false },
    // Highlighted (FOREST's model 0x4D98)
    { 0x0F24DC, 0x415F4D4F, 0x000002C5, false },
    { 0x0F24E0, 0x4E41434F, 0x000002E0, false },
    { 0x0F24E4, 0x20202020, 0x00000323, false },
    { 0x0F24E8, 0x00000000, 0x0000032A, false },
    { 0x0F24EC, 0x524E445F, 0x00004D98, false },
    // The highlight flags, all set (as the game sets them before the course selection)
    { 0x0F24A0, 0x44454641, 0x00000001, true },
    { 0x0F24A4, 0x554C5420, 0x00000001, true },
    { 0x0F24A8, 0x20202020, 0x00000001, true },
    { 0x0F24AC, 0x00000000, 0x00000001, true },
    { 0x0F24B0, 0x524E445F, 0x00000001, true },
    // The entries of models 0x4D97 and 0x4D98 in the table of models (0x0A4BA8, 16 bytes each), as MOUNTAIN's panel's
    // (0x2DF, 0x2E0) but for the model's address in VROM
    { 0x0F2518, 0x415F464F, 0x00FC0000, false },
    { 0x0F251C, 0x52455354, 0x00000000, false },
    { 0x0F2520, 0x20202020, 0xFF4DFCBA, false },
    { 0x0F2524, 0x00000000, 0x00000000, false },
    { 0x0F2528, 0x524E445F, 0x00FC1000, false },
    { 0x0F252C, 0x415F4445, 0x00000000, false },
    { 0x0F2530, 0x53455254, 0xFF4DFCBA, false },
    { 0x0F2534, 0x5F4F4C44, 0x00000000, false },
    // FOREST's weather is the falling particles of SNOWY's (its own table, 0x12FF0C, type 2), whose snowflake texture
    // (page 1, 256, 544) FOREST's bank does not have: a banner's corner there. They fall as rain on FOREST instead: a
    // routine in place of the debug names RND_C_* draws them (the call at 0x24A10) with a texture offset to a drop of
    // the patch's, set in the culling node template's word 2 (0x4A9888) around the call (the offset, variable: none
    // but during FOREST's races), and their speed (0x0ED0E8, -0.0586 a frame) is a rain's then
    { 0x0F25B4, 0x524E445F, 0x9421FFF0, false },  // stwu r1, -16(r1)
    { 0x0F25B8, 0x435F5357, 0x7C0802A6, false },  // mflr r0
    { 0x0F25BC, 0x45444953, 0x90010014, false },  // stw r0, 20(r1)
    { 0x0F25C0, 0x48202020, 0x3D60004B, false },  // lis r11, 0x4B
    { 0x0F25C4, 0x00000000, 0x38000000, false },  // li r0, 0
    { 0x0F25C8, 0x524E445F, 0x60000000, true },   // ori r0, r0, offset
    { 0x0F25CC, 0x435F4D4F, 0x900B9888, false },  // stw r0, -0x6778(r11): the template's word 2
    { 0x0F25D0, 0x4E41434F, 0x4BF7B01D, true },   // bl 0x6D5EC (the game's: a model drawn); nop under the rocks
    { 0x0F25D4, 0x20202020, 0x38000000, false },  // li r0, 0
    { 0x0F25D8, 0x00000000, 0x3D60004B, false },  // lis r11, 0x4B
    { 0x0F25DC, 0x524E445F, 0x900B9888, false },  // stw r0, -0x6778(r11): no offset
    { 0x0F25E0, 0x435F4445, 0x80010014, false },  // lwz r0, 20(r1)
    { 0x0F25E4, 0x53455254, 0x7C0803A6, false },  // mtlr r0
    { 0x0F25E8, 0x20202020, 0x38210010, false },  // addi r1, r1, 16
    { 0x0F25EC, 0x00000000, 0x4E800020, false },  // blr
    { 0x024A10, 0x48048BDD, 0x480CDBA5, false },  // bl 0x6D5EC -> bl 0x0F25B4
    { 0x0ED0E8, 0xBD700000, 0xBD700000, true },   // the particles' speed
    // The race's end: after CLEARED the staff roll replays the race, its shots from tables by course (0x11664: FOREST
    // 4) of four entries only (DESERT, MOUNTAIN, SNOWY, RIVIERA), the fifth something else: FOREST's shots read from
    // address 0 or 0x14, a stack of garbage objects, the PowerPC lost (the game hangs, a black screen). Tables of five
    // in a free place (kEnding, zero in all races, the staff roll and the attract), their code redirected (lis/addi
    // pairs), and FOREST's replay cameras in the third table's empty fifth entry (0x13689C)
    { 0x027E80, 0x3D200013, 0x3D200014, false },  // lis r9, 0x13 -> 0x14: the end shots (4 a course)
    { 0x027E84, 0x39294940, 0x3929EA40, false },  // addi r9, r9, 0x4940 -> -0x15C0 (0x13EA40)
    { 0x028CC8, 0x3D200013, 0x3D200014, false },  // lis r9, 0x13 -> 0x14: the staff roll's shots (5 a course)
    { 0x028CCC, 0x39294F90, 0x3929EA60, false },  // addi r9, r9, 0x4F90 -> -0x15A0 (0x13EA60)
    { 0x1368AC, 0x00000000, 0x0013ED50, false },  // the replay cameras of course index 4 (FOREST)
  };

  // The menus' state (5 the carousel's intro, 6 the selection, 7 the choice made) and the game chosen (1 practice)
  constexpr uint32_t kMenuState = 0x22435C;
  constexpr uint32_t kGameType = 0x22432C;

  // MOUNTAIN's panel, normal and highlighted (models 0x2DF and 0x2E0), and FOREST's copies, in VROM (32-bit words)
  constexpr uint32_t kMountainModels[2] = { 0x4B4F30, 0x4B5929 };
  constexpr uint32_t kForestModels[2] = { 0xFC0000, 0xFC1000 };
  constexpr uint32_t kModelRoom = 0x1000;

  // The texture they map instead of MOUNTAIN's photo (512, 1536) and title (768, 1792): page 1, x 1024, y 0 and 128
  constexpr unsigned kTexX = 1024, kPhotoY = 0, kTitleY = 128, kPage = 1;
  constexpr unsigned kPhotoW = 256, kPhotoH = 128, kTitleW = 256, kTitleH = 64;

  // A race (the menus' state then), its course (1 FOREST)
  constexpr uint32_t kRaceState = 17;
  constexpr uint32_t kCourse = 0x22CF44;

  // The rain: the routine's offset (ori r0, r0, 0x8203: 128, 96 from the snowflake's texture, to page 1, 384, 640, a
  // texture FOREST does not use, nor its mipmaps), the drop drawn there, the particles' speed
  constexpr uint32_t kOffsetOri = 0x0F25C8, kRainOffset = 0x60008203, kNoOffset = 0x60000000;
  constexpr unsigned kDropX = 384, kDropY = 640, kDropSize = 32;
  constexpr uint32_t kFallSpeed = 0x0ED0E8, kSnowFall = 0xBD700000, kRainFall = 0xBE800000;   // -0.0586, -0.25 m a frame

  // No rain under the rocks: the gorge's overhang covers the road from about point 105 to 128 of FOREST's line (its
  // weather table plans that, a value of 1 from sector 100 to 140, but FOREST's sector never advances: its particles
  // fall everywhere alike). The routine's call is a nop while the car is near a point of that stretch, two points
  // (33 m) more on each side
  constexpr uint32_t kRoutineCall = 0x0F25D0, kCallDraw = 0x4BF7B01D, kNop = 0x60000000;
  constexpr uint32_t kCarPosition = 0x22449C;   // the player's car: x, y, z
  constexpr uint32_t kLine = 0x0DE258, kLinePoints = 255;
  constexpr unsigned kShelterFirst = 103, kShelterLast = 130;

  // The particles (300 of 0x18 bytes: x, y, z, their speeds), falling in a box 8 m high (y from -4 to 4, wrapped).
  // SNOWY's start spread over its height; FOREST's all at one height, a single sheet falling: drops in bursts, then
  // none. Spread here when found so
  constexpr uint32_t kParticles = 0x22B2F4, kParticleSize = 0x18, kParticleCount = 300;

  // The race's end (see kProgram): the tables and FOREST's data, in RAM zero in all races, the staff roll and the
  // attract (0x13E924, 0x2DC4 bytes). The two tables of five (the four courses' entries as the program has them),
  // FOREST's end shots (4 records of 0x50 bytes: a camera's x, y, z at 0x18, a flag at 0x3C) and the staff roll's (5),
  // cameras beside FOREST's road at the lap's points MOUNTAIN's are (13 to 73 %), and its replay cameras (a header of
  // 12 bytes, records of 0x1C: first and last sector, mode, kind, x, y, z; all FF to end): one, following the car over
  // the whole lap (mode 3 at 0, 0, 0, as MOUNTAIN's last), FOREST's sector not advancing
  constexpr uint32_t kEnding = 0x13EA40, kEndingSize = 0x340;
  constexpr uint32_t kEndShots = kEnding + 0x40, kRollShots = kEnding + 0x180, kReplayCameras = kEnding + 0x310;
  constexpr uint32_t kEndTable[4] = { 0x134440, 0x134580, 0x1346C0, 0x134800 };
  constexpr uint32_t kRollTable[4] = { 0x134950, 0x134AE0, 0x134C70, 0x134E00 };
  constexpr unsigned kEndShotPoints[4] = { 59, 140, 163, 186 }, kRollShotPoints[5] = { 33, 59, 140, 163, 186 };

  // The car's reflections, three layers over it (models of the game's own, the texture each course gives them):
  // FOREST gives them a texture of 32x32 (page 1, 928, 832) much more opaque than the other courses', which covers the
  // car with white streaks. Its alpha is divided by 8 during FOREST's races (only the reflections use it there): a
  // faint sheen (by 3 they still showed, the owner found)
  constexpr unsigned kReflectX = 928, kReflectY = 832, kReflectSize = 32, kReflectDivisor = 8;

  // The drop: a narrow vertical ellipse, light blue-grey, its edges soft (RGBA4, the particles' texture format); the
  // particles are small, a thinner streak vanishes but close to the camera
  std::vector<uint16_t> DropTexels(void)
  {
    std::vector<uint16_t> texels(size_t(kDropSize) * kDropSize, 0);
    for (unsigned y = 0; y < kDropSize; y++)
    {
      float dy = (float(y) + 0.5f - kDropSize / 2.0f) / 15.0f;
      for (unsigned x = 0; x < kDropSize; x++)
      {
        float dx = (float(x) + 0.5f - kDropSize / 2.0f) / 5.0f;
        float a = std::clamp(1.4f * (1.0f - dx * dx - dy * dy), 0.0f, 0.9f);
        unsigned alpha = unsigned(a * 15.0f + 0.5f);
        texels[size_t(y) * kDropSize + x] = uint16_t((0xD << 12) | (0xE << 8) | (0xF << 4) | alpha);
      }
    }
    return texels;
  }

  uint16_t Texel(float r, float g, float b)
  {
    auto c = [](float v) { return unsigned(std::clamp(v, 0.0f, 255.0f) * 31.0f / 255.0f + 0.5f); };
    return uint16_t((c(r) << 10) | (c(g) << 5) | c(b));
  }

  // --- The title: a stroke font (the letters of FOREST and HIDDEN), drawn as the game's titles are (a neon tube: a
  // white core, the colour around it, a glow), over dark streaks

  struct Point { float x, y; };   // in a letter's box, 0..1, y down
  struct Glyph { char c; float width; std::vector<std::vector<Point>> strokes; };

  const std::vector<Glyph> &Glyphs()
  {
    static const std::vector<Glyph> glyphs =
    {
      { 'F', 0.72f, { { { 0, 0 }, { 0, 1 } }, { { 0, 0 }, { 0.85f, 0 } }, { { 0, 0.48f }, { 0.7f, 0.48f } } } },
      { 'E', 0.72f, { { { 0.85f, 0 }, { 0, 0 }, { 0, 1 }, { 0.85f, 1 } }, { { 0, 0.48f }, { 0.7f, 0.48f } } } },
      { 'T', 0.72f, { { { 0, 0 }, { 1, 0 } }, { { 0.5f, 0 }, { 0.5f, 1 } } } },
      { 'O', 0.72f, { { { 0.15f, 0 }, { 0.85f, 0 }, { 1, 0.15f }, { 1, 0.85f }, { 0.85f, 1 }, { 0.15f, 1 }, { 0, 0.85f },
                        { 0, 0.15f }, { 0.15f, 0 } } } },
      { 'R', 0.72f, { { { 0, 1 }, { 0, 0 }, { 0.85f, 0 }, { 1, 0.12f }, { 1, 0.36f }, { 0.85f, 0.48f }, { 0, 0.48f } },
                      { { 0.55f, 0.48f }, { 1, 1 } } } },
      { 'S', 0.72f, { { { 0.97f, 0.14f }, { 0.85f, 0 }, { 0.15f, 0 }, { 0.03f, 0.12f }, { 0.03f, 0.36f }, { 0.15f, 0.48f },
                        { 0.85f, 0.52f }, { 0.97f, 0.64f }, { 0.97f, 0.88f }, { 0.85f, 1 }, { 0.15f, 1 }, { 0.03f, 0.86f } } } },
      { 'H', 0.72f, { { { 0, 0 }, { 0, 1 } }, { { 1, 0 }, { 1, 1 } }, { { 0, 0.5f }, { 1, 0.5f } } } },
      { 'I', 0.35f, { { { 0.5f, 0 }, { 0.5f, 1 } } } },
      { 'D', 0.72f, { { { 0, 0 }, { 0, 1 }, { 0.8f, 1 }, { 1, 0.8f }, { 1, 0.2f }, { 0.8f, 0 }, { 0, 0 } } } },
      { 'N', 0.72f, { { { 0, 1 }, { 0, 0 }, { 1, 1 }, { 1, 0 } } } },
    };
    return glyphs;
  }

  // The distance of each pixel (centres) to the strokes of a text: x0, y0 its top left, h its height, slant its
  // italic (x per height)
  void TextDistance(std::vector<float> &dist, int w, int h, const char *text, float x0, float y0, float height, float gap,
                    float slant)
  {
    dist.assign(size_t(w) * h, 1e9f);
    float x = x0;
    for (const char *p = text; *p; p++)
    {
      auto glyph = std::find_if(Glyphs().begin(), Glyphs().end(), [p](const Glyph &g) { return g.c == *p; });
      if (glyph == Glyphs().end())
        continue;
      float gw = height * glyph->width;
      for (const auto &stroke : glyph->strokes)
      {
        for (size_t i = 0; i + 1 < stroke.size(); i++)
        {
          float ax = x + stroke[i].x * gw + (1 - stroke[i].y) * height * slant, ay = y0 + stroke[i].y * height;
          float bx = x + stroke[i + 1].x * gw + (1 - stroke[i + 1].y) * height * slant, by = y0 + stroke[i + 1].y * height;
          float dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
          for (int py = std::max(0, int(std::min(ay, by) - 12)); py < std::min(h, int(std::max(ay, by) + 12)); py++)
          {
            for (int px = std::max(0, int(std::min(ax, bx) - 12)); px < std::min(w, int(std::max(ax, bx) + 12)); px++)
            {
              float cx = px + 0.5f, cy = py + 0.5f;
              float t = len > 0 ? std::clamp(((cx - ax) * dx + (cy - ay) * dy) / len, 0.0f, 1.0f) : 0.0f;
              float d = std::hypot(cx - ax - t * dx, cy - ay - t * dy);
              float &cell = dist[size_t(py) * w + px];
              cell = std::min(cell, d);
            }
          }
        }
      }
      x += gw + gap;
    }
  }

  void Blur(std::vector<float> &a, int w, int h, float sigma)
  {
    int r = int(std::ceil(sigma * 3));
    std::vector<float> k(size_t(2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; i++)
      sum += k[size_t(i + r)] = std::exp(-float(i * i) / (2 * sigma * sigma));
    for (float &v : k)
      v /= sum;
    std::vector<float> tmp(a.size());
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        float s = 0;
        for (int i = -r; i <= r; i++)
          s += k[size_t(i + r)] * a[size_t(y) * w + std::clamp(x + i, 0, w - 1)];
        tmp[size_t(y) * w + x] = s;
      }
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        float s = 0;
        for (int i = -r; i <= r; i++)
          s += k[size_t(i + r)] * tmp[size_t(std::clamp(y + i, 0, h - 1)) * w + x];
        a[size_t(y) * w + x] = s;
      }
  }

  // The title, rows from the top (RGB floats)
  std::vector<float> DrawTitle(void)
  {
    const int w = kTitleW, h = kTitleH;
    const float color[3] = { 40, 230, 150 };
    std::vector<float> img(size_t(w) * h * 3);
    // Dark streaks, brighter towards the bottom third, as the game's titles' background
    uint32_t seed = 7;
    auto random = [&seed]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1 << 24); };
    for (int y = 0; y < h; y++)
    {
      float base = 10 + 30 * (1 - std::fabs(float(y) - 40) / 40.0f);
      float streak = std::pow(random(), 3.0f) * 70;
      float phase = random() * 6;
      for (int x = 0; x < w; x++)
      {
        float v = base + streak * (0.6f + 0.4f * std::sin(float(x) / float(w) * 6 + phase));
        float *p = &img[(size_t(y) * w + x) * 3];
        p[0] = v * 0.55f;
        p[1] = v * 0.6f;
        p[2] = v * 0.8f;
      }
    }
    std::vector<float> big, small;
    TextDistance(big, w, h, "FOREST", 22, 25, 30, 7, 0.18f);
    TextDistance(small, w, h, "HIDDEN", 24, 11, 9, 2.5f, 0.18f);
    std::vector<float> body(big.size()), core(big.size()), glow(big.size()), sub(big.size());
    for (size_t i = 0; i < big.size(); i++)
    {
      body[i] = std::clamp(3.9f - big[i], 0.0f, 1.0f);
      core[i] = std::clamp(1.9f - big[i], 0.0f, 1.0f);
      sub[i] = std::clamp(1.8f - small[i], 0.0f, 1.0f);
    }
    glow = body;
    Blur(glow, w, h, 3.0f);
    for (size_t i = 0; i < body.size(); i++)
    {
      float *p = &img[i * 3];
      float g = std::min(glow[i] * 1.6f, 1.0f);
      for (int c = 0; c < 3; c++)
      {
        float v = p[c] * (1 - g) + glow[i] * color[c] * 1.3f;
        v = v * (1 - body[i]) + body[i] * (color[c] * 0.75f + 255 * 0.25f);
        v = v * (1 - core[i]) + core[i] * (255 * 0.85f + color[c] * 0.15f);
        v = v * (1 - sub[i]) + sub[i] * (color[c] * 0.6f + 255 * 0.4f);
        p[c] = v;
      }
    }
    return img;
  }

  // An image (rows from the top, RGB floats) as a texture the panel maps: the bottom row first
  std::vector<uint16_t> ToTexels(const std::vector<float> &img, int w, int h)
  {
    std::vector<uint16_t> texels(size_t(w) * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        const float *p = &img[(size_t(h - 1 - y) * w + x) * 3];
        texels[size_t(y) * w + x] = Texel(p[0], p[1], p[2]);
      }
    return texels;
  }
}

bool CSrally2Forest::Supports(const Game &game, const uint8_t *crom)
{
  if (game.name != "srally2" || crom == nullptr)
    return false;
  for (const ProgramWord &word : kProgram)
  {
    uint32_t value;
    memcpy(&value, crom + word.addr, 4);
    if (value != word.original)
      return false;
  }
  return true;
}

CSrally2Forest::CSrally2Forest(uint8_t *ram, uint8_t *crom, uint8_t *vrom, CReal3D &gpu, const std::string &photoFile)
  : m_ram(ram),
    m_crom(crom),
    m_vrom(vrom),
    m_gpu(gpu),
    m_photoFile(photoFile)
{
  // The photo: the file's if there, else the one built in
  if (!LoadPhoto())
  {
    m_photo.resize(size_t(kPhotoW) * kPhotoH);
    for (unsigned y = 0; y < kPhotoH; y++)
      std::copy(&kSrally2ForestPhoto[(kPhotoH - 1 - y) * kPhotoW], &kSrally2ForestPhoto[(kPhotoH - y) * kPhotoW], &m_photo[size_t(y) * kPhotoW]);
  }
  else
    InfoLog("Sega Rally 2's FOREST course: its panel's photo read from '%s'.", m_photoFile.c_str());
}

uint32_t CSrally2Forest::Ram32(uint32_t addr) const
{
  uint32_t value;
  memcpy(&value, m_ram + addr, 4);
  return value;
}

void CSrally2Forest::WriteRam32(uint32_t addr, uint32_t value)
{
  memcpy(m_ram + addr, &value, 4);
}

void CSrally2Forest::SetEnabled(bool enabled)
{
  m_enabled = enabled;
  if (enabled)
    WriteModels();
  WriteRoute(enabled);
}

// The opponents' route: in FOREST's bank, two copies of MOUNTAIN's (500 points of x, y, z and heading, 10 m apart),
// FOREST's the second (the program's table at 0x1353DC, by course: 0 DESERT_OLD's the first), which the practice's
// rival follows off FOREST's road, falling from its ground. Replaced, while the patch is on, by one along FOREST's own
// line (the program's, 0x0DE258: 255 points, a closed lap of 4256 m), as the game's are: from 10 m past the start,
// 0.55 m above the line, the heading -atan2(dx, dz) between the neighbouring points; 426 points a lap (the length the
// program wraps the opponents' index at, 0x0EE104, patched with the program: MOUNTAIN's 342 copied, which sent the
// rival back to the start 3.4 km into the lap)
void CSrally2Forest::WriteRoute(bool forest)
{
  // The fixed CROM (the program) and the banked CROM's window 1 (FOREST's bank), both copies of the banked CROM
  constexpr uint32_t kRoutes[2] = { 0x137AD0, 0x139A10 };
  constexpr unsigned kRoutePoints = 500, kLapPoints = 426;
  constexpr uint32_t kWindows[2] = { 0x800000 + 1 * 0x800000, 0x800000 + 64 * 0x100000 + 1 * 0x800000 };
  auto word = [this](uint32_t offset) { uint32_t v; memcpy(&v, m_crom + offset, 4); return v; };
  auto real = [&word](uint32_t offset) { uint32_t v = word(offset); float f; memcpy(&f, &v, 4); return f; };

  if (m_routeSaved.empty())
  {
    // MOUNTAIN's copies, as the ROMs have them (to give back), if they are where the patch expects them
    for (uint32_t window : kWindows)
      for (uint32_t route : kRoutes)
        for (unsigned i = 0; i < kRoutePoints * 4; i++)
          m_routeSaved.push_back(word(window + route + 4 * i));
    float first[3] = { real(kWindows[0] + kRoutes[0]), real(kWindows[0] + kRoutes[0] + 4), real(kWindows[0] + kRoutes[0] + 8) };
    if (!(std::fabs(first[0]) < 1 && std::fabs(first[2] - 10) < 1))
    {
      m_routeSaved.clear();
      ErrorLog("Sega Rally 2's FOREST course: the opponents' route is not where the patch expects it; left as it is.");
      return;
    }
  }

  std::vector<uint32_t> words;
  if (!forest)
    words = m_routeSaved;
  else
  {
    // FOREST's line, a closed lap, resampled kLapPoints times from 10 m past the start
    std::vector<std::array<float, 3>> line(kLinePoints + 1);
    for (unsigned i = 0; i < kLinePoints; i++)
      line[i] = { real(kLine + 12 * i), real(kLine + 12 * i + 4), real(kLine + 12 * i + 8) };
    line[kLinePoints] = line[0];
    std::vector<float> along(line.size(), 0.0f);
    for (size_t i = 1; i < line.size(); i++)
    {
      const auto &a = line[i - 1], &b = line[i];
      along[i] = along[i - 1] + std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
    }
    float lap = along.back();
    auto at = [&](float distance)
    {
      distance = std::fmod(distance, lap);
      if (distance < 0)
        distance += lap;
      size_t i = std::upper_bound(along.begin(), along.end(), distance) - along.begin();
      i = std::clamp<size_t>(i, 1, line.size() - 1);
      const auto &a = line[i - 1], &b = line[i];
      float t = (distance - along[i - 1]) / std::max(along[i] - along[i - 1], 1e-3f);
      return std::array<float, 3>{ a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]) + 0.55f, a[2] + t * (b[2] - a[2]) };
    };
    float start = 0;
    while (start < lap && std::hypot(at(start)[0], at(start)[2]) < 10)
      start += 0.25f;
    float spacing = lap / float(kLapPoints);
    std::vector<std::array<float, 3>> points(kRoutePoints);
    for (unsigned k = 0; k < kRoutePoints; k++)
      points[k] = at(start + float(k) * spacing);
    for (unsigned k = 0; k < kRoutePoints; k++)
    {
      const auto &a = points[(k + kLapPoints - 1) % kLapPoints], &b = points[(k + 1) % kLapPoints];
      float heading = -std::atan2(b[0] - a[0], b[2] - a[2]);
      for (float v : { points[k][0], points[k][1], points[k][2], heading })
      {
        uint32_t w;
        memcpy(&w, &v, 4);
        words.push_back(w);
      }
    }
    std::vector<uint32_t> route = words;
    words.clear();
    for (size_t copy = 0; copy < 4; copy++)
      words.insert(words.end(), route.begin(), route.end());
  }
  if (words.size() != size_t(4) * kRoutePoints * 4)
    return;
  size_t at = 0;
  for (uint32_t window : kWindows)
    for (uint32_t route : kRoutes)
      for (unsigned i = 0; i < kRoutePoints * 4; i++)
        memcpy(m_crom + window + route + 4 * i, &words[at++], 4);
}

// FOREST's panel: MOUNTAIN's models copied to the end of VROM (zeros there), their photo and title mapped to the
// texture the patch shows
void CSrally2Forest::WriteModels(void)
{
  if (m_modelsWritten)
    return;
  uint32_t *vrom = reinterpret_cast<uint32_t *>(m_vrom);
  for (int m = 0; m < 2; m++)
  {
    // The model's size: its polygons up to the last (7 words of header, 4 for each vertex not shared)
    uint32_t src = kMountainModels[m], size = 0;
    while (size < kModelRoom)
    {
      const uint32_t *header = &vrom[src + size];
      unsigned vertices = (header[0] & 0x40) ? 4 : 3;
      unsigned shared = 0;
      for (int i = 0; i < 4; i++)
        shared += (header[0] >> i) & 1;
      size += 7 + 4 * (vertices - std::min(shared, vertices));
      if (header[1] & 4)
        break;
    }
    uint32_t dest = kForestModels[m];
    bool free = size < kModelRoom && std::all_of(&vrom[dest], &vrom[dest + kModelRoom], [](uint32_t v) { return v == 0; });
    if (!free)
    {
      ErrorLog("Sega Rally 2's FOREST course: no room for its panel in VROM; the patch shows MOUNTAIN's.");
      return;
    }
    std::copy(&vrom[src], &vrom[src + size], &vrom[dest]);

    // Its photo (256x128 at 512, 1536) and title (256x64 at 768, 1792) to x 1024, y 1024 and 1152 (page 1)
    for (uint32_t at = 0; at < size; )
    {
      uint32_t *header = &vrom[dest + at];
      unsigned x = 32 * ((((header[4] & 0x1F) << 1) | ((header[5] >> 7) & 1)));
      unsigned y = 32 * (header[5] & 0x1F) + 1024 * ((header[4] >> 6) & 1);
      unsigned newY = x == 512 && y == 1536 ? kPhotoY : x == 768 && y == 1792 ? kTitleY : ~0u;
      if (newY != ~0u)
      {
        header[4] = (header[4] & ~0x1Fu) | ((kTexX / 32) >> 1);
        header[5] = (header[5] & ~0x9Fu) | (((kTexX / 32) & 1) << 7) | (newY / 32);
      }
      unsigned vertices = (header[0] & 0x40) ? 4 : 3;
      unsigned shared = 0;
      for (int i = 0; i < 4; i++)
        shared += (header[0] >> i) & 1;
      at += 7 + 4 * (vertices - std::min(shared, vertices));
    }
  }
  m_modelsWritten = true;
}

void CSrally2Forest::Frame(void)
{
  // The texture shown no longer after a state loaded or a reset (texture RAM as they left it)
  if (m_shown != Shown::None && !m_gpu.TextureOverlaysShown())
    m_shown = Shown::None;

  // The program in RAM: the game's (copied from CROM), the patch's (or part of it: a state saved by an older one), or
  // not there yet (booting: each word either)
  bool original = true, patched = true, present = true;
  for (const ProgramWord &word : kProgram)
  {
    if (word.variable)
      continue;
    uint32_t value = Ram32(word.addr);
    original &= value == word.original;
    patched &= value == word.patched;
    present &= value == word.original || value == word.patched;
  }
  uint32_t state = Ram32(kMenuState);
  bool selecting = state >= 5 && state <= 7 && Ram32(kGameType) == 1;

  // Patched, or given back, outside the course selection only
  if (present && !selecting)
  {
    if (m_enabled && !patched)
    {
      for (const ProgramWord &word : kProgram)
        WriteRam32(word.addr, word.patched);
      patched = true;
    }
    else if (!m_enabled && !original)
    {
      for (const ProgramWord &word : kProgram)
        WriteRam32(word.addr, word.original);
      patched = false;
    }
  }

  // The race's end's data with the program (cleared with it)
  if (patched && Ram32(kEnding) != kEndTable[0])
    WriteEnding(true);
  else if (!patched && original && Ram32(kEnding) == kEndTable[0])
    WriteEnding(false);

  // During FOREST's races: the rain (the particles' texture offset and speed, none under the rocks) and the
  // reflections' texture
  bool forestRace = patched && Ram32(kMenuState) == kRaceState && Ram32(kCourse) == 1;
  if (patched)
  {
    WriteRam32(kOffsetOri, forestRace ? kRainOffset : kNoOffset);
    WriteRam32(kFallSpeed, forestRace ? kRainFall : kSnowFall);
    WriteRam32(kRoutineCall, forestRace && Sheltered() ? kNop : kCallDraw);
  }
  if (forestRace)
    SpreadParticles();

  // The textures shown: the panel's while the carousel is, the race's during FOREST's races
  Shown want = m_hiddenForSave ? Shown::None
             : patched && selecting && m_modelsWritten ? Shown::Panel
             : forestRace ? Shown::Race : Shown::None;
  if (want != m_shown)
  {
    if (m_shown != Shown::None)
      m_gpu.HideTextureOverlays();
    m_shown = Shown::None;
    if (want == Shown::Panel)
      ShowPanel();
    else if (want == Shown::Race)
      ShowRace();
  }
}

// The race's end's tables and FOREST's data (see kEnding), or zeros
void CSrally2Forest::WriteEnding(bool write)
{
  for (uint32_t a = kEnding; a < kEnding + kEndingSize; a += 4)
    WriteRam32(a, 0);
  if (!write)
    return;
  auto real = [](uint32_t v) { float f; memcpy(&f, &v, 4); return f; };
  auto bits = [](float f) { uint32_t v; memcpy(&v, &f, 4); return v; };
  for (unsigned i = 0; i < 4; i++)
  {
    WriteRam32(kEnding + 4 * i, kEndTable[i]);
    WriteRam32(kEnding + 0x20 + 4 * i, kRollTable[i]);
  }
  WriteRam32(kEnding + 0x10, kEndShots);
  WriteRam32(kEnding + 0x30, kRollShots);

  // A camera 8 m beside the road (to its left), 3 m above it, at a point of FOREST's line
  auto camera = [&](uint32_t record, unsigned point)
  {
    auto at = [&](unsigned i, unsigned k) { uint32_t v; memcpy(&v, m_crom + kLine + 12 * (i % kLinePoints) + 4 * k, 4); return real(v); };
    float x = at(point, 0), y = at(point, 1), z = at(point, 2);
    float dx = at(point + 1, 0) - at(point + kLinePoints - 1, 0), dz = at(point + 1, 2) - at(point + kLinePoints - 1, 2);
    float n = std::sqrt(dx * dx + dz * dz);
    if (n > 0)
    {
      x += -dz / n * 8.0f;
      z += dx / n * 8.0f;
    }
    WriteRam32(record + 0x18, bits(x));
    WriteRam32(record + 0x1C, bits(y + 3.0f));
    WriteRam32(record + 0x20, bits(z));
  };
  for (unsigned i = 0; i < 4; i++)
  {
    camera(kEndShots + 0x50 * i, kEndShotPoints[i]);
    WriteRam32(kEndShots + 0x50 * i + 0x3C, i < 3 ? 1 : 0);   // as MOUNTAIN's
  }
  for (unsigned i = 0; i < 5; i++)
    camera(kRollShots + 0x50 * i, kRollShotPoints[i]);

  // The replay's camera: sectors 0 to 255, mode 3, following the car; then the end
  uint32_t record = kReplayCameras + 0xC;
  WriteRam32(record + 0x04, 255);
  WriteRam32(record + 0x08, 3);
  for (uint32_t k = 0; k < 4; k++)
    WriteRam32(record + 0x1C + 4 * k, 0xFFFFFFFF);
}

// The particles' heights spread over their box when they are all at one (FOREST's start), the same each time
void CSrally2Forest::SpreadParticles(void)
{
  uint32_t first = Ram32(kParticles + 4);
  for (unsigned i = 1; i < kParticleCount; i++)
  {
    if (Ram32(kParticles + i * kParticleSize + 4) != first)
      return;
  }
  uint32_t seed = 0x2F6B1A3Du;
  for (unsigned i = 0; i < kParticleCount; i++)
  {
    seed = seed * 1664525u + 1013904223u;
    float y = -4.0f + 8.0f * float(seed >> 8) / float(1u << 24);
    uint32_t bits;
    memcpy(&bits, &y, 4);
    WriteRam32(kParticles + i * kParticleSize + 4, bits);
  }
}

// The player's car under the rocks (or near): the point of FOREST's line nearest to it
bool CSrally2Forest::Sheltered(void)
{
  auto real = [](uint32_t v) { float f; memcpy(&f, &v, 4); return f; };
  if (m_line.empty())
  {
    for (unsigned i = 0; i < kLinePoints; i++)
    {
      uint32_t x, z;
      memcpy(&x, m_crom + kLine + 12 * i, 4);
      memcpy(&z, m_crom + kLine + 12 * i + 8, 4);
      m_line.push_back(real(x));
      m_line.push_back(real(z));
    }
  }
  float x = real(Ram32(kCarPosition)), z = real(Ram32(kCarPosition + 8));
  unsigned nearest = 0;
  float best = -1.0f;
  for (unsigned i = 0; i < kLinePoints; i++)
  {
    float dx = m_line[2 * i] - x, dz = m_line[2 * i + 1] - z, d = dx * dx + dz * dz;
    if (best < 0 || d < best)
    {
      best = d;
      nearest = i;
    }
  }
  return nearest >= kShelterFirst && nearest <= kShelterLast;
}

void CSrally2Forest::ShowRace(void)
{
  std::vector<CReal3D::TextureOverlay> overlays = CReal3D::MakeTextureOverlays(kDropX, kDropY, kPage, kDropSize, kDropSize,
                                                                               DropTexels(), CReal3D::TexelFormat::RGBA4);
  std::vector<CReal3D::TextureOverlay> reflections = CReal3D::MakeAlphaScaleOverlays(kReflectX, kReflectY, kPage, kReflectSize,
                                                                                     kReflectSize, kReflectDivisor);
  overlays.insert(overlays.end(), reflections.begin(), reflections.end());
  m_gpu.ShowTextureOverlays(std::move(overlays));
  m_shown = Shown::Race;
}

void CSrally2Forest::ShowPanel(void)
{
  std::vector<CReal3D::TextureOverlay> overlays = CReal3D::MakeTextureOverlays(kTexX, kPhotoY, kPage, kPhotoW, kPhotoH, m_photo);
  std::vector<CReal3D::TextureOverlay> title = CReal3D::MakeTextureOverlays(kTexX, kTitleY, kPage, kTitleW, kTitleH,
                                                                            ToTexels(DrawTitle(), kTitleW, kTitleH));
  overlays.insert(overlays.end(), title.begin(), title.end());
  m_gpu.ShowTextureOverlays(std::move(overlays));
  m_shown = Shown::Panel;
}

// A photo of the user's (a BMP file of 256x128, 24 or 32 bits a pixel)
bool CSrally2Forest::LoadPhoto(void)
{
  std::ifstream in(m_photoFile, std::ios::binary);
  if (!in)
    return false;
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto u32 = [&file](size_t at) { uint32_t v = 0; if (at + 4 <= file.size()) memcpy(&v, &file[at], 4); return v; };
  auto u16 = [&file](size_t at) { uint16_t v = 0; if (at + 2 <= file.size()) memcpy(&v, &file[at], 2); return v; };
  if (file.size() < 54 || file[0] != 'B' || file[1] != 'M')
    return false;
  uint32_t offset = u32(10);
  int32_t w = int32_t(u32(18)), h = int32_t(u32(22));
  unsigned bpp = u16(28);
  bool bottomUp = h > 0;
  h = std::abs(h);
  if (w != int32_t(kPhotoW) || h != int32_t(kPhotoH) || (bpp != 24 && bpp != 32))
    return false;
  size_t stride = ((size_t(w) * bpp / 8) + 3) & ~size_t(3);
  if (offset + stride * size_t(h) > file.size())
    return false;
  std::vector<float> img(size_t(w) * h * 3);
  for (int y = 0; y < h; y++)
  {
    const uint8_t *row = &file[offset + stride * size_t(bottomUp ? h - 1 - y : y)];
    for (int x = 0; x < w; x++)
    {
      const uint8_t *p = row + size_t(x) * (bpp / 8);
      float *q = &img[(size_t(y) * w + x) * 3];
      q[0] = p[2];
      q[1] = p[1];
      q[2] = p[0];
    }
  }
  m_photo = ToTexels(img, w, h);
  return true;
}

void CSrally2Forest::BeforeSaveState(void)
{
  if (m_shown != Shown::None)
  {
    m_gpu.HideTextureOverlays();
    m_shown = Shown::None;
  }
  m_hiddenForSave = true;
}

void CSrally2Forest::AfterSaveState(void)
{
  m_hiddenForSave = false;
  // Shown again at the next frame if the carousel (or FOREST's race) still is
}
