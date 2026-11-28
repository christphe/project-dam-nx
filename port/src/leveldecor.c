/* leveldecor.c -- the level decor from the asset-pack stack: glTF -> per-room vertex arrays (see port/include/leveldecor.h).
 *
 * CLEAN ROOM: the glTF layout comes from docs/specs/level-decor.md, display-lists.md, texture-packs.md and the exporter
 * that writes it (port/export/level_export.c, export_common.c). Plain host C: compiled in its own target (ge_leveldecor,
 * CMakeLists.txt) without the decomp's shadow headers; LEVELDECOR_PURE_TEST builds only the parser (tests/test_leveldecor.c). */
#include "leveldecor.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LEVELDECOR_PURE_TEST
#include "texpack.h"
#include "system.h"
#include "ge_pkg.h"
#include "mission.h"
#endif

/* ------------------------------------------------------------------------ */
/* parser                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct { LdReadFn rd; LdFreeFn fr; void *user; } LdIo;

static cgltf_result ld_read(const struct cgltf_memory_options *mo, const struct cgltf_file_options *fo, const char *path,
                            cgltf_size *size, void **data)
{
    const LdIo *io = (const LdIo *)fo->user_data;
    uint8_t *buf = NULL;
    size_t n = 0;
    (void)mo;
    if (!io->rd(io->user, path, &buf, &n)) return cgltf_result_file_not_found;
    *size = n;
    *data = buf;
    return cgltf_result_success;
}

static void ld_release(const struct cgltf_memory_options *mo, const struct cgltf_file_options *fo, void *data)
{
    const LdIo *io = (const LdIo *)fo->user_data;
    (void)mo;
    if (data) io->fr(io->user, data);
}

static float lin_to_srgb(float c)
{
    if (c < 0.0f) c = 0.0f;
    if (c > 1.0f) c = 1.0f;
    return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static int wrap_of(cgltf_wrap_mode w) { return w == cgltf_wrap_mode_clamp_to_edge ? 2 : w == cgltf_wrap_mode_mirrored_repeat ? 1 : 0; }

typedef struct {
    LdPrim p;          /* first/count unset */
    int textured;
    float factor[4];   /* baseColorFactor, rgb in the shade's (sRGB) encoding */
} LdMat;

