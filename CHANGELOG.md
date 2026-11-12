# Changelog

All notable changes to Project Dam, the Nintendo Switch port of the
[GoldenEye 007 PC port](https://github.com/jkdansereau/goldeneye-pc-port).
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/). Releases are
tagged `project-dam-vX.Y.Z`; the `vX.Y.Z` tags are upstream's.

Changes to the game itself (fixes, options, the PC builds) come from upstream
and are listed in its release notes; this version is built on upstream v0.6.0.

## [Unreleased] - minor

### Changed
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
