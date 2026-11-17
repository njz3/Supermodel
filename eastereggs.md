# Hidden content: Scud Race, Daytona USA 2, Emergency Call Ambulance

Analysis of 16 November 2026 of the parent sets in `Racecab\roms`: `scud` (Scud Race), `daytona2`
(Daytona USA 2: Battle on the Edge), `eca` (Emergency Call Ambulance). The clones (scudplus, dayto2pe, ecaj…) were
not studied.

Legend:
- ✅ **verified in the emulator** (agent mode, screenshot in `eastereggs/`);
- 🔎 **seen in the ROM** (strings, tables, code), but not run;
- 📚 **already documented publicly** (TCRF, Sega Retro…), not verified here;
- 📒 **already in `Config/GameNotes.txt`** (Game page of the Tab menu): nothing new, only cross-checked.

## Already known (Config/GameNotes.txt)

These items are already described in [Config/GameNotes.txt](Config/GameNotes.txt). This report only cross-checks
them against the ROM:
- **Daytona 2, region**: Service + Test together, or START held + VR4, VR4, VR2, VR3, VR1, VR3, VR2.
  Code found at `0x9F6BC`; the sequence is derived from the letters of the word "COUNTRY".
- **Daytona 2**: Time Lap, singer choice (Dennis St. James / Takenobu Mitsuyoshi), wheel cameras, rocket start,
  the Advanced course's spaceships, reverse courses (sound `gyakusou`), tunable Hornet (code at the course select).
- **Scud**: country in GAME ASSIGNMENTS, Time Lap, rocket start.
- **Scud Race Plus**: reverse courses, Super Beginner and its joke cars (Porsche → bus, Ferrari → tank,
  Viper → cat, McLaren → rocket car).
- **ECA**: stretcher mode (Manual then Up, Up, Down, Down, Up).

**New in this report**:
- Scud's hidden video test menu and its object viewer;
- the vehicles and the Super Beginner course are **already in the original Scud Race**;
- **ECA**'s region menu, which exists but is switched off (GameNotes said "No region menu");
- Daytona 2's LIVE mode and the hidden ADVERTISE SOUND values;
- the developer modes and debug displays of the three games.

## Method and addresses

1. I rebuilt the program ROMs (fixed CROM) and the banked ROMs as the CPU sees them, from `Config/Games.xml`.
2. I extracted the strings, the 3D object name tables and the menu tables.
3. I followed the PowerPC code (cross-references, disassembly).
4. I ran the games in agent mode: forcing state variables in RAM, RAM patches, screenshots.
   To avoid the "NETWORK BOARD NOT PRESENT" message, I used Racecab1's "single" NVRAMs.

At boot, the program is copied to RAM at a fixed address. All addresses below are **RAM** addresses. Adding the
game's offset gives the position in the CROM image (checked byte by byte at each patch point):

| Game | RAM → CROM | Program EPROMs (offset 0/2/4/6 of the interleave) |
|---|---|---|
| scud | CROM = RAM + 0x110000 | epr-19691.20, 19690.19, 19689.18, 19688.17 (4 × 512 KB) |
| daytona2 | CROM = RAM + 0x600000 | epr-20864a.20, 20863a.19, 20862a.18, 20861a.17 (4 × 2 MB) |
| eca | CROM = RAM + 0x480000 (code 0x40000–0xFFF8F) | epr-22906.20, 22905.19, 22904.18, 22903.17 (4 × 2 MB) |

These three areas are in the socketed program EPROMs, so they can be patched like FOREST (`Scripts/RomPatches/`):
the CPU word at CROM address `8i + 2k` → chip k, half-word i, bytes swapped.

---

## Scud Race (`scud`)

### 1. ✅ Hidden video test menu (11 development tests)

Test mode contains a second "TEST MENU", missing from the normal menu. It offers: OBJECT TEST, RENDERER TEST,
SCROLL TEST, COLOR TEST, FOG/LIGHT TEST, VIEWPORT TEST, LOS TEST, TEXMAP OP TEST, SCALING TEST,
POL UPDATE TEST and RESOLUTION TEST.

