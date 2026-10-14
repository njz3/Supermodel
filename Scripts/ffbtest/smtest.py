"""Drive a Supermodel game through vJoy and log, timestamped:
- every byte the game writes to the drive board (Supermodel's network outputs, RawDrive);
- every force feedback packet Supermodel sends to vJoy (DirectInput);
- the controls sent, whether Supermodel's window was in the foreground, and screenshots.

Usage: python smtest.py --exe Supermodel.exe --rom scud.zip --out runs/scud_new [--games 3]
       python smtest.py ... --make-state      once: play to the start of a race, wait for F5 (Save State)
       python smtest.py ... --state <rundir>/Saves/scud.st0 --games 3    every game from that state

Supermodel is run with -background-input: it reads vJoy and sends force feedback whichever window
has the focus. A build without that option (--foreground-only) opens a force feedback joystick in
DirectInput's exclusive foreground mode: its window must then stay in the foreground."""
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
BTN_TRIGGER, BTN_EVENT = 17, 18     # the analog joystick's trigger and event buttons (Star Wars Trilogy)


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
        with open(self.out, "a") as f:
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
    ap.add_argument("--mode", choices=("wheel", "joystick"), default="wheel",
                    help="wheel: steering and pedals; joystick: the analog joystick on X/Y with its trigger")
    ap.add_argument("--stick-amp", type=float, default=0.35, help="joystick mode: amplitude of the stick's sweep")
    ap.add_argument("--foreground-only", action="store_true",
                    help="do not pass -background-input (builds without it): the window must keep the focus")
    ap.add_argument("--make-state", action="store_true",
                    help="boot, coin, start and menus once, then wait at the start of the race for someone to press "
                         "F5 (Save State) in Supermodel's window: its UI keys cannot be mapped to a joystick")
    ap.add_argument("--save-wait", type=float, default=180, help="--make-state: seconds to wait for the save")
    ap.add_argument("--state", help="start every game from this save state, Supermodel started again for each one "
                                    "with -load-state: no boot, no attract, no menus")
    ap.add_argument("--state-boot", type=float, default=8, help="--state: seconds for Supermodel to load the state")
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

    args = [a.exe, "-window", "-input-system=dinput", "-force-feedback", "-outputs=net"]
    if not a.foreground_only:
        args.append("-background-input")
    if a.state:
        args.append("-load-state=" + os.path.abspath(a.state))
    args += a.extra_args.split() + [a.rom]
    smlog = open(os.path.join(a.out, "supermodel_stdout.txt"), "w")
    drive_path = os.path.join(a.out, "drive.tsv")
    open(drive_path, "w").close()
    sm = {"proc": None, "drive": None, "hwnd": None}

    def launch():
        """Start Supermodel, and the reading of its outputs (appended to drive.tsv)."""
        sm["proc"] = subprocess.Popen(args, cwd=a.rundir, stdout=smlog, stderr=subprocess.STDOUT)
        sm["hwnd"] = None
        event("start", args=args, pid=sm["proc"].pid)
        sm["drive"] = DriveLog(8000, drive_path)
        sm["drive"].start()

    def stop():
        sm["drive"].stop = True
        proc = sm["proc"]
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(10)
            except subprocess.TimeoutExpired:
                proc.kill()
        sm["drive"].join(2)

    def running():
        return sm["proc"].poll() is None

    def tick(duration, step=0.02, driver=None):
        """Run for duration seconds: sample, apply the driver. The window is brought to the front once, when found."""
        end = time.time() + duration
        next_sample = 0
        while time.time() < end and running():
            if sm["hwnd"] is None:
                sm["hwnd"] = find_window(sm["proc"].pid)
                if sm["hwnd"]:
                    user32.SetForegroundWindow(sm["hwnd"])
            if driver:
                driver(time.time())
            now = time.time()
            if now >= next_sample:
                fg = sm["hwnd"] is not None and user32.GetForegroundWindow() == sm["hwnd"]
                for (tp, rec) in vj.take_ffb():
                    rec["t"] = round(tp, 4)
                    rec["kind"] = "ffb"
                    log.write(json.dumps(rec) + "\n")
                event("sample", fg=fg, **controls)
                next_sample = now + 0.1
            time.sleep(step)

    shot_n = [0]

    def shot(label):
        if sm["hwnd"]:
            shot_n[0] += 1
            screenshot(sm["hwnd"], os.path.join(a.out, f"shot_{shot_n[0]:03d}_{label}.png"))
            event("shot", label=label, n=shot_n[0])

    def start_game(game):
        """From the attract mode to the start of the race: coins, START, the menus."""
        press(BTN_COIN)
        tick(1.0)
        press(BTN_COIN)
        tick(2.0)
        press(BTN_START)
        tick(2.0)
        shot(f"g{game+1}_after_start")
        # Menus: confirm with START and a step on the accelerator (wheel) or the trigger (joystick) every few
        # seconds, the timers do the rest
        menu_end = time.time() + a.menu
        k = 0
        while time.time() < menu_end and running():
            press(BTN_START)
            tick(1.0)
            if a.mode == "joystick":
                press(BTN_TRIGGER)
                tick(2.0)
            else:
                set_controls(accel=1.0)
                tick(0.4)
                set_controls(accel=0.5)
                tick(1.6)
            k += 1
            if k % 3 == 0:
                shot(f"g{game+1}_menu{k}")

    def new_states():
        saves = os.path.join(a.rundir, "Saves")
        if not os.path.isdir(saves):
            return []
        return sorted(os.path.join(saves, f) for f in os.listdir(saves)
                      if ".st" in f and os.path.getmtime(os.path.join(saves, f)) > t0)

    try:
        launch()
        if a.state:
            tick(a.state_boot)
            shot("state_loaded")
        else:
            tick(a.boot)
            shot("boot")
        if a.make_state:
            event("game", n=1)
            start_game(0)
            set_controls(steer=0.5, accel=0.5, brake=0.5)
            shot("race_start")
            print("At the start of the race: click Supermodel's window and press F5 (Save State)", flush=True)
            event("waiting_for_save")
            end_wait = time.time() + a.save_wait
            while not new_states() and time.time() < end_wait and running():
                tick(1.0)
            made = new_states()
            event("state", path=made[-1] if made else None)
            print("state saved:", made[-1] if made else None)
            a.games = 0
        for game in range(a.games):
            if a.state and game > 0:
                stop()
                launch()
                tick(a.state_boot)
            event("game", n=game + 1)
            if not a.state:
                start_game(game)
            phase = random.random() * 6.28
            t_start = time.time()
            jolts = []

            def driver(now):
                # Wheel: floored, steering slowly from side to side, a hard turn now and then
                dt = now - t_start
                steer = 0.5 + a.steer_amp * math.sin(phase + dt * 2 * math.pi / 7.0)
                if not jolts or now > jolts[-1][1]:
                    if random.random() < a.jolt_rate:
                        jolts.append((now, now + 0.6, random.choice((-0.35, 0.35))))
                if jolts and jolts[-1][0] <= now <= jolts[-1][1]:
                    steer += jolts[-1][2]
                set_controls(steer=max(0.0, min(1.0, steer)), accel=1.0, brake=0.5)

            def pilot(now):
                # Joystick: the stick sweeping both axes (X over 5 s, Y over 7 s), the trigger in bursts, the event
                # button now and then
                dt = now - t_start
                x = 0.5 + a.stick_amp * math.sin(phase + dt * 2 * math.pi / 5.0)
                y = 0.5 + a.stick_amp * 0.8 * math.sin(dt * 2 * math.pi / 7.0)
                set_controls(steer=x, accel=y)
                vj.button(BTN_TRIGGER, (dt % 0.3) < 0.1)
                vj.button(BTN_EVENT, (dt % 8.0) < 0.15)

            if a.mode == "joystick":
                driver = pilot
            else:
                set_controls(accel=1.0)
            n_shots = 0
            seg = 10.0
            elapsed = 0.0
            while elapsed < a.game_length and running():
                tick(seg, driver=driver)
                elapsed += seg
                n_shots += 1
                if n_shots % 3 == 1:
                    shot(f"g{game+1}_drive{int(elapsed)}")
            set_controls(steer=0.5, accel=0.5, brake=0.5)
            vj.button(BTN_TRIGGER, False)
            vj.button(BTN_EVENT, False)
            if not a.state:
                # Game over, continue screen, name entry: let them time out
                tick(25.0)
            else:
                tick(1.0)
            shot(f"g{game+1}_end")
        event("done")
    finally:
        if sm["proc"]:
            stop()
        for (tp, rec) in vj.take_ffb():
            rec["t"] = round(tp, 4)
            rec["kind"] = "ffb"
            log.write(json.dumps(rec) + "\n")
        log.close()
        vj.release()
        smlog.close()
        print("done in", round(time.time() - t0), "s")


if __name__ == "__main__":
    main()
