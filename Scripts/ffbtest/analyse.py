"""Set the drive board commands of a smtest.py run against the force feedback vJoy received.

Usage: python analyse.py runs/<run> [--timeline]"""
import collections
import json
import os
import sys


def load(run):
    drive = []
    for line in open(os.path.join(run, "drive.tsv")):
        t, kv = line.rstrip("\n").split("\t", 1)
        if kv.startswith("RawDrive = "):
            drive.append((float(t), int(kv.split("=")[1])))
    ffb, samples, events = [], [], []
    for line in open(os.path.join(run, "session.jsonl")):
        r = json.loads(line)
        if r["kind"] == "ffb":
            ffb.append(r)
        elif r["kind"] == "sample":
            samples.append(r)
        else:
            events.append(r)
    return drive, ffb, samples, events


def family(c):
    hi = c >> 4
    if c >= 0xC0:
        return f"{c:02X}"
    return f"{hi:X}x"


def main():
    run = sys.argv[1]
    timeline = "--timeline" in sys.argv
    drive, ffb, samples, events = load(run)
    t0 = events[0]["t"] if events else (drive[0][0] if drive else 0)

    print(f"== {run}")
    fg = collections.Counter(s["fg"] for s in samples)
    print(f"window in the foreground: {fg[True]} of {sum(fg.values())} samples")
    games = [e for e in events if e["kind"] == "game"]
    print(f"games started: {len(games)}")

    print("\n-- commands received by the drive board (RawDrive), by family")
    fam = collections.Counter(family(c) for _, c in drive)
    print("  " + "  ".join(f"{k}:{v}" for k, v in sorted(fam.items())))
    vals = collections.Counter(c for _, c in drive)
    print("  most frequent: " + " ".join(f"{c:02X}x{n}" for c, n in vals.most_common(30)))

    print("\n-- FFB packets sent to vJoy")
    kinds = collections.Counter((r["type"], r.get("effect") or r.get("op") or r.get("ctrl") or "") for r in ffb)
    for (k, sub), n in kinds.most_common():
        print(f"  {k:10s} {sub:16s} {n}")
    dur = (max(r["t"] for r in ffb) - min(r["t"] for r in ffb)) if ffb else 0
    if ffb:
        per_s = collections.Counter(int(r["t"]) for r in ffb)
        print(f"  over {dur:.0f} s: {len(ffb)/max(dur,1):.1f} packets/s on average, {max(per_s.values())} in the busiest second")

    const = [r for r in ffb if r["type"] == "constant"]
    if const:
        mags = [r["magnitude"] for r in const]
        print(f"\n-- constant force: {len(mags)} updates, magnitude {min(mags)}..{max(mags)} (of +-10000)")
        hist = collections.Counter((m // 1000) * 1000 for m in mags)
        print("  histogram (by 1000): " + " ".join(f"{k}:{v}" for k, v in sorted(hist.items())))
    if const and samples:
        # The steering sent at the time of each constant force: a spring the board computes pushes against it
        # (DirectInput's magnitude is the opposite of Supermodel's force: positive pushes left)
        st = sorted((s["t"], s["steer"]) for s in samples)
        import bisect
        times = [x[0] for x in st]
        agree = against = 0
        for r in const:
            i = bisect.bisect_right(times, r["t"]) - 1
            if i < 0:
                continue
            off = st[i][1] - 0.5
            if abs(off) < 0.03 or r["magnitude"] == 0:
                continue
            if (off > 0) == (r["magnitude"] > 0):
                agree += 1
            else:
                against += 1
        print(f"  pushing back towards the centre: {agree}, away from it: {against} (wheel off-centre by 3 % or more)")
    cond = [r for r in ffb if r["type"] == "condition"]
    if cond:
        coefs = collections.Counter(r["pos"] for r in cond)
        print(f"\n-- spring/friction conditions: {len(cond)} updates; coefficients " +
              " ".join(f"{k}x{v}" for k, v in sorted(coefs.items())))
    per = [r for r in ffb if r["type"] == "periodic"]
    if per:
        m = collections.Counter(r["magnitude"] for r in per)
        print(f"\n-- periodic (vibration): {len(per)} updates; magnitudes " + " ".join(f"{k}x{v}" for k, v in sorted(m.items())))

    # Each command, the FFB packets that followed it within 100 ms
    print("\n-- what follows each command family (packets within 100 ms)")
    follow = collections.defaultdict(collections.Counter)
    j = 0
    ffb_sorted = sorted(ffb, key=lambda r: r["t"])
    for t, c in drive:
        while j < len(ffb_sorted) and ffb_sorted[j]["t"] < t:
            j += 1
        k = j
        while k < len(ffb_sorted) and ffb_sorted[k]["t"] < t + 0.1:
            r = ffb_sorted[k]
            follow[family(c)][r["type"] + ("/" + r.get("effect", "") if r["type"] == "effect" else "")] += 1
            k += 1
    for f in sorted(follow):
        print(f"  {f:4s}: " + ", ".join(f"{k} {v}" for k, v in follow[f].most_common(6)))

    if timeline:
        print("\n-- timeline (s: commands | constant magnitudes | spring coefs | periodic)")
        by_s = collections.defaultdict(lambda: [[], [], [], []])
        for t, c in drive:
            by_s[int(t - t0)][0].append(f"{c:02X}")
        for r in ffb:
            s = by_s[int(r["t"] - t0)]
            if r["type"] == "constant":
                s[1].append(str(r["magnitude"]))
            elif r["type"] == "condition":
                s[2].append(str(r["pos"]))
            elif r["type"] == "periodic":
                s[3].append(str(r["magnitude"]))
        for e in events:
            if e["kind"] in ("game", "shot"):
                by_s[int(e["t"] - t0)][0].append(f"[{e['kind']}:{e.get('label', e.get('n'))}]")
        for sec in sorted(by_s):
            a, b, c, d = by_s[sec]
            print(f"  {sec:4d}: {' '.join(a)[:90]:90s} | {' '.join(b)[:60]} | {' '.join(c)[:40]} | {' '.join(d)[:30]}")


if __name__ == "__main__":
    main()
