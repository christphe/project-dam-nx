// The cinema domain of the campaign API (#165): Luau bindings. Layout: mission_lua_ext.h; script contract:
// lib/mission.d.luau; host callbacks: mission_host_cinema.h (implemented in port/src/missions_host_cinema.c).
#include "mission_lua_ext.h"
#include "mission_host_cinema.h"

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include <stdlib.h>

#include <initializer_list>
#include <string.h>

namespace {

const MissionHostCinema *cx(lua_State *L)
{
    const MissionHost *h = missionLuaHost(L);
    return h ? h->cinema_ext : nullptr;
}

// "<name>: not implemented yet" / "not available here" is logged once per name and process.
void unavailable(lua_State *L, const char *name, const char *why)
{
    static char seen[48][40];
    static int nseen = 0;
    for (int i = 0; i < nseen; i++)
        if (!strcmp(seen[i], name)) return;
    if (nseen < 48) { strncpy(seen[nseen], name, 39); seen[nseen][39] = '\0'; nseen++; }
    missionLuaLog(L, "%s: %s", name, why);
}

#define NEED(L, name, cb) \
    const MissionHostCinema *h_ = cx(L); \
    if (!h_ || !h_->cb) { unavailable(L, name, "not implemented yet"); return 0; }

// The kind of the target at idx ("" when it is no target).
const char *kindAt(lua_State *L, int idx)
{
    static char buf[16];
    buf[0] = '\0';
    if (!lua_istable(L, idx)) return buf;
    lua_getfield(L, idx, "kind");
    if (lua_isstring(L, -1)) strncpy(buf, lua_tostring(L, -1), sizeof buf - 1), buf[sizeof buf - 1] = '\0';
    lua_pop(L, 1);
    return buf;
}

// A player (0-based) from a "player" or "body" target, -1 when it is neither.
int playerOf(lua_State *L, int idx)
{
    const char *k = kindAt(L, idx);
    if (strcmp(k, "player") && strcmp(k, "body")) return -1;
    lua_getfield(L, idx, "player");
    int p = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : 0;
    lua_pop(L, 1);
    return p;
}

// An original pad number from a number, a mission pad's string id or a "pad" target; -1: none.
int padArg(lua_State *L, int idx) { return missionLuaPadArg(L, idx); }

// ---------------------------------------------------------------- cinema
int c_begin(lua_State *L)
{
    NEED(L, "cinema.begin", begin)
    int keep = 0;
    if (lua_istable(L, 1)) {
        struct { const char *key; int bit; } k[] = { { "keep_hud", MHC_KEEP_HUD }, { "keep_top_text", MHC_KEEP_TOP_TEXT },
                                                     { "keep_bottom_text", MHC_KEEP_BOTTOM },
                                                     { "keep_countdown", MHC_KEEP_COUNTDOWN } };
        for (auto &e : k) {
            lua_getfield(L, 1, e.key);
            if (lua_toboolean(L, -1)) keep |= e.bit;
            lua_pop(L, 1);
        }
        lua_getfield(L, 1, "environment");
        if (lua_isstring(L, -1)) {
            if (strcmp(lua_tostring(L, -1), "intro") == 0) keep |= MHC_ENV_INTRO;
            else if (strcmp(lua_tostring(L, -1), "level") != 0)
                missionLuaLog(L, "cinema.begin: environment '%s' is unknown (\"intro\" or \"level\")", lua_tostring(L, -1));
        }
        lua_pop(L, 1);
    }
    h_->begin(h_->ud, keep);
    return 0;
}

int c_finish(lua_State *L) { NEED(L, "cinema.finish", finish) h_->finish(h_->ud); return 0; }

int c_hide_all_chrs(lua_State *L)
{
    NEED(L, "cinema.hide_all_chrs", hide_all_chrs)
    h_->hide_all_chrs(h_->ud, lua_toboolean(L, 1));
    return 0;
}

int c_skipped(lua_State *L)
{
    const MissionHostCinema *h = cx(L);
    if (!h || !h->skipped) { unavailable(L, "cinema.skipped", "not implemented yet"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, h->skipped(h->ud));
    return 1;
}

int c_in_intro(lua_State *L)
{
    const MissionHostCinema *h = cx(L);
    if (!h || !h->in_intro) { unavailable(L, "cinema.in_intro", "not implemented yet"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, h->in_intro(h->ud));
    return 1;
}

// ---------------------------------------------------------------- camera
// A camera target: a pad, a setup prop (a camera object by tag, or any prop with a pad) or a player / body.
bool camTarget(lua_State *L, int idx, int *kind, int *index)
{
    const char *k = kindAt(L, idx);
    if (!strcmp(k, "pad")) {
        int p = missionLuaTgtPad(L, idx);
        if (p < 0) return false;
        *kind = MHC_T_PAD; *index = p;
        return true;
    }
    if (!strcmp(k, "prop")) {
        int p = missionLuaTgtProp(L, idx);
        if (p < 0) return false;
        *kind = MHC_T_PROP; *index = p;
        return true;
    }
    int pl = playerOf(L, idx);
    if (pl >= 0) { *kind = MHC_T_PLAYER; *index = pl; return true; }
    return false;
}

int cam_fixed(lua_State *L)
{
    NEED(L, "camera.fixed", camera_fixed)
    int kind, index, look = 0;
    if (!camTarget(L, 1, &kind, &index)) {
        missionLuaLog(L, "camera.fixed: the target is no camera object, pad or player");
        return 0;
    }
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "look_at");
        if (lua_istable(L, -1)) {
            // The game's fixed camera can only look at Bond (a camera object's own direction otherwise).
            if (playerOf(L, -1) >= 0) look = 1;
            else missionLuaLog(L, "camera.fixed: look_at only supports a player; the camera keeps its own direction");
        }
        lua_pop(L, 1);
    }
    h_->camera_fixed(h_->ud, kind, index, look);
    return 0;
}

double numField(lua_State *L, int idx, const char *key, double def)
{
    lua_getfield(L, idx, key);
    double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

bool hasNum(lua_State *L, int idx, const char *key, float *out)
{
    lua_getfield(L, idx, key);
    const bool has = lua_isnumber(L, -1) != 0;
    if (has) *out = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return has;
}

// A vector given as mission.d.luau declares it, {x, y, z} (positional), or with named fields {x = .., y = .., z = ..}.
bool vec3At(lua_State *L, int idx, float out[3])
{
    if (hasNum(L, idx, "x", &out[0]) && hasNum(L, idx, "y", &out[1]) && hasNum(L, idx, "z", &out[2])) return true;
    for (int i = 0; i < 3; i++) {
        lua_rawgeti(L, idx, i + 1);
        const bool num = lua_isnumber(L, -1) != 0;
        if (num) out[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (!num) return false;
    }
    return true;
}

// A position reference from a Target (pad / prop / player / body) or a plain {x, y, z}. A pad target's mission id is
// resolved by missionLuaTgtPad.
bool refAt(lua_State *L, int idx, MissionCamRef *r)
{
    mhcRefInit(r);
    if (!lua_istable(L, idx)) return false;
    const char *k = kindAt(L, idx);
    if (!*k) {
        if (!vec3At(L, idx, r->xyz)) return false;
        r->kind = MHC_T_XYZ; r->has_xyz = 1;
        return true;
    }
    const MissionHost *h = missionLuaHost(L);
    if (!strcmp(k, "pad")) {
        int p = missionLuaTgtPad(L, idx);
        if (p < 0) return false;
        r->kind = MHC_T_PAD; r->index = p;
        return true;
    }
    if (!strcmp(k, "prop")) {
        int p = missionLuaTgtProp(L, idx);
        if (p < 0) return false;
        r->kind = MHC_T_PROP; r->index = p;
        r->has_xyz = h->prop_pos && h->prop_pos(h->ud, p, r->xyz);
        return true;
    }
    int pl = playerOf(L, idx);
    if (pl >= 0) {
        r->kind = MHC_T_PLAYER; r->index = pl;
        r->has_xyz = h->player_pos && h->player_pos(h->ud, pl, r->xyz);
        return true;
    }
    return false;
}

// One CameraPoint table; false (and a log line) when its pos is unusable.
bool pointAt(lua_State *L, int idx, MissionCamPoint *pt, const char *who)
{
    mhcPointInit(pt);
    if (!lua_istable(L, idx)) { missionLuaLog(L, "%s: a point must be a table", who); return false; }
    lua_getfield(L, idx, "pos");
    bool ok = refAt(L, lua_gettop(L), &pt->pos);
    lua_pop(L, 1);
    if (!ok) { missionLuaLog(L, "%s: pos is no target and no {x, y, z}", who); return false; }
    lua_getfield(L, idx, "offset");
    if (lua_istable(L, -1) && !vec3At(L, lua_gettop(L), pt->offset))
        pt->offset[0] = pt->offset[1] = pt->offset[2] = 0.0f;
    lua_pop(L, 1);
    pt->has_yaw = hasNum(L, idx, "yaw", &pt->yaw);
    pt->has_pitch = hasNum(L, idx, "pitch", &pt->pitch);
    pt->has_fov = hasNum(L, idx, "fov", &pt->fov);
    pt->has_speed = hasNum(L, idx, "speed", &pt->speed);
    pt->has_time = hasNum(L, idx, "time", &pt->time);
    pt->has_tension = hasNum(L, idx, "tension", &pt->tension);
    lua_getfield(L, idx, "control");
    pt->control = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
    lua_getfield(L, idx, "pad");
    if (!lua_isnil(L, -1)) {
        pt->pad = missionLuaPadArg(L, lua_gettop(L));
        if (pt->pad < 0) missionLuaLog(L, "%s: pad is no pad: the floor under the camera gives the room", who);
    }
    lua_pop(L, 1);
    hasNum(L, idx, "hold", &pt->hold);
    if (pt->hold < 0) pt->hold = 0;
    lua_getfield(L, idx, "look_at");
    if (lua_istable(L, -1) && !refAt(L, lua_gettop(L), &pt->look))
        missionLuaLog(L, "%s: look_at is no target and no {x, y, z}: ignored", who);
    lua_pop(L, 1);
    lua_getfield(L, idx, "ease");
    if (lua_isstring(L, -1)) {
        const char *e = lua_tostring(L, -1);
        if (!strcmp(e, "linear")) pt->ease = MHC_EASE_LINEAR;
        else if (!strcmp(e, "in")) pt->ease = MHC_EASE_IN;
        else if (!strcmp(e, "out")) pt->ease = MHC_EASE_OUT;
        else pt->ease = MHC_EASE_IN_OUT;
    }
    lua_pop(L, 1);
    return true;
}

int cam_set(lua_State *L)
{
    NEED(L, "camera.set", camera_set)
    MissionCamPoint pt;
    if (pointAt(L, 1, &pt, "camera.set")) h_->camera_set(h_->ud, &pt);
    return 0;
}

int cam_move(lua_State *L)
{
    NEED(L, "camera.move", camera_move)
    luaL_checktype(L, 1, LUA_TTABLE);
    static MissionCamPoint pts[MHC_MAX_POINTS];   // the host copies them
    int n = 0;
    const int len = (int)lua_objlen(L, 1);
    for (int i = 1; i <= len && n < MHC_MAX_POINTS; i++) {
        lua_rawgeti(L, 1, i);
        if (pointAt(L, lua_gettop(L), &pts[n], "camera.move")) n++;
        lua_pop(L, 1);
    }
    if (len > MHC_MAX_POINTS)   // said, never silent: a truncated move stops short of its end
        missionLuaLog(L, "camera.move: %d points, only the first %d are used", len, MHC_MAX_POINTS);
    if (n == 0) { missionLuaLog(L, "camera.move: no usable point"); return 0; }
    bool loop = false, smooth = true;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "loop");
        loop = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, 2, "smooth");
        if (lua_isboolean(L, -1)) smooth = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    h_->camera_move(h_->ud, pts, n, loop, smooth);
    return 0;
}

int cam_moving(lua_State *L)
{
    const MissionHostCinema *h = cx(L);
    if (!h || !h->camera_moving) { unavailable(L, "camera.moving", "not implemented yet"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, h->camera_moving(h->ud));
    return 1;
}

int cam_stop(lua_State *L) { NEED(L, "camera.stop", camera_stop) h_->camera_stop(h_->ud); return 0; }

int cam_orbit(lua_State *L)
{
    NEED(L, "camera.orbit", camera_orbit)
    luaL_checktype(L, 1, LUA_TTABLE);
    MissionCamRef refs[MHC_MAX_CENTRES];
    int n = 0;
    lua_getfield(L, 1, "centre");
    if (lua_istable(L, -1) && *kindAt(L, lua_gettop(L)) == '\0' && lua_objlen(L, -1) > 0) {
        // a list of targets: the camera circles their midpoint
        const int top = lua_gettop(L), len = (int)lua_objlen(L, top);
        for (int i = 1; i <= len && n < MHC_MAX_CENTRES; i++) {
            lua_rawgeti(L, top, i);
            if (refAt(L, lua_gettop(L), &refs[n])) n++;
            lua_pop(L, 1);
        }
    } else if (refAt(L, lua_gettop(L), &refs[0])) {
        n = 1;
    }
    lua_pop(L, 1);
    if (n == 0) {
        missionLuaLog(L, "camera.orbit: centre is no pad, prop, player or {x, y, z}");
        return 0;
    }
    h_->camera_orbit(h_->ud, refs, n, numField(L, 1, "distance", 300), numField(L, 1, "height", 100),
                     numField(L, 1, "look_height", 0), numField(L, 1, "start_angle", 0), numField(L, 1, "speed", 10));
    return 0;
}

// camera.path(anim): the game has no camera animation to drive (its cameras are the intro swirl splines of the setup,
// a fixed camera object and the pad orbit): a logged stub.
int cam_path(lua_State *L)
{
    unavailable(L, "camera.path", "not implemented yet");
    (void)L;
    return 0;
}

int cam_reset(lua_State *L) { NEED(L, "camera.reset", camera_reset) h_->camera_reset(h_->ud); return 0; }

// ---------------------------------------------------------------- screen / credits
int scr_fade(lua_State *L, int to_black)
{
    NEED(L, to_black ? "screen.fade_out" : "screen.fade_in", fade)
    double s = luaL_optnumber(L, 1, 1.0);
    h_->fade(h_->ud, to_black, s < 0 ? 0 : s);
    return 0;
}
int scr_fade_out(lua_State *L) { return scr_fade(L, 1); }
int scr_fade_in(lua_State *L) { return scr_fade(L, 0); }

int scr_fading(lua_State *L)
{
    const MissionHostCinema *h = cx(L);
    if (!h || !h->fading) { unavailable(L, "screen.fading", "not implemented yet"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, h->fading(h->ud));
    return 1;
}

int cr_roll(lua_State *L) { NEED(L, "credits.roll", credits_roll) h_->credits_roll(h_->ud); return 0; }

// The style / alignment named by the string at the top of the stack (nil: `def`); anything else is a script error.
int credits_enum(lua_State *L, const char *what, const char *const *names, int n, int def)
{
    int v = def;
    if (lua_isstring(L, -1)) {
        const char *s = lua_tostring(L, -1);
        v = -1;
        for (int i = 0; i < n; i++)
            if (!strcmp(s, names[i])) v = i;
        if (v < 0) luaL_error(L, "credits.show: %s must be one of \"%s\", \"%s\"%s%s%s, not \"%s\"", what, names[0], names[1],
                              n > 2 ? ", \"" : "", n > 2 ? names[2] : "", n > 2 ? "\"" : "", s);
    } else if (!lua_isnil(L, -1)) {
        luaL_error(L, "credits.show: %s must be a string", what);
    }
    return v;
}

// credits.show(def): the scripted credits (mission_host_credits.h). One script thread, and the list is too big for a
// stack: a static one is built and handed to the host, which copies it.
int cr_show(lua_State *L)
{
    NEED(L, "credits.show", credits_show)
    static const char *const kAlign[] = { "center", "left", "right" };
    static const char *const kStyle[] = { "text", "heading", "title" };
    static const char *const kBackdrop[] = { "world", "black" };
    static MissionCredits c;
    luaL_checktype(L, 1, LUA_TTABLE);
    mhcrInit(&c);

    lua_getfield(L, 1, "speed");
    if (lua_isnumber(L, -1)) mhcrSetSpeed(&c, lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, 1, "backdrop");
    c.black = credits_enum(L, "backdrop", kBackdrop, 2, 0);
    lua_pop(L, 1);
    lua_getfield(L, 1, "align");
    const int defAlign = credits_enum(L, "align", kAlign, 3, MHCR_AL_CENTER);
    lua_pop(L, 1);

    lua_getfield(L, 1, "lines");
    if (!lua_istable(L, -1)) luaL_error(L, "credits.show: lines must be a list");
    const int n = lua_objlen(L, -1);
    for (int i = 1; i <= n; i++) {
        int ok = 1;
        lua_rawgeti(L, -1, i);
        if (lua_isstring(L, -1)) {
            ok = mhcrAddLine(&c, MHCR_ST_TEXT, defAlign, lua_tostring(L, -1));
        } else if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "gap");
            const int isGap = lua_isnumber(L, -1);
            if (isGap) ok = mhcrAddGap(&c, lua_tonumber(L, -1));
            lua_pop(L, 1);
            if (!isGap) {
                lua_getfield(L, -1, "left");
                char left[MHCR_TEXT] = "", right[MHCR_TEXT] = "";
                const int hasLeft = lua_isstring(L, -1);
                if (hasLeft) mhcrCopy(left, lua_tostring(L, -1));
                lua_pop(L, 1);
                lua_getfield(L, -1, "right");
                const int hasRight = lua_isstring(L, -1);
                if (hasRight) mhcrCopy(right, lua_tostring(L, -1));
                lua_pop(L, 1);
                if (hasLeft || hasRight) {
                    ok = mhcrAddPair(&c, left, right);
                } else {
                    lua_getfield(L, -1, "text");
                    if (!lua_isstring(L, -1)) luaL_error(L, "credits.show: line %d has no text, left, right or gap", i);
                    char text[MHCR_TEXT];
                    mhcrCopy(text, lua_tostring(L, -1));
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "style");
                    const int style = credits_enum(L, "style", kStyle, 3, MHCR_ST_TEXT);
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "align");
                    const int align = credits_enum(L, "align", kAlign, 3, defAlign);
                    lua_pop(L, 1);
                    ok = mhcrAddLine(&c, style, align, text);
                }
            }
        } else {
            luaL_error(L, "credits.show: line %d is neither a string nor a table", i);
        }
        lua_pop(L, 1);
        if (!ok) luaL_error(L, "credits.show: more than %d lines", MHCR_MAX_ENTRIES);
    }
    lua_pop(L, 1);
    h_->credits_show(h_->ud, &c);
    return 0;
}

