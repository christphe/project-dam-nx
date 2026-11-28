#ifndef PORT_MISSION_H
#define PORT_MISSION_H

/* Custom co-op missions (issues #100-#105): the shared mission format.
 *
 * Compiled into the game (port/src/missions*.c applies it) and into the
 * editor (editor/ edits it). No ROM code and no ROM data: a mission is a
 * delta over one solo level's original setup, and every reference into the
 * original level is a number (setup prop index, pad number, AI list id,
 * item id, model id). Only this file and the mission's own Luau scripts are
 * ever shared between people.
 *
 * A mod IS a campaign: an ordered list of missions. The player picks the mod
 * on the game's main screen (port row, D960 pattern; "GoldenEye" = no mod);
 * after that the game's OWN menus are filled with the mod's content: the
 * mission select shows the mod's missions (names, thumb.png images, unlock
 * state) in place of the original ones, then the normal difficulty and
 * briefing screens (the mission's briefing text and objectives). No extra
 * screens. There is no mission outside a campaign. On disk, next to data/
 * (sdmc:/switch/project-dam/mods/ on the Switch):
 *   mods/<mod>/campaign.json          the campaign (format below)
 *   mods/<mod>/cover.png              optional: the mod's picture on the Mods screen, 4:3 (the editor's Mod tab >
 *                                     Choose picture, 320x240); without it the first mission's thumb.png
 *   mods/<mod>/scripts/*.luau         the mod's own scripts, usable by all its missions
 *   mods/<mod>/<mission>/mission.json one mission
 *   mods/<mod>/<mission>/thumb.png    its image in the mission grid, 4:3
 *                                     (the editor's Capture or Import picture, 320x240)
 * Progress is the game's own save, per mod: while a mod is selected the port
 * points the game's EEPROM file at <save dir>/mods/<id>.eep instead of
 * ge007.eep, so the game's own save code, folders, unlocks and best times
 * work unchanged and the original campaign save is never written. <id> is
 * the campaign's "id" (a UUID made by the editor when the mod is created),
 * so renaming the mod folder keeps the progress. A mod has at most as many
 * missions as the original campaign's mission select.
 *
 * campaign.json, format 1:
 * {
 *   "format": 1,
 *   "id": "3f0c9a52-7d41-4b1e-9a0e-2c6f5b8d1e47",   UUID, set once by the editor; the save key
 *   "name": "Bunker stories",
 *   "author": "display name",
 *   "description": "text for the mod list",
 *   "affects": ["story"],         what the mod replaces: "story" (the solo/co-op mission select)
 *                                 and/or "multiplayer"; shown on the mod chooser
 *   "unlock": "ordered",          "ordered" (as the game: mission n+1 opens when n is completed) | "open"
 *   "missions": [ "bunker_remix", "dam_night" ]   mission folder names, in campaign order
 * }
 * thumb.png is the editor's Capture, a plain crop of the view; the game shows
 * it as the mission's image in the mission select.
 *
 * mission.json, format 1:
 * {
 *   "format": 1,
 *   "name": "Bunker 1 remix",                    display name
 *   "level": "bunker1",                          solo level name, as port/src/main.c kSoloLevels (lowercase)
 *   "description": "text for the list",
 *   "briefing": "text for the briefing page",     optional
 *   "keep_level_objectives": false,               optional: true = the level keeps its own briefing, objectives and end of
 *                                                 mission; this mission's "objectives" (and objectives.add) come after the level's
 *                                                 and must be complete too before the level's exit ends the mission
 *   "level_ai": false,                            accepted and ignored: the game has no native AI interpreter any more;
 *                                                 guards, vehicles and story are driven by the mission's Luau scripts
 *   "level_intro": false,                         accepted and ignored: the game has no native intro (swirl, fixed
 *                                                 cameras) any more; a level starts in play and a script stages its intro
 *                                                 with the camera API
 *   "pads": [                                    new pads; referenced by their string id
 *     { "id": "p1", "pos": [x, y, z], "look": [x, y, z], "room": 12 }
 *   ],
 *   "zones": [ { "id": "z1", "pos": [x, y, z], "radius": 150, "room": 12 } ],
 *   "guards": [
 *     { "op": "move",   "prop": 17, "pad": 0x2715 },
 *     { "op": "move",   "prop": 21, "difficulties": [2, 3] },     a move without "pad" leaves the prop where it is:
 *                                                                      a presence edit (or "ai") of an original prop
 *     { "op": "move",   "prop": 19, "pad": 0x2716, "ai": 0x401 },    a move may carry "ai": the guard's new AI list
 *                                                                      (the same pad number keeps it where it is)
 *     { "op": "add",    "uuid": "6f1c2a9e-3b7d-4e55-9a10-5d2c8b7e4f01", "name": "Kalinin",
 *       "pad": "p1", "model": 3, "ai": 0x401, "weapons": [ { "item": 17 } ] },
 *     { "op": "remove", "prop": 18 }
 *   ],
 *   "objects": [
 *     { "op": "add",    "uuid": "0b8e7c64-52a1-4f3d-8c19-7e6a2d9b3f10", "type": 4, "pad": 0x2733, "model": 120, "item": 33 },
 *     { "op": "move",   "prop": 40, "pad": "p1" },
 *     { "op": "remove", "prop": 41 }
 *   ],
 *   "objectives": [ { "id": "o1", "text": "Find the key", "difficulty": 0 } ],
 *   "environment": {                              optional; every key optional, absent = the level's own
 *     "mood": "dusk",                             "day" | "dusk" | "night" (sun glow, stars, moon)
 *     "sun": { "azimuth": 120, "elevation": 12, "color": [1, 0.8, 0.6], "glow": 0.6 },   degrees
 *     "sky": [0.55, 0.62, 0.7], "clouds": [0.9, 0.9, 0.95], "cloud_speed": 1.0,
 *     "sea": { "enabled": true, "height": -120, "color": [0.1, 0.3, 0.35],
 *              "swell": 1.0, "chop": 0.5, "speed": 1.0 }
 *   },                                            colours 0..1; applied port-side (skyplus.c and the
 *                                                 level's env record copy); fog distance is not here
 *   "scripts": [
 *     { "use": "key_door", "target": { "kind": "prop", "prop": 52 },
 *       "props": { "key": 33, "message": "Locked" } },
 *     { "use": "guard_talk", "target": { "kind": "prop", "uuid": "6f1c2a9e-3b7d-4e55-9a10-5d2c8b7e4f01" },
 *       "props": { "door": 63, "terminal": "0b8e7c64-52a1-4f3d-8c19-7e6a2d9b3f10" } }
 *   ]
 * }
 *
 * - A guard's "weapons" is a list (at most two, one per hand) of { "item": 17, "model": 210, ... } and any field of the
 *   weapon record ("flags": 16384, "collectable": { "timer": -1 }); "weapon": 17 (older files) is a one-item list.
 * - An "add" op may also carry any field of the record it clones (all optional, absent
 *   = the template record's value; the table, names and meaning: mission_fields.h).
 *   A guard: { "op": "add", "pad": "p1", "model": 65535, "head": -1, "health": 80,
 *   "reaction_time": 40, "bitflags": 0, "preset": 0, "chrpreset1": 0 } ("model" is the
 *   body, 0xFFFF = random; "head" -1 = random). An object: header fields flat
 *   ("flags", "flags2", "maxdamage", "damage", "extrascale", "state") and its type's own
 *   fields in a sub-object named for the type, e.g. { "type": 1, "door": { "max_speed": 0.1 } }.
 *   level.json (--export-level) lists the same names for every original prop.
 * - "pad": an integer is an original setup pad number; a string is the id of
 *   one of this mission's "pads".
 * - "prop": the index of a prop in the level's original setup propDefs list,
 *   in file order (as level.json from --export-level lists them). move / remove ops and script references of an ORIGINAL prop
 *   use it.
 * - "uuid" / "name" on every guard or object "add" op: "uuid" is REQUIRED (lowercase RFC 4122 text, 36 characters, unique in the
 *   mission; the editor makes a v4 one), "name" is an optional display label. A prop a mission adds has no stable setup prop
 *   number (it is the level's prop count + its position in the add list), so scripts reference it by its uuid, never by a
 *   number: a "props" value that is a prop of a script (Guard, Door, Prop) is the uuid string, and a script target is
 *   { "kind": "prop", "uuid": "..." } (an original prop: { "kind": "prop", "prop": N }). A number at or past the level's own
 *   prop count is an error. The game resolves each uuid to the setup prop number the added record got when it builds the
 *   setup (a prop that is absent on the chosen difficulty resolves to -1: a script treats it as a missing prop).
 * - "type": the setup prop type number (PROPDEF_*), "model" a model id,
 *   "item" an ITEM_IDS value, "ai" an AI list id.
 * - "difficulties": [0, 1, 2, 3] on an add / move op of a guard or object: it is present only on those difficulties
 *   (0 Agent, 1 Secret Agent, 2 00 Agent, 3 007); absent = all. An object gets the game's own per-difficulty "do not
 *   load" bits (flags2 0x10 << difficulty) for the others; a guard is loaded at every difficulty by the game, so the
 *   port builds the setup for the chosen difficulty and sinks (as "remove") a guard that is not listed, with its weapons
 *   (missionApplySetupEx). An added guard that is not listed is simply not added.
 * - "difficulty": lowest difficulty the objective applies to (0 Agent,
 *   1 Secret Agent, 2 00 Agent).
 * - script "target.kind": "mission" (no other field), "prop" (+ "prop"),
 *   "pad" (+ "pad"), "zone" (+ "zone": id). "props" values are numbers,
 *   strings or booleans; their meaning comes from the script's typed
 *   property declarations.
 * Unknown keys are kept on load and written back on save ("extra"), so a
 * newer editor's fields survive an older one.
 */

