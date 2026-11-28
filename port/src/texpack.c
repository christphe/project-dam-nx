/*
 * texpack.c -- asset packs in the game: their textures (port/include/texpack.h; the stack is port/assets/ge_assetstack.h).
 *
 * Two tables, one lock:
 *  - address -> texture number, filled by texLoad on the game thread (the texture pool is a ring: a reused address is
 *    registered again with its new number). Addresses are taken in the V1 view of DRAM, the KSEG0 mirror folded onto it,
 *    as fast3d sees them either way.
 *  - texture number -> the pack's decoded picture, filled on the render thread the first time fast3d uploads that texture
 *    (one PNG decode, then fast3d's own texture cache keeps it on the GPU). Pictures are kept up to a byte budget and the
 *    oldest dropped past it; a dropped one is decoded again if fast3d ever uploads it again.
 * The enabled packs (Video.AssetPacks, in the player's order) are read through the stack's own indexes (assetStackOpen):
 * never through the package layer's shared cache, which the game thread uses for mods. Every pack may be partial: a texture
 * no pack and no mod has is the game's own.
 * The mod of the active mission (texpackSetMod) is a source with the same layout (textures/<n>.png in X.pmod), looked up
 * before the packs: it always wins. Stopgap until the game draws its levels from the glTF export: the editor already resolves a mod's
 * textures there (editor/decor.cpp), and this mod lookup goes away with the texLoad hook then.
 */
#include "platform.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "config.h"
#include "portaddr.h"
#include "texpack.h"
#include "ge_texpacks.h"
#include "ge_assetstack.h"
#include "ge_pkg.h"
#include "ge_png.h"

#define REG_SIZE 16384            /* registered textures kept; cleared when full */
#ifdef __SWITCH__
#define CACHE_BUDGET (192u << 20)
#else
#define CACHE_BUDGET (768u << 20)
#endif

#define FMT_IA 3                  /* G_IM_FMT_IA (PR/gbi.h) */
#define FMT_I 4                   /* G_IM_FMT_I */

static char s_cfgList[ASSETPACK_LIST_LEN];  /* Video.AssetPacks: every known pack in order, a disabled one as "-name" */
static char s_cfgLegacy[TEXPACK_FILE_LEN];  /* Video.TexturePack: the one pack of older versions, migrated by texpackInit */
static char s_inUse[ASSETPACK_LIST_LEN];
static AssetStack *s_stack;
static TexPackIndex *s_modIx;     /* the active mission's mod: its textures/ come first */
static char s_modDir[600];
static unsigned s_gen = 1;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/* The registered textures, sorted by address, none overlapping: a load by sub-rectangles (gDPLoadTile strips) starts inside
 * the texture, so the lookup is by range. */
typedef struct { uintptr_t addr; uint32_t size; int16_t texnum; } RegSlot;
static RegSlot s_reg[REG_SIZE];
static int s_regN;

typedef struct {
    uint8_t *rgba, *lum;          /* the picture; the same with alpha = luminance (intensity tiles), made on demand */
    int w, h, opaque, failed;
    unsigned order;               /* when it was decoded: the oldest goes first */
} Pic;
static Pic s_pic[TEXPACK_MAX_TEXNUM + 1];
static size_t s_picBytes;
#define CHECKER_SIZE 8
#define MISSING_LOG_MAX 64        /* "missing texture" warnings: one per texture number, up to this many */
static uint8_t s_checker[CHECKER_SIZE * CHECKER_SIZE * 4];   /* magenta / black, made on first use */
static uint8_t s_warned[TEXPACK_MAX_TEXNUM + 1];
static int s_warnedN;
static unsigned s_picTick;
static unsigned s_picGen;         /* the s_gen the pictures belong to: the render thread drops them when it changes */

PD_CONSTRUCTOR static void texpackConfigInit(void)
{
    configRegisterString("Video.AssetPacks", s_cfgList, sizeof s_cfgList);
    configRegisterString("Video.TexturePack", s_cfgLegacy, sizeof s_cfgLegacy);
}

/* The folders packs are found in: assetpacks/, then the older texturepacks/ (a file name found in both: assetpacks/ wins). */
static const char *const *packsDirs(void)
{
    static char a[600], t[600];
    static const char *dirs[2] = { a, t };
    snprintf(a, sizeof a, "%s", sysResolvePath("$S/../assetpacks"));
    snprintf(t, sizeof t, "%s", sysResolvePath("$S/../texturepacks"));
    return dirs;
}