// credits.rows(): the level's own credits block as { { text1 = id, text2 = id }, ... } (empty in a level without one).
int cr_rows(lua_State *L)
{
    static unsigned short ids[2 * 1024];
    const MissionHostCinema *h = cx(L);
    int n = 0;
    if (h && h->credits_rows) n = h->credits_rows(h->ud, ids, 1024);
    else unavailable(L, "credits.rows", "not implemented yet");
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, ids[2 * i]);
        lua_setfield(L, -2, "text1");
        lua_pushnumber(L, ids[2 * i + 1]);
        lua_setfield(L, -2, "text2");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int cr_stop(lua_State *L) { NEED(L, "credits.stop", credits_stop) h_->credits_stop(h_->ud); return 0; }

int cr_done(lua_State *L)
{
    const MissionHostCinema *h = cx(L);
    if (!h || !h->credits_done) { unavailable(L, "credits.done", "not implemented yet"); lua_pushboolean(L, 1); return 1; }
    lua_pushboolean(L, h->credits_done(h->ud));
    return 1;
}

// ---------------------------------------------------------------- bond_body and the player methods
int l_bond_body(lua_State *L)
{
    int p = (int)luaL_optinteger(L, 1, 0);
    missionLuaPushTarget(L, "body", -1, -1, p);
    return 1;
}

