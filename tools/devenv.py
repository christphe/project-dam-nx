"""Machine settings shared by tools/dev.py and the tools_pc viewers and scripts.

Settings come from <repo>/.env (see .env.example), or the main checkout's when run from a git worktree (.env is
not tracked, so a worktree has none); a variable set in the real environment wins over both.

    import devenv                                    # tools_pc scripts: sys.path.insert(0, <repo>/tools) first
    env = devenv.load_env()
    rom = devenv.find_rom(env)                       # the user's .z64, or None
    gxx = devenv.find_gxx(env, ("MODELVIEW_GXX",))   # a Windows g++, or None

Nothing here writes anything: the ROM stays where the user keeps it.
"""
import glob, os, subprocess

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

# Keys the real environment may override even when no .env names them.
KNOWN = ("SWITCH_IP", "SWITCH_EMU_EXE", "SWITCH_EMU_SD", "N64_EMU_EXE", "N64_ROM", "N64_ROM_US", "N64_ROM_JP", "N64_ROM_PAL", "DOCKER_IMAGE", "SEAVIEW_GXX",
         "MODELVIEW_GXX", "MODELVIEW_ROM", "MODELVIEW_EXPORT", "MODELVIEW_GAME", "FXVIEW_GXX", "FXVIEW_ROM", "H700_IP")
ROM_NAMES = ("ge007.ntsc-final.z64", "baserom.u.z64")   # the names the PC game looks for in its data/
SD_DIR = "switch/project-dam"                           # the game folder on the Switch emulator's SD card


def main_checkout():
    """The main checkout's root when this is a worktree (the git common dir's parent), else None."""
    try:
        common = subprocess.run(["git", "-C", REPO, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True).stdout.strip()
    except OSError:
        return None
    if not common:
        return None
    root = os.path.dirname(os.path.abspath(common))
    return None if os.path.normcase(root) == os.path.normcase(REPO) else root


def roots():
    """This checkout, then the main checkout when this is a worktree."""
    m = main_checkout()
    return [REPO] + ([m] if m else [])


def env_files():
    return [os.path.join(r, ".env") for r in roots()]


def load_env():
    """All the .env keys (this checkout's first), the real environment winning."""
    env = {}
    for p in env_files():
        if not os.path.exists(p):
            continue
        for line in open(p, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            env.setdefault(k.strip(), v.strip().strip("'\""))
    for k in set(KNOWN) | set(env):
        if os.environ.get(k):
            env[k] = os.environ[k]
    return env


def rom_candidates(env, keys=()):
    """(where, path) in the order find_rom tries them; the paths may not exist."""
    out = [(k, env[k]) for k in tuple(keys) + ("N64_ROM_US", "N64_ROM") if env.get(k)]
    for r in roots():
        for d in ("build-win/data", "build-pc/data", "build-linux/data", "data"):
            for n in ROM_NAMES:
                out.append((os.path.join(r, d).replace("\\", "/"), os.path.join(r, d, n)))
    if env.get("SWITCH_EMU_SD"):                        # the same ROM the Switch emulator already has
        out.append(("SWITCH_EMU_SD", os.path.join(env["SWITCH_EMU_SD"], SD_DIR, "data", ROM_NAMES[0])))
    return out


def find_rom(env, keys=()):
    """The user's ROM: the tool's own keys, N64_ROM, a game data/ folder (build-win, build-pc, build-linux, data,
    in this checkout then the main one), the Switch emulator's SD card. None when there is none."""
    for _, p in rom_candidates(env, keys):
        if os.path.isfile(p):
            return os.path.abspath(p)
    return None


def rom_path(region="ntsc-final"):
    """For the offline ROM scripts: the user's ROM (find_rom) for the US release, else data/ge007.<region>.z64.
    The result may not exist; the caller says so."""
    if region == "ntsc-final":
        rom = find_rom(load_env())
        if rom:
            return rom
    return os.path.join(REPO, "data", f"ge007.{region}.z64")


def find_gxx(env, keys=()):
    """A Windows g++ (MinGW-w64): the tool's own keys, SEAVIEW_GXX, else MSYS2's default install. None when none."""
    for k in tuple(keys) + ("SEAVIEW_GXX",):
        if env.get(k) and os.path.isfile(env[k]):
            return env[k]
    if os.name == "nt":
        p = r"C:\msys64\mingw64\bin\g++.exe"
        if os.path.isfile(p):
            return p
    return None


def find_data(env, region="ntsc-final"):
    """A game data/ folder the game has already converted (ROM + pccg-<region> + pcmodels-<region>), for the
    capture scripts that copy one per run. None when there is none."""
    need = (f"ge007.{region}.z64", f"pccg-{region}", f"pcmodels-{region}")
    for r in roots():
        for d in ("data", "build-pc/data", "build-win/data", "build-linux/data"):
            p = os.path.join(r, d)
            if all(os.path.exists(os.path.join(p, n)) for n in need):
                return os.path.abspath(p)
    return None
