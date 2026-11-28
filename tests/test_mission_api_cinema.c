/* The cinema domain of the campaign API (#165, port/mission/mission_lua_cinema.cpp) driven by a fake host: the
 * cinema / camera / screen / credits tables, bond_body and its methods, the player methods, the wrap of the shared
 * method names (a target that is not a body falls through to "nothing earlier": false), a host without the domain,
 * and the "intro_done" event reaching a script. */
#include "ge_test.h"
#include "mission.h"
#include "mission_lua.h"
#include "mission_host_cinema.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

typedef struct Fake {
    int nplayers;
    char msgs[64][96];
    int nmsg;
    char logs[64][160];
    int nlog;
    char calls[4096];                 /* what the host was asked, one "name(args);" each */
    int moving, fading, animDone, introDone, skipped, creditsDone;
} Fake;

static void callf(Fake *f, const char *fmt, ...)
{
    char b[160];
    va_list ap;
    size_t n = strlen(f->calls);
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n + strlen(b) + 2 < sizeof f->calls) { strcat(f->calls, b); strcat(f->calls, ";"); }
}

static void fLog(void *u, const char *l) { Fake *f = u; if (getenv("LUA_TEST_V")) printf("  [log] %s\n", l); if (f->nlog < 64) snprintf(f->logs[f->nlog++], 160, "%s", l); }
static void fMsg(void *u, const char *t) { Fake *f = u; if (f->nmsg < 64) snprintf(f->msgs[f->nmsg++], 96, "%s", t); }
static int  fPc(void *u) { return ((Fake *)u)->nplayers; }
static int  fPpos(void *u, int p, float *xyz) { (void)u; (void)p; xyz[0] = xyz[1] = xyz[2] = 0; return 1; }
static int  fPropCount(void *u) { (void)u; return 8; }
static int  fPropType(void *u, int i) { (void)u; (void)i; return 3; }
static int  fPropPos(void *u, int i, float *xyz) { (void)u; xyz[0] = (float)i; xyz[1] = xyz[2] = 0; return 1; }
static int  fTagProp(void *u, int tag) { (void)u; return tag == 5 ? 4 : -1; }

static void cBegin(void *u, int keep) { callf(u, "begin(%d)", keep); }
static void cFinish(void *u) { callf(u, "finish"); }
static void cHide(void *u, int on) { callf(u, "hide_all_chrs(%d)", on); }
static int  cSkipped(void *u) { return ((Fake *)u)->skipped; }
static int  cInIntro(void *u) { (void)u; return 1; }
static int  cFixed(void *u, int k, int i, int look) { callf(u, "fixed(%d,%d,%d)", k, i, look); return 1; }
static int  cOrbit(void *u, const MissionCamRef *c, int n, double d, double h, double lh, double sa, double sp)
{
    char b[96] = "";
    for (int i = 0; i < n; i++) { char t[24]; snprintf(t, sizeof t, "%s%d:%d", i ? "+" : "", c[i].kind, c[i].index); strcat(b, t); }
    callf(u, "orbit(%s,%.0f,%.0f,%.0f,%.0f,%.0f)", b, d, h, lh, sa, sp);
    return 1;
}
static int cSet(void *u, const MissionCamPoint *p)
{
    callf(u, "set(%d:%d,%.0f,%.0f,%.0f,yaw%d=%.0f,pitch%d=%.0f,fov%d=%.0f,look%d)", p->pos.kind, p->pos.index, p->pos.xyz[0], p->pos.xyz[1],
          p->pos.xyz[2], p->has_yaw, p->yaw, p->has_pitch, p->pitch, p->has_fov, p->fov, p->look.kind);
    return 1;
}
static int cMove(void *u, const MissionCamPoint *p, int n, int loop, int smooth)
{
    Fake *f = u;
    callf(f, "move(n=%d,loop=%d,smooth=%d)", n, loop, smooth);
    for (int i = 0; i < n; i++)
        callf(f, "pt%d(%d:%d,off=%.0f,time%d=%.1f,speed%d=%.0f,hold=%.1f,ease=%d,look=%d:%d)", i, p[i].pos.kind, p[i].pos.index, p[i].offset[1],
              p[i].has_time, p[i].time, p[i].has_speed, p[i].speed, p[i].hold, p[i].ease, p[i].look.kind, p[i].look.index);
    f->moving = 1;
    return 1;
}
static int  cMoving(void *u) { return ((Fake *)u)->moving; }
static void cStop(void *u) { callf(u, "stop"); ((Fake *)u)->moving = 0; }
static void cCamReset(void *u) { callf(u, "camera_reset"); }
static void cFade(void *u, int black, double s) { Fake *f = u; callf(f, "fade(%d,%.1f)", black, s); f->fading = 1; }
static int  cFading(void *u) { return ((Fake *)u)->fading; }
static int  cCreditsRoll(void *u) { callf(u, "credits_roll"); return 1; }
static int  cCreditsDone(void *u) { return ((Fake *)u)->creditsDone; }
static int  cCreditsShow(void *u, const MissionCredits *c)
{
    callf(u, "credits_show(n=%d,speed=%.1f,black=%d,total=%.1f)", c->n, c->speed, c->black, c->total);
    for (int i = 0; i < c->n; i++) {
        const MhcrEntry *e = &c->e[i];
        callf(u, "cr%d(k%d,s%d,a%d,y%.1f,'%s','%s')", i, e->kind, e->style, e->align, e->y, e->left, e->right);
    }
    return 1;
}
static void cCreditsStop(void *u) { callf(u, "credits_stop"); }
static int  cCreditsRows(void *u, unsigned short *ids, int max)
{
    static const unsigned short rows[] = { 0x5010, 0x5011, 0x5011, 0x5011, 0x5012, 0x5013 };
    int n = 3 < max ? 3 : max;
    (void)u;
    memcpy(ids, rows, (size_t)n * 2 * sizeof rows[0]);
    return n;
}
static int  cTeleport(void *u, int p, int pad) { callf(u, "teleport(%d,%d)", p, pad); return pad == 700 ? 0 : 1; }
static int  cAnim(void *u, int p, const char *name, int id, const MissionCinemaAnim *o)
{
    callf(u, "anim(%d,%s,%d,%d,%d,%.1f,%d,%d,%d,%.1f,%d)", p, name ? name : "-", id, o->from, o->to, o->speed, o->hold, o->reverse,
          o->loop, o->blend, o->move);
    return 1;
}
static int  cAnimDone(void *u, int p) { (void)p; return ((Fake *)u)->animDone; }
static int  cHold(void *u, int p, int item, int left) { callf(u, "hold_item(%d,%d,%d)", p, item, left); return 1; }
static void cShow(void *u, int p, int on) { callf(u, "show(%d,%d)", p, on); }
static void cBodyRemove(void *u, int p) { callf(u, "bodyremove(%d)", p); }
static void cBodyFade(void *u, int p, float to, float s) { callf(u, "fade(%d,%.2f,%.2f)", p, to, s); }
static void cShadow(void *u, int p, int on) { callf(u, "shadow(%d,%d)", p, on); }
static void cFrozen(void *u, int p, int on) { callf(u, "frozen(%d,%d)", p, on); }
static void cHideWeapon(void *u, int p, int on) { callf(u, "hide_weapon(%d,%d)", p, on); }
static int  cInCutscene(void *u, int p) { (void)u; return p == 1; }
static int  cAim(void *u, int p, int tk, int t, int kneel, int aim) { callf(u, "aim(%d,%d,%d,%d,%d)", p, tk, t, kneel, aim); return t != 99; }
static int  cWalk(void *u, int p, int pad, int sp) { callf(u, "walk(%d,%d,%d)", p, pad, sp); return 1; }
static void cDrop(void *u, int p, int left) { callf(u, "drop(%d,%d)", p, left); }
static void cGravity(void *u, int p, int on) { callf(u, "gravity(%d,%d)", p, on); }
static void cFlags(void *u, int p, int mask, int v) { callf(u, "flags(%d,%d,%d)", p, mask, v); }
static void cChrFlags(void *u, int chr, int mask, int v) { callf(u, "chrflags(%d,%d,%d)", chr, mask, v); }
static void cInv(void *u, int p, int on) { callf(u, "inv(%d,%d)", p, on); }
static int  cShowHidden(void *u, int chr) { callf(u, "showhidden(%d)", chr); return ((Fake *)u)->introDone; }   /* introDone: "hide_all is on" here */
static int  fPropChr(void *u, int p) { (void)u; return p + 100; }
static int  fPadById(void *u, const char *id) { (void)u; return !strcmp(id, "dock") ? 77 : -1; }