#include <stddef.h>
#include <stdint.h>
#include "mission_fields.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MISSION_FORMAT 1

/* A door record's pad field is a bound-pad index WITHOUT the +10000 (the game reads boundpads[door->pad], src/game/prop.c); every
 * other prop type writes a bound pad as pad >= 10000. A door op (add / move) may therefore only name an original pad number
 * >= 10000: a normal pad or a mission pad (mission pads are appended to the normal pads) is refused. One wording for the apply log,
 * the validator and the editor. */
#define MISSION_PROPDEF_DOOR 1
#define MISSION_DOOR_PAD_MSG "a door can only stand on a bound pad of the level (an original pad number >= 10000)"
#define MISSION_ID_LEN 32
#define MISSION_TEXT_LEN 512
#define MISSION_UUID_LEN 40             /* a UUID's 36 characters + NUL, with room */
#define MISSION_NAME_LEN 64             /* an added prop's display name */

typedef struct MissionPadRef {
    int is_new;                     /* 0: original pad number in `num`; 1: new pad id in `id` */
    int num;
    char id[MISSION_ID_LEN];
} MissionPadRef;

typedef struct MissionPad {
    char id[MISSION_ID_LEN];
    float pos[3], look[3];
    int room;
} MissionPad;

typedef struct MissionZone {
    char id[MISSION_ID_LEN];
    float pos[3];
    float radius;
    int room;
} MissionZone;

