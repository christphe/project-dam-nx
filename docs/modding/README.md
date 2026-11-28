# Modding: custom missions and scripts

A **mod** is a campaign: an ordered list of missions, each a small delta over one of the original solo levels
(guards moved, added or removed, objects added, objectives, zones, Luau scripts). A mod holds numbers, text and pictures
of its own, never game data, so it is safe to share. The format is documented in `port/mission/mission.h`.

## Installing a mod

A mod is shipped as a **`.pmod`** file: a plain, uncompressed `tar` (ustar) of the mod's source folder. Put it next to the
game's `data/` folder, under `mods/`:

```
<game>/mods/formula.pmod
```

(On the Switch: `sdmc:/switch/project-dam/mods/`.) The mod then shows on the game's Mods screen and its missions fill the
mission select. `mods/` holds only `.pmod` entries: anything else in it is ignored (the log names it). The file name
without `.pmod` is the mod's folder name (`--mod formula` starts the game on `formula.pmod`); progress is kept per mod by the
`id` in `campaign.json`.

The mod's *source* (`campaign.json`, `<mission>/mission.json`, `<mission>/thumb.png`, `cover.png`, `scripts/*.luau`,
anything else) lives in a folder of its own, outside `mods/`, usually its own git repository. The editor writes the
package: *Mod > Publish...* (**Publish** writes `<mod>/dist/<mod>.pmod` to share, **Deploy** writes it straight into the game's
`mods/`), and **Play** repackages the mod into `mods/` before it starts the game. Dotfiles (`.git`, `.gitignore`) and a
top-level `dist/` are left out of the package; a new mod gets a `.gitignore` with `dist/`.

To look inside a package: `tar -tf formula.pmod` lists it and `tar -xf formula.pmod` unpacks it. An unpacked folder named
`formula.pmod/` placed in `mods/` works exactly like the file (handy while developing without the editor).

To try mods on a Switch over nxlink without touching its SD card, pack them into the test NRO's romfs:
[docs/wiki/testing-on-switch.md](../wiki/testing-on-switch.md) §Mods in an nxlink build.

## The Formula sample

`docs/modding/samples/formula/` is a complete mod source. To play it, pack it with
`tar -cf mods/formula.pmod -C docs/modding/samples/formula .` (or copy the folder to `mods/formula.pmod/`); to edit it, open
it in the editor
(**project-dam-editor**: open the mod folder from its Mod tab, see `editor/README.md`; the editor shows the Facility from a
level export it makes from your own game data). It is a Facility mission with an ally to protect:

- **Agent:** Yuri Kalinin, a programmer, hides behind a washroom cubicle door (`hideDoor`, setup door 32). Opening it
  finds him: he turns to the player, hands up, then follows. He is added on the mission pad `p3` at (-663, -45, -546),
  present on Agent only (`"difficulties": [0]`). A door's pad in the setup is a *bound* pad (index without the +10000 of
  the pad numbers objects use), which is why a door is not found by looking for pads near a position.
- **Secret Agent and up:** he has been caught. A second Kalinin (`kalininHostage`) stands hands up in a room past door 56
  (`hostageDoor`), a guard (`captor`) behind him. Near that door the player overhears the captor questioning him; opening
  it, Kalinin begs for his life. The captor shoots him, as Frigate's hostage takers do, when he has seen a player for 2.5 s
  (2 s from 00 Agent), when a shot misses him, or as soon as he recovers from a hit. Kill the captor first: once no guard
  is left near Kalinin he thanks the player and follows.
- The Facility's own security terminals are removed and replaced by terminals of the mission's own that are linked to
  nothing. A player uses a terminal (the action button) to call Kalinin over: he stops following, walks to the spot in
  front of it, turns to it and types (`keyboard_right_hand_tapping`, 3 s), then the door opens and he follows again. The
  script uses `door:unlock(true)` so the door's native lock goes too and it can be used again after it closes.
- A fourth computer (on `p1`) gives the formulas: Kalinin downloads them and the script `give`s the item.
- The exit zone sits on the level's own exit (room 69), past the computer: getting the formulas does not end the mission,
  reaching the exit with Kalinin alive does, once the objectives of the difficulty are done. If he dies, "Keep Kalinin
  alive" fails (the game's own "objective failed" line), his body stays, the terminals tell the player they are useless,
  and reaching the exit ends the mission as failed. The last door before the exit (102) stays the game's own: on Secret
  Agent and up it needs Dr. Doak's decoder.