/* GE_ROM_TEXTURES=1 (dev token): every texture is drawn from the ROM's own decode, the mod and the packs are not asked. */
static int romTextures(void)
{
    static int v = -1;
    if (v < 0) { const char *e = getenv("GE_ROM_TEXTURES"); v = (e && atoi(e) > 0) ? 1 : 0; }
    return v;
}

static uintptr_t normAddr(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    if (a >= (uintptr_t)PORT_DRAM_K0_BASE && a < (uintptr_t)PORT_DRAM_K0_BASE + PORT_DRAM_SIZE)
        a -= (uintptr_t)PORT_DRAM_K0_BASE - (uintptr_t)PORT_DRAM_V1_BASE;
    return a;
}

static void picFree(Pic *p)
{
    s_picBytes -= (size_t)p->w * p->h * 4 * ((p->rgba ? 1 : 0) + (p->lum ? 1 : 0));
    free(p->rgba);
    free(p->lum);
    memset(p, 0, sizeof *p);
}

static void picsClear(void)
{
    for (int i = 0; i <= TEXPACK_MAX_TEXNUM; i++) if (s_pic[i].rgba || s_pic[i].failed) picFree(&s_pic[i]);
    s_picBytes = 0;
}

void texpackSetPacks(const char *list)
{
    static char all[ASSETPACK_MAX][ASSETPACK_FILE_LEN], files[ASSETPACK_MAX][ASSETPACK_FILE_LEN];
    static AssetPackInfo info[ASSETPACK_MAX * 2];
    static char rootBuf[ASSETPACK_MAX][800];
    const char *roots[ASSETPACK_MAX];
    char text[ASSETPACK_LIST_LEN];
    int on[ASSETPACK_MAX];
    const int nAll = assetPackEntriesParse(list ? list : "", all, on, ASSETPACK_MAX);
    const int n = assetPackEntriesEnabled(all, on, nAll, files, ASSETPACK_MAX);   /* the stack opens only the enabled ones, in order */
    int found = assetPackScan(packsDirs(), 2, info, ASSETPACK_MAX * 2);
    AssetStack *st;
    if (found > ASSETPACK_MAX * 2) found = ASSETPACK_MAX * 2;
    for (int i = 0; i < n; i++) {   /* a pack in the list but not found keeps its place: it may come back */
        const char *dir = packsDirs()[0];
        for (int k = 0; k < found; k++) if (!strcmp(info[k].file, files[i])) { dir = info[k].dir; break; }
        snprintf(rootBuf[i], sizeof rootBuf[i], "%s/%s", dir, files[i]);
        roots[i] = rootBuf[i];
    }
    assetPackEntriesFormat(all, on, nAll, text, sizeof text);
    snprintf(s_cfgList, sizeof s_cfgList, "%s", text);
    st = assetStackOpen(roots, n);
    for (int i = 0; i < n; i++) {
        if (!assetStackOpened(st, i)) sysLogPrintf(LOG_WARNING, "asset packs: cannot read %s (a .passets tar or folder expected)", roots[i]);
        else sysLogPrintf(LOG_INFO, "asset packs: %d. %s, %d textures", i + 1, roots[i], assetStackTextureCount(st, i));
    }
    if (!n && assetStackCount(s_stack)) sysLogPrintf(LOG_INFO, "asset packs: none");
    pthread_mutex_lock(&s_lock);
    assetStackClose(s_stack);
    s_stack = st;
    snprintf(s_inUse, sizeof s_inUse, "%s", text);
    s_gen++;   /* new cache keys: fast3d uploads every replaced texture again; the render thread drops the old pictures */
    pthread_mutex_unlock(&s_lock);
}

void texpackSetMod(const char *dir)
{
    TexPackIndex *ix = NULL;
    if (!dir) dir = "";
    if (!strcmp(dir, s_modDir)) return;
    if (dir[0]) {
        ix = texpackIndexOpen(dir);
        if (ix && texpackIndexCount(ix) == 0) { texpackIndexClose(ix); ix = NULL; }
        if (ix) sysLogPrintf(LOG_INFO, "asset packs: the mod %s replaces %d textures", dir, texpackIndexCount(ix));
    }
    pthread_mutex_lock(&s_lock);
    texpackIndexClose(s_modIx);
    s_modIx = ix;
    snprintf(s_modDir, sizeof s_modDir, "%s", dir);
    s_gen++;
    pthread_mutex_unlock(&s_lock);
}

