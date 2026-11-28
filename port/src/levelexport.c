/* levelexport.c -- `ge007 -level_XX --export-level DIR` (issue #102).
 *
 * Loads the level normally, then, at the first room display list of a frame
 * after the level has settled, runs the capture on the DECODER thread (fast3d's
 * gfx_run; port/levelrec/gfx_levelcap.inc holds the capture itself):
 *   - every room is loaded (the game's own bgLoadRoomModelData) and its primary
 *     and secondary display lists are run once under capture, with a model ->
 *     world matrix built here (room position * level scale), so the recording
 *     does not depend on the camera or on what the portal walk happens to show;
 *   - rooms the game had not loaded are unloaded again (delete_room_data);
 *   - level.json is written from the game's structures (rooms, stan tiles,
 *     portals, pads, setup props, ailists) and level.rec from the capture.
 * Then the game is asked to quit (videoRequestQuit).
 *
 * Thread safety: the game thread only builds the next frame while the previous
 * one renders, and pends on its completion; a frame is submitted before this
 * runs, so after a short sleep the game thread is parked waiting for this frame
 * to finish (boss.c pendingGfx == 2). Nothing here runs unless --export-level
 * is given. Output goes only to the directory given. */

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define LE_MKDIR(p) _mkdir(p)
#else
#include <sys/types.h>
#define LE_MKDIR(p) mkdir((p), 0777)
#endif

#include <ultra64.h>
#include "game/bg.h"
#include "game/stan.h"
#include "game/chrai.h"
#include "game/loadobjectmodel.h"
#include "game/matrixmath.h"
#include "game/model.h"
#include "game/chr.h"
#include "game/bondview.h"
#include "bondconstants.h"
#include "portaddr.h"
#include "levelcap.h"
#include "levelrec.h"
#include "system.h"
#include "video.h"
#include "versionhash.h"

extern void bgLoadRoomModelData(s32 roomID);
extern f32 inv_level_scale;
extern u8 list_of_tilesizes[];

#define LE_SETTLE_FRAMES 90   /* game frames the level runs before the capture */

static int s_exportDone = 0;
static int s_firstRoomFrame = -1;
static char s_dir[1024];
static char s_levelName[32];
static int s_stage = -1;

static const struct { const char *name; int num; } kLevels[] = {
    {"dam", 33}, {"facility", 34}, {"runway", 35}, {"surface1", 36},
    {"bunker1", 9}, {"silo", 20}, {"frigate", 26}, {"surface2", 43},
    {"bunker2", 27}, {"statue", 22}, {"archives", 24}, {"streets", 29},
    {"depot", 30}, {"train", 25}, {"jungle", 37}, {"control", 23},
    {"caverns", 39}, {"cradle", 41}, {"aztec", 28}, {"egypt", 32},
    {"cuba", 54},
};

/* 1 when --export-level DIR was given (called once by fast3d at the first frame). */
int levelExportWanted(void)
{
    const char *dir = sysArgGetString("--export-level");
    if (dir == NULL || dir[0] == 0) {
        return 0;
    }
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    s_levelName[0] = 0;
    for (int n = 0; n < 100; n++) {
        char tok[16];
        snprintf(tok, sizeof(tok), "-level_%02d", n);
        if (sysArgCheck(tok)) {
            snprintf(s_levelName, sizeof(s_levelName), "level%02d", n);
            s_stage = n;
            for (size_t k = 0; k < sizeof(kLevels) / sizeof(kLevels[0]); k++) {
                if (kLevels[k].num == n) snprintf(s_levelName, sizeof(s_levelName), "%s", kLevels[k].name);
            }
        }
    }
    sysLogPrintf(LOG_NOTE, "level export: armed, output %s (level %s)", s_dir, s_levelName[0] ? s_levelName : "?");
    levelcapArm(1);
    return 1;
}

/* ---- rooms ---------------------------------------------------------------- */