typedef enum MissionOpKind { MISSION_OP_MOVE, MISSION_OP_ADD, MISSION_OP_REMOVE } MissionOpKind;

/* A weapon an added guard carries: one more record (PROPDEF_COLLECTABLE, assigned to the chr), written by
 * mission_apply with the guard's new chrnum in its pad field. */
#define MISSION_MAX_WEAPONS 2       /* one per hand */
typedef struct MissionWeapon {
    int item;                       /* ITEM_IDS value (-1 = the template's) */
    int model;                      /* guard weapon model (-1 = by item) */
    MissionFieldVals f;             /* the record's own fields (mission_fields.h, type 8): flags (hand), timer, ... */
} MissionWeapon;

/* "difficulties": [0, 1, 2, 3] on a guard / object op that is not "remove": the difficulties the prop is present on
 * (0 Agent, 1 Secret Agent, 2 00 Agent, 3 007). In memory a bit mask, bit n = difficulty n; 0 = no key = all of them
 * (so a zeroed op is "everywhere"; a list of all four is read as absent). */
#define MISSION_DIFF_ALL 0xF

typedef struct MissionGuardOp {
    MissionOpKind op;
    int diff_mask;                  /* ADD / MOVE: the difficulties it is present on (0 = all) */
    int prop;                       /* MOVE/REMOVE: original setup prop index */
    MissionPadRef pad;              /* MOVE/ADD */
    int model, ai;                  /* ADD (-1 = level default); MOVE: ai = the guard's new AI list (-1 = keep its own) */
    int nweapons;                   /* ADD: the carried weapons */
    MissionWeapon weapons[MISSION_MAX_WEAPONS];
    MissionFieldVals f;             /* ADD: the other record fields (mission_fields.h, type 9) */
    char uuid[MISSION_UUID_LEN];    /* ADD: the stable id scripts reference the added guard by (required) */
    char name[MISSION_NAME_LEN];    /* ADD: display name (optional, editor only) */
    int setup_prop;                 /* ADD: the setup prop number the record got (missionApplySetupEx); -1 = not present */
} MissionGuardOp;

