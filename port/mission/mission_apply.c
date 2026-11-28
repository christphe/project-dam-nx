/* Applying a mission to a PC-layout stage setup: see mission_apply.h. */
#include "mission_apply.h"
#include "mission_fields.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PD_END     MISSION_PROPDEF_END
#define PD_GUARD   9
#define PD_DOOR    MISSION_PROPDEF_DOOR
#define PD_COLLECT 8
#define PD_KEY     4
#define PD_PROP    3
#define OBJ_PREFIX 144                  /* the ObjectRecord part of a record */
#define PROPFLAG_ASSIGNEDTOCHR 0x4000u
#define FLAGS2_NOLOAD_ALL      0xF0u    /* PROPFLAG2 "do not load" on Agent / Secret / 00 / 007 (1 << (difficulty + 4)) */
#define SINK_COORD 100000.0f            /* a pad here is inside no stan tile: the game does not spawn the guard */

/* conv_d88.c PD_PCB: the PC size of each propDefs record type (0 = unknown). */
static const uint16_t kPcb[49] = {
    [1] = 296, [2] = 8, [3] = 144, [4] = 152, [5] = 144, [6] = 272, [7] = 152, [8] = 160, [9] = 32,
    [10] = 288, [11] = 664, [12] = 144, [13] = 248, [14] = 32, [17] = 144, [18] = 12, [19] = 32,
    [20] = 200, [21] = 152, [22] = 24, [23] = 16, [24] = 4, [25] = 8, [26] = 8, [27] = 8, [28] = 8,
    [29] = 8, [30] = 24, [31] = 4, [32] = 24, [33] = 24, [34] = 12, [35] = 24, [36] = 144, [37] = 48,
    [38] = 32, [39] = 208, [40] = 208, [42] = 144, [43] = 144, [44] = 40, [45] = 248, [46] = 28,
    [47] = 168, [48] = 4,
};
static const uint8_t kIntroSz[10] = { 12, 16, 16, 32, 8, 8, 40, 12, 8, 4 };

unsigned missionPropdefSize(int type)
{
    return (type >= 1 && type <= 48) ? kPcb[type] : 0;
}

/* Guard weapon models by item id, from every shipped setup's guard-assigned collectables. */
int missionGuardWeaponModel(int item)
{
    switch (item) {
    case 4: return 191;  case 6: return 205;  case 7: return 193;  case 8: return 184;
    case 9: return 195;  case 10: return 189; case 12: return 194; case 13: return 188;
    case 14: return 197; case 16: return 207; case 17: return 210; case 18: return 190;
    case 22: return 187; case 24: return 185; case 43: return 198;
    default: return -1;
    }
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wrf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); wr32(p, u); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static void logf_(MissionApplyResult *r, const char *fmt, ...)
{
    size_t used = strlen(r->log);
    va_list ap;
    if (used + 2 >= sizeof r->log) return;
    va_start(ap, fmt);
    vsnprintf(r->log + used, sizeof r->log - used - 2, fmt, ap);
    va_end(ap);
    used = strlen(r->log);
    r->log[used] = '\n';
    r->log[used + 1] = 0;
}

typedef struct Rec { uint32_t off; int type; } Rec;     /* a propDefs record, offset in the INPUT file */

void missionApplyFree(MissionApplyResult *r)
{
    if (r) { free(r->data); r->data = NULL; r->size = 0; }
}

/* Where the record's pad goes: an original number, or a new pad's index. */
static int resolve_pad(const Mission *m, const MissionPadRef *p, int orig_pads)
{
    if (!p->is_new) return p->num;
    for (int i = 0; i < m->npads; i++)
        if (!strcmp(m->pads[i].id, p->id)) return orig_pads + i;
    return -1;
}

/* The value of a door record's pad field for a pad ref: the bound-pad index (pad - 10000), or -1 when the ref is not a bound pad. */
static int door_pad(const MissionPadRef *p)
{
    return (!p->is_new && p->num >= 10000) ? p->num - 10000 : -1;
}

/* The fields an add op names, written over the clone (absent ones keep the template's bytes). */
static void apply_fields(uint8_t *rec, int type, const MissionFieldVals *fv)
{
    const MissionField *f;
    int n = missionRecFields(type, &f);
    for (int i = 0; i < n && i < MISSION_MAX_FIELDS; i++)
        if (fv->set & ((uint64_t)1 << i)) missionFieldSet(&f[i], rec, fv->v[i]);
}

