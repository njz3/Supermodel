"""Drive a game in Supermodel's agent mode, frame by frame, from a save state, and check its force feedback.

Usage: python agent_drive.py --exe Supermodel.exe --rom scud.zip --rundir <rundir> --state <rundir>/Saves/scud.st0
                             --out runs/agent_scud [--seconds 60] [--no-throttle] [--inputs agent|vjoy] [--vjoy]
                             [--servo]

The driver floors the accelerator and steers slowly from side to side, a hard turn now and then, one frame at a time.
--inputs agent: the agent holds the steering and the pedals in Supermodel (set). --inputs vjoy: they are set on vJoy's
axes before each frame and Supermodel reads them through DirectInput, the whole input path; the bench then checks,
frame by frame, that the game read what was set (sm.get), and how many frames late.

Recorded in --out:
- drive.tsv: frame and byte of every write of the main board to the drive board;
- ffb.tsv: every force feedback command Supermodel sent (frame, input, effect, force), device or not;
- vjoy.jsonl (--vjoy, or --inputs vjoy): the packets vJoy received, with the frame after which they were read;
- servo.tsv (--servo): what the drive board's Z80 wrote at each NMI (frame, nmi, port, value): 28 a new command, 29
  its reply, 2A/2D/2E the servo or motor boards;
- inputs.tsv (--inputs vjoy): frame, steering set, steering read by the game;
- frame_*.png: a picture every --shot-every seconds.
With vJoy, the run directory's Supermodel.ini must map the steering to JOY1_XAXIS, the accelerator to JOY1_YAXIS_POS
and the brake to JOY1_ZAXIS_POS (Scripts/ffbtest/Supermodel.ini), and BackForceFeeder must not run."""
import argparse
import collections
import json
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smagent import SMAgent  # noqa: E402

FPS = 57.524
EFFECT_OF_PACKET = {"constant": "constant", "condition": "selfcenter"}


