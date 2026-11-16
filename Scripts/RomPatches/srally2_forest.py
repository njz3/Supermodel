"""
Sega Rally 2's hidden fifth course, FOREST, as a ROM patch of Supermodel's
(Src/RomPatches.h): a patch of the program EPROMs (EPR-20632 to EPR-20635,
the four on sockets of the ROM board), turned on in Game Options, its options
there too; Supermodel writes the patched EPROMs out to program a real board's.

    python srally2_forest.py ROMS.zip [OUT.xml]

ROMS.zip: srally2's ROMs (the four program EPROMs, found by their CRC32).
OUT.xml: Assets/Patches/srally2/forest.xml by default. The patch holds the
patch's bytes only (none of the game's), its EPROMs named with their CRC32.

The program (CROM) is copied to RAM by the boot ROM up to 0x1FFFFF, at the
same address, before the game runs: the patch's words are its RAM addresses.
Its own code and data go in padding at the end of the program's first part
(0x0FCE60 to 0x100000, zeros, never written by the game: checked in all its
modes), its larger data (the opponents' route, a texture) in the CROM's free
space, which the PowerPC reads at 0xFF800000 + offset.

The patch (base): the practice's course selection a carousel of five panels,
the fifth FOREST (the program's course 1, complete in the ROMs, no menu
leading to it; its panel MOUNTAIN's for now); FOREST's practice clock as the
other courses', the opponents' route along FOREST's line, its race's end (the
staff roll's shots; the game hung there). Its options: the weather as rain,
faint reflections, FOREST as the championship's fifth stage.
"""

import argparse
import math
import numpy as np
import os
import struct
import sys
import zipfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ppcasm import assemble, ha, lo   # noqa: E402
import rompatch                       # noqa: E402
import forest_panel                   # noqa: E402

# The program EPROMs, as the CROM interleaves them: 16-bit words, each EPROM two bytes of 8 (byte-swapped)
CHIPS = [("epr-20635.20", 0x7937473F), ("epr-20634.19", 0x45A09245), ("epr-20633.18", 0xF5A24F24),
         ("epr-20632.17", 0x6829A801)]
CHIP_SIZE = 0x200000


def read_chips(zip_path):
    by_crc = {}
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            by_crc[info.CRC] = info
        chips = []
        for name, crc in CHIPS:
            if crc not in by_crc:
                sys.exit("%s: %s (CRC32 %08X) not found" % (zip_path, name, crc))
            chips.append(z.read(by_crc[crc]))
    return chips


def interleave(chips):
    crom = bytearray(4 * CHIP_SIZE)
    for k, chip in enumerate(chips):
        crom[2 * k::8] = chip[1::2]
        crom[2 * k + 1::8] = chip[0::2]
    return crom


def deinterleave(crom):
    chips = []
    for k in range(4):
        chip = bytearray(CHIP_SIZE)
        chip[1::2] = crom[2 * k::8]
        chip[0::2] = crom[2 * k + 1::8]
        chips.append(bytes(chip))
    return chips


class Program:
    """The CROM as the PowerPC sees it (big-endian words), each word patched once, checked against the game's."""

    def __init__(self, crom):
        self.original = bytes(crom)
        self.data = bytearray(crom)
        self.written = set()

    def word(self, addr):
        return struct.unpack_from(">I", self.original, addr)[0]

    def real(self, addr):
        return struct.unpack_from(">f", self.original, addr)[0]

    def put(self, addr, value, original=None):
        if addr in self.written:
            raise ValueError("0x%06X patched twice" % addr)
        if original is not None and self.word(addr) != original:
            raise ValueError("0x%06X: 0x%08X, not the game's 0x%08X: another version of the program?"
                             % (addr, self.word(addr), original))
        self.written.add(addr)
        struct.pack_into(">I", self.data, addr, value & 0xFFFFFFFF)

    def put_free(self, words):
        """Words in free space ({address: word}): zeros in the game's program."""
        for addr, value in sorted(words.items()):
            self.put(addr, value, 0)

    def put_table(self, table):
        for addr, original, patched in table:
            self.put(addr, patched, original)


# --- The game's addresses (RAM; the program's are its CROM offsets)
MENU_STATE = 0x22435C          # 17: a race
COURSE = 0x22CF44              # 1: FOREST
PARTICLES = 0x22B2F4           # 300 of 0x18 bytes: x, y, z, their speeds
PARTICLE_COUNT, PARTICLE_SIZE = 300, 0x18
FALL_SPEED = 0x0ED0E8          # the particles' speed, m a frame
SNOW_FALL, RAIN_FALL = 0xBD700000, 0xBE800000   # -0.0586, -0.25
NODE_TEMPLATE_WORD2 = 0x4A9888  # the culling node template's word 2: a texture offset
DRAW_MODEL = 0x06D5EC          # the game's: a model drawn
QUEUE_TEXTURE = 0x0390E4       # the game's: a texture queued for the Real3D (r3: VROM address or RAM | 0x80000000,
                               # header, size)
CAR_POSITION = 0x22449C        # the player's car: x, y, z
LINE, LINE_POINTS = 0x0DE258, 255   # FOREST's line: x, y, z
SHELTER_FIRST, SHELTER_LAST = 103, 130   # the line's points under the rocks of the gorge (two more each side)
RAIN_OFFSET = 0x8203           # 128, 96 from the snowflake's texture: page 1, 384, 640 (FOREST does not use it)
G = 0x22CF14                   # the game's state: mode, course, stages, times...

IMAGE_DRAW = 0x068384          # the game's: an image of tiles drawn (r3 x, r4 y, r5 its set, r6 its index, r7 the name
                               # table)
TILE_WRITE = 0x067734          # the game's: a name table entry written (r3 its address, r4 the code)
TILEGEN = 0xF1000000           # the tile generator's RAM (8-bit patterns at code x 64), as the PowerPC reaches it
LABEL_PATTERNS = 0x3114        # patterns the final results do not show (the yellow DESERT STAGE's, which the patch's
                               # label replaces there), the labels' palette

# --- The patch's free space: padding at the end of the program's first part (copied to RAM, never written)
FREE, FREE_END = 0x0FCE60, 0x100000
CODE = 0x0FCE80                # the weather's routines
CHAMP = 0x0FD380               # the championship's stages, their times, the routines reading them
ENDING = 0x0FD400              # the race's end's tables (0x360)
HEIGHTS = 0x0FD760             # the particles' heights at the start
VARS = 0x0FDC10                # the shelter's: the car's x last time, the answer; 1e30
DROP_ENTRY = 0x0FDC20          # the drop's texture, queued: RAM | 0x80000000, header, size
LABEL_RECIPE = 0x0FDC30        # the FOREST label's letters
PANEL_ENTRIES = 0x0FD088       # FOREST's panel's two model entries (16 bytes each, as the table's)
PANEL_MODEL_NORMAL = (PANEL_ENTRIES - 0x0A4BA8) // 16   # entries of the table of models (MODEL_TABLE)
PANEL_MODEL_HIGHLIGHTED = PANEL_MODEL_NORMAL + 1
PANEL_STATE = 0x0FD0A8         # the menus' state at the panels' last drawing
PANEL_CODE = 0x0FD0B0          # the panel's routines
DROP = 0x0FDDC0                # the drop's texture as the Real3D takes it: size, header, texels
LABEL_BUFFER = 0x0FE880        # RAM only (no patch's word): the label's pixels, 160 x 32
LABEL_BUFFER_END = LABEL_BUFFER + 160 * 32
LABEL_CODE = LABEL_BUFFER_END    # the final results' FOREST label's routines
# --- The CROM's free space (read at 0xFF800000 + offset)
ROUTE = 0x2C0000               # FOREST's opponents' route, 500 points
REFLECTION = 0x2C8000          # FOREST's cars' reflections' texture
PANEL_DATA = 0x2D0000          # FOREST's panel: its overlay's models, then its photo and title textures
TITLE_CGM = 0x2F1000           # the title before FOREST's races: a CGM image

# FOREST's panel (the panel option): MOUNTAIN's model (as the carousel's tables give FOREST), then over its photo and
# title an overlay of the patch's: two models in polygon RAM (never written by the game: its models use up to 0x1675FF),
# their entries in the game's table of models (0x0A4BA8 + 16 x id: ids whose entries fall in the patch's free space)
MODEL_TABLE = 0x0A4BA8
MODEL_DRAW = 0x03902C          # the game's: a model of the table drawn (r3 its id, f1 its scale)
DMA_COPY = 0x06F468            # the game's: a DMA (r3 to, r4 from, r5 words), done when it returns
TEXTURE_FIFO = 0x94000000
POLYGON_RAM = 0x98000000
PANEL_POLY_NORMAL, PANEL_POLY_HIGHLIGHTED = 0xFFC00, 0xFFC40     # word addresses in polygon RAM
MOUNTAIN_PANEL_HIGHLIGHTED = 0x2E0
CROM_BASE = 0xFF800000


