#!/usr/bin/env python
"""Dev launcher: build, nxlink to the console, Switch emulator, N64 emulator, native Linux/Windows.

    python tools/dev.py build  {switch|args|link|linux|windows}
    python tools/dev.py nx     [--build link|args|switch|none] [--name N] [--level N] [--hard N] [--secs S] [ARGS...]
    python tools/dev.py switch [--build args|switch|none] [--level N] [--hard N] [ARGS...]
    python tools/dev.py n64
    python tools/dev.py win    [--build] [ARGS...]     # build-win/ge007.x86_64.exe
    python tools/dev.py linux  [--build] [ARGS...]     # build-linux/ge007.x86_64 (WSL if on Windows)
    python tools/dev.py editor [--build] [--level N] [--mod DIR] [--mission FILE] [ARGS...]   # build-win/project-dam-editor.exe
    python tools/dev.py config                         # show the resolved config
    python tools/dev.py tools                          # the dev tools, and what each still needs (g++, ROM)
    python tools/dev.py tools modelview [ARGS...]      # build if a source changed, then run (also fxview, menuview,
                                                       # verify, or any tools_pc/<name>.py|.sh)

Machine-specific paths are NOT in this file: they come from <repo>/.env, or the main checkout's when run from a worktree (gitignored):
SWITCH_IP, SWITCH_EMU_EXE, SWITCH_EMU_SD (default %APPDATA%/Ryujinx/sdcard),
N64_EMU_EXE, N64_ROM_US / N64_ROM_JP / N64_ROM_PAL (the .z64 to open), DOCKER_IMAGE (optional). The same lookup serves the tools_pc viewers and
scripts (tools/devenv.py): they find the ROM through N64_ROM, else a game data/ folder.
"""
import argparse, json, os, shutil, subprocess, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devenv   # .env (the main checkout's from a worktree), ROM and g++ lookup, shared with tools_pc

REPO = Path(__file__).resolve().parents[1]
HERE = Path(__file__).resolve().parent
KEYS = {"console_ip": "SWITCH_IP", "ryujinx_exe": "SWITCH_EMU_EXE", "ryujinx_sd": "SWITCH_EMU_SD",
        "ares_exe": "N64_EMU_EXE", "ares_rom": "N64_ROM_US", "image": "DOCKER_IMAGE"}
IMAGE = cfg_image = "ghcr.io/christphe/project-dam-build@sha256:7ba869fa0ea8b7195b7dcab3b23f87ae2e3065fac1001bd89ca817771dcc7baf"   # the published build image (tools/check_build_image.py keeps it in sync)
SD_DIR = "switch/project-dam"          # game folder on the SD card
NX_DIR = "project-dam"                 # the same folder for nxlink: the netloader writes under sdmc:/switch/
NRO = "ge007.aarch64.nro"

# build kind -> (build dir, cmake configure extras, output)
BUILDS = {
    "switch": ("build-switch", []),
    "args":   ("build-switch-args", ["-DNX_ARGS=ON"]),
    # NX_ARGS too: the NAME=value tokens (--secs, env) are only read with it
    "link":   ("build-switch-link", ["-DNX_LINKSTDIO=ON", "-DNX_ARGS=ON"]),
}
SWITCH_TC = "-DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake"


# solo levels, same name:number pairs as port/src/main.c (kSoloLevels)
LEVELS = {"dam": 33, "facility": 34, "runway": 35, "surface1": 36, "bunker1": 9, "silo": 20, "frigate": 26,
          "surface2": 43, "bunker2": 27, "statue": 22, "archives": 24, "streets": 29, "depot": 30, "train": 25,
          "jungle": 37, "control": 23, "caverns": 39, "cradle": 41, "aztec": 28, "egypt": 32, "cuba": 54}
LEVELS_HELP = ", ".join(f"{n.capitalize()}={v}" for n, v in LEVELS.items())


def level_arg(text):
    """--level takes a number or a level name (case-insensitive)."""
    t = text.strip().lower()
    if t in LEVELS:
        return LEVELS[t]
    if t.isdigit():
        return int(t)
    raise argparse.ArgumentTypeError(f"unknown level '{text}' (names: {', '.join(LEVELS)})")


def cfg():
    """Config from <repo>/.env, or the main checkout's when run from a worktree (gitignored) overridden by the real environment."""
    env = devenv.load_env()
    c = {name: env[var] for name, var in KEYS.items() if env.get(var)}
    if "ryujinx_sd" not in c and os.environ.get("APPDATA"):
        c["ryujinx_sd"] = str(Path(os.environ["APPDATA"]) / "Ryujinx" / "sdcard")
    return c