static MissionHostCinema cinemaHost(Fake *f)
{
    MissionHostCinema c;
    memset(&c, 0, sizeof c);
    c.ud = f;
    c.begin = cBegin; c.finish = cFinish; c.hide_all_chrs = cHide; c.skipped = cSkipped; c.in_intro = cInIntro;
    c.camera_fixed = cFixed; c.camera_orbit = cOrbit; c.camera_reset = cCamReset;
    c.camera_set = cSet; c.camera_move = cMove; c.camera_moving = cMoving; c.camera_stop = cStop;
    c.fade = cFade; c.fading = cFading; c.credits_roll = cCreditsRoll; c.credits_done = cCreditsDone;
    c.credits_show = cCreditsShow; c.credits_stop = cCreditsStop; c.credits_rows = cCreditsRows;
    c.body_teleport = cTeleport; c.body_anim = cAnim; c.body_anim_done = cAnimDone; c.body_hold_item = cHold;
    c.body_show = cShow; c.body_fade = cBodyFade; c.body_remove = cBodyRemove; c.body_set_shadow = cShadow; c.set_frozen = cFrozen; c.hide_weapon = cHideWeapon;
    c.in_cutscene = cInCutscene;
    c.body_aim = cAim; c.body_walk = cWalk; c.body_drop_item = cDrop; c.body_set_gravity = cGravity;
    c.body_set_flags = cFlags; c.chr_set_flags = cChrFlags; c.body_set_invincible = cInv; c.show_hidden_chr = cShowHidden;
    return c;
}

static MissionHost host(Fake *f, const MissionHostCinema *c)
{
    MissionHost h;
    memset(&h, 0, sizeof h);
    h.ud = f; h.log = fLog; h.message = fMsg; h.player_count = fPc; h.player_pos = fPpos;
    h.prop_count = fPropCount; h.prop_type = fPropType; h.prop_pos = fPropPos; h.tag_prop = fTagProp;
    h.cinema_ext = c;
    h.pad_by_id = fPadById;
    h.prop_chr = fPropChr;
    return h;
}

static int hasMsg(const Fake *f, const char *s)
{
    for (int i = 0; i < f->nmsg; i++) if (!strcmp(f->msgs[i], s)) return 1;
    return 0;
}

static int hasLog(const Fake *f, const char *needle)
{
    for (int i = 0; i < f->nlog; i++) if (strstr(f->logs[i], needle)) return 1;
    return 0;
}

static int called(const Fake *f, const char *s) { return strstr(f->calls, s) != NULL; }

static void run(MissionRuntime *rt, double secs)
{
    for (double t = 0; t < secs; t += 1.0 / 60.0) missionRtTick(rt, 1.0 / 60.0);
}