CAROUSEL = [
    # The carousel's intro: five panels shown (state 5 of the menus)
    (0x047420, 0x2C800003, 0x2C800004),  # cmpwi cr1, r0, 3 -> 4
    (0x047430, 0x38000004, 0x38000005),  # li r0, 4 -> 5
    # Its step, pi/2 -> 2pi/5, and its spacing, 1/4 -> 1/5 of a turn
    (0x0E97EC, 0x3FC90FDB, 0x3FA0D97C),
    (0x0E9814, 0x3E800000, 0x3E4CCCCD),
    # Its index modulo 5: >= 5 -> 0 (cmpwi 4 -> 5), -1 -> 4 (clrlwi r9,r9,30 -> rlwinm r9,r9,0,29,29)
    (0x00599C, 0x2C8B0004, 0x2C8B0005),
    (0x0059BC, 0x552907BE, 0x5529077A),
    (0x005D44, 0x2C8B0004, 0x2C8B0005),
    (0x005D64, 0x552907BE, 0x5529077A),
    (0x005E20, 0x2C8B0004, 0x2C8B0005),
    (0x005E44, 0x552907BE, 0x5529077A),
    (0x005F28, 0x2C8A0004, 0x2C8A0005),
    (0x005F50, 0x552907BE, 0x5529077A),
    # Its index from its angle, counted from 5 (li 4 -> 5), its angle from its index, (5 - index) steps
    (0x005CE8, 0x39600004, 0x39600005),
    (0x005DBC, 0x39600004, 0x39600005),
    (0x005ECC, 0x39400004, 0x39400005),
    (0x0056F8, 0x20000004, 0x20000005),
    (0x005F70, 0x200A0004, 0x200A0005),
    # The highlight flags, 0x47D424 -> 0x0F24A0 (lis/addi pairs), five of them cleared or set
    (0x0050E0, 0x3D200048, 0x3D20000F),
    (0x0050E4, 0x38E9D424, 0x38E924A0),
    (0x00537C, 0x3D200048, 0x3D20000F),
    (0x005380, 0x3929D424, 0x392924A0),
    (0x0058D4, 0x3D200048, 0x3D20000F),
    (0x0058D8, 0x38E9D424, 0x38E924A0),
    (0x00594C, 0x3D200048, 0x3D20000F),
    (0x005950, 0x3929D424, 0x392924A0),
    (0x0059C8, 0x3D200048, 0x3D20000F),
    (0x0059CC, 0x3929D424, 0x392924A0),
    (0x0059E4, 0x3D200048, 0x3D20000F),
    (0x0059E8, 0x3929D424, 0x392924A0),
    (0x00606C, 0x3D200048, 0x3D20000F),
    (0x006070, 0x3B89D424, 0x3B8924A0),
    (0x045DEC, 0x3D200048, 0x3D20000F),
    (0x045DF0, 0x3B69D424, 0x3B6924A0),
    (0x04622C, 0x3D200048, 0x3D20000F),
    (0x046230, 0x38C9D424, 0x38C924A0),
    (0x046288, 0x3D200048, 0x3D20000F),
    (0x04628C, 0x3929D424, 0x392924A0),
    (0x047564, 0x3D200048, 0x3D20000F),
    (0x047568, 0x3929D424, 0x392924A0),
    (0x0058E8, 0x39400003, 0x39400004),  # loops over the flags: 4 -> 5 words
    (0x0058EC, 0x3960000C, 0x39600010),
    (0x005958, 0x39400003, 0x39400004),
    (0x00595C, 0x3800000C, 0x38000010),
    (0x046240, 0x39400003, 0x39400004),
    (0x046244, 0x3900000C, 0x39000010),
    # The index to the course, 0x0F1F20 -> 0x0F24B4
    (0x046AE8, 0x3D20000F, 0x3D20000F),
    (0x046AEC, 0x39291F20, 0x392924B4),
    (0x048470, 0x3D20000F, 0x3D20000F),
    (0x048474, 0x39291F20, 0x392924B4),
    # The opponents' route: 426 points a lap on FOREST (MOUNTAIN's 342 copied; see WriteRoute)
    (0x0EE104, 0x00000156, 0x000001AA),
    # The panels' models, highlighted 0x0E9790 -> 0x0F24DC, normal 0x0E9780 -> 0x0F24C8 (the carousel's and the
    # panels' other drawing's)
    (0x006074, 0x3D20000F, 0x3D20000F),
    (0x006078, 0x3BA99790, 0x3BA924DC),
    (0x00607C, 0x3D20000F, 0x3D20000F),
    (0x006080, 0x3BC99780, 0x3BC924C8),
    (0x045DF4, 0x3D20000F, 0x3D20000F),
    (0x045DF8, 0x3B899790, 0x3B8924DC),
    (0x045DFC, 0x3D20000F, 0x3D20000F),
    (0x045E00, 0x3BA99780, 0x3BA924C8),
    # Linked cabinets (the battle): the cabinets' choices counted for five panels (the program clears five counters
    # but masks the index to 0-3, FOREST counted as DESERT), the most chosen among five (0x0E97DC: 0 1 2 3 -> 0x0F24F0)
    (0x0055D0, 0x5529173A, 0x5529103A),  # rlwinm r9,r9,2,28,29 -> slwi r9,r9,2
    (0x00561C, 0x3D20000F, 0x3D20000F),
    (0x005620, 0x38C997DC, 0x38C924F0),
    (0x005648, 0x2C8A0003, 0x2C8A0004),  # cmpwi cr1, r10, 3 -> 4
    (0x0F24F0, 0x415F4445, 0x00000000),
    (0x0F24F4, 0x53455254, 0x00000001),
    (0x0F24F8, 0x20202020, 0x00000002),
    (0x0F24FC, 0x00000000, 0x00000003),
    (0x0F2500, 0x524E445F, 0x00000004),
    # The tables, in place of the debug names RND_*: the course of each index (DESERT, MOUNTAIN, RIVIERA, SNOWY,
    # FOREST)
    (0x0F24B4, 0x5350494E, 0x00000003),
    (0x0F24B8, 0x5F4D4F4E, 0x00000002),
    (0x0F24BC, 0x41434F20, 0x00000004),
    (0x0F24C0, 0x00000000, 0x00000005),
    (0x0F24C4, 0x524E445F, 0x00000001),
    # The panels' models, normal: the four of the game's, FOREST's (MOUNTAIN's model 0x2DF until the patch has its own)
    (0x0F24C8, 0x415F5357, 0x000002C4),
    (0x0F24CC, 0x45444953, 0x000002DF),
    (0x0F24D0, 0x48202020, 0x00000322),
    (0x0F24D4, 0x00000000, 0x00000329),
    (0x0F24D8, 0x524E445F, 0x000002DF),
    # Highlighted (FOREST's: MOUNTAIN's 0x2E0)
    (0x0F24DC, 0x415F4D4F, 0x000002C5),
    (0x0F24E0, 0x4E41434F, 0x000002E0),
    (0x0F24E4, 0x20202020, 0x00000323),
    (0x0F24E8, 0x00000000, 0x0000032A),
    (0x0F24EC, 0x524E445F, 0x000002E0),
    # The highlight flags, all set (as the game sets them before the course selection)
    (0x0F24A0, 0x44454641, 0x00000001),
    (0x0F24A4, 0x554C5420, 0x00000001),
    (0x0F24A8, 0x20202020, 0x00000001),
    (0x0F24AC, 0x00000000, 0x00000001),
    (0x0F24B0, 0x524E445F, 0x00000001),
]