def need(c, key):
    if not c.get(key):
        sys.exit(f"missing '{key}': set the matching variable in <repo>/.env (SWITCH_IP, SWITCH_EMU_EXE, SWITCH_EMU_SD, N64_EMU_EXE, N64_ROM_US)")
    return c[key]


def run(cmd, **kw):
    print("+", " ".join(map(str, cmd)), flush=True)
    r = subprocess.run(cmd, **kw)
    if r.returncode:
        sys.exit(f"command failed ({r.returncode})")


def docker(shell_cmd):
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    run(["docker", "run", "--rm", "-v", f"{REPO}:/src", "-v", "project-dam-ccache:/ccache",
         "-w", "/src", cfg().get("image", IMAGE), "sh", "-c", shell_cmd], env=env)


def build(kind):
    if kind in BUILDS:
        d, extra = BUILDS[kind]
        conf = ""
        # configure once: the source GLOBs are CONFIGURE_DEPENDS, so ninja itself re-runs cmake when a file
        # is added or removed. Re-running cmake on every build rewrote generated files and cost ~4 s.
        if not (REPO / d / "build.ninja").exists():
            conf = (f"cmake -S . -B {d} -G Ninja -DROMID=ntsc-final {SWITCH_TC} "
                    + " ".join(extra) + " && ")
        docker(conf + f"cmake --build {d}")
        return REPO / d / NRO
    if kind == "windows":
        run(["docker", "compose", "run", "--rm", "windows"], cwd=REPO)
        return REPO / "build-win" / "ge007.x86_64.exe"
    if kind == "linux":
        run(["docker", "compose", "run", "--rm", "linux"], cwd=REPO)
        return REPO / "build-linux" / "ge007.x86_64"
    sys.exit(f"unknown build '{kind}'")


def game_args(a):
    out = []
    if a.level is not None:
        out.append(f"-level_{a.level:02d}")
    if a.hard is not None:
        out.append(f"-hard{a.hard}")
    return out + list(a.args)


def game_env(a):
    """--env NAME=value (repeatable) and --secs as {NAME: value}."""
    env = {}
    for t in (a.env or []) + ([f"NX_TEST_SECS={a.secs}"] if getattr(a, "secs", None) else []):
        k, eq, v = t.partition("=")
        if not eq or not k:
            sys.exit(f"--env wants NAME=value, got '{t}'")
        env[k] = v
    return env


def nx_tokens(a):
    """Game args + env as the NX_ARGS build reads them: NAME=value tokens beside the switches."""
    return game_args(a) + [f"{k}={v}" for k, v in game_env(a).items()]


def cmd_build(a):
    p = build(a.kind)
    print("->", p)


def cmd_nx(a):
    c = cfg()
    ip = need(c, "console_ip")
    kind = a.build
    nro = REPO / BUILDS[kind][0] / NRO if kind != "none" else REPO / BUILDS["link"][0] / NRO
    if kind != "none":
        build(kind)
    if a.name:                                             # identifiable copy, like <feature>-test.nro
        named = nro.with_name(f"{a.name}-test.nro")
        shutil.copyfile(nro, named)
        nro = named
    log = Path(a.log) if a.log else REPO / BUILDS.get(kind, BUILDS["link"])[0] / "nx.log"
    log.write_text("", encoding="utf-8")
    dest = "project-dam.nro" if a.installed else "project-dam-test.nro"   # --installed: the in-game update's target
    cmd = [sys.executable, str(HERE / "switch" / "nxlink.py"), ip, str(nro), f"{NX_DIR}/{dest}",
           str(log)] + nx_tokens(a)
    print(f"log: {log}")
    env = dict(os.environ, NXLINK_RETRIES=os.environ.get("NXLINK_RETRIES", "20"))
    # the client waits for the game's stdio after the upload; a TimeoutError then is normal for non-link builds
    subprocess.run(cmd, env=env)


def cmd_switch(a):
    c = cfg()
    exe = need(c, "ryujinx_exe")
    sd = Path(need(c, "ryujinx_sd")) / SD_DIR
    if a.build != "none":
        nro = build(a.build)
    else:
        nro = REPO / BUILDS["args"][0] / NRO
    sd.mkdir(parents=True, exist_ok=True)
    (sd / "data").mkdir(exist_ok=True)
    shutil.copyfile(nro, sd / "project-dam.nro")
    args = nx_tokens(a)
    if args:
        args_file = sd / "data" / "ge007.args"
        if args_file.exists() and not (args_file.with_suffix(".args.bak")).exists():
            shutil.copyfile(args_file, args_file.with_suffix(".args.bak"))
        args_file.write_text(" ".join(args) + "\n", encoding="utf-8")
        print("args:", " ".join(args))
    print("log:", sd / "data" / "ge007.log")
    subprocess.Popen([exe, str(sd / "project-dam.nro")], cwd=str(Path(exe).parent))


