/* The cinema domain of the campaign API (#165): host side, the game's own functions behind
 * port/mission/mission_host_cinema.h. Layout: port/mission/mission_lua_ext.h.
 *
 * Every verb calls what the game's AI command interpreter (src/game/chrai.c) calls for the same cutscene verb:
 *   cinema.begin        AI_BondDisableControl + AI_BondDisableDamageAndPickups
 *   cinema.finish       AI_BondEnableControl (+ AI_CameraReturnToBond, AI_ShowAllChrs, what hide_weapon took)
 *   hide_all_chrs       AI_HideAllChrs / AI_ShowAllChrs
 *   camera.fixed        AI_CameraSwitch (a camera object) / AI_CameraLookAtBondFromPad (a pad)
 *   camera.orbit        AI_CameraOrbitPad (a pad centre); any other centre, camera.set / move: the director (mission_host_cinema.h)
 *                       driving a CutsceneRecord of ours through the game's fixed camera (AI_CameraSwitch)
 *   camera.reset        AI_CameraReturnToBond
 *   screen.fade_*       AI_ScreenFadeToBlack / AI_ScreenFadeFromBlack, AI_IFScreenFadeCompleted
 *   credits.roll / done AI_CreditsRoll / AI_IFCreditsHasCompleted
 *   credits.show / stop our own (mission_host_credits.h): the menu draws the lines over every view (menu_ui.cpp drawCredits)
 *   credits.rows        the level's credits (the pack's levels/cuba/setup.json): their text ids, for a script to turn into credits.show lines
 *   bond_body:teleport  AI_TRYTeleportingChrToPad on CHR_BOND_CINEMA (the player's own chr)
 *   bond_body:anim      AI_PlayAnimation on that chr, anim_done AI_IFPlayingAnimation's test
 *   bond_body:hold_item AI_BondEquipItemCinema; hide_weapon AI_BondHideWeapons
 *   bond_body:aim_at / fire_at  AI_TRYFireOrAimAtTarget (+Kneel) on the player's chr; walk_to AI_WalkToPad / Run / Sprint;
 *                       drop_item AI_ChrDropAllHeldItems (one hand); set_gravity / set_body_flags AI_SetMychrflags
 *                       (LOCK_Y_POS, CULL_USING_HITBOX, IMPACT_ALWAYS); set_invincible the player's cheatBondInvincible + chr flag
 *   set_hidden(false) on a guard during hide_all_chrs(true): AI_ShowAllChrs for that chr
 *   cinema.in_intro     AI_IFCameraIsInIntro / AI_IFCameraIsInBondSwirl
 *
 * Co-op: see mission_host_cinema.h. The game's camera mode is global and a scene's camera is computed for the driver
 * (the first living player) and mirrored by coop.c (coopFrozenCamera, coopMirrorFade); the verbs below run the game
 * calls as that driver for the camera and for every player for freeze, HUD, fades and invulnerability. */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <ultra64.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrai.h"
#include "game/bondview.h"
#include "game/gun.h"
#include "game/lv.h"
#include "texpack.h"
#include "ge_pkg.h"
#include "game/player.h"
#include "game/propobj.h"
#include "game/stan.h"
#include "game/model.h"
#include "boss.h"
#include "system.h"
#include "mission_lua.h"
#include "mission_host_cinema.h"
#include "missions_priv.h"
#include "joy.h"

extern struct player *g_playerPointers[4];
extern PadRecord *g_CameraLookAtBondPad;       /* bondview2.c: the camera globals AI_CameraSwitch / AI_CameraOrbitPad set */
extern CutsceneRecord *gBondViewCutscene;
extern f32 flt_CODE_bss_80079A00;              /* orbit: angle (rad), per-tick step (rad), distance, camera height, look height */
extern f32 flt_CODE_bss_80079A04;
extern f32 flt_CODE_bss_80079A08;
extern f32 flt_CODE_bss_80079A0C;
extern f32 flt_CODE_bss_80079A10;
extern s32 dword_CODE_bss_80079A14;            /* orbit: the pad */
extern enum CAMERAMODE dword_CODE_bss_80079A18; /* CAMERAMODE_INTRO: a camera object looks at Bond */
extern s32 dword_CODE_bss_80079A1C;
extern bool currentPlayerIsFadeComplete(void); /* bondview2.c */
extern MissionRuntime *missionsRuntime(void);  /* missions_host.c */
extern void fogLoadLevelEnvironment(s32 level_id, s32 intro);   /* bgfog.c: 1 = the level's intro environment */


#define NO_PLAYER_SLOTS 4
/* bondaicommands.h's TARGET_* (that header's macros clash with the host struct's member names, so not included) */
#define TARGET_BOND     0x0001
#define TARGET_CHR      0x0004
#define TARGET_PAD      0x0008
#define TARGET_AIM_ONLY 0x0020

static struct {
    int camPad;          /* the current pose's room hint (a pad), -1: none */
    int introEnv;        /* the level's intro environment is loaded (cinema.begin environment = "intro") */
    int    active;            /* between begin and finish */
    int    skipped;
    int    savedTimer, savedInvincible;
    int    hidChrs;           /* hide_all_chrs(true) is in force */
    int    camera;            /* a scripted camera of ours is on */
    int    returnPending;     /* camera.reset / finish asked for the players' views: done at the next tick unless a
                                 new scene camera was taken meanwhile (finish + begin chains two scenes) */
    MhcDirector dir;          /* camera.set / move / orbit */
    int    fovSet;            /* bit per player: zoominfovy was taken over */
    float  fovOld[NO_PLAYER_SLOTS];
    int    introDone;         /* "intro_done" went out */
    int    frozen, frozenSaved;   /* set_frozen(true) is in force, and the is_timer_active it replaced */
    int    creditsFake;       /* credits.roll on a level with no credits: done at once */
    int    crScripted;        /* credits.show was called (done() then follows the scripted credits) */
    int    crActive;          /* the scripted credits are on screen */
    double crT;               /* seconds since they started */
    int    weaponHidden[NO_PLAYER_SLOTS];
    int    disarm[NO_PLAYER_SLOTS];     /* frames until the hands of a player whose body was removed are given back (0: none) */
    int    bodyItem[NO_PLAYER_SLOTS];   /* hold_item before show(true): the body is built holding this item (0: none) */
    int    heldRight[NO_PLAYER_SLOTS], heldLeft[NO_PLAYER_SLOTS];
    int    swirlRaised;       /* "intro_swirl" went out */
    int    aimOnly[NO_PLAYER_SLOTS];    /* the body's last aim_at / fire_at only aims */
    s32    shotStart[NO_PLAYER_SLOTS];  /* g_GlobalTimer when it started (anim_done gives up after 3 s of firing) */
} S = { 0, 0, 1 };

static void say(const char *what)
{
    sysLogPrintf(LOG_INFO, "missions: cinema: %s", what);
}

static int playerOk(int p)
{
    return p >= 0 && p < getPlayerCount() && p < NO_PLAYER_SLOTS && g_playerPointers[p] != NULL;
}

/* The player a scene's camera and scripted game calls run as: the first living one (coop.c coopDriver). */
static int driver(void)
{
    for (int i = 0; i < getPlayerCount() && i < NO_PLAYER_SLOTS; i++)
        if (g_playerPointers[i] != NULL && g_playerPointers[i]->bonddead == 0) return i;
    return 0;
}

/* Run fn for every player (the game's per-player calls act on the current one). */
static void forAll(void (*fn)(void *), void *arg)
{
    for (int p = 0; p < getPlayerCount() && p < NO_PLAYER_SLOTS; p++)
        if (g_playerPointers[p] != NULL) missionsHostWithPlayer(p, fn, arg);
}

