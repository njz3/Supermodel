"""Write a smtest.py run as a BFF capture: one line per drive board byte, its duration in frames, and
the force feedback vJoy received after it (until the next byte, 0.3 s at most).

Usage: python export_capture.py runs/<run> <out.txt> <header.txt>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyse import load  # noqa: E402

FPS = 57.524


def main():
    run, out, header = sys.argv[1], sys.argv[2], sys.argv[3]
    drive, ffb, samples, events = load(run)
    ffb.sort(key=lambda r: r["t"])
    t0 = events[0]["t"]
    games = {round(e["t"], 1): e["n"] for e in events if e["kind"] == "game"}
    # Which axis each effect block plays on (DirectInput folds the X axis into 90 degrees, the Y axis into 0 or 180):
    # named only when the run has effects on Y (the joystick)
    axis_of = {r["ebi"]: ("X" if r["dir_deg"] in (90, 270) else "Y") for r in ffb
               if r["type"] == "effect" and "dir_deg" in r}
    two_axes = "Y" in axis_of.values()

    def axis(r):
        return " " + axis_of.get(r.get("ebi"), "X") if two_axes else ""
    lines = open(header, encoding="utf-8").read().rstrip("\n").splitlines()
    lines.append("#")
    lines.append("# time_s  hex  frames | what vJoy received, until the next byte (0.3 s at most):")
    if two_axes:
        lines.append("#   const X, const Y = constant force on that axis, -10000..10000, positive lowers the axis's")
        lines.append("#   position (X to the left); spring = its coefficient; vib = the sine's magnitude; stop = stop all")
    else:
        lines.append("#   const = constant force, -10000..10000, positive pushes left; spring = its coefficient;")
        lines.append("#   vib = the sine's magnitude; stop = stop all effects")
    j = 0
    pending_games = sorted(games.items())
    for i, (t, c) in enumerate(drive):
        while pending_games and pending_games[0][0] <= t:
            lines.append(f"  {pending_games[0][0] - t0:8.3f}  -- game {pending_games[0][1]}: coins, start, menus")
            pending_games.pop(0)
        nxt = drive[i + 1][0] if i + 1 < len(drive) else t + 0.3
        frames = max(1, round((nxt - t) * FPS))
        end = min(nxt, t + 0.3)
        while j < len(ffb) and ffb[j]["t"] < t:
            j += 1
        k = j
        got = []
        while k < len(ffb) and ffb[k]["t"] < end:
            r = ffb[k]
            if r["type"] == "constant":
                got.append(f"const{axis(r)} {r['magnitude']}")
            elif r["type"] == "condition":
                got.append(f"spring{axis(r)} {r['pos']}")
            elif r["type"] == "periodic":
                got.append(f"vib{axis(r)} {r['magnitude']}")
            elif r["type"] == "control" and r.get("ctrl") == "stop-all":
                got.append("stop")
            k += 1
        dedup = []
        for g in got:
            if not dedup or dedup[-1] != g:
                dedup.append(g)
        lines.append(f"  {t - t0:8.3f}  {c:02X}  x{frames}" + (f"  | {', '.join(dedup)}" if dedup else ""))
    open(out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("wrote", out, len(lines), "lines")


if __name__ == "__main__":
    main()