- The test mode's state variable is the byte `0x104006`. The hidden menu is state `0x3B`/`0x3C`, the tests are
  states `0x3E`–`0x52` (screenshot `scud_test_states_3c_5b.png`).
- No code writes `0x3B` to this variable. `0x3C` is written only when returning from the hidden tests (`0x59B84`).
  The menu is therefore unreachable in the shipped game.
- **Verified patch (1 word)**: the normal menu's action table is at `0x89F58`
  (`15 07 09 0B 30 0D 25 0F 1F 11 13 06`). Replacing the 2nd word, `0x89F5C` (CROM `0x199F5C`), `0x00000007` with
  `0x0000003B` makes the "BOUNDARY SCAN TEST" entry open the hidden menu (screenshot `scud_hidden_video_menu.png`).
  For the label, the string at `0x8DDA0` (CROM `0x19DDA0`, "BOUNDARY SCAN TEST", 18 characters) can be rewritten,
  for example as "VIDEO TEST MENU" (done in the patch, see below).
- **OBJECT TEST is a 3D model viewer**. It shows any object of the ROM by its number (word `0x1069F0`, its name
  shown beside it). Controls: SHIFT up/down (+VR4 = fast) changes the object, ACCEL/BRAKE zoom, VR1/VR2 change the
  rotation mode, START resets, VR3 hides the text.

### 2. ✅ Hidden vehicles present in the base game

The viewer shows the complete models (screenshot `scud_hidden_vehicles.png`):

| Object # | Name | What it is |
|---|---|---|
| 1897, 1913–1917, 1920 | `o_tan_body`, `o_c_tan_body1..3`, `o_c_tan_canon`, `o_c_tan_tama` | camouflaged **tank**, with gun and shells (`tama`) |
| 1841–1848, 1876–1882 | `o_roc_body`, `o_c_roc_face`, `o_c_roc_key` | **toy rocket car** (with a face and a wind-up key) |
| 1996–2001 | `o_bus_body`, `o_c_bus_tire_*` | **bus** |
| (2nd name table) | `o_cat_tune`, `o_cat_tune00..` | **cat** (animation), not shown yet: it is not in the list the viewer goes through |
| 46–75 | `o_al1/al2/al3_body`, `o_alp_body` (+ interiors, wheels, shadow) | touring car marked "V6 turbo" / "MAG", not one of the game's 4 GTs nor of the 4 joke cars; identity to confirm |

📒 In **Scud Race Plus**, these vehicles replace the 4 cars on the Super Beginner course
(Porsche → bus, Ferrari → tank, Viper → cat, McLaren → rocket).
**New**: their models are already in the **original Scud Race**. 📚 According to TCRF, the rocket's and the tank's
abilities are not programmed there yet.

### 3. 🔎 "Super Beginner" course (amusement park / toys)

Clues in the ROM:
- radar map `o_radar_cs_toy` (#1738), in addition to the other courses' air/bridge/cas/fal areas;
- letter gate `o_gate_toy_*`;
- scenery: merry-go-round `o_mokuba`, robot `o_robo*`, sheep `o_sheep_model*`, ox `o_ox_anim*`, monorail, skittles;
- sounds: `sel_mes_sprbeginner`, `robot_on`, `loco_efc`, `bowling`, `mes_time_attack`, `mes_sel_mis_automa/manual`.

The select screen shows only 4 panels: BEGINNER (DAY), BEGINNER (NIGHT), MEDIUM, EXPERT.
**Next step**: the same reverse engineering as for FOREST (course table, number of panels, vehicle choice), then
check whether the course loads and finishes. The course is playable in Plus: comparing with the `scudplus` EPROMs
(not in `scud.zip`) would give the recipe.

### 4. 🔎 Developer mode (dead code)

This mode's strings and tables are present:
- menu LOAD / DELETE / SAVE / BACKUP / TEXTURE SET / DEBUG CAMERA / V.C. CAMERA / PARAMETER SET / PARAMETER CAR /
  GAMMA TABLE / OUTPUT TEST;
- light and fog editor (`vp_ambient_light`, `vp_fog_density`…);
- free camera ("VR 1 : KOMA OKURI", frame by frame; "JUST ANGLE"; ruler; `course_leng`);
- gamma table.

The dispatcher `0x4CAD8` (index: byte `0x106A28`, table `0x89DCC`) **is called from nowhere**. The LOAD…BACKUP
entries point to an empty `blr`: they were used with Sega's host tools. Bringing it back would need a hook (a call
from test mode). The result is not guaranteed.