// A method shared with an earlier domain: upvalue 1 is the earlier function (nil when none). A target that is not
// ours goes to it with every argument; with none, `neutral` (0: nothing, 1: false) comes back.
int passOn(lua_State *L, int neutral)
{
    if (lua_isnoneornil(L, lua_upvalueindex(1))) {
        if (neutral) { lua_pushboolean(L, 0); return 1; }
        return 0;
    }
    const int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    for (int i = 1; i <= n; i++) lua_pushvalue(L, i);
    lua_call(L, n, LUA_MULTRET);
    return lua_gettop(L) - n;
}

int b_teleport(lua_State *L)
{
    const int p = (lua_istable(L, 1) && !strcmp(kindAt(L, 1), "body")) ? playerOf(L, 1) : -1;
    if (p < 0) return passOn(L, 1);
    const MissionHostCinema *h = cx(L);
    if (!h || !h->body_teleport) { unavailable(L, "bond_body:teleport", "not implemented yet"); lua_pushboolean(L, 0); return 1; }
    const int pad = padArg(L, 2);
    if (pad < 0) {
        missionLuaLog(L, "bond_body:teleport: no such pad");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, h->body_teleport(h->ud, p, pad));
    return 1;
}

int b_anim(lua_State *L)
{
    const int p = (lua_istable(L, 1) && !strcmp(kindAt(L, 1), "body")) ? playerOf(L, 1) : -1;
    if (p < 0) return passOn(L, 1);
    const MissionHostCinema *h = cx(L);
    if (!h || !h->body_anim) { unavailable(L, "bond_body:anim", "not implemented yet"); lua_pushboolean(L, 0); return 1; }
    const char *name = nullptr;
    int id = -1;
    if (lua_type(L, 2) == LUA_TSTRING) name = lua_tostring(L, 2);
    else id = (int)luaL_checkinteger(L, 2);
    MissionCinemaAnim o;
    o.from = -1; o.to = -1; o.speed = 1; o.hold = 0; o.reverse = 0; o.loop = 0; o.blend = -1; o.move = 1;
    if (lua_istable(L, 3)) {
        o.from = (int)numField(L, 3, "from", -1);
        o.to = (int)numField(L, 3, "to", -1);
        o.speed = numField(L, 3, "speed", 1);
        o.blend = numField(L, 3, "blend", -1);
        const char *flags[] = { "hold", "reverse", "loop" };
        int *dst[] = { &o.hold, &o.reverse, &o.loop };
        for (int i = 0; i < 3; i++) {
            lua_getfield(L, 3, flags[i]);
            *dst[i] = lua_toboolean(L, -1);
            lua_pop(L, 1);
        }
        lua_getfield(L, 3, "move");
        if (lua_isboolean(L, -1)) o.move = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    lua_pushboolean(L, h->body_anim(h->ud, p, name, id, &o));
    return 1;
}

int b_anim_done(lua_State *L)
{
    const int p = (lua_istable(L, 1) && !strcmp(kindAt(L, 1), "body")) ? playerOf(L, 1) : -1;
    if (p < 0) return passOn(L, 1);
    const MissionHostCinema *h = cx(L);
    if (!h || !h->body_anim_done) { unavailable(L, "bond_body:anim_done", "not implemented yet"); lua_pushboolean(L, 1); return 1; }
    lua_pushboolean(L, h->body_anim_done(h->ud, p));
    return 1;
}

int b_hold_item(lua_State *L)
{
    const int p = playerOf(L, 1);
    if (p < 0 || strcmp(kindAt(L, 1), "body")) { missionLuaLog(L, "hold_item: only on bond_body()"); return 0; }
    NEED(L, "bond_body:hold_item", body_hold_item)
    const int item = (int)luaL_checkinteger(L, 2);
    const char *hand = lua_isstring(L, 3) ? lua_tostring(L, 3) : "right";
    h_->body_hold_item(h_->ud, p, item, !strcmp(hand, "left"));
    return 0;
}

int b_set_shadow(lua_State *L)
{
    const int p = playerOf(L, 1);
    if (p < 0 || strcmp(kindAt(L, 1), "body")) { missionLuaLog(L, "set_shadow: only on bond_body()"); return 0; }
    NEED(L, "bond_body:set_shadow", body_set_shadow)
    h_->body_set_shadow(h_->ud, p, lua_toboolean(L, 2));
    return 0;
}

// show: the earlier domain's show (a guard) keeps every target that is not a player or a body.
int b_show(lua_State *L)
{
    const int p = playerOf(L, 1);
    if (p < 0) return passOn(L, 0);
    NEED(L, "show", body_show)
    h_->body_show(h_->ud, p, lua_toboolean(L, 2));
    return 0;
}

// ---- #165 second pass: the body in a scene. Every method wraps the earlier domain's of the same name and takes only
// kind "body".
int bodyOf(lua_State *L) { return (lua_istable(L, 1) && !strcmp(kindAt(L, 1), "body")) ? playerOf(L, 1) : -1; }

// remove: a guard's remove keeps every target that is not a body; on a body it takes the third-person body away (the
// player's hands are his own again; show(true) makes a new one).
int b_remove(lua_State *L)
{
    const int p = bodyOf(L);
    if (p < 0) return passOn(L, 0);
    NEED(L, "bond_body:remove", body_remove)
    h_->body_remove(h_->ud, p);
    return 0;
}


// fade(to, seconds?): the body's opacity goes from where it is to `to` (0..1) over `seconds` (0 / none: at once); the
// body stays in place, animating, holding what it holds. Only a body has it.
int b_fade(lua_State *L)
{
    const int p = bodyOf(L);
    if (p < 0) {
        if (lua_isnoneornil(L, lua_upvalueindex(1))) { missionLuaLog(L, "fade: only on bond_body()"); return 0; }
        return passOn(L, 0);
    }
    double to = luaL_checknumber(L, 2), secs = luaL_optnumber(L, 3, 0.0);
    if (to < 0) to = 0;
    if (to > 1) to = 1;
    NEED(L, "bond_body:fade", body_fade)
    h_->body_fade(h_->ud, p, (float)to, secs > 0 ? (float)secs : 0.0f);
    return 0;
}

// fade_out is a guard's fade-and-remove: on a body it says what to use instead.
int b_fade_out(lua_State *L)
{
    if (bodyOf(L) >= 0) { missionLuaLog(L, "bond_body:fade_out: a guard's verb; a body fades with :fade(0, seconds)"); return 0; }
    return passOn(L, 0);
}


// The target of aim_at / fire_at: a player or body (0-based; -1 / nil: the first living one), a guard (its chr number)
// or a pad.
bool shotTarget(lua_State *L, int idx, int *kind, int *target)
{
    if (lua_isnoneornil(L, idx)) { *kind = MHC_T_PLAYER; *target = -1; return true; }
    const int pl = playerOf(L, idx);
    if (pl >= 0) { *kind = MHC_T_PLAYER; *target = pl; return true; }
    const int chr = missionLuaTgtChr(L, idx);
    if (chr >= 0) { *kind = MHC_T_PROP; *target = chr; return true; }
    const int pad = missionLuaTgtPad(L, idx);
    if (pad >= 0) { *kind = MHC_T_PAD; *target = pad; return true; }
    return false;
}

int bodyShot(lua_State *L, const char *name, int kneel, int aim_only)
{
    const MissionHostCinema *h = cx(L);
    if (!h || !h->body_aim) { unavailable(L, name, "not implemented yet"); lua_pushboolean(L, 0); return 1; }
    const int p = bodyOf(L);
    int kind, target;
    if (!shotTarget(L, 2, &kind, &target)) {
        missionLuaLog(L, "%s: the target is no player, guard or pad", name);
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, h->body_aim(h->ud, p, kind, target, kneel, aim_only));
    return 1;
}

int b_aim_at(lua_State *L)
{
    if (bodyOf(L) < 0) return passOn(L, 1);
    return bodyShot(L, "bond_body:aim_at", 0, 1);
}

int b_fire_at(lua_State *L)
{
    if (bodyOf(L) < 0) return passOn(L, 1);
    int kneel = 0, aim_only = 0;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "stance");
        if (lua_isstring(L, -1) && !strcmp(lua_tostring(L, -1), "kneel")) kneel = 1;
        lua_pop(L, 1);
        lua_getfield(L, 3, "aim_only");
        aim_only = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    return bodyShot(L, "bond_body:fire_at", kneel, aim_only);
}