/* The same address through the K0 mirror and the V1 view compares equal. */
static uintptr_t norm(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    if (a >= (uintptr_t)PORT_DRAM_K0_BASE && a < (uintptr_t)PORT_DRAM_K0_BASE + PORT_DRAM_SIZE) {
        a -= (uintptr_t)PORT_DRAM_K0_BASE - (uintptr_t)PORT_DRAM_V1_BASE;
    }
    return a;
}

static int isRoomDl(const void *dl)
{
    const uintptr_t d = norm(dl);
    for (s32 r = 1; r < g_MaxNumRooms; r++) {
        if (g_BgRoomInfo[r].model_bin_loaded == 0) continue;
        if (g_BgRoomInfo[r].ptr_expanded_mapping_info != NULL && norm(g_BgRoomInfo[r].ptr_expanded_mapping_info) == d) return 1;
    }
    return 0;
}

/* src/game/matrixmath.c, not in its header: save + reset / restore the conversion scale. */
void matrix_4x4_7F058C64(void);
void matrix_4x4_7F058C88(void);

/* model -> world of a room: the level scale and the room position, as
 * setupRoomTransformationMatrix builds it without the player offset. The fixed-point
 * matrix gets only the scale: the position (pos * scale) passes 32767 on big levels
 * (Dam), so it goes to the capture as a float offset (levelcapSetWorldOffset). */
static void roomWorldMatrix(s32 room, s32 out[4][4], float offset[3])
{
    f32 m[4][4];
    const f32 s = get_room_data_float2();
    memset(m, 0, sizeof(m));
    m[0][0] = m[1][1] = m[2][2] = s;
    m[3][3] = 1.0f;
    for (int k = 0; k < 3; k++) offset[k] = ptr_bgdata_room_fileposition_list[room].pos.f[k] * s;
    /* The game bakes the level's visibility scale into every f32 -> fixed conversion
     * (bg.c: matrix_4x4_7F058C4C(mCurrentLevelVisibilityScale); 0.2 on Dam) and undoes it
     * in its projection. The export wants plain world units: the game's own save/reset
     * and restore pair turns it off for this one conversion. */
    matrix_4x4_7F058C64();
    matrix_4x4_f32_to_s32(m, out);
    matrix_4x4_7F058C88();
}

static int s_roomOwner[256];
static int s_roomHasGeometry[256];