static PadRecord *padRec(int pad)
{
    if (pad < 0 || !g_CurrentSetup.pads) return NULL;
    if (isNotBoundPad(pad)) return &g_CurrentSetup.pads[pad];
    return g_CurrentSetup.boundpads ? (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(pad)] : NULL;
}

/* The pad a setup prop sits on (missions_host.c propPad): -1 when it has none. */
static int propPadOf(int prop)
{
    PropDefHeaderRecord *h = (PropDefHeaderRecord *)missionsHostPropRec(prop);
    if (!h) return -1;
    switch (h->type) {
    case PROPDEF_GUARD:     return ((GuardRecord *)h)->PadID;
    case PROPDEF_DOOR:      return ((DoorRecord *)h)->pad + 10000;
    case PROPDEF_CAMERAPOS: return ((CutsceneRecord *)h)->pad;
    default:                return ((ObjectRecord *)h)->pad;
    }
}

/* ---- cinema -------------------------------------------------------------- */

struct beginArg { int keep; };

/* AI_BondDisableControl for one player: the sight and ammo count, the bottom / top message flags, the timer */
static void doBegin(void *a)
{
    const int keep = ((struct beginArg *)a)->keep;
    if (!(keep & MHC_KEEP_HUD)) {
        gunSetSightVisible(GUNSIGHTREASON_NOCONTROL, FALSE);
        gunSetGunAmmoVisible(GUNAMMOREASON_NOCONTROL, FALSE);
    }
    if (!(keep & MHC_KEEP_BOTTOM)) hudmsgsSetOff(PLAYERFLAG_NOCONTROL);
    if (!(keep & MHC_KEEP_TOP_TEXT)) bondviewSetUpperTextDisplayFlag(PLAYERFLAG_NOCONTROL);
}

/* AI_BondEnableControl for one player */
static void doEnd(void *a)
{
    (void)a;
    gunSetSightVisible(GUNSIGHTREASON_NOCONTROL, TRUE);
    gunSetGunAmmoVisible(GUNAMMOREASON_NOCONTROL, TRUE);
    hudmsgsSetOn(PLAYERFLAG_NOCONTROL);
    bondviewClearUpperTextDisplayFlag(2);
}

/* The level's environment (fog, far clip): the intro one or the normal one (what the game's own fixed intro shot and
 * swirl load). Nothing when it is already the one asked for. */
static void setIntroEnv(int on)
{
    if (on == S.introEnv) return;
    fogLoadLevelEnvironment(bossGetStageNum(), on);
    S.introEnv = on;
}

static void hBegin(void *ud, int keep)
{
    struct beginArg a;
    (void)ud;
    a.keep = keep;
    if (!S.active) {
        S.savedTimer = is_timer_active;
        S.savedInvincible = g_PlayerInvincible;
    }
    forAll(doBegin, &a);
    if (!(keep & MHC_KEEP_COUNTDOWN)) countdownTimerSetVisible(16, FALSE);
    /* is_timer_active = 0 puts a player in the FP camera mode on the game's frozen path (bondview2.c: bondviewFrozenMoveBond:
     * no stick, no buttons, the mission clock stops); g_PlayerInvincible = "Bond damage and pickups disabled" (no
     * shot hurts, no pickup is taken: propsTickPlayer, gunfire.c). The guards' sight is the AI lists' own test of
     * Bond's chr (chrCheckTargetInSight), which a scene leaves alone. */
    is_timer_active = FALSE;
    g_PlayerInvincible = TRUE;
    setIntroEnv((keep & MHC_ENV_INTRO) != 0);
    S.active = 1;
    S.skipped = 0;
    say("begin");
}

static void hideWeaponFor(int player, int on);
static void hHideAllChrs(void *ud, int on);
static void hCameraReset(void *ud);

static void hFinish(void *ud)
{
    int p;
    if (!S.active) return;
    forAll(doEnd, NULL);
    countdownTimerSetVisible(16, TRUE);
    /* AI_BondEnableControl: control is back, always. Not "what it was at begin": a scene begun while the level loads sees
     * the title screen's leftover 0 (the level's own init sets 1 after), and restoring that froze the players for good. */
    is_timer_active = TRUE;
    g_PlayerInvincible = S.savedInvincible;
    hHideAllChrs(ud, 0);
    for (p = 0; p < NO_PLAYER_SLOTS; p++)
        if (S.weaponHidden[p] && playerOk(p)) hideWeaponFor(p, 0);
    hCameraReset(ud);
    setIntroEnv(0);
    S.active = 0;
    say("finish: control returned");
}

static void hHideAllChrs(void *ud, int on)
{
    (void)ud;
    if (on) {
        /* AI_HideAllChrs (GE_CHR_HIDEABLE: a co-op player's body is a chr too, and stays) */
        for (s32 n = get_numguards() - 1; n >= 0; n--)
            if (GE_CHR_HIDEABLE(&g_ChrSlots[n])) g_ChrSlots[n].chrflags |= CHRFLAG_HIDDEN;
        S.hidChrs = 1;
    } else if (S.hidChrs) {
        /* AI_ShowAllChrs */
        for (s32 n = get_numguards() - 1; n >= 0; n--) g_ChrSlots[n].chrflags &= ~CHRFLAG_HIDDEN;
        S.hidChrs = 0;
    }
}

static int hSkipped(void *ud) { (void)ud; return S.skipped; }

/* AI_IFCameraIsInIntro (INTRO, FADESWIRL) and AI_IFCameraIsInBondSwirl (SWIRL); MP is the co-op spawn swirl */
static int introMode(u32 m)
{
    return m == CAMERAMODE_INTRO || m == CAMERAMODE_FADESWIRL || m == CAMERAMODE_SWIRL || m == CAMERAMODE_MP;
}

static int hInIntro(void *ud) { (void)ud; return introMode(bondviewGetCameraMode()); }

/* ---- camera -------------------------------------------------------------- */

struct modeArg { s32 mode; };
static void doSetMode(void *a) { bondviewSetCameraMode(((struct modeArg *)a)->mode); }

/* the camera mode switch runs as the driver (the game's AI runs as it: coop.c coopAiTick) */
static void setCameraMode(s32 mode)
{
    struct modeArg a;
    a.mode = mode;
    missionsHostWithPlayer(driver(), doSetMode, &a);
}

/* The scene camera of the director (camera.set / move / orbit): the game's own fixed cutscene camera (AI_CameraSwitch:
 * gBondViewCutscene, a CutsceneRecord with a position, theta / verta and a pad for the camera tile) pointing at a record
 * of ours that the tick rewrites every frame. bondview2.c computes the camera from it: pos, and pos + (cos(verta) sin(theta),
 * sin(verta), -cos(verta) cos(theta)) as the look point; in co-op coopFrozenCamera mirrors the driver's camera to every
 * view. The director's yaw (0 = +z, positive = left) is theta = pi - yaw; pitch is verta. */
static CutsceneRecord s_cam;

static void raiseCameraDone(void)
{
    MissionRuntime *rt = missionsRuntime();
    if (rt) missionRtRaise(rt, "camera_done", NULL, 0);
}

/* The director stops; a travelling move ends the script's camera.wait() (camera_done) */
static void dirOff(int raise)
{
    const int wasMove = S.dir.mode == MHC_MOVE;
    S.dir.mode = MHC_OFF;
    if (wasMove && raise) raiseCameraDone();
}

