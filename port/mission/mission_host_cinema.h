#ifndef PORT_MISSION_HOST_CINEMA_H
#define PORT_MISSION_HOST_CINEMA_H

/* Host callbacks of the cinema domain of the campaign API (#165): scenes (cinema.*), cameras, screen fades, the
 * player's third-person body (bond_body), player freeze / invulnerability / weapon hiding in a scene, the intro
 * state and the credits. See mission_lua_ext.h for the layout; the script-side contract is
 * port/mission/lib/mission.d.luau.
 *
 * Co-op (2 to 4 players in split screen), decided once for the whole domain: the game's camera mode is global and a
 * scene's camera is computed for the DRIVER (the first living player, port/src/coop.c coopDriver) and mirrored to every
 * view, exactly as the game's own intro / outro cutscenes do. So a scene's camera, the credits and the fades show on
 * EVERY view; freeze, invulnerability, the HUD and the chr hiding apply to every player; the third-person body
 * (bond_body, the animations, the item) is per player and exists only where the game gives a player a body (player 1,
 * and the others only without a character of their own: coopBondBody).
 *
 * The scripted camera (camera.set / move / orbit) is a small director: pure maths in this header (mhc*), driven by the
 * host once per frame, so the timing and the interpolation are unit-tested without the game (tests/test_mission_api_cinema.c). */

#include <math.h>
#include <string.h>

#include "mission_host_credits.h"

