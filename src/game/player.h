#ifndef _PLAYER_H_
#define _PLAYER_H_
#include <ultra64.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include "bondview.h"

struct player_data {
    /* 0x0 */
    s32 shot_count[7];

    /* 0x1c */
    int kill_count;

    /* 0x20 */
    int killed_gg_owner_count;

    /* 0x24 */
    int kill_counts[MAX_PLAYER_COUNT];

    /* 0x34 */
    int time_other_players_on_screen;

    /* 0x38 */
    float distance_traveled;

    /* 0x3c */
    int damage_to_backside;

    /* 0x40 */
    float body_armor_pickups;

    /* 0x44 */
    int min_time_between_kills;

    /* 0x48 */
    int max_time_between_kills;

    /* 0x4c */
    int longest_inning;

    /* 0x50 */
    s32 shortest_inning;

    /* 0x54 */
    int most_killed_one_life;

    /* 0x58 */
    int most_killed_one_time;

    /* 0x5c */
    float handicap;

    /* 0x60 */
    int flag_counter;

    /* 0x64 */
    float player_perspective_height;

    /* 0x68 */
    u8 order_out_in_yolt;

    /* 0x69 */
    u8 have_token_or_goldengun;

    /* 0x6a */
    u8 autoaim;

    /* 0x6b */
    u8 sight;

    /* 0x6c */
    int killed_civilians;
};

extern struct player *g_playerPointers[4];

// extern struct player_data player1_player_data;
// extern struct player_data player2_player_data;
// extern struct player_data player3_player_data;
// extern struct player_data player4_player_data;

extern struct player_data g_playerPlayerData[4];

extern struct player *g_CurrentPlayer;
extern struct player_data *g_playerPerm;
extern s32 player_num;
extern s32 random_byte;
extern PLAYER_ID array_PLAYER_IDs[4];


void reset_play_data_ptrs(void);
void init_player_data_ptrs_construct_viewports(int playercount);
void shuffle_player_ids(void);
s32 get_nth_player_from_shuffled(PLAYER_ID id);
void set_cur_player(s32 playernum);
s32 getPlayerCount(void);
void set_cur_player_screen_size(u32 width, u32 height);
void set_cur_player_viewport_size(u32 ulx, u32 uly);


s32 get_cur_playernum(void);
void set_cur_player_fovy(f32 fovy);
s32 getPlayerPointerIndex( PropRecord* prop);
void set_cur_player_aspect(f32 aspect);
void initBONDdataforPlayer(s32 player_num);
s32 get_player_position_in_shuffled(s32 current_player_num);
void sub_GAME_7F09B368(enum GUNHAND hand);
void sub_GAME_7F09B398(enum GUNHAND hand);

#ifdef PORT
/* Fork-local co-op campaign for 2 to 4 players (port/src/coop.c, docs/dev/findings.md D960).
 * Each macro is the original N64 expression when built without PORT. */