static void mat_of(const cgltf_material *m, LdMat *o)
{
    memset(o, 0, sizeof *o);
    o->p.texnum = -1;
    o->p.ztest = 1; o->p.zupd = 1; o->p.zcmp = 1;
    o->p.cull = 1;
    o->p.linear = 1;
    o->factor[0] = o->factor[1] = o->factor[2] = o->factor[3] = 1.0f;
    if (!m) return;
    o->p.alpha = m->alpha_mode == cgltf_alpha_mode_blend ? 2 : m->alpha_mode == cgltf_alpha_mode_mask ? 1 : 0;
    o->p.cull = m->double_sided ? 0 : 1;
    if (o->p.alpha == 2) o->p.zupd = 0;
    if (m->has_pbr_metallic_roughness) {
        const float *f = m->pbr_metallic_roughness.base_color_factor;
        for (int k = 0; k < 3; k++) o->factor[k] = lin_to_srgb(f[k]);
        o->factor[3] = f[3] < 0.0f ? 0.0f : f[3] > 1.0f ? 1.0f : f[3];
        const cgltf_texture *t = m->pbr_metallic_roughness.base_color_texture.texture;
        if (t) {
            o->textured = 1;
            if (t->sampler) {
                o->p.cms = (uint8_t)wrap_of(t->sampler->wrap_s);
                o->p.cmt = (uint8_t)wrap_of(t->sampler->wrap_t);
                o->p.linear = t->sampler->mag_filter != cgltf_filter_type_nearest;
            }
        }
    }
    if (m->extras.data) {
        cJSON *j = cJSON_Parse(m->extras.data);
        if (j) {
            const cJSON *tn = cJSON_GetObjectItemCaseSensitive(j, "texnum");
            const cJSON *tk = cJSON_GetObjectItemCaseSensitive(j, "texture");
            if (cJSON_IsString(tk) && tk->valuestring) o->p.texnum = atoi(tk->valuestring);
            else if (cJSON_IsNumber(tn)) o->p.texnum = (int)tn->valuedouble;
            const cJSON *n64 = cJSON_GetObjectItemCaseSensitive(j, "n64");
            if (cJSON_IsObject(n64)) {
                const cJSON *om = cJSON_GetObjectItemCaseSensitive(n64, "othermode");
                const cJSON *lo = cJSON_IsArray(om) ? cJSON_GetArrayItem(om, 1) : NULL;
                const cJSON *gm = cJSON_GetObjectItemCaseSensitive(n64, "geometry_mode");
                const cJSON *li = cJSON_GetObjectItemCaseSensitive(n64, "list");
                if (cJSON_IsNumber(lo)) {
                    const uint32_t oml = (uint32_t)lo->valuedouble;
                    o->p.zcmp = (oml & 0x10u) != 0;
                    o->p.zupd = (oml & 0x20u) != 0;
                    o->p.zmode = (uint16_t)(oml & 0xC00u);
                    o->p.fog = (oml >> 30) == 3u || ((oml >> 26) & 3u) == 1u;   /* blender: fog colour, or fog alpha */
                }
                if (cJSON_IsNumber(gm)) {
                    const uint32_t g = (uint32_t)gm->valuedouble;
                    /* the room lists never set the z-buffer bit themselves (inherited from the game's state): always tested */
                    o->p.cull = (g & 0x3000u) == 0x3000u ? 3 : (g & 0x2000u) ? 1 : (g & 0x1000u) ? 2 : 0;
                }
                if (cJSON_IsString(li) && li->valuestring) o->p.list = strcmp(li->valuestring, "secondary") == 0;
            }
            cJSON_Delete(j);
        }
    }
    if (!o->textured) o->p.texnum = -1;
}

static int room_of(const cgltf_node *n)
{
    int r = -1;
    if (n->extras.data) {
        cJSON *j = cJSON_Parse(n->extras.data);
        if (j) {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "room");
            if (cJSON_IsNumber(v)) r = (int)v->valuedouble;
            cJSON_Delete(j);
        }
    }
    if (r < 0 && n->name && strncmp(n->name, "room_", 5) == 0) r = atoi(n->name + 5);
    return r;
}

