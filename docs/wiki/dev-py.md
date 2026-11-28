---
title: tools/dev.py
sources:
  - tools/dev.py
  - .env.example
  - tools/switch/nxlink.py
  - tools/devenv.py
verified: 409b3d28
---

# `tools/dev.py`: build and run

One command for the build -> run loop: Docker builds, sending a build to the console over
nxlink, the Switch emulator, the N64 reference emulator, and the native Linux / Windows builds.
Use it instead of hand-written `docker run` or `nxlink.py` command lines.

```sh
python tools/dev.py build  {switch|args|link|linux|windows}
python tools/dev.py nx     [--build link|args|switch|none] [--name N] [--installed] [--level L] [--hard N] [--secs S] [ARGS...]
python tools/dev.py switch [--build args|switch|none] [--level L] [--hard N] [ARGS...]
python tools/dev.py n64
python tools/dev.py win    [--build] [--level L] [--hard N] [ARGS...]
python tools/dev.py linux  [--build] [--level L] [--hard N] [ARGS...]
python tools/dev.py editor [--build] [--level L] [--mod DIR] [--mission FILE] [ARGS...]
python tools/dev.py config
python tools/dev.py tools [NAME [ARGS...]]
```

`-h` works on every command.

## Setup

Machine paths are never in the script: they come from `<repo>/.env` (gitignored; copy
`.env.example`). A worktree has no `.env` of its own and uses the main checkout's. Real
environment variables override the file.

| Variable | Used by |
|---|---|
| `SWITCH_IP` | `nx`: the console's address on the local network |
| `SWITCH_EMU_EXE` | `switch`: the Switch emulator |
| `SWITCH_EMU_SD` | `switch`: its virtual SD card (default `%APPDATA%/Ryujinx/sdcard`) |
| `N64_EMU_EXE`, `N64_ROM_US` (`N64_ROM_JP`, `N64_ROM_PAL` reserved; `N64_ROM` still read) | `n64`; `N64_ROM_US` also seeds the native builds' `data/` and is the ROM every tool reads |
| `SEAVIEW_GXX` | `tools`: the Windows g++ the viewers build with (default: MSYS2's `C:\msys64\mingw64in\g++.exe`) |
| `DOCKER_IMAGE` | builds (default: the published, digest-pinned `ghcr.io/christphe/project-dam-build`, pulled on first use; `local/project-dam-build:latest` after `docker compose build image-local`) |

`python tools/dev.py config` prints what it resolved.

## Build targets

| Target | Directory | What it is |
|---|---|---|
| `switch` | `build-switch` | The console build, what CI ships. |
| `args` | `build-switch-args` | `-DNX_ARGS=ON`: reads its arguments from `data/ge007.args`. For the emulator. |
| `link` | `build-switch-link` | `-DNX_LINKSTDIO=ON -DNX_ARGS=ON`: stdout and the crash dump come back over nxlink. For the console. |
| `linux` | `build-linux` | `docker compose run --rm linux` |
| `windows` | `build-win` | `docker compose run --rm windows` |

The Switch builds run in the Docker image with a shared ccache volume. A build directory is
configured once; later builds re-run CMake so new source files are picked up.

## Game arguments

Every run command takes:

- `--level L`: boot straight into a solo level, by number or by name (`dam`, `facility`,
  `runway`, … `cuba`; `-h` lists them). Becomes `-level_NN`.
- `--hard N`: difficulty 0..3 (`-hardN`).
- `--env NAME=value` (repeatable): an environment variable for the game (`--env GE_COOP=1 --env GE_PADS=2`,
  `--env GE_QUITFRAME=300`…). `win` and `linux` set it in the game's environment (through `WSLENV` for `linux`
  on Windows). On the Switch the game reads them as `NAME=value` tokens, which `nx` and `switch` write for you;
  only the `args` and `link` builds do that (the plain `switch` build ignores them).
- `-- ARGS...`: everything after `--` goes to the game as it is (`-- -level_09`). An unknown option before
  `--` is an error.


## Commands

**`nx`** builds (default `link`) and sends the NRO to the console over nxlink. The game starts
at once; with a `link` build its output is written to `<build dir>/nx.log` (or `--log FILE`)
and a crash dump comes back too.