static void exportRooms(void)
{
    static s32 mtx[4][4] __attribute__((aligned(16)));
    static Gfx dl[8] __attribute__((aligned(16)));
    int loadedNow = 0, loadedBefore = 0, failed = 0;

    for (s32 r = 1; r < g_MaxNumRooms && r < 256; r++) {
        const int was = g_BgRoomInfo[r].model_bin_loaded != 0;
        if (!was) {
            bgLoadRoomModelData(r);
        }
        if (g_BgRoomInfo[r].model_bin_loaded == 0) {
            failed++;
            sysLogPrintf(LOG_WARNING, "level export: room %d did not load", (int)r);
            continue;
        }
        was ? loadedBefore++ : loadedNow++;
        if (g_BgRoomInfo[r].ptr_expanded_mapping_info == NULL) {
            if (!was) delete_room_data(r);
            continue;
        }
        char name[32];
        snprintf(name, sizeof(name), "room %d", (int)r);
        const int owner = levelcapAddOwner(LEVELREC_OWNER_ROOM, r, name);
        s_roomOwner[r] = owner;
        float offset[3];
        roomWorldMatrix(r, mtx, offset);

        Gfx *g = dl;
        gSPMatrix(g++, mtx, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPSegment(g++, SPSEGMENT_BG_VTX, OS_K0_TO_PHYSICAL(g_BgRoomInfo[r].vertices));
        gSPDisplayList(g++, OS_K0_TO_PHYSICAL(g_BgRoomInfo[r].ptr_expanded_mapping_info));
        if (g_BgRoomInfo[r].ptr_secondary_expanded_mapping_info != NULL) {
            gSPDisplayList(g++, OS_K0_TO_PHYSICAL(g_BgRoomInfo[r].ptr_secondary_expanded_mapping_info));
        }
        gSPEndDisplayList(g++);
        levelcapSetWorldOffset(offset[0], offset[1], offset[2]);
        levelcapRunDl(dl, owner);
        levelcapSetWorldOffset(0.0f, 0.0f, 0.0f);
        s_roomHasGeometry[r] = 1;

        if (!was) {
            delete_room_data(r);
        }
    }
    sysLogPrintf(LOG_NOTE, "level export: %d rooms (%d were loaded, %d loaded for the export, %d failed)",
                 (int)g_MaxNumRooms - 1, loadedBefore, loadedNow, failed);
}

/* ---- level.json ------------------------------------------------------------ */

static void jv(FILE *f, float x, float y, float z)
{
    fprintf(f, "[%.4f,%.4f,%.4f]", x, y, z);
}

static void writeRooms(FILE *f)
{
    const f32 s = get_room_data_float2();
    fprintf(f, "  \"scale\": %.6f,\n  \"rooms\": [", s);
    int first = 1;
    for (s32 r = 1; r < g_MaxNumRooms && r < 256; r++) {
        float mn[3] = { 0, 0, 0 }, mx[3] = { 0, 0, 0 };
        int have = s_roomHasGeometry[r] && levelcapOwnerBounds(s_roomOwner[r], mn, mx);
        if (!have && r < 139) {
            for (int k = 0; k < 3; k++) {
                mn[k] = g_StanRoomBounds[r].min[k] * inv_level_scale;
                mx[k] = g_StanRoomBounds[r].max[k] * inv_level_scale;
            }
        }
        fprintf(f, "%s\n    {\"index\":%d,\"geometry\":%s,\"min\":", first ? "" : ",", (int)r, have ? "true" : "false");
        jv(f, mn[0], mn[1], mn[2]);
        fprintf(f, ",\"max\":");
        jv(f, mx[0], mx[1], mx[2]);
        fprintf(f, "}");
        first = 0;
    }
    fprintf(f, "\n  ],\n");
}

static void writeStan(FILE *f)
{
    fprintf(f, "  \"stan\": [");
    int first = 1, n = 0;
    StandTile *start = NULL;
    for (int r = 0; r < 139; r++) {
        if (firststaninroom[r] != NULL && (start == NULL || (uintptr_t)firststaninroom[r] < (uintptr_t)start)) {
            start = firststaninroom[r];
        }
    }
    for (StandTile *t = start; t != NULL && *(u32 *)t != 0 && n < 200000; n++) {
        const int pc = t->tail.hdrTail.pointCount & 0xf;
        if (pc >= 3 && list_of_tilesizes[pc] != 0) {
            fprintf(f, "%s\n    {\"room\":%d,\"v\":[", first ? "" : ",", (int)t->room);
            for (int i = 0; i < pc; i++) {
                fprintf(f, "%s", i ? "," : "");
                jv(f, t->points[i].x * inv_level_scale, t->points[i].y * inv_level_scale, t->points[i].z * inv_level_scale);
            }
            fprintf(f, "]}");
            first = 0;
        }
        if (list_of_tilesizes[pc] == 0) break;
        t = (StandTile *)((u8 *)t + list_of_tilesizes[pc]);
    }
    fprintf(f, "\n  ],\n");
}

static void writePortals(FILE *f)
{
    const f32 s = get_room_data_float2();
    fprintf(f, "  \"portals\": [");
    int first = 1;
    for (int i = 0; i < PORTMAX && g_BgPortals != NULL && g_BgPortals[i].offset_portal != NULL; i++) {
        const bg_portal_entry *p = g_BgPortals[i].offset_portal;
        const coord3d *pts = &p->point;
        fprintf(f, "%s\n    {\"rooms\":[%d,%d],\"v\":[", first ? "" : ",", (int)g_BgPortals[i].connectedRoom1, (int)g_BgPortals[i].connectedRoom2);
        for (int k = 0; k < p->numPoints; k++) {
            fprintf(f, "%s", k ? "," : "");
            jv(f, pts[k].x * s, pts[k].y * s, pts[k].z * s);
        }
        fprintf(f, "]}");
        first = 0;
    }
    fprintf(f, "\n  ],\n");
}

/* Object-like setup records: ObjectRecord (pad, model = obj). */
static int isObjectType(int t)
{
    switch (t) {
        case PROPDEF_DOOR: case PROPDEF_PROP: case PROPDEF_KEY: case PROPDEF_ALARM: case PROPDEF_CCTV:
        case PROPDEF_MAGAZINE: case PROPDEF_COLLECTABLE: case PROPDEF_MONITOR: case PROPDEF_MULTI_MONITOR:
        case PROPDEF_RACK: case PROPDEF_AUTOGUN: case PROPDEF_HAT: case PROPDEF_AMMO: case PROPDEF_ARMOUR:
        case PROPDEF_GAS_RELEASING: case PROPDEF_VEHICHLE: case PROPDEF_AIRCRAFT: case PROPDEF_UNK41:
        case PROPDEF_GLASS: case PROPDEF_SAFE: case PROPDEF_TANK: case PROPDEF_TINTED_GLASS:
            return 1;
        default:
            return 0;
    }
}


/* ---- models ------------------------------------------------------------------ */

#define LE_MAX_MODELS 512
static struct { int guard; int model; int owner; } s_modelOwner[LE_MAX_MODELS];
static int s_nModelOwners;

static int modelOwnerOf(int guard, int model)
{
    for (int i = 0; i < s_nModelOwners; i++) {
        if (s_modelOwner[i].guard == guard && s_modelOwner[i].model == model) return s_modelOwner[i].owner;
    }
    return -1;
}

extern ModelRenderData D_8002CCBC;   /* chr.c: the renderer's default ModelRenderData */

/* In the executable image, so osVirtualToPhysical() (as the game's own DLs use) can name them. */
static Mtxf s_ident __attribute__((aligned(16)));
static Mtxf s_mtxlist[512] __attribute__((aligned(16)));
static Gfx s_modelDl[65536] __attribute__((aligned(16)));

/* The live Model of a setup prop, or NULL. */
static Model *propModel(PropDefHeaderRecord *p)
{
    if (isObjectType(p->type)) {
        return ((ObjectRecord *)p)->model;
    }
    if (p->type == PROPDEF_GUARD) {
        const GuardRecord *g = (const GuardRecord *)p;
        for (s32 i = 0; g_ChrSlots != NULL && i < g_NumChrSlots; i++) {
            if (g_ChrSlots[i].model != NULL && g_ChrSlots[i].chrnum == (s16)g->chrnum) return g_ChrSlots[i].model;
        }
    }
    return NULL;
}

/* A model with joint (GROUP) nodes but no animation yet would make
 * modelUpdateMatrices read anim->unk06 through NULL (a chr that never spawned). */
static int modelNeedsAnim(Model *m)
{
    ModelNode *node = m->obj->RootNode;
    while (node != NULL) {
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_GROUP) {
            return m->anim == NULL;
        }
        if (node->Child) {
            node = node->Child;
        } else {
            while (node) {
                if (node->Next) { node = node->Next; break; }
                node = node->Parent;
            }
        }
    }
    return 0;
}