def cmd_n64(a):
    c = cfg()
    if "ares_rom" not in c and devenv.load_env().get("N64_ROM"):   # the older single-ROM key
        c["ares_rom"] = devenv.load_env()["N64_ROM"]
    exe, rom = need(c, "ares_exe"), need(c, "ares_rom")
    subprocess.Popen([exe, rom], cwd=str(Path(exe).parent))   # ROM path only: --system opens an Error window


def ensure_native_data(exe):
    """The PC build reads data/ next to the exe (ROM, ge007.ini, log). A fresh worktree has none:
    create it and copy the ROM from N64_ROM under the name the game looks for."""
    data = Path(exe).parent / "data"
    data.mkdir(exist_ok=True)
    names = ("ge007.ntsc-final.z64", "baserom.u.z64")
    if not any((data / n).exists() or (Path(exe).parent / n).exists() for n in names):
        env = devenv.load_env()
        if not env.get("SWITCH_EMU_SD") and cfg().get("ryujinx_sd"):   # same ROM the Switch emulator already has
            env["SWITCH_EMU_SD"] = cfg()["ryujinx_sd"]
        src = devenv.find_rom(env)                         # N64_ROM, another game data/ folder, the emulator's SD card
        if src is None:
            sys.exit(f"no ROM for the native build: set N64_ROM_US in .env (or put {names[0]} in {data})")
        src = Path(src)
        print(f"copying the ROM into {data / names[0]}")
        shutil.copyfile(src, data / names[0])
    return data


def sync_campaign(exe):
    """The solo campaign (campaign/) as a mod folder next to the exe: mods/campaign.pmod/ in directory form
    (a .pmod may be a plain folder), refreshed on every run so edits to the Luau scripts show up."""
    dest = Path(exe).parent / "mods" / "campaign.pmod"
    if dest.is_dir():
        shutil.rmtree(dest)
    elif dest.exists():
        dest.unlink()                                      # a packed .pmod from a release zip
    shutil.copytree(REPO / "campaign", dest, ignore=shutil.ignore_patterns(".*", "dist"))
    print(f"campaign -> {dest}")


def cmd_native(a, kind):
    if a.build:
        exe = build(kind)
    else:
        exe = REPO / ("build-win/ge007.x86_64.exe" if kind == "windows" else "build-linux/ge007.x86_64")
    if not exe.exists():
        sys.exit(f"{exe} does not exist: run with --build")
    data = ensure_native_data(exe)
    if not a.no_campaign:
        sync_campaign(exe)
    # Only the Switch NX_ARGS build turns NAME=value tokens into environment variables; the PC game would take
    # them as plain arguments and ignore them (GE_QUITFRAME=300 never quit). Set them in its environment here.
    args, env = game_args(a), dict(os.environ, **game_env(a))
    if kind == "linux" and os.name == "nt":
        wsl = subprocess.run(["wsl", "wslpath", "-a", exe.as_posix()], capture_output=True, text=True).stdout.strip()
        if not wsl:                                         # backslashes would be eaten by the WSL shell
            sys.exit("WSL could not translate " + str(exe))
        names = [k for k in env if k not in os.environ or env[k] != os.environ[k]]
        if names:                                           # WSLENV carries Windows variables into the Linux side
            env["WSLENV"] = ":".join(filter(None, [os.environ.get("WSLENV", "")] + names))
        p = subprocess.Popen(["wsl", wsl] + args, cwd=str(exe.parent), env=env)
    else:
        p = subprocess.Popen([str(exe)] + args, cwd=str(exe.parent), env=env)
    time.sleep(3)
    if p.poll() is not None:                               # a game that dies at once would otherwise be silent
        sys.exit(f"the game exited right away (code {p.returncode}); see {data / 'ge007.log'}")
    print("running; log:", data / "ge007.log")