/* One primitive appended to the room (expanded triangles). 0 ok, -1 out of memory, 1 skipped. */
static int add_prim(LdRoom *rm, const cgltf_primitive *pr, const LdMat *mat, double scale, const float tr[3], size_t *capv,
                    int *capp)
{
    const cgltf_accessor *apos = NULL, *auv = NULL, *acol = NULL;
    if (pr->type != cgltf_primitive_type_triangles) return 1;
    for (cgltf_size ai = 0; ai < pr->attributes_count; ai++) {
        const cgltf_attribute *at = &pr->attributes[ai];
        if (at->index != 0) continue;
        if (at->type == cgltf_attribute_type_position) apos = at->data;
        else if (at->type == cgltf_attribute_type_texcoord) auv = at->data;
        else if (at->type == cgltf_attribute_type_color) acol = at->data;
    }
    if (!apos || apos->count == 0) return 1;
    const size_t n = apos->count;
    const int nc = acol ? (int)cgltf_num_components(acol->type) : 0;
    float *pos = (float *)malloc(n * 3 * sizeof(float));
    float *uv = (float *)calloc(n * 2, sizeof(float));
    float *col = (float *)malloc(n * 4 * sizeof(float) + 16);
    uint32_t *idx = NULL;
    size_t ni = pr->indices ? pr->indices->count : n;
    int rc = -1;
    if (!pos || !uv || !col) goto out;
    if (cgltf_accessor_unpack_floats(apos, pos, n * 3) < n * 3) { rc = 1; goto out; }
    if (mat->textured && auv && auv->count == n && cgltf_accessor_unpack_floats(auv, uv, n * 2) < n * 2) memset(uv, 0, n * 2 * sizeof(float));
    for (size_t i = 0; i < n * 4; i++) col[i] = 1.0f;
    if (acol && acol->count == n && nc >= 3) {
        float *c = (float *)malloc(n * (size_t)nc * sizeof(float));
        if (!c) goto out;
        if (cgltf_accessor_unpack_floats(acol, c, n * (size_t)nc) == n * (size_t)nc)
            for (size_t i = 0; i < n; i++) {
                for (int k = 0; k < 3; k++) col[i * 4 + k] = lin_to_srgb(c[i * nc + k]);
                col[i * 4 + 3] = nc >= 4 ? c[i * nc + 3] : 1.0f;
            }
        free(c);
    }
    idx = (uint32_t *)malloc((ni ? ni : 1) * sizeof(uint32_t));
    if (!idx) goto out;
    if (pr->indices) {
        if (cgltf_accessor_unpack_indices(pr->indices, idx, sizeof(uint32_t), ni) < ni) { rc = 1; goto out; }
    } else {
        for (size_t i = 0; i < n; i++) idx[i] = (uint32_t)i;
    }
    ni -= ni % 3;
    for (size_t i = 0; i < ni; i++)
        if (idx[i] >= n) { rc = 1; goto out; }
    if (ni == 0) { rc = 1; goto out; }

    if (rm->nverts + ni > *capv) {
        size_t nc2 = *capv ? *capv : 1024;
        while (nc2 < rm->nverts + ni) nc2 *= 2;
        float *v = (float *)realloc(rm->v, nc2 * LD_VSTRIDE * sizeof(float));
        if (!v) goto out;
        rm->v = v; *capv = nc2;
    }
    if (rm->nprims + 1 > *capp) {
        int np = *capp ? *capp * 2 : 16;
        LdPrim *p = (LdPrim *)realloc(rm->prims, (size_t)np * sizeof(LdPrim));
        if (!p) goto out;
        rm->prims = p; *capp = np;
    }
    {
        LdPrim *p = &rm->prims[rm->nprims++];
        *p = mat->p;
        p->first = rm->nverts;
        p->count = (uint32_t)ni;
        for (size_t i = 0; i < ni; i++) {
            const uint32_t s = idx[i];
            float *o = &rm->v[(size_t)(rm->nverts + i) * LD_VSTRIDE];
            for (int k = 0; k < 3; k++) {
                o[k] = (float)(pos[s * 3 + k] * scale);
                const float w = pos[s * 3 + k] + tr[k];
                if (w < rm->mn[k]) rm->mn[k] = w;
                if (w > rm->mx[k]) rm->mx[k] = w;
            }
            o[3] = uv[s * 2]; o[4] = uv[s * 2 + 1];
            for (int k = 0; k < 4; k++) o[5 + k] = col[s * 4 + k] * mat->factor[k];
        }
        rm->nverts += (uint32_t)ni;
        rm->ntris[p->list] += (uint32_t)(ni / 3);
    }
    rc = 0;
out:
    free(pos); free(uv); free(col); free(idx);
    return rc;
}

void leveldecorFree(LdSet *s)
{
    if (!s) return;
    for (int r = 0; r < s->nrooms; r++) { free(s->rooms[r].prims); free(s->rooms[r].v); }
    free(s->rooms);
    free(s);
}

