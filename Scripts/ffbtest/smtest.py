"""Drive a Supermodel game through vJoy and log, timestamped:
- every byte the game writes to the drive board (Supermodel's network outputs, RawDrive);
- every force feedback packet Supermodel sends to vJoy (DirectInput);
- the controls sent, whether Supermodel's window was in the foreground, and screenshots.

Usage: python smtest.py --exe Supermodel.exe --rom scud.zip --out runs/scud_new [--games 3]

Supermodel opens a force feedback joystick in DirectInput's exclusive foreground mode: its
window must stay in the foreground, or it neither reads vJoy nor sends force feedback."""
import argparse
import ctypes
import json
import math
import os
import random
import re
import socket
import subprocess
import sys
import threading
import time
from ctypes import wintypes

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vjoy import AXIS_X, AXIS_Y, AXIS_Z, VJoy  # noqa: E402

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.SetProcessDPIAware()

BTN_COIN, BTN_START, BTN_VIEW = 1, 2, 3
BTN_GEAR = {1: 6, 2: 7, 3: 8, 4: 9}
BTN_VR = [13, 14, 15, 16]


def find_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            if "Supermodel" in buf.value:
                found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def screenshot(hwnd, path):
    try:
        from PIL import ImageGrab
        ImageGrab.grab(window=hwnd).save(path)
    except Exception as exc:
        print("screenshot failed:", exc)