struct ChrRecord;
s32 coopIsMission(void);
s32 coopRuleMulti(void);
s32 coopDeathFinal(void);
s32 coopPvpAllowed(void);
s32 coopSimOwner(void);
s32 coopBodySlots(void);
void coopVersusDeathState(void);
f32 coopDownFade(f32 frac);
f32 coopDownChrFade(f32 frac);
s32 coopRespawnButton(s32 pressed);
void coopSetBondKia(void);
s32 coopMissionEndOk(void);
void coopAiTick(struct ChrRecord *chr, s32 perView);
void coopFrameTick(void);
s32 coopStageNumPlayers(s32 stage, s32 selected);
void coopSpawnOffset(f32 *x, f32 *z, void *pstan, f32 lookx, f32 lookz);
s32 coopWatchStart(void);
s32 coopWatchPad(void);
s32 coopPickupTaken(struct PropRecord *prop);
s32 coopPickupOp(struct PropRecord *prop, s32 op);
s32 coopHasKeyFlags(u32 flags);
void coopPropFreed(struct PropRecord *prop);
s32 coopAnyHasPropInInv(struct PropRecord *prop);
s32 coopAnyCopiedGoldenEye(void);
s32 coopChrIsPlayer(struct ChrRecord *chr);
s32 coopAutoAimAtPlayer(void);
void coopNpcHurt(struct ChrRecord *chr, f32 damage, f32 subroty, s32 item);
s32 coopBondBody(void);
s32 coopProjSimOwner(s32 ownedByCurrent, struct PropRecord *ownerprop, s32 viewOwner);
s32 coopSplitView(void);
void coopViewOrder(void);
s32 coopWatchLock(s32 lock);
void coopFrozenCamera(u16 buttons, u16 oldbuttons, struct coord3d *pos, struct coord3d *pos2, struct coord3d *offset, struct StandTile **stan, struct coord3d *arg6);
#define GE_RULE_SOLO()           coopIsMission()
#define GE_RULE_MULTI()          coopRuleMulti()
#define GE_DEATH_SOLO()          (getPlayerCount() == 1 || coopDeathFinal())
#define GE_PVP_ALLOWED()         coopPvpAllowed()
#define GE_AUTOAIM_AT_PLAYER()   coopAutoAimAtPlayer()
#define GE_VERSUS_DEATH_STATE()  coopVersusDeathState()
#define GE_DOWN_FADE(f)          coopDownFade(f)
#define GE_DOWN_CHRFADE(f)       coopDownChrFade(f)
#define GE_RESPAWN_BUTTON(x)     coopRespawnButton(x)
/* co-op: a death is KIA only when final; a player still down at the exit fails the mission (revive first) */
#define GE_SET_BOND_KIA()        coopSetBondKia()
#define GE_MISSION_END_OK()      coopMissionEndOk()
#define GE_SIM_TICKS(x)          (coopSimOwner() ? (x) : 0)
#define GE_SIM_OWNER()           coopSimOwner()
#define GE_COOP_BODY_SLOTS()     coopBodySlots()
#define GE_AI_TICK_VIEW(chr)     coopAiTick((chr), 1)
#define GE_AI_TICK_FRAME(chr)    coopAiTick((chr), 0)
#define GE_COOP_FRAME_TICK()     coopFrameTick()
#define GE_COOP_SPAWN_OFFSET(x, z, st, lx, lz) coopSpawnOffset((x), (z), (st), (lx), (lz))
#define GE_WATCH_START()         (getPlayerCount() == 1 || coopWatchStart())
#define GE_WATCH_LOCK(l)         coopWatchLock(l)
#define GE_WATCH_PAD()           coopWatchPad()
#define GE_PICKUP_TAKEN(prop)    coopPickupTaken(prop)
#define GE_PICKUP_VISIBLE(prop)  (!coopPickupTaken(prop))
#define GE_PICKUP_OP(prop, op)   coopPickupOp((prop), (op))
#define GE_HAS_KEY_FLAGS(f)      coopHasKeyFlags(f)
#define GE_NPC_HURT(chr, dmg, rot, item) coopNpcHurt((chr), (dmg), (rot), (item))
#define GE_CHR_PICKABLE(chr)     ((chr)->model && !coopChrIsPlayer(chr))
/* AI hide_all_chrs (before the exit camera): a co-op player's body is a chr too, and hiding it hid Bond in the outro */
#define GE_CHR_HIDEABLE(chr)     ((chr)->model != NULL && !coopChrIsPlayer(chr))
#define GE_HAS_PROP_IN_INV(prop) coopAnyHasPropInInv(prop)
#define GE_KEY_COPIED()          coopAnyCopiedGoldenEye()
#define GE_PROP_FREED(prop)      coopPropFreed(prop)
/* stage scripts ask "fewer than N players?" to tell a mission from a versus match: a co-op mission is a mission */
#define GE_AI_PLAYERS()          (coopIsMission() ? 1 : getPlayerCount())
#define GE_BOND_BODY()           (getPlayerCount() == 1 || coopBondBody())
#define GE_PROJ_SIM_OWNER(own, op, view) coopProjSimOwner((own), (op), (view))
#define GE_SPLIT_GE2()           (getPlayerCount() >= 2 && coopSplitView())
#define GE_SPLIT_EQ2()           (getPlayerCount() == 2 && coopSplitView())
#define GE_SPLIT_GE3()           (getPlayerCount() >= 3 && coopSplitView())
extern int portSplitVertical;   /* Game.SplitScreen: 2 players side by side instead of one above the other */
#define GE_SPLIT_VERT()          (GE_SPLIT_EQ2() && portSplitVertical)
#define GE_WATCH_NO_OPTIONS()    (getPlayerCount() == 1 || coopIsMission())   /* the menu replaces the watch's settings pages */
#define GE_COOP_VIEW_ORDER()     coopViewOrder()
#define GE_FROZEN_CAMERA         coopFrozenCamera
/* Co-op and Multiplayer stay open with one controller: the menus count 4 available, and the controller screen
 * (port/src/assign.c) is asked for when a mode needs more than are connected. GE_NEED_PADS(n) is 1 when n are there. */
extern int portNeedPads(int n);
#define GE_MP_AVAIL()            4
#define GE_NEED_PADS(n)          portNeedPads(n)
extern int coopMenuIntent(void);
extern int coopMenuPlayers(void);
extern int portAimIsMouse(void);
#define GE_AUTOAIM_ON()          (cur_player_get_autoaim() && !portAimIsMouse())   /* no auto-aim while the mouse aims */
#define GE_COOP_WAIT()           (coopMenuIntent() && !portNeedPads(coopMenuPlayers()))   /* co-op chosen, fewer pads than players */
/* Game event hooks (port/include/portevents.h, port/src/fxhooks.c; RULE-2-SIGNOFF 2026-11-23,
 * docs/dev/findings/2026-11-23-rule-2-signoff-fx-markers.md): each raises an event on the port's bus.
 * GE_FX_RENDER_REPLACED(kind, &gdl) is nonzero when a port subscriber drew that effect kind itself;
 * the game then skips its own draw (rendering only). Empty / 0 without PORT. */
