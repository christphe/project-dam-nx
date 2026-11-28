---
title: Testing on the Switch
sources:
  - tools/dev.py
  - tools/switch/nxlink.py
  - tools/switch/make_sd.sh
  - port/src/config.c
  - port/src/input_script.c
  - port/src/coop.c
  - port/src/missions_mods.c
  - cmake/SwitchPort.cmake
verified: e2fda9e2
---

# Testing on the Switch

The three ways to run a build, what each is good for, and the arguments that put the game in the
state you want to look at. Commands go through [dev.py](dev-py.md).

## Where to run it

| | Command | Good for | Not for |
|---|---|---|---|
| **Emulator** | `dev.py switch` | Menus, logic, screenshots, anything a script can drive. | Performance, feel, the exact look of the sea / sky (judge those on the console). |
| **Console over nxlink** | `dev.py nx` | Performance, feel, crashes: the log and crash dump come back to the PC. | Runs you need to start with buttons on a second pad you don't have. |
| **Console from the SD card** | `dev.py build switch`, then `tools/switch/make_sd.sh` | What a player gets: the release build, the update path. | Iterating (no log comes back). |

- The emulator reads its arguments from `data/ge007.args` on its SD card (`args` build). The console
  gets them on the nxlink command line (`link` build). The plain `switch` build reads none: never
  ship an `args` or `link` NRO.
- nxlink sends to `sdmc:/switch/project-dam/project-dam-test.nro`, beside the installed game.
  The console must be awake with the netloader up; one client at a time. Keep the client running
  for the whole test: the log arrives through it.
- A game window closed by hand shows up in the log as "quit requested (window closed)": not a bug.

## Arguments

| Argument | Effect |
|---|---|
| `--level L` / `-level_NN` | Boot straight into a solo level. |
| `--hard N` / `-hardN` | Difficulty 0..3. |
| `--secs S` / `NX_TEST_SECS=S` | Quit after S seconds. |
| `GE_CFG="Key=value;Key2=value2"` | Override ini values for this run; the ini is not written back. |
| `GE_COOP=1..4`, `GE_PADS=N` | Co-op without the menu; force N controller seats. Always together ([coop.md](coop.md)). |
| `GE_STARTWEAPON=N` | Start with that item (8 = KF7). |
| `GE_INPUTSCRIPT="frame:BUTTONS;..."` | Scripted input for player 1 (`GE_INPUTSCRIPT_P1..P3` for the others), counted in controller reads: `120:START;180:A`, or `300:ZHOLD` to hold fire from 300. Token list: the header of `port/src/input_script.c`. |
| `GE_ROM_TEXTURES=1` | Draw every texture from the ROM's own decode; the asset-pack stack (mod and packs) is bypassed. Without it a texture no mod or pack has is a magenta/black checker (log: `missing texture N`), never the ROM's bytes. |
| `GE_ROM_SOUNDS=1` | Sound effects from the ROM's sound bank instead of the asset packs' `sounds/sfx/` (`port/src/sounds.c`). Without it a stack with no `sounds/sfx/sfx.json` plays silence (one warning in the log); this flag stays until the ROM bank is removed. |
| `GE_ROM_MUSIC=1` | Music from the ROM's sequence player instead of the asset packs' `music/track_NN.mid` + `music/instruments.sf2` (TinySoundFont, `port/src/musicpack.c`). Without it a missing MIDI or SoundFont plays silence (one warning in the log); this flag stays until the ROM player is removed. `GE_MUSICLOG=1` logs every music request (slot, track, volume, fade) with the retrace count, in both paths. |
| `GE_MENUGOTO=tab,entry[,focus]` | Open the options menu on that page ([options-menu.md](options-menu.md)). |
| `GE_ROM_LABELS=1` | Dev flag, until the ROM banks are removed: read the game's text from the ROM's language banks. Without it the text comes only from the asset packs' `labels/text_<lang>.json`; a missing file shows every string as `[0x....]` and logs one warning (`port/src/labels.c`). |
| `GE_LABELS_CHECK=1` | Compare every language bank the game loads with the pack's strings and log `labels check:` lines with the counts (same / differ / only ROM / only pack). |
| `GE_ROM_FONTS=1` | Dev flag, until the ROM path is removed: read the fonts from the ROM. Without it the fonts come only from the asset packs and the mod ([fonts.md](../specs/fonts.md)); a font in none of them is empty (nothing drawn, one warning in the log), the ROM is not read. `GE_FONTS_CHECK=1` compares pack and ROM at the first font load and logs the counts (dev). |
| `GE_ROM_LEVELS=1` | Dev flag, until the ROM path is removed: draw the level's rooms from the ROM's display lists. Without it the decor comes only from the asset packs' `levels/<level>/<level>.gltf` (`port/src/leveldecor.c`); a level or room the stack does not have is not drawn (one warning in the log), the ROM's lists are not used. The game still picks the visible rooms, and the collision and setups stay on the ROM. |

Every `GE_*` probe, with its file and status: `docs/dev/GE-ENV-PROBES.md`. `python
tools_pc/gen_env_probes.py` reports the probes it misses and today's file:line of each one.

## Mods in an nxlink build (romfs)

nxlink sends one file: the NRO. To try mods on the console without copying them to the SD card, pack them
into the NRO's romfs. The game also scans `romfs:/mods/` (Switch only, after the SD card's `mods/`: a mod of
the same name on the SD card wins).

```sh
cd <repo>
mkdir -p build-switch-link/romfs/mods
# one ustar per mod, from inside its source folder (no README, no dist/)
tar -cf build-switch-link/romfs/mods/michel.pmod -C <repos>/project-dam-mods/michel --exclude=.gitignore --exclude=README.md --exclude=dist .
tar -cf build-switch-link/romfs/mods/formula.pmod -C docs/modding/samples/formula .
python tools/dev.py build link        # once, if build-switch-link is not configured yet
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -v project-dam-ccache:/ccache -w /src "$(python tools/check_build_image.py --print)" \
  sh -c 'cmake build-switch-link -DNX_ROMFS_DIR=/src/build-switch-link/romfs && cmake --build build-switch-link'
python tools/switch/nxlink.py <switch ip> build-switch-link/ge007.aarch64.nro switch/project-dam/project-dam-test.nro \
  build-switch-link/nx.log NX_TEST_SECS=86400
```

- The build prints `Writing .../romfs/mods/<mod>.pmod to RomFS image...` for each mod it packs.
- Repack the `.pmod` and rebuild after every change to a mod: the romfs is baked into the NRO.
- `NX_ROMFS_DIR` stays in `build-switch-link`'s CMake cache. Clear it (`cmake build-switch-link -DNX_ROMFS_DIR=`)
  before a build that must not carry the mods. The release (`switch`) build never sets it.
- Append `GE_*` arguments after the log path, as for any nxlink run (`GE_FX=0`, `GE_CFG=...`).

## Logs

- Emulator: `switch/project-dam/data/ge007.log` on its SD card (`ge007.prev.log` is the run before).
- Console over nxlink: `<build dir>/nx.log` on the PC.

## A bug at a precise spot

Input record / replay (`GE_INPUTRECORD` / `GE_INPUTREPLAY`) diverges under the emulator, so it does
not bring you back to a spot. What works: log the player position once while someone walks there,
then start each run at that position with a temporary local teleport hook (not committed: it is a
game-code change), plus `GE_STARTWEAPON` and `GE_INPUTSCRIPT` for the action. Nobody has to replay
the level for each attempt.