/* Live position of a reference: a pad, a player, a living guard (tracked) or the pad of any other prop. */
static int resolveRef(void *ctx, const MissionCamRef *r, float out[3])
{
    (void)ctx;
    if (r->kind == MHC_T_PAD) {
        PadRecord *p = padRec(r->index);
        if (!p) return 0;
        out[0] = p->pos.f[0]; out[1] = p->pos.f[1]; out[2] = p->pos.f[2];
        return 1;
    }
    if (r->kind == MHC_T_PLAYER) {
        if (!playerOk(r->index) || !g_playerPointers[r->index]->prop) return 0;
        out[0] = g_playerPointers[r->index]->prop->pos.f[0];
        out[1] = g_playerPointers[r->index]->prop->pos.f[1];
        out[2] = g_playerPointers[r->index]->prop->pos.f[2];
        return 1;
    }
    if (r->kind == MHC_T_PROP) {
        PropDefHeaderRecord *h = (PropDefHeaderRecord *)missionsHostPropRec(r->index);
        PadRecord *p;
        if (!h) return 0;
        if (h->type == PROPDEF_GUARD) {
            ChrRecord *c = chrFindByLiteralId(((GuardRecord *)h)->chrnum);
            if (c && c->prop && c->model) {
                out[0] = c->prop->pos.f[0]; out[1] = c->prop->pos.f[1]; out[2] = c->prop->pos.f[2];
                return 1;
            }
        }
        p = padRec(propPadOf(r->index));
        if (!p) return 0;
        out[0] = p->pos.f[0]; out[1] = p->pos.f[1]; out[2] = p->pos.f[2];
        return 1;
    }
    return 0;
}

/* The pad whose tile the camera starts its room search from (the game's cutscene cameras carry the pad of the camera
 * object): the nearest pad of a setup guard, door or camera object, since the game has no pad count to scan. */
static int nearestPad(const float pos[3])
{
    static int last = 0;
    double best = 1e30;
    int found = 0;
    for (int i = 0; i < missionsHostPropCount(); i++) {
        PropDefHeaderRecord *h = (PropDefHeaderRecord *)missionsHostPropRec(i);
        PadRecord *p;
        int pad;
        double dx, dy, dz, d;
        if (!h || (h->type != PROPDEF_GUARD && h->type != PROPDEF_DOOR && h->type != PROPDEF_CAMERAPOS)) continue;
        pad = propPadOf(i);
        if (pad < 0 || (pad >= 3000 && pad < 10000)) continue;
        p = padRec(pad);
        if (!p || !p->stan) continue;
        dx = p->pos.f[0] - pos[0]; dy = p->pos.f[1] - pos[1]; dz = p->pos.f[2] - pos[2];
        d = dx * dx + dy * dy + dz * dz;
        if (d < best) { best = d; last = pad; found = 1; }
    }
    (void)found;
    return last;
}

static void applyPose(const MhcPose *p)
{
    s_cam.pos.f[0] = p->pos[0]; s_cam.pos.f[1] = p->pos[1]; s_cam.pos.f[2] = p->pos[2];
    s_cam.theta = (f32)(MHC_PI - (double)p->yaw / MHC_DEG);
    s_cam.verta = (f32)((double)p->pitch / MHC_DEG);
    /* the room hint: the point's pad, else the floor under the camera (missionsHostCinemaCameraTile); s_cam.pad must still name
     * a real pad for the game's path, so it names a nearby one then */
    S.camPad = p->pad;
    s_cam.pad = p->pad >= 0 ? p->pad : nearestPad(p->pos);
    if (p->fov > 0) {
        /* the view's field of view is the watch zoom's (bondview2.c: set_cur_player_fovy(zoominfovy) every frame) */
        for (int i = 0; i < getPlayerCount() && i < NO_PLAYER_SLOTS; i++) {
            struct player *pl = g_playerPointers[i];
            if (!pl) continue;
            if (!(S.fovSet & (1 << i))) { S.fovOld[i] = pl->zoominfovy; S.fovSet |= 1 << i; }
            pl->zoominfovy = pl->zoominfovyold = pl->zoominfovynew = p->fov;
        }
    }
}

static void restoreFov(void)
{
    for (int i = 0; i < NO_PLAYER_SLOTS; i++)
        if ((S.fovSet & (1 << i)) && g_playerPointers[i]) {
            struct player *pl = g_playerPointers[i];
            pl->zoominfovy = pl->zoominfovyold = pl->zoominfovynew = S.fovOld[i];
        }
    S.fovSet = 0;
}

/* The game's camera is ours: the director's record, own direction (not "look at Bond"), scripted mode */
static void ensureSceneCamera(void)
{
    g_CameraLookAtBondPad = NULL;
    gBondViewCutscene = &s_cam;
    dword_CODE_bss_80079A18 = CAMERAMODE_NONE;
    dword_CODE_bss_80079A1C = 0;
    S.returnPending = 0;   /* a new scene camera cancels a return to the players' views not done yet */
    if (bondviewGetCameraMode() != CAMERAMODE_POSEND) setCameraMode(CAMERAMODE_POSEND);
    S.camera = 1;
}

/* the first frame of a new director state, so no frame shows the old camera */
static void directorKick(void)
{
    MhcPose pose;
    int fin = 0;
    ensureSceneCamera();
    /* a new shot: the room the camera was drawn from is forgotten (the renderer finds it again from the new pose's
     * start tile), as the game does when its own intro changes shot; else the first frames keep the old shot's room */
    for (int i = 0; i < getPlayerCount() && i < NO_PLAYER_SLOTS; i++)
        if (g_playerPointers[i]) g_playerPointers[i]->cameratile = NULL;
    if (mhcStep(&S.dir, 0.0, NULL, resolveRef, &pose, &fin)) applyPose(&pose);
}

static int hCameraSet(void *ud, const MissionCamPoint *pt)
{
    (void)ud;
    dirOff(1);
    mhcSet(&S.dir, pt);
    directorKick();
    return 1;
}

static int hCameraMove(void *ud, const MissionCamPoint *pts, int n, int loop, int smooth)
{
    (void)ud;
    dirOff(1);
    if (!mhcMove(&S.dir, pts, n, loop, smooth)) return 0;
    directorKick();
    return 1;
}

static int hCameraMoving(void *ud) { (void)ud; return S.dir.mode == MHC_MOVE; }

static void hCameraStop(void *ud)
{
    const int wasMove = S.dir.mode == MHC_MOVE;
    (void)ud;
    if (S.dir.mode == MHC_OFF) return;
    mhcFreeze(&S.dir);   /* where it is now, no target tracked */
    if (wasMove) raiseCameraDone();
}

static int hCameraFixed(void *ud, int at_kind, int at_index, int look_at_bond)
{
    (void)ud;
    dirOff(1);
    if (at_kind == MHC_T_PROP) {
        PropDefHeaderRecord *h = (PropDefHeaderRecord *)missionsHostPropRec(at_index);
        if (h && h->type == PROPDEF_CAMERAPOS) {
            /* AI_CameraSwitch */
            g_CameraLookAtBondPad = NULL;
            gBondViewCutscene = (CutsceneRecord *)h;
            dword_CODE_bss_80079A18 = look_at_bond ? CAMERAMODE_INTRO : CAMERAMODE_NONE;
            dword_CODE_bss_80079A1C = 0;
            setCameraMode(CAMERAMODE_POSEND);
            S.camera = 1;
            return 1;
        }
        at_index = propPadOf(at_index);
        at_kind = MHC_T_PAD;
    }
    if (at_kind == MHC_T_PAD) {
        PadRecord *p = padRec(at_index);
        if (!p) { say("camera.fixed: no such pad"); return 0; }
        /* AI_CameraLookAtBondFromPad: the camera stands on the pad and looks at Bond */
        g_CameraLookAtBondPad = p;
        gBondViewCutscene = NULL;
        setCameraMode(CAMERAMODE_POSEND);
        S.camera = 1;
        return 1;
    }
    say("camera.fixed: a player is not a camera spot");
    return 0;
}

