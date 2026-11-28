#ifndef PORT_LEVELDECOR_H
#define PORT_LEVELDECOR_H

/* leveldecor.h -- the level decor (the static room meshes) drawn from the asset-pack stack (issue #171, wave 2).
 *
 * The stack's levels/<level>/<level>.gltf + .bin (docs/specs/level-decor.md, written by port/export/level_export.c) is
 * read on the game thread into one LdSet per level and stack generation. The game keeps choosing the visible rooms
 * (portals): where it would issue a room's primary or secondary display list (src/game/bg.c, #ifdef PORT) it emits
 * G_LEVELDECOR_EXT instead, and fast3d (port/fast3d/gfx_leveldecor.inc) draws that room's glTF primitives of that list
 * with the room matrix the game loaded just before, the level's live fog, through the static-mesh API.
 *
 * GE_ROM_LEVELS=1 (or --export-level) keeps the ROM display lists.
 *
 * Plain host C (its own target, ge_leveldecor, without the decomp's shadow headers). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* w0: opcode, list (bit 16: 0 primary, 1 secondary; bits 17, 18: the GE_LDSTATS ROM bracket), room number (bits 0..15); w1: 0. */
#define G_LEVELDECOR_EXT 0x4E
#define gSPLevelDecor(pkt, room, list) do { \
        Gfx *_g = (Gfx *)(pkt); \
        _g->words.w0 = ((unsigned int)G_LEVELDECOR_EXT << 24) | (((unsigned int)(list) & 7u) << 16) | ((unsigned int)(room) & 0xFFFFu); \
        _g->words.w1 = 0; \
    } while (0)

/* Floats per vertex of LdRoom.v: x y z in the room's own units (the ROM vertex units: glTF position x level_scale, so the
 * game's room matrix applies unchanged), u v (0..1 over the picture), r g b a (the game's shade: the sRGB bytes / 255,
 * baseColorFactor applied). Triangles are expanded (3 vertices each). */
#define LD_VSTRIDE 9

typedef struct LdPrim {
    uint32_t first, count;   /* vertices of LdRoom.v */
    int texnum;              /* the texture key as a number (texpack), -1 untextured */
    uint8_t list;            /* 0 primary, 1 secondary (extras.n64.list) */
    uint8_t alpha;           /* 0 OPAQUE, 1 MASK, 2 BLEND */
    uint8_t cull;            /* 0 none, 1 back, 2 front (extras.n64.geometry_mode, else doubleSided) */
    uint8_t cms, cmt;        /* 0 wrap, 1 mirror, 2 clamp (the sampler) */
    uint8_t linear;          /* sampler magFilter linear */
    uint8_t ztest, zupd, zcmp;
    uint16_t zmode;          /* other-mode low z mode bits (0xC00 = decal) */
    uint8_t fog;             /* the material blends with fog (other-mode low blender) */
} LdPrim;

typedef struct LdRoom {
    int present;             /* the glTF has a node room_<n> */
    int nprims;
    LdPrim *prims;           /* primary list first, then secondary, each in file order */
    uint32_t nverts;
    float *v;                /* nverts * LD_VSTRIDE */
    uint32_t ntris[2];       /* per list */
    float mn[3], mx[3];      /* world bounds in glTF units (node translation + position); mn > mx when empty */
    float tr[3];             /* the node's translation (glTF units): world = v / scale + tr */
} LdRoom;

typedef struct LdSet {
    char level[32];
    unsigned gen;            /* assetStackGen() it was read for */
    int stage;
    double scale;            /* asset.extras.level_scale */
    int nrooms;              /* rooms[0 .. nrooms-1], indexed by room number */
    LdRoom *rooms;
    int missing;             /* no glTF for the level: nothing is drawn */
} LdSet;

/* ---- the parser (pure, tests/test_leveldecor.c) ---- */
typedef int (*LdReadFn)(void *user, const char *path, uint8_t **data, size_t *len);   /* 1 ok; data freed by LdFreeFn */
typedef void (*LdFreeFn)(void *user, void *data);
/* `path` is the glTF's path for the reader ("levels/dam/dam.gltf"); the buffers are read relative to it. NULL + err on failure. */
LdSet *leveldecorParse(const uint8_t *json, size_t len, const char *path, LdReadFn rd, LdFreeFn fr, void *user, char *err, size_t errsz);
void leveldecorFree(LdSet *s);

/* ---- game thread ---- */
/* 1: draw the rooms from the stack (emit G_LEVELDECOR_EXT); 0: the ROM display lists (GE_ROM_LEVELS=1, --export-level).
 * Loads (or reloads, after a level change or a stack change) the current level's set and hands it to the renderer. */
int leveldecorPrepare(void);

/* GE_LDSTATS=<frame> (dev probe): the ROM path brackets each room list with G_LEVELDECOR_EXT (bit 17 begin, bit 18 end)
 * and the pack path logs what it draws, for the pack-vs-ROM geometry check. The set is read in both paths then. */
int leveldecorStatsOn(void);
#define LD_MARK_ROM_BEGIN (1u << 17)
#define LD_MARK_ROM_END (1u << 18)

/* ---- decoder thread (fast3d) ---- */
/* The set published since the last call (the caller now owns it and frees it with leveldecorFree), or NULL. */
LdSet *leveldecorTake(void);

#ifdef __cplusplus
}
#endif

#endif