Difficulty (`mission.difficulty()`): **Agent** keep Kalinin alive and leave. **Secret Agent** also download the formulas
(and override the lab and east doors). **00 Agent / 007** also find today's password list, a pickup in the laboratory
(object on `p4`, present only on those difficulties through the object op's `"difficulties": [2, 3]`); the computer
refuses until a player has it. Objectives carry their lowest difficulty in `mission.json`.

How it is built:

| Part | Where |
|---|---|
| Removed terminals (props 64, 70, 78) and Trevelyan (guard 382) | `objects` / `guards` `remove` ops in `mission.json` |
| New terminals, the computer, the password list | `objects` `add` ops (the terminals are type 10 monitors, model 337, on the removed ones' pads) |
| Kalinin | `guards` `add` ops with a body (33), a head (50), sunglasses (`bitflags` 1) and a passive AI list: one per difficulty set |
| The captor | a `guards` `add` op (body 2, the level's guard weapon), Secret Agent and up |
| Pictures | `cover.png` (the mod on the Mods screen) and `formula/thumb.png` (the mission), 320x240 |
| The story | `formula/scripts/formula.luau`, run on the zone `exit` |

**Props a mission adds have a uuid.** Every guard or object `add` op carries a `"uuid"` (required: lowercase RFC 4122 text, 36
characters, unique in the mission; the editor makes a random v4 one) and an optional `"name"` (display only). A script
references a prop the mission adds by that uuid and never by a number: the setup number of an added prop is only
the level's prop count plus its position in the add list, so adding one guard would silently shift every later reference.
A `Guard` / `Door` / `Prop` property of a script is an **original level prop's number** (door1 = 63, hideDoor = 32) **or the
uuid string of an added prop**, and a script target is `{ "kind": "prop", "prop": 63 }` or `{ "kind": "prop", "uuid": "..." }`.
A number at or past the level's own prop count is an error (the editor reports it, the game logs it and treats the prop as
missing). When the game builds the level's setup it numbers the added records (the Facility has 523 props, so the first added guard present
on the difficulty played becomes 523, and so on: the numbers differ between difficulties, the uuids do not) and resolves every uuid to that number, logging one `missions:` line per
resolved uuid; the scripts still see plain numbers (`Guard = number`). A prop that is absent on the chosen difficulty
(an add op with `"difficulties"`) resolves to -1: scripts treat it as a missing prop. In the sample, `kalinin`,
`terminal1..3` and `computer` are uuids and `door1..3`, `hideDoor` stay numbers (original doors).

## Pictures

A mission's `thumb.png` is its image in the mission grid; the mod's `cover.png` is its picture on the Mods screen (without
it, the first mission's thumb). Both are 320x240 RGB PNGs, made in the editor (Mission tab > Thumbnail > Capture or Import
picture; Mod tab > Choose picture). The editor writes them compressed (per-row PNG filters, zlib level 9): a capture is
a few tens of KB.

## Textures

A mod can give the level's walls, floors and ceilings pictures of its own: `textures/<n>.png` in the mod's folder replaces
texture number `n` (0 to 4095, the game's own numbering) in every mission of the mod.

**Only pictures you made yourself.** A texture taken from the game, even retouched, is game data: it never goes in a mod.

- **In the editor:** Mod tab > Textures. *Pick a surface* and click a wall in the view to get its texture number (or type
  it), then *Replace...* copies a PNG of yours as `textures/<n>.png`; *Remove* takes it out. The list shows the mod's
  replacements; the view draws them at once. Over MCP: `textures` (the level's textures, where each drawn picture comes
  from, the rooms using it), `mod_set_texture` (`texnum`, `png`, or `remove`).
- **The picture:** a PNG, 8-bit (grey, grey + alpha, RGB, RGBA or palette), not interlaced, up to 4096 pixels on a side.
  Any size works: the texture coordinates are a fraction of the picture, so a picture of the same proportions as the
  original lays out the same way, and a larger one only adds detail. Keep the alpha of a see-through texture (a fence, a
  grille): an opaque picture fills the holes.
- **Where it shows:** in the editor's view, and in the game while one of the mod's missions is played, over the player's
  texture pack (below). *Check mod* flags a file in `textures/` that is not `<number>.png` or not a readable PNG.

## Asset packs (texture packs)

An asset pack replaces the game's textures everywhere, for every level and mod (other kinds of assets follow: `docs/specs/asset-packs.md`).
It is a `.passets` file (`.ptexpack`, the older name, still works), a plain uncompressed `tar` like a `.pmod`, holding:

```
pack.json              optional: {"name": "...", "author": "...", "description": "..."}
textures/<n>.png       the picture of texture n (same rules as a mod's textures above)
```

Put it in `assetpacks/` next to the game's `data/` folder (on the Switch `sdmc:/switch/project-dam/assetpacks/`; the older
`texturepacks/` is still read) and enable it in the game's menu, Video > Asset packs. Any number of packs can be enabled:
where two have the same texture, the one higher in the order wins, and every pack may be partial. The change shows at once,
also in a mission. A mod's own textures win over the pack's. An unpacked folder `X.passets/` works like the file. To make one: `tar -cf mine.passets pack.json
textures` from the pack's folder.

The editor draws a level with the pack of your choice (Mod tab > Textures, or Tools > Settings), to see how a mission looks
with the pack its players use; the mod's own textures are drawn over the pack's. Over MCP: `set_texture_pack`.

## Scripts

A script is a Luau file (`<mission>/scripts/<name>.luau`, `<mod>/scripts/<name>.luau`, or a built-in library script).
`mission.json` lists the scripts of a mission; each entry names the script (`use`), a `target` (a door or guard prop, a
pad, a zone, or the mission itself) and `props`, the values the script reads.

The whole API (globals, target methods, event names) is declared in **`port/mission/lib/mission.d.luau`**, a Luau
definition file. It is the reference; the prose version is the comment at the top of `port/mission/mission_lua.h`. In
short: `give` / `take` / `has` for items, `message` and `say` for text, `wait(seconds)` and `wait(event(...))`,
`objectives.*` (`complete`, `fail`), `alarm.trigger()`, `mission.win()` / `mission.fail()`, and targets with `:lock()`,
`:unlock([force])`, `:open()`, `:near([radius] [, height])`, `:distance()`, `:distance_to(other)`, `:is_dead()`, `:set_ai()`,
and `:say(who, text [, range])`: the line of `say`, with a marker over the target (above a guard's head) in every
player's view (`player(n):say(...)` makes a player speak: the others see the marker over it), pinned to the edge of the view when the speaker is out of it, fainter and smaller the further away; with
a range, players farther away see neither the line nor the marker.
Guards also have `:follow()` / `:stop()`, `:go_to(target)` / `:is_moving()`, `:face(target)`, `:play_anim(name)`,
`:surrender()` / `:stand()`, `:aim_at(other)` / `:shoot(other)`, `:sees_player()`, `:was_shot_at()`, `:hits()`,
`:guards_near(radius)` and `:keep_body()`; objects have `:make_usable()` / `:was_used()` (a terminal a player activates).

Each script runs as a coroutine in a sandbox (no `io`, `os`, `debug`), so a script waits instead of looping: use
`wait(0.25)` in a polling loop or `wait(event(...))`.

A script's life, like a BepInEx / Unity plugin's `Awake` then `Start`:

1. **Load**: its top level runs up to its first `wait()`, `require`s included, while the level loads.
2. **`start()`**: if the script defines a global `function start()`, the engine calls it once every script of the
   mission is loaded and the level's guards and objects exist (it may `wait()`). Set-up goes there.
3. **Frames**: then everything is frame time: the waits that end, the event callbacks, the spawned tasks.

Time budgets (a first version: nothing is ever cut, going over one logs a warning and the script goes on): 10 s for a
script's load, 10 s for its `start()`, 10 s for a `require`d module's top level, and 2 ms per frame for all the scripts
together. A frame-time resume still running after 1 s is named in the log: a loop that never waits freezes the game.

### Credits

`credits.show(def)` scrolls your own credits up the screen, over every player's view, until the last line has left (it
works in any level; `credits.roll()` is the game's own end credits and only works in the Cuba level):

```lua
credits.show({
    speed = 1.5,           -- lines per second (the screen is 24 lines high); 0.2 to 10
    backdrop = "black",    -- "world" (the default: the game stays visible behind) or "black"
    align = "center",      -- the default alignment of the plain lines: "center", "left" or "right"
    lines = {
        { text = "Operation Halcyon", style = "title" },
        { gap = 2 },                                        -- two empty lines
        { text = "Cast", style = "heading" },
        { left = "Agent", right = "Alice Example" },        -- two columns: a role and a name
        "A plain line",
        { text = "Thanks to everyone", align = "right" },
    },
})
while not credits.done() do wait(0.25) end
```

`credits.stop()` ends them at once; `credits.done()` is true when they ended (or were stopped). Lines are measured in
lines of the screen, so the credits look the same at any resolution; a text longer than 95 bytes is cut, and a list holds
400 lines at most (a longer one is a script error). The credits stand still while the game is paused. Text is yours, in
the language you write it in: the host adds nothing.

### Events, tasks and modules

- `on(name, fn [, filter]) -> Connection` calls `fn(event)` every time a matching event is raised, on the game tick; each call
  is a new coroutine (like `spawn`), so `fn` may `wait()`. An error in `fn` is logged, not fatal. `Connection:off()`
  unsubscribes. `wait(event(name [, filter]))` works as before; the filter is a table of `field = value` that must all match.
- `spawn(fn) -> Task` runs `fn` as a coroutine of its own (it may `wait()`); `Task:cancel()` stops it (a finished task: nothing).
  `clock()` is the mission time in seconds (it stops while the game is paused).
- `require(name)` loads the module `<name>.luau` from the mission folder, its `scripts/`, the mod's `scripts/` or the built-in
  library, runs it once per mission in an environment of its own over the mission globals (not the script's `target` /
  `props`), and returns what it returns (cached; `true` when it returns nothing). The name is letters, digits, `_` and `-`
  only, so `require("jokes_" .. level)` works. A module's top level must not `wait()`; its functions may when a script calls them.
- Events: `start`, `end {win}`, `pickup {item, player}`, `death {prop}` (a guard), `door {prop, state}`, `zone {id, player}`,
  `objective {id}`, `objective_failed {id}`, plus the ones the game side raises each frame:
  `player_death {player}` (a player's bond died), `player_hurt {player, health, armor}` (health or body armor went down;
  both 0..1), `player_revived {player}` (a co-op revive), `gunfire {x, y, z, player?, prop?}` (a shot starts at x y z:
  `player` is 0-based when a player fired, `prop` the setup prop when a setup guard did; at most one event per shooter per
  0.25 s), `alarm {}` (the alarm went from off to on), `guard_hurt {prop, health}` (a setup guard took damage; `health` in
  game units), `guard_down {prop}` / `guard_revived {prop}` (a revivable guard went down instead of dying / a player
  revived it).
- Guards: `:health()` / `:max_health()` (game units: maxdamage minus damage, maxdamage), `:set_health(n)`,
  `:set_max_health(n)` (written as given: the 007-difficulty health modifier a setup applies is NOT applied),
  `:set_revivable()`: when the game would kill the guard it goes down instead. It lies on the floor, invincible and not
  auto-aimed, and no order reaches it (a `:follow()` resumes by itself once it is up). A living player within 120 units
  pressing B revives it in 10 s, with the co-op revive's countdown (walking away stops it); it gets up with full health.
  `:is_down()` is true while it lies there,
  `:play_anim(name, hold)` with `"kneel_cough"` / `"kneel_cough2"` as well (`hold = true`: the guard stays on the last frame until
  `:stand()` or another order), `:pos()`. `:make_usable()` / `:was_used()` also work on a guard: a player within 120 units
  facing it presses the game's B button, or stands within 90 units for 1.5 s, and `:was_used()` is true once.
  `objectives.reset(id)` puts an objective back to incomplete (not failed).

### Keeping the level's objectives

`mission.json` has an optional top-level `"keep_level_objectives": true` (the editor's Mission tab: *Keep the level's
objectives*). The level then keeps its own briefing, objectives, objective HUD lines and exit: the mission's `"objectives"` (and
`objectives.add`) are appended after the level's on the briefing and pause screens (lettering continues), and the level's exit ends
the mission only once the mission's objectives of the difficulty played are complete too. `mission.win()` / `mission.fail()` still
work. Without the flag the mission's objectives replace the level's, as before.

### Typed props

A script declares what it reads at its top, as a Luau type:

```lua
export type Props = { key: Item, message: string?, objective: Objective? }
```

The aliases `Item`, `Objective`, `Door`, `Guard`, `Prop`, `Zone` and `Pad` are defined in `mission.d.luau`; they are plain
numbers and strings at run time (the compiler erases type annotations), and tell the editor which picker to show for
each property. Read the values through the `props` global as before; `local P = props :: Props` gives a typed view.

### Library scripts (`port/mission/lib/`)

| Script | Does | Props |
|---|---|---|
| `key_door` | a door stays locked until a player has the key item | `key: Item`, `message: string?`, `objective: Objective?` |
| `give_item` | gives an item when a player reaches the target | `item: Item`, `message: string?`, `radius: number?` |
| `exit_unlock` | once every objective is complete, unlocks a door or wins at a zone | `message: string?` |
| `pickup_objective` | completes an objective when a player picks an item up | `item: Item`, `objective: Objective`, `message: string?` |

Your own scripts can use the same names as library scripts; the mission's folder wins, then the mod's `scripts/`, then
the library.