static int hCameraOrbit(void *ud, const MissionCamRef *centres, int n, double distance, double height,
                        double look_height, double start_deg, double speed_dps)
{
    const double rad = MHC_PI / 180.0;
    (void)ud;
    if (n < 1) return 0;
    dirOff(1);
    if (n == 1 && centres[0].kind == MHC_T_PAD) {
        if (!padRec(centres[0].index)) { say("camera.orbit: no such pad"); return 0; }
        /* AI_CameraOrbitPad: bondview2.c puts the camera on a circle around the pad and advances the angle per tick;
         * this keeps the pad's own tile for the camera. */
        g_CameraLookAtBondPad = NULL;
        gBondViewCutscene = NULL;
        flt_CODE_bss_80079A00 = (f32)(start_deg * rad);
        flt_CODE_bss_80079A04 = (f32)(speed_dps * rad / 60.0);
        flt_CODE_bss_80079A08 = (f32)distance;
        flt_CODE_bss_80079A0C = (f32)height;
        flt_CODE_bss_80079A10 = (f32)look_height;
        dword_CODE_bss_80079A14 = centres[0].index;
        setCameraMode(CAMERAMODE_POSEND);
        S.camera = 1;
        return 1;
    }
    /* any other centre (a guard, a player, a point, several targets' midpoint): the director's orbit */
    mhcOrbit(&S.dir, centres, n, distance, height, look_height, start_deg, speed_dps);
    directorKick();
    return 1;
}

static void hCameraReset(void *ud)
{
    (void)ud;
    dirOff(1);
    restoreFov();
    if (!S.camera && bondviewGetCameraMode() != CAMERAMODE_POSEND) return;
    S.camera = 0;
    /* Deferred to the next tick (cameraReturnTick): switching to the players' views now, then taking the camera
     * again in the same tick (finish, then begin + camera.move: two scenes in a row) leaves the game in its
     * return transition, and the director, seeing a mode that is not its own, stops after one step. */
    S.returnPending = 1;
}

/* The deferred camera.reset: the players' own views, unless a scene camera was taken since. */
static void cameraReturnTick(void)
{
    if (!S.returnPending) return;
    S.returnPending = 0;
    if (S.camera || S.dir.mode != MHC_OFF) return;
    if (bondviewGetCameraMode() != CAMERAMODE_POSEND) return;   /* a death camera or the like is not ours to end */
    g_CameraLookAtBondPad = NULL;
    gBondViewCutscene = NULL;
    /* AI_CameraReturnToBond: removes the third-person body and goes to the players' own views */
    setCameraMode(CAMERAMODE_FP_NOINPUT);
}

/* ---- screen -------------------------------------------------------------- */

struct fadeArg { int to_black; float frames; };

static void doFade(void *a)
{
    const struct fadeArg *f = (struct fadeArg *)a;
    if (f->to_black) {   /* AI_ScreenFadeToBlack */
        currentPlayerSetFadeColour(0, 0, 0, 0);
        currentPlayerSetFadeFrac(f->frames, 1);
    } else {             /* AI_ScreenFadeFromBlack */
        currentPlayerSetFadeColour(0, 0, 0, 1);
        currentPlayerSetFadeFrac(f->frames, 0);
    }
}

static void hFade(void *ud, int to_black, double seconds)
{
    struct fadeArg a;
    (void)ud;
    a.to_black = to_black;
    a.frames = (float)(seconds * 60.0);   /* the game's fade time is in 60ths of a second */
    forAll(doFade, &a);
}

static void doFading(void *a) { if (!currentPlayerIsFadeComplete()) *(int *)a = 1; }

static int hFading(void *ud)
{
    int fading = 0;
    (void)ud;
    forAll(doFading, &fading);
    return fading;
}

/* ---- credits ------------------------------------------------------------- */

/* The scripted credits (credits.show): a copy of the script's list (too big for S, which a reset clears with memset). */
static MissionCredits s_credits;

static int hCreditsRoll(void *ud)
{
    (void)ud;
    S.creditsFake = 0;
    S.crScripted = S.crActive = 0;
    /* The game draws the credits in the Cuba level only (bondview2.c: stage == LEVELID_CUBA, credits_pointer from the
     * level's text); anywhere else they would never start or finish. */
    if (bossGetStageNum() == LEVELID_CUBA && credits_pointer != NULL) {
        credits_state = TRUE;   /* AI_CreditsRoll */
        return 1;
    }
    S.creditsFake = 1;
    say("credits.roll: this level carries no credits; done() is true at once");
    return 0;
}

static int hCreditsDone(void *ud)
{
    (void)ud;
    if (S.crScripted) return !S.crActive;
    return S.creditsFake || credits_state == 2;   /* AI_IFCreditsHasCompleted */
}

static int hCreditsShow(void *ud, const MissionCredits *c)
{
    (void)ud;
    memcpy(&s_credits, c, sizeof s_credits);
    S.creditsFake = 0;
    S.crScripted = 1;
    S.crActive = 1;
    S.crT = 0.0;
    return 1;
}

static void hCreditsStop(void *ud)
{
    (void)ud;
    S.crActive = 0;
}

/* credits.rows: the Cuba credits come from the asset pack's levels/cuba/setup.json ("credits": rows of {"text1", "text2", ...},
 * exported from the ROM's setup file, so the region's own list). The game's own pointer (credits_pointer) is not used: the
 * converter widens the setup file without moving the credits offset, so on the PC it points at other bytes. */
static int hCreditsRows(void *ud, unsigned short *ids, int max)
{
    unsigned char *file = NULL;
    unsigned long len = 0;
    char *text, *p, *end;
    int n = 0;
    (void)ud;
    if (bossGetStageNum() != LEVELID_CUBA) return 0;
    if (!assetRead("levels/cuba/setup.json", &file, &len, NULL) || !file) {
        say("credits.rows: levels/cuba/setup.json is in no asset pack");
        return 0;
    }
    text = (char *)malloc((size_t)len + 1);
    if (!text) { pkgFree(file); return 0; }
    memcpy(text, file, len);
    text[len] = '\0';
    pkgFree(file);
    p = strstr(text, "\"credits\":[");
    end = p ? strchr(p, ']') : NULL;   /* the rows hold no array: the first ] ends the list */
    if (p && end) {
        p += strlen("\"credits\":[");
        while (n < max) {
            char *t1 = strstr(p, "\"text1\":");
            char *t2 = t1 ? strstr(t1, "\"text2\":") : NULL;
            if (!t1 || !t2 || t1 > end || t2 > end) break;
            ids[2 * n] = (unsigned short)strtol(t1 + strlen("\"text1\":"), NULL, 10);
            ids[2 * n + 1] = (unsigned short)strtol(t2 + strlen("\"text2\":"), NULL, 10);
            n++;
            p = t2 + 8;
        }
    }
    free(text);
    return n;
}

/* The menu's overlay (menu_game.cpp): the scripted credits on screen, and how far they got. 0 when there are none. */
int portCreditsView(const MissionCredits **credits, double *seconds)
{
    if (!S.crActive) return 0;
    *credits = &s_credits;
    *seconds = S.crT;
    return 1;
}

/* ---- bond_body ----------------------------------------------------------- */

static ChrRecord *bodyChr(int p)
{
    if (!playerOk(p) || !g_playerPointers[p]->prop) return NULL;
    return g_playerPointers[p]->prop->chr;
}

struct bodyArg {
    int pad, item, left, on, id;
    const MissionCinemaAnim *o;
    int ret;
};