#ifdef __cplusplus
extern "C" {
#endif

/* cinema.begin keep flags */
#define MHC_KEEP_HUD       1
#define MHC_KEEP_TOP_TEXT  2
#define MHC_KEEP_BOTTOM    4
#define MHC_KEEP_COUNTDOWN 8
#define MHC_ENV_INTRO      16   /* begin: the level's intro environment (its intro fog and view distance) until the next begin / finish */

/* bond_body:set_body_flags bits */
#define MHC_BF_HITBOX_CULL 1
#define MHC_BF_HEIGHT_LOCK 2
#define MHC_BF_KNOCKABLE   4
#define MHC_BF_SORTED      8    /* CHRFLAG_00020000: drawn with the translucent things, far to near, so nearer ones hide it and it hides
                                 * the farther ones (the level's own ending sets it on both lovers: the foliage behind them stays behind) */
#define MHC_BF_NO_COLLISION 16  /* CHRFLAG_00010000: no collision box (the same ending sets it too) */

/* What a camera is aimed at / centred on: a pad (index: the original pad number, >= 10000 bound), a setup prop
 * (index: the prop number; a camera object, or any prop with a pad), a player (index: 0-based) or a fixed point. */
#define MHC_T_NONE   (-1)
#define MHC_T_PAD    0
#define MHC_T_PROP   1
#define MHC_T_PLAYER 2
#define MHC_T_XYZ    3

typedef struct MissionCamRef {
    int   kind;        /* MHC_T_* */
    int   index;
    int   has_xyz;     /* xyz: the position when the reference was made, used when the live one is gone */
    float xyz[3];
} MissionCamRef;

enum { MHC_EASE_IN_OUT = 0, MHC_EASE_LINEAR, MHC_EASE_IN, MHC_EASE_OUT };

/* One camera.set / camera.move waypoint (mission.d.luau CameraPoint). Angles in degrees: yaw 0 looks along +z and a
 * positive yaw turns left (towards +x); a positive pitch looks up. */
typedef struct MissionCamPoint {
    MissionCamRef pos;
    float  offset[3];
    int    has_yaw;   float yaw;
    int    has_pitch; float pitch;
    MissionCamRef look;               /* kind MHC_T_NONE: none; else the camera tracks it instead of yaw / pitch */
    int    has_fov;   float fov;
    int    has_speed; float speed;    /* units per second to reach this point from the previous one */
    int    has_time;  float time;     /* seconds to reach it (wins over speed; 0 = a cut) */
    float  hold;                      /* seconds to stay */
    int    ease;                      /* MHC_EASE_* */
    int    has_tension; float tension;   /* the curve's tension on the leg that starts here (0.5 = Catmull-Rom) */
    int    control;                   /* first / last point of a move: shapes the curve's end, never travelled */
    int    pad;                       /* the room hint: a pad in the room the camera stands in while at this point or on
                                       * the leg that starts here (the original pad number); -1: none, the floor under the camera */
} MissionCamPoint;

static inline void mhcRefInit(MissionCamRef *r) { memset(r, 0, sizeof *r); r->kind = MHC_T_NONE; }
static inline void mhcPointInit(MissionCamPoint *p)
{
    memset(p, 0, sizeof *p);
    p->pos.kind = MHC_T_NONE;
    p->look.kind = MHC_T_NONE;
    p->pad = -1;
}

typedef struct MissionCinemaAnim {
    int    from, to;      /* frames (30 per second), -1: the whole animation */
    double speed;         /* 1 = normal */
    int    hold, reverse, loop;
    double blend;         /* seconds to blend in, < 0: the game's default */
    int    move;          /* 0: no root translation */
} MissionCinemaAnim;

typedef struct MissionHostCinema {
    void *ud;
    /* cinema */
    void (*begin)(void *ud, int keep);                    /* MHC_KEEP_* */
    void (*finish)(void *ud);                             /* control back, camera back, chrs shown, weapons back */
    void (*hide_all_chrs)(void *ud, int on);
    int  (*skipped)(void *ud);                            /* a player pressed a button since begin */
    int  (*in_intro)(void *ud);
    /* camera: 1 when it took, 0 when refused (the reason is in the log) */
    int  (*camera_fixed)(void *ud, int at_kind, int at_index, int look_at_bond);
    int  (*camera_set)(void *ud, const MissionCamPoint *pt);                          /* a cut to the point */
    int  (*camera_move)(void *ud, const MissionCamPoint *pts, int n, int loop, int smooth);
    int  (*camera_moving)(void *ud);                      /* a move (not a loop, not an orbit) is under way */
    void (*camera_stop)(void *ud);                        /* the camera stays where it is */
    int  (*camera_orbit)(void *ud, const MissionCamRef *centres, int ncentres, double distance, double height,
                         double look_height, double start_deg, double speed_dps);
    void (*camera_reset)(void *ud);
    /* screen */
    void (*fade)(void *ud, int to_black, double seconds);
    int  (*fading)(void *ud);
    /* credits: roll returns 0 when this level carries no credits (done() is then true at once) */
    int  (*credits_roll)(void *ud);
    int  (*credits_done)(void *ud);
    /* credits.show: the scripted credits (mission_host_credits.h) start now over every view; the host copies the list.
     * credits.stop ends them; credits_done is then true. 1 when it took */
    int  (*credits_show)(void *ud, const MissionCredits *credits);
    void (*credits_stop)(void *ud);
    /* credits.rows: the level's own credits block (the Cuba level's; 0 rows elsewhere), two text ids per row (a row with
     * the "nothing here" id 0x5011 in a column has no text in it), up to max rows; ids has 2 * max entries */
    int  (*credits_rows)(void *ud, unsigned short *ids, int max);
    /* bond_body(player): 1 when it took, 0 when the player has no body or the request is refused */
    int  (*body_teleport)(void *ud, int player, int pad);
    int  (*body_anim)(void *ud, int player, const char *name, int id, const MissionCinemaAnim *opts);
    int  (*body_anim_done)(void *ud, int player);
    int  (*body_hold_item)(void *ud, int player, int item, int left_hand);
    void (*body_show)(void *ud, int player, int on);
    void (*body_remove)(void *ud, int player);   /* the third-person body is taken away; the hands are the player's again */
    void (*body_fade)(void *ud, int player, float to, float seconds);   /* opacity 0..1 from the current one; 0 s: at once */
    void (*body_set_shadow)(void *ud, int player, int on);
    /* #165 second pass. aim / fire at a target: tkind MHC_T_PROP (target: the guard's chr number), MHC_T_PAD (target:
     * the original pad number) or MHC_T_PLAYER (target: 0-based player, -1: the first living one); 0: the body has no
     * weapon or no body */
    int  (*body_aim)(void *ud, int player, int tkind, int target, int kneel, int aim_only);
    int  (*body_walk)(void *ud, int player, int pad, int speed);        /* speed 0 walk, 1 run, 2 sprint */
    void (*body_drop_item)(void *ud, int player, int left_hand);
    void (*body_set_gravity)(void *ud, int player, int on);
    void (*body_set_flags)(void *ud, int player, int mask, int values); /* MHC_BF_* bits in mask are set to their bit in values */
    void (*chr_set_flags)(void *ud, int chr, int mask, int values);     /* the same on a guard's chr (its literal chr number) */
    void (*body_set_invincible)(void *ud, int player, int on);
    /* set_hidden(false) on a guard while hide_all_chrs(true) is in force: shows it; 0: not handled (no hide-all in force
     * or no such chr), the guard domain's set_hidden then does it */
    int  (*show_hidden_chr)(void *ud, int chr);
    /* the player methods */
    void (*set_frozen)(void *ud, int player, int on);
    void (*hide_weapon)(void *ud, int player, int on);
    int  (*in_cutscene)(void *ud, int player);
} MissionHostCinema;

/* ======================================================================== the camera director
 * State: STILL (a held point, tracking its targets), MOVE (travelling through waypoints), ORBIT (circling a centre).
 * mhcStep(dt) gives the pose of this frame; the host writes it into the game's camera. */
#define MHC_MAX_POINTS  128   /* waypoints, not a sampled curve: the director draws the spline (tension, control points) */
#define MHC_MAX_CENTRES 8

typedef struct MhcPose { float pos[3]; float yaw, pitch, fov; int pad; } MhcPose;   /* degrees; fov 0: not controlled; pad: the room hint, -1 none */
/* Live position of a reference (a tracked guard, a player ...): 1 when known */
typedef int (*MhcResolve)(void *ctx, const MissionCamRef *r, float out[3]);

enum { MHC_OFF = 0, MHC_STILL, MHC_MOVE, MHC_ORBIT };

typedef struct MhcDirector {
    int  mode;
    int  loop, smooth, n, lap0;
    MissionCamPoint pts[MHC_MAX_POINTS];
    MissionCamPoint ctrlPrev, ctrlNext;   /* a move's control-only end points (control = 1) */
    int  hasCtrlPrev, hasCtrlNext;
    float effYaw[MHC_MAX_POINTS], effPitch[MHC_MAX_POINTS], effFov[MHC_MAX_POINTS];   /* explicit, else inherited */
    MhcPose from, cur;
    int  hasCur;
    int  seg, holding;
    double t, dur, hold;
    MissionCamRef centres[MHC_MAX_CENTRES];
    int  ncentres;
    double angle, step, dist, height, lookHeight;   /* orbit: rad, rad per second, units */
} MhcDirector;

#define MHC_DEG 57.29577951308232
#define MHC_PI  3.14159265358979323846

static inline float mhcAngLerp(float a, float b, double u)
{
    float d = (float)fmod((double)b - (double)a + 540.0, 360.0) - 180.0f;   /* the shortest way round */
    return (float)(a + d * u);
}

static inline double mhcEase(int ease, double u)
{
    if (u <= 0) return 0;
    if (u >= 1) return 1;
    switch (ease) {
    case MHC_EASE_LINEAR: return u;
    case MHC_EASE_IN:     return u * u;
    case MHC_EASE_OUT:    return 1.0 - (1.0 - u) * (1.0 - u);
    default:              return u * u * (3.0 - 2.0 * u);
    }
}

static inline void mhcLookAngles(const float from[3], const float to[3], float *yaw, float *pitch)
{
    const double dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    const double l = sqrt(dx * dx + dy * dy + dz * dz);
    if (dx != 0 || dz != 0) *yaw = (float)(atan2(dx, dz) * MHC_DEG);
    *pitch = l > 1e-6 ? (float)(asin(dy / l) * MHC_DEG) : 0.0f;
}

static inline int mhcRefPos(const MissionCamRef *r, void *ctx, MhcResolve res, float out[3])
{
    if (r->kind == MHC_T_NONE) return 0;
    if (r->kind == MHC_T_XYZ) { memcpy(out, r->xyz, sizeof(float) * 3); return 1; }
    if (res && res(ctx, r, out)) return 1;
    if (r->has_xyz) { memcpy(out, r->xyz, sizeof(float) * 3); return 1; }
    return 0;
}

static inline int mhcPointPos(const MissionCamPoint *p, void *ctx, MhcResolve res, float out[3])
{
    if (!mhcRefPos(&p->pos, ctx, res, out)) return 0;
    out[0] += p->offset[0]; out[1] += p->offset[1]; out[2] += p->offset[2];
    return 1;
}

/* Fill the inherited angles: a point with no yaw / pitch / fov keeps the previous one's. */
static inline void mhcInherit(MhcDirector *d)
{
    float y = d->hasCur ? d->cur.yaw : 0, p = d->hasCur ? d->cur.pitch : 0, f = d->hasCur ? d->cur.fov : 0;
    for (int i = 0; i < d->n; i++) {
        if (d->pts[i].has_yaw) y = d->pts[i].yaw;
        if (d->pts[i].has_pitch) p = d->pts[i].pitch;
        if (d->pts[i].has_fov) f = d->pts[i].fov;
        d->effYaw[i] = y; d->effPitch[i] = p; d->effFov[i] = f;
    }
}

/* camera.set: hold one point (its targets tracked) */
static inline void mhcSet(MhcDirector *d, const MissionCamPoint *pt)
{
    d->n = 1;
    d->pts[0] = *pt;
    d->pts[0].hold = 0;
    mhcInherit(d);
    d->mode = MHC_STILL;
}

/* camera.move: travel through the waypoints from where the camera is (a first point with no camera yet is a cut) */
static inline int mhcMove(MhcDirector *d, const MissionCamPoint *pts, int n, int loop, int smooth)
{
    d->hasCtrlPrev = d->hasCtrlNext = 0;
    /* control-only end points shape the first / last leg (the cardinal spline's outer neighbours) and are not travelled */
    if (n >= 3 && pts[0].control) { d->ctrlPrev = pts[0]; d->hasCtrlPrev = 1; pts++; n--; }
    if (n >= 3 && pts[n - 1].control) { d->ctrlNext = pts[n - 1]; d->hasCtrlNext = 1; n--; }
    if (n < 1) return 0;
    if (n > MHC_MAX_POINTS) n = MHC_MAX_POINTS;
    memcpy(d->pts, pts, sizeof(MissionCamPoint) * (size_t)n);
    d->n = n;
    d->loop = loop;
    d->smooth = smooth;
    d->lap0 = 1;
    d->from = d->cur;
    d->seg = 0; d->holding = 0; d->t = 0; d->dur = -1; d->hold = 0;
    mhcInherit(d);
    d->mode = MHC_MOVE;
    return 1;
}

static inline void mhcOrbit(MhcDirector *d, const MissionCamRef *centres, int n, double distance, double height,
                            double look_height, double start_deg, double speed_dps)
{
    if (n > MHC_MAX_CENTRES) n = MHC_MAX_CENTRES;
    memcpy(d->centres, centres, sizeof(MissionCamRef) * (size_t)n);
    d->ncentres = n;
    d->angle = start_deg / MHC_DEG;
    d->step = speed_dps / MHC_DEG;
    d->dist = distance; d->height = height; d->lookHeight = look_height;
    d->mode = MHC_ORBIT;
}

/* Freeze the camera where it is (camera.stop): a held point with no targets. */
static inline void mhcFreeze(MhcDirector *d)
{
    MissionCamPoint p;
    mhcPointInit(&p);
    p.pos.kind = MHC_T_XYZ;
    memcpy(p.pos.xyz, d->cur.pos, sizeof p.pos.xyz);
    p.has_yaw = p.has_pitch = 1; p.yaw = d->cur.yaw; p.pitch = d->cur.pitch;
    if (d->cur.fov > 0) { p.has_fov = 1; p.fov = d->cur.fov; }
    mhcSet(d, &p);
}

static inline int mhcPt(const MhcDirector *d, int i, void *ctx, MhcResolve res, float out[3])
{
    if (mhcPointPos(&d->pts[i], ctx, res, out)) return 1;
    memcpy(out, d->cur.pos, sizeof(float) * 3);
    return 0;
}

static inline int mhcWrap(const MhcDirector *d, int i) { return ((i % d->n) + d->n) % d->n; }

/* A cardinal (Hermite) spline leg p1 -> p2, its tangents tension * (p2 - p0) and tension * (p3 - p1); tension 0.5 is
 * Catmull-Rom. Not smooth: a straight line. */
static inline void mhcCardinal(float out[3], const float p0[3], const float p1[3], const float p2[3], const float p3[3],
                               double u, int smooth, double tension)
{
    const double u2 = u * u, u3 = u2 * u;
    const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    for (int k = 0; k < 3; k++) {
        if (!smooth) { out[k] = (float)(p1[k] + (p2[k] - p1[k]) * u); continue; }
        const double m1 = tension * (p2[k] - p0[k]), m2 = tension * (p3[k] - p1[k]);
        out[k] = (float)(h00 * p1[k] + h10 * m1 + h01 * p2[k] + h11 * m2);
    }
}

/* the pose of the segment travelling to point `s`, `u` (eased) of the way */
static inline void mhcSample(MhcDirector *d, void *ctx, MhcResolve res, double u, MhcPose *out)
{
    const int s = d->seg;
    const int first = (s == 0 && d->lap0);
    const int prv = first ? -1 : mhcWrap(d, s - 1);
    float p0[3], p1[3], p2[3], p3[3];
    float ya, pa, yb, pb, fa, fb;
    if (first) memcpy(p1, d->from.pos, sizeof p1); else mhcPt(d, prv, ctx, res, p1);
    mhcPt(d, s, ctx, res, p2);
    if (d->loop && !first && (s >= 2 || d->n > 2)) mhcPt(d, mhcWrap(d, s - 2), ctx, res, p0);
    else if (!first && s >= 2) mhcPt(d, s - 2, ctx, res, p0);
    else if (!first && s == 1 && d->hasCtrlPrev && mhcPointPos(&d->ctrlPrev, ctx, res, p0)) { /* the control point */ }
    else if (!first && s == 1) memcpy(p0, d->from.pos, sizeof p0);
    else memcpy(p0, p1, sizeof p0);
    if (d->loop) mhcPt(d, mhcWrap(d, s + 1), ctx, res, p3);
    else if (s + 1 < d->n) mhcPt(d, s + 1, ctx, res, p3);
    else if (!(d->hasCtrlNext && mhcPointPos(&d->ctrlNext, ctx, res, p3))) memcpy(p3, p2, sizeof p3);
    {
        /* the leg's tension is its start point's (the previous one; the first leg: its end point's) */
        const MissionCamPoint *tp = first ? &d->pts[s] : &d->pts[prv];
        mhcCardinal(out->pos, p0, p1, p2, p3, u, d->smooth, tp->has_tension ? tp->tension : 0.5);
        out->pad = tp->pad;   /* the room hint of the leg is its start point's too */
    }

    /* angles: a look_at point is tracked from the camera's own position; the others use their (inherited) values */
    if (first) { ya = d->from.yaw; pa = d->from.pitch; fa = d->from.fov; }
    else { ya = d->effYaw[prv]; pa = d->effPitch[prv]; fa = d->effFov[prv]; }
    yb = d->effYaw[s]; pb = d->effPitch[s]; fb = d->effFov[s];
    {
        float t[3];
        if (!first && mhcRefPos(&d->pts[prv].look, ctx, res, t)) mhcLookAngles(out->pos, t, &ya, &pa);
        if (mhcRefPos(&d->pts[s].look, ctx, res, t)) mhcLookAngles(out->pos, t, &yb, &pb);
    }
    out->yaw = mhcAngLerp(ya, yb, u);
    out->pitch = (float)(pa + (pb - pa) * u);
    out->fov = (fa > 0 && fb > 0) ? (float)(fa + (fb - fa) * u) : (fb > 0 ? fb : fa);
}

static inline double mhcSegDur(MhcDirector *d, void *ctx, MhcResolve res)
{
    const MissionCamPoint *pt = &d->pts[d->seg];
    const int first = (d->seg == 0 && d->lap0);
    if (first && !d->hasCur) return 0;           /* nothing to travel from: the first point is a cut */
    if (pt->has_time) return pt->time > 0 ? pt->time : 0;
    if (pt->has_speed && pt->speed > 0) {
        float a[3], b[3];
        if (first) memcpy(a, d->from.pos, sizeof a); else mhcPt(d, mhcWrap(d, d->seg - 1), ctx, res, a);
        mhcPt(d, d->seg, ctx, res, b);
        const double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
        return sqrt(dx * dx + dy * dy + dz * dz) / pt->speed;
    }
    return 1.0;
}

/* One frame. Returns 1 when `out` is a pose to show; *finished is set the frame a (non-looping) move ends. */
static inline int mhcStep(MhcDirector *d, double dt, void *ctx, MhcResolve res, MhcPose *out, int *finished)
{
    if (finished) *finished = 0;
    if (d->mode == MHC_OFF) return 0;

    if (d->mode == MHC_ORBIT) {
        float c[3] = { 0, 0, 0 }, t[3];
        int k = 0;
        for (int i = 0; i < d->ncentres; i++)
            if (mhcRefPos(&d->centres[i], ctx, res, t)) { c[0] += t[0]; c[1] += t[1]; c[2] += t[2]; k++; }
        if (!k) return 0;
        c[0] /= k; c[1] /= k; c[2] /= k;
        d->angle += d->step * dt;
        out->pos[0] = c[0] + (float)(sin(d->angle) * d->dist);
        out->pos[1] = c[1] + (float)(d->lookHeight + d->height);
        out->pos[2] = c[2] + (float)(cos(d->angle) * d->dist);
        t[0] = c[0]; t[1] = c[1] + (float)d->lookHeight; t[2] = c[2];
        mhcLookAngles(out->pos, t, &out->yaw, &out->pitch);
        out->fov = 0;
        out->pad = -1;
    } else if (d->mode == MHC_STILL) {
        const MissionCamPoint *pt = &d->pts[0];
        float t[3];
        if (!mhcPointPos(pt, ctx, res, out->pos)) memcpy(out->pos, d->cur.pos, sizeof out->pos);
        out->yaw = d->effYaw[0]; out->pitch = d->effPitch[0]; out->fov = d->effFov[0];
        out->pad = pt->pad;
        if (mhcRefPos(&pt->look, ctx, res, t)) mhcLookAngles(out->pos, t, &out->yaw, &out->pitch);
    } else {
        int guard = 0;
        d->t += dt;
        while (guard++ < 64) {
            if (!d->holding) {
                if (d->dur < 0) d->dur = mhcSegDur(d, ctx, res);
                if (d->t < d->dur) break;
                d->t -= d->dur;
                d->holding = 1;
                d->hold = d->pts[d->seg].hold;
            }
            if (d->t < d->hold) break;
            d->t -= d->hold;
            if (d->seg + 1 >= d->n && !d->loop) {
                /* the move ends: the camera stays on the last point, still tracking its targets */
                MhcPose last;
                MissionCamPoint lp = d->pts[d->n - 1];
                const float y = d->effYaw[d->n - 1], p = d->effPitch[d->n - 1], f = d->effFov[d->n - 1];
                d->seg = d->n - 1; d->holding = 1;
                mhcSample(d, ctx, res, 1.0, &last);
                lp.hold = 0;
                d->pts[0] = lp;
                d->n = 1;
                d->effYaw[0] = y; d->effPitch[0] = p; d->effFov[0] = f;
                d->mode = MHC_STILL;
                d->cur = last;
                d->hasCur = 1;
                *out = last;
                if (finished) *finished = 1;
                return 1;
            }
            if (d->seg + 1 >= d->n) { d->seg = 0; d->lap0 = 0; } else d->seg++;
            d->holding = 0;
            d->dur = -1;
        }
        {
            const double u = d->holding ? 1.0 : (d->dur > 0 ? d->t / d->dur : 1.0);
            mhcSample(d, ctx, res, mhcEase(d->pts[d->seg].ease, u), out);
        }
    }
    d->cur = *out;
    d->hasCur = 1;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif
