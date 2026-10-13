"""vJoy from Python (vJoyInterface.dll, ctypes): axes and buttons of a device, and
the force feedback packets an application sends to it, timestamped.

Adapted from sm2-emu's autopilot/vjoy.py. BackForceFeeder must not run (it owns
vJoy then)."""
import ctypes
import subprocess
import threading
import time
from ctypes import wintypes

DLL = r"C:\Program Files\vJoy\x64\vJoyInterface.dll"

AXIS_X, AXIS_Y, AXIS_Z, AXIS_RX, AXIS_RY, AXIS_RZ = 0x30, 0x31, 0x32, 0x33, 0x34, 0x35
AXIS_MAX = 0x8000
STAT = {0: "owned", 1: "free", 2: "busy", 3: "missing", 4: "unknown"}

PT = {0x01: "effect", 0x02: "envelope", 0x03: "condition", 0x04: "periodic", 0x05: "constant",
      0x06: "ramp", 0x07: "custom", 0x08: "sample", 0x0A: "operation", 0x0B: "free", 0x0C: "control",
      0x0D: "gain", 0x0E: "custom-set", 0x11: "new", 0x12: "block-load", 0x13: "pool"}
ET = {0: "none", 1: "constant", 2: "ramp", 3: "square", 4: "sine", 5: "triangle", 6: "sawtooth-up",
      7: "sawtooth-down", 8: "spring", 9: "damper", 10: "inertia", 11: "friction", 12: "custom"}
OP = {1: "start", 2: "solo", 3: "stop"}
CTRL = {1: "enable-actuators", 2: "disable-actuators", 3: "stop-all", 4: "reset", 5: "pause", 6: "continue"}


class EffConstant(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("Magnitude", ctypes.c_long)]


class EffReport(ctypes.Structure):
    # vJoy 2.2's FFB_EFF_REPORT (vjoyinterface.h). DirectInput folds a cartesian direction into the polar one before
    # vJoy sees it: an effect on the X axis comes as 90 degrees, one on the Y axis as 180 (or 0)
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("EffectType", ctypes.c_int), ("Duration", ctypes.c_ushort),
                ("TrigerRpt", ctypes.c_ushort), ("SamplePrd", ctypes.c_ushort), ("StartDelay", ctypes.c_ushort),
                ("Gain", ctypes.c_ubyte), ("TrigerBtn", ctypes.c_ubyte), ("AxesEnabledDirection", ctypes.c_ubyte),
                ("Polar", wintypes.BOOL), ("Direction", ctypes.c_ushort), ("DirY", ctypes.c_ushort)]


class EffOp(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("EffectOp", ctypes.c_int), ("LoopCount", ctypes.c_ubyte)]


class EffCond(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("isY", wintypes.BOOL), ("CenterPointOffset", ctypes.c_long),
                ("PosCoeff", ctypes.c_long), ("NegCoeff", ctypes.c_long), ("PosSatur", ctypes.c_ulong),
                ("NegSatur", ctypes.c_ulong), ("DeadBand", ctypes.c_long)]


class EffPeriod(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("Magnitude", ctypes.c_ulong), ("Offset", ctypes.c_long),
                ("Phase", ctypes.c_ulong), ("Period", ctypes.c_ulong)]


FFB_CB = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)


