# PokeDaisy for the Nintendo 3DS (early preview)

> [!IMPORTANT]
> **This is ratherDashing's working fork, not PokeDaisy.** It is where I try 3DS changes before sending them to
> [lidor30/pokedaisy](https://github.com/lidor30/pokedaisy) as pull requests. It has no releases and no support.
> For PokeDaisy on any device, the 3DS included, use the official project:
> **https://github.com/lidor30/pokedaisy** · [pokedaisy.com](https://pokedaisy.com)

A homebrew port of PokeDaisy for the **New 3DS / New 3DS XL / New 2DS XL**: the game runs on the top screen and the
companion lives on the touch screen. Game Boy Advance games run on **armGBA** (`third_party/armgba`, the
[ratherDashing/armGBA](https://github.com/ratherDashing/armGBA) fork's `3ds` branch: a cycle-accurate core with a
recompiler for the 3DS's ARM11, its renderer and sound mixer on the New 3DS's other cores), Game Boy and Game Boy Color
games on **Gambatte**, as armGBA carries it. It is a rewrite, not a recompile - the Android app is Kotlin and Jetpack
Compose, which the 3DS can't run - so this folder holds a small, portable C core that re-implements the companion's
reading and drawing, plus a 3DS host, the emulator's seam (`emu/`) and a desktop simulator around it.

**Status: preview, and this core has not run on a console yet.** Until 2026-11 the port ran libmgba, an interpreter on
the 3DS: full speed in the retail games on a New 3DS, 43-59 FPS in Unbound. armGBA's recompiler is why it was replaced.
With armGBA the build has been run in the headless simulator (`make sim-test`: the same core with its x86-64
recompiler) and in the Azahar emulator (the real `.3dsx`, with the ARM recompiler, both render threads and the sound
thread): Emerald and Yellow boot real saves to the field with the companion, the HUD and the minimap up, save in the
game, and continue from that save. Azahar shows that it works, not how fast: **speed, sound and the screen on a real
New 3DS are still to be measured** (armGBA-3DS's own README has its numbers for two other games). No DS games (the 3DS
can't run melonDS), and no original 3DS / 3DS XL / 2DS: the app says so there and stops.

## What works

- Game list (`sdmc:/pokedaisy/roms/`: `.gba`, `.gb`, `.gbc`), D-pad or stylus.
- The game on the top screen, with sound (ndsp), in three SCREEN modes:
  **PIXEL 1x** (pixel for pixel, centred), **SHARP 1.5x** (fills the height;
  the default) and **STRETCH** (fills the screen). The smooth two use mGBA's
  own "sharp bilinear" look: the frame at 2x nearest, sampled linearly.
- **Fast-forward**: the ▶▶ chip on the companion toggles it, ZR (New 3DS) holds
  it; 2x / 3x / 4x in SETTINGS. The game is muted meanwhile.
- **Save state**: `sdmc:/pokedaisy/states/`. A state is armGBA's (or Gambatte's) own, under a header that names this
  core and the game. The cartridge's save memory stays as it is through a load, so loading a state can't write an old
  copy over the save file; the save is backed up before every load anyway. **A state made by the mGBA build doesn't
  load**: it is refused with a notice and left where it is (a SAVE over that slot keeps it for UNDO, as any SAVE does).
- Saves next to the ROM (`<rom>.sav`), in the layout mGBA wrote: the cartridge's save memory, then its clock where it
  has one (16 bytes for a GBA cartridge, 48 for a Game Boy MBC3). A save from the mGBA build loads here and one written
  here loads in mGBA (checked both ways on Emerald and Yellow). It is written to a new file first, then put in the old
  one's place, once the game's writes have stopped for 15 frames, and as a game is left. A backup of the save is made
  before every start in `sdmc:/pokedaisy/roms/pokedaisy-backups/` (newest 10 per game) - the repo's "save files are
  sacred" rule.
- `sdmc:/pokedaisy/log.txt` says what the last run did (`log-prev.txt`: the run before): the memory it had, whether the
  recompiler and its threads started, each save read and written. Attach it to a bug report.
- An optional `sdmc:/pokedaisy/system/gba_bios.bin` is used if it's there; without it armGBA runs its own BIOS.
- Settings kept in `sdmc:/pokedaisy/settings.ini`.
- The companion, in the app's FireRed OPTION-screen look:
  - **PARTY**: two columns of three across the whole width, each card the game's own where
    the app has its art: its own height and paddings, widened only, shown as big as three
    rows allow (4/3 or 5/4 in sharp bilinear like the game on the top screen, 1x under the
    battle pane's bar). A scaled card's text is drawn over it at whole pixels in Pixel Operator.
    `pd_party_art` rebuilds the frames, Poke Ball, status icons, FONT_SMALL and the
    backdrop from your ROM, by RomArt's fingerprints. Any other game keeps the slot in its
    palette (`PartySlot` / `SlotGeometry`), edge to edge like
    `PartyGrid`; tap one for its summary (moves, type, PP).
  - **BATTLE**: opens by itself in a battle and goes back after it. **INFO**:
    the foe and you (with the game's own party icons, read from your ROM),
    your moves with the app's verdicts (SUPER 2x / NEUTRAL 1x / RESISTED 1/2x /
    IMMUNE 0x / STATUS). **SUGGESTIONS**: your party's three best picks against
    the foe, each with its best move and verdict (OUT on the one already in
    battle) - the app's ranking. In a trainer battle the bar under both carries
    the **FOE TEAM**: the trainer's party in order, Poké Balls until sent out
    or announced, fainted ones faded; tap one to see it (NEXT / FAINTED /
    RESERVE / UNSEEN) and have INFO and SUGGESTIONS work against it, tap it
    again to follow the battle.
  - **BAG**: one pocket at a time in the game's own order and colours (FireRed's
    or Emerald's bag), item names and counts as the game prints them, a list a
    drag scrolls, the tapped item's description.
  - **MAP**: the game's own region map, rebuilt from your ROM (the app's
    `RomArt`: found by fingerprint, nothing bundled) - Kanto and the Sevii
    pages, or Hoenn - with the player's head where the game puts it, the place
    name in the game's own label, and FireRed's blinking cursor on a tapped
    tile. PLACES lists every town, route and place to jump to.
  - **GUIDE**: the app's hand-written pages (TIPS / WHERE IS / STUCK?, with the
    save's progress checked off), hint first, then the answer. HERE is the
    current area: its to-dos, wild POKéMON (from the ROM, with odds), gifts,
    trades and items (a ball = done). BOSS is the next unbeaten gym leader /
    Elite Four / champion with their team from the ROM. A game with no WHERE IS
    of its own gets one built from its area data: HMs, key items, gift POKéMON.
    Red, Blue, Yellow in five languages, Yellow Legacy, Crystal and Crystal Legacy
    have it from the app's generated guide tables (`core/pd_guide_gen.c`,
    `tools/gen_guide.py`), Polished Crystal from its own world (`core/pd_gb_world_gen.c`,
    `tools/gen_gb_world.py`): HERE follows the game's clock and, on Polished Crystal,
    your badges (Crystal Legacy's three any-order gyms too); the champion's teams show
    side by side until the save settles one. A first-open notice says the guide is
    AI-written and may be wrong, as in the app.
  - **POKéDEX**: seen / caught from the save, in Kanto / Hoenn or National
    order (the save's own, or the other with a tap); an entry has the game's
    picture, category, types, height / weight, text and base stats. On every Game
    Boy game too, one list each, read from the cart (`core/pd_gb_dex.c`: Gen 1's
    sprite compression, Gen 2's LZ, Crystal Clear's dictionary, Prism's packed text,
    Polished Crystal's Huffman). `make gb-check EXPORT=<a gameExport>` holds every
    page, picture and wild list of every cart against the app's own reading.
  - **SETTINGS** (the gear): place, money, file and speed, then SCREEN and
    FAST-FORWARD (pick-lists), TAB BAR (which tabs sit on the bar, at most five;
    the rest open from a MORE chip), SAVE STATE / LOAD STATE / LEAVE GAME
    (confirmed). The list scrolls (a drag) where it doesn't fit.
  - **TRAINER CARD** (the button in SETTINGS' title, like the app's TOOLS): the
    game's own card, drawn from your ROM's art pixel for pixel - name, ID,
    money, POKéDEX, play time with its blinking colon, badges, stars, the
    trainer pic; a tap flips it to the back (Hall of Fame debut, link battles,
    trades, FireRed's stickers ...) the way the game does. FireRed rev 1,
    LeafGreen and Emerald, as in the app.
- **Games: what the app knows**, found by SHA-1 (`core/pd_game.c`; configs from `gamedata/`, `core/pd_configs_gen.h`,
  the looks the TOML doesn't hold from the app's export, `core/pd_games_gen.c`): every retail GBA game in every
  language, collection dumps of retail, retail FireRed / Emerald as Universal Pokemon Randomizer ZX leaves them (any
  seed: `core/pd_randomized.c`, what the randomizer changed read from the ROM), the GBA hacks, and the Game Boy games
  (Red, Blue, Yellow in five languages, Yellow Legacy, Gold, Silver, Crystal, Crystal Clear, Crystal Legacy, Prism,
  Polished Crystal, PureRed / PureBlue). Per game: its names, moves, types and type chart, party and bag palettes,
  backdrop, and the companion's windows in the Game Boy games' black on white. No DS: the 3DS can't run melonDS.
- **Library**: the app's rail, view pills and tiles.
- **Battle controls**: FIGHT / BAG / RUN / POKéMON, the moves, the party to send in (BEST / OUT), ONE-TAP MOVES,
  on the GBA games with their handlers known and on every Game Boy game (its 2x2 menu in the cart's own words).
- **QUICK MENU** (ZL + Y): PC, FLY, REPEL, BIKE, WILD, states, FF. The Game Boy games have FLY and WILD and Crystal's PC
  (`GbField`); Yellow's PC from anywhere answers "You can't do that now" (see "Not here yet").
- The **HUD** (party, minimap, balls, Repel steps) over the game's picture on the field.
- **BOXES**: the PC's boxes live, in FireRed's and Emerald's colours and wallpapers; encrypted, plain and CFRU
  storage. WITHDRAW, DEPOSIT and SWAP through the game's own functions where the app has them.
- **USE / GIVE / TAKE**, the summary's STATS, the bag's SORT, **DEX** sections (INFO, EVOLVE, AREA, MOVES on
  vanilla's tables), region maps on the hacks (`RomRegionMap`, RomArt's fingerprints, smol) and the Game Boy games'
  town maps, **STATES** with ten slots, thumbnails and UNDO, **DARK MODE**, **THEME**, **FF MODE**, **CHEATS**
  (an mGBA `.cheats` file, `sdmc:/pokedaisy/cheats/<CRC32>.cheats`, one master switch: its enabled sets' GameShark,
  Action Replay and CodeBreaker lines go to armGBA's cheat engine; mGBA's raw `address:value` lines aren't read. Not yet
  tried with a real cheat file).

Not here yet, since the move to armGBA: **EXPAND / ZOOM OUT** (they draw past the game's picture from mGBA's renderer:
each pixel's layer and each line's registers as it was drawn, which armGBA's doesn't hand out; ASPECT is ORIGINAL
whatever `settings.ini` says, and `pk_wide` only tells the scene for the HUD, AUTO-RUN and SMART fast-forward, from the
registers a frame ends with), **Yellow's PC from anywhere** (it stepped the Game Boy's CPU into the game's own code;
Gambatte has no such step), and the native sound mixer (below). Never here: GUIDE's live pages on hacks with their own tables, the hacks'
evolutions and learnsets in DEX, Quetzal v1's BAG4, the game's own font on the Game Boy games, Japanese text
(kana), REWIND, STEADY music, shaders, ACHIEVEMENTS, Nuzlocke, co-op, translated UI, seasonal themes, RAINBOW's
coloured chips, per-cheat switches.

## Sound and speed

armGBA translates the game's code to ARM as it runs (`third_party/armgba/src/cpu/a32/`), draws on the New 3DS's third
and fourth cores and mixes the sound on the fourth; PokeDaisy asks for all three (`pd_emu_host.threaded`), as
armGBA-3DS does. No speed hacks are set: the BIOS intro is skipped and frames are only left undrawn in fast-forward (the
ones nobody sees). The mGBA build ran the game's sound mixer natively (`host/pd_mixer.c`): that is gone, the game's own
mixer runs on the recompiler like the rest of it.

The executable memory the recompiler needs comes from Luma3DS (`svcControlProcessMemory`, or Luma's own call): the
`.3dsx` under the Homebrew Launcher and the `.cia` both ask for it. Where it's refused armGBA falls back to its
interpreter, about twelve times slower, and `log.txt` says so in capitals.

The app's own draw thread (the textures and the wait for the screen's refresh) shares the fourth core with armGBA's
second render worker and its mixer, a priority step below them: a step above, the worker never drew and the game
stopped half a second in (found in Azahar).

Both cores make 32768 samples for a second of the game's time, and the game runs a frame to a screen refresh (59.83 a
second against its own 59.73), so the sound channel plays at 32825 Hz and the DSP resamples it. A game under full
speed can't make its sound in time, so the channel's rate follows how fast the chunks arrive (`audio_follow` in
`ctr/main.c`): the music slows with the game rather than breaking up, down to 85%, and is back at exactly 1 at full
speed. `stat` on the dev link reports `gaps`, `drops` and the `rate`; the test build logs them each second.

## Controls

| | |
|---|---|
| A / B / START / SELECT / L / R / D-pad or circle pad | the GBA's buttons |
| Touch screen | the companion |
| X / Y | turbo A / turbo B: held, the button is pressed 15 times a second (SETTINGS > X / Y BUTTONS: TURBO A / B) |
| ZL + Y (New 3DS) | QUICK MENU (the circle pad aims the ring) |
| ZL + X (New 3DS) | on the field: the buttons go to the companion, a silver strip on the game pointing down at it (L / R step its tabs, the D-pad and A its cursor; X, START, or B with nothing left to go back from bring them back). Elsewhere companion back |
| ZL alone (New 3DS) | previous companion tab, when let go |
| X / Y BUTTONS: MENUS | Y is the QUICK MENU and X the companion button without ZL, and there is no turbo |
| SETTINGS > GB SCREEN | Red and Blue, which run as an original Game Boy: GRAY, GREEN (the first Game Boy's screen), POCKET or LIGHT. The Color games keep their own colours |
| ZR (New 3DS) | fast-forward while held |
| START on the game list | quit to the Homebrew Launcher |

## Installing on a 3DS

Needs a **New 3DS, New 3DS XL or New 2DS XL** with custom firmware (Luma3DS: it lets the recompiler make memory
executable). The original 3DS, 3DS XL and 2DS aren't supported: the app tells you and stops.

1. Copy `pokedaisy.3dsx` to `sdmc:/3ds/` for the Homebrew Launcher, or install `pokedaisy.cia` with FBI for the HOME Menu.
2. Put your own legally dumped `.gba`, `.gb` and `.gbc` files (and any `.sav` with the same name) in
   `sdmc:/pokedaisy/roms/`. Saves from the mGBA build are used as they are.
3. For sound, the DSP firmware must be dumped once (`sdmc:/3ds/dspfirm.cdc`, the DSP1 homebrew does it) - without it
   the game runs silent.

This fork publishes no builds: the steps above are for one you built yourself. For a build to install, see the official
project, [lidor30/pokedaisy](https://github.com/lidor30/pokedaisy). GBA games run on armGBA (the submodule is its
`3ds-v0.0.3-alpha` release) and Game Boy and Game Boy Color games on the Gambatte that armGBA carries; see "Licences"
below for where that leaves a build that is passed on.

## Running it without a 3DS (Azahar)

[Azahar](https://azahar-emu.org) (Citra's successor) runs `.3dsx` homebrew on
a Mac, Windows or Linux.

1. Install Azahar (macOS: the `.dmg` from its releases page). In its settings,
   turn on **Enable New 3DS mode** (System): the app refuses to start without it.
2. Build the app: `cd 3ds && make 3ds` (Docker Desktop running) gives
   `ctr/pokedaisy.3dsx`.
3. **File > Open Azahar Folder**, then in `sdmc/` make `pokedaisy/roms/` and
   put your `.gba` / `.gb` / `.gbc` files there (a `.sav` beside one with the same name is used).
   No ROM handy? `make decomps fixture-roms` makes test ROMs: the
   `build/fixture-*-rom.gba` ones open every tab (the trainer battle is
   `fixture-emerald_trainer-rom.gba`).
4. **File > Load File...** and pick `pokedaisy.3dsx`. The mouse is the stylus
   on the bottom screen.

Azahar's default keys (Emulation > Configure > Controls to change them): A =
`A`, B = `S`, X = `Z` (next companion tab), Y = `X` (companion back), START =
`M`, SELECT = `N`, L / R = `Q` / `W`, ZR = `2` (fast-forward), D-pad =
`T` `F` `G` `H`, circle pad = the arrow keys. Azahar plays sound without the
DSP firmware. It shows that things work, not how fast they'll run on a console.

Nothing can press Azahar's buttons on a headless machine, so there's a **test build**: `make 3ds-test` gives
`ctr/pokedaisy-test.3dsx`, which reads `sdmc:/pokedaisy/autotest.txt` (`ctr/autotest.h`: `launch`, `wait`, `press`,
`tap`, `shot`, `leave`, `quit`), writes its screenshots to `sdmc:/pokedaisy/shots/` and logs its speed and a watchdog's
findings to `log.txt`. Run it as `xvfb-run -a <Azahar> ctr/pokedaisy-test.3dsx` and wait for `autotest done` in the log.
Never a player's build.


## A dev build over Wi-Fi

`make 3ds-dev` builds `ctr/pokedaisy-dev.3dsx`, kept apart from the real one (`sdmc:/pokedaisy-dev/`, title
"PokeDaisy dev"), with a small link (`ctr/devlink.c`, TCP 17498, a UDP beacon on 17499) that `tools/pdlink.py`
talks to: `find`, `ping`, `stat`, `log`, `put` / `push` / `get` / `ls` / `rm` (only under `sdmc:/pokedaisy-dev/`),
`press`, `tap`, `drag`, `shot`, `leave`, `restart`, `launch`. Start it once from the Homebrew Launcher (press Y
there for its netloader and send it with `3dslink`, or copy it to `sdmc:/3ds/`), then `tools/pdlink.py find`. Its port
17491 can take a while to open after Y, so retry `3dslink -a IP`. `push` puts the build in `sdmc:/3ds/` too, and games go
in `roms/` (a new one shows after `restart`). Never in a release build.

## The simulator

`make sim` builds `build/sim/pd_sim` (`desktop/sim/`, plain make: a C and a C++ compiler and zlib): the real session
(`host/`) and companion over the same emulator the console runs - armGBA with its x86-64 recompiler, Gambatte - with no
3DS code, from a script of `run`, `press`, `hold`, `quick`, `tap`, `hit ID`, `hits`, `shot`, `wild`, `wait-state`,
`press-to-state`, `poke`, `peek`, `state`, `expect`, `state-save N`, `state-load N`, `ff N`, `reset`, `audio`, `info`,
`aspect`, `screen`, `dark`, `theme`. `pd_sim ROM SAVE OUTDIR [SCRIPT]`: the save is copied to `OUTDIR/sim.sav` first,
never written where it was (`-` for none, `=` to go on from the `sim.sav` the last run left: a save's round trip).
`PD_SIM_THREADS=1` puts armGBA's renderer and mixer on threads as on the console, `PD_SIM_LOG=1` prints the core's log.
`make sim-test` runs `test/sim_*.txt` on FireRed and Emerald (`ROM_DIR`, `SAVE_DIR`, `SAVE_EXT`), `make sim-test-gb` the
Game Boy script on Yellow. `sim_battle.txt` and the Game Boy script switch Pokémon, so they want a save with a second
one in the party.

## Game data from the app

```sh
cd ui-preview && gradle gameExport -PgameExport -Proms=<ROMs folder> -Pout=<dir>
python3 3ds/tools/gen_games.py <dir>
```

`src/render/GameExport.kt` runs each ROM through the app's own detection (with `-PgameExport` the core is a stub
answering from the file) and writes what it picked: the config, the tables, the palettes, every theme's windows,
which art it wants. `gen_games.py` turns that into `core/pd_games_gen.c`. A new game or a changed table: run both.

## Building and testing

Everything runs in Docker, so it works the same on a Mac or a cloud session.

```sh
cd 3ds
make decomps       # pret's FireRed rev 1 / Emerald, = retail (below; optional)
make preview       # the companion from the RAM fixtures -> build/shots/*.png
make 3ds           # ctr/pokedaisy.3dsx (devkitpro/devkitarm image; git submodule update --init third_party/armgba first)
make 3ds-test      # ctr/pokedaisy-test.3dsx: a script instead of hands, for Azahar (above)
make cia           # ctr/pokedaisy.cia too (makerom and bannertool: see ctr/Makefile)
make sim sim-test  # the simulator and its scripts (above)
make fixture-roms  # build/fixture-*.gba test ROMs (below)
make azahar-test   # the .3dsx in the Azahar 3DS emulator -> build/azahar/*.png
make azahar-battle-test  # BATTLE in Azahar, a trainer battle -> build/azahar-battle/
make tables        # regenerate core/*_gen.c and ctr/icon.png from the app
```

- **`make preview`** is this port's ui-preview: `desktop/pd_preview.c` reads a
  fixture's `ewram.bin` / `iwram.bin` (`app/src/test/resources/fixtures/`) and
  drives the companion with taps found by hit id - a pressed slot, a summary,
  BATTLE, BAG (an item, a drag, the next pocket), SETTINGS, a pick-list, a
  confirm, MAP (a tapped tile, the blink, PLACES), POKéDEX (scrolled, an
  entry, the next, the other dex), GUIDE (the notice, every page, a hint and
  its answer), BATTLE's INFO and SUGGESTIONS, the TRAINER CARD (blinked, mid-flip,
  the back) - drawing each as the 3DS's two screens. It also renders each card
  whole and prints the CRC32 `TrainerCardTest` pins: FireRed's `d62339db` /
  `045996a2` and Emerald's `f3e14960` / `74484c21`, the cards that matched the
  games' own screens pixel for pixel, come out the same here. There's no retail trainer-battle fixture, so the preview makes one
  from the wild battle (`emerald_de_battle`): three of the player's Pokémon
  copied into gEnemyParty beside the wild one, the trainer flag set; then the
  first faints and the trainer's next pick is set, and the last slot is
  tapped (`-trainer*` shots). That RAM is kept as
  `build/fixtures/emerald_trainer` for `make fixture-roms`. No emulator: look at
  every UI change here. `PD_PREVIEW_HITS=1` also prints each shot's tap
  targets, for writing the Azahar steps.
- **`make decomps`** (`test/build_decomps.sh`; git, a C compiler, libpng and
  devkitARM) builds pret's pokefirered (rev 1) and pokeemerald at pinned
  commits into `build/decomps/` and checks they match the retail SHA1s. The
  tabs that read the ROM need one; the preview passes them in when they're
  there (or `make preview FR_ROM=... EM_ROM=...`). Never commit them.
- **Fixture ROMs** (`test/fixture_rom.c`) are a few lines of GBA homebrew that
  copy a fixture's RAM into the GBA and idle, with FireRed's or Emerald's game
  code in the header. The companion can't tell them from the real game, so the
  whole app can be tested without a Pokémon ROM. They hold no game code or
  data, only the fixture's save state. With the decomps built there's also
  `build/fixture-*-rom.gba`: the same written over the decomp's ROM (its
  first ~300 KB, game code the loader never runs), so MAP / POKéDEX / GUIDE
  find the game's tables and art - derived from a game ROM, so build/ only.
- **`make azahar-test`** boots the real `.3dsx` in Azahar (the
  `linuxserver/azahar` image, on Xvfb) on the FireRed fixture ROM and taps
  through it like a player: game list, PARTY, BAG, SETTINGS, each SCREEN mode,
  fast-forward, a save state and loading it, MAP, PLACES, POKéDEX, GUIDE (on
  the `-rom` fixture when it's built) - screenshots in `build/azahar/`,
  and the emulated SD card (states, backups, settings) in `build/azahar/sd/`.
  Azahar shows that it works, not how fast: speed has to be measured on a
  console. **`make azahar-battle-test`** does BATTLE the same way on the
  trainer battle: INFO with the FOE TEAM, SUGGESTIONS, foes tapped.

## Layout

| Path | What |
|---|---|
| `core/` | Portable C, no platform code: game detection (`pd_game`, the app's `NativeConfig` addresses), the snapshot reader (`pd_snapshot`: party, battle, bag, money, place - ports `Gen3Mon.kt` / `readNativeTelemetry` / `readNativeBag`), the canvas (`pd_canvas`: pixel-stepped corners, layered OPTION frames, GBA-shadowed and wrapped text, drawn cursors), the companion (`pd_ui.c` frame + `pd_ui_party` / `_battle` (INFO, SUGGESTIONS, FOE TEAM) / `_bag` / `_map` / `_guide` / `_dex` / `_settings` / `_widgets`), the ROM readers (`pd_romart`: `RomArt`'s scan - every blob of the game in one pass; `pd_party_art`: the party's slot art and backdrop; `pd_map`: the region map and heads, `RegionMapModel`'s pick, `PlayerMapTile`; `pd_dex`: the dex tables and front pics; `pd_guide`: `GuideRom.kt`'s bosses, teams and wild tables), the party icons (`pd_icon`: `DecompIconSource`'s tables), the TRAINER CARD (`pd_card`: `readTrainerCard` + `TrainerCardArt`; `pd_ui_card`), the game list (`pd_menu`) and the settings file (`pd_settings`) |
| `core/*_gen.c` | Generated: names / moves / type chart / map sections / item text / gender ratios from the app's Kotlin tables (`tools/gen_tables.py`), the ROM art's fingerprints (map and card) and the cursor grids (`tools/gen_map_tables.py`), the guides and area data (`tools/gen_guide.py`), Pixel Operator as a 1-bit font plus drawn ♂ ♀ (`tools/gen_font.py`) |
| `ctr/` | The 3DS host: `main.c` (game loop, ndsp audio, HID, files), `gpu.c` (citro2d: the game and canvases written into textures in the GPU's tiled order, drawn as quads), `host.c` (the log, the memory split, what armGBA's own 3DS host code asks a frontend), `autotest.c` (the test build's script), and the Makefile that builds it all with armGBA's sources and its `platform/3ds/source/jitmem.c` and `svc.s` (executable memory, cache flushes, the render and sound threads) |
| `emu/` | The emulator's seam. `pd_emu.c` drives armGBA and Gambatte through armGBA's libretro layer, keeps the save file and the states, and answers `struct mCore`; `pd_emu_call.c` calls the game's own functions on armGBA's interpreter (`pk_call`). `include/` holds the port's own headers under mGBA's names - a `struct mCore` with the members PokeDaisy's shared C uses, `struct GBA` / `struct GB` as views into the running machine - so the files shared with the mGBA hosts (`linux/fly.c`, `field_items.c`, `gb_field.c`, `app/src/main/cpp/pk_wide*.c`) build unchanged. `armgba.mk` lists armGBA's sources for both builds |
| `host/` | What a running game needs on any host: the session (`pd_session`), USE / GIVE / TAKE and the PC's moves (`pd_items`), the Game Boy games' field actions through the Linux host's `linux/gb_field.c` |
| `desktop/` | The preview tool, its small PNG writer and the simulator (`sim/`) |
| `test/` | The fixture ROM, the decomp build and the Azahar script |
| `tools/` | The generators |

Keep in sync with the app: `core/pd_configs_gen.h`'s addresses are generated from
`gamedata/` with the app's (`make gamedata`; `gamedata/targets/3ds.toml` picks what
this port reads), `pd_snapshot.c` mirrors `Gen3Mon.kt` / `readNativeTelemetry`,
and the tables are regenerated (`make tables`) whenever the app's change.

GPU notes (found in Azahar): textures are written by the CPU straight into
linear memory in the tiled order, top row first (v = 1). Display transfers into
textures came out upside down, and into VRAM textures rotated, so they're
not used; nor is render-to-texture.

## Hardware

- **New 3DS / New 3DS XL / New 2DS XL** (804 MHz, four cores, 256 MB): the only target. The app turns on the faster
  clock (`osSetSpeedupEnable`), armGBA's renderer and mixer take the third and fourth cores, and the heap gets all the
  memory but 8 MiB of linear (`ctr/host.c`): the game (up to 32 MiB), the recompiler's code (up to 24) and the
  machine's copies for its threads live there.
- **3DS / 3DS XL / 2DS**: not supported. The recompiler's threads have no cores there and one 268 MHz core can't hold
  full speed; the app shows "PokeDaisy needs a New 3DS" and closes.

## Licences

PokeDaisy is GPL-3.0 (`NOTICE`). armGBA's core, its libretro layer and its 3DS host code are MPL-2.0, which may be
combined with GPL-3.0 code and conveyed under it. **Gambatte is GPL-2.0 only** (every source file says "version 2 as
published", none "or later"), and GPL-2.0-only code can't be conveyed in one program with GPL-3.0 code. So a build
that holds Gambatte - the default, with Game Boy and Game Boy Color games - is outside what either licence grants once
it is passed on. This fork's two alpha releases were withdrawn on 2026-11-12 and it publishes no builds. `NOTICE` has
the details and what would settle it.

## Next steps

- Run it on a New 3DS: the frame time per game and screen mode (Unbound first: it is why the core changed), sound, the
  draw thread beside armGBA's workers, memory under the Homebrew Launcher and as a `.cia`, save writes on a real SD card.
- EXPAND on armGBA: its renderer would have to hand out each pixel's layer and each line's registers (a change in
  armGBA itself).
- A Game Boy core that can be distributed with the app, or Gambatte's step for Yellow's PC.