/* AI_TRYTeleportingChrToPad, for the player's own chr (CHR_BOND_CINEMA) */
static void doTeleport(void *a)
{
    struct bodyArg *b = (struct bodyArg *)a;
    ChrRecord *chr = g_CurrentPlayer->prop->chr;
    PadRecord *pad = padRec(b->pad);
    f32 facing;
    coord3d pos;
    StandTile *stan;
    b->ret = 0;
    if (!chr || !pad) return;
    facing = atan2f(pad->look.x, pad->look.z);
    pos.x = pad->pos.x; pos.y = pad->pos.y; pos.z = pad->pos.z;
    stan = pad->stan;
    sub_GAME_7F03D058(chr->prop, FALSE);
    if (chrAdjustPosForSpawn(&pos, &stan, facing, TRUE)) {
        chr->prop->pos.x = pos.x;
        chr->prop->pos.y = pos.y;
        chr->prop->pos.z = pos.z;
        chr->prop->stan = stan;
        chr->chrflags |= CHRFLAG_INIT;
        setsubroty(chr->model, facing);
        setsuboffset(chr->model, &pos);
        chrDetectRooms(chr);
        if (chr->prop == g_CurrentPlayer->prop) {
            g_CurrentPlayer->field_488.collision_position.x = pos.x;
            g_CurrentPlayer->field_488.collision_position.y = pos.y;
            g_CurrentPlayer->field_488.collision_position.z = pos.z;
            g_CurrentPlayer->field_488.current_tile_ptr = stan;
        }
        b->ret = 1;
    }
    sub_GAME_7F03D058(chr->prop, TRUE);
}

static int hBodyTeleport(void *ud, int player, int pad)
{
    struct bodyArg a;
    (void)ud;
    memset(&a, 0, sizeof a);
    if (!bodyChr(player)) { say("bond_body: the player has no third-person body now (show(true) or a scene camera first)"); return 0; }
    a.pad = pad;
    missionsHostWithPlayer(player, doTeleport, &a);
    return a.ret;
}

/* The animations a script may name: the same table as guard:play_anim (missions_host.c), frame range as the AI lists. */
static const struct { const char *name; int anim, start, end; } kAnims[] = {
    { "keyboard_right_hand_tapping", ANIM_keyboard_right_hand_tapping, 0, 89 },
    { "keyboard_left_hand",          ANIM_keyboard_left_hand,          0, 79 },
    { "keyboard_right_hand2",        ANIM_keyboard_right_hand2,        0, 74 },
    { "kneel_cough",                 ANIM_coughing_kneel1,             0, -1 },
    { "kneel_cough2",                ANIM_coughing_kneel2,             0, -1 },
    { "fall_face_down",              ANIM_death_forward_face_down_soft, 0, -1 },
    { "fall_face_up",                ANIM_death_backward_fall_face_up1, 0, -1 },
};

/* AI_PlayAnimation on the player's chr: check_if_able_to_then_perform_animation(chr, id, start, end, bitfield, blend) */
static void doAnim(void *a)
{
    struct bodyArg *b = (struct bodyArg *)a;
    ChrRecord *chr = g_CurrentPlayer->prop->chr;
    const MissionCinemaAnim *o = b->o;
    int bits = 0, blend = 16;   /* the game's ANIM_DEFAULT_INTERPOLATION (bondaicommands.h) */
    b->ret = 0;
    if (!chr || !chr->model) return;
    if (o->hold || o->loop) bits |= 0x04;   /* ANIM_LOOP_HOLD_LAST_FRAME */
    if (o->reverse) bits |= 0x80 | 0x04;    /* ANIM_REVERSE_LOOPING_ANIMATION needs the loop bit */
    if (!o->move) bits |= 0x40;             /* ANIM_NO_TRANSLATION */
    if (o->blend >= 0) blend = (int)(o->blend * 60.0);
    b->ret = check_if_able_to_then_perform_animation(chr, b->id, o->from < 0 ? 0 : o->from, o->to, (u8)bits, blend) != 0;
    /* always set: the model's play speed outlives the animation (a step at speed 0.02 would otherwise slow every later
     * animation of the body to 2 %: measured on Dam's body, animation 99 crawled after the slow rest pose). 1 = the
     * game's normal 0.5. */
    if (b->ret)
        modelSetAnimPlaySpeed(chr->model, (o->reverse ? -0.5f : 0.5f) * (o->speed > 0 ? (f32)o->speed : 1.0f), 0.0f);
}

extern s32 animation_table_ptrs1[];

static int hBodyAnim(void *ud, int player, const char *name, int id, const MissionCinemaAnim *opts)
{
    struct bodyArg a;
    MissionCinemaAnim o = *opts;
    (void)ud;
    memset(&a, 0, sizeof a);
    if (name) {
        id = -1;
        for (size_t k = 0; k < sizeof kAnims / sizeof kAnims[0]; k++)
            if (!strcmp(kAnims[k].name, name)) {
                id = kAnims[k].anim;
                if (o.from < 0) o.from = kAnims[k].start;
                if (o.to < 0) o.to = kAnims[k].end;
                break;
            }
        if (id < 0) { say("bond_body:anim: unknown animation name"); return 0; }
    }
    if (id < 0 || id >= ANIM_MAX || animation_table_ptrs1[id] == 1) { say("bond_body:anim: no such animation id"); return 0; }
    if (!bodyChr(player)) { say("bond_body: the player has no third-person body now (show(true) or a scene camera first)"); return 0; }
    a.id = id;
    a.o = &o;
    missionsHostWithPlayer(player, doAnim, &a);
    return a.ret;
}

/* AI_IFPlayingAnimation's test (actiontype == ACT_ANIM), and chrlvTickAnim's end test: a held animation sits on its last
 * frame with the action still ACT_ANIM */
static int hBodyAnimDone(void *ud, int player)
{
    ChrRecord *chr = bodyChr(player);
    (void)ud;
    if (!chr || !chr->model) return 1;
    /* walk_to: not done while the chr walks its route (ACT_GOPOS) */
    if (chr->actiontype == ACT_GOPOS) return 0;
    /* aim_at / fire_at: an aim holds (done at once); a shot is done when its animation ends (or after 3 s) */
    if (chr->actiontype == ACT_ATTACK) {
        if (S.aimOnly[player]) return 1;
        if ((g_GlobalTimer - S.shotStart[player]) > 180) return 1;
        return modelGetAnimEndFrame(chr->model) <= modelGetAnimFrame(chr->model);
    }
    if (chr->actiontype != ACT_ANIM) return 1;
    return modelGetAnimEndFrame(chr->model) <= modelGetAnimFrame(chr->model);
}

/* ---- #165 second pass: aim / fire / walk / drop / gravity / flags / invincible on the body ---------------------- */

struct aimArg { ChrRecord *c; s32 type, id; int kneel, res; };

static void doAim(void *a)
{
    struct aimArg *m = (struct aimArg *)a;
    m->res = (m->kneel ? actor_kneel_aim_at_actor(m->c, m->type, m->id) : actor_aim_at_actor(m->c, m->type, m->id)) != 0;
}

/* AI_TRYFireOrAimAtTarget / ...Kneel on the player's chr (TARGET_AIM_ONLY: only aims). TARGET_BOND is the CURRENT player,
 * so the call runs as the target player. */
static int hBodyAim(void *ud, int player, int tkind, int target, int kneel, int aim_only)
{
    ChrRecord *chr = bodyChr(player);
    struct aimArg a;
    (void)ud;
    if (!chr || !chr->model) { say("bond_body: the player has no third-person body now (show(true) or a scene camera first)"); return 0; }
    memset(&a, 0, sizeof a);
    a.c = chr; a.kneel = kneel;
    a.type = aim_only ? TARGET_AIM_ONLY : 0;
    if (tkind == MHC_T_PROP) {
        ChrRecord *t = chrFindByLiteralId(target);
        if (!t || !t->prop) return 0;
        a.type |= TARGET_CHR; a.id = t->chrnum;
        doAim(&a);
    } else if (tkind == MHC_T_PAD) {
        a.type |= TARGET_PAD; a.id = target;
        doAim(&a);
    } else {
        const int tp = playerOk(target) ? target : driver();
        a.type |= TARGET_BOND;
        missionsHostWithPlayer(tp, doAim, &a);
    }
    if (a.res) { S.aimOnly[player] = aim_only != 0; S.shotStart[player] = g_GlobalTimer; }
    return a.res;
}

