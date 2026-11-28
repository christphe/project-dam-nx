---
title: Co-op
sources:
  - port/src/coop.c
  - port/src/assign.c
  - src/game/player.h
  - port/src/input.c
verified: e2fda9e2
---

# Co-op

The solo campaign for 2, 3 or 4 players in split screen, on the Switch and the PC (#75, #84).
Read this before changing anything that depends on the player count.

## For the player

- **Co-op** on the mode menu asks for the player count (a window drawn by the options menu),
  then for any missing controllers, then opens the character select. Player 1 is Bond; the others
  pick a character on their own device.
- Two halves or four quarters, like multiplayer. A watch or a cutscene goes full screen for that
  player.
- **Down and revive:** a standing player near a downed one revives them with A (10 s), one revive
  at a time per reviver. The mission fails when everyone is down; a player still down at the exit
  counts as KIA.
- **Pickups:** each player gets their own copy; the item goes once all N have taken it.
- **Co-op enemy health** (`Game.CoopEnemyHealth`): a bonus per extra player, `1 + k·(N−1)` with k
  = 0, 0.5 or 1.

## How it is wired

The engine has no mode flag: "mission or versus" is inferred from `getPlayerCount()`. Co-op runs
the mission code with more than one player, so the rule sites in `src/game` ask
`GE_RULE_SOLO()` / `GE_RULE_MULTI()` (macros in `src/game/player.h`, mapped to `coopIsMission()` /
`coopRuleMulti()` in a port build) instead of the raw count. Everything else lives in
`port/src/coop.c`; `src/game` only carries one-line hooks.

When co-op is off, `coopActive()` is 0 and every hook returns what the original expression gave.

| Hook | Job |
|---|---|
| `coopStageNumPlayers` | Stage start (`src/boss.c`) asks for the chosen 2 to 4 players. |
| `coopAiTick` | A guard's AI tick runs with the nearest living player as "Bond". |
| `coopSimOwner` | `chrTick` runs once per view (props tick inside the render loop): the simulation part runs for one view per frame only. |
| `GE_SIM_OWNER` | The other views' `chrTick` pass restores the guard's `prevpos`, or the next waypoint arrival test sees no movement. |
| `coopBodySlots` | One extra chr and animated-model slot per player: each keeps a body chr all mission, which solo's 10 spares do not count. |
| `coopSpawnOffset` | Players 2..N spawn around the start pad, at distinct spots. |
| `coopFrameTick` | Enemy health, down and revive. |
| `coopPickupOp` / `coopPickupTaken` | Per-player copies of pickups. |
| `coopWatchStart` / `coopWatchLock` | The watch of one player while the others play on. |

## Controllers

`port/src/assign.c` asks who plays with what when a multiplayer game (co-op or versus) needs more
devices than it has:

- **Switch:** the system controller applet (`nxConfigureInput`).
- **PC:** a "press a button" screen, drawn by the menu: a pad button takes that pad, a key takes
  the keyboard and mouse (player 1 only). Saved in the ini (`Input.Player<N>Pad`,
  `Input.P1Type`); skipped while it still holds; Esc skips it for this player count.

The first menu's **Controllers** button opens the same screen at any time.

## Testing

- `GE_COOP=1` (2 players), `GE_COOP=3`, `GE_COOP=4` start co-op without the menu.
- Always pair it with `GE_PADS=N`: the Switch only seats connected controllers, so with one pad
  co-op would start with a single player. `GE_PADS=1..4` forces the first N seats.
- `GE_COOP_SPLIT`, `GE_COOP_CINEFULL=0`: split-screen and cutscene variants, for checks.
- Example: `python tools/dev.py nx --level dam --env GE_COOP=1 --env GE_PADS=2` ([dev-py.md](dev-py.md)).