PRACTICE_TIMES = [
    # The practice's time (mode 2; the championship's, mode 0, has FOREST's): 999 s for FOREST at the five
    # difficulties (0xE62C8 + ((difficulty x 8 + course) x 12) x 4), so its clock never ran. As the other courses: 60 s
    # at the start, then a credit at each of its checkpoints (sectors 0x2B, 0x8D and the lap's end, 0xE5828 + course x
    # 0x14), each the time of the section that follows at the average speed MOUNTAIN asks at that difficulty scaled to
    # 47 m/s at the first (a lap of 91 s; the practice's rival laps in 93 s), in half seconds; then -1
    (0x0E62F8, 0x0000EA24, 0x00000E10),
    (0x0E62FC, 0xFFFFFFFF, 0x000006AE),
    (0x0E6300, 0x00000000, 0x000007DA),
    (0x0E6304, 0x00000000, 0x000002EE),
    (0x0E6308, 0x00000000, 0x000006AE),
    (0x0E630C, 0x00000000, 0x000007DA),
    (0x0E6310, 0x00000000, 0x000002EE),
    (0x0E6314, 0x00000000, 0x000006AE),
    (0x0E6318, 0x00000000, 0x000007DA),
    (0x0E631C, 0x00000000, 0xFFFFFFFF),
    (0x0E6478, 0x0000EA24, 0x00000E10),
    (0x0E647C, 0xFFFFFFFF, 0x00000690),
    (0x0E6480, 0x00000000, 0x000007DA),
    (0x0E6484, 0x00000000, 0x000002EE),
    (0x0E6488, 0x00000000, 0x00000690),
    (0x0E648C, 0x00000000, 0x000007DA),
    (0x0E6490, 0x00000000, 0x000002EE),
    (0x0E6494, 0x00000000, 0x00000690),
    (0x0E6498, 0x00000000, 0x000007DA),
    (0x0E649C, 0x00000000, 0xFFFFFFFF),
    (0x0E65F8, 0x0000EA24, 0x00000E10),
    (0x0E65FC, 0xFFFFFFFF, 0x00000654),
    (0x0E6600, 0x00000000, 0x00000780),
    (0x0E6604, 0x00000000, 0x000002D0),
    (0x0E6608, 0x00000000, 0x00000654),
    (0x0E660C, 0x00000000, 0x00000780),
    (0x0E6610, 0x00000000, 0x000002D0),
    (0x0E6614, 0x00000000, 0x00000654),
    (0x0E6618, 0x00000000, 0x00000780),
    (0x0E661C, 0x00000000, 0xFFFFFFFF),
    (0x0E6778, 0x0000EA24, 0x00000E10),
    (0x0E677C, 0xFFFFFFFF, 0x00000744),
    (0x0E6780, 0x00000000, 0x000008AC),
    (0x0E6784, 0x00000000, 0x00000348),
    (0x0E6788, 0x00000000, 0x00000744),
    (0x0E678C, 0x00000000, 0x000008AC),
    (0x0E6790, 0x00000000, 0x00000348),
    (0x0E6794, 0x00000000, 0x00000744),
    (0x0E6798, 0x00000000, 0x000008AC),
    (0x0E679C, 0x00000000, 0xFFFFFFFF),
    (0x0E68F8, 0x0000EA24, 0x00000E10),
    (0x0E68FC, 0xFFFFFFFF, 0x000006EA),
    (0x0E6900, 0x00000000, 0x00000816),
    (0x0E6904, 0x00000000, 0x0000030C),
    (0x0E6908, 0x00000000, 0x000006EA),
    (0x0E690C, 0x00000000, 0x00000816),
    (0x0E6910, 0x00000000, 0x0000030C),
    (0x0E6914, 0x00000000, 0x000006EA),
    (0x0E6918, 0x00000000, 0x00000816),
    (0x0E691C, 0x00000000, 0xFFFFFFFF),
]


