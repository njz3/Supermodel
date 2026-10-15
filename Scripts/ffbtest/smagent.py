"""Supermodel's agent mode (-agent) from Python: Supermodel started with its window hidden, its frames run on our
commands, with the inputs we hold; back come the drive board bytes, the force feedback Supermodel sends, the
picture, the RAM. The protocol is described in Docs/README.txt and Src/OSD/SDL/Agent.cpp.

    from smagent import SMAgent
    with SMAgent(exe, rom, rundir, ["-load-state=" + state]) as sm:
        sm.set("Accelerator", 1.0)
        frame, drive = sm.step(60)        # one second; the drive board bytes written meanwhile
        image = sm.obs(248, 192)          # PIL image (RGB)
        ffb = sm.ffb()                    # [(frame, input, effect, force), ...]

The force feedback still goes to the device the steering is mapped to (vJoy on the bench, read with vjoy.py), and
it is also reported here, as Supermodel sent it, device or not."""
import subprocess


class SMAgent:
    def __init__(self, exe, rom, rundir, args=(), throttle=True, log=None):
        """throttle: real time (57.5 frames a second) or as fast as the host goes; log: a file for Supermodel's
        messages (its stderr)."""
        cmd = [exe, "-agent"] + ([] if throttle else ["-no-throttle"]) + list(args) + [rom]
        self.log = open(log, "w") if log else subprocess.DEVNULL
        self.proc = subprocess.Popen(cmd, cwd=rundir, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log)
        while True:
            line = self._line()
            if line.startswith("ready "):
                _, self.game, width, height = line.split()
                self.width, self.height = int(width), int(height)
                break

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _line(self):
        data = self.proc.stdout.readline()
        if not data:
            raise RuntimeError("Supermodel exited (see its log)")
        return data.decode("ascii", "replace").rstrip("\r\n")

    def _send(self, text):
        self.proc.stdin.write((text + "\n").encode("ascii"))
        self.proc.stdin.flush()

    def _ask(self, text):
        self._send(text)
        return self._line()

    def _ok(self, text):
        answer = self._ask(text)
        if not answer.startswith("ok"):
            raise RuntimeError(f"{text!r}: {answer}")
        return answer

    def _read(self, n):
        data = b""
        while len(data) < n:
            chunk = self.proc.stdout.read(n - len(data))
            if not chunk:
                raise RuntimeError("Supermodel exited (see its log)")
            data += chunk
        return data

    def step(self, n=1):
        """Run n frames; (frame number, [drive board bytes written meanwhile])."""
        words = self._ok(f"step {n}").split()
        return int(words[1]), [int(b, 16) for b in words[2:]]

    def step_send(self, n=1):
        """Start n frames without waiting: several Supermodels then run together (step_wait gets the answer)."""
        self._send(f"step {n}")

    def step_wait(self):
        answer = self._line()
        if not answer.startswith("ok"):
            raise RuntimeError(f"step: {answer}")
        words = answer.split()
        return int(words[1]), [int(b, 16) for b in words[2:]]

    def set(self, name, value):
        """Hold an input: an axis -1..1, a pedal 0..1, a button 0 or 1 (names: Supermodel.ini's, without 'Input').
        An axis goes the trigonometric way, as a real wheel and BackForceFeeder: +1 fully left (or up), -1 fully
        right."""
        self._ok(f"set {name} {value}")

    def setraw(self, name, value):
        self._ok(f"setraw {name} {int(value)}")

    def release(self, name="all"):
        self._ok(f"release {name}")

    def get(self, name):
        """The raw value the game reads from an input now (held, or read from its device at the last frame)."""
        answer = self._ask(f"get {name}")
        if not answer.startswith("value "):
            raise RuntimeError(answer)
        return int(answer.split()[1])

    def servo_on(self):
        """Start the drive board's journal: what its Z80 writes to its outputs at each NMI."""
        self._ok("servo on")

    def servo_off(self):
        self._ok("servo off")

    def servo(self):
        """The journal since the last call: [(frame, nmi, port, value)]. Ports: 28 a new command shown to the Z80,
        29 its reply (changes only), 2A, 2D, 2E the servo or motor boards (every write)."""
        n = int(self._ask("servo").split()[1])
        out = []
        for _ in range(n):
            frame, nmi, port, value = self._line().split()
            out.append((int(frame), int(nmi), int(port, 16), int(value, 16)))
        return out

    def inputs(self):
        """The game's inputs: [(name, type)], type axis, analog, switch or other."""
        n = int(self._ask("inputs").split()[1])
        return [tuple(self._line().split()) for _ in range(n)]

    def obs(self, width=0, height=0, raw=False):
        """The last frame scaled to width x height (its own size if 0): a PIL image, or (w, h, RGB bytes) if raw."""
        header = self._ask(f"obs {width} {height}").split()
        if header[0] != "obs":
            raise RuntimeError(" ".join(header))
        w, h = int(header[1]), int(header[2])
        data = self._read(w * h * 3)
        if raw:
            return w, h, data
        from PIL import Image
        return Image.frombytes("RGB", (w, h), data)

    def shot(self, path):
        """Save the last frame at full size as a BMP file."""
        self._ok(f"shot {path}")

    def save(self, path):
        self._ok(f"save {path}")

    def load(self, path):
        self._ok(f"load {path}")

    def ffb(self):
        """The force feedback commands Supermodel sent since the last call: [(frame, input, effect, force)].
        force -1..1, the trigonometric way as the axes: a constant force > 0 pushes to the left (towards
        set('Steering', 1)), so vJoy receives a magnitude of force * 10000 (the same rule as sm2-emu's)."""
        n = int(self._ask("ffb").split()[1])
        out = []
        for _ in range(n):
            frame, name, effect, force = self._line().split()
            out.append((int(frame), name, effect, float(force)))
        return out

    def outputs(self):
        """The outputs set so far: {name: value} (RawDrive, lamps...)."""
        return {k: int(v, 16) for k, v in (item.split("=") for item in self._ask("outputs").split()[1:])}

    def ram(self, address, length):
        """length bytes read by the main CPU from address."""
        header = self._ask(f"ram {address:X} {length:X}").split()
        if header[0] != "ram":
            raise RuntimeError(" ".join(header))
        return self._read(int(header[1]))

    def settle(self, ms):
        """Wait ms milliseconds before each frame, so that vJoy set just before a step is read in that frame, as fast
        as the host goes (-no-throttle); real-time pacing leaves that time anyway. The same command as sm2-emu's."""
        return int(self._ask(f"settle {int(ms)}").split()[1])

    def frame(self):
        return int(self._ask("frame").split()[1])

    def close(self):
        if self.proc.poll() is None:
            try:
                self._ask("quit")
            except (RuntimeError, OSError):
                pass
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.log is not subprocess.DEVNULL:
            self.log.close()