- It lands at `sdmc:/switch/project-dam/project-dam-test.nro`, next to the installed game,
  which it does not replace. `--installed` sends it over `project-dam.nro` instead (to test the
  in-game update).
- `--name foo` sends a copy named `foo-test.nro`, to tell test builds apart.
- `--secs S` quits after S seconds (`NX_TEST_SECS`).
- `--build none` resends the last build.
- The console must be awake with the netloader listening; one client at a time. On a non-link
  build, the timeout after the upload is expected (nothing sends output back).
- Details of the protocol: [`docs/switch/nxlink-stdio.md`](../switch/nxlink-stdio.md); test arguments and logs: [testing-on-switch.md](testing-on-switch.md).

**`switch`** builds (default `args`), copies the NRO to the emulator's SD card as
`switch/project-dam/project-dam.nro`, writes the arguments to `data/ge007.args` (the first time
it keeps a `.args.bak` of the one already there) and starts the emulator. The game's log is
`switch/project-dam/data/ge007.log` on that SD card.

**`win` / `linux`** run the native build (`--build` rebuilds it first). The first run in a new
worktree creates `data/` next to the executable and copies the ROM into it (from `N64_ROM_US`, or
the one the Switch emulator already has). Every run also copies `campaign/` to `mods/campaign.pmod/` next to the
executable, as a folder (a `.pmod` may be a plain directory); `--no-campaign` skips it. `--env NAME=value` (repeatable) sets an environment variable of the game; everything after `--` goes to the game
as it is: `dev.py win --env GE_QUITFRAME=60 -- -level_09`. It reports a game that exits within three seconds
instead of failing silently. `linux` on Windows runs through WSL.

**`editor`** runs the mission editor of the Windows build (`build-win/project-dam-editor.exe`)
with the game next to it for Play and the level exports (`--build` rebuilds the `windows`
target first). `--level` opens a solo level, `--mod` a mod's source folder, `--mission` a
`mission.json`; other arguments go to the editor.

**`n64`** opens the ROM in the N64 emulator, for reference captures.

**`tools`** runs the dev tools from the repo root. With no name it lists them and what each still
needs (g++, ROM, a converted `data/`). `tools NAME [ARGS...]` runs one:

| Name | What | Default subcommand |
|---|---|---|
| `modelview` | characters from the export (body + head pickers): subdivision, animations | `pc` (also `export`, `selftest`, `shots`) |
| `fxview` | combat effects, and the GE+ sky/sea/water (`--mode sea`, `water`, `sky`) | `win` (also `ryujinx`, `nx`, `selftest`, `shots`, `decalshots`, `tabshots`) |
| `menuview` | the options menu on its own | none (`--build` builds only) |
| `verify` | `tools_pc/verify.sh`: headless capture vs the golden frames | none |
| any other | `tools_pc/<name>.py` or `.sh` (`disasm`, `d88_propdef_scan`, …) | none |

The viewers rebuild themselves when a source changed. Arguments that are not a subcommand go to
the default one: `tools modelview --model CtrevelyanZ` is `modelview.py pc --model CtrevelyanZ`.
Every tool finds the ROM the same way (`tools/devenv.py`): its own key if it has one
(`MODELVIEW_ROM`, `FXVIEW_ROM`), then `N64_ROM`, then a game `data/` folder (`build-win`,
`build-pc`, `build-linux`, `data`, in this checkout then the main one), then the Switch emulator's
SD card. The `.sh` tools run under Git's or MSYS2's bash and get `GE_DATA`, a `data/` the game has
already converted, to copy into their per-run work dir.

## Examples

```sh
python tools/dev.py nx --level facility                    # console, Facility, log in build-switch-link/nx.log
python tools/dev.py nx --name coop --env GE_COOP=1 --env GE_PADS=2     # co-op test build, named coop-test.nro
python tools/dev.py switch --level dam --hard 3            # emulator, Dam on 00 Agent
python tools/dev.py switch --build none GE_MENUGOTO=2,0    # emulator, last build, menu open on Input
python tools/dev.py win --build                            # rebuild and run the Windows build
python tools/dev.py tools                                  # list the dev tools and what they need
python tools/dev.py tools modelview                        # build if needed and open the model viewer
python tools/dev.py tools disasm 7F000000 20               # any tools_pc script, ROM from .env
```