/* src/game/propobj.c, not in a header */
Gfx *process_monitor_animation_microcode(Model *model, ModelNode *node, MonitorRecord *screen, Gfx *gdl, s32 arg4, s32 arg5);

#define LE_MON_DL 192
static Gfx s_monDl[4][LE_MON_DL] __attribute__((aligned(16)));
typedef struct { union ModelRwData *rw; Gfx *gdl; Vertex *vtx; } MonSave;

/* A monitor's screen images are not drawn by the model renderer: the object render (propobj.c,
 * sub_GAME_7F04AC20) builds them every frame into the screen node's rw data, with
 * gSPMatrix(model->render_pos) of that frame, the live camera-relative matrix. Replayed as is, the
 * images land thousands of units from the model (Frigate's multi-monitors). Rebuild them here, while
 * render_pos is the export's own list, into our buffers; monitorImagesRestore puts the live ones back.
 * The rebuild steps the screen's TV command script once, harmless on the way out (the game quits). */
static int monitorImagesBuild(Model *m, PropDefHeaderRecord *p, MonSave save[4])
{
    MonitorRecord *screens;
    int count, n = 0;
    if (p->type == PROPDEF_MONITOR) { screens = &((MonitorObjRecord *)p)->Monitor; count = 1; }
    else if (p->type == PROPDEF_MULTI_MONITOR) { screens = ((MultiMonitorObjRecord *)p)->Monitor; count = 4; }
    else return 0;
    for (int k = 0; k < count; k++) {
        ModelNode *node = m->obj->Switches[k];
        if (node == NULL || (node->Opcode & 0xff) != MODELNODE_OPCODE_DLCOLLISION) continue;
        union ModelRwData *rw = modelGetNodeRwData(m, node);
        if (rw == NULL) continue;
        save[n].rw = rw;
        save[n].gdl = rw->DisplayListCollisions.gdl;
        save[n].vtx = rw->DisplayListCollisions.Vertices;
        Gfx *end = process_monitor_animation_microcode(m, node, &screens[k], s_monDl[k], 1, 1);
        if (end - s_monDl[k] > LE_MON_DL) sysLogPrintf(LOG_ERROR, "level export: monitor image list overflow (%d)", (int)(end - s_monDl[k]));
        n++;
    }
    return n;
}