struct walkArg { ChrRecord *c; int pad, speed, res; };

/* AI_WalkToPad / RunToPad / SprintToPad on the player's chr */
static void doWalk(void *a)
{
    struct walkArg *w = (struct walkArg *)a;
    w->res = chrGoToPad(w->c, w->pad, w->speed >= 2 ? SPEED_SPRINT : w->speed == 1 ? SPEED_RUN : SPEED_WALK) != 0;
}

static int hBodyWalk(void *ud, int player, int pad, int speed)
{
    ChrRecord *chr = bodyChr(player);
    struct walkArg a;
    (void)ud;
    if (!chr || !chr->model) { say("bond_body: the player has no third-person body now (show(true) or a scene camera first)"); return 0; }
    a.c = chr; a.pad = pad; a.speed = speed; a.res = 0;
    missionsHostWithPlayer(player, doWalk, &a);
    S.aimOnly[player] = 0;
    return a.res;
}

/* AI_ChrDropAllHeldItems for one hand (the guard domain's drop_items, one hand) */
static void hBodyDropItem(void *ud, int player, int left_hand)
{
    ChrRecord *chr = bodyChr(player);
    const int hand = left_hand ? GUNLEFT : GUNRIGHT;
    (void)ud;
    if (!chr) { say("bond_body:drop_item: the player has no third-person body now"); return; }
    if (chr->weapons_held[hand]) {
        propobjSetDropped(chr->weapons_held[hand], 1);
        chr->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
    }
}

/* AI_SetMychrflags: CHRFLAG_LOCK_Y_POS (no gravity, the Dam / Cradle jump) */
static void hBodySetGravity(void *ud, int player, int on)
{
    ChrRecord *chr = bodyChr(player);
    (void)ud;
    if (!chr) { say("bond_body:set_gravity: the player has no third-person body now"); return; }
    if (on) chr->chrflags &= ~CHRFLAG_LOCK_Y_POS;
    else chr->chrflags |= CHRFLAG_LOCK_Y_POS;
}

static void applyChrFlags(ChrRecord *chr, int mask, int values)
{
    static const struct { int bit; u32 flag; } map[] = {
        { MHC_BF_HITBOX_CULL,   CHRFLAG_CULL_USING_HITBOX },
        { MHC_BF_HEIGHT_LOCK,   CHRFLAG_LOCK_Y_POS },
        { MHC_BF_KNOCKABLE,     CHRFLAG_IMPACT_ALWAYS },
        { MHC_BF_SORTED,        CHRFLAG_00020000 },
        { MHC_BF_NO_COLLISION,  CHRFLAG_00010000 },
    };
    for (size_t k = 0; k < sizeof map / sizeof map[0]; k++) {
        if (!(mask & map[k].bit)) continue;
        if (values & map[k].bit) chr->chrflags |= map[k].flag;
        else chr->chrflags &= ~map[k].flag;
    }
}

static void hBodySetFlags(void *ud, int player, int mask, int values)
{
    ChrRecord *chr = bodyChr(player);
    (void)ud;
    if (!chr) { say("bond_body:set_body_flags: the player has no third-person body now"); return; }
    applyChrFlags(chr, mask, values);
}

/* set_body_flags on a guard (AI_SetChrchrflags on that chr) */
static void hChrSetFlags(void *ud, int chrnum, int mask, int values)
{
    ChrRecord *chr = chrnum >= 0 ? chrFindByLiteralId(chrnum) : NULL;
    (void)ud;
    if (!chr || !chr->prop) { say("set_body_flags: no such guard in the level"); return; }
    applyChrFlags(chr, mask, values);
}

/* the player invincible: the game's own per-player invincibility (as the world's player set_invincible) and the body chr's flag */
static void hBodySetInvincible(void *ud, int player, int on)
{
    (void)ud;
    if (!playerOk(player)) return;
    g_playerPointers[player]->cheatBondInvincible = on ? 1 : 0;
    {
        ChrRecord *chr = bodyChr(player);
        if (chr) {
            if (on) chr->chrflags |= CHRFLAG_INVINCIBLE;
            else chr->chrflags &= ~CHRFLAG_INVINCIBLE;
        }
    }
}

/* AI_ShowAllChrs for one chr, while hide_all_chrs(true) is in force */
static int hShowHiddenChr(void *ud, int chr)
{
    ChrRecord *c;
    (void)ud;
    if (!S.hidChrs || chr < 0) return 0;
    c = chrFindByLiteralId(chr);
    if (!c || !c->prop) return 0;
    c->chrflags &= ~CHRFLAG_HIDDEN;
    return 1;
}

/* AI_BondEquipItemCinema: currentPlayerUnEquipWeaponWrapper(hand, item) (the game's name for the cinema equip) */
static void doHold(void *a)
{
    struct bodyArg *b = (struct bodyArg *)a;
    if (b->left) {
        currentPlayerUnEquipWeaponWrapper(GUNLEFT, (ITEM_IDS)b->item);
    } else {
        currentPlayerUnEquipWeaponWrapper(GUNRIGHT, (ITEM_IDS)b->item);
        currentPlayerUnEquipWeaponWrapper(GUNLEFT, 0);
    }
    b->ret = 1;
}

static int hBodyHoldItem(void *ud, int player, int item, int left_hand)
{
    struct bodyArg a;
    (void)ud;
    memset(&a, 0, sizeof a);
    if (!playerOk(player)) return 0;
    a.item = item;
    a.left = left_hand;
    /* no body yet: the body is BUILT holding the item (the original builds the intro body with the start weapon in its right
     * hand); the player's own hands are not touched */
    if (!left_hand) S.bodyItem[player] = item;
    if (!left_hand && !bodyChr(player)) return 1;
    missionsHostWithPlayer(player, doHold, &a);
    return a.ret;
}

/* show: the body is made when there is none (solo_char_load, as the scene cameras do), and faded in or out at once
 * (currentPlayerStartChrFade is what the intro swirl uses) */
static void doShow(void *a)
{
    struct bodyArg *b = (struct bodyArg *)a;
    if (b->on && g_CurrentPlayer->prop->chr != NULL && S.bodyItem[get_cur_playernum()] > 1) {
        /* a body exists (a scene made it at the level start) but the script asked for one holding an item: build it again,
         * as the original builds the intro body when the swirl opens (at the player's place and facing, holding the item) */
        bondviewRemovePlayerBody();
    }
    if (b->on && g_CurrentPlayer->prop->chr == NULL) {
        const int p = get_cur_playernum();
        if (S.bodyItem[p] > 1) {
            /* the game gives the body the item only when it is built in the swirl camera mode (the start weapon) */
            const ITEM_IDS savedItem = starting_weapon[GUNRIGHT];
            const enum CAMERAMODE savedMode = g_CameraMode;
            starting_weapon[GUNRIGHT] = (ITEM_IDS)S.bodyItem[p];
            g_CameraMode = CAMERAMODE_SWIRL;
            solo_char_load();
            g_CameraMode = savedMode;
            starting_weapon[GUNRIGHT] = savedItem;
        } else {
            solo_char_load();
        }
        S.bodyItem[p] = 0;
    }
    currentPlayerStartChrFade(0.0f, b->on ? 1.0f : 0.0f);
}