LdSet *leveldecorParse(const uint8_t *json, size_t len, const char *path, LdReadFn rd, LdFreeFn fr, void *user, char *err, size_t errsz)
{
    LdIo io = { rd, fr, user };
    cgltf_options opt;
    cgltf_data *data = NULL;
    LdSet *s = NULL;
    int maxroom = 0;
    memset(&opt, 0, sizeof opt);
    opt.file.read = ld_read;
    opt.file.release = ld_release;
    opt.file.user_data = &io;
    if (err && errsz) err[0] = 0;
#define LDFAIL(...) do { if (err && errsz) snprintf(err, errsz, __VA_ARGS__); goto fail; } while (0)
    if (cgltf_parse(&opt, json, len, &data) != cgltf_result_success) LDFAIL("cannot parse %s", path);
    if (cgltf_load_buffers(&opt, data, path) != cgltf_result_success) LDFAIL("cannot read the buffers of %s", path);
    s = (LdSet *)calloc(1, sizeof *s);
    if (!s) LDFAIL("out of memory");
    s->scale = 0;
    if (data->asset.extras.data) {
        cJSON *j = cJSON_Parse(data->asset.extras.data);
        if (j) {
            const cJSON *sc = cJSON_GetObjectItemCaseSensitive(j, "level_scale");
            const cJSON *lv = cJSON_GetObjectItemCaseSensitive(j, "level");
            if (cJSON_IsNumber(sc)) s->scale = sc->valuedouble;
            if (cJSON_IsString(lv) && lv->valuestring) snprintf(s->level, sizeof s->level, "%s", lv->valuestring);
            cJSON_Delete(j);
        }
    }
    if (!(s->scale > 0)) LDFAIL("%s: no asset.extras.level_scale", path);
    for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node *nd = &data->nodes[ni];
        if (nd->name && strncmp(nd->name, "collision", 9) == 0) continue;
        const int r = room_of(nd);
        if (r > 0 && r < 65536 && r + 1 > maxroom) maxroom = r + 1;
    }
    s->nrooms = maxroom;
    s->rooms = (LdRoom *)calloc(maxroom ? (size_t)maxroom : 1, sizeof(LdRoom));
    if (!s->rooms) LDFAIL("out of memory");
    for (int r = 0; r < maxroom; r++)
        for (int k = 0; k < 3; k++) { s->rooms[r].mn[k] = 1e30f; s->rooms[r].mx[k] = -1e30f; }
    for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node *nd = &data->nodes[ni];
        if (nd->name && strncmp(nd->name, "collision", 9) == 0) continue;
        const int r = room_of(nd);
        if (r <= 0 || r >= maxroom) continue;
        LdRoom *rm = &s->rooms[r];
        size_t capv = 0;
        int capp = 0;
        float tr[3] = { 0, 0, 0 };
        rm->present = 1;
        if (nd->has_translation) memcpy(tr, nd->translation, sizeof tr);
        memcpy(rm->tr, tr, sizeof tr);
        if (!nd->mesh) continue;
        /* the primary list's primitives first, then the secondary's (the game's draw order: the lists are drawn apart) */
        for (int pass = 0; pass < 2; pass++)
            for (cgltf_size pi = 0; pi < nd->mesh->primitives_count; pi++) {
                const cgltf_primitive *pr = &nd->mesh->primitives[pi];
                LdMat m;
                mat_of(pr->material, &m);
                if (m.p.list != pass) continue;
                if (add_prim(rm, pr, &m, s->scale, tr, &capv, &capp) < 0) LDFAIL("out of memory");
            }
    }
    cgltf_free(data);
    return s;
fail:
#undef LDFAIL
    if (data) cgltf_free(data);
    leveldecorFree(s);
    return NULL;
}

#ifndef LEVELDECOR_PURE_TEST

/* ------------------------------------------------------------------------ */
/* game thread                                                              */
/* ------------------------------------------------------------------------ */

extern int lvlGetCurrentStageToLoad(void);   /* src/game (s32) */

static LdSet *s_pending;     /* published, not taken yet (exchanged atomically) */
static int s_stage = -1;
static unsigned s_gen = 0;
static int s_on = -1;
static int s_stats = -1;