/* A copy of a record kept to be cloned from. */
static uint8_t *clone_rec(const uint8_t *file, const Rec *r)
{
    uint8_t *c = malloc(kPcb[r->type]);
    if (c) memcpy(c, file + r->off, kPcb[r->type]);
    return c;
}

/* the do-not-load bits (flags2) of the difficulties a mask leaves out */
static uint32_t diff_noload_bits(int mask)
{
    uint32_t b = 0;
    if (!mask) return 0;
    for (int d = 0; d < 4; d++) if (!(mask & (1 << d))) b |= 0x10u << d;
    return b;
}

static int diff_excluded(int mask, int difficulty)
{
    return mask && difficulty >= 0 && difficulty <= 3 && !(mask & (1 << difficulty));
}

static int has_pad(const MissionPadRef *p) { return p->is_new || p->num >= 0; }

int missionApplySetup(const Mission *m, const uint8_t *in, uint32_t insize, int spawn_pad, float room_scale,
                      MissionApplyResult *out, char *err, size_t errlen)
{
    return missionApplySetupEx(m, in, insize, spawn_pad, room_scale, -1, out, err, errlen);
}

int missionApplySetupEx(const Mission *m_, const uint8_t *in, uint32_t insize, int spawn_pad, float room_scale, int difficulty,
                        MissionApplyResult *out, char *err, size_t errlen)
{
    Mission *m = (Mission *)m_;   /* the added ops' setup_prop is written back (see mission.h); the caller owns a mutable Mission */
    uint32_t H[10];
    Rec *recs = NULL;
    int nrec = 0, caprec = 0, endrec = -1;
    int rc = 0;
    uint32_t pads_off, padn = 0, stream_off, new_pads_off, new_stream_off, pad_bytes, total;
    uint8_t *buf = NULL, *stream = NULL;
    uint32_t stream_len = 0, stream_cap = 0;
    int sink_idx = -1, total_new_pads;
    int max_chr = -1;
    const Rec *guard_tpl = NULL, *prop_tpl[49] = { 0 }, *assigned_tpl = NULL;
    uint8_t **extra = NULL; int nextra = 0;      /* records appended before END */
    uint8_t *newpads = NULL;
    int has_remove_guard = 0;
    (void)errlen;

    memset(out, 0, sizeof *out);
    for (int i = 0; i < m->nguards; i++) m->guards[i].setup_prop = -1;
    for (int i = 0; i < m->nobjects; i++) m->objects[i].setup_prop = -1;
    if (room_scale <= 0.0f) room_scale = 1.0f;
    if (insize < MISSION_SETUP_HDR_SIZE) { snprintf(err, errlen, "setup too small (%u)", insize); return 0; }
    for (int i = 0; i < 10; i++) {
        H[i] = rd32(in + 8 * i);
        if (H[i] >= insize) { snprintf(err, errlen, "setup header field %d offset %#x past the file (%#x)", i, H[i], insize); return 0; }
    }
    pads_off = H[6];
    stream_off = H[3];
    if (!pads_off || !stream_off) { snprintf(err, errlen, "setup has no pads or no propDefs"); return 0; }

    /* the pads table: records until a null plink */
    for (uint32_t o = pads_off; ; o += MISSION_SETUP_PAD_SIZE, padn++) {
        if (o + MISSION_SETUP_PAD_SIZE > insize) { snprintf(err, errlen, "pads table runs past the file"); return 0; }
        if (rd32(in + o + 0x28) == 0) break;
        if (padn > 20000) { snprintf(err, errlen, "pads table runaway"); return 0; }
    }

    /* the propDefs stream */
    for (uint32_t o = stream_off; ; ) {
        int t;
        if (o + 4 > insize) { snprintf(err, errlen, "propDefs run past the file"); goto done; }
        t = in[o + 3];
        if (t < 1 || t > 48 || !kPcb[t] || o + kPcb[t] > insize) { snprintf(err, errlen, "propDefs: bad record type %d at %#x", t, o); goto done; }
        if (nrec == caprec) {
            caprec = caprec ? caprec * 2 : 512;
            recs = realloc(recs, (size_t)caprec * sizeof *recs);
            if (!recs) { snprintf(err, errlen, "out of memory"); goto done; }
        }
        recs[nrec].off = o; recs[nrec].type = t;
        if (t == PD_END) { endrec = nrec++; break; }
        nrec++;
        o += kPcb[t];
    }

    /* templates and the highest chrnum */
    for (int i = 0; i < nrec; i++) {
        const Rec *r = &recs[i];
        const uint8_t *p = in + r->off;
        if (r->type == PD_GUARD) {
            int cn = rd16(p + 4);
            if (!guard_tpl) guard_tpl = r;
            if (cn > max_chr) max_chr = cn;
        } else if (r->type == PD_COLLECT) {
            if (rd32(p + 8) & PROPFLAG_ASSIGNEDTOCHR) { if (!assigned_tpl) assigned_tpl = r; }
            else if (!prop_tpl[r->type]) prop_tpl[r->type] = r;
        } else if (!prop_tpl[r->type]) {
            prop_tpl[r->type] = r;
        }
    }

    for (int i = 0; i < m->nguards; i++)
        if (m->guards[i].op == MISSION_OP_REMOVE || (m->guards[i].op == MISSION_OP_MOVE && diff_excluded(m->guards[i].diff_mask, difficulty))) has_remove_guard = 1;
    total_new_pads = m->npads + (has_remove_guard ? 1 : 0);
    if (has_remove_guard) sink_idx = (int)padn + m->npads;

    /* new pads: a plink hint from the nearest original pad (the game looks the stan tile up by position when
     * the named tile does not hold the point), a position, an up vector and a look direction. */
    pad_bytes = ((uint32_t)padn + (uint32_t)total_new_pads + 1) * MISSION_SETUP_PAD_SIZE;
    newpads = calloc(1, pad_bytes);
    if (!newpads) { snprintf(err, errlen, "out of memory"); goto done; }
    memcpy(newpads, in + pads_off, (size_t)padn * MISSION_SETUP_PAD_SIZE);
    for (int k = 0; k < total_new_pads; k++) {
        uint8_t *d = newpads + ((size_t)padn + k) * MISSION_SETUP_PAD_SIZE;
        float pos[3] = { SINK_COORD, 0.0f, SINK_COORD }, look[3] = { 0, 0, 1 }, len;
        uint32_t plink = 0;
        float best = 3.4e38f;
        if (k < m->npads) {
            for (int a = 0; a < 3; a++) pos[a] = m->pads[k].pos[a] / room_scale;
            for (int a = 0; a < 3; a++) look[a] = m->pads[k].look[a];
        }
        len = sqrtf(look[0] * look[0] + look[1] * look[1] + look[2] * look[2]);
        if (len > 1e-4f) { for (int a = 0; a < 3; a++) look[a] /= len; } else { look[0] = 0; look[1] = 0; look[2] = 1; }
        for (uint32_t i = 0; i < padn; i++) {
            const uint8_t *s = in + pads_off + (size_t)i * MISSION_SETUP_PAD_SIZE;
            float dx = rdf(s) - pos[0], dy = rdf(s + 4) - pos[1], dz = rdf(s + 8) - pos[2], dd = dx * dx + dy * dy + dz * dz;
            if (dd < best) { best = dd; plink = rd32(s + 0x28); }
        }
        if (!plink && padn) plink = rd32(in + pads_off + 0x28);
        for (int a = 0; a < 3; a++) wrf(d + 4 * a, pos[a]);
        wrf(d + 12, 0.0f); wrf(d + 16, 1.0f); wrf(d + 20, 0.0f);            /* up */
        for (int a = 0; a < 3; a++) wrf(d + 24 + 4 * a, look[a]);
        wr32(d + 0x28, plink);
        if (k < m->npads)
            logf_(out, "pad %s: new pad %d at (%.0f, %.0f, %.0f)", m->pads[k].id, (int)padn + k, m->pads[k].pos[0], m->pads[k].pos[1], m->pads[k].pos[2]);
        else
            logf_(out, "pad %d: removal sink (outside the map)", (int)padn + k);
    }
    out->pads_added = m->npads;

    /* the edited copy of the stream: records in file order, then the additions, then END */
    stream_cap = 0;
    for (int i = 0; i < nrec; i++) stream_cap += kPcb[recs[i].type];
    stream_cap += (uint32_t)(m->nguards + m->nobjects) * 2u * 296u + 64u;
    stream = malloc(stream_cap);
    if (!stream) { snprintf(err, errlen, "out of memory"); goto done; }
    for (int i = 0; i < endrec; i++) {
        memcpy(stream + stream_len, in + recs[i].off, kPcb[recs[i].type]);
        stream_len += kPcb[recs[i].type];
    }

    /* record i lives at stream + start[i]; compute lazily by walking */
#define REC_AT(idx, outp) do { uint32_t q_ = 0; for (int j_ = 0; j_ < (idx); j_++) q_ += kPcb[recs[j_].type]; (outp) = stream + q_; } while (0)

    /* guards */
    for (int i = 0; i < m->nguards; i++) {
        const MissionGuardOp *g = &m->guards[i];
        if (g->op == MISSION_OP_ADD) {
            uint8_t *c;
            int pad = resolve_pad(m, &g->pad, (int)padn), chrnum;
            if (diff_excluded(g->diff_mask, difficulty)) { logf_(out, "guard add: \"%s\" %s not present at difficulty %d, not added (setup prop -1)", g->name, g->uuid, difficulty); continue; }
            if (!guard_tpl) { logf_(out, "guard add: skipped, the level has no guard to clone"); out->skipped++; continue; }
            if (pad < 0) { logf_(out, "guard add: skipped, unknown pad"); out->skipped++; continue; }
            if (pad >= 10000) { logf_(out, "guard add: skipped, pad %d is a bound pad", pad); out->skipped++; continue; }
            c = clone_rec(in, guard_tpl);
            if (!c) { out->skipped++; continue; }
            apply_fields(c, PD_GUARD, &g->f);
            chrnum = ++max_chr;                 /* never copied: every added chr is new */
            wr16(c + 4, (unsigned)chrnum);
            wr16(c + 6, (unsigned)pad);
            if (g->model >= 0) wr16(c + 8, (unsigned)g->model);
            if (g->ai >= 0) wr16(c + 10, (unsigned)g->ai);
            extra = realloc(extra, (size_t)(nextra + 2) * sizeof *extra);
            m->guards[i].setup_prop = endrec + nextra;
            extra[nextra++] = c;
            for (int wi = 0; wi < g->nweapons && wi < MISSION_MAX_WEAPONS; wi++) {
                const MissionWeapon *wp = &g->weapons[wi];
                if (!assigned_tpl) {
                    logf_(out, "guard add: chr %d has no weapon, the level has no weapon to clone", chrnum);
                    break;
                } else {
                    uint8_t *w = clone_rec(in, assigned_tpl);
                    const Rec *best = NULL;
                    int model = wp->model >= 0 ? wp->model : missionGuardWeaponModel(wp->item);
                    for (int k = 0; k < nrec && !best && wp->item >= 0; k++)         /* a record of the same weapon wins */
                        if (recs[k].type == PD_COLLECT && (rd32(in + recs[k].off + 8) & PROPFLAG_ASSIGNEDTOCHR) && in[recs[k].off + OBJ_PREFIX] == (uint8_t)wp->item) best = &recs[k];
                    if (best) { free(w); w = clone_rec(in, best); }
                    if (!best && model >= 0) wr16(w + 4, (unsigned)model);
                    if (wp->model >= 0) wr16(w + 4, (unsigned)wp->model);
                    if (wp->item >= 0) w[OBJ_PREFIX] = (uint8_t)wp->item;
                    apply_fields(w, PD_COLLECT, &wp->f);
                    wr16(w + 6, (unsigned)chrnum);                          /* the carrier's chr number, not a pad */
                    wr32(w + 8, rd32(w + 8) | PROPFLAG_ASSIGNEDTOCHR);
                    extra = realloc(extra, (size_t)(nextra + 2) * sizeof *extra);
                    extra[nextra++] = w;
                    logf_(out, "guard add: chr %d carries weapon %d (model %d)", chrnum, wp->item, rd16(w + 4));
                    if (!best && model < 0 && wp->item >= 0) logf_(out, "guard add: weapon %d has no known model, shows as the template's", wp->item);
                }
            }
            logf_(out, "guard add: \"%s\" %s -> setup prop %d (chr %d at pad %d, body %d, ai %#x, weapon %d)", g->name, g->uuid, g->setup_prop, chrnum, pad, g->model, g->ai, g->nweapons ? g->weapons[0].item : -1);
            out->guards_added++;
        } else {
            uint8_t *p;
            int idx = g->prop;
            if (idx < 0 || idx >= endrec || recs[idx].type != PD_GUARD) {
                logf_(out, "guard %s: skipped, prop %d is not a guard", g->op == MISSION_OP_MOVE ? "move" : "remove", idx);
                out->skipped++;
                continue;
            }
            REC_AT(idx, p);
            if (g->op == MISSION_OP_MOVE && diff_excluded(g->diff_mask, difficulty)) {   /* absent at this difficulty: as "remove", weapons too */
                int chr = rd16(p + 4);
                wr16(p + 6, (unsigned)sink_idx);
                for (int k = 0; k < endrec; k++) {
                    uint8_t *wq;
                    if (recs[k].type != PD_COLLECT) continue;
                    REC_AT(k, wq);
                    if ((rd32(wq + 8) & PROPFLAG_ASSIGNEDTOCHR) && rd16(wq + 6) == chr) wr32(wq + 12, rd32(wq + 12) | FLAGS2_NOLOAD_ALL);
                }
                logf_(out, "guard move: prop %d (chr %d) not present at difficulty %d: sunk with its weapons", idx, chr, difficulty);
                out->guards_removed++;
                continue;
            }
            if (g->op == MISSION_OP_MOVE) {
                int pad = has_pad(&g->pad) ? resolve_pad(m, &g->pad, (int)padn) : -2;
                if (pad == -2) { logf_(out, "guard edit: prop %d (chr %d)%s", idx, rd16(p + 4), g->ai >= 0 ? ", new AI list" : ""); if (g->ai >= 0) wr16(p + 10, (unsigned)g->ai); out->guards_moved++; continue; }
                if (pad < 0 || pad >= 10000) { logf_(out, "guard move: skipped, bad pad for prop %d", idx); out->skipped++; continue; }
                wr16(p + 6, (unsigned)pad);
                if (g->ai >= 0) wr16(p + 10, (unsigned)g->ai);     /* a move may also carry a new AI list (the pad stays when it names the same one) */
                logf_(out, "guard move: prop %d (chr %d) to pad %d%s", idx, rd16(p + 4), pad, g->ai >= 0 ? ", new AI list" : "");
                out->guards_moved++;
            } else {
                wr16(p + 6, (unsigned)sink_idx);
                logf_(out, "guard remove: prop %d (chr %d)", idx, rd16(p + 4));
                out->guards_removed++;
            }
        }
    }

    /* objects */
    for (int i = 0; i < m->nobjects; i++) {
        const MissionObjectOp *o = &m->objects[i];
        if (o->op == MISSION_OP_ADD) {
            const Rec *tpl;
            uint8_t *c;
            int pad = resolve_pad(m, &o->pad, (int)padn);
            if (o->type < 1 || o->type > 47 || o->type == PD_GUARD || kPcb[o->type] < OBJ_PREFIX) {
                logf_(out, "object add: skipped, type %d is not an object record", o->type); out->skipped++; continue;
            }
            tpl = prop_tpl[o->type];
            if (!tpl) { logf_(out, "object add: skipped, the level has no record of type %d to clone", o->type); out->skipped++; continue; }
            if (pad < 0) { logf_(out, "object add: skipped, unknown pad"); out->skipped++; continue; }
            if (o->type == PD_DOOR) {
                pad = door_pad(&o->pad);
                if (pad < 0) { logf_(out, "object add: skipped, %s", MISSION_DOOR_PAD_MSG); out->skipped++; continue; }
            }
            c = clone_rec(in, tpl);
            if (!c) { out->skipped++; continue; }
            wr16(c + 6, (unsigned)pad);
            if (o->model >= 0) wr16(c + 4, (unsigned)o->model);
            if (o->type == PD_COLLECT && o->item >= 0) c[OBJ_PREFIX] = (uint8_t)o->item;
            if (o->type == PD_KEY && o->item >= 0) wr32(c + OBJ_PREFIX, (uint32_t)o->item);
            apply_fields(c, o->type, &o->f);        /* after the op's own keys: an explicit field wins */
            wr32(c + 8, rd32(c + 8) & ~PROPFLAG_ASSIGNEDTOCHR);
            if (o->diff_mask) wr32(c + 12, (rd32(c + 12) & ~FLAGS2_NOLOAD_ALL) | diff_noload_bits(o->diff_mask));
            extra = realloc(extra, (size_t)(nextra + 2) * sizeof *extra);
            m->objects[i].setup_prop = endrec + nextra;     /* the END record's index is the first added record's number */
            extra[nextra++] = c;
            logf_(out, "object add: \"%s\" %s -> setup prop %d (type %d model %d item %d at pad %d)", o->name, o->uuid, o->setup_prop, o->type, o->model, o->item, pad);
            out->objects_added++;
        } else {
            uint8_t *p;
            int idx = o->prop;
            if (idx < 0 || idx >= endrec || recs[idx].type == PD_GUARD || kPcb[recs[idx].type] < OBJ_PREFIX) {
                logf_(out, "object %s: skipped, prop %d is not an object", o->op == MISSION_OP_MOVE ? "move" : "remove", idx);
                out->skipped++;
                continue;
            }
            REC_AT(idx, p);
            if (o->op == MISSION_OP_MOVE) {
                int pad = has_pad(&o->pad) ? resolve_pad(m, &o->pad, (int)padn) : -2;
                if (o->diff_mask) wr32(p + 12, (rd32(p + 12) & ~FLAGS2_NOLOAD_ALL) | diff_noload_bits(o->diff_mask));
                if (pad == -2) { logf_(out, "object edit: prop %d (type %d), presence mask %d", idx, recs[idx].type, o->diff_mask); out->objects_moved++; continue; }
                if (pad < 0) { logf_(out, "object move: skipped, bad pad for prop %d", idx); out->skipped++; continue; }
                if (recs[idx].type == PD_DOOR) {
                    pad = door_pad(&o->pad);
                    if (pad < 0) { logf_(out, "object move: skipped, prop %d: %s", idx, MISSION_DOOR_PAD_MSG); out->skipped++; continue; }
                }
                wr16(p + 6, (unsigned)pad);
                logf_(out, "object move: prop %d (type %d) to pad %d", idx, recs[idx].type, pad);
                out->objects_moved++;
            } else {
                wr32(p + 12, rd32(p + 12) | FLAGS2_NOLOAD_ALL);
                logf_(out, "object remove: prop %d (type %d) not loaded", idx, recs[idx].type);
                out->objects_removed++;
            }
        }
    }

    /* append the additions and END */
    for (int k = 0; k < nextra; k++) {
        /* the type is in the record's own byte 3 */
        uint32_t sz = kPcb[extra[k][3]];
        if (stream_len + sz + 4 > stream_cap) { stream_cap = stream_len + sz + 4 + 1024; stream = realloc(stream, stream_cap); if (!stream) goto done; }
        memcpy(stream + stream_len, extra[k], sz);
        stream_len += sz;
    }
    memcpy(stream + stream_len, in + recs[endrec].off, kPcb[PD_END]);
    stream_len += kPcb[PD_END];

    /* assemble: original, pads, stream (each 8-aligned) */
    new_pads_off = (insize + 7u) & ~7u;
    new_stream_off = (new_pads_off + pad_bytes + 7u) & ~7u;
    total = new_stream_off + stream_len;
    buf = calloc(1, total + 16);
    if (!buf) { snprintf(err, errlen, "out of memory"); goto done; }
    memcpy(buf, in, insize);
    memcpy(buf + new_pads_off, newpads, pad_bytes);
    memcpy(buf + new_stream_off, stream, stream_len);
    wr32(buf + 8 * 6, new_pads_off);
    wr32(buf + 8 * 3, new_stream_off);

    if (spawn_pad <= -2) {   /* a mission pad: it was appended after the level's own pads */
        const int k = -2 - spawn_pad;
        spawn_pad = k < m->npads ? (int)padn + k : -1;
        if (spawn_pad < 0) logf_(out, "spawn: mission pad #%d does not exist, the level's start is kept", k);
    }
    if (spawn_pad >= 0 && H[2]) {
        uint32_t o = H[2];
        int done_ = 0;
        for (int g = 0; g < 5000 && o + 4 <= insize; g++) {
            uint32_t t = rd32(in + o);
            if (t > 9) break;
            if (t == 0) { wr32(buf + o + 4, (uint32_t)spawn_pad); logf_(out, "spawn: pad %d", spawn_pad); done_ = 1; break; }
            if (t == 9) break;
            o += kIntroSz[t];
        }
        if (!done_) { logf_(out, "spawn: no spawn record, pad %d ignored", spawn_pad); out->skipped++; }
    }

    out->data = buf;
    out->size = total;
    buf = NULL;
    rc = 1;
done:
    free(buf); free(stream); free(newpads); free(recs);
    for (int k = 0; k < nextra; k++) free(extra[k]);
    free(extra);
    return rc;
}
