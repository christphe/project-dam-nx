--!nonstrict
-- intro_min: the minimal solo intro, for a mission with "level_intro": false that has no intro of its own.
--
--     require("intro_min").play({ level = P.level })        -- blocks until the players have control
--
-- It is the campaign's intro (campaign/scripts/intro.luau) without the fixed shot and the long swirl: a 3 s shot that
-- starts in front of Bond's eye, looking at him, turns round him and ends on his eyes; Bond's third-person body is
-- shown holding the start weapon and fades out over the last 0.5 s. Then the hand-over, in the campaign's order: the
-- body is removed (that frees the player's hands), camera.reset(), the start weapon is forced, cinema.finish()
-- (control back, the mission clock runs).
--
-- opts.level   the level name ("dam"...): its start weapon is looked up in M.START_WEAPON
-- opts.weapon  the start weapon item, instead (1 = the fist, bare-handed: nothing is forced)
--
-- Co-op: the shot follows the first living player; the others stay frozen until the hand-over, like the campaign's.

local M = {}

-- The first start item of each solo level (campaign/<level>/scripts/<level>_intro_data.luau).
M.START_WEAPON = {
    dam = 5, facility = 5, runway = 5, surface1 = 5, bunker1 = 5, silo = 5, frigate = 11, surface2 = 5,
    bunker2 = 1, statue = 4, archives = 1, streets = 4, depot = 4, train = 4, jungle = 4, control = 4,
    caverns = 9, cradle = 4, aztec = 4, egypt = 4,
}

local FOV = 60
local LOOK_AHEAD = 40     -- the last points look at the eye pushed 40 units forward (the first person view)
local BODY_FADE = 0.5

-- The shot: offsets from the player's eye in the player's frame (x left, y up, z forward), the time to reach each
-- point from the previous one, and whether it looks at the eye itself (false: at the point 40 units ahead of it).
local SHOT = {
    { off = { 0, 15, 190 },  time = 0,   at_eye = true  },    -- in front of Bond, looking at him
    { off = { 170, 40, 90 }, time = 1.1, at_eye = true  },    -- round to his left
    { off = { 0, 30, -130 }, time = 1.1, at_eye = false },    -- behind him
    { off = { 0, 0, 0 },     time = 0.8, at_eye = false },    -- his eyes: the first person view
}

local function living()
    local out = {}
    for n = 0, 3 do
        local ok, p = pcall(function() return player(n) end)
        if ok and p ~= nil then
            local alive = true
            local ok2, dead = pcall(function() return p:is_dead() end)
            if ok2 and dead then alive = false end
            if alive then table.insert(out, n) end
        end
    end
    return out
end

function M.play(opts)
    opts = opts or {}
    local weapon = opts.weapon
    if weapon == nil and opts.level ~= nil then weapon = M.START_WEAPON[opts.level] end

    local who = living()
    local p = who[1] or 0
    local me = player(p)
    local function read_eye()
        local x, y, z = me:eye()
        if x == nil or y == nil or z == nil then x, y, z = me:pos() end
        return x or 0, y or 0, z or 0
    end
    -- The eye point is not settled at the level's first tick, and under a scripted camera the player's y drops (no head
    -- animation): read it before the scene starts and again a moment after, and keep the higher one.
    local bx, by, bz = read_eye()
    cinema.begin()
    wait(0.2)
    local ax, ay, az = read_eye()
    local ex, ey, ez = bx, by, bz
    if ay > by then ex, ey, ez = ax, ay, az end
    local eye = { ex or 0, ey or 0, ez or 0 }
    local face = me:facing()                       -- the player's own facing, degrees (API yaw)
    local psi = math.rad(face)
    local cs, sn = math.cos(psi), math.sin(psi)

    -- the shot as camera points: world position (eye + the offset turned by the facing), yaw / pitch toward the look point
    local prev_yaw = nil
    local function point(s)
        local o = s.off
        local c = { eye[1] + o[1] * cs + o[3] * sn, eye[2] + o[2], eye[3] - o[1] * sn + o[3] * cs }
        local pu = s.at_eye and 0 or LOOK_AHEAD
        local dx, dy, dz = eye[1] + pu * sn - c[1], eye[2] - c[2], eye[3] + pu * cs - c[3]
        local flat = math.sqrt(dx * dx + dz * dz)
        local yaw, pitch = face, 0
        if flat > 0.001 or math.abs(dy) > 0.001 then
            if flat > 0.001 then yaw = math.deg(math.atan2(dx, dz)) end
            pitch = math.deg(math.atan2(dy, flat))
        end
        if prev_yaw ~= nil then                    -- unwrapped: the blend takes the short way round
            while yaw - prev_yaw > 180 do yaw -= 360 end
            while yaw - prev_yaw < -180 do yaw += 360 end
        end
        prev_yaw = yaw
        return { pos = c, yaw = yaw, pitch = pitch, fov = FOV, ease = "linear", tension = 0.5 }
    end

    local points, total = {}, 0
    local first = point(SHOT[1])
    first.control = true                           -- the spline's outer neighbour: shapes the first leg only
    table.insert(points, first)
    for i, s in SHOT do
        local cp = point(s)
        cp.time = s.time
        table.insert(points, cp)
        if i > 1 then total += s.time end
    end
    local last = point(SHOT[#SHOT])
    last.control = true
    table.insert(points, last)

    -- Bond's body, built where the player is, holding the start weapon (before show(true): the body then carries it)
    local b = bond_body(p)
    b:hold_item((weapon == nil or weapon <= 1) and 0 or weapon, "right")
    b:show(true)
    b:anim(61, { from = 95, speed = 0.02, hold = true })    -- the near-still pose (the campaign's table entry 0)

    local t0 = clock()
    spawn(function()
        local left = total - BODY_FADE - (clock() - t0)
        while left > 0 do
            wait(left)
            left = total - BODY_FADE - (clock() - t0)
        end
        b:fade(0, BODY_FADE)                       -- transparent, in place, the weapon with it
    end)
    camera.move(points, { smooth = true })
    camera.wait()

    -- the hand-over
    b:stop_anim()
    b:remove()                                     -- removed, not hidden: that frees the player's hands
    camera.reset()
    if weapon ~= nil and weapon > 1 then
        for _, n in who do
            pcall(function() player(n):force_weapon(weapon) end)
        end
    end
    cinema.finish()                                -- control back; the mission clock runs from here
end

return M
