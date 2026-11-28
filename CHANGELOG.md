# Changelog

## [Unreleased]

### Added
- Custom missions and mods: a MODS row on the main screen lists the mods installed in `mods/` next to `data/` (on the Switch `sdmc:/switch/project-dam/mods/`) with their cover, author, description and progress, and plays their missions through the game's own menus, solo or co-op, with a save per mod. A mission reworks a solo level (guards, objects, pads, zones, objectives, briefing, environment, what is present at each difficulty) and runs Luau scripts: doors, items, objectives, dialogue lines (spoken from a guard or a place, with a bubble over the speaker in every player's view, pinned to the edge of the view when the speaker is out of it), guards that follow, walk, face, surrender or shoot. The Project Dam editor (PC, built from source) makes them: the level in 3D, a props browser, a script editor that checks types, a validator, test runs and an MCP server. Formula, an escort mission in the Facility, comes as a sample with the modding guide (`docs/modding/`)
- Combat effects (Enhanced visuals, F9 toggles them): fast, bright tracers; impact bursts per material (stone and tile chips, dirt, crisp wood splinters, metal sparks, glass shards, water splashes, snow); blood in directional jets; bullet holes that fuse into one cratered patch per surface, take the wall's colour and stop at its edges; holes in crates and windows, the window ones cut through the game's own glass
- Dynamic lights (Enhanced visuals, Switch and PC on Vulkan): explosions light the walls, floor, guards and objects around them in a warm glow that fades with the blast, brighter for bigger blasts, and every shot's muzzle flash, Bond's and the guards', briefly lights its surroundings
- PC: the first launch measures the GPU for about a second (shown on screen) and lowers the sky and sea detail, the anti-aliasing and alpha-to-coverage only where they cost frames; settings you changed are left alone. Sky and sea detail gets a Quarter (fastest) level
- PC: the options menu names the buttons of the controller in use: the Nintendo letters for a Switch Pro controller, the cross, circle, square and triangle for a PlayStation one (on the controller drawing and in the button hints)

- Asset packs: `.passets` files in `assetpacks/` next to `data/` (on the Switch `sdmc:/switch/project-dam/assetpacks/`; `.ptexpack` files in `texturepacks/` still work) replace the game's textures (pictures of any size), text, fonts and sound effects; the game's own are exported from your ROM into `ge007.passets` at first launch. Video > Asset packs lists them with their author, enables any number of them and orders them, every pack may be partial, and the change applies when you choose Apply, also during a mission. The Project Dam editor draws a level with the pack of your choice, and a mod can give a level's walls, floors and ceilings pictures of its own (Mod tab > Textures), drawn over the pack's while its missions are played. Format and rules in `docs/modding/`
- Mod and campaign scripts can show their own credits: `credits.show({ lines = { ... } })` scrolls titles, headings, plain lines and two-column role / name lines up over every player's view, with an optional black backdrop and a speed; `credits.stop()` ends them and `credits.done()` tells when they ended (`docs/modding/`)
- Rounder heads: the conversion subdivides the characters' heads (and the heads built into bodies such as Boris or Natalya) up close, with the original mesh as the far level of detail; bodies keep their detailed mesh at every distance instead of switching to the simplified one. The models are converted again once at the next launch

