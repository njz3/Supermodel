# ffbtest: measuring Supermodel's force feedback through vJoy

A bench that plays a game in Supermodel through a vJoy wheel and records, with timestamps:

- every byte the game writes to its drive board, read on Supermodel's network outputs
  (`Outputs = net`, TCP port 8000, `RawDrive = n` lines);
- every force feedback packet Supermodel sends to the wheel through DirectInput, read with vJoy's
  FFB callback;
- the controls sent, whether Supermodel's window had the foreground, and screenshots.

`check_scud.py` then checks, command by command, that what reached the wheel is what the drive
board's program (EPR-19338A) makes of the command, translated the way `WheelBoard.cpp` translates
the servo board's registers. See `FFBRework.md` at the root of the repository for the context and
the results.

## Before running it

- **Do not use the PC while it runs.** Supermodel acquires a force feedback wheel in DirectInput's
  exclusive foreground mode: it reads the wheel and sends forces only while its window has the
  foreground. The bench brings the window to the front once, when it appears, and logs whether it
  kept it (`fg` in the samples). It never simulates keys and never takes the focus back: an earlier
  version did, every 20 ms, and blocked the keyboard and the mouse.
- **BackForceFeeder must not run**: it owns vJoy.
- A system dialog (Windows Security, a credentials prompt) keeps the foreground and makes a run
  useless: close it first.

## Requirements

- Windows, vJoy 2.2 (`C:\Program Files\vJoy\x64\vJoyInterface.dll`), device 1 with the X, Y, Z axes,
  16 buttons or more and force feedback enabled.
- Python 3 with Pillow (screenshots).
- A Supermodel build, and a working directory for it:

```
rundir/
  Config/Supermodel.ini   this folder's Supermodel.ini (vJoy as JOY1, dinput, ForceFeedback = 1, Outputs = net)
  Config/Games.xml        from Config/ of the repository
  Config/Music.xml
  NVRAM/                  configured NVRAM files: with a new one, Scud Race is LINK ID MASTER and
                          reboots on "NETWORK BOARD NOT PRESENT"
  Assets/                 the crosshair bitmaps
```

`python ffbprobe.py <SDL2.dll>` checks that vJoy receives force feedback at all: it plays a rumble
on the vJoy device through SDL's haptic API and prints the packets the callback got.

## Running

```
python smtest.py --exe <Supermodel.exe> --rom <scud.zip> --rundir <rundir> --out runs/scud --games 3
python analyse.py runs/scud --timeline
python check_scud.py runs/scud <epr-19338a.bin> --strength 5 --cabinet B1
python export_capture.py runs/scud capture.txt header.txt
```

`smtest.py` inserts coins, presses START, confirms the menus with START and the accelerator, then
drives floored for `--game-length` seconds (150 by default), the wheel swinging slowly
(`--steer-amp`) with a hard turn now and then (`--jolt-rate`). The car hits the walls often, which
is what makes the board work. Each run writes `drive.tsv`, `session.jsonl`, `supermodel_stdout.txt`
and the screenshots into `--out`.

`check_scud.py` needs the drive board's program, `epr-19338a.bin`, from the game's ROM set. It is
Sega's code: never commit it.