def cmd_editor(a):
    """The mission editor of the Windows build, with Play and the level export on the game next to it."""
    if a.build:
        build("windows")
    exe, game = REPO / "build-win" / "project-dam-editor.exe", REPO / "build-win" / "ge007.x86_64.exe"
    for p in (exe, game):
        if not p.exists():
            sys.exit(f"{p} does not exist: run with --build")
    ensure_native_data(game)                               # the game exports levels from the ROM in data/
    args = ["--game", str(game)]
    if a.level is not None:                                # the editor takes the level name
        name = next((n for n, v in LEVELS.items() if v == a.level), None)
        if name is None:
            sys.exit(f"level {a.level} is not a solo level")
        args += ["--level", name]
    if a.mod:
        args += ["--mod", str(Path(a.mod).resolve())]
    if a.mission:
        args += ["--mission", str(Path(a.mission).resolve())]
    if a.renderer:
        args += ["--renderer", a.renderer]
    args += list(a.args)
    env = dict(os.environ, **game_env(a))
    print("+", exe, " ".join(args), flush=True)
    p = subprocess.Popen([str(exe)] + args, cwd=str(exe.parent), env=env)
    time.sleep(2)
    if p.poll() is not None:
        sys.exit(f"the editor exited right away (code {p.returncode})")
    print("running; level exports go to", game.parent / "data" / "exports")


# name -> (script, what it is, its subcommands; the first one is the default when none is given)
TOOLS = {
    "modelview": ("tools_pc/modelview/modelview.py", "characters from the export: body + head pickers, subdivision, animations",
                  ["pc", "export", "selftest", "shots"]),
    "fxview":    ("tools_pc/fxview/fxview.py", "combat effects on ge_render: sprites, bullet holes, cluster scene, muzzle / tracer / burst / explosion tabs (win | ryujinx | nx)",
                  ["win", "ryujinx", "nx", "selftest", "shots", "decalshots", "tabshots"]),
    "menuview":  ("tools_pc/menuview/menuview.py", "the options menu on its own", []),
    "verify":    ("tools_pc/verify.sh", "headless capture of a level vs the golden frames (build-pc)", []),
}
GXX_KEYS = {"modelview": ("MODELVIEW_GXX",), "fxview": ("FXVIEW_GXX", "MODELVIEW_GXX"), "menuview": ()}
ROM_KEYS = {"modelview": ("MODELVIEW_ROM",), "fxview": ("FXVIEW_ROM", "MODELVIEW_ROM")}


def find_bash():
    """Git's or MSYS2's bash on Windows (System32's bash.exe is WSL, which cannot run the Windows game)."""
    if os.name != "nt":
        return shutil.which("bash") or "bash"
    for p in (r"C:\Program Files\Git\bin\bash.exe", r"C:\msys64\usr\bin\bash.exe"):
        if os.path.isfile(p):
            return p
    b = shutil.which("bash")
    if b and "system32" not in b.lower():
        return b
    sys.exit("no bash for the .sh tools: install Git for Windows or MSYS2")


def tools_status():
    env = devenv.load_env()
    print("dev tools (python tools/dev.py tools <name> [args]):\n")
    for name, (script, what, subs) in TOOLS.items():
        print(f"  {name:10} {what}")
        print(f"  {'':10} {script}" + (f"  [{' | '.join(subs)}]" if subs else ""))
        if name in GXX_KEYS:
            gxx = devenv.find_gxx(env, GXX_KEYS[name])
            print(f"  {'':10} g++: {gxx or 'MISSING (set SEAVIEW_GXX in .env)'}")
        if name in ROM_KEYS:
            rom = devenv.find_rom(env, ROM_KEYS[name])
            print(f"  {'':10} ROM: {rom or 'none (set N64_ROM_US in .env)'}")
        if name == "verify":
            d = devenv.find_data(env)
            print(f"  {'':10} data: {d or 'none: run the game once so it converts the ROM (dev.py win)'}")
        print()
    print("  any other tools_pc/<name>.py or .sh runs the same way, from the repo root, the ROM found through .env.")
    print("  ROM:", devenv.find_rom(env) or "none (set N64_ROM_US in .env)")
    print("  .env:", ", ".join(p for p in devenv.env_files() if os.path.exists(p)) or "none (copy .env.example)")


def cmd_tools(a):
    if not a.name:
        return tools_status()
    args = list(a.args)
    if a.name in TOOLS:
        script, _, subs = TOOLS[a.name]
        if subs and (not args or (args[0] not in subs and args[0] not in ("-h", "--help"))):
            args = [subs[0]] + args                         # `tools modelview --model X` = `modelview.py pc --model X`
    else:
        hits = [f"tools_pc/{a.name}{ext}" for ext in ("", ".py", ".sh") if (REPO / f"tools_pc/{a.name}{ext}").is_file()]
        if not hits:
            sys.exit(f"no tool '{a.name}': run `python tools/dev.py tools` for the list, or name a tools_pc script")
        script = hits[0]
    env = dict(os.environ)
    if script.endswith(".sh"):
        if not os.environ.get("GE_DATA"):                    # verify.sh / capture_p7.sh copy a converted data/ per run
            d = devenv.find_data(devenv.load_env())
            if d:
                env["GE_DATA"] = d
        cmd = [find_bash(), script] + args
    else:
        cmd = [sys.executable, script] + args
    print("+", " ".join(cmd), flush=True)
    sys.exit(subprocess.run(cmd, cwd=REPO, env=env).returncode)