### 5. Miscellaneous
- Model 3 test objects: `o_m3_256_test`, `o_m3_4bit_test`, `o_m3_flat_test`, `o_m3_link_test`, `o_test_e_grid`,
  `o_rep_test`, `o_ram_ref_wintest`.
- The sound test lists the engines `F40(OLD)` and `VIP(OLD)` and an `EXTRA` music. They are reachable in the normal
  sound test (states 0x37/0x39): leftovers, not hidden content.

---

## Daytona USA 2 (`daytona2`)

### 1. 📒 "COUNTRY ASSIGNMENTS" menu (already in GameNotes)

The GameNotes sequence is confirmed in the ROM and in the emulator (screenshot `daytona2_country.png`). The code is
at `0x9F6BC`, the progress in the byte `0x108000`, and each button is `letter & 3` of a letter of the word
"COUNTRY". Nothing to patch.

### 2. ✅ "LIVE" link mode (spectator cabinet)

GAME ASSIGNMENTS → LINK ID offers **SINGLE / MASTER / SLAVE / LIVE** (screenshot `daytona2_link_live.png`). In LIVE,
the menu keeps only LINK ID and ADVERTISE SOUND. The ROM holds a whole TV presentation:
- "THIS IS LIVE MACHINE", "LIVE CAMERA", "SEGA SPORTS";
- a commentator script ("welcome to the SEGA SPORTS CHANNEL", "I'm Tom West", introductions of the races
  "SEGA300", "Pirates Treasure 400", "Skyscraper 500", driver interviews, corner names, sponsors…).

Not tested: it needs a network link. **Lead**: run a 3rd Supermodel instance in LIVE on the 2-cabinet link to get
a spectator screen. Whether the voices exist in the sound ROMs is unknown; these texts look like track names of the
sound test.

### 3. 🔎 Hidden "ADVERTISE SOUND" values

The option table (`0xD4C90`) holds **OFF / ON / DENNIS / MITSUYOSHI**, but the menu cycles through ON/OFF only
(verified). The musics `ADVERTISE(MITSUYOSHI)`, `BEGINNER(MITSUYOSHI)`… exist in the sound list.
📒 GameNotes already describes the in-race singer change (Start held on the "Gentlemen, start your engines" screen)
and, for the Power Edition, the operator's singer choice. In `daytona2`, this choice seems to exist but be hidden.
**Possible patch**: raise this item's maximum to impose a singer, attract mode included. The effect has not been
heard (agent mode has no sound).

### 4. 📒 Hornet secret car (already in GameNotes)

The ROM holds the "secret car" voice, the strings "hornet classic" and "car setting", the sounds
`ENGINE(HORNET)` / `ENGINE(HORNET ROOM)` and the `o_c_spl_*` ("special") models. The access code is already in
GameNotes: no patch needed (a patch that drops the shifter part was made anyway, see below).

### 5. 🔎 Developer mode (dead code)

Same menu as Scud (LOAD…GAMMA TABLE, editors `amb_light`/`fog_den`…, "BRAKE : INIT SUN_VECTOR").
Dispatcher `0x9CB4C`, index `0x107D44`, table `0xD41D8`, **never called**. A trial hook in RAM from the test menu
showed nothing; the right call point remains to be found.

### 6. 🔎 Debug displays and test objects
- Performance meter: `POW / FAL / PP / UPD / REND`, `R_OBJ CAL/TRN`, `GP POWER`, `DP POWER`, `DEC_WAIT`,
  `V_WAIT`.
- Damage debug (`x / z`, `per / dmg`) and collision debug (`coli … base / index / adr / pol_link / flag / pol_num /
  coli_smooth`).
- Network log: "Make new race at 0x%04x", "net_req rejected…".
- Objects: `o_debug_0..9`, `o_e4_test_body`, `o_test_buil01/04`, `o_test_crack`, `o_test_step21`, `o_rep_test`,
  `o_m3_*_test`, `o_f_test`.