typedef struct MissionObjectOp {
    MissionOpKind op;
    int diff_mask;                  /* ADD / MOVE: the difficulties it is present on (0 = all) */
    int prop;                       /* MOVE/REMOVE */
    MissionPadRef pad;              /* MOVE/ADD */
    int type, model, item;          /* ADD (-1 = none) */
    MissionFieldVals f;             /* ADD: the other record fields (mission_fields.h, type = `type`) */
    char uuid[MISSION_UUID_LEN];    /* ADD: the stable id scripts reference the added object by (required) */
    char name[MISSION_NAME_LEN];    /* ADD: display name (optional) */
    int setup_prop;                 /* ADD: the setup prop number the record got; -1 = not present */
} MissionObjectOp;

typedef struct MissionObjective {
    char id[MISSION_ID_LEN];
    char text[MISSION_TEXT_LEN];
    int difficulty;
} MissionObjective;

typedef enum MissionValueKind { MISSION_VAL_NUMBER, MISSION_VAL_STRING, MISSION_VAL_BOOL } MissionValueKind;

typedef struct MissionProp {        /* one script property value */
    char name[MISSION_ID_LEN];
    MissionValueKind kind;
    double num;                     /* NUMBER, BOOL (0/1) */
    char str[MISSION_TEXT_LEN];     /* STRING */
} MissionProp;

typedef enum MissionTargetKind { MISSION_TGT_MISSION, MISSION_TGT_PROP, MISSION_TGT_PAD, MISSION_TGT_ZONE } MissionTargetKind;

typedef struct MissionScript {
    char use[MISSION_ID_LEN];       /* library script name, without .luau */
    MissionTargetKind kind;
    int prop;                       /* PROP: an original prop's number (-1 when `uuid` names an added one) */
    char uuid[MISSION_UUID_LEN];    /* PROP: the uuid of an added prop ("" = `prop` is the target) */
    MissionPadRef pad;              /* PAD */
    char zone[MISSION_ID_LEN];      /* ZONE */
    MissionProp *props; int nprops;
} MissionScript;

typedef enum MissionMood { MISSION_MOOD_KEEP = -1, MISSION_MOOD_DAY, MISSION_MOOD_DUSK, MISSION_MOOD_NIGHT } MissionMood;

/* environment: each has_* flag says the key was present (0 = keep the level's). */
typedef struct MissionEnv {
    MissionMood mood;
    int has_sun;    float sun_azimuth, sun_elevation, sun_color[3], sun_glow;
    int has_sky;    float sky[3];
    int has_clouds; float clouds[3];
    int has_cloud_speed; float cloud_speed;
    int has_sea;    int sea_enabled;      /* "sea" present: sea_enabled from "enabled" (default 1) */
    int has_sea_height; float sea_height;
    int has_sea_color;  float sea_color[3];
    int has_swell;  float swell;
    int has_chop;   float chop;
    int has_speed;  float speed;
} MissionEnv;