int b_walk_to(lua_State *L)
{
    const int p = bodyOf(L);
    if (p < 0) return passOn(L, 1);
    const MissionHostCinema *h = cx(L);
    if (!h || !h->body_walk) { unavailable(L, "bond_body:walk_to", "not implemented yet"); lua_pushboolean(L, 0); return 1; }
    const int pad = padArg(L, 2);
    if (pad < 0) {
        missionLuaLog(L, "bond_body:walk_to: no such pad");
        lua_pushboolean(L, 0);
        return 1;
    }
    int speed = 0;
    if (lua_isstring(L, 3)) {
        const char *s = lua_tostring(L, 3);
        speed = !strcmp(s, "sprint") ? 2 : !strcmp(s, "run") ? 1 : 0;
    }
    lua_pushboolean(L, h->body_walk(h->ud, p, pad, speed));
    return 1;
}

int b_drop_item(lua_State *L)
{
    const int p = bodyOf(L);
    if (p < 0) return passOn(L, 0);
    NEED(L, "bond_body:drop_item", body_drop_item)
    const char *hand = lua_isstring(L, 2) ? lua_tostring(L, 2) : "right";
    h_->body_drop_item(h_->ud, p, !strcmp(hand, "left"));
    return 0;
}

int b_set_gravity(lua_State *L)
{
    const int p = bodyOf(L);
    if (p < 0) return passOn(L, 0);
    NEED(L, "bond_body:set_gravity", body_set_gravity)
    h_->body_set_gravity(h_->ud, p, lua_toboolean(L, 2));
    return 0;
}

