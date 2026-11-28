/*
 * Bank -> SoundFont 2.04 file.
 *
 * Layout: one SF2 sample per distinct wave table, one SF2 instrument and one
 * preset (bank 0, number = bank instrument index) per bank instrument, one
 * instrument zone per sound. The mapping of the bank's envelope / volume / pan
 * to SF2 generators is an approximation, all of it in ge_ae_map_sound() below
 * under the tuning constants (squared levels; the instrument volume / pan / bend
 * range travel in the MIDI files).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ge_audio_internal.h"

/* ======================== TUNING: envelope / volume / pan mapping =========== */
/* Extra attenuation added to every zone (centibels, 0.1 dB units). */
#define GE_AE_BASE_ATTENUATION_CB 0
/* Attenuation for a gain of 0 and the SF2 limit of initialAttenuation / sustain. */
#define GE_AE_MAX_ATTENUATION_CB 1440
/* Envelope times shorter than this (microseconds) become the SF2 minimum. */
#define GE_AE_MIN_TIME_US 1000
#define GE_AE_MIN_TIMECENTS (-12000)
#define GE_AE_MAX_TIMECENTS 8000
/* 1: the bank's decay time is the time of the whole attack-volume -> decay-volume
 * drop, so scale it to SF2's "time for a 100 dB drop" semantics. 0: use as is. */
#define GE_AE_DECAY_SCALE_TO_100DB 0
/* Scale applied to the bank's attack / decay / release times. */
#define GE_AE_ATTACK_TIME_SCALE 1.0
#define GE_AE_DECAY_TIME_SCALE 1.0
#define GE_AE_RELEASE_TIME_SCALE 1.0
/* Pan: 0..127 with 64 = centre. Instrument and sound offsets from the centre add. */
#define GE_AE_PAN_CENTRE 64
#define GE_AE_PAN_MAX_TENTHS_PERCENT 500
/* ============================================================================= */

typedef struct {
    int attenuation_cb; /* initialAttenuation */
    int pan_tenths;     /* pan, -500..500 */
    int attack_tc;      /* timecents */
    int decay_tc;
    int sustain_cb; /* attenuation below the peak at the end of decay */
    int release_tc;
} zone_map;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int gain_to_cb(double g)
{
    if (g <= 0.0)
        return GE_AE_MAX_ATTENUATION_CB;
    return clampi((int)lrint(-200.0 * log10(g)), 0, GE_AE_MAX_ATTENUATION_CB);
}

static int us_to_timecents(double us)
{
    if (us < GE_AE_MIN_TIME_US)
        return GE_AE_MIN_TIMECENTS;
    return clampi((int)lrint(1200.0 * log2(us / 1e6)), GE_AE_MIN_TIMECENTS, GE_AE_MAX_TIMECENTS);
}

/* The single place that turns the bank's per-sound parameters into SF2 values. The N64 synth SQUARES every level factor
 * (docs/specs/audio.md "Voice level", "Exporter consequence"): the sound's sample volume and attack volume enter the zone's
 * attenuation as 40 x log10(ratio) dB, the sustain level below the attack peak is 40 x log10(decay vol / attack vol) dB.
 * The instrument's own volume and pan are NOT in the zone: the MIDI carries them as CC7 and CC10 after each program change
 * (a player applies the same laws to those). Velocity is applied by the synth. The decay time is the length of the
 * straight-line segment from the peak to the sustain level (the player's linear amplitude envelope; a standard SF2 synth
 * reads it as the time of a longer exponential fall: only the shape differs). */
static void ge_ae_map_sound(const ge_ae_inst *in, const ge_ae_sound *s, zone_map *m)
{
    double peak = s->attack_vol / 127.0;
    double level = (s->vol / 127.0) * (s->vol / 127.0) * peak * peak;
    double sustain_ratio = s->attack_vol ? (double)s->decay_vol / (double)s->attack_vol : 1.0;
    int pan = s->pan - GE_AE_PAN_CENTRE;
    double decay_us = s->decay_us * GE_AE_DECAY_TIME_SCALE;

    (void)in;
    m->attenuation_cb = clampi(gain_to_cb(level) + GE_AE_BASE_ATTENUATION_CB, 0, GE_AE_MAX_ATTENUATION_CB);
    m->sustain_cb = gain_to_cb(sustain_ratio > 1.0 ? 1.0 : sustain_ratio * sustain_ratio);
#if GE_AE_DECAY_SCALE_TO_100DB
    if (m->sustain_cb > 0)
        decay_us = decay_us * 1000.0 / m->sustain_cb;
#endif
    m->attack_tc = us_to_timecents(s->attack_us * GE_AE_ATTACK_TIME_SCALE);
    m->decay_tc = us_to_timecents(decay_us);
    m->release_tc = us_to_timecents(s->release_us * GE_AE_RELEASE_TIME_SCALE);
    m->pan_tenths = clampi(pan * GE_AE_PAN_MAX_TENTHS_PERCENT / GE_AE_PAN_CENTRE, -GE_AE_PAN_MAX_TENTHS_PERCENT,
                           GE_AE_PAN_MAX_TENTHS_PERCENT);
}

