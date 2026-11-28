-- cuba: the Cuba ending (specs/cuba.md): one scene, no gameplay. Bond and Natalya on the clearing with their
-- animation loop (rules 2-3), the orbiting camera, three dialogue lines, the credits, then the level ends (rule 4).
local guard_ai = require("guard_ai")
local L = require("campaign_lib")
guard_ai.boot()

local NATALYA = 0x00

-- rule 1: the closing scene at once; HUD hidden except the top text line, weapon hidden, controls locked.
-- TODO(api): no verb stops the mission clock (cinema.begin does not say it does).
cinema.begin({ keep_top_text = true })
for _, i in L.players() do
    player(i):hide_weapon(true)
    if i > 0 then
        bond_body(i):show(false)   -- Co-op: only player 1's Bond body is shown
    end
end

-- rule 2: the couple on their pads, each on its animation
local natalya: Target = chr(NATALYA)
guard_ai.stop(natalya)
natalya:teleport(0x2D)
local bond: Target = bond_body(0)
bond:teleport(0x2C)
bond:show(true)
-- rule 2: whole animation with its root translation, played once and held on its last frame (nothing loops)
bond:anim(0xB5, { hold = true })
natalya:anim(0xB6, { hold = true })

-- rule 3: each time Bond's play ends, the pair picks the next shared moment
spawn(function()
    while true do
        wait(0.25)
        if bond:anim_done() then
            local from: number, to: number? = 158, 250
            if math.random() * 100 >= 86 then
                from, to = 259, nil   -- 14%: frames 259 to the end
            end
            -- no root translation, once, held on the last frame; only Bond's play is watched
            bond:anim(0xB5, { from = from, to = to, move = false, blend = 0.5, hold = true })
            natalya:anim(0xB6, { from = from, to = to, move = false, blend = 0.5, hold = true })
        end
    end
end)

-- Cinematics 1: third-person orbit around the couple (the midpoint of pads 0x2C and 0x2D), starting at 0x1C71 (40
-- degrees). The distance, the height and the speed are measured on the game's own ending (its camera logged frame by
-- frame): a circle of radius 300 around the couple's feet, the camera 100 above them, 18/65536 of a turn per tick (about
-- 5.9 degrees per second). The pads stand about 207 units above the feet, hence the look height.

camera.orbit({
    centre = { pad(0x2C), pad(0x2D) },
    distance = 300,
    height = 100,
    look_height = -207,
    start_angle = 0x1C71 * 360 / 65536,
    speed = 18 * 60 * 360 / 65536,
})

-- Cinematics 2-5: fade in, then the three lines
screen.fade_in()
L.wait_fade({ poll = 0.1 })
wait(0.5)
L.top("5008")
wait(3)
L.top("5009")
wait(3)
L.top("500A")
-- Cinematics 6: the credits, the orbit and the animation loop going on behind them. The level's credit rows (two text
-- ids each) become lines: two texts are a role and a name, one text is a centred line, none is a blank row. A single
-- text after a run of blank rows opens a group (a role: "Produced and Directed by") and is a heading, in the title
-- colour; the single texts right after it (the names) are plain. The game's block starts with the intro's cast list (a
-- few rows, then a long run of blank rows): that is not credits, so the rows before the first long blank run are left
-- out (and kept if there is no such run).
local NO_TEXT = 0x5011   -- "nothing in this column"
local CAST_GAP = 8       -- blank rows that end the cast list
local GROUP_GAP = 2      -- blank rows that separate two groups (a role and its names have one between them)
local ROW = 1.5          -- screen lines of one row of the block

local function credit_text(id: number): string?
    if id == 0 or id == NO_TEXT then
        return nil
    end
    local t = text(id)
    if t == nil or t == "" or t:find("[\128-\255]") then   -- no text, or Japanese glyph codes (the overlay has no font)
        return nil
    end
    return t
end

local function credit_lines(skip_cast: boolean): { CreditsLine }
    local lines: { CreditsLine } = {}
    local blanks, pending = 0, 0
    local started = not skip_cast
    for _, row in credits.rows() do
        local a, b = credit_text(row.text1), credit_text(row.text2)
        if a == nil and b == nil then
            blanks += 1
            if started then
                pending += ROW
            end
        elseif not started and blanks < CAST_GAP then
            blanks = 0   -- still in the cast list
        else
            local opens_group = blanks >= GROUP_GAP
            started = true
            blanks = 0
            if pending > 0 then
                table.insert(lines, { gap = pending })
                pending = 0
            end
            if a ~= nil and b ~= nil then
                table.insert(lines, { left = a, right = b })
                pending += ROW - 1
            elseif opens_group then
                local heading: CreditsLine = { text = (a or b) :: string, style = "heading" }
                table.insert(lines, heading)   -- 1.7 lines high: no extra gap after it
            else
                table.insert(lines, (a or b) :: string)
                pending += ROW - 1
            end
        end
    end
    return lines
end

wait(5)
local credit_rows = credit_lines(true)
if #credit_rows == 0 then
    credit_rows = credit_lines(false)
end
credits.show({ speed = 2.5, lines = credit_rows })
while not credits.done() do
    wait(0.25)
end
-- Cinematics 7, rule 4: fade to black and the level ends as completed
screen.fade_out()
L.wait_fade({ poll = 0.1 })
mission.win()