int b_set_body_flags(lua_State *L)
{
    const int p = bodyOf(L);
    const MissionHostCinema *h = cx(L);
    int chr = -1;
    if (p < 0) {   // a guard's chr: ours too; anything else goes to the earlier function
        if (h && h->chr_set_flags && lua_istable(L, 1)) chr = missionLuaTgtChr(L, 1);
        if (chr < 0) return passOn(L, 0);
    } else if (!h || !h->body_set_flags) {
        unavailable(L, "bond_body:set_body_flags", "not implemented yet");
        return 0;
    }
    int mask = 0, values = 0;
    if (lua_istable(L, 2)) {
        struct { const char *key; int bit; } k[] = { { "hitbox_cull", MHC_BF_HITBOX_CULL }, { "height_lock", MHC_BF_HEIGHT_LOCK },
                                                     { "knockable", MHC_BF_KNOCKABLE }, { "sorted", MHC_BF_SORTED },
                                                     { "no_collision", MHC_BF_NO_COLLISION } };
        for (auto &e : k) {
            lua_getfield(L, 2, e.key);
            if (lua_isboolean(L, -1)) {
                mask |= e.bit;
                if (lua_toboolean(L, -1)) values |= e.bit;
            }
            lua_pop(L, 1);
        }
    }
    if (!mask) return 0;
    if (p >= 0) h->body_set_flags(h->ud, p, mask, values);
    else h->chr_set_flags(h->ud, chr, mask, values);
    return 0;
}