/* ---- SF2 generators ---------------------------------------------------------- */
enum {
    GEN_PAN = 17,
    GEN_ATTACK = 34,
    GEN_DECAY = 36,
    GEN_SUSTAIN = 37,
    GEN_RELEASE = 38,
    GEN_INSTRUMENT = 41,
    GEN_KEYRANGE = 43,
    GEN_VELRANGE = 44,
    GEN_ATTENUATION = 48,
    GEN_COARSETUNE = 51,
    GEN_FINETUNE = 52,
    GEN_SAMPLEID = 53,
    GEN_SAMPLEMODES = 54,
    GEN_ROOTKEY = 58
};

typedef struct {
    uint16_t oper, amount;
} gen_t;

#define MAX_ZONE_GENS 16

/* Generator order per the spec: keyRange, velRange first; sampleID last. */
static int zone_gens(const ge_ae_bank *b, const ge_ae_inst *in, const ge_ae_sound *s, int sid, gen_t *g)
{
    zone_map m;
    int n = 0, cents = s->detune, coarse = cents / 100, fine = cents % 100;
    const ge_ae_wave *w = &b->waves[s->wave];

    ge_ae_map_sound(in, s, &m);
    g[n].oper = GEN_KEYRANGE;
    g[n++].amount = (uint16_t)(s->key_min | (s->key_max << 8));
    if (s->vel_min != 0 || s->vel_max != 127) {
        g[n].oper = GEN_VELRANGE;
        g[n++].amount = (uint16_t)(s->vel_min | (s->vel_max << 8));
    }
    g[n].oper = GEN_ATTENUATION;
    g[n++].amount = (uint16_t)m.attenuation_cb;
    g[n].oper = GEN_PAN;
    g[n++].amount = (uint16_t)(int16_t)m.pan_tenths;
    g[n].oper = GEN_ATTACK;
    g[n++].amount = (uint16_t)(int16_t)m.attack_tc;
    g[n].oper = GEN_DECAY;
    g[n++].amount = (uint16_t)(int16_t)m.decay_tc;
    g[n].oper = GEN_SUSTAIN;
    g[n++].amount = (uint16_t)m.sustain_cb;
    g[n].oper = GEN_RELEASE;
    g[n++].amount = (uint16_t)(int16_t)m.release_tc;
    if (coarse) {
        g[n].oper = GEN_COARSETUNE;
        g[n++].amount = (uint16_t)(int16_t)coarse;
    }
    g[n].oper = GEN_FINETUNE;
    g[n++].amount = (uint16_t)(int16_t)fine;
    g[n].oper = GEN_ROOTKEY;
    g[n++].amount = s->key_base;
    if (w->has_loop) {
        g[n].oper = GEN_SAMPLEMODES;
        g[n++].amount = 1; /* loop continuously */
    }
    g[n].oper = GEN_SAMPLEID;
    g[n++].amount = (uint16_t)sid;
    return n;
}

/* ---- RIFF helpers ---------------------------------------------------------------- */
static int begin_chunk(ge_ae_buf *o, const char *id, size_t *lenpos)
{
    if (ge_buf_put(o, id, 4) || ge_buf_le32(o, 0))
        return -1;
    *lenpos = o->size - 4;
    return 0;
}

static int end_chunk(ge_ae_buf *o, size_t lenpos)
{
    size_t len = o->size - lenpos - 4;
    ge_buf_patch_le32(o, lenpos, (uint32_t)len);
    return (len & 1) ? ge_buf_u8(o, 0) : 0;
}

static int put_name(ge_ae_buf *o, const char *s)
{
    char name[20];
    size_t n = strlen(s);
    memset(name, 0, sizeof name);
    memcpy(name, s, n < 19 ? n : 19);
    return ge_buf_put(o, name, 20);
}

static int put_zstr_chunk(ge_ae_buf *o, const char *id, const char *text)
{
    size_t lp;
    if (begin_chunk(o, id, &lp) || ge_buf_put(o, text, strlen(text) + 1))
        return -1;
    return end_chunk(o, lp);
}