/* 1 when the mod or an enabled pack has texture n. Under s_lock. */
static int hasSource(int n)
{
    return assetStackTextureFrom(s_stack, s_modIx, n) != ASSET_FROM_NONE;
}

void texpackInit(void)
{
    char want[ASSETPACK_LIST_LEN];
    if (!s_cfgList[0] && s_cfgLegacy[0]) {   /* an older ini: its one texture pack becomes the list */
        sysLogPrintf(LOG_INFO, "asset packs: Video.TexturePack %s moves to Video.AssetPacks", s_cfgLegacy);
        snprintf(s_cfgList, sizeof s_cfgList, "%s", s_cfgLegacy);
    }
    s_cfgLegacy[0] = 0;
    {   /* the base pack was texturepacks/ge007.ptexpack; it is assetpacks/ge007.passets now */
        static char files[ASSETPACK_MAX][ASSETPACK_FILE_LEN];
        int on[ASSETPACK_MAX];
        const int n = assetPackEntriesParse(s_cfgList, files, on, ASSETPACK_MAX);
        const int k = assetPackListFind(files, n, "ge007.ptexpack");
        if (k >= 0) {
            snprintf(files[k], ASSETPACK_FILE_LEN, "%s", "ge007.passets");
            assetPackEntriesFormat(files, on, n, s_cfgList, sizeof s_cfgList);
        }
    }
    if (!s_cfgList[0]) {   /* no pack list at all (first launch, nothing ever chosen): every pack found is on. A list whose packs are all "-off" is not empty. */
        static AssetPackInfo info[ASSETPACK_MAX * 2];
        static char files[ASSETPACK_MAX][ASSETPACK_FILE_LEN];
        int on[ASSETPACK_MAX], n = 0;
        int found = assetPackScan(packsDirs(), 2, info, ASSETPACK_MAX * 2);
        if (found > ASSETPACK_MAX * 2) found = ASSETPACK_MAX * 2;
        for (int k = 0; k < found && n < ASSETPACK_MAX; k++) {
            if (assetPackListFind(files, n, info[k].file) >= 0) continue;
            snprintf(files[n], ASSETPACK_FILE_LEN, "%s", info[k].file);
            on[n++] = 1;
        }
        assetPackEntriesFormat(files, on, n, s_cfgList, sizeof s_cfgList);
    }
    snprintf(want, sizeof want, "%s", s_cfgList);
    if (want[0]) texpackSetPacks(want);
}

const char *texpackPacks(void) { return s_inUse; }

/* A path inside a pack: no "..", no drive, no leading slash. */
static int relOk(const char *rel)
{
    if (!rel || !rel[0] || rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':')) return 0;
    for (const char *p = rel; *p; p++)
        if (p[0] == '.' && p[1] == '.' && (p == rel || p[-1] == '/' || p[-1] == '\\')) return 0;
    return 1;
}

int assetRead(const char *rel, unsigned char **out, unsigned long *len, int *from)
{
    char path[1400];
    uint8_t *buf = NULL;
    size_t n = 0;
    *out = NULL; *len = 0;
    if (from) *from = ASSET_FROM_NONE;
    if (!relOk(rel)) return 0;
    if (s_modDir[0]) {
        snprintf(path, sizeof path, "%s/%s", s_modDir, rel);
        if (pkgReadFile(path, &buf, &n)) { *out = buf; *len = (unsigned long)n; if (from) *from = ASSET_FROM_MOD; return 1; }
    }
    for (int i = 0; i < assetStackCount(s_stack); i++) {
        if (!assetStackOpened(s_stack, i)) continue;
        snprintf(path, sizeof path, "%s/%s", assetStackRoot(s_stack, i), rel);
        if (pkgReadFile(path, &buf, &n)) { *out = buf; *len = (unsigned long)n; if (from) *from = i; return 1; }
    }
    return 0;
}

unsigned assetStackGen(void)
{
    unsigned g;
    pthread_mutex_lock(&s_lock);
    g = s_gen;
    pthread_mutex_unlock(&s_lock);
    return g;
}