def cmd_config(a):
    for k, v in cfg().items():
        print(f"{k} = {v}")


TARGETS_HELP = ("Targets:\n"
    "  switch   console build (build-switch), what ships\n"
    "  args     Switch build reading data/ge007.args (build-switch-args), for the emulator\n"
    "  link     Switch build with nxlink stdio + crash dump (build-switch-link), for the console\n"
    "  linux    native Linux build (build-linux)\n"
    "  windows  native Windows build (build-win)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def game_opts(p):
        p.add_argument("--level", type=level_arg, metavar="LEVEL", help="boot into a solo level: a number or a name. " + LEVELS_HELP)
        p.add_argument("--hard", type=int, help="-hardN (0..3)")
        p.add_argument("--env", action="append", metavar="NAME=value", help="environment variable for the game (repeatable)")
        p.add_argument("args", nargs="*", help="after `--`: game args passed as they are (-level_09, ...)")

    p = sub.add_parser("build", help="build one target in Docker", formatter_class=argparse.RawDescriptionHelpFormatter,
                       description=TARGETS_HELP)
    p.add_argument("kind", choices=["switch", "args", "link", "linux", "windows"], metavar="{switch,args,link,linux,windows}")
    p.set_defaults(fn=cmd_build)

    p = sub.add_parser("nx", help="build + send to the console over nxlink")
    p.add_argument("--build", choices=["link", "args", "switch", "none"], default="link",
                   help="which build to send (default link: log + crash dump come back); none = send the last one without rebuilding")
    p.add_argument("--name", help="copy the NRO to <name>-test.nro before sending")
    p.add_argument("--installed", action="store_true",
                   help="send over the installed project-dam.nro instead of project-dam-test.nro (tests the in-game update)")
    p.add_argument("--secs", type=int, help="NX_TEST_SECS=N")
    p.add_argument("--log", help="stdio log file (default: <build dir>/nx.log)")
    game_opts(p); p.set_defaults(fn=cmd_nx)

    p = sub.add_parser("switch", help="build + run in the Switch emulator")
    p.add_argument("--build", choices=["args", "switch", "none"], default="args",
                   help="which build to run (default args: reads the level/args this command writes); none = no rebuild")
    game_opts(p); p.set_defaults(fn=cmd_switch)

    p = sub.add_parser("n64", help="launch the N64 emulator (ares) on the ROM"); p.set_defaults(fn=cmd_n64)

    for name, kind in (("win", "windows"), ("linux", "linux")):
        p = sub.add_parser(name, help=f"run the native {kind} build")
        p.add_argument("--build", action="store_true", help="rebuild first (Docker)")
        p.add_argument("--no-campaign", action="store_true", help="do not copy campaign/ to mods/campaign.pmod/ (default: copy)")
        game_opts(p); p.set_defaults(fn=lambda a, k=kind: cmd_native(a, k))

    p = sub.add_parser("editor", help="run the mission editor (Windows build), its game next to it")
    p.add_argument("--build", action="store_true", help="rebuild first (Docker, the windows target)")
    p.add_argument("--level", type=level_arg, metavar="LEVEL",
                   help="open a solo level (exported by the game the first time). " + LEVELS_HELP)
    p.add_argument("--mod", help="mod folder (mods/<mod>) to open")
    p.add_argument("--mission", help="mission.json to open (its level comes with it)")
    p.add_argument("--renderer", choices=["gl", "vk"], help="the editor's ge_render backend: OpenGL or Vulkan (default: the Settings choice, OpenGL)")
    p.add_argument("--env", action="append", metavar="NAME=value", help="environment variable for the editor (repeatable)")
    p.add_argument("args", nargs="*", help="after `--`: editor args passed as they are (see project-dam-editor --help)")
    p.set_defaults(fn=cmd_editor)

    p = sub.add_parser("config"); p.set_defaults(fn=cmd_config)

    p = sub.add_parser("tools", help="the dev tools (modelview, fxview (also the GE+ sky/sea: --mode sea), menuview, verify, any tools_pc script): "
                                     "no name lists them and what each still needs")
    p.add_argument("name", nargs="?", help="modelview | fxview | menuview | verify | a tools_pc script name")
    p.add_argument("args", nargs=argparse.REMAINDER, help="passed to the tool (its default subcommand when none is given)")
    p.set_defaults(fn=cmd_tools)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