static MissionRuntime *start(Mission *m, const char *src, Fake *f, const MissionHost *h)
{
    char err[160] = "";
    FILE *fp = fopen("tmp_cinematest/scripts/probe.luau", "wb");
    if (fp) { fputs(src, fp); fclose(fp); }
    missionInit(m);
    if (!missionParse("{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\", \"scripts\": [\n"
                      "  { \"use\": \"probe\", \"target\": { \"kind\": \"mission\" } } ] }", m, err, sizeof err))
        return NULL;
    (void)f;
    return missionRtCreate(m, h, "tmp_cinematest", NULL, err, sizeof err);
}

/* ---- the camera director (mission_host_cinema.h), no host: a fake world of two tracked things ---------- */
static float g_tracked[3];   /* the position of MHC_T_PROP 7 */
static int dirResolve(void *ctx, const MissionCamRef *r, float out[3])
{
    (void)ctx;
    if (r->kind != MHC_T_PROP || r->index != 7) return 0;
    memcpy(out, g_tracked, sizeof g_tracked);
    return 1;
}

static MissionCamPoint xyzPoint(float x, float y, float z)
{
    MissionCamPoint p;
    mhcPointInit(&p);
    p.pos.kind = MHC_T_XYZ; p.pos.xyz[0] = x; p.pos.xyz[1] = y; p.pos.xyz[2] = z;
    return p;
}

static int near_(double a, double b) { return fabs(a - b) < 0.02; }