### Changed
- The Enhanced and Original control styles are gone, with the layout presets (1.1 Jinx ... 1.4 Elektra, Custom), the per-button pad rebinding and the virtual N64 pad: the gamepad has one layout, Modern, for every player. The *Control style*, *Layout* and *Controller style* rows are removed from the menu, and their ini keys (`Input.ControlScheme*`, `Input.PadPreset*`, `Input.Pad*.<Action>`, `Input.N64Pad*.<Button>`, `Input.N64StickFeel`) are no longer read. The keyboard and mouse bindings are unchanged.
- The Original aim (the N64 crosshair that travels across the screen) is gone: the aim is always Centered. The *Aim style* and *Aim range* rows are removed, and `Input.AimMode`, `Input.AimRange` and the older aim keys are no longer read.
- The ROM is checked against the three retail images (US, EU, JP) at every start, and the converter takes the region from that check: patched or modified ROMs are refused with a clear message, and a byte-swapped .v64 or .n64 dump is told to be converted to .z64
- The intro is gone: the legal page, the Nintendo, Rare and GoldenEye logos, the gun barrel and the demos that played after a while on the main screen. The game opens straight on the file select, and it no longer goes back to an intro when left idle. The *Skip intro* row and its `Game.SkipIntro` key are removed (an old ini keeps the key unused)
- PC: a fresh install now uses the Switch's defaults for the Centered aim style, 4x anti-aliasing and blood on hit; an existing ini keeps its values
- PC: the stick deadzone is 10% on both sticks by default, as on the Switch (it was 25%, which swallowed the small aim movements); a deadzone you set yourself is kept

### Fixed
- PC: a first launch that did not end with a clean quit (a crash, a killed window) left the stick deadzones at 0% from the next launch, so the sticks drifted; such inis are repaired to 10% once

## [0.4.0] - 2026-11-21