static void hBodyShow(void *ud, int player, int on)
{
    struct bodyArg a;
    (void)ud;
    memset(&a, 0, sizeof a);
    if (!playerOk(player)) return;
    a.on = on;
    missionsHostWithPlayer(player, doShow, &a);
}

/* remove: the original's bondviewRemovePlayerBody (solo): the body goes, and three ticks later the player's weapon models are
 * loaded back into his hands (the body's model had taken the right hand's buffer and locked both hands) */
static void doBodyRemove(void *a)
{
    (void)a;
    if (g_CurrentPlayer->prop->chr != NULL) {
        bondviewRemovePlayerBody();
        S.disarm[get_cur_playernum()] = 3;
    }
}

/* what the game's move code does three ticks after a removal (it does not run while a scene or the frozen path is on): the hands
 * are unlocked and the weapon models of both hands are loaded back */
static void doDisarm(void *a)
{
    (void)a;
    if (g_bondviewForceDisarm <= 0) return;
    g_bondviewForceDisarm = 0;
    g_CurrentPlayer->lock_hand_model[GUNLEFT] = 0;
    g_CurrentPlayer->lock_hand_model[GUNRIGHT] = 0;
    currentPlayerUnEquipWeaponWrapper(GUNLEFT, getCurrentPlayerWeaponId(GUNLEFT));
    currentPlayerUnEquipWeaponWrapper(GUNRIGHT, getCurrentPlayerWeaponId(GUNRIGHT));
}

static void hBodyRemove(void *ud, int player)
{
    (void)ud;
    if (!playerOk(player)) return;
    missionsHostWithPlayer(player, doBodyRemove, NULL);
}

/* the original's body fade: from the body's current opacity to `to` over `secs` (0: at once); the body stays where it
 * is, drawn at that opacity (0: not drawn) */
struct bodyFadeArg { float to, secs; };
static void doBodyFade(void *a)
{
    const struct bodyFadeArg *f = (const struct bodyFadeArg *)a;
    if (g_CurrentPlayer->prop->chr == NULL) { say("bond_body:fade: the player has no third-person body now (show it first)"); return; }
    currentPlayerStartChrFade(f->secs * 60.0f, f->to);
}

static void hBodyFade(void *ud, int player, float to, float seconds)
{
    struct bodyFadeArg f;
    (void)ud;
    if (!playerOk(player)) return;
    f.to = to; f.secs = seconds > 0 ? seconds : 0;
    missionsHostWithPlayer(player, doBodyFade, &f);
}

static void hBodySetShadow(void *ud, int player, int on)
{
    ChrRecord *chr = bodyChr(player);
    (void)ud;
    if (!chr) { say("bond_body:set_shadow: the player has no third-person body now"); return; }
    if (on) chr->chrflags &= ~CHRFLAG_NO_SHADOW;
    else chr->chrflags |= CHRFLAG_NO_SHADOW;
}

/* ---- the player methods -------------------------------------------------- */

/* set_frozen: the game has one freeze for everyone (is_timer_active, the frozen path of every player's tick), so a player's
 * freeze is every player's. It is independent of cinema.begin (which freezes too). */
static void hSetFrozen(void *ud, int player, int on)
{
    (void)ud; (void)player;
    if (on) {
        if (!S.frozen) S.frozenSaved = is_timer_active;
        S.frozen = 1;
        is_timer_active = FALSE;
    } else if (S.frozen) {
        S.frozen = 0;
        is_timer_active = S.frozenSaved;
    }
}

struct weaponArg { int on; int player; };

/* AI_BondHideWeapons: remove_item_in_hand for both hands; on = false puts back what was in them */
static void doHideWeapon(void *a)
{
    struct weaponArg *w = (struct weaponArg *)a;
    const int p = w->player;
    if (w->on) {
        if (!S.weaponHidden[p]) {
            S.heldRight[p] = (int)get_item_in_hand_or_watch_menu(GUNRIGHT);
            S.heldLeft[p] = (int)get_item_in_hand_or_watch_menu(GUNLEFT);
            S.weaponHidden[p] = 1;
        }
        remove_item_in_hand(GUNRIGHT);
        remove_item_in_hand(GUNLEFT);
    } else if (S.weaponHidden[p]) {
        S.weaponHidden[p] = 0;
        currentPlayerEquipWeaponWrapper(GUNRIGHT, S.heldRight[p]);
        currentPlayerEquipWeaponWrapper(GUNLEFT, S.heldLeft[p]);
    }
}

static void hideWeaponFor(int player, int on)
{
    struct weaponArg w;
    if (!playerOk(player)) return;
    w.on = on;
    w.player = player;
    missionsHostWithPlayer(player, doHideWeapon, &w);
}

static void hHideWeapon(void *ud, int player, int on)
{
    (void)ud;
    hideWeaponFor(player, on);
}

/* in_cutscene: a scripted camera is up (the game's own "scripted camera" test), or a scene of ours is on */
static int hInCutscene(void *ud, int player)
{
    (void)ud;
    if (!playerOk(player)) return 0;
    return S.active || gameScriptedCameraActive();
}

/* ---- the host struct, tick, reset ---------------------------------------- */

const MissionHostCinema *missionsHostCinema(void)
{
    static MissionHostCinema h;
    memset(&h, 0, sizeof h);
    h.begin = hBegin; h.finish = hFinish; h.hide_all_chrs = hHideAllChrs; h.skipped = hSkipped; h.in_intro = hInIntro;
    h.camera_fixed = hCameraFixed; h.camera_orbit = hCameraOrbit; h.camera_reset = hCameraReset;
    h.camera_set = hCameraSet; h.camera_move = hCameraMove; h.camera_moving = hCameraMoving; h.camera_stop = hCameraStop;
    h.fade = hFade; h.fading = hFading;
    h.credits_roll = hCreditsRoll; h.credits_done = hCreditsDone;
    h.credits_show = hCreditsShow; h.credits_stop = hCreditsStop; h.credits_rows = hCreditsRows;
    h.body_teleport = hBodyTeleport; h.body_anim = hBodyAnim; h.body_anim_done = hBodyAnimDone;
    h.body_hold_item = hBodyHoldItem; h.body_show = hBodyShow; h.body_fade = hBodyFade; h.body_remove = hBodyRemove; h.body_set_shadow = hBodySetShadow;
    h.body_aim = hBodyAim; h.body_walk = hBodyWalk; h.body_drop_item = hBodyDropItem; h.body_set_gravity = hBodySetGravity;
    h.body_set_flags = hBodySetFlags; h.chr_set_flags = hChrSetFlags; h.body_set_invincible = hBodySetInvincible; h.show_hidden_chr = hShowHiddenChr;
    h.set_frozen = hSetFrozen; h.hide_weapon = hHideWeapon; h.in_cutscene = hInCutscene;
    return &h;
}

/* GE_CAMLOG=1: the game's actual camera of player 0 (what the view is built from: pos, the look point pos2, the theta /
 * verta they give in the director's convention, the eye point and the facing the swirl is built on), every 60 game ticks in the
 * fixed shot, every 12 after it, at most 900 lines (t = g_GlobalTimer). Quiet unless set. Every game camera passes through player->pos / pos2
 * (bondviewSetCurrentPlayerPosition), the original intro and a scripted one alike. */