int assetHas(const char *rel)
{
    char path[1400];
    if (!relOk(rel)) return 0;
    if (s_modDir[0]) { snprintf(path, sizeof path, "%s/%s", s_modDir, rel); if (pkgExists(path)) return 1; }
    for (int i = 0; i < assetStackCount(s_stack); i++) {
        if (!assetStackOpened(s_stack, i)) continue;
        snprintf(path, sizeof path, "%s/%s", assetStackRoot(s_stack, i), rel);
        if (pkgExists(path)) return 1;
    }
    return 0;
}

void texpackPackWritten(const char *file, int add)
{
    static char files[ASSETPACK_MAX][ASSETPACK_FILE_LEN];
    char text[ASSETPACK_LIST_LEN];
    int on[ASSETPACK_MAX];
    int n = assetPackEntriesParse(s_cfgList, files, on, ASSETPACK_MAX);
    const int added = add && assetPackIsFileName(file) && assetPackListFind(files, n, file) < 0 && n < ASSETPACK_MAX;
    if (added) { snprintf(files[n], ASSETPACK_FILE_LEN, "%s", file); on[n++] = 1; }   /* first write: enabled, last */
    assetPackEntriesFormat(files, on, n, text, sizeof text);
    texpackSetPacks(text);   /* reopened either way: a pack in the list may not have existed until now */
    if (added) configSave();
}

int texpackListPacks(char (*files)[128], char (*names)[64], char (*authors)[64], int max)
{
    static AssetPackInfo info[64];
    int n = assetPackScan(packsDirs(), 2, info, 64);
    if (n > 64) n = 64;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        snprintf(files[i], 128, "%s", info[i].file);
        snprintf(names[i], 64, "%s", info[i].name);
        snprintf(authors[i], 64, "%s", info[i].author);
    }
    return n;
}

/* The first entry whose end is past `a` (entries are sorted and disjoint). Under s_lock. */
static int regFirstEnding(uintptr_t a)
{
    int lo = 0, hi = s_regN;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (s_reg[mid].addr + s_reg[mid].size <= a) lo = mid + 1; else hi = mid;
    }
    return lo;
}

void texpackRegister(const void *data, int texnum, int bytes)
{
    const uintptr_t a = normAddr(data);
    const uint32_t size = bytes > 8 ? (uint32_t)bytes : 8u;
    int first, last;
    if (!data || texnum < 0 || texnum > TEXPACK_MAX_TEXNUM) return;
    pthread_mutex_lock(&s_lock);
    if (s_regN >= REG_SIZE - 1) s_regN = 0;
    first = regFirstEnding(a);                      /* the entries [first, last) overlap the new range: the ring reused it */
    last = first;
    while (last < s_regN && s_reg[last].addr < a + size) last++;
    if (last - first != 1) memmove(&s_reg[first + 1], &s_reg[last], (size_t)(s_regN - last) * sizeof s_reg[0]);
    s_regN += 1 - (last - first);
    s_reg[first].addr = a; s_reg[first].size = size; s_reg[first].texnum = (int16_t)texnum;
    pthread_mutex_unlock(&s_lock);
}

/* The texture holding address `a`: its number, and a's offset into it. A load by blocks starts at the texture's own address
 * (a mip level further in is not the picture); a load by tiles may start anywhere inside. -1 when none. Under s_lock. */
static int lookupLocked(uintptr_t a, int fromTile, uint32_t *offset)
{
    const int i = regFirstEnding(a);
    if (i >= s_regN || s_reg[i].addr > a) return -1;
    if (!fromTile && s_reg[i].addr != a) return -1;
    *offset = (uint32_t)(a - s_reg[i].addr);
    return s_reg[i].texnum;
}