def match_ffb(ffb, vjoy):
    """Pair each command Supermodel sent with the packet vJoy received for it, in order. The agent's constant force
    is trigonometric (> 0 to the left), as DirectInput's magnitude: magnitude = force * 10000 (DirectInput...Max =
    100); a spring's coefficient is its strength times 10000. vJoy's first packet of each effect block is its creation, at 0: skipped."""
    packets = {"constant": [], "selfcenter": []}
    created = set()
    for r in vjoy:
        if r["type"] in EFFECT_OF_PACKET:
            if r["ebi"] not in created:
                created.add(r["ebi"])
                continue
            value = r["magnitude"] if r["type"] == "constant" else r["pos"]
            packets[EFFECT_OF_PACKET[r["type"]]].append((r["frame"], value))
    report = {}
    for effect, sign in (("constant", 1), ("selfcenter", 1)):
        sent = [(f, force) for f, name, e, force in ffb if e == effect]
        got = packets[effect]
        n = min(len(sent), len(got))
        good = sum(1 for i in range(n) if abs(round(sign * sent[i][1] * 10000) - got[i][1]) <= 2)
        lags = collections.Counter(got[i][0] - sent[i][0] for i in range(n))
        report[effect] = (len(sent), len(got), good, dict(sorted(lags.items())))
    return report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--rundir", required=True)
    ap.add_argument("--state", required=True, help="the save state to start from (-load-state)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", type=float, default=60, help="seconds of game time")
    ap.add_argument("--no-throttle", action="store_true", help="as fast as the host goes, not in real time")
    ap.add_argument("--inputs", choices=("agent", "vjoy"), default="agent",
                    help="agent: held in Supermodel; vjoy: set on vJoy's axes, read through DirectInput")
    ap.add_argument("--vjoy", action="store_true", help="record the force feedback vJoy receives (always with "
                                                        "--inputs vjoy)")
    ap.add_argument("--servo", action="store_true", help="journal what the drive board's Z80 writes at each NMI")
    ap.add_argument("--shot-every", type=float, default=5, help="seconds of game time between pictures (0: none)")
    ap.add_argument("--steer-amp", type=float, default=0.25)
    ap.add_argument("--deadzone", type=float, default=3, help="--inputs vjoy: the X axis' dead zone in Supermodel, "
                                                              "%% (InputJoy1XDeadZone, 3 by default)")
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    random.seed(a.seed)
    use_vjoy = a.vjoy or a.inputs == "vjoy"

    vj = None
    if use_vjoy:
        from vjoy import AXIS_X, AXIS_Y, AXIS_Z, VJoy
        vj = VJoy(1)
        vj.acquire()
        vj.log_ffb()
        for axis in (AXIS_X, AXIS_Y, AXIS_Z):
            vj.axis(axis, 0.5)

    frames = int(a.seconds * FPS)
    shot_every = int(a.shot_every * FPS) if a.shot_every > 0 else 0
    logs = {name: open(os.path.join(a.out, name), "w") for name in ("drive.tsv", "ffb.tsv")}
    if vj:
        logs["vjoy.jsonl"] = open(os.path.join(a.out, "vjoy.jsonl"), "w")
    if a.servo:
        logs["servo.tsv"] = open(os.path.join(a.out, "servo.tsv"), "w")
    if a.inputs == "vjoy":
        logs["inputs.tsv"] = open(os.path.join(a.out, "inputs.tsv"), "w")
    drive_count = collections.Counter()
    all_ffb, all_vjoy, all_servo = [], [], []
    input_checks = collections.Counter()

    def collect_vjoy(frame):
        # After each frame: the packets of that frame's DirectInput calls are in by then
        for tp, rec in vj.take_ffb():
            rec["t"] = round(tp, 4)
            rec["frame"] = frame
            logs["vjoy.jsonl"].write(json.dumps(rec) + "\n")
            all_vjoy.append(rec)

    def collect(sm, frame):
        for f, name, effect, force in sm.ffb():
            logs["ffb.tsv"].write(f"{f}\t{name}\t{effect}\t{force:.4f}\n")
            all_ffb.append((f, name, effect, force))
        if vj:
            collect_vjoy(frame)
        if a.servo:
            for f, nmi, port, value in sm.servo():
                logs["servo.tsv"].write(f"{f}\t{nmi}\t{port:02X}\t{value:02X}\n")
                all_servo.append((f, nmi, port, value))

    t0 = time.time()
    with SMAgent(a.exe, a.rom, a.rundir, ["-load-state=" + os.path.abspath(a.state)], throttle=not a.no_throttle,
                 log=os.path.join(a.out, "supermodel.log")) as sm:
        print(f"{sm.game}: {sm.width}x{sm.height}, inputs {a.inputs}")
        if a.servo:
            sm.servo_on()
        jolt_until, jolt = 0, 0.0
        phase = random.random() * 6.28
        expected_prev = None
        frame = 0
        for i in range(frames):
            steer = a.steer_amp * math.sin(phase + i * 2 * math.pi / (7 * FPS))
            if i >= jolt_until and random.random() < 0.002:
                jolt_until, jolt = i + int(0.6 * FPS), random.choice((-0.7, 0.7))
            if i < jolt_until:
                steer += jolt
            steer = max(-1.0, min(1.0, steer))
            if a.inputs == "agent":
                sm.set("Steering", steer)
                sm.set("Accelerator", 1.0)
                sm.set("Brake", 0.0)
            else:
                vj.axis(AXIS_X, 0.5 - 0.5 * steer)    # trigonometric: +1 fully left, vJoy's X minimum
                vj.axis(AXIS_Y, 1.0)              # JOY1_YAXIS_POS: 0.5 released, 1.0 floored
                vj.axis(AXIS_Z, 0.5)
            frame, drive = sm.step(1)
            if vj:
                collect_vjoy(frame)
            for b in drive:
                logs["drive.tsv"].write(f"{frame}\t{b:02X}\n")
                drive_count[f"{b >> 4:X}x" if b < 0xC0 else f"{b:02X}"] += 1
            if a.inputs == "vjoy":
                # What the game read this frame, against what was set before it (same frame) or one frame earlier
                got = sm.get("Steering")
                # Supermodel's dead zone, then the rest of the travel scaled to the full range
                dz = a.deadzone / 100.0
                scaled = max(0.0, (abs(steer) - dz) / (1.0 - dz))
                expected = 0x80 - scaled * 0x80 if steer >= 0 else 0x80 + scaled * 0x7F
                logs["inputs.tsv"].write(f"{frame}\t{steer:.4f}\t{got}\n")
                if abs(got - expected) <= 2:
                    input_checks["same frame"] += 1
                elif expected_prev is not None and abs(got - expected_prev) <= 2:
                    input_checks["one frame late"] += 1
                else:
                    input_checks["other"] += 1
                expected_prev = expected
            if i % 30 == 29:
                collect(sm, frame)
            if shot_every and i % shot_every == 0:
                sm.obs().save(os.path.join(a.out, f"frame_{frame:06d}.png"))
        time.sleep(0.2)
        collect(sm, frame)
        sm.obs().save(os.path.join(a.out, "frame_last.png"))
    elapsed = time.time() - t0
    if vj:
        vj.release()
    for f in logs.values():
        f.close()

    print(f"{frames} frames in {elapsed:.1f} s ({frames / elapsed:.1f} frames/s, {frames / FPS / elapsed:.2f}x real time)")
    print("drive board bytes by family:", " ".join(f"{k}:{v}" for k, v in sorted(drive_count.items())))
    print("force feedback sent by Supermodel:",
          " ".join(f"{k}:{v}" for k, v in sorted(collections.Counter(f"{n}/{e}" for _, n, e, _ in all_ffb).items())))
    if a.inputs == "vjoy":
        print("steering read by the game:", " ".join(f"{k} {v}" for k, v in input_checks.most_common()))
    if vj:
        print("packets received by vJoy:",
              " ".join(f"{k}:{v}" for k, v in sorted(collections.Counter(r["type"] for r in all_vjoy).items())))
        for effect, (sent, got, good, lags) in match_ffb(all_ffb, all_vjoy).items():
            print(f"  {effect}: Supermodel sent {sent}, vJoy got {got}, {good} of them with the right value; "
                  f"frames between the two: {lags}")
    if a.servo and all_servo:
        nmis = all_servo[-1][1] - all_servo[0][1]
        frames_seen = all_servo[-1][0] - all_servo[0][0]
        ports = collections.Counter(f"{p:02X}" for _, _, p, _ in all_servo)
        print(f"servo journal: {len(all_servo)} writes, {nmis} NMIs over {frames_seen} frames "
              f"({nmis / max(frames_seen, 1):.2f} NMIs a frame); by port:",
              " ".join(f"{k}:{v}" for k, v in sorted(ports.items())))


if __name__ == "__main__":
    main()
