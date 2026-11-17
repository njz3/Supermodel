"""
Small unlock patches of Model 3 games' program EPROMs (hidden menus, secrets without their conditions), written as
Supermodel ROM patches (Assets/Patches/<ROM set>/<patch>.xml, see Src/RomPatches.h).

    python unlocks.py ROMS_DIR [GAME ...]

ROMS_DIR holds the games' zips (scud.zip, eca.zip...); the program EPROMs' names and CRC32s come from
Config/Games.xml. Each patch is a list of (CROM offset, game's word, patched word): the CROM is the program as the
PowerPC sees it (big-endian, the four EPROMs interleaved), each address checked against the game's word. The
addresses come from eastereggs.md (the RAM copy of the program is the CROM at a fixed offset per game).
"""

import os
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rompatch                  # noqa: E402
from ppcasm import assemble      # noqa: E402

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


class Crom:
    """A game's fixed CROM from its program EPROMs (Games.xml's crom region: stride 8, 2-byte chunks, byte-swapped)."""

    def __init__(self, romset, roms_dir):
        root = ET.parse(os.path.join(REPO, "Config", "Games.xml")).getroot()
        game = next(g for g in root.iter("game") if g.get("name") == romset)
        region = next(r for r in game.iter("region") if r.get("name") == "crom")
        self.chips = []   # (lane 0-3, name, crc32)
        for f in region.iter("file"):
            self.chips.append((int(f.get("offset"), 0) // 2, f.get("name"), int(f.get("crc32"), 16)))
        by_crc = {}
        zpath = os.path.join(roms_dir, (game.get("parent") or romset) + ".zip")
        with zipfile.ZipFile(zpath) as z:
            for info in z.infolist():
                by_crc[info.CRC] = info
            self.files = {}
            for lane, name, crc in self.chips:
                if crc not in by_crc:
                    sys.exit("%s: %s (CRC32 %08X) not found" % (zpath, name, crc))
                self.files[name] = z.read(by_crc[crc])
        size = len(next(iter(self.files.values())))
        self.data = bytearray(4 * size)
        for lane, name, crc in self.chips:
            chip = self.files[name]
            self.data[2 * lane::8] = chip[1::2]
            self.data[2 * lane + 1::8] = chip[0::2]
        self.original = bytes(self.data)
        self.written = set()

    def word(self, addr):
        return struct.unpack_from(">I", self.original, addr)[0]

    def put(self, addr, original, value):
        if addr in self.written:
            raise ValueError("0x%06X patched twice" % addr)
        if self.word(addr) != original:
            raise ValueError("0x%06X: 0x%08X, not the game's 0x%08X: another version of the program?"
                             % (addr, self.word(addr), original))
        self.written.add(addr)
        struct.pack_into(">I", self.data, addr, value)

    def put_words(self, words, originals):
        """words {address: word} (from assemble), originals {address: the game's word}"""
        for addr in sorted(words):
            self.put(addr, originals[addr], words[addr])

    def put_bytes(self, addr, original, new):
        """A string or data: the game's bytes checked, the new ones of the same length"""
        if len(original) != len(new):
            raise ValueError("0x%06X: not the same length" % addr)
        if self.original[addr:addr + len(original)] != original:
            raise ValueError("0x%06X: not the game's bytes" % addr)
        self.data[addr:addr + len(new)] = new

    def chip_files(self, data=None):
        data = data if data is not None else self.data
        out = {}
        for lane, name, crc in self.chips:
            chip = bytearray(len(self.files[name]))
            chip[1::2] = data[2 * lane::8]
            chip[0::2] = data[2 * lane + 1::8]
            out[name] = bytes(chip)
        return out

    def changes(self):
        """The base's changes against the game's files"""
        return rompatch.diff(self.chip_files(self.original), self.chip_files())


def option_changes(crom, apply):
    """An option's changes: made on a copy of the game's CROM"""
    saved_data, saved_written = bytearray(crom.data), set(crom.written)
    crom.data, crom.written = bytearray(crom.original), set()
    apply(crom)
    changes = crom.changes()
    crom.data, crom.written = saved_data, saved_written
    return changes


def write(patch, romset):
    folder = os.path.join(REPO, "Assets", "Patches", romset)
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, patch.name + ".xml")
    patch.write(path)
    print("wrote", os.path.relpath(path, REPO))


# --- Emergency Call Ambulance: the region menu (COUNTRY ASSIGNMENTS) -------------------------------------------
# The test mode's start (RAM 0x0ABB80) asks 0x0AB9E8 whether to open COUNTRY ASSIGNMENTS; that function is a stub
# (li r3,0 ; blr). The patch replaces its call by a test of the VIEW CHANGE button, held (byte 0x1E00A2 bit 0,
# r13 = 0x1E8000). RAM = CROM - 0x480000 there.
ECA_RAM_TO_CROM = 0x480000


def eca_region(roms):
    crom = Crom("eca", roms)
    at = 0x0ABB94
    words = assemble("""
        lbz   r3, -0x7F5E(r13)      # 0x1E00A2: VIEW CHANGE held is bit 0
        andi. r3, r3, 1
        beq   0x0ABBB4              # not held: the test menu as usual
    """, origin=at)
    crom.put_words({a + ECA_RAM_TO_CROM: w for a, w in words.items()},
                   {at + ECA_RAM_TO_CROM: 0x4BFFFE55,        # bl 0xAB9E8
                    at + 4 + ECA_RAM_TO_CROM: 0x2C830000,    # cmpwi cr1, r3, 0
                    at + 8 + ECA_RAM_TO_CROM: 0x41860018})   # beq cr1, 0xABBB4
    patch = rompatch.Patch(
        "region_menu", "Region menu (COUNTRY ASSIGNMENTS)",
        """The game has a region menu (Universe, Japan, U.S.A., Export, Australia, Korea) that its program never
opens: the function that decides it always says no. With this patch, hold VIEW CHANGE while pressing Test: the
test mode opens on COUNTRY ASSIGNMENTS (Service changes the country, Test leaves). Test alone opens the usual test
menu.

The region changes the game's texts and settings like the other versions (ecaj, ecau): a new region may reset the
coin settings (free play), check them in COIN ASSIGNMENTS.

A patch of the program EPROMs (EPR-22903 to EPR-22906), made by Scripts/RomPatches/unlocks.py.""",
        comment="Made by Scripts/RomPatches/unlocks.py from eca's ROMs: the patch's bytes only")
    patch.base = crom.changes()
    write(patch, "eca")


# --- Scud Race: the hidden video test menu ---------------------------------------------------------------------
# The test mode's state byte (RAM 0x104006): 0x3B opens a second TEST MENU of 11 development tests (OBJECT TEST,
# a 3D model viewer, RENDERER, SCROLL, COLOR, FOG/LIGHT, VIEWPORT, LOS, TEXMAP OP, SCALING, POL UPDATE, RESOLUTION),
# which nothing sets. The usual menu's actions are the table at 0x89F58 (one state a line): BOUNDARY SCAN TEST's
# (state 7, a JTAG test only) becomes 0x3B, and its label "VIDEO TEST MENU". CROM = RAM + 0x110000.
SCUD_RAM_TO_CROM = 0x110000


def scud_video_tests(roms):
    crom = Crom("scud", roms)
    crom.put(0x89F5C + SCUD_RAM_TO_CROM, 0x00000007, 0x0000003B)
    old = b"BOUNDARY SCAN TEST"
    crom.put_bytes(0x8DDA0 + SCUD_RAM_TO_CROM, old, b"VIDEO TEST MENU".ljust(len(old), b"\0"))
    patch = rompatch.Patch(
        "video_tests", "Hidden video test menu",
        """The test mode has a second TEST MENU, of the developers' video tests, that no menu leads to: OBJECT TEST
(a viewer of the game's 3D models, among them the tank, the rocket car and the bus of Scud Race Plus), RENDERER,
SCROLL, COLOR, FOG/LIGHT, VIEWPORT, LOS, TEXMAP OP, SCALING, POL UPDATE and RESOLUTION TEST. This patch puts it in
the place of BOUNDARY SCAN TEST (a test of the board's JTAG chain), as VIDEO TEST MENU.

In OBJECT TEST: Shift up/down change the model (with VR4: fast), accelerator/brake zoom, VR1/VR2 change the
rotation, Start resets, VR3 hides the text.

A patch of the program EPROMs (EPR-19688 to EPR-19691), made by Scripts/RomPatches/unlocks.py.""",
        comment="Made by Scripts/RomPatches/unlocks.py from scud's ROMs: the patch's bytes only")
    patch.base = crom.changes()
    write(patch, "scud")


# --- Sega Rally 2: the secrets without their conditions ---------------------------------------------------------
# Three functions say whether a secret is open (RAM = CROM): each, linked as a slave, reads the master's answer; else
# it asks the settings' flag 0x40 (set after 100 games), the flag 0x224418, and the hours since the date the
# machine first ran: more than 719 (30 days) for the Sega Rally 1 cars (0x4BCF0) and the cone mini-game (0x4BDA8),
# more than 2879 (120 days) for a hint scrolled across the attract (0x4BC38). Each option makes one answer yes on
# its own path (li r3,1 then to its end); a master gives its answers to the slaves as before.
SR2_SECRETS = {   # name: (the standalone path's first two words and their game's words, the function's end)
    "sr1_cars": (0x4BD4C, (0x38610008, 0x480295D1), 0x4BD98),
    "mini_game": (0x4BE04, (0x38610008, 0x48029519), 0x4BE50),
    "hint": (0x4BC94, (0x38610008, 0x48029689), 0x4BCE0),
}


def sr2_secret(name):
    at, originals, end = SR2_SECRETS[name]

    def apply(crom):
        words = assemble("li r3, 1\nb 0x%X" % end, origin=at)
        crom.put_words(words, {at: originals[0], at + 4: originals[1]})
    return apply


def srally2_secrets(roms):
    crom = Crom("srally2", roms)
    patch = rompatch.Patch(
        "secrets", "Secrets without their conditions",
        """Sega Rally 2's secrets open only on a machine that has run more than 100 games and 30 days (120 for
the attract's hint): never with a new NVRAM, so never on most emulators. This patch opens them at once; each
option one secret. Their codes stay the game's (see the Game page's notes). Linked, the master's answers go to the
other cabinets, as on the real board.

A patch of the program EPROMs (EPR-20632 to EPR-20635), made by Scripts/RomPatches/unlocks.py.""",
        comment="Made by Scripts/RomPatches/unlocks.py from srally2's ROMs: the patch's bytes only")
    patch.options.append(rompatch.Option(
        "sr1_cars", "Sega Rally 1 cars", """The Toyota Celica GT-Four and the Lancia Delta HF Integrale: at the car
select, press VIEW CHANGE and pull the handbrake together (again to go back to the other cars).""",
        default=True, changes=option_changes(crom, sr2_secret("sr1_cars"))))
    patch.options.append(rompatch.Option(
        "mini_game", "Cone mini-game", """Riviera: on lap 1, knock down both red cones after the second hairpin; an
opening appears on the left, drive through it on lap 2 for 30 seconds of cones, the race clock stopped.""",
        default=True, changes=option_changes(crom, sr2_secret("mini_game"))))
    patch.options.append(rompatch.Option(
        "hint", "Hint in the attract", """The attract's snowy part scrolls a line across the top: "When you select a
car, either the Celica or Delta can be chosen using VIEW CHANGE and side brake." (on the real board after 120
days).""",
        default=False, changes=option_changes(crom, sr2_secret("hint"))))
    write(patch, "srally2")


# --- Daytona USA 2: the Hornet by its VIEW sequence alone -------------------------------------------------------
# The course select's frame (RAM = CROM - 0x600000) runs a state in its object (byte 0x6F(r15)): 1 if the shifter was
# in 1st when the coin went in, 2 once in 4th, 3-7 the presses VR1 VR4 VR2 VR4 VR1 (table 0x0B07E8), 8 (the Hornet)
# back in 1st or with VR2. The block 0x03472C-0x0347C7 becomes the presses alone: no coin-time or shifter condition.
D2_RAM_TO_CROM = 0x600000
D2_HORNET = """
        lbz    r3, 0x6F(r15)        # the code's state: 2-6 the five presses, then 8 the Hornet
        cmpwi  r3, 7
        bge    out
        cmpwi  r3, 2
        bge    pressed
        li     r3, 2                # 0 or 1 (the shifter's condition): the sequence's start
pressed:
        lis    r6, 0x10
        lwz    r6, 0x1678(r6)       # buttons pressed this frame
        rlwinm r5, r6, 24, 28, 31   # VR1-VR4: 1, 2, 4, 8
        cmpwi  r5, 0
        beq    out
        lis    r7, 0xB
        addi   r7, r7, 0x7E6        # the game's sequence 0xB07E8: 1 8 2 8 1, from state 2
        lbzx   r4, r3, r7
        cmpw   r5, r4
        li     r4, 0                # a wrong button: the sequence again
        bne    store
        addi   r4, r3, 1
        cmpwi  r4, 7
        bne    store
        li     r4, 8
store:
        stb    r4, 0x6F(r15)
out:
        b      0x347C8
"""


def daytona2_hornet(roms):
    crom = Crom("daytona2", roms)
    first, end = 0x3472C, 0x347C8
    words = assemble(D2_HORNET, origin=first)
    for a in range(first, end, 4):
        words.setdefault(a, 0x60000000)
    if crom.word(first + D2_RAM_TO_CROM) != 0x886F006F or crom.word(end - 4 + D2_RAM_TO_CROM) != 0x986F006F:
        raise ValueError("daytona2: not the course select's code expected (another version of the program?)")
    for a, w in sorted(words.items()):
        crom.put(a + D2_RAM_TO_CROM, crom.word(a + D2_RAM_TO_CROM), w)
    patch = rompatch.Patch(
        "hornet", "Hornet by the VIEW buttons alone",
        """The Hornet, the special car with a tuning screen, needs a long code with the shifter (in 1st before the
coin, 4th at the course select, back to 1st). With this patch, at the course select press VR1, VR4, VR2, VR4, VR1
(the game's own buttons, a wrong one starts again), then choose the course: the tuning screen (engine, steering,
suspension) replaces the car select. The shifter plays no part.

A patch of the program EPROMs (EPR-20861A to EPR-20864A), made by Scripts/RomPatches/unlocks.py.""",
        comment="Made by Scripts/RomPatches/unlocks.py from daytona2's ROMs: the patch's bytes only")
    patch.base = crom.changes()
    write(patch, "daytona2")


GAMES = {"eca": [eca_region], "scud": [scud_video_tests], "srally2": [srally2_secrets], "daytona2": [daytona2_hornet]}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    roms = sys.argv[1]
    for game in sys.argv[2:] or list(GAMES):
        for make in GAMES[game]:
            make(roms)


if __name__ == "__main__":
    main()