static void monitorImagesRestore(MonSave save[4], int n)
{
    for (int k = 0; k < n; k++) {
        save[k].rw->DisplayListCollisions.gdl = save[k].gdl;
        save[k].rw->DisplayListCollisions.Vertices = save[k].vtx;
    }
}

/* One recording per model type the setup props use: the game's model renderer
 * (subcalcpos / subcalcmatrices / subdraw, the sequence of sub_GAME_7F074790)
 * at the identity matrix, so the joint matrices are model -> model space. The
 * live instance is only read: its render_pos is put back. */
static void exportModels(void)
{
    const char *skip = getenv("GE_EXPORT_NOMODELS");
    if (skip != NULL && skip[0] == '1') return;
    s_ident.m[0][0] = s_ident.m[1][1] = s_ident.m[2][2] = s_ident.m[3][3] = 1.0f;
    int done = 0, bad = 0, noanim = 0;
    PropDefHeaderRecord *p = g_CurrentSetup.propDefs;
    for (int i = 0; p != NULL && p->type != PROPDEF_END && i < 100000; i++, p = p + sizepropdef(p)) {
        const int guard = p->type == PROPDEF_GUARD;
        const int id = guard ? (int)((GuardRecord *)p)->BodyID : isObjectType(p->type) ? (int)((ObjectRecord *)p)->obj : -1;
        if (id < 0 || modelOwnerOf(guard, id) >= 0 || s_nModelOwners >= LE_MAX_MODELS) continue;
        Model *m = propModel(p);
        if (m == NULL || m->obj == NULL || m->obj->RootNode == NULL || m->obj->numMatrices <= 0 || m->obj->numMatrices > 512) {
            continue;
        }
        const int rigid = m->anim == NULL && !guard;   /* a prop: the game builds its matrices by hand (propobj.c) */
        if (!rigid && modelNeedsAnim(m)) { noanim++; continue; }
        RenderPosView *savedPos = m->render_pos;
        ModelRenderData rd = D_8002CCBC;
        rd.basemtx = &s_ident;
        rd.mtxlist = s_mtxlist;
        rd.gdl = s_modelDl;
        rd.flags = 3;
        rd.zbufferenabled = TRUE;
        rd.PropType = guard ? 7 : 9;
        rd.envcolour.word = 0;
        rd.fogcolour.word = 0xFFFFFFFFu;
        rd.cullmode = CULLMODE_BACK;
        if (rigid) {
            /* what objects get in propobj.c: one matrix per joint, the object matrix first (here the model
             * scale, at the origin), then modelUpdateRelationsQuick for the switches / LODs */
            Mtxf *list = s_mtxlist;
            for (int k = 0; k < m->obj->numMatrices; k++) {
                memset(&list[k], 0, sizeof(Mtxf));
                list[k].m[0][0] = list[k].m[1][1] = list[k].m[2][2] = list[k].m[3][3] = 1.0f;
            }
            if (m->scale > 0.0f) list[0].m[0][0] = list[0].m[1][1] = list[0].m[2][2] = m->scale;
            m->render_pos = (RenderPosView *)list;
            modelUpdateRelationsQuick(m, m->obj->RootNode);
        } else {
            /* a chr's root node carries its world position and facing (process_01_group_heading):
             * zero them for the call (restored below) so the model is at the origin, facing +z */
            ModelNode *root = m->obj->RootNode;
            union ModelRwData *rw = (root->Opcode & 0xff) == MODELNODE_OPCODE_HEADER ? modelGetNodeRwData(m, root) : NULL;
            coord3d savedPos;
            memset(&savedPos, 0, sizeof(savedPos));
            f32 savedAngle = 0.0f, savedBlend = 0.0f;
            if (rw != NULL) {
                savedPos = rw->Header.pos;
                savedAngle = rw->Header.unk14;
                savedBlend = rw->Header.unk18;
                rw->Header.pos.x = rw->Header.pos.y = rw->Header.pos.z = 0.0f;
                rw->Header.unk14 = 0.0f;
                rw->Header.unk18 = 0.0f;
            }
            subcalcmatrices(&rd, m);   /* not subcalcpos: it would step the chr's root motion once more */
            if (rw != NULL) {
                rw->Header.pos = savedPos;
                rw->Header.unk14 = savedAngle;
                rw->Header.unk18 = savedBlend;
            }
        }
        MonSave monSave[4];
        const int nMon = rigid ? monitorImagesBuild(m, p, monSave) : 0;   /* render_pos is the export's list now */
        subdraw(&rd, m);
        Gfx *end = rd.gdl;
        if (end - s_modelDl > 65000) { bad++; monitorImagesRestore(monSave, nMon); m->render_pos = savedPos; continue; }
        gSPEndDisplayList(end++);
        matrix_4x4_7F058C64();   /* no visibility scale in the conversion (see roomWorldMatrix) */
        bondviewTransformManyPosToViewMatrix(m->render_pos, m->obj->numMatrices);   /* Mtxf -> the Mtx the DL reads */
        matrix_4x4_7F058C88();
        char name[32];
        snprintf(name, sizeof(name), "%s %d", guard ? "guard" : "object", id);
        const int owner = levelcapAddOwner(LEVELREC_OWNER_MODEL, id, name);
        s_modelOwner[s_nModelOwners].guard = guard;
        s_modelOwner[s_nModelOwners].model = id;
        s_modelOwner[s_nModelOwners].owner = owner;
        s_nModelOwners++;
        levelcapRunDl(s_modelDl, owner);
        monitorImagesRestore(monSave, nMon);
        m->render_pos = savedPos;
        done++;
    }
    sysLogPrintf(LOG_NOTE, "level export: %d model types recorded (%d too large, %d chrs without animation)", done, bad, noanim);
}