def bff_running():
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
    return any(line.lower().startswith('"backforcefeeder') for line in out.splitlines())


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class VJoy:
    def __init__(self, device=1):
        self.dll = ctypes.WinDLL(DLL)
        self.dev = device
        self.ffb = []                      # (time, dict) for each FFB packet, filled from vJoy's thread
        self._lock = threading.Lock()
        self._cb = None

    def status(self):
        return STAT.get(self.dll.GetVJDStatus(self.dev), "?")

    def describe(self):
        d = self.dll
        axes = [name for name, u in (("X", AXIS_X), ("Y", AXIS_Y), ("Z", AXIS_Z), ("RX", AXIS_RX), ("RY", AXIS_RY),
                                     ("RZ", AXIS_RZ)) if d.GetVJDAxisExist(self.dev, u)]
        ffb = bool(d.vJoyFfbCap(ctypes.byref(ctypes.c_bool())))
        return {"status": self.status(), "axes": axes, "buttons": d.GetVJDButtonNumber(self.dev),
                "ffb_driver": ffb, "enabled": bool(d.vJoyEnabled())}

    def acquire(self):
        if bff_running():
            raise RuntimeError("BackForceFeeder is running: vJoy is its own")
        if self.status() not in ("free", "owned"):
            raise RuntimeError(f"vJoy device {self.dev} is {self.status()}")
        if not self.dll.AcquireVJD(self.dev):
            raise RuntimeError(f"cannot acquire vJoy device {self.dev}")
        self.dll.ResetVJD(self.dev)

    def release(self):
        self.dll.ResetVJD(self.dev)
        self.dll.RelinquishVJD(self.dev)

    def axis(self, usage, fraction):
        """fraction 0..1 of the axis' travel."""
        v = int(round(1 + max(0.0, min(1.0, fraction)) * (AXIS_MAX - 1)))
        self.dll.SetAxis(v, self.dev, usage)

    def button(self, number, pressed):
        """number: vJoy's, from 1."""
        self.dll.SetBtn(bool(pressed), self.dev, number)

    def log_ffb(self):
        """Record every FFB packet sent to the device (self.ffb)."""
        dll = self.dll

        def cb(data, _user):
            try:
                p = ctypes.c_void_p(data)
                t = ctypes.c_int()
                if dll.Ffb_h_Type(p, ctypes.byref(t)) != 0:
                    return
                rec = {"type": PT.get(t.value, hex(t.value))}
                if t.value == 0x05:
                    e = EffConstant()
                    if dll.Ffb_h_Eff_Constant(p, ctypes.byref(e)) == 0:
                        rec.update(ebi=e.EffectBlockIndex, magnitude=s16(e.Magnitude))
                elif t.value == 0x01:
                    e = EffReport()
                    if dll.Ffb_h_Eff_Report(p, ctypes.byref(e)) == 0:
                        rec.update(ebi=e.EffectBlockIndex, effect=ET.get(e.EffectType, e.EffectType),
                                   duration=e.Duration, gain=e.Gain, axes=e.AxesEnabledDirection,
                                   polar=bool(e.Polar), dir_deg=round(e.Direction * 360.0 / 32767), dir_y=e.DirY)
                elif t.value == 0x0A:
                    e = EffOp()
                    if dll.Ffb_h_EffOp(p, ctypes.byref(e)) == 0:
                        rec.update(ebi=e.EffectBlockIndex, op=OP.get(e.EffectOp, e.EffectOp))
                elif t.value == 0x03:
                    e = EffCond()
                    if dll.Ffb_h_Eff_Cond(p, ctypes.byref(e)) == 0:
                        rec.update(ebi=e.EffectBlockIndex, y=bool(e.isY), centre=s16(e.CenterPointOffset),
                                   pos=s16(e.PosCoeff), neg=s16(e.NegCoeff), dead=e.DeadBand)
                elif t.value == 0x04:
                    e = EffPeriod()
                    if dll.Ffb_h_Eff_Period(p, ctypes.byref(e)) == 0:
                        rec.update(ebi=e.EffectBlockIndex, magnitude=e.Magnitude, period=e.Period)
                elif t.value == 0x0C:
                    c = ctypes.c_int()
                    if dll.Ffb_h_DevCtrl(p, ctypes.byref(c)) == 0:
                        rec.update(ctrl=CTRL.get(c.value, c.value))
                elif t.value == 0x0D:
                    g = ctypes.c_ubyte()
                    if dll.Ffb_h_DevGain(p, ctypes.byref(g)) == 0:
                        rec.update(gain=g.value)
                with self._lock:
                    self.ffb.append((time.time(), rec))
            except Exception as exc:          # never let an exception into vJoy's thread
                with self._lock:
                    self.ffb.append((time.time(), {"type": "error", "error": str(exc)}))

        self._cb = FFB_CB(cb)
        self.dll.FfbRegisterGenCB(self._cb, None)

    def take_ffb(self):
        with self._lock:
            out, self.ffb = self.ffb, []
        return out


if __name__ == "__main__":
    vj = VJoy(1)
    print(vj.describe())
    print("BFF running:", bff_running())