class DriveLog(threading.Thread):
    """Supermodel's network outputs: 'RawDrive = n' lines on TCP."""

    def __init__(self, port, out):
        super().__init__(daemon=True)
        self.port, self.out, self.stop = port, out, False

    def run(self):
        sock = None
        while not self.stop and sock is None:
            try:
                sock = socket.create_connection(("127.0.0.1", self.port), timeout=1)
            except OSError:
                time.sleep(0.5)
        if sock is None:
            return
        sock.settimeout(0.5)
        buf = b""
        with open(self.out, "w") as f:
            while not self.stop:
                try:
                    data = sock.recv(4096)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not data:
                    break
                t = time.time()
                buf += data
                *lines, buf = re.split(rb"[\r\n]+", buf)
                for line in lines:
                    if line:
                        f.write(f"{t:.4f}\t{line.decode(errors='replace')}\n")
                f.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--rundir", required=True, help="Supermodel's working directory (Config/, NVRAM/...)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--games", type=int, default=3)
    ap.add_argument("--boot", type=float, default=45, help="seconds before the first coin")
    ap.add_argument("--game-length", type=float, default=150, help="seconds of driving per game")
    ap.add_argument("--menu", type=float, default=35, help="seconds of menus after start")
    ap.add_argument("--extra-args", default="")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--steer-amp", type=float, default=0.10, help="amplitude of the slow steering, 0..0.5")
    ap.add_argument("--jolt-rate", type=float, default=0.002, help="chance of a hard turn at each 20 ms tick")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    random.seed(a.seed)

    vj = VJoy(1)
    vj.acquire()
    vj.log_ffb()
    controls = {"steer": 0.5, "accel": 0.5, "brake": 0.5}

    def set_controls(steer=None, accel=None, brake=None):
        if steer is not None:
            controls["steer"] = steer
        if accel is not None:
            controls["accel"] = accel
        if brake is not None:
            controls["brake"] = brake
        vj.axis(AXIS_X, controls["steer"])
        vj.axis(AXIS_Y, controls["accel"])      # mapped to JOY1_YAXIS_POS: 0.5 released, 1.0 floored
        vj.axis(AXIS_Z, controls["brake"])

    def press(btn, hold=0.15):
        vj.button(btn, True)
        time.sleep(hold)
        vj.button(btn, False)

    set_controls()
    for s in (0.6, 0.4, 0.5):                  # move the axis once
        set_controls(steer=s)
        time.sleep(0.1)

    log = open(os.path.join(a.out, "session.jsonl"), "w")
    t0 = time.time()

    def event(kind, **kw):
        kw.update(t=round(time.time(), 4), kind=kind)
        log.write(json.dumps(kw) + "\n")
        log.flush()

    args = [a.exe, "-window", "-input-system=dinput", "-force-feedback", "-outputs=net"] + a.extra_args.split() + [a.rom]
    smlog = open(os.path.join(a.out, "supermodel_stdout.txt"), "w")
    proc = subprocess.Popen(args, cwd=a.rundir, stdout=smlog, stderr=subprocess.STDOUT)
    event("start", args=args, pid=proc.pid)
    drive = DriveLog(8000, os.path.join(a.out, "drive.tsv"))
    drive.start()

    hwnd = None
    shot_n = [0]

    def tick(duration, step=0.02, driver=None):
        """Run for duration seconds: keep the window in front, sample, apply the driver."""
        nonlocal hwnd
        end = time.time() + duration
        next_sample = 0
        while time.time() < end and proc.poll() is None:
            if hwnd is None:
                hwnd = find_window(proc.pid)
                if hwnd:
                    user32.SetForegroundWindow(hwnd)
            if driver:
                driver(time.time())
            now = time.time()
            if now >= next_sample:
                fg = hwnd is not None and user32.GetForegroundWindow() == hwnd
                for (tp, rec) in vj.take_ffb():
                    rec["t"] = round(tp, 4)
                    rec["kind"] = "ffb"
                    log.write(json.dumps(rec) + "\n")
                event("sample", fg=fg, **controls)
                next_sample = now + 0.1
            time.sleep(step)

    def shot(label):
        if hwnd:
            shot_n[0] += 1
            screenshot(hwnd, os.path.join(a.out, f"shot_{shot_n[0]:03d}_{label}.png"))
            event("shot", label=label, n=shot_n[0])

    try:
        tick(a.boot)
        shot("boot")
        for game in range(a.games):
            event("game", n=game + 1)
            press(BTN_COIN)
            tick(1.0)
            press(BTN_COIN)
            tick(2.0)
            press(BTN_START)
            tick(2.0)
            shot(f"g{game+1}_after_start")
            # Menus: confirm with START and a step on the accelerator every few seconds, the timers do the rest
            menu_end = time.time() + a.menu
            k = 0
            while time.time() < menu_end and proc.poll() is None:
                press(BTN_START)
                tick(1.0)
                set_controls(accel=1.0)
                tick(0.4)
                set_controls(accel=0.5)
                tick(1.6)
                k += 1
                if k % 3 == 0:
                    shot(f"g{game+1}_menu{k}")
            # Drive: floored, steering slowly from side to side, a hard turn now and then
            phase = random.random() * 6.28
            t_start = time.time()
            jolts = []

            def driver(now):
                dt = now - t_start
                steer = 0.5 + a.steer_amp * math.sin(phase + dt * 2 * math.pi / 7.0)
                if not jolts or now > jolts[-1][1]:
                    if random.random() < a.jolt_rate:
                        jolts.append((now, now + 0.6, random.choice((-0.35, 0.35))))
                if jolts and jolts[-1][0] <= now <= jolts[-1][1]:
                    steer += jolts[-1][2]
                set_controls(steer=max(0.0, min(1.0, steer)), accel=1.0, brake=0.5)

            set_controls(accel=1.0)
            n_shots = 0
            seg = 10.0
            elapsed = 0.0
            while elapsed < a.game_length and proc.poll() is None:
                tick(seg, driver=driver)
                elapsed += seg
                n_shots += 1
                if n_shots % 3 == 1:
                    shot(f"g{game+1}_drive{int(elapsed)}")
            set_controls(steer=0.5, accel=0.5, brake=0.5)
            # Game over, continue screen, name entry: let them time out
            tick(25.0)
            shot(f"g{game+1}_end")
        event("done")
    finally:
        drive.stop = True
        for (tp, rec) in vj.take_ffb():
            rec["t"] = round(tp, 4)
            rec["kind"] = "ffb"
            log.write(json.dumps(rec) + "\n")
        log.close()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(10)
            except subprocess.TimeoutExpired:
                proc.kill()
        vj.release()
        smlog.close()
        print("done in", round(time.time() - t0), "s")


if __name__ == "__main__":
    main()