/* the field tables' offsets (port/mission/mission_fields.c) must be the structs' */
#define FA(T, m, off) _Static_assert(__builtin_offsetof(T, m) == (off), #T "." #m)
FA(GuardRecord, extrascale, 0);
FA(GuardRecord, state, 2);
FA(GuardRecord, Preset, 12);
FA(GuardRecord, chrpreset1, 14);
FA(GuardRecord, health, 16);
FA(GuardRecord, ReactionTime, 18);
FA(GuardRecord, bitflags, 20);
FA(GuardRecord, HeadID, 22);
FA(ObjectRecord, extrascale, 0);
FA(ObjectRecord, state, 2);
FA(ObjectRecord, flags, 8);
FA(ObjectRecord, flags2, 12);
FA(ObjectRecord, maxdamage, 128);
FA(ObjectRecord, damage, 132);
FA(DoorRecord, linkedDoorOffset, 144);
FA(DoorRecord, maxFrac, 148);
FA(DoorRecord, perimFrac, 152);
FA(DoorRecord, accel, 156);
FA(DoorRecord, decel, 160);
FA(DoorRecord, maxSpeed, 164);
FA(DoorRecord, doorFlags, 168);
FA(DoorRecord, doorType, 170);
FA(DoorRecord, keyflags, 172);
FA(DoorRecord, autoCloseFrames, 176);
FA(DoorRecord, doorOpenSound, 180);
FA(KeyRecord, keyflags, 144);
FA(CCTVRecord, lookpad, 144);
FA(AmmoCrateRecord, ammoType, 144);
FA(WeaponObjRecord, weaponnum, 144);
FA(WeaponObjRecord, LinkedWeaponType, 145);
FA(WeaponObjRecord, timer, 146);
FA(MonitorObjRecord, OwnerOffset, 272);
FA(MonitorObjRecord, OwnerPart, 276);
FA(MonitorObjRecord, ImageNum, 280);
FA(MultiMonitorObjRecord, ImageNums[0], 656);
FA(MultiMonitorObjRecord, ImageNums[1], 657);
FA(MultiMonitorObjRecord, ImageNums[2], 658);
FA(MultiMonitorObjRecord, ImageNums[3], 659);
FA(AutogunRecord, padID, 144);
FA(AutogunRecord, rot_related, 148);
FA(AutogunRecord, unk88, 152);
FA(AutogunRecord, unk8C, 156);
FA(AutogunRecord, speed, 180);
FA(AutogunRecord, aimdist, 184);
FA(AutogunRecord, unkAC, 188);
FA(AutogunRecord, unkB8, 200);
FA(AutogunRecord, unkBC, 204);
FA(AutogunRecord, unkC0, 208);
FA(AutogunRecord, is_active, 240);
FA(MultiAmmoCrateRecord, slots[0].modelnum, 144);
FA(MultiAmmoCrateRecord, slots[0].quantity, 146);
FA(MultiAmmoCrateRecord, slots[1].modelnum, 148);
FA(MultiAmmoCrateRecord, slots[1].quantity, 150);
FA(MultiAmmoCrateRecord, slots[2].modelnum, 152);
FA(MultiAmmoCrateRecord, slots[2].quantity, 154);
FA(MultiAmmoCrateRecord, slots[3].modelnum, 156);
FA(MultiAmmoCrateRecord, slots[3].quantity, 158);
FA(MultiAmmoCrateRecord, slots[4].modelnum, 160);
FA(MultiAmmoCrateRecord, slots[4].quantity, 162);
FA(MultiAmmoCrateRecord, slots[5].modelnum, 164);
FA(MultiAmmoCrateRecord, slots[5].quantity, 166);
FA(MultiAmmoCrateRecord, slots[6].modelnum, 168);
FA(MultiAmmoCrateRecord, slots[6].quantity, 170);
FA(MultiAmmoCrateRecord, slots[7].modelnum, 172);
FA(MultiAmmoCrateRecord, slots[7].quantity, 174);
FA(MultiAmmoCrateRecord, slots[8].modelnum, 176);
FA(MultiAmmoCrateRecord, slots[8].quantity, 178);
FA(MultiAmmoCrateRecord, slots[9].modelnum, 180);
FA(MultiAmmoCrateRecord, slots[9].quantity, 182);
FA(MultiAmmoCrateRecord, slots[10].modelnum, 184);
FA(MultiAmmoCrateRecord, slots[10].quantity, 186);
FA(MultiAmmoCrateRecord, slots[11].modelnum, 188);
FA(MultiAmmoCrateRecord, slots[11].quantity, 190);
FA(MultiAmmoCrateRecord, slots[12].modelnum, 192);
FA(MultiAmmoCrateRecord, slots[12].quantity, 194);
FA(BodyArmourRecord, initialamount, 144);
FA(VehichleRecord, aioffset, 152);
FA(VehichleRecord, aireturnlist, 154);
FA(AircraftRecord, aioffset, 152);
FA(AircraftRecord, aireturnlist, 154);
FA(TankRecord, unkA4, 184);
FA(TankRecord, unkA8, 188);
FA(TankRecord, unkAC, 192);
FA(TankRecord, unkB0, 196);
FA(TankRecord, unkB4, 200);
FA(TankRecord, unkB8, 204);
FA(TankRecord, unkBC, 208);
FA(TankRecord, unkC0, 212);
FA(TintedGlassRecord, TintDist, 144);
FA(TintedGlassRecord, CullDist, 148);
FA(TintedGlassRecord, unk90, 160);
#undef FA

