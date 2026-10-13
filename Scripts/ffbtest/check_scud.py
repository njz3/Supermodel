"""Check, command by command, the force feedback vJoy received in a Scud Race run against what
EPR-19338A makes of the command, translated the way Supermodel translates the servo board.

The board's side is read in the program (tables of EPR-19338A); Supermodel's translation is the
fixed WheelBoard.cpp: FE as a constant force 2*(TQ distance)/127, times the FB gain; FC 4 as a
spring of ((FC & 0x78) >> 3) * 16 + 15; FC 0-3 as a vibration; FF as a stop.

Usage: python check_scud.py runs/<run> <rom epr-19338a.bin> [--strength 5] [--cabinet B1]"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyse import load  # noqa: E402

WINDOW = 0.3  # s: up to two frames for the board to take the command, a frame of Supermodel, DirectInput


def main():
    run, rom = sys.argv[1], open(sys.argv[2], "rb").read()
    strength = int(sys.argv[sys.argv.index("--strength") + 1]) if "--strength" in sys.argv else 5
    cabinet = sys.argv[sys.argv.index("--cabinet") + 1] if "--cabinet" in sys.argv else "B1"
    drive, ffb, samples, events = load(run)
    ffb.sort(key=lambda r: r["t"])

    row = 0x19F9 if cabinet == "B1" else 0x1AF9
    row += 0x20 * (strength - 1)
    fc_of = [rom[row + 4 * n + 3] for n in range(8)]
    curves = {0: 0x3219, 1: 0x3209, 2: 0x31E9, 3: 0x31C9, 4: 0x31B9, 5: 0x31A9, 6: 0x3219, 7: 0x3219}
    curve = 0x31A9

    def spring_coef(fc):
        s = ((fc & 0x78) >> 3) * 0x10 + 0xF
        return int(s / 255.0 * 10000)

    def const_mag(tq, gain):
        if tq > 0x81:
            v = 2 * (0x81 - tq) if tq <= 0xC0 else -0x80
        elif tq < 0x7F:
            v = 2 * (0x7F - tq) if tq >= 0x40 else 0x7F
        else:
            v = 0
        v = int(v * gain / 0x7F)            # C++ integer division truncates towards zero
        f = v / (127.0 if v >= 0 else 128.0)
        return int(-f * 10000)

    def vib_mag(n, gain):
        s = [rom[0x19B9 + 4 * n + i] >> 4 for i in range(4)]
        p1, p2 = (s[1] << 4) | s[0], (s[3] << 4) | s[2]
        if p1 == 0 or p2 == 0:
            return 0
        v = max(0, min(255, ((p1 >> 1) - 7) * 0x50 + ((p2 >> 1) - 5) * 0x10 + 0xF))
        v = v * gain // 0x7F
        return int(v / 255.0 * 10000)

    end_of = {}
    for i, (t, c) in enumerate(drive):
        nxt = drive[i + 1][0] if i + 1 < len(drive) else t + WINDOW
        end_of[t] = min(t + WINDOW, nxt + 0.03)

    def packets(t, kind):
        return [r for r in ffb if t <= r["t"] < end_of[t] and r["type"] == kind]

    gain = 0x7F
    mode = None
    res = collections.defaultdict(lambda: [0, 0, []])   # family -> ok, bad, examples
    last_spring = None
    last_const = None
    last_vib = 0
    last_9x = -1.0
    for t, c in drive:
        fam = None
        if c >= 0xC0:
            mode = c
            if c == 0xC7:
                gain = rom[curve + 15] & 0x7F
                last_spring = spring_coef(fc_of[0])
            continue
        if 0x70 <= c <= 0x7F and mode == 0xC4:
            curve = curves[c & 7]
            continue
        if mode != 0xC7:
            continue
        hi, n = c >> 4, c & 7
        if hi == 1:
            fam = "1x spring"
            want = spring_coef(fc_of[n])
            got = [r["pos"] for r in packets(t, "condition")]
            ok = (want in got) or (want == last_spring and not got)
            last_spring = want
        elif hi in (5, 6):
            fam = "5x/6x torque"
            k = 21 + 2 * n
            tq = 0x80 - k if hi == 5 else 0x80 + k
            want = const_mag(tq, gain)
            got = [r["magnitude"] for r in packets(t, "constant")]
            ok = (want in got) or (want == last_const and not got)
            last_const = want
        elif hi == 9:
            fam = "9x motor idle"
            want = "stop-all"
            got = [r.get("ctrl") for r in packets(t, "control")]
            ok = "stop-all" in got or t - last_9x < WINDOW
            last_const = 0
            last_vib = 0
            last_9x = t
        elif hi == 2:
            gain = rom[curve + (c & 0xF)] & 0x7F
            fam = "2x gain"
            want, got, ok = f"gain {gain}", "-", True
        elif hi == 3:
            fam = "3x vibration"
            want = vib_mag(c & 0xF, gain)
            got = [r["magnitude"] for r in packets(t, "periodic")]
            ok = (want in got) or (want == last_vib and not got)
            last_vib = want
        elif hi == 0:
            fam = "0x road effect"
            want, got, ok = "constant updates", "-", True
        elif hi == 4:
            fam = "4x power slide"
            want, got, ok = "constant wave", "-", True
        if fam:
            r = res[fam]
            r[0 if ok else 1] += 1
            if not ok and len(r[2]) < 5:
                r[2].append(f"{c:02X}@{t:.2f}: want {want}, got {got}")
    print(f"== {run}  (cabinet {cabinet}, strength {strength}, MOTOR POWER curve {curve:04X})")
    for fam, (ok, bad, ex) in sorted(res.items()):
        print(f"  {fam:16s} as the board would: {ok:4d}   not: {bad:4d}")
        for e in ex:
            print("      " + e)


if __name__ == "__main__":
    main()