static void directorTests(void)
{
    static MhcDirector d;
    MhcPose o;
    int fin;
    MissionCamPoint pts[3];

    /* set: a cut, held, with the angles given */
    memset(&d, 0, sizeof d);
    pts[0] = xyzPoint(1, 2, 3); pts[0].has_yaw = 1; pts[0].yaw = 90; pts[0].has_pitch = 1; pts[0].pitch = 20; pts[0].has_fov = 1; pts[0].fov = 40;
    mhcSet(&d, &pts[0]);
    GE_CHECK(mhcStep(&d, 0.5, NULL, dirResolve, &o, &fin) == 1);
    GE_CHECK(near_(o.pos[0], 1) && near_(o.pos[1], 2) && near_(o.pos[2], 3) && near_(o.yaw, 90) && near_(o.pitch, 20) && near_(o.fov, 40));
    GE_CHECK_EQ_INT(fin, 0);
    GE_CHECK_EQ_INT(o.pad, -1);                                         /* no room hint given */
    pts[0].pad = 42;
    mhcSet(&d, &pts[0]);
    mhcStep(&d, 0.1, NULL, dirResolve, &o, &fin);
    GE_CHECK_EQ_INT(o.pad, 42);                                         /* the held point's room hint */

    /* move with a time: linear halfway; the first point is a cut when there is no camera yet; hold; finish once */
    memset(&d, 0, sizeof d);
    pts[0] = xyzPoint(0, 0, 0); pts[0].has_yaw = 1; pts[0].yaw = 0;
    pts[0].pad = 7;
    pts[1] = xyzPoint(100, 0, 0); pts[1].has_time = 1; pts[1].time = 2; pts[1].ease = MHC_EASE_LINEAR; pts[1].hold = 1; pts[1].has_yaw = 1; pts[1].yaw = 80;
    pts[1].pad = 9;
    GE_CHECK(mhcMove(&d, pts, 2, 0, 0));
    mhcStep(&d, 0.0, NULL, dirResolve, &o, &fin);
    GE_CHECK(near_(o.pos[0], 0));                                       /* the cut to point 0 */
    mhcStep(&d, 1.0, NULL, dirResolve, &o, &fin);
    GE_CHECK(near_(o.pos[0], 50) && near_(o.yaw, 40));                  /* halfway in 2 s: position and yaw */
    GE_CHECK_EQ_INT(o.pad, 7);                                          /* the leg's room hint is its start point's */
    GE_CHECK_EQ_INT(d.mode, MHC_MOVE);
    mhcStep(&d, 1.0, NULL, dirResolve, &o, &fin);
    GE_CHECK(near_(o.pos[0], 100) && near_(o.yaw, 80));                 /* arrived, holding */
    GE_CHECK_EQ_INT(fin, 0);
    GE_CHECK_EQ_INT(d.mode, MHC_MOVE);
    mhcStep(&d, 0.5, NULL, dirResolve, &o, &fin);
    GE_CHECK_EQ_INT(fin, 0);                                            /* 0.5 s of the 1 s hold */
    mhcStep(&d, 0.6, NULL, dirResolve, &o, &fin);
    GE_CHECK_EQ_INT(fin, 1);                                            /* the move ends: once */
    GE_CHECK_EQ_INT(d.mode, MHC_STILL);                                 /* the camera stays on the last point */
    mhcStep(&d, 1.0, NULL, dirResolve, &o, &fin);
    GE_CHECK_EQ_INT(fin, 0);
    GE_CHECK(near_(o.pos[0], 100));

    /* control-only end points shape the curve and are not travelled; the leg's tension is its start point's */
    {
        MissionCamPoint cp[4];
        for (int ten = 0; ten < 2; ten++) {
            memset(&d, 0, sizeof d);
            cp[0] = xyzPoint(0, 0, -100); cp[0].control = 1;
            cp[1] = xyzPoint(0, 0, 0); cp[1].has_time = 1; cp[1].time = 0; cp[1].has_tension = 1; cp[1].tension = ten ? 0.5f : 0.0f;
            cp[2] = xyzPoint(100, 0, 0); cp[2].has_time = 1; cp[2].time = 2; cp[2].ease = MHC_EASE_LINEAR;
            cp[3] = xyzPoint(100, 0, 100); cp[3].control = 1;
            GE_CHECK(mhcMove(&d, cp, 4, 0, 1));
            GE_CHECK_EQ_INT(d.n, 2);                                    /* the two control points are not waypoints */
            mhcStep(&d, 0.0, NULL, dirResolve, &o, &fin);
            GE_CHECK(near_(o.pos[0], 0) && near_(o.pos[2], 0));        /* the camera starts on A, not on the control point */
            mhcStep(&d, 0.5, NULL, dirResolve, &o, &fin);               /* a quarter of the 2 s leg */
            if (ten) GE_CHECK(fabs(o.pos[0] - 20.3125) < 0.05 && fabs(o.pos[2] - 4.6875) < 0.05);   /* Hermite, tension 0.5 */
            else GE_CHECK(fabs(o.pos[0] - 15.625) < 0.05 && fabs(o.pos[2]) < 0.05);                 /* tension 0: on the line */
        }
    }

    /* a move from an existing camera travels to the first point; speed gives the time; ease in / out / in_out */
    memset(&d, 0, sizeof d);
    pts[0] = xyzPoint(0, 0, 0);
    mhcSet(&d, &pts[0]);
    mhcStep(&d, 0.0, NULL, dirResolve, &o, &fin);
    pts[0] = xyzPoint(200, 0, 0); pts[0].has_speed = 1; pts[0].speed = 100; pts[0].ease = MHC_EASE_LINEAR;   /* 2 s */
    mhcMove(&d, pts, 1, 0, 0);
    mhcStep(&d, 1.0, NULL, dirResolve, &o, &fin);
    GE_CHECK(near_(o.pos[0], 100));
    GE_CHECK(near_(mhcEase(MHC_EASE_IN, 0.5), 0.25) && near_(mhcEase(MHC_EASE_OUT, 0.5), 0.75) && near_(mhcEase(MHC_EASE_IN_OUT, 0.25), 0.15625));
    GE_CHECK(near_(mhcEase(MHC_EASE_IN_OUT, 0.5), 0.5) && near_(mhcEase(MHC_EASE_LINEAR, 0.3), 0.3));

    /* the shortest way round: 350 -> 10 passes 0, not 180 */
    GE_CHECK(near_(mhcAngLerp(350, 10, 0.5), 360) || near_(mhcAngLerp(350, 10, 0.5), 0));
    GE_CHECK(near_(mhcAngLerp(10, 350, 0.25), 5));

    /* smooth: a Catmull-Rom curve passes through the middle waypoint and bends (not the straight line) */
    memset(&d, 0, sizeof d);
    pts[0] = xyzPoint(0, 0, 0); pts[0].has_time = 1; pts[0].time = 0;
    pts[1] = xyzPoint(100, 0, 0); pts[1].has_time = 1; pts[1].time = 1; pts[1].ease = MHC_EASE_LINEAR;
    pts[2] = xyzPoint(100, 0, 100); pts[2].has_time = 1; pts[2].time = 1; pts[2].ease = MHC_EASE_LINEAR;
    mhcMove(&d, pts, 3, 0, 1);
    mhcStep(&d, 0.0, NULL, dirResolve, &o, &fin);
    mhcStep(&d, 1.0, NULL, dirResolve, &o, &fin);
    GE_CHECK(near_(o.pos[0], 100) && near_(o.pos[2], 0));               /* through waypoint 1 */
    mhcStep(&d, 0.5, NULL, dirResolve, &o, &fin);
    GE_CHECK(o.pos[0] > 100.0f && o.pos[2] > 40.0f && o.pos[2] < 60.0f);   /* overshoots the corner a little, no straight 100 */
    memset(&d, 0, sizeof d);
    mhcMove(&d, pts, 3, 0, 0);                                          /* the same, linear: exactly the corner */
    mhcStep(&d, 0.0, NULL, dirResolve, &o, &fin);
    mhcStep(&d, 1.5, NULL, dirResolve, &o, &fin);
    GE_CHECK(near_(o.pos[0], 100) && near_(o.pos[2], 50));

    /* look_at is tracked while the target moves; pos can track too */
    memset(&d, 0, sizeof d);
    pts[0] = xyzPoint(0, 0, 0);
    pts[0].look.kind = MHC_T_PROP; pts[0].look.index = 7;
    mhcSet(&d, &pts[0]);
    g_tracked[0] = 0; g_tracked[1] = 0; g_tracked[2] = 100;
    mhcStep(&d, 0.1, NULL, dirResolve, &o, &fin);
    GE_CHECK(near_(o.yaw, 0) && near_(o.pitch, 0));                     /* straight ahead (+z) */
    g_tracked[0] = 100; g_tracked[1] = 100; g_tracked[2] = 0;
    mhcStep(&d, 0.1, NULL, dirResolve, &o, &fin);
    GE_CHECK(near_(o.yaw, 90) && near_(o.pitch, 45));                   /* +x is yaw 90 (turned left), above: pitch up */

    /* loop: never finishes, wraps */
    memset(&d, 0, sizeof d);
    pts[0] = xyzPoint(0, 0, 0); pts[0].has_time = 1; pts[0].time = 1; pts[0].ease = MHC_EASE_LINEAR;
    pts[1] = xyzPoint(10, 0, 0); pts[1].has_time = 1; pts[1].time = 1; pts[1].ease = MHC_EASE_LINEAR;
    mhcMove(&d, pts, 2, 1, 0);
    int fins = 0;
    for (int i = 0; i < 700; i++) { mhcStep(&d, 0.01, NULL, dirResolve, &o, &fin); fins += fin; }
    GE_CHECK_EQ_INT(fins, 0);
    GE_CHECK_EQ_INT(d.mode, MHC_MOVE);

    /* orbit: angle advances by speed (degrees per second); the camera looks at the centre; several centres: midpoint */
    memset(&d, 0, sizeof d);
    {
        MissionCamRef c[2];
        mhcRefInit(&c[0]); mhcRefInit(&c[1]);
        c[0].kind = MHC_T_XYZ; c[0].xyz[0] = 0; c[0].xyz[1] = 0; c[0].xyz[2] = 0;
        c[1].kind = MHC_T_XYZ; c[1].xyz[0] = 100; c[1].xyz[1] = 0; c[1].xyz[2] = 0;
        mhcOrbit(&d, c, 2, 200, 50, 10, 0, 90);                         /* centre (50, 0, 0), 90 deg/s */
        mhcStep(&d, 0.0, NULL, dirResolve, &o, &fin);
        GE_CHECK(near_(o.pos[0], 50) && near_(o.pos[2], 200) && near_(o.pos[1], 60));   /* angle 0: +z of the centre */
        mhcStep(&d, 1.0, NULL, dirResolve, &o, &fin);
        GE_CHECK(near_(o.pos[0], 250) && near_(o.pos[2], 0));           /* a quarter turn: +x */
        GE_CHECK(near_(o.yaw, -90));                                    /* looking back at the centre: -x is yaw -90 */
        GE_CHECK(o.pitch < 0);                                          /* from above the look point */
    }
}