/* Which recorded model owner draws which model id (the editor reads the setup itself, from the pccg sidecar:
 * pads, props, fields, AI lists; see port/mission/mission_setup.h). Only the renderer knows the owners. */
static void writeModels(FILE *f)
{
    fprintf(f, "  \"models\": [");
    for (int i = 0; i < s_nModelOwners; i++) {
        fprintf(f, "%s\n    {\"guard\":%s,\"model\":%d,\"owner\":%d}", i ? "," : "", s_modelOwner[i].guard ? "true" : "false",
                s_modelOwner[i].model, s_modelOwner[i].owner);
    }
    fprintf(f, "\n  ]\n");
}

extern char *setup_text_pointers[];   /* src/game/chraidata.c: stage number -> "Usetup<name>Z" */

static int writeJson(void)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/level.json", s_dir);
    FILE *f = fopen(path, "wb");
    if (f == NULL) return 1;
    fprintf(f, "{\n  \"version\": 1,\n  \"level\": \"%s\",\n  \"build\": \"%s\",\n", s_levelName, GE007_VERSION_HASH);
    /* the setup row the editor reads from the pccg sidecar */
    fprintf(f, "  \"setup\": \"%s\",\n", (s_stage >= 0 && s_stage <= 90 && setup_text_pointers[s_stage]) ? setup_text_pointers[s_stage] : "");
    writeRooms(f);
    writeStan(f);
    writePortals(f);
    writeModels(f);
    fprintf(f, "}\n");
    return fclose(f) != 0;
}