unsigned texpackSalt(const void *addr, int fromTile, int *texnum, uint32_t *offset)
{
    unsigned salt = 0;
    *texnum = -1;
    *offset = 0;
    if (romTextures()) return 0;                            /* GE_ROM_TEXTURES=1: the ROM's own decode, stack bypassed */
    pthread_mutex_lock(&s_lock);
    if (s_picGen != s_gen) { picsClear(); s_picGen = s_gen; }
    {
        uint32_t off = 0;
        const int n = lookupLocked(normAddr(addr), fromTile, &off);
        if (n >= 0) {   /* a registered texture: the picture, or the checker when no pack has it or it cannot be read */
            *texnum = n;
            *offset = off;
            salt = ((0x5EA70000u ^ (s_gen * 2654435761u)) + (unsigned)n * 40503u + off * 2246822519u) | 1u;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return salt;
}

static void evictFor(size_t need)
{
    while (s_picBytes + need > CACHE_BUDGET) {
        int old = -1;
        for (int i = 0; i <= TEXPACK_MAX_TEXNUM; i++)
            if (s_pic[i].rgba && (old < 0 || s_pic[i].order < s_pic[old].order)) old = i;
        if (old < 0) return;
        picFree(&s_pic[old]);
    }
}

/* The stand-in for a texture no mod and no pack has (or whose picture cannot be read): a small magenta / black checker; the
 * vertex pass normalises texture coordinates by the tile's texel count, so any size keeps the UVs working. */
static const uint8_t *checkerPixels(int texnum, int missing, int *w, int *h)
{
    if (!s_checker[3]) {
        for (int y = 0; y < CHECKER_SIZE; y++)
            for (int x = 0; x < CHECKER_SIZE; x++) {
                uint8_t *d = s_checker + (y * CHECKER_SIZE + x) * 4;
                const int on = ((x / 2) + (y / 2)) & 1;
                d[0] = on ? 255 : 0; d[1] = 0; d[2] = on ? 255 : 0; d[3] = 255;
            }
    }
    if (missing && !s_warned[texnum]) {
        s_warned[texnum] = 1;
        if (++s_warnedN <= MISSING_LOG_MAX)
            sysLogPrintf(LOG_WARNING, "asset packs: missing texture %d (no mod, no pack has textures/%d.png): a checker is drawn%s",
                         texnum, texnum, s_warnedN == MISSING_LOG_MAX ? " (further missing textures are not logged)" : "");
    }
    *w = *h = CHECKER_SIZE;
    return s_checker;
}

const uint8_t *texpackPixels(int texnum, int fmt, int maxSize, int *w, int *h)
{
    const uint8_t *out = NULL;
    Pic *p;
    if (texnum < 0 || texnum > TEXPACK_MAX_TEXNUM) return NULL;
    pthread_mutex_lock(&s_lock);
    if (s_picGen != s_gen) { picsClear(); s_picGen = s_gen; }
    p = &s_pic[texnum];
    if (!p->rgba && !p->failed && hasSource(texnum)) {
        uint8_t *file = NULL, *rgba = NULL;
        size_t len = 0;
        int pw = 0, ph = 0;
        if (assetStackReadTexture(s_stack, s_modIx, texnum, &file, &len) && gePngDecode(file, len, &pw, &ph, &rgba)) {
            if (pw > maxSize || ph > maxSize) {
                sysLogPrintf(LOG_WARNING, "asset packs: textures/%d.png is %dx%d, over this GPU's %d: a checker is drawn", texnum, pw, ph, maxSize);
                free(rgba);
                p->failed = 1;
            } else {
                const size_t n = (size_t)pw * ph;
                evictFor(n * 4);
                p->rgba = rgba; p->w = pw; p->h = ph; p->order = ++s_picTick;
                p->opaque = 1;
                for (size_t i = 0; i < n; i++) if (rgba[i * 4 + 3] != 255) { p->opaque = 0; break; }
                s_picBytes += n * 4;
            }
        } else {
            sysLogPrintf(LOG_WARNING, "asset packs: textures/%d.png is not readable (an 8-bit PNG, not interlaced, is expected): a checker is drawn", texnum);
            p->failed = 1;
        }
        free(file);
    }
    if (p->rgba) {
        out = p->rgba;
        if ((fmt == FMT_I || fmt == FMT_IA) && p->opaque) {   /* the RDP reads an intensity texel's alpha from its value */
            if (!p->lum && (p->lum = (uint8_t *)malloc((size_t)p->w * p->h * 4)) != NULL) {
                for (size_t i = 0; i < (size_t)p->w * p->h; i++) {
                    const uint8_t *s = p->rgba + i * 4;
                    uint8_t *d = p->lum + i * 4;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                    d[3] = (uint8_t)((s[0] * 299 + s[1] * 587 + s[2] * 114) / 1000);
                }
                s_picBytes += (size_t)p->w * p->h * 4;
            }
            if (p->lum) out = p->lum;
        }
        *w = p->w; *h = p->h;
    } else {
        out = checkerPixels(texnum, !hasSource(texnum), w, h);
    }
    pthread_mutex_unlock(&s_lock);
    return out;
}