int main(void)
{
    static Mission m;
    Fake f;
    MissionHostCinema c;
    MissionHost h;
    MissionRuntime *rt;

    GE_TEST_BEGIN("mission api cinema");
    directorTests();
    MKDIR("tmp_cinematest");
    MKDIR("tmp_cinematest/scripts");
    {
        FILE *t = fopen("tmp_cinematest/scripts/.w", "wb");
        if (!t) { printf("SKIP: cannot write tmp_cinematest\n"); return GE_TEST_END(); }
        fclose(t);
        remove("tmp_cinematest/scripts/.w");
    }

    /* ---- every verb reaches its callback with the right arguments ------------- */
    memset(&f, 0, sizeof f);
    f.nplayers = 2;
    c = cinemaHost(&f);
    h = host(&f, &c);
    rt = start(&m,
        "cinema.begin({ keep_hud = true, keep_countdown = true })\n"
        "message('skipped ' .. tostring(cinema.skipped()))\n"
        "message('intro ' .. tostring(cinema.in_intro()))\n"
        "cinema.hide_all_chrs(true)\n"
        "camera.fixed(tag(5), { look_at = player(0) })\n"
        "camera.fixed(guard(3))\n"
        "camera.orbit({ centre = player(1), distance = 300, height = 80, look_height = 20, start_angle = 90, speed = 12 })\n"
        "camera.orbit({ centre = guard(2), distance = 100, height = 50 })\n"
        "camera.orbit({ centre = { player(0), guard(2), { x = 1, y = 2, z = 3 } }, distance = 50, height = 5 })\n"
        "camera.set({ pos = { x = 10, y = 20, z = 30 }, yaw = 90, pitch = -10, fov = 45 })\n"
        "camera.set({ pos = guard(3), look_at = player(0), offset = { x = 0, y = 7, z = 0 } })\n"
        "camera.move({ { pos = { x = 0, y = 0, z = 0 }, time = 0 },\n"
        "              { pos = guard(2), offset = { x = 0, y = 150, z = 0 }, speed = 200, hold = 1.5, ease = 'linear', look_at = player(1) },\n"
        "              { pos = player(0), time = 2, yaw = 350 } }, { loop = true, smooth = false })\n"
        "message('moving ' .. tostring(camera.moving()))\n"
        "camera.stop()\n"
        "message('moving ' .. tostring(camera.moving()))\n"
        "camera.wait()\n"
        "message('waited')\n"
        "camera.path(3)\n"
        "camera.reset()\n"
        "screen.fade_out(2)\n"
        "message('fading ' .. tostring(screen.fading()))\n"
        "screen.fade_in()\n"
        "credits.roll()\n"
        "message('credits ' .. tostring(credits.done()))\n"
        "local b = bond_body(1)\n"
        "message('tp ' .. tostring(b:teleport(12)) .. tostring(b:teleport(700)))\n"
        "message('anim ' .. tostring(b:anim(42, { from = 5, to = 60, speed = 2, hold = true, blend = 0.5, move = false })))\n"
        "message('animname ' .. tostring(b:anim('kneel_cough')))\n"
        "message('done ' .. tostring(b:anim_done()))\n"
        "b:hold_item(7, 'left')\n"
        "b:hold_item(9)\n"
        "b:show(true)\n"
        "b:set_shadow(false)\n"
        "player(0):set_frozen(true)\n"
        "player(0):hide_weapon(true)\n"
        "message('cut ' .. tostring(player(0):in_cutscene()) .. tostring(player(1):in_cutscene()))\n"
        "player(0):show(false)\n"
        "bond_body():show(true)\n"
        "bond_body():fade(0, 0.5)\n"
        "bond_body():fade(0.25)\n"
        "bond_body():fade_out()\n"
        "bond_body():remove()\n"
        "cinema.finish()\n"
        "camera.move({ { pos = { x = 5, y = 5, z = 5 } } })\n"
        "camera.wait()\n"
        "message('arrived')\n"
        "wait(event('start'))\n", &f, &h);
    GE_CHECK(rt != NULL);
    if (!rt) return GE_TEST_END();
    run(rt, 0.3);
    if (!called(&f, "finish")) for (int i = 0; i < f.nlog; i++) printf("  log: %s\n", f.logs[i]);
    GE_CHECK(called(&f, "begin(9);"));                              /* keep_hud 1 + keep_countdown 8 */
    GE_CHECK(hasMsg(&f, "skipped false"));
    GE_CHECK(hasMsg(&f, "intro true"));
    GE_CHECK(called(&f, "hide_all_chrs(1);"));
    GE_CHECK(called(&f, "fixed(1,4,1);"));                          /* tag(5) is setup prop 4, look_at a player */
    GE_CHECK(called(&f, "fixed(1,3,0);"));                          /* guard(3): a prop, no look_at */
    GE_CHECK(called(&f, "orbit(2:1,300,80,20,90,12);"));            /* centre player(1) */
    GE_CHECK(called(&f, "orbit(1:2,100,50,0,0,10);"));              /* a prop; look_height 0, speed 10 by default */
    GE_CHECK(called(&f, "orbit(2:0+1:2+3:0,50,5,0,0,10);"));        /* a list of targets */
    GE_CHECK(called(&f, "set(3:0,10,20,30,yaw1=90,pitch1=-10,fov1=45,look-1);"));
    GE_CHECK(called(&f, "set(1:3,3,0,0,yaw0=0,pitch0=0,fov0=0,look2);"));   /* guard 3 at (3,0,0) by the fake host; look_at a player */
    GE_CHECK(called(&f, "move(n=3,loop=1,smooth=0);"));
    GE_CHECK(called(&f, "pt0(3:0,off=0,time1=0.0,speed0=0,hold=0.0,ease=0,look=-1:0);"));
    GE_CHECK(called(&f, "pt1(1:2,off=150,time0=0.0,speed1=200,hold=1.5,ease=1,look=2:1);"));
    GE_CHECK(called(&f, "pt2(2:0,off=0,time1=2.0,speed0=0,hold=0.0,ease=0,look=-1:0);"));
    GE_CHECK(hasMsg(&f, "moving true"));
    GE_CHECK(called(&f, "stop;"));
    GE_CHECK(!hasMsg(&f, "arrived"));                               /* the second camera.wait() sleeps until camera_done */
    GE_CHECK(called(&f, "camera_reset;"));
    GE_CHECK(called(&f, "fade(1,2.0);"));
    GE_CHECK(called(&f, "fade(0,1.0);"));                           /* fade_in() default: one second */
    GE_CHECK(hasMsg(&f, "fading true"));
    GE_CHECK(called(&f, "credits_roll;"));
    GE_CHECK(hasMsg(&f, "credits false"));
    GE_CHECK(called(&f, "teleport(1,12);"));
    GE_CHECK(hasMsg(&f, "tp truefalse"));                           /* the host refused pad 700 */
    GE_CHECK(called(&f, "anim(1,-,42,5,60,2.0,1,0,0,0.5,0);"));
    GE_CHECK(called(&f, "anim(1,kneel_cough,-1,-1,-1,1.0,0,0,0,-1.0,1);"));
    GE_CHECK(hasMsg(&f, "anim true"));
    GE_CHECK(hasMsg(&f, "done false"));
    GE_CHECK(called(&f, "hold_item(1,7,1);"));
    GE_CHECK(called(&f, "hold_item(1,9,0);"));
    GE_CHECK(called(&f, "show(1,1);"));
    GE_CHECK(called(&f, "shadow(1,0);"));
    GE_CHECK(called(&f, "frozen(0,1);"));
    GE_CHECK(called(&f, "hide_weapon(0,1);"));
    GE_CHECK(hasMsg(&f, "cut falsetrue"));
    GE_CHECK(called(&f, "show(0,0);"));                             /* the player's own show */
    GE_CHECK(called(&f, "show(0,1);"));                             /* bond_body() is player 0 */
    GE_CHECK(called(&f, "fade(0,0.00,0.50);"));                       /* a fade over 0.5 s */
    GE_CHECK(called(&f, "fade(0,0.25,0.00);"));                       /* a partial opacity, at once */
    GE_CHECK(called(&f, "bodyremove(0);"));                           /* the body is taken away */
    GE_CHECK(hasMsg(&f, "waited"));                                 /* nothing moving: camera.wait() returns at once */
    f.moving = 0;
    missionRtRaise(rt, "camera_done", NULL, 0);
    run(rt, 0.2);
    GE_CHECK(hasMsg(&f, "arrived"));
    GE_CHECK(called(&f, "finish;"));
    GE_CHECK(hasLog(&f, "camera.path: not implemented yet"));       /* a logged stub */
    GE_CHECK(hasLog(&f, "credits.roll") == 0);                      /* the fake host rolls */
    missionRtDestroy(rt);
    missionFree(&m);

    /* ---- anim_done follows the host; the wrap falls through for a target that is not a body ------------- */
    memset(&f, 0, sizeof f);
    f.nplayers = 1; f.animDone = 1;
    c = cinemaHost(&f);
    h = host(&f, &c);
    rt = start(&m,
        "message('done ' .. tostring(bond_body(0):anim_done()))\n"
        "message('guard anim ' .. tostring(guard(3):anim(7)))\n"           /* no earlier function: false */
        "message('guard done ' .. tostring(guard(3):anim_done()))\n"
        "message('guard tp ' .. tostring(guard(3):teleport(5)))\n"
        "guard(3):show(true)\n"                                            /* no earlier function: nothing */
        "message('end')\n"
        "wait(event('start'))\n", &f, &h);
    GE_CHECK(rt != NULL);
    if (rt) {
        run(rt, 0.3);
        GE_CHECK(hasMsg(&f, "done true"));
        GE_CHECK(hasMsg(&f, "guard anim false"));
        GE_CHECK(hasMsg(&f, "guard done false"));
        GE_CHECK(hasMsg(&f, "guard tp false"));
        GE_CHECK(hasMsg(&f, "end"));
        GE_CHECK(called(&f, "anim(") == 0);                             /* none of them reached the body callbacks */
        GE_CHECK(called(&f, "show(") == 0);
        missionRtDestroy(rt);
    }
    missionFree(&m);

    /* ---- #165 second pass: body aim / fire / walk / drop / gravity / flags / invincible, mission pad ids, set_hidden ---- */
    memset(&f, 0, sizeof f);
    f.nplayers = 2; f.introDone = 1;
    c = cinemaHost(&f);
    h = host(&f, &c);
    rt = start(&m,
        "local b = bond_body(1)\n"
        "message('aim ' .. tostring(b:aim_at(guard(3))))\n"
        "message('fire ' .. tostring(b:fire_at(player(0), { stance = 'kneel', aim_only = true })))\n"
        "message('fire2 ' .. tostring(b:fire_at(pad('dock'))))\n"
        "message('fire3 ' .. tostring(b:fire_at()))\n"
        "message('walk ' .. tostring(b:walk_to('dock', 'run')))\n"
        "b:walk_to(pad(5), 'sprint')\n"
        "b:walk_to(12)\n"
        "message('walkbad ' .. tostring(b:walk_to('nowhere')))\n"
        "b:drop_item('left')\n"
        "b:drop_item()\n"
        "b:set_gravity(false)\n"
        "b:set_body_flags({ hitbox_cull = true, height_lock = false, knockable = true })\n"
        "b:set_body_flags({ sorted = true, no_collision = true })\n"
        "guard(2):set_body_flags({ sorted = true })\n"
        "b:set_invincible(true)\n"
        "message('tp ' .. tostring(b:teleport('dock')))\n"
        "camera.fixed(pad('dock'))\n"
        "guard(3):set_invincible(true)\n"          /* not a body: no earlier function here, nothing */
        "guard(3):set_hidden(false)\n"
        "guard(3):set_hidden(true)\n"
        "message('guard aim ' .. tostring(guard(3):aim_at(guard(2))))\n"
        "message('end')\n"
        "wait(event('start'))\n", &f, &h);
    GE_CHECK(rt != NULL);
    if (rt) {
        run(rt, 0.3);
        GE_CHECK(hasMsg(&f, "end"));
        GE_CHECK(hasMsg(&f, "aim true"));
        GE_CHECK(called(&f, "aim(1,1,103,0,1);"));                      /* a guard: its chr number, aim only */
        GE_CHECK(called(&f, "aim(1,2,0,1,1);"));                        /* player(0), kneel, aim_only */
        GE_CHECK(called(&f, "aim(1,0,77,0,0);"));                       /* a pad by its mission id, fire */
        GE_CHECK(called(&f, "aim(1,2,-1,0,0);"));                       /* no target: the first living player */
        GE_CHECK(called(&f, "walk(1,77,1);"));
        GE_CHECK(hasMsg(&f, "walk true"));
        GE_CHECK(called(&f, "walk(1,5,2);"));
        GE_CHECK(called(&f, "walk(1,12,0);"));
        GE_CHECK(hasMsg(&f, "walkbad false"));                          /* an unknown pad id: refused, with a log */
        GE_CHECK(hasLog(&f, "bond_body:walk_to: no such pad"));
        GE_CHECK(called(&f, "drop(1,1);"));
        GE_CHECK(called(&f, "drop(1,0);"));
        GE_CHECK(called(&f, "gravity(1,0);"));
        GE_CHECK(called(&f, "flags(1,5,5);") == 0);
        GE_CHECK(called(&f, "flags(1,7,5);"));                          /* mask: all three; values: hitbox_cull + knockable */
        GE_CHECK(called(&f, "flags(1,24,24);"));                        /* sorted (8) + no_collision (16) */
        GE_CHECK(called(&f, "chrflags(102,8,8);"));                     /* a guard: the host is given its chr (the fake's prop + 100) */
        GE_CHECK(called(&f, "inv(1,1);"));
        GE_CHECK(hasMsg(&f, "tp true"));
        GE_CHECK(called(&f, "teleport(1,77);"));                        /* a mission pad id on the body */
        GE_CHECK(called(&f, "fixed(0,77,0);"));                         /* and on a camera point */
        GE_CHECK(hasMsg(&f, "guard aim false"));                        /* a guard falls through to the earlier: nothing */
        missionRtDestroy(rt);
    }
    missionFree(&m);

    /* set_hidden(false) on a guard: the host shows it only while hide_all is on; else the earlier function runs */
    memset(&f, 0, sizeof f);
    f.nplayers = 1; f.introDone = 0;
    c = cinemaHost(&f);
    h = host(&f, &c);
    rt = start(&m, "guard(3):set_hidden(false)\nmessage('end')\nwait(event('start'))\n", &f, &h);
    GE_CHECK(rt != NULL);
    if (rt) {
        run(rt, 0.2);
        GE_CHECK(hasMsg(&f, "end"));
        GE_CHECK(called(&f, "showhidden(103);"));                       /* a guard: asked; hide_all off: the host declines */
        missionRtDestroy(rt);
    }
    missionFree(&m);

    /* ---- the "intro_done" event reaches a script ------------------------------------------------------- */
    memset(&f, 0, sizeof f);
    f.nplayers = 1;
    c = cinemaHost(&f);
    h = host(&f, &c);
    rt = start(&m,
        "wait(event('intro_done'))\n"
        "message('go')\n"
        "wait(event('start'))\n", &f, &h);
    GE_CHECK(rt != NULL);
    if (rt) {
        run(rt, 0.2);
        GE_CHECK(!hasMsg(&f, "go"));
        missionRtRaise(rt, "intro_done", NULL, 0);                      /* what missionsHostCinemaTick does */
        run(rt, 0.2);
        GE_CHECK(hasMsg(&f, "go"));
        missionRtDestroy(rt);
    }
    missionFree(&m);

    /* ---- credits.show: the list a script gives reaches the host as the model lays it out --------------- */
    memset(&f, 0, sizeof f);
    f.nplayers = 1;
    c = cinemaHost(&f);
    h = host(&f, &c);
    rt = start(&m,
        "credits.show({\n"
        "  speed = 2, backdrop = 'black', align = 'left',\n"
        "  lines = {\n"
        "    { text = 'Operation Halcyon', style = 'title', align = 'center' },\n"
        "    { gap = 2 },\n"
        "    { text = 'Cast', style = 'heading' },\n"
        "    { left = 'Agent', right = 'Alice Example' },\n"
        "    'Plain line',\n"
        "    { text = 'Right one', align = 'right' },\n"
        "  } })\n"
        "local ok1, e1 = pcall(credits.show, { lines = 5 })\n"
        "message('e1 ' .. tostring(e1))\n"
        "local ok2, e2 = pcall(credits.show, { lines = { true } })\n"
        "message('e2 ' .. tostring(e2))\n"
        "local ok3, e3 = pcall(credits.show, { lines = { { style = 'title' } } })\n"
        "message('e3 ' .. tostring(e3))\n"
        "local ok4, e4 = pcall(credits.show, { lines = { { text = 'x', style = 'huge' } } })\n"
        "message('e4 ' .. tostring(e4))\n"
        "local ok5, e5 = pcall(credits.show, { lines = { 'a' }, backdrop = 'red' })\n"
        "message('e5 ' .. tostring(e5))\n"
        "local many = {}\n"
        "for i = 1, 401 do many[i] = 'line ' .. i end\n"
        "local ok6, e6 = pcall(credits.show, { lines = many })\n"
        "message('e6 ' .. tostring(e6))\n"
        "local rows = credits.rows()\n"
        "message('rows ' .. #rows .. ' ' .. rows[1].text1 .. ' ' .. rows[2].text2 .. ' ' .. rows[3].text2)\n"
        "credits.show({ speed = 99, lines = { 'fast' } })\n"
        "credits.stop()\n"
        "wait(event('start'))\n", &f, &h);
    GE_CHECK(rt != NULL);
    if (rt) {
        run(rt, 0.3);
        if (!called(&f, "credits_stop;")) for (int i = 0; i < f.nlog; i++) printf("  log: %s\n", f.logs[i]);
        GE_CHECK(called(&f, "credits_show(n=6,speed=2.0,black=1,total=9.3);"));   /* 2.6 + 2 + 1.7 + 1 + 1 + 1 */
        GE_CHECK(called(&f, "cr0(k0,s2,a0,y0.0,'Operation Halcyon','');"));
        GE_CHECK(called(&f, "cr1(k2,s0,a0,y2.6,'','');"));                       /* a gap of two lines */
        GE_CHECK(called(&f, "cr2(k0,s1,a1,y4.6,'Cast','');"));                  /* the default alignment: left */
        GE_CHECK(called(&f, "cr3(k1,s0,a0,y6.3,'Agent','Alice Example');"));    /* two columns */
        GE_CHECK(called(&f, "cr4(k0,s0,a1,y7.3,'Plain line','');"));
        GE_CHECK(called(&f, "cr5(k0,s0,a2,y8.3,'Right one','');"));
        GE_CHECK(hasMsg(&f, "e1 credits.show: lines must be a list"));
        GE_CHECK(hasMsg(&f, "e2 credits.show: line 1 is neither a string nor a table"));
        GE_CHECK(hasMsg(&f, "e3 credits.show: line 1 has no text, left, right or gap"));
        GE_CHECK(hasMsg(&f, "e4 credits.show: style must be one of \"text\", \"heading\", \"title\", not \"huge\""));
        GE_CHECK(hasMsg(&f, "e5 credits.show: backdrop must be one of \"world\", \"black\", not \"red\""));
        GE_CHECK(hasMsg(&f, "e6 credits.show: more than 400 lines"));
        GE_CHECK(hasMsg(&f, "rows 3 20496 20497 20499"));                          /* 0x5010, 0x5011, 0x5013 as numbers */
        GE_CHECK(called(&f, "credits_show(n=1,speed=10.0,black=0,total=1.0);"));   /* the speed is clamped */
        GE_CHECK(called(&f, "credits_stop;"));
        missionRtDestroy(rt);
    }
    missionFree(&m);

    /* ---- a host without the domain: neutral values, one log line each, no crash ----------------------- */
    memset(&f, 0, sizeof f);
    f.nplayers = 1;
    h = host(&f, NULL);
    rt = start(&m,
        "cinema.begin()\n"
        "camera.fixed(guard(1))\n"
        "screen.fade_out(1)\n"
        "message('fading ' .. tostring(screen.fading()))\n"
        "message('skipped ' .. tostring(cinema.skipped()))\n"
        "message('credits ' .. tostring(credits.done()))\n"
        "message('tp ' .. tostring(bond_body():teleport(3)))\n"
        "message('cut ' .. tostring(player(0):in_cutscene()))\n"
        "player(0):set_frozen(true)\n"
        "cinema.finish()\n"
        "message('end')\n"
        "wait(event('start'))\n", &f, &h);
    GE_CHECK(rt != NULL);
    if (rt) {
        run(rt, 0.3);
        GE_CHECK(hasMsg(&f, "end"));
        GE_CHECK(hasMsg(&f, "fading false"));
        GE_CHECK(hasMsg(&f, "skipped false"));
        GE_CHECK(hasMsg(&f, "credits true"));
        GE_CHECK(hasMsg(&f, "tp false"));
        GE_CHECK(hasMsg(&f, "cut false"));
        GE_CHECK(hasLog(&f, "cinema.begin: not implemented yet"));
        missionRtDestroy(rt);
    }
    missionFree(&m);

    return GE_TEST_END();
}