# The final results' fifth row: the game draws the yellow DESERT STAGE there (its image 11, the row's index + 7: no
# FOREST among its labels). The patch's label instead, made from the green labels on the screen above it (DESERT's two
# rows up, SNOWY's one, as the game drew them: no graphics of the game's in the patch): FOREST's letters, each taken
# from one of theirs (F: DESERT's first E without its bottom bar, O: SNOWY's, R E S T: DESERT's) by its columns in each
# of the label's 32 rows (only its colours above 1, the outline's 1 drawn again around them), a pixel apart. Each
# letter: the label it comes from (its rows above: 16 DESERT's, 8 SNOWY's), the shift of its columns, and in each
# row its first and last column (255, 0: none)
LABEL_LETTERS = [
    (16, -23, [(255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (29, 43), (28, 44), (28, 43), (28, 43), (27, 34), (27, 34), (27, 34), (27, 34), (27, 34), (27, 34), (26, 40), (26, 41), (26, 40), (26, 33), (26, 33), (26, 33), (25, 33), (25, 33), (25, 32), (25, 32), (25, 32), (25, 32), (24, 31), (24, 31), (24, 31), (255, 0)]),   # DESERT's
    (8, -22, [(255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (55, 62), (52, 64), (51, 65), (50, 66), (49, 66), (48, 67), (47, 67), (47, 67), (47, 67), (46, 67), (46, 67), (46, 67), (46, 67), (45, 67), (45, 66), (45, 66), (45, 66), (45, 65), (45, 65), (46, 65), (46, 64), (47, 63), (47, 62), (48, 61), (50, 59), (255, 0)]),   # SNOWY's
    (16, -37, [(255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (88, 100), (88, 103), (87, 104), (87, 105), (87, 106), (87, 106), (87, 106), (87, 106), (86, 105), (86, 105), (86, 104), (86, 103), (86, 102), (86, 101), (85, 103), (85, 103), (85, 103), (85, 103), (85, 103), (84, 103), (84, 103), (84, 103), (84, 103), (84, 103), (84, 103), (255, 0)]),   # DESERT's
    (16, 47, [(255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (29, 43), (28, 44), (28, 43), (28, 43), (27, 34), (27, 34), (27, 34), (27, 34), (27, 34), (27, 34), (26, 40), (26, 41), (26, 40), (26, 33), (26, 33), (26, 33), (25, 33), (25, 33), (25, 32), (25, 32), (25, 32), (25, 41), (24, 41), (24, 42), (24, 41), (255, 0)]),   # DESERT's
    (16, 49, [(255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (53, 58), (50, 60), (49, 62), (48, 62), (47, 62), (46, 63), (46, 62), (46, 62), (46, 59), (46, 57), (46, 58), (46, 60), (47, 60), (48, 61), (49, 62), (47, 62), (44, 62), (44, 62), (44, 62), (44, 61), (44, 61), (44, 60), (44, 59), (45, 58), (47, 56), (255, 0)]),   # DESERT's
    (16, 6, [(255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (255, 0), (109, 127), (108, 128), (108, 127), (108, 127), (114, 121), (113, 121), (113, 121), (113, 121), (113, 121), (112, 120), (112, 120), (112, 120), (112, 120), (112, 120), (112, 120), (112, 119), (111, 119), (111, 119), (111, 119), (111, 119), (111, 118), (110, 118), (110, 118), (110, 118), (110, 118), (255, 0)]),   # DESERT's
]


def texture_header(x, y, page, width, height, mipmaps=True):
    """A Real3D texture header: 16-bit texels, both bytes written (as the game's)."""
    code = {32: 0, 64: 1, 128: 2, 256: 3, 512: 4, 1024: 5, 2048: 6}
    return ((0 if mipmaps else 1) << 24 | 1 << 23 | 1 << 22 | 1 << 21 | page << 20 | code[height] << 17
            | code[width] << 14 | (y // 32) << 7 | 1 << 6 | (x // 32))


def texture_words(texels, width, height, mipmaps=True, average=None):
    """The texels (row 0 at the texture's y) as the Real3D takes them: 8x8 tiles (smaller for the small mipmaps),
    in each 2x2 blocks along pairs of rows; then the mipmaps, each half the last, down to 2 texels (RGBA4 averaged)"""
    average = average or average_rgba4
    levels = [(texels, width, height)]
    w, h, t = width, height, texels
    while mipmaps and w >= 4 and h >= 4:
        t = [average(t[(2 * y) * w + 2 * x], t[(2 * y) * w + 2 * x + 1], t[(2 * y + 1) * w + 2 * x],
                     t[(2 * y + 1) * w + 2 * x + 1]) for y in range(h // 2) for x in range(w // 2)]
        w, h = w // 2, h // 2
        levels.append((t, w, h))
    out = []
    for t, w, h in levels:
        tw, th = min(8, w), min(8, h)
        for ty in range(0, h, th):
            for tx in range(0, w, tw):
                for r in range(0, th, 2):
                    for p in range(0, tw, 2):
                        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
                            out.append(t[(ty + r + dy) * w + tx + p + dx])
    if len(out) % 2:
        out.append(0)
    return [out[i] << 16 | out[i + 1] for i in range(0, len(out), 2)]


def average_rgba4(*texels):
    """RGBA4 texels averaged field by field"""
    return sum(((sum((t >> s) & 15 for t in texels) + 2) // 4) << s for s in (12, 8, 4, 0))


def drop_texels():
    """The drop: a narrow vertical ellipse, light blue-grey, its edges soft (RGBA4, the particles' format)"""
    out = []
    for y in range(32):
        dy = (y + 0.5 - 16) / 15.0
        for x in range(32):
            dx = (x + 0.5 - 16) / 5.0
            a = min(max(1.4 * (1 - dx * dx - dy * dy), 0.0), 0.9)
            out.append(0xD << 12 | 0xE << 8 | 0xF << 4 | int(a * 15 + 0.5))
    return out


def texture_blob(header, words):
    """A texture as the game queues it from RAM: its size word (8 x words + 12), its header, its texels"""
    return [8 * len(words) + 12, header] + words


def particle_heights():
    """The particles' heights over their box (-4 to 4 m), the same each time"""
    seed, out = 0x2F6B1A3D, []
    for _ in range(PARTICLE_COUNT):
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        out.append(struct.unpack(">I", struct.pack(">f", -4.0 + 8.0 * (seed >> 8) / float(1 << 24)))[0])
    return out


def float_bits(x):
    return struct.unpack(">I", struct.pack(">f", x))[0]


RAIN_SOURCE = """
; In place of the particles' drawing (the call at 0x24A10): during FOREST's races, their texture offset to the
; drop, their speed a rain's, none drawn under the rocks; the game's arguments (r3-r10, f1-f4) kept for its call
rain:
    stwu    r1, -96(r1)
    mflr    r0
    stw     r0, 100(r1)
    stw     r3, 8(r1)
    stw     r4, 12(r1)
    stw     r5, 16(r1)
    stw     r6, 20(r1)
    stw     r7, 24(r1)
    stw     r8, 28(r1)
    stw     r9, 32(r1)
    stw     r10, 36(r1)
    stw     r31, 40(r1)
    stfd    f1, 48(r1)
    stfd    f2, 56(r1)
    stfd    f3, 64(r1)
    stfd    f4, 72(r1)
    li      r31, 0
    lis     r11, ha(MENU_STATE)
    lwz     r0, lo(MENU_STATE)(r11)
    cmpwi   r0, 17
    bne     snow
    lis     r11, ha(COURSE)
    lwz     r0, lo(COURSE)(r11)
    cmpwi   r0, 1
    bne     snow
    lis     r0, hi(RAIN_FALL)
    lis     r11, ha(FALL_SPEED)
    stw     r0, lo(FALL_SPEED)(r11)
    bl      spread
    bl      sheltered
    cmpwi   r3, 0
    bne     rain_end
    ori     r31, r31, RAIN_OFFSET
    b       rain_draw
snow:
    lis     r0, hi(SNOW_FALL)
    lis     r11, ha(FALL_SPEED)
    stw     r0, lo(FALL_SPEED)(r11)
rain_draw:
    lis     r11, ha(NODE_TEMPLATE_WORD2)
    stw     r31, lo(NODE_TEMPLATE_WORD2)(r11)
    lwz     r3, 8(r1)
    lwz     r4, 12(r1)
    lwz     r5, 16(r1)
    lwz     r6, 20(r1)
    lwz     r7, 24(r1)
    lwz     r8, 28(r1)
    lwz     r9, 32(r1)
    lwz     r10, 36(r1)
    lfd     f1, 48(r1)
    lfd     f2, 56(r1)
    lfd     f3, 64(r1)
    lfd     f4, 72(r1)
    bl      DRAW_MODEL
    li      r0, 0
    lis     r11, ha(NODE_TEMPLATE_WORD2)
    stw     r0, lo(NODE_TEMPLATE_WORD2)(r11)
rain_end:
    lwz     r31, 40(r1)
    lwz     r0, 100(r1)
    mtlr    r0
    addi    r1, r1, 96
    blr

; The particles all at one height (FOREST's start: a single sheet falling, drops in bursts): spread over their box,
; and the drop's texture queued (the race's textures are loaded then)
spread:
    lis     r11, ha(PARTICLES + 4)
    addi    r11, r11, lo(PARTICLES + 4)
    lwz     r0, 0(r11)
    lwz     r12, PARTICLE_SIZE(r11)
    cmpw    r0, r12
    bne     spread_end
    li      r12, PARTICLE_COUNT - 1
    mtctr   r12
    addi    r10, r11, PARTICLE_SIZE
spread_check:
    lwz     r12, 0(r10)
    cmpw    r0, r12
    bne     spread_end
    addi    r10, r10, PARTICLE_SIZE
    bdnz    spread_check
    lis     r10, ha(HEIGHTS)
    addi    r10, r10, lo(HEIGHTS)
    li      r12, PARTICLE_COUNT
    mtctr   r12
spread_write:
    lwz     r0, 0(r10)
    stw     r0, 0(r11)
    addi    r10, r10, 4
    addi    r11, r11, PARTICLE_SIZE
    bdnz    spread_write
    lis     r3, ha(DROP_ENTRY)
    addi    r3, r3, lo(DROP_ENTRY)
    b       QUEUE_TEXTURE
spread_end:
    blr

; r3: 1 if the car is under the rocks (the point of FOREST's line nearest to it there), kept while it stays still
sheltered:
    lis     r12, ha(VARS)
    addi    r12, r12, lo(VARS)
    lis     r11, ha(CAR_POSITION)
    addi    r11, r11, lo(CAR_POSITION)
    lwz     r0, 0(r11)
    lwz     r3, 0(r12)
    cmpw    r0, r3
    bne     sheltered_find
    lwz     r3, 4(r12)
    blr
sheltered_find:
    stw     r0, 0(r12)
    lfs     f1, 0(r11)
    lfs     f2, 8(r11)
    lfs     f0, 8(r12)
    lis     r10, ha(LINE)
    addi    r10, r10, lo(LINE)
    li      r9, 0
    li      r3, 0
sheltered_point:
    lfs     f3, 0(r10)
    lfs     f4, 8(r10)
    fsubs   f3, f3, f1
    fsubs   f4, f4, f2
    fmuls   f3, f3, f3
    fmuls   f4, f4, f4
    fadds   f3, f3, f4
    fcmpu   cr0, f3, f0
    bge     sheltered_next
    fmr     f0, f3
    mr      r3, r9
sheltered_next:
    addi    r10, r10, 12
    addi    r9, r9, 1
    cmpwi   r9, LINE_POINTS
    blt     sheltered_point
    subi    r3, r3, SHELTER_FIRST
    cmplwi  r3, SHELTER_LAST - SHELTER_FIRST
    li      r3, 0
    bgt     sheltered_store
    li      r3, 1
sheltered_store:
    stw     r3, 4(r12)
    blr
"""


LABEL_SOURCE = r'''
; In place of the game's image drawing in the final results' and the name entry's rows (r3 x, r4 y, r5 the set, r6
; the image, r7 the name table): the fifth row's (image 11) is FOREST's label; the others, and the fifth when the
; labels above are not there, the game's
results_row:
    cmpwi   r6, 11
    beq     label
    b       IMAGE_DRAW
label:
    stwu    r1, -80(r1)
    mflr    r0
    stw     r0, 84(r1)
    stw     r21, 8(r1)
    stw     r22, 12(r1)
    stw     r23, 16(r1)
    stw     r24, 20(r1)
    stw     r25, 24(r1)
    stw     r26, 28(r1)
    stw     r27, 32(r1)
    stw     r28, 36(r1)
    stw     r29, 40(r1)
    stw     r30, 44(r1)
    stw     r31, 48(r1)
    stw     r5, 52(r1)
    mr      r29, r3
    mr      r28, r4
    mr      r27, r7
    mr      r5, r29
    subi    r6, r28, 16
    bl      entry_code
    cmpwi   r11, 0
    beq     label_game
    mr      r5, r29
    subi    r6, r28, 8
    bl      entry_code
    cmpwi   r11, 0
    beq     label_game

    ; the pixels cleared
    lis     r26, ha(LABEL_BUFFER)
    addi    r26, r26, lo(LABEL_BUFFER)
    li      r0, 0
    li      r9, 1280
    mtctr   r9
    mr      r9, r26
label_clear:
    stw     r0, 0(r9)
    addi    r9, r9, 4
    bdnz    label_clear

    ; the letters' colours (above 1) copied, each row's columns shifted
    lis     r31, ha(LABEL_RECIPE)
    addi    r31, r31, lo(LABEL_RECIPE)
    li      r30, 6
label_letter:
    lbz     r25, 0(r31)
    subf    r25, r25, r28
    lbz     r24, 1(r31)
    extsb   r24, r24
    addi    r31, r31, 2
    li      r23, 0
label_row:
    lbz     r22, 0(r31)
    lbz     r21, 1(r31)
    addi    r31, r31, 2
    cmpwi   r22, 255
    beq     label_row_next
label_pixel:
    mr      r3, r22
    mr      r4, r23
    bl      pixel
    cmplwi  r3, 1
    ble     label_pixel_next
    mulli   r9, r23, 160
    add     r9, r9, r22
    add     r9, r9, r24
    stbx    r3, r26, r9
label_pixel_next:
    addi    r22, r22, 1
    cmpw    r22, r21
    ble     label_pixel
label_row_next:
    addi    r23, r23, 1
    cmpwi   r23, 32
    blt     label_row
    subi    r30, r30, 1
    cmpwi   r30, 0
    bgt     label_letter

    ; the outline: 1 where nothing is, next to a colour
    li      r23, 0
outline_y:
    li      r22, 0
outline_x:
    mulli   r9, r23, 160
    add     r9, r9, r22
    lbzx    r0, r26, r9
    cmpwi   r0, 0
    bne     outline_next
    li      r10, -1
outline_dy:
    add     r5, r23, r10
    cmplwi  r5, 31
    bgt     outline_dy_next
    li      r11, -1
outline_dx:
    add     r6, r22, r11
    cmplwi  r6, 159
    bgt     outline_dx_next
    mulli   r7, r5, 160
    add     r7, r7, r6
    lbzx    r0, r26, r7
    cmplwi  r0, 1
    ble     outline_dx_next
    li      r0, 1
    stbx    r0, r26, r9
    b       outline_next
outline_dx_next:
    addi    r11, r11, 1
    cmpwi   r11, 1
    ble     outline_dx
outline_dy_next:
    addi    r10, r10, 1
    cmpwi   r10, 1
    ble     outline_dy
outline_next:
    addi    r22, r22, 1
    cmpwi   r22, 160
    blt     outline_x
    addi    r23, r23, 1
    cmpwi   r23, 32
    blt     outline_y

    ; its 80 tiles as patterns (8-bit: pixel 0 in the top byte, as the game reads them, byte-reversed)
    li      r23, 0
    lis     r21, ha(TILEGEN + LABEL_PATTERNS * 64)
    addi    r21, r21, lo(TILEGEN + LABEL_PATTERNS * 64)
label_tile:
    li      r0, 20
    divwu   r5, r23, r0
    mulli   r6, r5, 20
    subf    r6, r6, r23
    mulli   r9, r5, 1280
    slwi    r6, r6, 3
    add     r9, r9, r6
    add     r9, r9, r26
    li      r22, 8
    mtctr   r22
label_tile_row:
    lwz     r0, 0(r9)
    stwbrx  r0, r0, r21
    lwz     r0, 4(r9)
    addi    r21, r21, 4
    stwbrx  r0, r0, r21
    addi    r21, r21, 4
    addi    r9, r9, 160
    bdnz    label_tile_row
    addi    r23, r23, 1
    cmpwi   r23, 80
    blt     label_tile

    ; and in the name table, where the game's image went
    li      r23, 0
label_name:
    li      r0, 20
    divwu   r5, r23, r0
    mulli   r6, r5, 20
    subf    r6, r6, r23
    add     r5, r5, r28
    add     r6, r6, r29
    slwi    r3, r5, 6
    add     r3, r3, r6
    slwi    r3, r3, 1
    add     r3, r3, r27
    addi    r4, r23, LABEL_PATTERNS
    bl      TILE_WRITE
    addi    r23, r23, 1
    cmpwi   r23, 80
    blt     label_name
    li      r3, 11
    b       label_end

label_game:
    mr      r3, r29
    mr      r4, r28
    lwz     r5, 52(r1)
    li      r6, 11
    mr      r7, r27
    bl      IMAGE_DRAW
label_end:
    lwz     r21, 8(r1)
    lwz     r22, 12(r1)
    lwz     r23, 16(r1)
    lwz     r24, 20(r1)
    lwz     r25, 24(r1)
    lwz     r26, 28(r1)
    lwz     r27, 32(r1)
    lwz     r28, 36(r1)
    lwz     r29, 40(r1)
    lwz     r30, 44(r1)
    lwz     r31, 48(r1)
    lwz     r0, 84(r1)
    mtlr    r0
    addi    r1, r1, 80
    blr

; r11: the code (15 bits) of the name table's entry at tile r5, r6 (two a word: the even one in the top half)
entry_code:
    slwi    r0, r6, 6
    add     r0, r0, r5
    slwi    r0, r0, 1
    add     r9, r27, r0
    andi.   r10, r9, 2
    rlwinm  r9, r9, 0, 0, 29
    lwbrx   r11, r0, r9
    bne     entry_low
    srwi    r11, r11, 16
entry_low:
    clrlwi  r11, r11, 17
    blr

; r3: the pixel r3, r4 (x, y) of the label whose first row is r25 (at x r29), as the screen has it
pixel:
    mflr    r12
    srwi    r5, r3, 3
    add     r5, r5, r29
    srwi    r6, r4, 3
    add     r6, r6, r25
    bl      entry_code
    mtlr    r12
    slwi    r11, r11, 6
    rlwinm  r0, r4, 3, 26, 28
    add     r11, r11, r0
    andi.   r0, r3, 4
    add     r11, r11, r0
    oris    r11, r11, hi(TILEGEN)
    lwbrx   r11, r0, r11
    andi.   r0, r3, 3
    slwi    r0, r0, 3
    subfic  r0, r0, 24
    srw     r11, r11, r0
    andi.   r3, r11, 0xFF
    blr
'''


def symbols():
    return {k: v for k, v in globals().items() if k.isupper() and isinstance(v, int)}


def patch_carousel(p):
    p.put_table(CAROUSEL)


def patch_weather(p):
    sym = symbols()
    code = assemble(RAIN_SOURCE, CODE, sym)
    if max(code) >= CHAMP:
        raise ValueError("the routines overflow their room")
    p.put_free(code)
    p.put(0x024A10, 0x48000001 | ((sym["rain"] - 0x024A10) & 0x3FFFFFC), 0x48048BDD)   # bl 0x6D5EC -> bl rain
    p.put_free({HEIGHTS + 4 * i: h for i, h in enumerate(particle_heights())})
    p.put_free({VARS + 8: float_bits(1e30)})
    words = texture_words(drop_texels(), 32, 32)
    header = texture_header(384, 640, 1, 32, 32)
    blob = texture_blob(header, words)
    if DROP + 4 * len(blob) > FREE_END:
        raise ValueError("the drop's texture overflows the free space")
    p.put_free({DROP + 4 * i: w for i, w in enumerate(blob)})
    p.put_free({DROP_ENTRY: 0x80000000 | DROP, DROP_ENTRY + 4: header, DROP_ENTRY + 8: blob[0]})


def line_points(p):
    return [(p.real(LINE + 12 * i), p.real(LINE + 12 * i + 4), p.real(LINE + 12 * i + 8)) for i in range(LINE_POINTS)]


def patch_route(p):
    """The opponents' route on FOREST: along its line (the game's table by course, 0x1353DC, gives FOREST MOUNTAIN's,
    which leaves its road), as the game's routes are: 500 points from 10 m past the start, 0.55 m above the line, their
    heading -atan2(dx, dz); 426 a lap (the length the program wraps the index at, 0x0EE104, in CAROUSEL)"""
    route_points, lap_points = 500, 426
    line = line_points(p) + [line_points(p)[0]]
    along = [0.0]
    for a, b in zip(line, line[1:]):
        along.append(along[-1] + math.dist(a, b))
    lap = along[-1]

    def at(d):
        d %= lap
        i = next(k for k in range(1, len(along)) if along[k] > d) if d < along[-1] else len(along) - 1
        a, b = line[i - 1], line[i]
        t = (d - along[i - 1]) / max(along[i] - along[i - 1], 1e-3)
        return (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]) + 0.55, a[2] + t * (b[2] - a[2]))

    start = 0.0
    while start < lap and math.hypot(at(start)[0], at(start)[2]) < 10:
        start += 0.25
    spacing = lap / lap_points
    points = [at(start + k * spacing) for k in range(route_points)]
    words = {}
    for k in range(route_points):
        a, b = points[(k + lap_points - 1) % lap_points], points[(k + 1) % lap_points]
        heading = -math.atan2(b[0] - a[0], b[2] - a[2])
        for j, v in enumerate((*points[k], heading)):
            words[ROUTE + 16 * k + 4 * j] = float_bits(v)
    p.put_free(words)
    p.put(0x1353E0, CROM_BASE + ROUTE, 0xFF139A10)


def reflection_texels():
    """The cars' reflections on FOREST: a sheen of the patch's (a soft vertical band of light grey, faint, over faint
    grain), RGBA4. FOREST's own texture is far more opaque than the other courses' (white streaks over the car)"""
    seed, out = 0x1F2E3D4C, []
    for y in range(32):
        for x in range(32):
            seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
            band = math.exp(-((x + 0.5 - 16) / 4.5) ** 2)
            alpha = int(2.4 * band + ((seed >> 24) / 255.0) * 1.2)
            out.append(0xC << 12 | 0xD << 8 | 0xC << 4 | min(alpha, 3))
    return out


def patch_reflection(p):
    """The cars' reflections on FOREST (three layers of the game's models over the car, the texture each course gives
    them, 32x32 at page 1, 928, 832): FOREST's course textures' list (in the program, 12 bytes an entry: the texture's
    address, its header, its size; the game DMAs it from the CROM to the Real3D's texture FIFO when the address is the
    CROM's) gets the patch's (in the CROM's free space) in place of FOREST's own"""
    entry, header, size = 0x0A26FC, 0x00F03D5D, 0x1554
    p.put(entry, CROM_BASE + REFLECTION, 0xFF30B5F8)
    if p.word(entry + 4) != header or p.word(entry + 8) != size:
        raise ValueError("FOREST's textures' list is not the one the patch expects")
    blob = [size, header] + texture_words(reflection_texels(), 32, 32)
    p.put_free({REFLECTION + 4 * i: w for i, w in enumerate(blob)})


PANEL_SOURCE = r"""
; In place of the carousel's model drawing (0x6228: the panel's index in r31; 0x45E7C: in r30): FOREST's panel (index
; 4, MOUNTAIN's model) gets the patch's overlay drawn after it, its photo and title; and at a new course selection (the
; menus' state 5 or 6 after another), the overlay's models and textures are sent first
panel_a:
    mr      r12, r31
    b       panel_draw
panel_b:
    mr      r12, r30
    b       panel_draw
panel_draw:
    stwu    r1, -48(r1)
    mflr    r0
    stw     r0, 52(r1)
    stw     r3, 8(r1)
    stfd    f1, 16(r1)
    stw     r12, 24(r1)
    lis     r11, ha(MENU_STATE)
    lwz     r0, lo(MENU_STATE)(r11)
    lis     r11, ha(PANEL_STATE)
    lwz     r10, lo(PANEL_STATE)(r11)
    stw     r0, lo(PANEL_STATE)(r11)
    cmpwi   r10, 7
    beq     panel_new
    cmpwi   r10, 0
    bne     panel_model
panel_new:
    cmpwi   r0, 7
    beq     panel_model
    bl      panel_send
panel_model:
    lwz     r3, 8(r1)
    lfd     f1, 16(r1)
    bl      MODEL_DRAW
    lwz     r12, 24(r1)
    cmpwi   r12, 4
    bne     panel_end
    lwz     r3, 8(r1)
    cmpwi   r3, MOUNTAIN_PANEL_HIGHLIGHTED
    li      r3, PANEL_MODEL_NORMAL
    bne     panel_overlay
    li      r3, PANEL_MODEL_HIGHLIGHTED
panel_overlay:
    lfd     f1, 16(r1)
    bl      MODEL_DRAW
panel_end:
    lwz     r0, 52(r1)
    mtlr    r0
    addi    r1, r1, 48
    blr

; The overlay's models into polygon RAM (byte-reversed: as the Real3D takes words), its photo and title to the texture
; FIFO (DMAs from the CROM, as the game sends its courses' textures)
panel_send:
    stwu    r1, -16(r1)
    mflr    r0
    stw     r0, 20(r1)
    lis     r9, ha(CROM_BASE + PANEL_DATA)
    addi    r9, r9, lo(CROM_BASE + PANEL_DATA)
    lis     r10, ha(POLYGON_RAM + PANEL_POLY_NORMAL * 4)
    addi    r10, r10, lo(POLYGON_RAM + PANEL_POLY_NORMAL * 4)
    li      r0, 0x40 + PANEL_MODEL_WORDS
    mtctr   r0
panel_send_word:
    lwz     r0, 0(r9)
    stwbrx  r0, r0, r10
    addi    r9, r9, 4
    addi    r10, r10, 4
    bdnz    panel_send_word
    lis     r3, hi(TEXTURE_FIFO)
    lis     r4, ha(CROM_BASE + PANEL_PHOTO)
    addi    r4, r4, lo(CROM_BASE + PANEL_PHOTO)
    li      r5, PANEL_PHOTO_WORDS
    bl      DMA_COPY
    lis     r3, hi(TEXTURE_FIFO)
    lis     r4, ha(CROM_BASE + PANEL_TITLE)
    addi    r4, r4, lo(CROM_BASE + PANEL_TITLE)
    li      r5, PANEL_TITLE_WORDS
    bl      DMA_COPY
    lwz     r0, 20(r1)
    mtlr    r0
    addi    r1, r1, 16
    blr
"""


def panel_models():
    """The overlay's models (normal, highlighted): MOUNTAIN's panel's photo and title polygons (model 0x2DF, 0x2E0) as
    the patch's own, 2 units nearer the viewer, textured at page 1, 1024, 0 and 1024, 128 (Real3D polygon headers: 7
    words, then each vertex x, y, z (24.8 fixed, the normal's byte below) and u, v (1/8 texel))"""
    def vertex(x, y, z, u, v):
        return [(x << 8) & 0xFFFFFF00, (y << 8) & 0xFFFFFF00, ((z << 8) & 0xFFFFFF00) | 0x81, u << 16 | v]

    def texture(h4, h5, x, y):   # the texture's place: x / 32 in h4 bits 0-4 and h5 bit 7, y / 32 in h5 bits 0-4
        return (h4 & ~0x1F) | ((x // 32) >> 1), (h5 & ~0x9F) | (((x // 32) & 1) << 7) | (y // 32)

    words = []
    for colour in (0x666666, 0xFFFFFF):          # the normal panel's tint, the highlighted's (64 words further)
        words += [0] * (-len(words) % 0x40)
        h4, h5 = texture(colour << 8 | 0x68, 0x00014070, 1024, 0)
        words += [0x40, 0x0A, 0, 0xC000001A, h4, h5, 0x84817440]
        words += vertex(-1208, -102, -2, 0, 0) + vertex(1208, -102, -2, 0x800, 0) + \
            vertex(1208, 1105, -2, 0x800, 0x400) + vertex(-1208, 1105, -2, 0, 0x400)
        h4, h5 = texture(colour << 8 | 0x6C, 0x00013078, 1024, 128)
        words += [0x40, 0x0E, 0, 0xC0000019, h4, h5, 0x84817440]      # its last polygon
        words += vertex(-1206, -102, -2, 0, 0x200) + vertex(-1206, -679, -2, 0, 0) + \
            vertex(1206, -679, -2, 0x800, 0) + vertex(1206, -102, -2, 0x800, 0x200)
    return words


PANEL_MODEL_WORDS = 46                       # each model's (two quads)
PANEL_PHOTO = PANEL_DATA + 0x200
PANEL_PHOTO_WORDS = 2 + 21844                # its size and header, its texels with their mipmaps
PANEL_TITLE = PANEL_PHOTO + 4 * PANEL_PHOTO_WORDS
PANEL_TITLE_WORDS = 2 + 10920


def patch_panel(p):
    """FOREST's panel: the overlay (see PANEL_SOURCE, panel_models), its photo and title (forest_panel.py)"""
    models = panel_models()
    if len(models) != 0x40 + PANEL_MODEL_WORDS or PANEL_POLY_HIGHLIGHTED != PANEL_POLY_NORMAL + 0x40:
        raise ValueError("the panel's models")
    p.put_free({PANEL_DATA + 4 * i: w for i, w in enumerate(models) if w})
    photo = texture_words(forest_panel.photo_texels(), 256, 128, average=forest_panel.average_rgb5)
    title = texture_words(forest_panel.title_texels(), 256, 64, average=forest_panel.average_rgb5)
    if len(photo) + 2 != PANEL_PHOTO_WORDS or len(title) + 2 != PANEL_TITLE_WORDS:
        raise ValueError("the panel's textures' sizes")
    p.put_free({PANEL_PHOTO + 4 * i: w for i, w in enumerate(texture_blob(texture_header(1024, 0, 1, 256, 128), photo))})
    p.put_free({PANEL_TITLE + 4 * i: w for i, w in enumerate(texture_blob(texture_header(1024, 128, 1, 256, 64), title))})
    entry = p.word(MODEL_TABLE + 16 * MOUNTAIN_PANEL_HIGHLIGHTED + 8)     # the table's third word, as MOUNTAIN's panel's
    for i, poly in enumerate((PANEL_POLY_NORMAL, PANEL_POLY_HIGHLIGHTED)):
        p.put_free({PANEL_ENTRIES + 16 * i: poly, PANEL_ENTRIES + 16 * i + 8: entry})
    sym = symbols()
    code = assemble(PANEL_SOURCE, PANEL_CODE, sym)
    if max(code) >= CHAMP:
        raise ValueError("the panel's routines overflow their room")
    p.put_free(code)
    for site, label in ((0x006228, "panel_a"), (0x045E7C, "panel_b")):   # bl MODEL_DRAW -> bl panel_*
        p.put(site, 0x48000001 | ((sym[label] - site) & 0x3FFFFFC), 0x48000001 | ((MODEL_DRAW - site) & 0x3FFFFFC))


def cgm_image(rgb, name):
    """A full-screen image (rows from the top, RGB floats, 496x384) as a Sega CGM file, as the game's titles are: a
    flag byte (0x10: 8 bits a pixel), "CGM 2.0", its pages (u16 count; each a palette of 256 colours, 0BBBBBGGGGGRRRRR,
    then a u16 0 and the u16 count of its 8x8 patterns, 64 bytes each, the pixels of each 4 reversed), then its
    images (u16 count; each a 16-byte name, its width and height in tiles, its tiles' codes: page x 256 + pattern).
    Colours: 255 for all the pages (colour 0 none), the tiles alike once"""
    from PIL import Image
    h, w = rgb.shape[:2]
    img = Image.fromarray(np.clip(rgb + 0.5, 0, 255).astype("uint8"), "RGB")
    q = img.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    index = np.array(q, dtype=np.uint16) + 1
    pal = q.getpalette()[:3 * 255]
    palette = [0x8000] + [((pal[3 * i + 2] >> 3) << 10) | ((pal[3 * i + 1] >> 3) << 5) | (pal[3 * i] >> 3) for i in range(255)]
    palette += [0] * (256 - len(palette))
    tiles, codes = {}, []
    for ty in range(h // 8):
        for tx in range(w // 8):
            block = index[8 * ty:8 * ty + 8, 8 * tx:8 * tx + 8]
            data = bytes(int(block[y, x ^ 3]) for y in range(8) for x in range(8))
            if data not in tiles:
                tiles[data] = len(tiles)
            n = tiles[data]
            codes.append((n // 256) << 8 | (n % 256))
    patterns = sorted(tiles, key=tiles.get)
    pages = [patterns[i:i + 256] for i in range(0, len(patterns), 256)]
    out = bytearray(b"\x10CGM 2.0") + struct.pack(">H", len(pages))
    for page in pages:
        out += struct.pack(">256H", *palette) + struct.pack(">HH", 0, len(page)) + b"".join(page)
    out += struct.pack(">H", 1) + name.encode().ljust(16, b"\0") + struct.pack(">hh", w // 8, h // 8)
    out += struct.pack(">%dH" % len(codes), *codes)
    return bytes(out), len(pages)


def patch_title_card(p):
    """The title before FOREST's races: the game loads a race's title from its stage record (0xE8714 + 0x7C x k: word 0
    the course, word 1 its title's CGM image, word 2 its music), and FOREST's records (k 1, 7, 13) have MOUNTAIN's
    (0xFF14991C, in the banked CROM). The patch's instead (forest_panel.title_card_rgb), in the CROM's free space"""
    data, pages = cgm_image(forest_panel.title_card_rgb(), "E_forest")
    data += bytes(-len(data) % 4)
    p.put_free({TITLE_CGM + i: struct.unpack_from(">I", data, i)[0] for i in range(0, len(data), 4)
                if struct.unpack_from(">I", data, i)[0]})
    for k in (1, 7, 13):
        record = 0x0E8714 + 0x7C * k
        if p.word(record) != 1:
            raise ValueError("stage record %d is not FOREST's" % k)
        p.put(record + 4, CROM_BASE + TITLE_CGM, 0xFF14991C)


FOREST_RECORDS = (1, 7, 13)      # FOREST's stage records (0xE8714 + 0x7C x k; word 0 the course, 1 the title, 2 the music)
# The music tracks the stage records use (0x185 0x187 0x189 0x18A 0x18B 0x18E 0x190 0x192), and those between that none
# does: one may have been FOREST's
MUSIC_TRACKS = [("mountain", "MOUNTAIN's (as the game)", None)] +     [("%X" % t, "Track %X (no course's)" % t, t) for t in (0x186, 0x188, 0x18C, 0x18D, 0x18F, 0x191)] +     [("%X" % t, "Track %X (%s)" % (t, n), t) for t, n in ((0x185, "RIVIERA's"), (0x189, "SNOWY's"), (0x18A, "MOUNTAIN's, second"),
                                                        (0x18B, "SNOWY's, second"), (0x18E, "DESERT's"), (0x190, "DESERT's, second"),
                                                        (0x192, "RIVIERA's, second"))]


def music_patch(track):
    def patch(p):
        for k in FOREST_RECORDS:
            record = 0x0E8714 + 0x7C * k
            if p.word(record) != 1:
                raise ValueError("stage record %d is not FOREST's" % k)
            p.put(record + 8, track, p.word(record + 8))
    return patch


def patch_ending(p):
    """The race's end: after CLEARED the staff roll replays the race, its shots from tables by course (FOREST 4) of four
    entries (the game hung reading a fifth). Tables of five, FOREST's shots beside its road (8 m left of the line, 3 m
    up, at the points of the lap MOUNTAIN's are), its replay one camera following the car"""
    end_tables = [0x134440, 0x134580, 0x1346C0, 0x134800]
    roll_tables = [0x134950, 0x134AE0, 0x134C70, 0x134E00]
    end_shots, roll_shots, replay = ENDING + 0x40, ENDING + 0x180, ENDING + 0x310
    words = {}
    for i in range(4):
        words[ENDING + 4 * i] = end_tables[i]
        words[ENDING + 0x20 + 4 * i] = roll_tables[i]
    words[ENDING + 0x10] = end_shots
    words[ENDING + 0x30] = roll_shots
    line = line_points(p)
    n = len(line)

    def camera(record, point):
        x, y, z = line[point % n]
        dx = line[(point + 1) % n][0] - line[(point - 1) % n][0]
        dz = line[(point + 1) % n][2] - line[(point - 1) % n][2]
        d = math.hypot(dx, dz)
        if d > 0:
            x, z = x - dz / d * 8.0, z + dx / d * 8.0
        words[record + 0x18], words[record + 0x1C], words[record + 0x20] = float_bits(x), float_bits(y + 3.0), float_bits(z)

    for i, point in enumerate((59, 140, 163, 186)):
        camera(end_shots + 0x50 * i, point)
        if i < 3:
            words[end_shots + 0x50 * i + 0x3C] = 1
    for i, point in enumerate((33, 59, 140, 163, 186)):
        camera(roll_shots + 0x50 * i, point)
    record = replay + 0xC
    words[record + 4], words[record + 8] = 255, 3
    for k in range(4):
        words[record + 0x1C + 4 * k] = 0xFFFFFFFF
    p.put_free({a: v for a, v in words.items() if v})
    p.put(0x027E80, 0x3D200000 | ha(ENDING), 0x3D200013)              # lis r9: the end shots' tables
    p.put(0x027E84, 0x39290000 | lo(ENDING), 0x39294940)              # addi r9, r9
    p.put(0x028CC8, 0x3D200000 | ha(ENDING + 0x20), 0x3D200013)       # the staff roll's
    p.put(0x028CCC, 0x39290000 | lo(ENDING + 0x20), 0x39294F90)
    p.put(0x1368AC, replay, 0)                                         # the replay cameras of course index 4


def patch_practice_times(p):
    p.put_table(PRACTICE_TIMES)


def patch_championship(p):
    """FOREST as the championship's fifth stage: the stages' list (3 2 5 4, then 1) and number (4 -> 5) at the
    championship's start (0x2C8A0); the stages' times (G+0x174, four words, a buffer after them) in five words of the
    patch's, read and written through routines (the five instructions that used G+0x174 + 4 x stage, rX = G + 4 x stage),
    cleared at the start; the final results and the name entry's times on five rows. FOREST's times are the game's"""
    stages, times, routines = CHAMP, CHAMP + 0x20, CHAMP + 0x40
    p.put(0x02C8A0, 0x3D200000 | ha(stages), 0x3D20000F)       # lis r9: the stages' list
    p.put(0x02C8A4, 0x39290000 | lo(stages), 0x3929D740)       # addi r9, r9
    p.put(0x02C8AC, 0x38000005, 0x38000004)                    # li r0, 4 -> 5: the number of stages
    p.put_free({stages + 4 * k: v for k, v in enumerate((3, 2, 5, 4, 1))})
    delta = times - G
    source = """
    A:  addis r5, r9, ha(D)
        addi  r5, r5, lo(D)
        lwz   r5, 0(r5)
        blr
    B:  addis r5, r29, ha(D)
        addi  r5, r5, lo(D)
        lwz   r5, 0(r5)
        blr
    C:  addis r9, r31, ha(D)
        addi  r9, r9, lo(D)
        lwz   r9, 0(r9)
        blr
    D_: addis r9, r9, ha(D)
        addi  r9, r9, lo(D)
        stw   r0, 0(r9)
        blr
    """
    sym = {"D": delta & 0xFFFFFFFF}
    p.put_free(assemble(source, routines, sym))
    for site, original, routine in ((0x029A58, 0x80A90174, "A"),    # lwz r5, 0x174(r9): a stage's time
                                    (0x02ADA0, 0x80BD0174, "B"),    # lwz r5, 0x174(r29): the final results
                                    (0x04AEF8, 0x80BD0174, "B"),    # the name entry's
                                    (0x02C044, 0x813F0174, "C"),    # lwz r9, 0x174(r31): the stages' records
                                    (0x03E448, 0x90090174, "D_")):  # stw r0, 0x174(r9): a stage's time stored
        p.put(site, 0x48000001 | ((sym[routine] - site) & 0x3FFFFFC), original)
    p.put(0x02CBB4, 0x3C600000 | ha(times), 0x3C600023)        # lis r3: the times cleared
    p.put(0x02CBB8, 0x38630000 | lo(times), 0x3863D088)        # addi r3, r3
    p.put(0x02CBBC, 0x38800014, 0x38800010)                    # li r4, 0x10 -> 0x14 (five)
    p.put(0x02ADB0, 0x2C9E0004, 0x2C9E0003)                    # cmpwi cr1, r30, 3 -> 4: the final results' rows
    p.put(0x04AF1C, 0x2C9E0004, 0x2C9E0003)                    # the name entry's
    patch_label(p)


def patch_label(p):
    """FOREST's label on the final results' and the name entry's fifth row (see LABEL_LETTERS)"""
    sym = symbols()
    code = assemble(LABEL_SOURCE, LABEL_CODE, sym)
    if max(code) >= FREE_END:
        raise ValueError("the label's routines overflow their room")
    p.put_free(code)
    data = bytearray()
    for rowoff, shift, spans in LABEL_LETTERS:
        data += bytes([rowoff, shift & 0xFF]) + b"".join(bytes(span) for span in spans)
    data += bytes(-len(data) % 4)
    if LABEL_RECIPE + len(data) > DROP:
        raise ValueError("the label's letters overflow their room")
    p.put_free({LABEL_RECIPE + i: struct.unpack_from(">I", data, i)[0] for i in range(0, len(data), 4)})
    for site in (0x02AD9C, 0x04AEE8):                         # bl IMAGE_DRAW -> bl results_row
        p.put(site, 0x48000001 | ((sym["results_row"] - site) & 0x3FFFFFC),
              0x48000001 | ((IMAGE_DRAW - site) & 0x3FFFFFC))


DESCRIPTION = """
The practice's course selection gets a fifth panel, FOREST: a complete course of the game's ROMs that no menu leads to.
Its practice clock runs as the other courses' (60 s, then a credit at each checkpoint), its rival follows its own road,
and its race's end plays (the game hung after CLEARED). Its options give it its own panel, title and weather, and
another music than MOUNTAIN's to try.

A patch of the program EPROMs (EPR-20632 to EPR-20635), made by Scripts/RomPatches/srally2_forest.py. Whatever
options are on, the EPROMs keep their CRCs: the test menu's MEMORY TEST says GOOD.
"""

OPTIONS = [
    ("title", "FOREST's title before its races", True, patch_title_card, """
The title shown while FOREST loads is FOREST's (the patch's own: its photo, its name), not MOUNTAIN's.
"""),
    ("panel", "FOREST's panel", True, patch_panel, """
FOREST's panel in the course selection shows FOREST: a photo of the course and its title, the patch's own, drawn over
MOUNTAIN's panel (which FOREST's otherwise is).
"""),
    ("rain", "Rain", True, patch_weather, """
FOREST's weather, SNOWY's falling particles (FOREST's textures have no snowflake: a banner's corner falls instead), falls
as rain: a drop of the patch's, faster, spread over its height, none under the rocks of the gorge.
"""),
    ("reflections", "Faint reflections", True, patch_reflection, """
The cars' reflections on FOREST take a faint texture of the patch's instead of FOREST's own, far more opaque than the
other courses' (white streaks over the car).
"""),
    ("championship", "The championship's fifth stage", False, patch_championship, """
The championship runs DESERT, MOUNTAIN, SNOWY, RIVIERA, then FOREST (its times the game's own); the final results and the
name entry show five stages, the fifth labelled FOREST.
"""),
]


# --- The ROM test (the test menu's MEMORY TEST, 0x73A40): each program EPROM's CRC (CRC-16/CCITT, 0xFFFF first, the
# result inverted) over its 16 bits of each CPU word of the CROM below 0x7FF000 (IC20 the top half of the words at 8n,
# IC19 their bottom half, IC18 and IC17 those at 8n + 4; each half's low byte first), against the CROM's 0x7FFF70 to
# 0x7FFF7C. Each part of the patch (its base, each option, each value of a choice) has two bytes a lane in the CROM's
# free space (CRC_SLOTS + 8 x slot) that cancel its changes' effect on the CRCs: whatever options are on, the EPROMs
# keep their CRCs and the test says GOOD
CRC_POLY = 0x11021
CRC_END = 0x7FF000
CRC_WORDS = CRC_END // 8                 # each lane's 16-bit words
CRC_SLOTS = 0x7FE000                     # free (zeros up to the CRCs at 0x7FFF00)


def _gf_mulmod(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        b >>= 1
        a <<= 1
        if a & 0x10000:
            a ^= CRC_POLY
    return r


def _gf_powx(n):
    """x^n mod the CRC's polynomial"""
    r, base = 1, 2
    while n:
        if n & 1:
            r = _gf_mulmod(r, base)
        base = _gf_mulmod(base, base)
        n >>= 1
    return r


def _gf_inverse(a):
    """a^-1 mod the polynomial (a^(2^16 - 2): the multiplicative group of GF(2)[x]/P is not a field, but x^k is a unit;
    found by the extended Euclid instead)"""
    r0, r1, s0, s1 = CRC_POLY, a, 0, 1
    while r1:
        q, r = 0, r0
        while r and r.bit_length() >= r1.bit_length():
            shift = r.bit_length() - r1.bit_length()
            q ^= 1 << shift
            r ^= r1 << shift
        r0, r1 = r1, r
        qs, t = 0, s1
        # s0 - q * s1 (carry-less)
        prod, qq, sh = 0, q, 0
        while qq:
            if qq & 1:
                prod ^= s1 << sh
            qq >>= 1
            sh += 1
        s0, s1 = s1, s0 ^ prod
    if r0 != 1:
        raise ValueError("not invertible")
    while s0.bit_length() > 16:
        s0 ^= CRC_POLY << (s0.bit_length() - 17)
    return s0


def _lane_position(addr):
    """(lane 0-3: IC20 IC19 IC18 IC17, the byte's distance from its lane's end) for a CROM byte below CRC_END"""
    o = addr & 3
    lane = (2 if addr & 4 else 0) + (1 if o >= 2 else 0)
    first = o in (1, 3)                  # each half's low byte (the higher address) first
    s = 2 * (addr >> 3) + (0 if first else 1)
    return lane, 2 * CRC_WORDS - 1 - s


def crc_fix(before, after, slot):
    """The bytes to add to after (bytes of the CROM, CPU order) so that each lane's CRC is before's"""
    diff = [a for a in range(0, min(len(before), CRC_END)) if before[a] != after[a]]
    acc = [0, 0, 0, 0]
    for a in diff:
        lane, d = _lane_position(a)
        acc[lane] ^= _gf_mulmod(before[a] ^ after[a], _gf_powx(8 * d + 16))
    fix = {}
    for lane in range(4):
        word = CRC_SLOTS + 8 * slot + (4 if lane >= 2 else 0)
        hi = lane % 2 == 0                   # the word's top half: bytes word+1 (first) and word+0
        first, second = (word + 1, word) if hi else (word + 3, word + 2)
        if after[first] or after[second] or before[first] or before[second]:
            raise ValueError("the CRC slot %d is not free" % slot)
        _, d = _lane_position(second)
        v = _gf_mulmod(acc[lane], _gf_inverse(_gf_powx(8 * d + 16)))
        fix[first], fix[second] = v >> 8, v & 0xFF
    return fix


def rom_test_crcs(crom):
    """The four program EPROMs' CRCs as the ROM test computes them (slow: for the checks)"""
    import array
    table = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        table.append(c & 0xFFFF)
    out = []
    for base, order in ((0, (1, 0)), (0, (3, 2)), (4, (1, 0)), (4, (3, 2))):
        crc = 0xFFFF
        for a in range(base, CRC_END, 8):
            for o in order:
                crc = ((crc << 8) ^ table[((crc >> 8) ^ crom[a + o]) & 0xFF]) & 0xFFFF
        out.append(crc ^ 0xFFFF)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("roms")
    parser.add_argument("out", nargs="?", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                                                               "Assets", "Patches", "srally2", "forest.xml"))
    parser.add_argument("--check", action="store_true", help="the ROM test's CRCs checked (slow)")
    args = parser.parse_args()

    chips = read_chips(args.roms)
    names = [name for name, _ in CHIPS]
    original = dict(zip(names, chips))

    crom = interleave(chips)
    slots = iter(range(64))

    def build(*parts, reference=None, on=None):
        """The CROM (or on, the base's) with these parts, the CRC bytes that cancel their changes (against reference)"""
        p = Program(on if on is not None else crom)
        for part in parts:
            part(p)
        if any(LABEL_BUFFER <= addr < LABEL_BUFFER_END for addr in p.written):
            raise ValueError("a patch's word in the label's buffer")
        data = bytearray(p.data)
        if reference is not None:
            for addr, byte in crc_fix(reference, data, next(slots)).items():
                data[addr] = byte
        return data, dict(zip(names, deinterleave(data)))

    base_parts = (patch_carousel, patch_route, patch_practice_times, patch_ending)
    base_crom, base = build(*base_parts, reference=crom)
    everything = Program(crom)                                       # no word patched twice
    for part in base_parts + tuple(option[3] for option in OPTIONS):
        part(everything)
    patch = rompatch.Patch("forest", "FOREST, the hidden fifth course", DESCRIPTION,
                           comment="Made by Scripts/RomPatches/srally2_forest.py from srally2's ROMs: the patch's bytes only")
    patch.base = rompatch.diff(original, base)
    for name, title, default, part, description in OPTIONS:
        _, with_option = build(part, reference=base_crom, on=base_crom)
        patch.options.append(rompatch.Option(name, title, description, default, rompatch.diff(base, with_option, original)))
    values = []
    music_slot = next(slots)
    for value, title, track in MUSIC_TRACKS:
        changes = {}
        if track is not None:
            data, _ = build(music_patch(track), on=base_crom)
            for addr, byte in crc_fix(base_crom, data, music_slot).items():   # the values share a slot (one applies)
                data[addr] = byte
            changes = rompatch.diff(base, dict(zip(names, deinterleave(data))), original)
        values.append(rompatch.Value(value, title, changes=changes))
    patch.options.append(rompatch.Option("music", "FOREST's music", """
The music of FOREST's races: MOUNTAIN's, as the game has it, or another of its tracks. Those no course uses may hold
music meant for FOREST (or nothing): listen.
""", "mountain", values=values))
    if args.check:
        # The ROM test's CRCs with the defaults and with every option on: the game's
        want = rom_test_crcs(crom)
        for label, chosen in (("defaults", [o for o in patch.options if o.default is True]), ("all", patch.options)):
            data = bytearray(crom)
            files = {n: bytearray(c) for n, c in zip(names, deinterleave(data))}
            for changes in [patch.base] + [o.changes for o in chosen if not o.values] + \
                           [o.values[1].changes for o in chosen if o.values]:
                for name, (crc, runs) in changes.items():
                    for offset, run in runs:
                        files[name][offset:offset + len(run)] = run
            got = rom_test_crcs(interleave([bytes(files[n]) for n in names]))
            print("ROM test CRCs (%s): %s, the game's %s: %s" % (label, " ".join("%04X" % v for v in got),
                                                                  " ".join("%04X" % v for v in want),
                                                                  "GOOD" if got == want else "BAD"))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    patch.write(args.out)
    print("%s: %d words patched in all, %d bytes of XML" % (os.path.normpath(args.out), len(everything.written),
                                                            os.path.getsize(args.out)))


if __name__ == "__main__":
    main()