extern double atan2(double y, double x);   /* the game's math.h shadows the C one */
static void camLog(u32 mode)
{
    static int on = -1, n = 0, last = -1;
    struct player *pl = g_playerPointers[0];
    const int period = (mode == CAMERAMODE_INTRO || mode == CAMERAMODE_FADESWIRL) ? 60 : 12;   /* game ticks */
    double dx, dy, dz, flat;
    if (on < 0) { const char *e = getenv("GE_CAMLOG"); on = e != NULL && e[0] != 0 && e[0] != '0'; }
    if (!on || n >= 900 || !pl || (last >= 0 && g_GlobalTimer / period == last / period && g_GlobalTimer >= last)) return;
    last = g_GlobalTimer;
    dx = pl->pos2.f[0] - pl->pos.f[0]; dy = pl->pos2.f[1] - pl->pos.f[1]; dz = pl->pos2.f[2] - pl->pos.f[2];
    flat = sqrt(dx * dx + dz * dz);
    n++;
    fprintf(stderr, "CAMLOG t=%d mode=%u pos=(%.2f %.2f %.2f) look=(%.2f %.2f %.2f) api_yaw=%.2f pitch=%.2f "
            "eye=(%.2f %.2f %.2f) tt=(%.4f %.4f)\n",
            g_GlobalTimer, (unsigned)mode, pl->pos.f[0], pl->pos.f[1], pl->pos.f[2],
            pl->pos2.f[0], pl->pos2.f[1], pl->pos2.f[2],
            atan2(dx, dz) * 180.0 / MHC_PI, atan2(dy, flat) * 180.0 / MHC_PI,
            pl->field_3C4, pl->field_3C8, pl->field_3CC,
            pl->field_488.theta_transform.f[0], pl->field_488.theta_transform.f[2]);
}

/* Per tick, after the scripts ran (missions_host.c). */
void missionsHostCinemaTick(void)
{
    const u32 m = bondviewGetCameraMode();
    int p;
    camLog(m);
    cameraReturnTick();
    for (p = 0; p < NO_PLAYER_SLOTS; p++)
        if (S.disarm[p] > 0 && --S.disarm[p] == 0 && playerOk(p)) missionsHostWithPlayer(p, doDisarm, NULL);

    /* "intro_done": the start cameras are over and a player has control (or a script already took the camera) */
    if (!S.introDone && (m == CAMERAMODE_FP || m == CAMERAMODE_FP_NOINPUT || m == CAMERAMODE_POSEND ||
                         m == CAMERAMODE_DEATH_CAM_SP || m == CAMERAMODE_DEATH_CAM_MP)) {
        MissionRuntime *rt = missionsRuntime();
        S.introDone = 1;
        if (rt) missionRtRaise(rt, "intro_done", NULL, 0);
    }

    /* "intro_swirl": the intro's swirl around the player starts (the camera mode enters the swirl), once */
    if (!S.swirlRaised && m == CAMERAMODE_SWIRL) {
        MissionRuntime *rt = missionsRuntime();
        S.swirlRaised = 1;
        if (rt) missionRtRaise(rt, "intro_swirl", NULL, 0);
    }

    /* cinema.skipped: any player pressed a button since begin (the game's own skip buttons, bondview2.c) */
    if (S.active && !S.skipped) {
        for (p = 0; p < getPlayerCount() && p < NO_PLAYER_SLOTS; p++)
            if (joyGetButtonsPressedThisFrame((s8)p, A_BUTTON | B_BUTTON | Z_TRIG | START_BUTTON) != 0) S.skipped = 1;
    }

    /* the scripted credits scroll with the game's clock (it stands still when the game is paused) */
    if (S.crActive) {
        S.crT += (double)g_GlobalTimerDelta / 60.0;
        if (mhcrFinished(&s_credits, S.crT)) S.crActive = 0;
    }

    /* the scene camera (camera.set / move / orbit): one step per game frame (g_GlobalTimerDelta is in 60ths of a second) */
    if (S.dir.mode != MHC_OFF) {
        if (m != CAMERAMODE_POSEND || gBondViewCutscene != &s_cam) {
            if (getenv("GE_CAMLOG")) fprintf(stderr, "CAMLOG director off: mode=%u ours=%d\n", m, gBondViewCutscene == &s_cam);
            dirOff(1);   /* someone else took the camera (a death camera, the game's own cutscene) */
        } else {
            MhcPose pose;
            int fin = 0;
            if (mhcStep(&S.dir, (double)g_GlobalTimerDelta / 60.0, NULL, resolveRef, &pose, &fin)) applyPose(&pose);
            if (fin && getenv("GE_CAMLOG")) fprintf(stderr, "CAMLOG director finished\n");
            if (fin) raiseCameraDone();
        }
    }
}

/* A level starts or the runtime stops: drop this domain's state (missionsHostDomainsReset). The game itself resets its
 * own globals at a level start (bondview_r.c); when the runtime stops inside a level the two plain flags a scene took
 * (invulnerable, frozen) are given back so a scene never outlives its mission. */
void missionsHostCinemaReset(void)
{
    if (S.active) {
        is_timer_active = S.savedTimer;
        g_PlayerInvincible = S.savedInvincible;
    }
    if (S.frozen) is_timer_active = S.frozenSaved;
    /* (cheatBondInvincible and the body's chr flags live in per-level structs: they end with the level; the player
     * pointers may already be stale when a level start calls this, so nothing is written through them here) */
    memset(&S, 0, sizeof S);
    S.savedTimer = 1;
    S.camPad = -1;
}

/* missions_priv.h: a cinema.begin scene is running (the guard domain makes guards blind and deaf meanwhile). */
int missionsHostSceneActive(void)
{
    return S.active;
}

/* coop.c (the frozen-camera hook): the room the director's camera is drawn from. A point with a pad (the room hint,
 * mission.d.luau CameraPoint.pad) gives it through the camera record's pad, the game's own path. A point without one:
 * the floor under the camera; with no floor under it (over the void, outside the level), the first floor along its
 * look direction, and the renderer's walk from that floor back to the camera stops at the level's edge facing it.
 * Cached per pose (the look search scans every room's tiles at each step). Returns 0 when nothing changed (not the
 * director's camera, a pad given, or no floor found). */
#define CAM_ROOM_STEP  150.0f    /* look search: step and reach, world units */
#define CAM_ROOM_REACH 30000.0f
int missionsHostCinemaCameraTile(const struct coord3d *pos, const struct coord3d *pos2, StandTile **stan,
                                 struct coord3d *walk)
{
    static struct coord3d lastPos, lastLook, lastWalk;
    static StandTile *lastTile = NULL;
    struct coord3d p;
    StandTile *tile;
    float dx, dy, dz, len, t;

    if (gBondViewCutscene != &s_cam || bondviewGetCameraMode() != CAMERAMODE_POSEND || S.camPad >= 0) return 0;
    if (lastTile && memcmp(&lastPos, pos, sizeof lastPos) == 0 && memcmp(&lastLook, pos2, sizeof lastLook) == 0) {
        tile = lastTile;
    } else {
        p = *pos;
        tile = stanFindTileBelowPos(&p, NULL, NULL);
        if (!tile) {
            dx = pos2->f[0] - pos->f[0]; dy = pos2->f[1] - pos->f[1]; dz = pos2->f[2] - pos->f[2];
            len = sqrtf(dx * dx + dy * dy + dz * dz);
            if (len > 0) {
                dx /= len; dy /= len; dz /= len;
                for (t = CAM_ROOM_STEP; t <= CAM_ROOM_REACH && !tile; t += CAM_ROOM_STEP) {
                    p.f[0] = pos->f[0] + dx * t; p.f[1] = pos->f[1] + dy * t; p.f[2] = pos->f[2] + dz * t;
                    tile = stanFindTileBelowPos(&p, NULL, NULL);
                }
            }
        }
        lastPos = *pos; lastLook = *pos2; lastTile = tile; lastWalk = p;
    }
    if (!tile) return 0;   /* no floor under it nor ahead: the nearby pad applyPose named stays */
    *stan = tile;
    *walk = lastWalk;
    return 1;
}