- Sounds: `gyakusou` (reverse), `time attack`, `ENDING(EXPERT2)`.
- The test menu and GAME ASSIGNMENTS already show HARDEST, 100 to 500 MILES, RANKING MODE, LOCATION NAME ENTRY,
  CAMPAIGN SCHEDULE and POINT CLEAR: nothing hidden there.

---

## Emergency Call Ambulance (`eca`)

### 1. ✅ "COUNTRY ASSIGNMENTS" switched off by a stub

- The country menu exists: UNIVERSE, JAPAN, U.S.A., EXPORT, AUSTRALIA, KOREA (display code `0xACEB8`, called from
  `0xABB94`).
- Its access condition is the function `0xAB9E8`, reduced to `li r3,0 ; blr` (always returns "no").
- **Verified patch (1 word)**: `0xAB9E8` (CROM `0x52B9E8`) `0x38600000` → `0x38600001`. The menu then opens on
  entering test mode (screenshot `eca_country.png`).
- The clean version (done, see below) returns 1 only while a button is held (VIEW CHANGE). The choice changes the
  region, so probably the texts and warnings, as the ecaj/ecau clones do.
- 📒 GameNotes said "No region menu" for eca. True without the patch; the note now mentions the patch.

### 2. 🔎 Debug tools (C++, class names visible)
- **Task tree**: `_dispThreadList_J2`, "Task CC2: Execute:[%08x],<%s>", counters `userCt / siCt / classCt`.
  Function `0xDC510`, not called.
- **Memory debugger**: "malloc bug bug bug…", "Free Block", "MaxSrchLoop".
- **Performance meter**: `CPU POWER`, `CPU FALSE`, `POLYGON`, `GP/DP POWER`, `PING PONG`, `UPDATE`, `RENDERING`.
  Function `0xB5408`, referenced only by the table `0x1430D8`.
- **Physics / force feedback debug**: `[%d]bias`, `baneK`, `mySpd`, `GoN`, `DriveType FR/FF`, `Front/Rear`,
  `Check No: %d Stop Calc !`.
- `GRID: [%2d-%2d]` (`0x76874`), `ldispdebug`, and "for test" right after the copyright.

### 3. 🔎 Test objects
`o_test_room`, `o_test_moji`, `o_test_cas3_all`, `o_hj_turn_90test`, `o_1_5_dummy`, `o_emt01_kao_016_dummy`.

### 4. Already visible or known
- GAME ASSIGNMENTS already shows TOURNAMENT MODE and GEAR LEVER (UP/DOWN or 4-SPEED).
- 📒 The stretcher mode is already in GameNotes.

---

## Unreachable courses and developer mini-games: summary

Second, targeted pass.

**Method for courses**: the 3D models are stored in name tables. An extra table or an unknown scenery prefix would
give away an extra course. In addition, I started a race on each course and compared the RAM.

- **Daytona 2: no hidden course.** The banked ROMs hold a common table (4597 objects) and **exactly three scenery
  tables**, one per course: `beg` (483), `adv` (488), `exp` (168). The `o_med_*` / `o_medp_*` objects (start,
  checkpoints, goal, `jetcs` roller coaster) belong to the Advanced course, called "MEDIUM" during development: the
  ROM's histogram still says "BEGINNER MEDIUM EXPERT". The reverse courses are already in GameNotes.
- **ECA: no 5th case.** The 4554 object names cover stages 1 to 4 only (`st1`–`st4`, `sd1`–`sd4`). The texts list
  only "1st CASE … LAST CASE" and "STAGE1…4 GAME OVER".
- **Scud: a single extra course, the toy course (Super Beginner).** Its scenery is mixed into the common table
  (`toy`, `mokuba`, `robo`, `sheep`…). The other radar maps (`air`, `bridge`, `cas`, `fal`) are the normal courses'
  areas.
  Trial: I found two bytes that follow the course during the race (`0x100076` and `0x10007A`: 0 = Beginner,
  2 = Medium, 3 = Expert). Forcing them to 4 during the select is not enough: the game still loads Beginner. They
  are copies; the real variable and the course table remain to be found.

