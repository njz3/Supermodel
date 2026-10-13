"""Check that force feedback sent to vJoy reaches our callback: SDL2's haptic API (DirectInput
underneath) plays a rumble on the vJoy device while vjoy.py logs the packets."""
import ctypes
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vjoy import VJoy  # noqa: E402

SDL = ctypes.CDLL(sys.argv[1])
SDL.SDL_HapticName.restype = ctypes.c_char_p
SDL.SDL_HapticOpen.restype = ctypes.c_void_p
SDL.SDL_GetError.restype = ctypes.c_char_p

vj = VJoy(1)
vj.acquire()
vj.log_ffb()
print("SDL_Init", SDL.SDL_Init(0x200 | 0x1000))
n = SDL.SDL_NumHaptics()
print("haptics:", n, [SDL.SDL_HapticName(i) for i in range(n)])
for i in range(n):
    if b"vJoy" in (SDL.SDL_HapticName(i) or b""):
        h = SDL.SDL_HapticOpen(i)
        print("open", h, SDL.SDL_GetError())
        print("rumble supported", SDL.SDL_HapticRumbleSupported(ctypes.c_void_p(h)))
        print("init", SDL.SDL_HapticRumbleInit(ctypes.c_void_p(h)), SDL.SDL_GetError())
        print("play", SDL.SDL_HapticRumblePlay(ctypes.c_void_p(h), ctypes.c_float(0.5), 500), SDL.SDL_GetError())
        time.sleep(1)
        SDL.SDL_HapticClose(ctypes.c_void_p(h))
time.sleep(0.5)
pk = vj.take_ffb()
print("packets:", len(pk))
for t, r in pk[:20]:
    print(r)
vj.release()
