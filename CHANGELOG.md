# Changelog

## [Unreleased] - minor

### Added
- Look precision and Turn acceleration on the Controller page: a steeper stick curve (3.0 by default, the original game uses 2.0) for fine aim, and a turn that speeds up when the stick is held at the edge (Low by default) for wide turns
- Centered + sway aim style, the default on the Switch: the crosshair stays at the centre and the weapon trails the camera when it turns, then settles back at the weapon's own pace
- Aim sensitivity on the Controller page: how fast the right stick turns while aiming (0.5x by default)

### Changed
- The right stick deadzone is 10% by default on the Switch instead of 25%, so small aim movements register; settings that still had the old default move to it once
- The options no longer show the Mouse and Key Bindings pages, which need a mouse or a keyboard; Aim sensitivity replaces the aim-mode sensitivity and precision curve options
- Aim style moves to the Controller page; the Centered styles turn the camera with the right stick, and the view stays where it is when the stick is released (Original keeps the N64 crosshair)
- Faster rendering of busy scenes: vertex data goes to the GPU through one streaming buffer instead of a new buffer per draw, about three times cheaper per draw on the console

### Fixed
- Restart Game no longer shows at the bottom of the options menu on the Switch, where it could only quit the game

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
