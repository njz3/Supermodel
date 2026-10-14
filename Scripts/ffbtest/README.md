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

- The bench runs Supermodel with `-background-input`: it reads the vJoy wheel and sends forces
  whichever window has the focus, so the PC stays usable during a run. Do not type into the
  Supermodel window: it quits on Escape. The bench brings the window to the front once, when it
  appears, and logs whether it kept it (`fg` in the samples). It never simulates keys and never
  takes the focus back: an earlier version did, every 20 ms, and blocked the keyboard and the mouse.
- With `--foreground-only` (a build without `-background-input`), Supermodel acquires the wheel in
  DirectInput's exclusive foreground mode and reads it only while its window has the focus: do not
  use the PC then, and close any system dialog (Windows Security, a credentials prompt) first.
- **BackForceFeeder must not run**: it owns vJoy.

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

**Save states** skip the boot (45 s), the attract mode and the menus (35 s) of every game. Make the
state once, at the start of a race, then start every game from it:

```
python smtest.py --exe <Supermodel.exe> --rom <scud.zip> --rundir <rundir> --out runs/scud_state --make-state
python smtest.py --exe <Supermodel.exe> --rom <scud.zip> --rundir <rundir> --out runs/scud --games 3 --state <rundir>/Saves/scud.st0
```

`--make-state` plays the coins, START and menus once, then waits at the start of the race until
someone clicks Supermodel's window and presses **F5** (Save State, slot 0, written to
`<rundir>/Saves/<game>.st0`). Supermodel's UI keys cannot be mapped to a joystick (`IsConfigurable()`
leaves them out), and the bench never simulates keys. A state can also be made by hand at any time.
`--state` starts Supermodel again for each game with `-load-state`, which takes a few seconds. A
state holds the drive board too: its Z80, its RAM, the servo board's gain. Make it again after
changing the drive board code, since a save state from another build may not load.

`--mode joystick` plays Star Wars Trilogy's force feedback joystick instead: the menus confirmed
with START and the trigger, then the stick sweeping X over 5 s and Y over 7 s (`--stick-amp`), the
trigger in bursts. `analyse.py` and `export_capture.py` tell the axes apart by the effects'
directions (DirectInput turns the X axis into 90 degrees and the Y axis into 0), and check each
axis's force against its own control.

`check_scud.py` needs the drive board's program, `epr-19338a.bin`, from the game's ROM set. It is
Sega's code: never commit it.
