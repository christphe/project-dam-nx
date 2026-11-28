-- mission.d.luau: the definition file of the mission script API (a Luau "declare" file).
-- It is a reference for modders and the source the editor reads for its pickers; it is NOT a runnable script
-- (port/mission/mission.cmake embeds it as g_missionApiDefs, not as a library script). The runtime is
-- port/mission/mission_lua.cpp; the full prose is the API comment in port/mission/mission_lua.h.
--
-- Editor-facing aliases: a script declares the properties it reads as a type at its top,
--     export type Props = { key: Item, message: string?, objective: Objective? }
-- and the editor offers the right picker for each alias (an item list, the mission's objectives, ...).
-- At run time they are all plain numbers or strings: type annotations are erased by the compiler.
--
-- Dialect: Luau 0.746 (the vendored Analysis the editor checks scripts with): `declare extern type X [extends Y] with ... end`,
-- `export type` for the aliases (a plain `type` is not visible to scripts), and overloads as an intersection of function types.

export type Item = number            -- an ITEM_IDS value (the editor shows the name)
export type Objective = string       -- the id of one of the mission's objectives
export type Door = number            -- a setup prop number of a door; in mission.json an original's number or an added door's uuid
export type Guard = number           -- a setup prop number of a guard; in mission.json an original's number or an added guard's uuid
export type Prop = number            -- a setup prop number of any object; in mission.json an original's number or an added object's uuid
export type Zone = string            -- the id of one of the mission's zones
export type Pad = number | string    -- an original pad number, or the id of one of the mission's pads

-- What wait(event(...)) returns, and the fields an event filter can match.
export type MissionEvent = {
    name: string,             -- "start" | "end" | "pickup" | "death" | "door" | "zone" | "objective" | "objective_failed"
                              -- | "player_death" | "player_hurt" | "player_revived" | "gunfire" | "alarm" | "guard_hurt"
                              -- | "guard_down" | "guard_revived"
    win: number?,             -- "end": 1 if the mission was won
    item: number?,            -- "pickup": the ITEM_IDS value
    player: number?,          -- "pickup", "zone", "player_*": 0-based player; "gunfire": the player who fired, if a player did
    prop: number?,            -- "death", "door", "guard_hurt", "guard_down", "guard_revived": the setup prop number; "gunfire": the guard who fired, if a setup guard did
    state: string?,           -- "door": "open" | "closed" | "moving"
    id: string?,              -- "zone": the zone id; "objective" / "objective_failed": the objective id
    health: number?,          -- "player_hurt": the player's health 0..1; "guard_hurt": the guard's health in game units
    armor: number?,           -- "player_hurt": the player's body armor 0..1 (the event fires when health or armor went down)
    x: number?,               -- "gunfire": where the shot starts
    y: number?,
    z: number?,
    -- #165 (the campaign API events)
    tag: number?,             -- "destroyed" | "activated" | "gadget_used" | "picked_up" | "door_refused": the object tag
    chr: number?,             -- the guard events, "anim_end", "guard_damaged": the guard's chr number
    what: string?,            -- "guard_witnessed": "hit" | "death"
    other: number?,           -- "guard_witnessed": the chr of the guard it saw
    mode: string?,            -- "guard_mode_end" (emitted by campaign/scripts/guard_ai): the mode that ended
    run: number?,             -- "guard_mode_end": which start() of that guard it was
}

export type EventFilter = { [string]: number | string | boolean }
export type EventDescriptor = { __event: string, filter: EventFilter }

-- spawn() returns a Task, on() a Connection.
export type Task = { cancel: (self: Task) -> () }              -- a cancelled task never resumes; cancelling a finished one does nothing
export type Connection = { off: (self: Connection) -> () }     -- unsubscribe (callbacks already running finish)

-- A target: a setup prop (door, guard, object), a pad, a zone, or the mission itself.
declare extern type Target with
    kind: string              -- "prop" | "pad" | "zone" | "mission"
    prop: number?             -- kind "prop": the setup prop number
    zone: string?             -- kind "zone": the zone id
    pad: (number | string)?   -- kind "pad": the pad number or id

    -- doors
    function lock(self)                       -- the door stays shut until unlock()
    function unlock(self, force: boolean?)    -- force: also remove the door's native lock (decoder / key) for the rest of the level
    function open(self)
    function close(self)
    function is_open(self): boolean
    -- guards
    function is_dead(self): boolean           -- true once the guard's chr is gone or dead
    function set_ai(self, list: number)       -- an AI list id
    function follow(self, player: number?)    -- walk to the nearest living player (or only `player`, 0-based) whenever
                                              -- farther than 200 units, run beyond 350, sprint beyond 1200. The guard
                                              -- gets a passive AI list so it neither flees nor attacks.
    function stop(self)                       -- stop issuing follow moves
    function surrender(self)                  -- hands up (an armed guard drops its weapons)
    function stand(self)                      -- stand still facing the nearest player, hands down
    function go_to(self, target: Target, speed: string?): boolean   -- to a prop's pad: "walk" | "run" (default) | "sprint";
                                              -- false: no route. :stop() a follow first or it takes the guard away
    function is_moving(self): boolean         -- still on its way after go_to / follow
    function face(self, target: Target): boolean   -- face a prop square on, set exactly on the spot go_to aims for when
                                              -- close; false: someone stands on that spot (call again once it is free)
    function keep_body(self)                  -- once dead, the body stays where it fell (the game fades bodies out)
    function aim_at(self, other: Target): boolean   -- an armed guard keeps another guard covered; false: unarmed.
                                              -- #165: on a body (bond_body), it raises its weapon at the target (scene)
    function shoot(self, other: Target)       -- fires at the other guard, which takes a hostage taker's hit
    function sees_player(self): boolean       -- the guard sees a living player
    function was_shot_at(self): boolean       -- a player's shot just missed it
    function guards_near(self, radius: number): number   -- living guards within radius (the target itself left out)
    function hits(self): number               -- how many times the guard has been hit
    -- objects
    function make_usable(self)                -- players can activate it (B / use), like the game's own terminals; on a GUARD:
                                              -- a player within 120 units facing it presses B, or stands within 90 units for 1.5 s
    function was_used(self): boolean          -- true once per activation by a player since the last call
    function play_anim(self, name: string, hold: boolean?): boolean   -- once: "keyboard_right_hand_tapping" | "keyboard_left_hand" |
                                              -- "keyboard_right_hand2" | "kneel_cough" | "kneel_cough2"; hold: the guard stays on the
                                              -- last frame until :stand() or another order
    function health(self): number             -- a guard's health in game units (maxdamage - damage); 0 when it is not there;
                                              -- #165: on a player, 0..1
    function max_health(self): number         -- its maxdamage
    function set_health(self, n: number)      -- damage = maxdamage - n (never below 0 damage: at most max_health)
    function set_max_health(self, n: number)  -- maxdamage = n, as given (the 007 health modifier is NOT applied)
    function set_revivable(self, on: boolean?)   -- goes down instead of dying ("guard_down"): lies on the floor, invincible,
                                              -- no order reaches it; a player within 120 units pressing B revives it in 10 s
                                              -- (walking away stops it): up with full health, "guard_revived", follows again
    function is_down(self): boolean           -- a revivable guard lying down
    function pos(self): (number?, number?, number?)   -- the target's world position now (a guard, player, prop, pad or zone);
                                              -- #165: under a scripted camera a player's y drops (no head animation):
                                              -- use player(n):eye() for the player's eye point
                                              -- nil, nil, nil when it has none (a guard not spawned, an original pad number)
    -- distances and presence (world units)
    function near(self, radius: number?, height: number?): boolean   -- a player within `radius` (a zone's own radius when
                                              -- omitted); with `height`: radius measured flat, and at most `height`
                                              -- above or below (a floor above or below does not count)
    function distance(self): number                     -- to the nearest living player
    function distance_to(self, other: Target): number   -- between two targets; 1e30 when one has no position
    -- dialogue
    function say(self, who: string, text: string, range: number?)   -- say(who, text) spoken from the target: a marker
                                              -- over it (above a guard's or a player's head, followed; a player never sees
                                              -- it over its own head) in every player's view, pinned to
                                              -- the edge when out of view, fainter and smaller further away; range: players
                                              -- farther away get neither the line nor the marker. No position: plain say()

    -- ================================================================== #165: the campaign API
    -- Everything below works on setup guards AND on guards spawned at run time (spawn_guard, clone): a guard target
    -- carries its chr number in `.chr`. "The player" is the guard's target player in co-op (the one it fights or
    -- last saw), else the nearest living one.
    chr: number?                              -- a guard: its chr number
    tag: number?                              -- an object: its object tag, when it has one

    -- presence
    function exists(self): boolean            -- a guard spawned and alive (a setup guard of another difficulty: false)
    function is_dying(self): boolean          -- in its death animation
    function remove(self)                     -- gone at once, drops nothing. On bond_body(): the third-person body is taken
                                              -- away (after fade(0, ..)); the player's weapon models come back into his
                                              -- hands a few ticks later, show(true) makes a new body
    function fade_out(self)                   -- fades away and is removed (drops what it holds)
    function teleport(self, pad: Pad): boolean   -- placed on the pad at once; false: the spot is blocked
    function set_hidden(self, on: boolean)    -- not drawn, not hit, not solid (a guard waiting for its entrance); during
                                              -- cinema.hide_all_chrs(true) a guard set_hidden(false) is shown again. On
                                              -- an object: not drawn and silent (an aircraft's engine and rotor too)
    function in_room_of(self, pad: Pad): boolean   -- a guard, object or player stands in the room of that pad

    -- settings (setup units; the difficulty modifiers apply as they do to the setup's own values unless raw)
    function set_stats(self, stats: GuardStats)
    function set_flag(self, flag: GuardFlag, on: boolean)
    function get_flag(self, flag: GuardFlag): boolean
    function give_weapon(self, item: Item, opts: { model: number?, left_hand: boolean?, concealed: boolean? }?): boolean
    function give_hat(self, model: number): boolean
    function drop_items(self)                 -- drops what it holds (a pickup on the floor)
    function remove_held_items(self)          -- what it holds vanishes
    function preset_pad(self): number?        -- the setup's own preset pad of the guard (the alarm raiser's alarm)
    function setup_ai(self): number?          -- the AI list id the setup gives this guard (0x0000..0x0011: a generic
                                              -- behaviour; 0x0401 and up: one of the level's own lists)

    -- movement
    function patrol(self, path: number)       -- walks patrol path `path` (the setup's path ids) until disturbed
    function is_patrolling(self): boolean
    function run_to(self, pad: Pad, speed: Speed?): boolean     -- to a pad; false: no route
    function run_to_player(self, speed: Speed?, player: number?): boolean
                                              -- to where the player is now; false: no route, hit, dying, or 10 guards
                                              -- already chase (the game's own cap)
    function face_player(self)
    function face_pad(self, pad: Pad)
    function halt(self)                       -- stops what it is doing and stands
    function flee_pad(self): number?          -- the pad the game would pick to flee the player
    function cover_pad(self): number?         -- a pad near the player the game would pick to take cover

    -- gestures and animation (a number is an animation id of the game; a name, the list of play_anim above)
    function look_around(self)                -- the startled look-around
    function point_at_player(self)            -- the "there he is" gesture
    function raise_arms(self)                 -- arms up briefly (not a surrender)
    function kneel(self)
    function idle_anim(self)                  -- one of the game's idle fidgets, at random
    function anim(self, anim: number | string, opts: AnimOpts?): boolean   -- play an animation (play_anim with all options)
    function anim_done(self): boolean         -- the last anim() finished (or was cut short)
    function stop_anim(self)

    -- combat (a target is a player, a guard or a pad; nil: the player)
    function fire_at(self, target: Target?, opts: { stance: Stance?, aim_only: boolean? }?): boolean
                                              -- #165: on a body (bond_body), it fires its weapon at the target
                                              -- (scene); the hit is the script's (target:hit(...))
                                              -- aims, then fires as soon as the aim is set; false: unarmed or no target
    function combat_move(self, kind: CombatMove): boolean   -- one move with the game's own conditions; false: not possible now
    function throw_grenade(self): boolean     -- false: no grenade, no throw line
    function grenade_chance(self): number     -- 0..100, the setup's grenade probability
    function hit(self, body_part: number, item: Item)   -- takes one hit as if shot with `item` (a scripted kill)
    function is_busy(self): boolean           -- running, firing, a combat move, a hit reaction, a scripted animation ...
    function is_idle(self): boolean           -- standing, patrolling, or in an interruptible idle animation
    function clone(self): Target?             -- sends one reinforcement (chr + 10000) like the game's clone guards; nil: cannot
    function press_alarm(self, pad: Pad): boolean   -- walks the last steps and presses the alarm switch on that pad;
                                              -- false: the switch is destroyed or missing, or the guard was stopped

    -- perception (native engine state; each "within" window is in seconds)
    function spotted_player(self): number?    -- the engine sight check this tick (vision cone, range, reaction roll); the
                                              -- player spotted, 0-based; spotting restarts the guard's alert window
    function line_to_player(self): number?    -- a clear walkable line to a player (no cone, no range); that player
    function on_screen(self): boolean         -- drawn in a player's view now
    function was_seen(self): boolean          -- has ever been drawn in a player's view
    function is_aimed_at(self): boolean       -- a player's crosshair is on it
    function alerted_within(self, seconds: number): boolean   -- saw a player, or was hit, that recently
    function heard_shot_within(self, seconds: number): boolean
    function near_misses(self): number        -- running total of player shots that passed it
    function witnessed(self): (string?, Target?)   -- "hit" | "death" of a guard it saw this tick (within 20 m, facing)
    function angle_to_player(self): number    -- degrees, 0 = facing the player, positive = the player on its left
    function was_damaged(self): boolean       -- took damage since the last call

    -- objects (by tag(n) or a setup prop)
    function is_destroyed(self): boolean
    function destroy(self)                    -- destroyed at once (its explosion, if it has one)
    function explode(self)                    -- an explosion at the object (it is destroyed if it can be)
    function set_invincible(self, on: boolean)   -- an object, a guard or a player cannot be damaged
    function set_collectable(self, on: boolean)  -- a pickup can (not) be picked up
    function move_to(self, pad: Pad)          -- the object is placed on the pad
    function activate(self)                   -- as if a player used it (terminals, switches)
    function show_screen(self, bank: number, screen: number?)   -- a TV / monitor object shows image bank `bank`
    function held_by(self, player: number?): boolean   -- a player carries this object (any player when omitted)
    function set_rotor(self, speed: number, ramp_seconds: number?)   -- a helicopter's rotor speed (0 = stopped)
    function drive(self, path: number, speed: number, ramp_seconds: number?)   -- a vehicle follows a path
    -- doors (open(), close(), lock(), unlock() above)
    function state(self): string              -- "closed" | "opening" | "open" | "closing"
    function open_now(self)                   -- snaps fully open, no animation
    function set_passable(self, on: boolean)  -- false: shut against everyone, guards and players, until set back

    -- players (player(n)) and the player's body in cinematics (bond_body(n))
    function armour(self): number             -- 0..1
    function held_item(self): Item            -- the item in the right hand: 1 (the fist) when the player fights bare-
                                              -- handed, 0 (no item) when the weapon is put away (a scene)
    function holds(self, item: Item): boolean -- in either hand
    function set_frozen(self, on: boolean)    -- no control (cinematics)
    function hide_weapon(self, on: boolean)
    function force_weapon(self, item: Item)   -- switch the player to that item
    function push(self, dx: number, dz: number)   -- moves the player by that much at once
    function in_cutscene(self): boolean
    function show(self, on: boolean)          -- the player's body is (not) drawn in third person, at once
    function fade(self, to: number, seconds: number?)   -- bond_body() only: the body's opacity goes from where it is
                                              -- to `to` (0 invisible .. 1 opaque) over `seconds` (none: at once);
                                              -- it stays in place, animating, holding what it holds. (fade_out is a
                                              -- guard's fade-and-remove: on a body it does nothing.)
    function hold_item(self, item: Item, hand: string?)   -- the body holds that item ("right" | "left") in the cinematic;
                                              -- item 0 (unarmed) empties that hand
                                              -- The body's hands ARE the player's: what it holds is what the player
                                              -- holds after the scene (empty a hand in a scene: the player plays
                                              -- unarmed until he is given a weapon back)
    function set_shadow(self, on: boolean)

    -- ================================================================== #165, second pass (docs/campaign/api-gaps.md)
    -- guards
    function held_weapon(self): Item?         -- the item in the guard's right hand, nil when it holds none
    function spot_near_player(self, player: number?): number?   -- a pad near the player the guard can reach (not cover)
    function drop_grenade(self): boolean      -- drops a live grenade at its feet
    function drop(self, object: Target): boolean   -- drops that one object it carries (a key, a briefcase)
    function give_object(self, object: Target): boolean   -- the guard carries that tagged object (a decoder in its hand)
    function set_flag_raw(self, bits: number, on: boolean)   -- a raw chr flag the specs keep as data (unknown meaning)
    -- objects
    function set_active(self, on: boolean)    -- a turret / drone gun on or off
    function set_damage(self, kind: string)   -- "all" (default) | "explosions" (only explosions hurt it) | "none"
    function same_room_as(self, other: Target): boolean   -- two targets (guards, objects, players) in one room
    -- doors
    function set_usable_by_guards(self, on: boolean)   -- guards may (not) open it
    -- players
    function kill(self)                       -- on a player: dies at once (a fall)
    function facing(self): number             -- on a player or a guard: the direction it faces, in the camera's degrees
                                              -- (yaw 0 = +z, positive turns left / towards +x)
    function eye(self): (number?, number?, number?)   -- on a player: its eye point (the intro swirl's origin)
    -- the third-person body in a scene (bond_body)
    function walk_to(self, pad: Pad, speed: Speed?): boolean   -- on a body: walks there (scene)
    function drop_item(self, hand: string?)   -- on a body: drops what that hand holds ("right" default | "left")
    function set_gravity(self, on: boolean)   -- on a body: false holds it in the air (a jump, a fall)
    function set_body_flags(self, flags: { hitbox_cull: boolean?, height_lock: boolean?, knockable: boolean? })
end

-- #165 types
export type Speed = string                 -- "walk" | "run" | "sprint"
export type Stance = string                -- "stand" | "kneel"
export type CombatMove = string            -- "roll_fire" | "walk_fire" | "run_fire" | "sidestep" | "side_hop" | "side_run"
export type GuardFlag = string             -- "invincible" | "no_autoaim" | "can_shoot_guards" | "civilian" | "dormant"
                                           -- | "no_blood" | "fast_sprint" | "no_shadow" | "sunglasses" | "clone_sender"
export type GuardStats = {
    health: number?, max_health: number?, armour: number?,       -- game units
    speed: number?, accuracy: number?, reaction: number?,        -- 0..100 ratings
    grenade_chance: number?,                                      -- 0..100
    flinch: (boolean | number)?,                                  -- false: hits do not interrupt it; a number 0..100:
                                                                  -- the chance a hit interrupts it
    raw: boolean?,                                                -- true: no difficulty modifier
    add_armour: number?,                                          -- armour added to what it has (game units)
    hearing: number?, vision: number?,                            -- the setup's hearing scale and vision range
    recovery: number?,                                            -- 0..100: how fast it recovers from a hit
}
export type AnimOpts = {
    from: number?, to: number?,           -- frames (default: the whole animation)
    speed: number?,                       -- 1 = normal
    hold: boolean?,                       -- stays on the last frame
    reverse: boolean?, loop: boolean?, blend: number?,   -- blend: seconds to blend in
    move: boolean?,                       -- false: the animation does not move the body (no root translation)
    idle_after: boolean?,                 -- back to the idle pose at the end (default true unless hold)
    sound: number?,                       -- a sound effect played with it
    interruptible: boolean?,              -- counts as idle while it plays (a fidget the guard drops when disturbed)
}
-- #165 the engine's one-update guard signals are EVENTS, queued and all delivered (several in one frame included):
--   "guard_spotted"   {chr, prop, player}         the engine's sight check noticed a player (on the edge from not
--                                                  seeing to seeing, not every frame it keeps seeing)
--   "guard_heard"     {chr, prop, player}         a player's shot was heard by the guard
--   "guard_near_miss" {chr, prop, player}         a player's shot passed the guard without hitting it
--   "guard_witnessed" {chr, prop, what, other}    it saw a guard hit (what = "hit") or killed ("death"); other: that
--                                                  guard's chr
-- Filter by guard with on("guard_spotted", fn, {chr = g.chr}). The queries spotted_player(), was_shot_at() and
-- witnessed() stay for a script that only wants "this tick"; they do not roll the sight check again.
-- grenade_chance() is 0..100 (the setup's 0..255 scaled).
-- is_moving() covers go_to, run_to, run_to_player and patrol steps; anim_done() turns true on the last frame of a
-- held animation; halt(), stop() and stop_anim() do nothing on a guard that is not moving / animating;
-- face_player() turns once to face the player (call it again to keep tracking).

-- The type of `target` per attachment kind (the editor binds the one that matches the entry; fields of that kind are not optional).
declare extern type MissionTarget extends Target with
end
declare extern type PropTarget extends Target with
    prop: number
end
declare extern type PadTarget extends Target with
    pad: number | string
end
declare extern type ZoneTarget extends Target with
    zone: string
end

-- Globals of every script.
declare target: Target        -- the script's own target (mission.json "target")
declare props: { [string]: any }   -- the script's values (mission.json "props"); type them with `export type Props`

declare function door(prop: number): Target
declare function guard(prop: number): Target
declare function player(n: number): Target         -- player n (0-based) as a target: :say(), :distance(), :near(), :distance_to()
declare function prop(n: number): Target           -- #165: any setup prop by its number (a vehicle, an aircraft, an object
                                                    -- with no tag); door() and guard() give the same target
declare function chr(n: number): Target            -- the guard with chr number n (the setup's own numbering); no prop when absent
declare function tag(n: number): Target            -- the object the object tag n names (doors, terminals, screens ...)
declare function text(id: number | string): string?   -- the game's text id (0x0400..0xB3FF, or 4 hex digits "2C04") in the
                                                    -- language played; nil when absent. Campaign scripts reference text by id only

declare function give(item: number, player: number?, opts: { silent: boolean? }?)   -- player: 0-based; omitted = every
                                                        -- player; #165 opts.silent: no pickup message
declare function take(item: number, player: number?)
declare function has(item: number, player: number?): boolean   -- omitted = any player

declare function message(text: string)                  -- on-screen text, bottom of the screen
declare function say(who: string, text: string)         -- dialogue line at the top of the screen: "who: text"

declare wait: ((seconds: number) -> ()) & ((ev: EventDescriptor) -> MissionEvent)   -- wait(seconds) sleeps; wait(event(...)) sleeps until it happens
declare function event(name: string, filter: EventFilter?): EventDescriptor
declare function clock(): number                         -- the mission time in seconds (stops while the game is paused)
declare function spawn(fn: () -> ()): Task              -- fn runs as a new coroutine (it may wait())
declare function on(name: string, fn: (ev: MissionEvent) -> (), filter: EventFilter?): Connection
                                                         -- fn runs as a new coroutine on each matching event (it may wait()); an error in
                                                         -- it is logged, not fatal
declare function require(name: string): any             -- a module <name>.luau of the mission / mod (or the built-in library), run once, cached;
                                                         -- name: letters, digits, _ and -. The module's top level must not wait()

declare function print(...: any)                        -- to the game log

declare objectives: {
    add: (id: string, text: string, difficulty: number?) -> (),
    complete: (id: Objective, quiet: boolean?) -> (),   -- quiet: no "completed" line, now or later (a protect objective)
    is_complete: (id: Objective) -> boolean,
    fail: (id: Objective) -> (),                        -- "Objective a: failed" on screen, shown as failed; no completing it
    is_failed: (id: Objective) -> boolean,
    all_complete: () -> boolean,                        -- of the difficulty being played
    reset: (id: Objective) -> (),                       -- back to incomplete (not failed): complete it again later
    set_failed: (id: Objective, on: boolean) -> (),     -- #165: a failure that can be lifted again (on = false)
    fail_all: () -> (),                                 -- #165: every objective of the difficulty fails
}

declare alarm: {
    trigger: () -> (),
    is_on: () -> boolean,                               -- #165
    stop: () -> (),                                     -- #165
}

declare mission: {
    win: () -> (),                                      -- end the level as a success
    fail: () -> (),                                     -- end the level as a failure
    difficulty: () -> number,                           -- 0 Agent, 1 Secret Agent, 2 00 Agent, 3 007; -1 without one
    -- #165
    finish: () -> (),                                   -- end the level now; won when every objective of the difficulty is
                                                        -- complete and none failed (the game's own end-of-level rule)
    civilians_killed: () -> number,                     -- guards with the "civilian" flag killed by players
    exiting: () -> boolean,                             -- the level's end sequence has started
    players: () -> number,                              -- #165: how many players this game has (1..4)
    killed_in_action: () -> (),                         -- #165: end the level as "killed in action" (a fall, a blast)
}

-- ====================================================================== #165: the campaign API (globals)

declare function pad(n: Pad): Target                    -- a pad as a target (kind "pad"): :pos(), :distance(), :near()
declare function bond_body(player: number?): Target     -- the player's third-person body in a cinematic (kind "body"):
                                                        -- :teleport, :anim, :anim_done, :hold_item, :show, :fade, :set_shadow
declare function spawn_guard(opts: {
    chr: number?,                 -- its chr number (a setup guard's slot is refused while that guard is alive)
    pad: Pad,                     -- where it appears
    body: number, head: number?,  -- the game's body / head numbers (docs/specs/characters.md)
    force: boolean?,              -- appear even in a player's view (default: only out of view)
    sunglasses: boolean?,
}): Target?                                             -- nil: refused (blocked slot, in view, no room)
declare function all_guards(): { Target }               -- every guard of the setup present on this difficulty (chr set)
declare function emit(name: string, fields: { [string]: number | string | boolean }?)
                                                        -- #165: raise an event of the scripts' own (on() / wait(event())
                                                        -- receive it next tick); names of the engine's events are refused
declare function give_object(object: Target, player: number?): boolean   -- a tagged object goes to that player's
                                                        -- inventory (every player when omitted): a key, a black box
declare function item_in_room(item: Item, pad: Pad): boolean   -- a thrown / planted item lies in the room of that pad
declare function explosions_around_players(on: boolean) -- endless explosions around every living player (a base
                                                        -- blowing up) until turned off or the level ends
declare function player_in_room(pad: Pad): number?      -- the first player (0-based) in the room of that pad, or nil
declare function item_landed(item: Item): (string?, number?)   -- a thrown item at rest: "stuck", tag of what it sticks to
                                                        -- | "loose" | nil while in flight or not thrown

declare hud: {
    top: (text: string) -> (),                          -- a line at the top of the screen (the game's own position)
    bottom: (text: string) -> (),                       -- a line at the bottom (as message())
    hide: (on: boolean) -> (),                          -- the whole HUD
}

declare countdown: {                                    -- the HUD countdown clock
    set: (seconds: number) -> (),
    start: () -> (),
    stop: () -> (),
    show: (on: boolean) -> (),
    left: () -> number,                                 -- seconds left
}

declare sound: {
    play: (id: number, opts: { from: Target?, channel: number? }?) -> number,   -- a sound effect id; from: emitted
                                                        -- from that target; returns the channel it plays on
    stop: (channel: number) -> (),
    volume: (channel: number, volume: number, fade_seconds: number?) -> (),     -- volume 0..1
}

declare music: {
    play_extra: (slot: number, opts: { stop_after: number?, total: number?, fade: number? }?) -> (),
                                                        -- the level's extra music track `slot` (alarm, chase ...)
    stop_extra: (slot: number) -> (),
}

declare fog: {
    next: (slow: boolean?) -> (),                       -- the level's next fog setting (the game's own fog table)
}

declare screen: {
    fade_out: (seconds: number?) -> (),                 -- to black
    fade_in: (seconds: number?) -> (),
    fading: () -> boolean,
}

declare cinema: {
    begin: (opts: { keep_hud: boolean?, keep_top_text: boolean?, keep_bottom_text: boolean?,
                    keep_countdown: boolean?, environment: ("intro" | "level")? }?) -> (),
                                                        -- environment "intro": the level's intro environment (its own
                                                        -- fog and view distance, made for the intro's fixed shots)
                                                        -- until the next begin or finish; "level" (the default): the
                                                        -- normal one. A level without an intro environment keeps its own
                                                        -- a scripted scene: every player frozen and invulnerable, no
                                                        -- pickups, HUD hidden, guards cannot see players, and the
                                                        -- mission clock (the end-of-level time) stopped until finish()
    finish: () -> (),                                   -- control returns to the players
    hide_all_chrs: (on: boolean) -> (),                 -- every guard hidden (the scene shows only who it brings back)
    skipped: () -> boolean,                             -- a player pressed a button since begin()
    in_intro: () -> boolean,                            -- the level's intro (start cameras) is still running
}

-- A scripted camera position: a world point or a target, and where it looks (yaw / pitch in degrees, or a target).
export type CameraPoint = {
    pos: Target | { number },     -- a target (pad, guard, object, body) or {x, y, z} in world units
    offset: { number }?,          -- {x, y, z} added to pos (a target's position moves with it)
    yaw: number?, pitch: number?, -- degrees; yaw 0 = +z, positive turns left; pitch positive looks up
    look_at: Target?,             -- instead of yaw / pitch: keep looking at this target (followed while it moves)
    fov: number?,                 -- vertical field of view in degrees (default: the game's)
    speed: number?,               -- units per second to reach THIS point from the previous one, or
    time: number?,                -- seconds to reach it (wins over speed); 0 = cut
    hold: number?,                -- seconds to stay here before moving on
    ease: string?,                -- "linear" | "in" | "out" | "in_out" (default) for this leg
    tension: number?,             -- the curve's tension on the leg that STARTS here (smooth moves; 0.5 = Catmull-Rom,
                                  -- the default): its tangents are tension * (next - previous)
    control: boolean?,            -- on the first or last point of a move: it only shapes the first / last leg's curve
                                  -- (the spline's outer neighbour) and is never travelled
    pad: Pad?,                    -- the room hint: a pad in the room the camera stands in at this point and on the leg
                                  -- that starts here (what the renderer draws from). Without it, the floor under the
                                  -- camera, or with none under it the first floor it looks at. Give it for a camera
                                  -- outside the level whose view needs a precise room
}

declare camera: {
    fixed: (at: Target, opts: { look_at: Target? }?) -> (),        -- a fixed camera (a camera object by tag, a pad)
    set: (point: CameraPoint) -> (),                    -- the scene camera jumps to that point (a cut)
    move: (points: { CameraPoint }, opts: { loop: boolean?, smooth: boolean? }?) -> (),
                                                        -- a travelling through the waypoints in order, each leg at its
                                                        -- speed / time and ease; smooth: a curve through the points
                                                        -- (default) instead of straight legs; yaw / pitch / fov
                                                        -- interpolated, look_at tracked. Returns at once
    moving: () -> boolean,                              -- a move() is still running
    wait: () -> (),                                     -- sleep until the move() ends (a script, not a module top level)
    stop: () -> (),                                     -- the travelling stops where it is
    orbit: (opts: { centre: Target | { Target }, distance: number, height: number, look_height: number?,
                    start_angle: number?, speed: number? }) -> (),
                                                        -- circles the centre (several targets: their midpoint);
                                                        -- start_angle and speed in degrees (per second); look_height:
                                                        -- added to the centre's height for the look point
    path: (anim: number) -> (),                         -- a camera travelling: the game's camera animation `anim`
    reset: () -> (),                                    -- back to the players' own views
}
-- Every player's view shows the scene camera while one is set (set, move, orbit, fixed, path) inside a cinema scene;
-- outside a scene, the camera calls show it to every player too, until camera.reset().

-- One line of credits. A string is a plain line. A table is one of:
--   { text = string, style = "text" | "heading" | "title"?, align = "center" | "left" | "right"? }   a styled line
--   { left = string?, right = string? }                                                              two columns (a role and a name)
--   { gap = number }                                                                                  that many empty lines
export type CreditsLine = string
    | { text: string, style: ("text" | "heading" | "title")?, align: ("center" | "left" | "right")? }
    | { left: string?, right: string? }
    | { gap: number }

declare credits: {
    roll: () -> (),                                     -- the game's own end credits (the Cuba level only)
    show: (def: { lines: { CreditsLine }, speed: number?, backdrop: ("world" | "black")?, align: ("center" | "left" | "right")? }) -> (),
                                                        -- your own credits, scrolling up over every player's view.
                                                        -- speed: lines per second (default 1.5; the screen is 24 lines
                                                        -- high); backdrop: "world" (default, the game stays visible) or
                                                        -- "black"; align: the default alignment of the plain lines
                                                        -- ("center" by default). At most 400 lines, 95 bytes each.
    stop: () -> (),                                     -- ends the scripted credits at once (done() is then true)
    done: () -> boolean,                                -- the credits (roll or show) ended
}

-- #165 events (raised by the host; wait(event(name, filter)) / on(name, fn, filter)):
--   "intro_done" {}                         the intro cameras ended, players have control
--   "destroyed" {tag, prop}                 an object was destroyed
--   "activated" {tag, prop, player}         a player used an object
--   "gadget_used" {item, tag, player}       a gadget was used on an object (a key on a door, a bug on a terminal ...)
--   "picked_up" {tag, prop, item, player}   a player picked an object up
--   "door_refused" {prop, tag, player}      a locked door refused a player
--   "anim_end" {chr}                        a guard's anim() finished
--   "guard_damaged" {chr, prop}             any guard (spawned ones too) took damage
--   "guard_died" {chr, prop, player}        any guard (spawned ones too) died; player: who killed it, when a player did
--   "intro_swirl" {}                        the intro's swirl around the player starts (the last intro shot)
--   "key_copied" {player, tag?}             the game's key copier made a copy (Bunker 1); the game keeps no tag for it
--   "photographed" {tag, player}            a player took a photo of that object with the camera
-- give(item, player, {silent = true}): no pickup message. countdown.set() on a running countdown keeps it running.
-- set_rotor(speed, ramp): speed in the game's rotor units, the setup's own values (0 = stopped).
-- anim() on an object target (an aircraft, the shuttle): the object's own model animation (id, opts.from = frame).
-- During a cinema scene guards neither see nor hear players (no guard_spotted / guard_heard, line_to_player nil).
-- A mission pad's string id is accepted wherever a Pad is (in_room_of, move_to, teleport, camera points ...).