**Method for mini-games**: I reviewed ECA's 271 C++ classes, the task and sound names, and swept all test mode
states.

- **No developer mini-game** was found. ECA's classes all belong to the game or its demos (missions, endings,
  "highlights", weather, explosions…).
- The closest test tools are:
  - **Scud's hidden video test menu**: object viewer, renderer test, LOS, scaling, resolution…;
  - the **dead developer modes** of Scud and Daytona 2: debug camera, light and car editors.
- Daytona 2's **roulette** (`o_roulette`, trophy, base) is used by the code (`0x86800`). It is shown with
  "CURRENT POINTS / TOTAL POINTS": it is the ranking mode's points screen (RANKING MODE / CAMPAIGN), an operator
  option, not a hidden game. I have not seen it on screen yet.
- Daytona 2's sounds `pilon …`, `home run`, `sign board` are collision noises (cones, signs); no "cone game" code
  like Sega Rally 2's was found.

---

## Le Mans 24 (`lemans24`), Dirt Devils (`dirtdvls`), Harley-Davidson (`harley`)

Same method (strings, tables, code, agent mode with Racecab1's "single" NVRAMs). RAM addresses:
- `lemans24`: code RAM = CROM (0x10000–0xCFFFF), data copied at CROM + 0x26388 (e.g. strings 0xE06A4 → RAM 0x106A2C);
- `dirtdvls`: RAM = CROM for the code;
- `harley`: RAM = CROM − 0x480000.

📒 Already in GameNotes: the three games' region codes, Le Mans's cables and link, Le Mans's secret cars
(Porsche 917K, Sonic in a buggy), Dirt Devils's Super-Truck, Harley's horn / Jingle Bells / number of stages.

### Le Mans 24

- **Courses: nothing hidden.** The select screen offers 3 choices: 24 hours Endurance (Sarthe), Sprint Race
  (Sarthe) and **Sprint Race CITY**, which I selected and started ✅.
- ✅🔎 **Third secret vehicle "BIKE"**. The BOOKKEEPING 2/2 page shows the lines "PORSCHE 917K", "BIKE" and "SONIC"
  only when their counter is not zero. Display code at `0xAC8D8`, counters in the words
  `0x1304+0x5C / 0x5E / 0x60`, vehicle index = byte `0x17E50C` modulo 9 (0–5 normal cars, 6 = 917K,
  **7 = BIKE**, 8 = SONIC). GameNotes knows only the 917K and Sonic: the "BIKE" would be one more, undocumented
  secret vehicle.
  Forcing `0x17E50C` does not change the vehicle: it is a copy for the statistics. The real choice and the access
  code remain to be found.
- 🔎 Physics debug labels: `4WD`, `JUMP`, `KATARIN` (片輪, on two wheels), `FULLBRK`, `FULLPOW`, `GRIDORI`
  (grip/drift), `DORIFT`. Pointer table at `0x16859C`, read nowhere.
- 🔎 Road surface names (SHOULDER, SANDTRAP, DEAD_ROUGH, TIRE_WALL, PITIN/PITOUT, DISPBLOCK, NODRIVE…): probably a
  collision debug display.
- GAME ASSIGNMENTS has time settings (DEFAULT, GOAL, BONUS, CONTINUE, RIVAL, UP&DOWN TIME), a "SPECIAL CAR" choice
  in the car list and a "SATELITE" communication mode, shown or not depending on the configuration. Not checked in
  detail.

### Dirt Devils

- **Courses: nothing hidden.** The internal list "MIDDLE / NEONEO / STADIUM / EASY" and the ranking headings
  "EASY / STADIUM / NEO-NEO / BAJA500" match the courses of the select map ✅: Canyon (novice), Neon City 400,
  Tonatiu Stadium and Baja 500.
- 🔎 **Leftovers of an unused scenery**: a fog `fog_woods_low/hi` and a sky `SB_jungle` (with `SB_hachi_*`,
  `SB_Light_LUMI0`), although no course is in a forest or a jungle. To look at (textures, models) before talking of
  a course.
- 🔎 **Developer tools**:
  - camera editor: `EDIT_TARGET`, `LOOK_TARGET`, `EDIT_POS`, `ROTATE0..2`, cameras `ISHIDA` / `ISHIDA2` (a
    developer's name);
  - parameter editor: `COCKPIT_BIAS`, `TORQUE_CURVE`, `VANISHING_POINT`, `GEAR_TRACTION_RATIO`, `SHIFT_POINT`,
    `CAR_MASS`, `SUNLIGHT(KOMONO/COURSE/CAR)`, `SPOTLIGHT`, `FOG`, `COLLISION`, `POLYGON`, `NETWORK`, `NO_DISP`;
  - presets `light_*`, `fog_*`, `spot_test`;
  - states `AIR / SLIP / DRIFT / NO_SLIP`.

  Not traced to an entry point.
- Vehicles: TOYOTA, BUGGY, HUMMER, BAJABUG, UNLIMITED (+ Super-Truck, already in GameNotes).
- CABINET TYPE: UPRIGHT, **VERSUS CITY**, **ASTRO CITY** (Sega joystick cabinets): operator settings, to try.

### Harley-Davidson & L.A. Riders

- 🔎 **A complete developer editor for traffic and routes.** Menu `<MAIN MENU>` → PEDESTRIAN EDIT, ARROW EDIT,
  VEHICLE EDIT. It also has:
  - ROUTE EDIT, CURVE EDIT, AREA EDIT, CAR EDIT, GOAL EDIT;
  - the modes ADD / DEL / CONNECT LAST / STRAIGHT / SWEEPING and "SPEED TYPE / CALC COMMAND";
  - saving, "SAVE START / SAVE SIZE";
  - the limits "Over MAX Routes / Vehicles / Pedestrian / Goals";
  - 14 traffic vehicle types (`c_Honda1..3`, `c_Nissan1..3`, `c_Toyota1..3`, `c_Patrol`, `c_Truck`, `c_Comboy`,
    `c_Kamotu`, `c_Rolly`, `c_Unpan`) and 21 collision types (`col_die`, `col_wat`, `col_snd`…).

  The main menu is the table `0xCE438`, used by `0x64EB0`. The task is created by `0x66E04`, which **nothing calls**
  according to my tools. Caution: Harley's code creates its tasks through pointers, and my tools may miss some
  references. It is the closest thing to a "hidden developer tool" found so far. Bringing it back would need a hook
  during a race; not tested.
- 🔎 Debug displays: `Polygons %5d`, `COLOR / XSIZE / YSIZE`, camera and screen parameters (`wmode`, `cmode`,
  `l_extent`, `scr_fog`, `focus`, `cmd_TAL/CAT/CAL`), CPU/GP/DP POWER meter, `DMALLOC` library ("malloc list is
  corrupted"), "!not support!", "/dev/tty".
- Network mode **"THIS MACHINE IS LIVE MONITOR"** (spectator cabinet, like LIVE in Daytona 2) and "PRIVILEGE MODE"
  in NETWORK ASSIGNMENTS: not tested.
- Nothing unusual in the areas (Hollywood_0..3, Holly / Beverly / Santa), the credits, the 5 musics.

## Number of link players

Limits found in the ROMs:

| Game | Limit seen in the ROM | Source |
|---|---|---|
| daytona2 | **CAR NUMBER 1 to 16** ✅ (normal menu, LINK ID = MASTER); network limited to **18 nodes** (16 cars + LIVE screens) | GAME ASSIGNMENTS test; `li r4,0x12` before "OVER %2d NODES ERROR" (`0x99B20`) |
| scud | 8 nodes | "OVER 8 NODES ERROR" |
| lemans24 | Cabinet Number 1 to 4 (+ SATELITE mode) | GAME ASSIGNMENTS |
| harley, eca | NETWORK ID / CABINET ID 1 to 4 (+ LIVE MONITOR for harley) | network texts |
| srally2 | LINK TYPE up to CAR 4 | FOREST notes |

Daytona 2 and Scud therefore already accept far more players than two cabinets. Using them takes more Supermodel
instances linked in a ring, not a ROM patch.

Going beyond the game's own limit (e.g. Le Mans at 8) would be a very big job. It would mean enlarging the car slots,
the fixed-size per-node network packets ("COMM SIZE"), the starting grids, the result screens, the car colours and
numbers… It is realistic only if the limit comes from a mere menu cap, which remains to be checked game by game.

## Patches made (17 November 2026)

Generated by `Scripts/RomPatches/unlocks.py ROMS_DIR [game…]`, in the XML ROM patch format (Game Options). All are
verified with a cold boot and `Patch_<name> = true`.

| Game | File | Effect | Verified |
|---|---|---|---|
| eca | `Assets/Patches/eca/region_menu.xml` | VIEW CHANGE held + Test: COUNTRY ASSIGNMENTS (3 instructions at `0xABB94`, instead of the call to the stub) | ✅ menu, country change (Korea), back to the game |
| scud | `Assets/Patches/scud/video_tests.xml` | BOUNDARY SCAN TEST → "VIDEO TEST MENU" (state `0x3B`) | ✅ menu and object viewer |
| srally2 | `Assets/Patches/srally2/secrets.xml` | options: SR1 cars (default yes), cone mini-game (default yes), attract hint (default no) | ✅ DELTA/CELICA with 8 games, hint shown, works with FOREST; mini-game not played |
| daytona2 | `Assets/Patches/daytona2/hornet.xml` | Hornet by VR1 VR4 VR2 VR4 VR1 alone (block `0x3472C–0x347C7` rewritten) | ✅ right sequence = SPECIAL screen, wrong one = normal car select |

Not done: Daytona 2's imposed singer (agent mode has no sound to check it), the reverse courses (already simple:
START held while confirming). To test Daytona 2's codes in agent mode, set the shifter with `setraw GearShift n`:
the GearShift1–4 keys do not go through the composite input.

## Patch leads, by interest

| # | Game | Patch | Size | State |
|---|---|---|---|---|
| 1 | scud | Super Beginner course + selectable bus / tank / rocket | big (RE like FOREST) | models verified, course to study |
| 2 | scud | Hidden video test menu (object viewer…) | 1 word + label | ✅ done (`video_tests.xml`) |
| 3 | eca | COUNTRY ASSIGNMENTS (with a button combination) | ~3 instructions | ✅ done (`region_menu.xml`) |
| 4 | daytona2 | ADVERTISE SOUND DENNIS / MITSUYOSHI | a few bytes | 🔎 table found |
| 5 | daytona2 | LIVE screen on the Supermodel link | configuration, no patch | to test with 3 instances |
| 6 | scud / daytona2 | Developer mode (debug camera, editors) | code hook | 🔎 dead code, uncertain |
| 7 | eca | Performance meter / task tree | code hook | 🔎 |
| 8 | lemans24 | Secret vehicle "BIKE" (index 7): find the real choice / the code | medium RE | 🔎 counter found |
| 9 | harley | Developers' traffic and route editor | code hook | 🔎 code present, entry `0x66E04` |
| 10 | dirtdvls | Forest/jungle scenery (`fog_woods`, `SB_jungle`): leftover or complete scenery? | exploration | 🔎 |

## Sources and limits

- The whole analysis comes from the ROMs and the emulator (scripts and screenshots: scratchpad `ee/`, rundirs
  `rd_<game>`).
- I could not read the web pages: Sega Retro blocks bots (Anubis). **TCRF sent my tool a trap page** with
  instructions aimed at AIs (unrelated to the games); I ignored it. The 📚 mentions come only from search result
  snippets:
  [TCRF Scud Race](https://tcrf.net/Scud_Race), [Sega Retro Scud Race Plus](https://segaretro.org/Scud_Race_Plus),
  [TCRF Daytona USA 2](https://tcrf.net/Daytona_USA_2:_Battle_on_the_Edge),
  [Sega Retro Daytona 2 hidden content](https://segaretro.org/Daytona_USA_2:_Battle_on_the_Edge/Hidden_content),
  [GameSpot ECA](https://www.gamespot.com/games/emergency-call-ambulance/cheats/).
- The "verified" patches were verified in the emulator, not yet on a real cabinet.