// set_invincible: a body is ours; an object, a player or a guard goes to the earlier (world / guard) function.
int b_set_invincible(lua_State *L)
{
    const int p = bodyOf(L);
    if (p < 0) return passOn(L, 0);
    NEED(L, "bond_body:set_invincible", body_set_invincible)
    h_->body_set_invincible(h_->ud, p, lua_toboolean(L, 2));
    return 0;
}

// set_hidden(false) on a guard while cinema.hide_all_chrs(true) is in force shows it; everything else is the earlier one's.
int b_set_hidden(lua_State *L)
{
    const MissionHostCinema *h = cx(L);
    if (h && h->show_hidden_chr && !lua_toboolean(L, 2) && lua_istable(L, 1) && strcmp(kindAt(L, 1), "body")) {
        const int chr = missionLuaTgtChr(L, 1);
        if (chr >= 0 && h->show_hidden_chr(h->ud, chr)) return 0;
    }
    return passOn(L, 0);
}

int p_set_frozen(lua_State *L)
{
    const int p = playerOf(L, 1);
    if (p < 0) { missionLuaLog(L, "set_frozen: only on a player"); return 0; }
    NEED(L, "set_frozen", set_frozen)
    h_->set_frozen(h_->ud, p, lua_toboolean(L, 2));
    return 0;
}