typedef struct Mission {
    int format;
    MissionEnv env;
    char name[MISSION_TEXT_LEN];
    char level[MISSION_ID_LEN];
    char description[MISSION_TEXT_LEN];
    char briefing[2048];
    int keep_level_objectives;      /* "keep_level_objectives": the level keeps its briefing, objectives and exit; the mission's are added after */
    int level_ai_off;               /* "level_ai": false: the setup's own AI lists are emptied (missionApplySetupEx) */
    int level_intro_off;            /* "level_intro": false: the setup's intro cameras / swirl / animation are removed */
    MissionPad *pads;             int npads;
    MissionZone *zones;           int nzones;
    MissionGuardOp *guards;       int nguards;
    MissionObjectOp *objects;     int nobjects;
    MissionObjective *objectives; int nobjectives;
    MissionScript *scripts;       int nscripts;
    void *extra;                    /* cJSON* of unknown top-level keys (owned) */
} Mission;

/* All return 1 on success; on failure 0 and a one-line message in err. */
void missionInit(Mission *m);
void missionFree(Mission *m);
int  missionParse(const char *json, Mission *out, char *err, size_t errlen);
int  missionLoadFile(const char *path, Mission *out, char *err, size_t errlen);
/* Returns a malloc'd, pretty-printed JSON string (caller frees). */
char *missionToJson(const Mission *m);
int  missionSaveFile(const char *path, const Mission *m, char *err, size_t errlen);
/* Structural checks that need no level data: format, level name known,
 * pad string ids resolve, zone ids resolve, ids unique, ops complete. */
int  missionValidate(const Mission *m, char *err, size_t errlen);

/* A lowercase RFC 4122 UUID text: 36 characters, dashes at 8/13/18/23, lowercase hex elsewhere. */
int  missionUuidValid(const char *s);
/* The added prop with this uuid: 1 = guard (index into m->guards), 2 = object (m->objects), 0 = none. */
int  missionFindAdded(const Mission *m, const char *uuid, int *index);
/* The setup prop number an added prop got when the mission was applied (see setup_prop); -1 when `uuid` names no added prop
 * or the prop is not present on this difficulty. */
int  missionAddedSetupProp(const Mission *m, const char *uuid);

/* Solo level name -> the -level_XX stage number (e.g. "bunker1" -> 9), -1 if unknown. */
int  missionLevelNumber(const char *name);
/* The i-th solo level name in mission order (0 = "dam"), NULL past the last one. */
const char *missionLevelNameAt(int i);

/* ---- campaign.json (a mod) ---- */
#define CAMPAIGN_FORMAT 1

#define CAMPAIGN_AFFECTS_STORY       1
#define CAMPAIGN_AFFECTS_MULTIPLAYER 2

typedef enum CampaignUnlock { CAMPAIGN_UNLOCK_ORDERED, CAMPAIGN_UNLOCK_OPEN } CampaignUnlock;

#define CAMPAIGN_ID_LEN 64
#define CAMPAIGN_MAX_MISSIONS 32

typedef struct Campaign {
    int format;
    char id[CAMPAIGN_ID_LEN];           /* UUID; empty in a hand-written file: campaignValidate fails */
    char name[MISSION_TEXT_LEN];
    char author[MISSION_TEXT_LEN];
    char description[MISSION_TEXT_LEN];
    CampaignUnlock unlock;
    int affects;                        /* CAMPAIGN_AFFECTS_* bits; default story */
    char (*missions)[MISSION_ID_LEN];  /* mission folder names, in order */
    int nmissions;
    void *extra;                        /* cJSON* of unknown top-level keys (owned) */
} Campaign;

void  campaignInit(Campaign *c);
void  campaignFree(Campaign *c);
int   campaignParse(const char *json, Campaign *out, char *err, size_t errlen);
int   campaignLoadFile(const char *path, Campaign *out, char *err, size_t errlen);
char *campaignToJson(const Campaign *c);
int   campaignSaveFile(const char *path, const Campaign *c, char *err, size_t errlen);
/* name set, at least one mission, folder names non-empty, unique, no '/', '\\' or "..". */
int   campaignValidate(const Campaign *c, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif

#endif