struct coord3d;
void portEvShot(void *beam, s32 item, struct coord3d *from, struct coord3d *to);
void portEvPuff(struct coord3d *pos, s32 type, f32 size, s16 room);
void portEvImpact(struct coord3d *pos, struct coord3d *normal, s16 type, s16 room, void *prop);
void portEvHitChr(void *prop, s32 hitpart, struct coord3d *dirView, struct coord3d *pos);
s32 portFxRenderReplaced(s32 kind, Gfx **gdl);
#define GE_EV_SHOT(beam, item, from, to)              portEvShot((beam), (item), (from), (to))
#define GE_EV_PUFF(pos, type, size, room)             portEvPuff((pos), (type), (size), (room))
#define GE_EV_IMPACT(pos, normal, type, room, prop)   portEvImpact((pos), (normal), (type), (room), (prop))
#define GE_EV_HIT_CHR(prop, part, dirview, pos)       portEvHitChr((prop), (part), (dirview), (pos))
#define GE_FX_RENDER_REPLACED(kind, gdlp)             portFxRenderReplaced((kind), (gdlp))
void portEvSurface(const void *hit);   /* a HitThing (bondtypes.h): its texture, triangle, display-list command */
#define GE_EV_SURFACE(hit)                            portEvSurface(hit)
s32 portFxPropImpact(Gfx **gdl, s32 slot);   /* port/src/fxclusters.c: nonzero when it drew bullet hole `slot` */
#define GE_FX_PROP_IMPACT(gdlp, slot)                 portFxPropImpact((gdlp), (slot))
#else
#define GE_RULE_SOLO()           (getPlayerCount() == 1)
#define GE_RULE_MULTI()          (getPlayerCount() >= 2)
#define GE_DEATH_SOLO()          (getPlayerCount() == 1)
#define GE_PVP_ALLOWED()         1
#define GE_AUTOAIM_AT_PLAYER()   1
#define GE_VERSUS_DEATH_STATE()  set_missionstate(6)
#define GE_DOWN_FADE(f)          (f)
#define GE_DOWN_CHRFADE(f)       (f)
#define GE_RESPAWN_BUTTON(x)     (x)
#define GE_SET_BOND_KIA()        (g_isBondKIA = 1)
#define GE_MISSION_END_OK()      1
#define GE_SIM_TICKS(x)          (x)
#define GE_SIM_OWNER()           1
#define GE_COOP_BODY_SLOTS()     0
#define GE_AI_TICK_VIEW(chr)     chrlvActionTick(chr)
#define GE_AI_TICK_FRAME(chr)    chrlvActionTick(chr)
#define GE_COOP_FRAME_TICK()
#define GE_COOP_SPAWN_OFFSET(x, z, st, lx, lz)
#define GE_WATCH_START()         (getPlayerCount() == 1)
#define GE_WATCH_LOCK(l)         (l)
#define GE_FROZEN_CAMERA         bondviewFrozenCameraTick
#define GE_SPLIT_GE2()           (getPlayerCount() >= 2)
#define GE_SPLIT_EQ2()           (getPlayerCount() == 2)
#define GE_SPLIT_GE3()           (getPlayerCount() >= 3)
#define GE_SPLIT_VERT()          0
#define GE_WATCH_NO_OPTIONS()    0
#define GE_COOP_VIEW_ORDER()
#define GE_MP_AVAIL()            joyGetControllerCount()
#define GE_NEED_PADS(n)          (joyGetControllerCount() >= (n))
#define GE_COOP_WAIT()           0
#define GE_AUTOAIM_ON()          cur_player_get_autoaim()
#define GE_PICKUP_TAKEN(prop)    0
#define GE_PICKUP_VISIBLE(prop)  1
#define GE_PICKUP_OP(prop, op)   (op)
#define GE_HAS_KEY_FLAGS(f)      bondinvCheckHasKeyFlags(f)
#define GE_NPC_HURT(chr, dmg, rot, item) bondviewCallRecordDamageKills((dmg), (rot), -1, 1)
#define GE_CHR_PICKABLE(chr)     ((chr)->model)
#define GE_CHR_HIDEABLE(chr)     ((chr)->model != NULL)
#define GE_HAS_PROP_IN_INV(prop) bondinvHasPropInInv(prop)
#define GE_KEY_COPIED()          get_keyanalyzer_flag()
#define GE_PROP_FREED(prop)
#define GE_AI_PLAYERS()          getPlayerCount()
#define GE_BOND_BODY()           (getPlayerCount() == 1)
#define GE_PROJ_SIM_OWNER(own, op, view) (own)
#define GE_EV_SHOT(beam, item, from, to)
#define GE_EV_PUFF(pos, type, size, room)
#define GE_EV_IMPACT(pos, normal, type, room, prop)
#define GE_EV_HIT_CHR(prop, part, dirview, pos)
#define GE_FX_RENDER_REPLACED(kind, gdlp)             0
#define GE_EV_SURFACE(hit)
#define GE_FX_PROP_IMPACT(gdlp, slot)                 0
#endif

#endif