int p_hide_weapon(lua_State *L)
{
    const int p = playerOf(L, 1);
    if (p < 0) { missionLuaLog(L, "hide_weapon: only on a player"); return 0; }
    NEED(L, "hide_weapon", hide_weapon)
    h_->hide_weapon(h_->ud, p, lua_toboolean(L, 2));
    return 0;
}

int p_in_cutscene(lua_State *L)
{
    const int p = playerOf(L, 1);
    const MissionHostCinema *h = cx(L);
    if (p < 0 || !h || !h->in_cutscene) {
        if (p >= 0) unavailable(L, "in_cutscene", "not implemented yet");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, h->in_cutscene(h->ud, p));
    return 1;
}

struct Fn { const char *name; lua_CFunction f; };

void table(lua_State *L, const char *name, std::initializer_list<Fn> fns)
{
    lua_createtable(L, 0, (int)fns.size());
    for (const Fn &f : fns) {
        lua_pushcfunction(L, f.f, f.name);
        lua_setfield(L, -2, f.name);
    }
    lua_setglobal(L, name);
}

// Install `f` as Target method `name`, keeping the earlier function of that name (guard domain) as upvalue 1.
void wrap(lua_State *L, int targetMT, const char *name, lua_CFunction f)
{
    lua_getfield(L, targetMT, name);   // the earlier function, or nil
    lua_pushcclosure(L, f, name, 1);
    lua_setfield(L, targetMT, name);
}

}  // namespace