/* ---- the driver ------------------------------------------------------------- */

static int writeAll(const char *what)
{
    int ntex = 0, nowners = 0, nruns = 0;
    unsigned long long ntris = 0;
    levelcapCounts(&ntex, &nowners, &nruns, &ntris);
    char path[1100];
    snprintf(path, sizeof(path), "%s/level.rec", s_dir);
    const int rc = levelcapWriteRec(path, GE007_VERSION_HASH, s_levelName);
    const int jc = writeJson();
    sysLogPrintf(rc == 0 && jc == 0 ? LOG_NOTE : LOG_ERROR,
                 "level export (%s): %s: %d textures, %d owners, %d runs, %llu triangles (level.rec %s, level.json %s)",
                 what, s_dir, ntex, nowners, nruns, ntris, rc == 0 ? "ok" : "FAILED", jc == 0 ? "ok" : "FAILED");
    return rc || jc;
}

static void runExport(void)
{
    LE_MKDIR(s_dir);
    levelcapArm(0);
    /* let the game thread finish the frame it is building and park (see the header) */
    sysSleep(250000);

    levelcapBegin();
    exportRooms();
    writeAll("rooms");   /* on disk before the model renderer runs: a crash there keeps the rooms */
    exportModels();
    levelcapEnd();
    writeAll("final");
    videoRequestQuit("level export done");
}

/* fast3d: a G_DL is about to run `dl`. The capture runs at the first room list
 * once the level has run LE_SETTLE_FRAMES frames. */
int levelExportOnDl(const void *dl)
{
    if (s_exportDone || !isRoomDl(dl)) {
        return 0;
    }
    const int frame = (int)videoGetFrameCount();
    if (s_firstRoomFrame < 0) {
        s_firstRoomFrame = frame;
    }
    if (frame - s_firstRoomFrame < LE_SETTLE_FRAMES) {
        return 0;
    }
    s_exportDone = 1;
    runExport();
    return 1;
}