/* ---- the file --------------------------------------------------------------------- */
int ge_ae_bank_to_sf2(const ge_ae_bank *b, const char *bank_name, ge_ae_buf *out, char *err)
{
    int *sid = NULL; /* wave index -> SF2 sample index, -1 for waves not written */
    uint32_t *smpl_start = NULL;
    int nsamples = 0, i, k, rc = -1;
    uint32_t total_zones = 0, total_gens = 0;
    size_t riff, list, ck, tot;
    gen_t g[MAX_ZONE_GENS];
    char nm[32];
    uint32_t pos;

    out->data = NULL;
    out->size = out->cap = 0;
    sid = (int *)malloc((size_t)(b->nwaves ? b->nwaves : 1) * sizeof *sid);
    smpl_start = (uint32_t *)malloc((size_t)(b->nwaves ? b->nwaves : 1) * sizeof *smpl_start);
    if (!sid || !smpl_start) {
        ge_seterr(err, "out of memory");
        goto done;
    }
    /* Only waves referenced by a valid sound are written. */
    for (i = 0; i < b->nwaves; i++)
        sid[i] = -1;
    for (i = 0; i < b->ninst; i++)
        for (k = 0; k < b->inst[i].nsounds; k++)
            if (b->inst[i].sounds[k].valid) {
                int w = b->inst[i].sounds[k].wave;
                if (sid[w] < 0)
                    sid[w] = nsamples++;
                total_zones++;
            }
    /* sample start offsets (in sample points) follow SF2 sample order */
    pos = 0;
    for (k = 0; k < nsamples; k++) {
        for (i = 0; i < b->nwaves; i++)
            if (sid[i] == k) {
                smpl_start[i] = pos;
                pos += b->waves[i].nsamples + 46; /* 46 zero points after each sample (SF2 2.04 §6.1) */
                break;
            }
    }

    if (begin_chunk(out, "RIFF", &riff) || ge_buf_put(out, "sfbk", 4))
        goto oom;

    /* INFO */
    if (begin_chunk(out, "LIST", &list) || ge_buf_put(out, "INFO", 4))
        goto oom;
    if (begin_chunk(out, "ifil", &ck) || ge_buf_le16(out, 2) || ge_buf_le16(out, 4) || end_chunk(out, ck))
        goto oom;
    if (put_zstr_chunk(out, "isng", "EMU8000") || put_zstr_chunk(out, "INAM", bank_name) ||
        put_zstr_chunk(out, "ICMT", "Generated by ge007-audio-export from a user-supplied ROM"))
        goto oom;
    if (end_chunk(out, list))
        goto oom;

    /* sdta */
    if (begin_chunk(out, "LIST", &list) || ge_buf_put(out, "sdta", 4) || begin_chunk(out, "smpl", &ck))
        goto oom;
    for (k = 0; k < nsamples; k++) {
        for (i = 0; i < b->nwaves; i++)
            if (sid[i] == k) {
                uint32_t j;
                for (j = 0; j < b->waves[i].nsamples; j++)
                    if (ge_buf_le16(out, (uint16_t)b->waves[i].pcm[j]))
                        goto oom;
                if (ge_buf_fill(out, 0, 46 * 2))
                    goto oom;
                break;
            }
    }
    if (end_chunk(out, ck) || end_chunk(out, list))
        goto oom;

    /* pdta */
    if (begin_chunk(out, "LIST", &list) || ge_buf_put(out, "pdta", 4))
        goto oom;

    if (begin_chunk(out, "phdr", &ck))
        goto oom;
    for (i = 0; i < b->ninst; i++) {
        snprintf(nm, sizeof nm, "inst %d", i);
        if (put_name(out, nm) || ge_buf_le16(out, (uint32_t)i) || ge_buf_le16(out, 0) || ge_buf_le16(out, (uint32_t)i) ||
            ge_buf_fill(out, 0, 12))
            goto oom;
    }
    if (put_name(out, "EOP") || ge_buf_le16(out, 0) || ge_buf_le16(out, 0) || ge_buf_le16(out, (uint32_t)b->ninst) ||
        ge_buf_fill(out, 0, 12) || end_chunk(out, ck))
        goto oom;

    if (begin_chunk(out, "pbag", &ck))
        goto oom;
    for (i = 0; i <= b->ninst; i++)
        if (ge_buf_le16(out, (uint32_t)i) || ge_buf_le16(out, 0))
            goto oom;
    if (end_chunk(out, ck))
        goto oom;

    if (begin_chunk(out, "pmod", &ck) || ge_buf_fill(out, 0, 10) || end_chunk(out, ck))
        goto oom;

    if (begin_chunk(out, "pgen", &ck))
        goto oom;
    for (i = 0; i < b->ninst; i++)
        if (ge_buf_le16(out, GEN_INSTRUMENT) || ge_buf_le16(out, (uint32_t)i))
            goto oom;
    if (ge_buf_le16(out, 0) || ge_buf_le16(out, 0) || end_chunk(out, ck))
        goto oom;

    if (begin_chunk(out, "inst", &ck))
        goto oom;
    {
        uint32_t zone = 0;
        for (i = 0; i < b->ninst; i++) {
            snprintf(nm, sizeof nm, "inst %d", i);
            if (put_name(out, nm) || ge_buf_le16(out, zone))
                goto oom;
            for (k = 0; k < b->inst[i].nsounds; k++)
                zone += b->inst[i].sounds[k].valid ? 1 : 0;
        }
        if (put_name(out, "EOI") || ge_buf_le16(out, zone) || end_chunk(out, ck))
            goto oom;
    }

    if (begin_chunk(out, "ibag", &ck))
        goto oom;
    {
        uint32_t gi = 0;
        for (i = 0; i < b->ninst; i++)
            for (k = 0; k < b->inst[i].nsounds; k++) {
                const ge_ae_sound *s = &b->inst[i].sounds[k];
                if (!s->valid)
                    continue;
                if (ge_buf_le16(out, gi) || ge_buf_le16(out, 0))
                    goto oom;
                gi += (uint32_t)zone_gens(b, &b->inst[i], s, sid[s->wave], g);
            }
        total_gens = gi;
        if (ge_buf_le16(out, gi) || ge_buf_le16(out, 0) || end_chunk(out, ck))
            goto oom;
    }
    if (total_zones + 1 > 65535 || total_gens + 1 > 65535) {
        ge_seterr(err, "bank too large for SF2 (%u zones, %u generators)", (unsigned)total_zones, (unsigned)total_gens);
        goto done;
    }

    if (begin_chunk(out, "imod", &ck) || ge_buf_fill(out, 0, 10) || end_chunk(out, ck))
        goto oom;

    if (begin_chunk(out, "igen", &ck))
        goto oom;
    for (i = 0; i < b->ninst; i++)
        for (k = 0; k < b->inst[i].nsounds; k++) {
            const ge_ae_sound *s = &b->inst[i].sounds[k];
            int n, q;
            if (!s->valid)
                continue;
            n = zone_gens(b, &b->inst[i], s, sid[s->wave], g);
            for (q = 0; q < n; q++)
                if (ge_buf_le16(out, g[q].oper) || ge_buf_le16(out, g[q].amount))
                    goto oom;
        }
    if (ge_buf_le16(out, 0) || ge_buf_le16(out, 0) || end_chunk(out, ck))
        goto oom;

    if (begin_chunk(out, "shdr", &ck))
        goto oom;
    for (k = 0; k < nsamples; k++) {
        for (i = 0; i < b->nwaves; i++)
            if (sid[i] == k) {
                const ge_ae_wave *w = &b->waves[i];
                uint32_t st = smpl_start[i];
                /* a sound's key base is a per-zone generator (overridingRootKey), so
                 * the sample header only carries a neutral root key */
                snprintf(nm, sizeof nm, "wave %d", i);
                if (put_name(out, nm) || ge_buf_le32(out, st) || ge_buf_le32(out, st + w->nsamples) ||
                    ge_buf_le32(out, w->has_loop ? st + w->loop_start : st) ||
                    ge_buf_le32(out, w->has_loop ? st + w->loop_end : st) ||
                    ge_buf_le32(out, (uint32_t)b->sample_rate) || ge_buf_u8(out, 60) || ge_buf_u8(out, 0) ||
                    ge_buf_le16(out, 0) || ge_buf_le16(out, 1))
                    goto oom;
                break;
            }
    }
    if (put_name(out, "EOS") || ge_buf_fill(out, 0, 26) || end_chunk(out, ck))
        goto oom;

    if (end_chunk(out, list) || end_chunk(out, riff))
        goto oom;
    tot = out->size;
    (void)tot;
    rc = 0;
    goto done;
oom:
    ge_seterr(err, "out of memory");
done:
    free(sid);
    free(smpl_start);
    if (rc)
        ge_ae_buf_free(out);
    return rc;
}