void missionLuaRegCinema(lua_State *L, int targetMT)
{
    table(L, "cinema", { Fn{ "begin", c_begin }, Fn{ "finish", c_finish }, Fn{ "hide_all_chrs", c_hide_all_chrs },
                         Fn{ "skipped", c_skipped }, Fn{ "in_intro", c_in_intro } });
    table(L, "camera", { Fn{ "fixed", cam_fixed }, Fn{ "orbit", cam_orbit }, Fn{ "path", cam_path },
                         Fn{ "reset", cam_reset }, Fn{ "set", cam_set }, Fn{ "move", cam_move },
                         Fn{ "moving", cam_moving }, Fn{ "stop", cam_stop } });
    // camera.wait(): sleeps until the move ends = wait(event("camera_done")). A script function, so the yield is the
    // runtime's own wait(); it returns at once when nothing is moving (the event would never come).
    {
        static const char src[] =
            "local wait, event = wait, event\n"
            "return function() if camera.moving() then wait(event('camera_done')) end end\n";
        lua_CompileOptions opts = {};
        size_t bsz = 0;
        char *bc = luau_compile(src, sizeof src - 1, &opts, &bsz);
        if (bc && luau_load(L, "=camera.wait", bc, bsz, 0) == 0 && lua_pcall(L, 0, 1, 0) == 0) {
            lua_getglobal(L, "camera");
            lua_pushvalue(L, -2);
            lua_setfield(L, -2, "wait");
            lua_pop(L, 2);
        } else {
            missionLuaLog(L, "camera.wait: could not be set up");
            lua_settop(L, targetMT);
        }
        free(bc);
    }
    table(L, "screen", { Fn{ "fade_out", scr_fade_out }, Fn{ "fade_in", scr_fade_in }, Fn{ "fading", scr_fading } });
    table(L, "credits", { Fn{ "roll", cr_roll }, Fn{ "show", cr_show }, Fn{ "stop", cr_stop }, Fn{ "rows", cr_rows }, Fn{ "done", cr_done } });
    lua_pushcfunction(L, l_bond_body, "bond_body");
    lua_setglobal(L, "bond_body");

    // teleport / anim / anim_done / show are shared with the guard domain: wrap what it installed
    wrap(L, targetMT, "teleport", b_teleport);
    wrap(L, targetMT, "anim", b_anim);
    wrap(L, targetMT, "anim_done", b_anim_done);
    wrap(L, targetMT, "show", b_show);
    wrap(L, targetMT, "fade", b_fade);
    wrap(L, targetMT, "remove", b_remove);
    wrap(L, targetMT, "fade_out", b_fade_out);
    // #165 second pass: aim_at / fire_at (a guard's), set_invincible (guard + world), set_hidden (guard) are wrapped too
    wrap(L, targetMT, "aim_at", b_aim_at);
    wrap(L, targetMT, "fire_at", b_fire_at);
    wrap(L, targetMT, "walk_to", b_walk_to);
    wrap(L, targetMT, "drop_item", b_drop_item);
    wrap(L, targetMT, "set_gravity", b_set_gravity);
    wrap(L, targetMT, "set_body_flags", b_set_body_flags);
    wrap(L, targetMT, "set_invincible", b_set_invincible);
    wrap(L, targetMT, "set_hidden", b_set_hidden);
    for (const Fn &f : { Fn{ "hold_item", b_hold_item }, Fn{ "set_shadow", b_set_shadow }, Fn{ "set_frozen", p_set_frozen },
                         Fn{ "hide_weapon", p_hide_weapon }, Fn{ "in_cutscene", p_in_cutscene } }) {
        lua_pushcfunction(L, f.f, f.name);
        lua_setfield(L, targetMT, f.name);
    }
}
