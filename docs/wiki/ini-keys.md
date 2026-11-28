---
title: ini keys
sources:
  - port/src/defaults.json
  - port/src/defaults.c
  - port/src/config.c
  - port/src/menu/menu.json
  - port/src/switch/nxgame.c
  - port/src/gpuprobe.c
  - port/src/video.c
verified: e2fda9e2
---

# ini keys

The settings the game keeps in `ge007.ini`, with their default per platform and the menu row
that edits them. How the menu binds them: [options-menu.md](options-menu.md).

## The file

- **Where:** `data/ge007.ini` next to the executable on the PC, `sdmc:/switch/project-dam/data/ge007.ini`
  on the Switch (the `$S/` data folder, beside `ge007.log`). Read and written by `config.c`
  (`configLoad` / `configSave`).
- **Defaults:** `port/src/defaults.json` is the only source. `defaultsApply()` sets them before
  `configLoad()`, so a missing key takes its default and a fresh install gets a complete ini.
- **Version:** `Config.Version`. An older ini runs the migrations of `defaults.json` once
  (`set` / `reset` steps, optionally `when` another key has a value). Keys removed from the game
  but still read by a migration are listed in `legacy` (`Video.NativeWidescreen`,
  `Video.WidescreenAuto` today).
- **Keys outside the table:** some are registered without a default here: the key and pad
  bindings (`Input.Bind.*`), per-player device choices (`Input.Player<N>Pad`),
  `Dev.Mode` (shows the menu's dev rows), `Menu.Language`, the `Debug.*` keys, and
  `Game.LookAheadLevel` (0..3, `port/src/switch/nxgame.c`: the Original / Low / Medium / High part
  of the per-profile Look ahead, written by the menu; one value for every profile).
- **Hidden keys** (registered, no menu row, no entry in `defaults.json`; set by the machine, editable in the ini):
  - `Video.GpuProbeDone` (0/1): the first-launch GPU benchmark (`port/src/gpuprobe.c`, GL half `gfx_opengl_gpuprobe`)
    has run, or its cautious fallback was applied. It runs once per ini, on the PC and handhelds (not the Switch), and
    measures what the GPU can afford: it lowers `Video.SkyScale` (full / half / quarter sky), `Video.MSAA` (4x down to
    2x or 1x) and `Video.AlphaToCoverage` (off) from their defaults only; a value the player has changed is left alone.
    It draws a short "Measuring the GPU..." screen with the numbers (a few seconds at most). Delete the key or set it to
    0 to run it again. Env: `GE_GPUPROBE=0` never, `=1` run again and overwrite, `GE_GPUPROBE_HIDDEN=1` do not show it.
    When `Video.Renderer` is Vulkan, the first launch runs on OpenGL for the probe (the stored renderer is not
    changed) and Vulkan is used from the next launch.
  - `Video.AlphaToCoverage` (0/1, default 1): smooth alpha cut-outs (leaves, fences) through MSAA instead of a plain blend.
    The probe turns it off where it costs frames; `GE_A2C=0/1` pins it for one run.
  - `System.Console` (0/1): run as a console (no keyboard or mouse; the window is the panel). A PortMaster handheld
    launcher sets it; the Switch is always a console. `GE_CONSOLE=1` overrides it (`port/include/console.h`).
  - `System.Handheld` (0/1): the screen is small (a Linux handheld, not a Pi on a TV); with console mode it turns on
    `Game.AimZoom`. Off the Switch only (the Switch uses its docked/handheld state). `GE_HANDHELD=1` overrides it;
    `packaging/linux-arm64/project-dam-handheld.sh` sets it (#131).
- **Per-profile settings** (`Bond.*` in the menu: music and FX volume, aim control, auto-aim, look ahead (Off, Original, Low, Medium or High; only Off / Original is in the save, the level is `Game.LookAheadLevel` above),
  sight and ammo on screen) are not in the ini, apart from that level: they live
  in the save file, as on the N64.
- **One-off override (test builds):** `GE_CFG="Key=value;Key2=value2"` sets values for one run and
  the ini is not written back. See [testing-on-switch.md](testing-on-switch.md).

## Keys with a default

`—` = no default on that platform (the setting does not exist there). Choice values are the
option's position or its `value` in `menu.json`.

| Key | Switch | PC | In the menu |
|---|---|---|---|
| `Audio.MasterVolume` | 100 | 100 | Audio > Volume > Master volume |
| `Game.AimAssist` | 2 | 2 | Input > Aiming > Aim assist |
| `Game.AimEntry` | 1 | 1 | Input > Aiming > Aim entry |
| `Game.AimStop` | 1 | 1 | Input > Aiming > Stop on aim |
| `Game.AimZoom` | 2 | — | Input > Aiming > Aim zoom (handheld) |
| `Game.AllUnlocked` | 0 | 0 | Game > Gameplay > All unlocked |
| `Game.BloodOnHit` | 1 | 1 | Video > Effects > Blood on hit |
| `Game.CheckUpdates` | 0 | 0 | Game > Updates > Check for updates at start |
| `Game.CoopEnemyHealth` | 1 | 1 | Game > Gameplay > Co-op enemy health |
| `Game.HudScale` | 100 | 100 | Game > Screen > HUD scale |
| `Game.MissionTimer` | 0 | 0 | Game > Gameplay > Mission timer |
| `Game.NoHitFlash` | 0 | 0 | Game > Gameplay > No hit flash |
| `Game.ScreenShakeIntensity` | 1 | 1 | Video > Effects > Explosion shake |
| `Game.SplitScreen` | 0 | 0 | Video > Display > Split screen |
| `Input.CrosshairCursor` | 1 | 1 | Input > Mouse > Crosshair pointer |
| `Input.CrouchMode` | 1 | 1 | Input > Controls > Crouch |
| `Input.MouseEnabled` | 1 | 1 | Input > Mouse > Mouse enabled |
| `Input.MouseInvertY` | 0 | 0 | Input > Mouse > Invert look |
| `Input.MouseRawInput` | 0 | 0 | Input > Mouse > Raw input |
| `Input.MouseSensitivity` | 230 | 230 | Input > Mouse > Horizontal sensitivity |
| `Input.MouseSmoothing` | 0 | 0 | Input > Mouse > Smoothing |
| `Input.MouseYScale` | 100 | 100 | Input > Mouse > Vertical sensitivity |
| `Input.P1Type` | 2 | 2 | — (the device assignment screen) |
| `Input.PadAimLookSens` | 75 | 100 | Input > Aiming > Aim sensitivity |
| `Input.PadDeadzoneL` | 3000 | 7500 | Input > Look and sticks > Left stick deadzone |
| `Input.PadDeadzoneR` | 3000 | 7500 | Input > Look and sticks > Right stick deadzone |
| `Input.PadLookCurve` | 200 | 200 | Input > Look and sticks > Look precision |
| `Input.PadLookInvertY` | 1 | 1 | Input > Look and sticks > Invert look |
| `Input.PadLookSensX` | 85 | 100 | Input > Look and sticks > Horizontal look sensitivity |
| `Input.PadLookSensY` | 85 | 100 | Input > Look and sticks > Vertical look sensitivity |
| `Input.PadLookSmooth` | 2 | 0 | Input > Look and sticks > Look smoothing |
| `Input.PadSouthpaw` | 0 | 0 | Input > Look and sticks > Southpaw |
| `Input.PadTriggerPct` | 10 | 25 | Input > Look and sticks > Trigger threshold |
| `Input.PadTurnAccel` | 2 | — | Input > Look and sticks > Turn acceleration |
| `Input.RumbleScale` | 0.5 | 0.5 | Input > Look and sticks > Vibration |
| `Video.Anisotropy` | 4 | 4 | Video > Performance > Anisotropic filtering |
| `Video.AspectMode` | 0 | 0 | Video > Display > Aspect ratio |
| `Video.CrosshairAlpha` | 100 | 100 | Game > Crosshair > Opacity |
| `Video.CrosshairBlue` | 255 | 255 | Game > Crosshair > Blue |
| `Video.CrosshairColor` | 0 | 0 | Game > Crosshair > Color |
| `Video.CrosshairGreen` | 255 | 255 | Game > Crosshair > Green |
| `Video.CrosshairHealthColor` | 0 | 0 | Game > Crosshair > Color by health |
| `Video.CrosshairHide` | 0 | 0 | Game > Crosshair > Show crosshair |
| `Video.CrosshairPersistent` | 0 | 0 | Game > Crosshair > Always on |
| `Video.CrosshairRed` | 255 | 255 | Game > Crosshair > Red |
| `Video.CrosshairSize` | 100 | 100 | Game > Crosshair > Size |
| `Video.CrosshairStyle` | 0 | 0 | Game > Crosshair > Style |
| `Video.DisplayFPS` | 0 | 0 | Video > Performance > Show FPS |
| `Video.DrawDistance` | 200 | 200 | Video > Performance > Draw distance |
| `Video.FXAA` | 1 | 1 | Video > Performance > FXAA |
| `Video.FogDistance` | 100 | 100 | Video > Performance > Fog distance |
| `Video.FovScale` | 100 | 100 | Video > Display > Vertical field of view |
| `Video.FpsCap` | -1 | -1 | Video > Performance > Frame rate cap |
| `Video.Fullscreen` | 0 | 0 | Video > Display > Fullscreen |
| `Video.FullscreenMode` | 0 | 0 | Video > Display > Fullscreen mode |
| `Video.LodDistance` | 200 | 200 | Video > Performance > LOD distance |
| `Video.MSAA` | 4 | 4 | Video > Performance > Anti-aliasing (docked on the Switch) |
| `Video.MSAAHandheld` | 2 | — | Video > Performance > Anti-aliasing (handheld) |
| `Video.PerPixelFog` | 1 | 1 | Video > Performance > Per-pixel fog |
| `Video.SafeAreaCrop` | 1 | 1 | Video > Display > Crop overscan |
| `Video.SkyScale` | — | 0 | Video > Performance > Sky and sea detail |
| `Video.TextureFilter` | 1 | 1 | Video > Performance > Texture filter |
| `Video.AssetPacks` | "" | "" | Video > Asset packs: every known pack, file names in `assetpacks/` (or `texturepacks/`), highest priority first, `;`-separated, a disabled pack prefixed `-` (`ge007.passets;-test.passets`); a value without `-` means all enabled; not in `defaults.json`: empty (never set) means every pack found is enabled. An older `Video.TexturePack` moves into it once (`docs/specs/asset-packs.md`) |
| `Video.VSync` | 1 | 1 | Video > Performance > VSync |
| `Video.Visuals` | 2 | 2 | Video > Display > Visuals |

Regenerate this table from `defaults.json` and `menu.json` when keys are added (a short script
reading both files; the menu path is tab > entry > label).