int leveldecorStatsOn(void)
{
    if (s_stats < 0) { const char *e = getenv("GE_LDSTATS"); s_stats = e && e[0]; }
    return s_stats;
}

static int ld_rd(void *user, const char *path, uint8_t **data, size_t *len)
{
    unsigned char *b = NULL;
    unsigned long n = 0;
    (void)user;
    if (!assetRead(path, &b, &n, NULL)) return 0;
    *data = b; *len = (size_t)n;
    return 1;
}

static void ld_fr(void *user, void *data) { (void)user; pkgFree(data); }

static const char *stage_name(int stage)
{
    for (int i = 0;; i++) {
        const char *n = missionLevelNameAt(i);
        if (!n) return NULL;
        if (missionLevelNumber(n) == stage) return n;
    }
}

static void publish(LdSet *s)
{
    LdSet *old = __atomic_exchange_n(&s_pending, s, __ATOMIC_ACQ_REL);
    leveldecorFree(old);   /* never taken by the renderer */
}

static LdSet *load(int stage)
{
    char rel[128];
    const char *name = stage_name(stage);
    LdSet *s = NULL;
    if (name) {
        unsigned char *json = NULL;
        unsigned long len = 0;
        snprintf(rel, sizeof rel, "levels/%s/%s.gltf", name, name);
        if (assetRead(rel, &json, &len, NULL)) {
            char err[256];
            s = leveldecorParse(json, len, rel, ld_rd, ld_fr, NULL, err, sizeof err);
            pkgFree(json);
            if (!s) sysLogPrintf(LOG_WARNING, "level decor: %s: nothing is drawn", err);
            else {
                int nr = 0;
                unsigned long long t = 0;
                for (int r = 0; r < s->nrooms; r++) { nr += s->rooms[r].present; t += s->rooms[r].ntris[0] + s->rooms[r].ntris[1]; }
                sysLogPrintf(LOG_NOTE, "level decor: %s from the asset packs (%d rooms, %llu triangles, scale %.9g)", rel, nr, t, s->scale);
            }
        } else {
            sysLogPrintf(LOG_WARNING, "level decor: no %s in the asset packs: the level's rooms are not drawn (GE_ROM_LEVELS=1 draws the ROM's)", rel);
        }
    } else {
        sysLogPrintf(LOG_WARNING, "level decor: stage %d has no level name: its rooms are not drawn (GE_ROM_LEVELS=1 draws the ROM's)", stage);
    }
    if (!s) {
        s = (LdSet *)calloc(1, sizeof *s);
        if (!s) return NULL;
        s->missing = 1;
    }
    s->stage = stage;
    return s;
}

int leveldecorPrepare(void)
{
    if (s_on < 0) {
        const char *e = getenv("GE_ROM_LEVELS");
        const char *x = sysArgGetString("--export-level");
        s_on = !(e && e[0] == '1') && !(x && x[0]);
        sysLogPrintf(LOG_NOTE, "level decor: %s", s_on ? "drawn from the asset packs (levels/<level>/<level>.gltf)"
                                                       : "the ROM's room display lists (GE_ROM_LEVELS=1 or --export-level)");
    }
    if (!s_on && !leveldecorStatsOn()) return 0;
    const int stage = lvlGetCurrentStageToLoad();
    const unsigned gen = assetStackGen();
    if (stage != s_stage || gen != s_gen) {
        if (s_stage >= 0 && stage == s_stage) sysLogPrintf(LOG_NOTE, "level decor: the asset packs changed: the rooms are read again");
        s_stage = stage;
        s_gen = gen;
        LdSet *s = load(stage);
        if (s) { s->gen = gen; publish(s); }
    }
    return s_on;
}

LdSet *leveldecorTake(void)
{
    if (!__atomic_load_n(&s_pending, __ATOMIC_ACQUIRE)) return NULL;
    return __atomic_exchange_n(&s_pending, (LdSet *)NULL, __ATOMIC_ACQ_REL);
}

#endif