### Added
- A Windows build (x86-64 zip) in every release, next to the Switch NRO; the PC update check offers it
- A fullscreen options menu, the same on the Switch and the PC (F10 on the PC, Select on a pad): a first menu (Resume, Options, Controllers, Check for updates, Restart, Quit), then Video, Audio, Input and Game pages with a help line for each value. It replaces the small card of the old overlay
- Modern control style: right trigger fires, left trigger aims, the face buttons reload, switch weapon and use (by position, so a Nintendo pad gets the same hands), a click of the right stick crouches. Hold the left bumper for a weapon wheel or the right one for a gadget wheel and pick with the right stick: you keep walking, the wheel only takes the aim
- Check for updates in the first menu (PC and Switch): "Update available" opens the release notes, and on the Switch installs the new version (A twice: it downloads, checks the hash and replaces the NRO, #68). The version is shown beside the title
- Aspect ratio Auto, Fit, 4:3, 16:9 or 21:9, and a split screen with the two players side by side
- Co-op for 2, 3 or 4 players (#75), and the Co-op row of the mode menu now also on PC: choosing Co-op asks how many players, each partner picks a character, and the mission runs in a split screen of two or four views; any player standing at a downed player's body can revive them, and the mission fails when everyone is down; Co-op enemy health is now a bonus per extra player (+0, +50 or +100%, so +50% gives 150% at 2 players, 200% at 3 and 250% at 4)
- Co-op and Multiplayer stay open with one controller: Co-op asks for the controllers it needs when it is chosen, Multiplayer asks for as many as there are players when Start is pressed (the Controllers button does the same any time, the system's controller screen on the Switch)
- PC only, over the upstream port (what the Switch build does not have; it renders with deko3d):
  - A Vulkan rendering backend (#44), the default, with OpenGL as the fallback: a *Renderer* row on the Display page (restart needed) or `GE_RENDERER=vk` / `gl`. It needs a Vulkan 1.3 driver (without one the game logs a line and uses OpenGL) and keeps a pipeline cache in `ge007.vkcache`. An ini written by an earlier build stays on OpenGL until the row is changed. `GE_PCDUMP` and the screenshot key work under Vulkan, and the validation layer is on whenever it is installed (`GE_VKVALIDATE=0` turns it off)
  - One Input page per player: input type (Auto, Keyboard + mouse or Gamepad, the keyboard and mouse for player 1 only), controller choice by id, mode (Enhanced or Original) and a key bindings sub-menu
  - Windows builds cross-compiled from Docker (MinGW-w64), next to the Linux one
- GE+ water on Dam and Caverns (Enhanced visuals, Switch and PC on Vulkan or OpenGL): the levels' own water gets the moving waves and the sky's reflection, with its colour remade from the level's water texture in large soft patches that keep the original's tones (#79)
- Vertical field of view in degrees (60 is the original) with the horizontal one shown for the window's aspect ratio; a crosshair page with a preview of the sprite at its real size, colour and opacity; the crosshair size goes from 10 to 100%

### Changed
- Look ahead (Game page, per profile) is Off, Original, Low, Medium or High, and follows the floor slope ahead with every control style (#85). Original is the game's own; Low, Medium and High wait for the look stick to rest (1, 0.7 or 0.5 s), then level the view at 45, 60 or 90 degrees per second, toward the slope and level on flat ground. It replaces Look ahead (Extended) on the Controller page, which only levelled to the horizon; Look ahead is off by default
- Enhanced and Modern walk and strafe at the speed of the left stick's tilt instead of the N64's full-step moves, for every player in split screen (#78). Modern with inverted look is the default control style on a fresh install
- Auto-aim is off while the mouse is the device that aims, and comes back when a stick is pushed
- GE_LOWLAT (lower input latency) is on by default on the PC too; GE_LOWLAT=0 turns it off
- The mouse always aims Centered; Input.AimMode is the gamepad's. Escape no longer frees the mouse
- PC: the mouse sensitivity is 230 by default on a fresh install (it was 100); an existing ini keeps its value

### Fixed
- PC, 2 or more players: the options menu opens on the pages of the player who opened it, and only that player's device drives it (any pad's Select opens it, in the menus too). Each player's pad settings (preset, bindings, Modern) are that player's even with the keyboard and mouse as player 1, and the Modern wheels read the right pad (#83)
- PC: the optional update check (Game.CheckUpdates, off by default) looked at the upstream port's releases and compared them with the upstream version; it now looks at Project Dam's releases and only announces one that has a PC build
- The button that closes the menu no longer reaches the game as a press
- PC, character select with the keyboard and mouse as player 1: the first pad now picks for player 2 (it drove player 1 with the keyboard, and the last player had no device); the button that closes the "who plays with what" screen, or that confirms the mode menu, no longer picks a character on the next screen
- Switch: the controllers vibrate when the game uses the Rumble Pak (shots, hits, explosions), with the Vibration setting (#40)

## [0.3.0] - 2026-11-19

### Added
- First launch converts your ROM by itself and shows it: a Project Dam screen with a progress bar (models, stage backgrounds, stage setups) that waits for A when it is done, instead of a black screen; a missing or refused ROM shows a message with the file name and the folder to put it in
- Frigate gets the GE+ sky and a sea (Visuals: Enhanced): near you the waves move and reflect the sky, and the distant sea is a still panorama of it; the GE+ sky used to leave water levels on the original sky (#38)

### Changed
- Faster rendering on the Switch: models and rooms that do not change are now also moved, fogged and clipped on the GPU instead of the CPU, which takes the renderer's busiest stage from about 6-8 ms to about 2.5-4 ms per frame in Jungle co-op (docked); same picture
- The download no longer carries a separate ROM converter: the game converts the ROM's models and levels itself on first launch
- Display mode is Enhanced, Original or Custom (it was Switch, N64 or Custom); only the names change
- Visuals on the Video page replaces the Sky option (Custom display mode): Original is the game's own picture, Fixed corrects the original game's display flaws listed under Fixed and draws exact per-pixel fog, Enhanced also adds the GE+ sky (Enhanced in the Enhanced display mode, Original in the Original one); it takes effect at once, no restart

### Fixed
- GE+ sky: the clouds overhead are no longer lit by a sun that is low or below the horizon
- Leaves, fences and other cut-out textures now hide one another in the right order and get smooth edges, with anti-aliasing on (Visuals: Fixed): the Jungle plant walls and trees no longer show the leaves behind them, and the Jungle bridge planks now hide the vines behind them
- Jungle: the crashed plane at the start no longer loses a wing, and nothing else in the level is cut off at a room boundary any more; the port never applied the level's rule that every room is drawn in full
- Jungle: far fern and palm leaves no longer show through the leaves in front of them; the plants' leaves now hide what is behind them (Visuals: Fixed) (#35)
- Surface 1 and 2: the satellite dish no longer disappears when the room system or the fog would hide it (Visuals: Fixed)
- Switch: the ground no longer shows a dark patch that moves with the view (Dam, by the wall below the hangars); colour gradients now follow the perspective
- Switch: some models and scenery were drawn with another object's texture or colours when their display lists were reused from a previous frame (dark triangles and yellow patches in Dam while firing)

## [0.2.0] - 2026-11-16

### Added
- Co-op friendly fire: your shots now hurt your partner (a partner shot down is revived like after any death), and auto-aim no longer locks onto your partner (#21)
- GE+ sky (Switch, on by default; Video > Sky): the original clouds in three layers at different heights on a curved sky, a sun with a halo on day and dusk levels, stars and a moon on night levels, and a haze band at the horizon that the scenery fades into; Original (or the N64 display mode) restores the game's own sky, and Frigate keeps it
- Co-op on the mode menu (Switch, between Select Mission and Multiplayer; needs a second controller): player 2 picks a character, then a solo mission starts for two players in split screen; a downed player is revived by the partner standing at the body and pressing A for 10 seconds, and the mission fails when both are down; Start opens that player's own watch in a full-screen view and pauses both players; scripted end-of-mission cameras show player 1's view in both halves; dropped weapons fall and can be picked up; ammo, armour and weapons lying in the level exist once per player, so a pickup one player takes stays for the other; no multiplayer radar; Co-op enemy health (100, 150 or 200 percent) on the Game page makes guards tougher
- Look ahead (Extended) on the Controller page (Switch; Off, Low, Medium or High speed, High by default): walking forward with the look stick at rest eases the view back to level; the game's own Look ahead never fired with the Extended controls because its walk is digital, and stays on the Game page
- Anti-aliasing on the Switch: MSAA 2x handheld and 4x docked, plus FXAA, a final pass that smooths polygon and rail edges without blurring flat textures; the Switch display mode turns them on
- Aim zoom and Aim assist on the Controller page (Switch): handheld only, the picture zooms 2x while you aim (1.5x available) so a head is big enough to hit on the small screen, and the aim stick slows down while the crosshair is on or near an enemy; centered aim only, and the aim stick slows with the zoom
- Aim entry on the Controller page (Switch, Move view by default): when you start aiming, the crosshair no longer slides away from the enemy it was on; the view turns with it to the centre in a few frames (or the crosshair centres at once)
- Stop on aim on the Controller page (Switch, on by default): aiming with the move stick released stops your character at once; the game lets him glide for about a second (the head-bob filter keeps pushing him), so the enemy the auto-aim had picked drifted out of the crosshair
- Blood on hit (Game page, on by default): a bullet in a guard's body makes a dark-red puff instead of the wall's pale-yellow one, so hits read as hits; off restores the original game
- Look precision and Turn acceleration on the Controller page: a stick curve you can steepen (2.0 by default, the original game's) for fine aim, and a turn that speeds up when the stick is held at the edge (High by default) for wide turns
- Centered aim style, the default on the Switch: the crosshair stays at the centre and the weapon trails the camera when it turns, then settles back at the weapon's own pace (there is no separate Centered + sway: it is part of Centered)
- Aim sensitivity on the Controller page: how fast the right stick turns while aiming (0.75x by default)

### Changed
- 2-player split screen (Switch, versus and co-op): each half no longer sees about 130 degrees across with stretched edges; it uses two thirds of the FOV setting and its own width, about 70 degrees across at 16:9 at the default FOV; the FOV setting still shows 60, solo and 3-4 players are unchanged, and the N64 display mode keeps the original view
- GE+ sky: Bunker 1 shows Surface 1's sky at its exit and through its windows instead of a flat dark-blue fill (the original game has no sky record for that mission); Original keeps the fill
- The options entry on the file select screen and the title of the options menu read "Options" instead of "PC Options"
- The Switch controller defaults are tuned values: both stick deadzones 10%, look sensitivity 0.85x, vertical look inverted, look smoothing 2, trigger threshold 10%, Aim sensitivity 0.75x, Turn acceleration High, Aim zoom 2x, Aim assist High, Look ahead High
- The Controller page hides the aim rows that need the Centered aim style (Aim zoom, Aim assist, Aim entry, Stop on aim, Look ahead, Look precision, Turn acceleration) while Aim style is Original, and the aim group and these rows with the Original control style: they only work with the Extended controls
- Display mode on the Switch has three choices: Switch (the game's own settings), N64 (the original look) and Custom; Switch and N64 only show Show FPS, and Custom shows every other graphics option (aspect, widescreen, VSync, frame cap, anti-aliasing, FXAA, texture filter, anisotropy, draw, fog and LOD distance) to tune the picture
- The right stick deadzone is 10% by default on the Switch instead of 25%, so small aim movements register; settings that still had the old default move to it once
- The options no longer show the Mouse and Key Bindings pages, which need a mouse or a keyboard; Aim sensitivity replaces the aim-mode sensitivity and precision curve options
- Aim style moves to the Controller page; the Centered styles turn the camera with the right stick, and the view stays where it is when the stick is released (Original keeps the N64 crosshair)
- Faster rendering of busy scenes: vertex data goes to the GPU through one streaming buffer instead of a new buffer per draw, about three times cheaper per draw on the console
- Faster rendering on the Switch: models and rooms that do not change are prepared once and reused on the next frames instead of being worked out again every frame, which frees about a third of the renderer's time in busy scenes (Jungle in co-op); same picture
- The Switch draws through deko3d, the console's own graphics API, instead of OpenGL: handing each frame to the GPU costs about a quarter of what it did, so busy scenes keep 60 fps where they used to drop (Jungle in co-op, docked, at 150% draw distance: 48-51 fps before, 60 now); same picture
- Quitting the game on the Switch now returns to the launcher it was started from (sphaira or the homebrew menu) instead of closing the whole application and dropping to Home; if the game's threads do not stop in time it falls back to the old exit

### Fixed
- GE+ sky: looking down from a height (Cradle), the view under the horizon showed blue clouds or streaks of earlier frames instead of the sky colour
- Split-screen uses the whole screen like solo: no black bars at the top and bottom, and no thin band of fog or sky colour on the left and right edges of each view (#25)
- Co-op: the "copy the GoldenEye key" objective (Bunker 1) no longer flips between completed and not completed every frame once one player has copied the key (#17)
- Multiplayer and Co-op on the Switch count only the controllers actually connected: four were always counted, so leaving a match asked for Start on four controllers; with one controller Co-op and Multiplayer are greyed out, and plugging or unplugging a controller is picked up at once
- Docking or undocking the console while the game runs no longer crashes it: the picture is resized at the start of a frame on the render thread, not behind its back
- Restart Game no longer shows at the bottom of the options menu on the Switch, where it could only quit the game
- About one launch in fifty no longer stops at boot on the Switch: the game's memory windows used to sit at fixed addresses that the system's own stack could land on, and they now move to a free spot chosen at each launch
- Co-op: a player who went down and was revived no longer ends the mission "killed in action"; a mission that ends while a player is still down now fails (killed in action, the next mission is not unlocked), so revive your partner before the exit (#20)

## [0.1.0] - 2026-11-11

### Added
- Project Dam: the GoldenEye 007 PC port as a Nintendo Switch homebrew (NRO), started by title takeover from the homebrew menu
- The release archive carries prepare-assets, which converts your own US (NTSC-U) .z64 ROM on a PC; no game content is included
- Renders at 1920x1080 docked and 1280x720 handheld, and follows the console when it is docked or undocked
- The buttons follow the labels printed on the controllers (A confirms, B goes back)
- Aim-mode sensitivity and precision curve for the right stick (Options), set for the short Joy-Con stick: fine aim for headshots, and a trigger press no longer moves the crosshair
- Quit Game in the options menu returns to the HOME menu
- After a crash, data/ge007-crash.log on the SD card records where it happened

### Changed
- Options that have no meaning on the console are hidden: window mode, fullscreen, resolution, MSAA, Restart Game and the update check

### Fixed
- No more stutter on zone changes: textures are no longer rebuilt every time the game reloads them
