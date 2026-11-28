/*
 * musicseq.c -- the pack music sequencer (port/include/musicseq.h). Written from docs/specs/audio.md only.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* not <stdlib.h>: in the game build -Iinclude resolves it to the N64 stub, which declares none of these (D324, D83) */
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void *malloc(size_t);
void free(void *);
long strtol(const char *, char **, int);

#include "musicseq.h"

#define MSQ_DEFAULT_TEMPO 500000u
#define MSQ_MAX_DISPATCH 200000   /* per msqAdvance call: a guard against a loop of no length */

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t be16(const unsigned char *p) { return (uint32_t)p[0] << 8 | p[1]; }

static int rdvlq(const unsigned char *d, size_t e, size_t *q, uint32_t *out)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if (*q >= e)
            return -1;
        v = (v << 7) | (d[*q] & 0x7F);
        if (!(d[(*q)++] & 0x80)) {
            *out = v;
            return 0;
        }
    }
    return -1;
}

static int push(MsqTrack *t, int *cap, const MsqEvent *e)
{
    if (t->n == *cap) {
        int nc = *cap ? *cap * 2 : 256;
        MsqEvent *nv = (MsqEvent *)realloc(t->ev, (size_t)nc * sizeof *nv);
        if (!nv)
            return -1;
        t->ev = nv;
        *cap = nc;
    }
    t->ev[t->n++] = *e;
    return 0;
}

/* "loopStart <id>" / "loopEnd <id> <count|inf>"; returns 1 start, 2 end, 0 another marker (an older pack's bare
 * "loopStart" / "loopEnd" included: such a file plays straight through). */
static int parse_marker(const unsigned char *s, uint32_t len, uint32_t *id, int *count)
{
    char buf[64];
    unsigned a;
    char c[16];
    if (len >= sizeof buf)
        return 0;
    memcpy(buf, s, len);
    buf[len] = 0;
    if (!strncmp(buf, "loopStart ", 10) && sscanf(buf + 10, "%u", &a) == 1) {
        *id = a;
        return 1;
    }
    if (!strncmp(buf, "loopEnd ", 8) && sscanf(buf + 8, "%u %15s", &a, c) == 2) {
        *id = a;
        if (!strcmp(c, "inf")) {
            *count = 255;
        } else {
            char *end;
            long n = strtol(c, &end, 10);
            if (*end || n < 0 || n > 254)
                return 0;
            *count = (int)n;
        }
        return 2;
    }
    return 0;
}

static int parse_track(MsqSeq *s, MsqTrack *t, const unsigned char *d, size_t q, size_t e, char *err, size_t errlen)
{
    int cap = 0, running = 0, i;
    uint32_t tick = 0;
    int open[16][128];
    int lcap = 0;
    MsqEvent ev;

    for (i = 0; i < 16 * 128; i++)
        open[i / 128][i % 128] = -1;
    while (q < e) {
        uint32_t dt;
        int st;
        memset(&ev, 0, sizeof ev);
        if (rdvlq(d, e, &q, &dt) || q >= e)
            goto bad;
        tick += dt;
        ev.tick = tick;
        st = d[q];
        if (st == 0xFF) {
            uint32_t type, len, id;
            int count;
            if (q + 2 > e)
                goto bad;
            type = d[q + 1];
            q += 2;
            if (rdvlq(d, e, &q, &len) || len > e - q)
                goto bad;
            running = 0;
            if (type == 0x2F) {
                ev.type = MSQ_END;
                if (push(t, &cap, &ev))
                    goto oom;
                break;
            } else if (type == 0x51 && len == 3) {
                ev.type = MSQ_TEMPO;
                ev.val = (uint32_t)d[q] << 16 | (uint32_t)d[q + 1] << 8 | d[q + 2];
                if (push(t, &cap, &ev))
                    goto oom;
            } else if (type == 0x06) {
                int k = parse_marker(d + q, len, &id, &count);
                if (k == 1) {
                    ev.type = MSQ_LOOP_START;
                    ev.val = id;
                    if (id != (uint32_t)t->nloops) {
                        /* ids are written in stream order: a gap means another writer; keep the id it gave */
                    }
                    if ((int)id >= lcap) {
                        int nc = (int)id + 16;
                        int *nv = (int *)realloc(t->loop_start_at, (size_t)nc * sizeof *nv);
                        if (!nv)
                            goto oom;
                        for (i = lcap; i < nc; i++)
                            nv[i] = -1;
                        t->loop_start_at = nv;
                        lcap = nc;
                    }
                    t->loop_start_at[id] = t->n;
                    if ((int)id + 1 > t->nloops)
                        t->nloops = (int)id + 1;
                    if (push(t, &cap, &ev))
                        goto oom;
                } else if (k == 2) {
                    ev.type = MSQ_LOOP_END;
                    ev.val = id;
                    ev.b = (uint8_t)count;
                    if (push(t, &cap, &ev))
                        goto oom;
                }
            }
            q += len;
            continue;
        }
        if (st == 0xF0 || st == 0xF7) {
            uint32_t len;
            q++;
            if (rdvlq(d, e, &q, &len) || len > e - q)
                goto bad;
            q += len;
            running = 0;
            continue;
        }
        if (st & 0x80) {
            running = st;
            q++;
        } else if (!running) {
            goto bad;
        }
        {
            int type = running & 0xF0, ch = running & 0x0F, d1, d2 = 0;
            int two = type != 0xC0 && type != 0xD0;
            if (q + (two ? 2u : 1u) > e)
                goto bad;
            d1 = d[q] & 0x7F;
            if (two)
                d2 = d[q + 1] & 0x7F;
            q += two ? 2 : 1;
            ev.ch = (uint8_t)ch;
            if (type == 0x90 && d2 > 0) {
                if (open[ch][d1] >= 0) /* a second note-on of a held key: the first ends here */
                    t->ev[open[ch][d1]].dur = tick - t->ev[open[ch][d1]].tick;
                ev.type = MSQ_NOTE_ON;
                ev.a = (uint8_t)d1;
                ev.b = (uint8_t)d2;
                ev.dur = 0xFFFFFFFFu;
                open[ch][d1] = t->n;
                if (push(t, &cap, &ev))
                    goto oom;
            } else if (type == 0x80 || type == 0x90) {
                if (open[ch][d1] >= 0) {
                    t->ev[open[ch][d1]].dur = tick - t->ev[open[ch][d1]].tick;
                    open[ch][d1] = -1;
                }
            } else if (type == 0xB0) {
                ev.type = MSQ_CONTROL;
                ev.a = (uint8_t)d1;
                ev.b = (uint8_t)d2;
                if (push(t, &cap, &ev))
                    goto oom;
            } else if (type == 0xC0) {
                ev.type = MSQ_PROGRAM;
                ev.a = (uint8_t)d1;
                if (push(t, &cap, &ev))
                    goto oom;
            } else if (type == 0xE0) {
                ev.type = MSQ_BEND;
                ev.val = (uint32_t)(d1 | d2 << 7);
                if (push(t, &cap, &ev))
                    goto oom;
            }
            /* 0xA0 / 0xD0 pressure: no meaning for this synth */
        }
    }
    if (!t->n || t->ev[t->n - 1].type != MSQ_END) {
        memset(&ev, 0, sizeof ev);
        ev.tick = tick;
        ev.type = MSQ_END;
        if (push(t, &cap, &ev))
            goto oom;
    }
    /* a note never ended: it ends at the track's end */
    for (i = 0; i < t->n; i++)
        if (t->ev[i].type == MSQ_NOTE_ON && t->ev[i].dur == 0xFFFFFFFFu)
            t->ev[i].dur = tick - t->ev[i].tick;
    /* loop ends: resolve the id, give each a counter */
    for (i = 0; i < t->n; i++) {
        MsqEvent *x = &t->ev[i];
        if (x->type != MSQ_LOOP_END)
            continue;
        if ((int)x->val >= t->nloops || t->loop_start_at[x->val] < 0 || t->loop_start_at[x->val] > i) {
            x->type = 0; /* unresolved: ignored */
            s->unresolved++;
            continue;
        }
        x->counter = (uint16_t)s->ncounters++;
    }
    return 0;
bad:
    snprintf(err, errlen, "malformed track data");
    return -1;
oom:
    snprintf(err, errlen, "out of memory");
    return -1;
}

void msqFree(MsqSeq *s)
{
    int i;
    if (!s)
        return;
    for (i = 0; i < s->ntracks; i++) {
        free(s->tracks[i].ev);
        free(s->tracks[i].loop_start_at);
    }
    free(s->tracks);
    free(s->counter_init);
    free(s);
}

MsqSeq *msqLoad(const unsigned char *d, size_t n, char *err, size_t errlen)
{
    MsqSeq *s;
    size_t p;
    int ntrk, t, k;

    if (n < 14 || memcmp(d, "MThd", 4) || be32(d + 4) < 6) {
        snprintf(err, errlen, "not a Standard MIDI File");
        return NULL;
    }
    ntrk = (int)be16(d + 10);
    if (be16(d + 12) == 0 || (be16(d + 12) & 0x8000) || ntrk <= 0) {
        snprintf(err, errlen, "unsupported division or no track");
        return NULL;
    }
    s = (MsqSeq *)calloc(1, sizeof *s);
    if (!s || !(s->tracks = (MsqTrack *)calloc((size_t)ntrk, sizeof *s->tracks))) {
        free(s);
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    s->division = (int)be16(d + 12);
    p = 8 + be32(d + 4);
    for (t = 0; t < ntrk && p + 8 <= n; t++) {
        uint32_t len;
        if (memcmp(d + p, "MTrk", 4)) {
            snprintf(err, errlen, "chunk %d is not a track", t);
            msqFree(s);
            return NULL;
        }
        len = be32(d + p + 4);
        if (len > n - p - 8) {
            snprintf(err, errlen, "track %d runs past the end of the file", t);
            msqFree(s);
            return NULL;
        }
        s->ntracks = t + 1;
        if (parse_track(s, &s->tracks[t], d, p + 8, p + 8 + len, err, errlen)) {
            msqFree(s);
            return NULL;
        }
        p += 8 + len;
    }
    if (!s->ntracks) {
        snprintf(err, errlen, "no track");
        msqFree(s);
        return NULL;
    }
    s->counter_init = (uint8_t *)calloc((size_t)(s->ncounters ? s->ncounters : 1), 1);
    if (!s->counter_init) {
        msqFree(s);
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    for (t = 0; t < s->ntracks; t++)
        for (k = 0; k < s->tracks[t].n; k++)
            if (s->tracks[t].ev[k].type == MSQ_LOOP_END)
                s->counter_init[s->tracks[t].ev[k].counter] = s->tracks[t].ev[k].b;
    return s;
}

/* ---- the player ------------------------------------------------------------------------------------------------ */

void msqStopPlayer(MsqPlayer *p)
{
    free(p->trk);
    free(p->remain);
    memset(p, 0, sizeof *p);
}

int msqStart(MsqPlayer *p, const MsqSeq *seq)
{
    msqStopPlayer(p);
    p->trk = (MsqTrackPos *)calloc((size_t)seq->ntracks, sizeof *p->trk);
    p->remain = (uint8_t *)malloc((size_t)(seq->ncounters ? seq->ncounters : 1));
    if (!p->trk || !p->remain) {
        msqStopPlayer(p);
        return 0;
    }
    memcpy(p->remain, seq->counter_init, (size_t)seq->ncounters);
    p->seq = seq;
    p->us_per_tick = MSQ_DEFAULT_TEMPO / (uint32_t)seq->division;
    if (!p->us_per_tick)
        p->us_per_tick = 1;
    return 1;
}

void msqPause(MsqPlayer *p)
{
    p->nheld = 0;
}

void msqResume(MsqPlayer *p)
{
    if (p->seq && p->now > p->last_due)
        p->now = p->last_due;
}

/* The event a track must play next, or NULL when it ended. */
static const MsqEvent *next_ev(const MsqPlayer *p, int t)
{
    const MsqTrack *tr = &p->seq->tracks[t];
    if (p->trk[t].ended || p->trk[t].pos >= tr->n)
        return NULL;
    return &tr->ev[p->trk[t].pos];
}

static void dispatch(MsqPlayer *p, int t, const MsqEvent *e, double due, const MsqSink *k)
{
    MsqTrackPos *tp = &p->trk[t];
    const MsqTrack *tr = &p->seq->tracks[t];
    tp->pos++;
    p->last_due = due;
    switch (e->type) {
    case MSQ_NOTE_ON:
        if (k->note_on)
            k->note_on(k->user, e->ch, e->a, e->b);
        if (p->nheld < MSQ_MAX_HELD) {
            p->held[p->nheld].due = due + e->dur;
            p->held[p->nheld].ch = e->ch;
            p->held[p->nheld].key = e->a;
            p->nheld++;
        } else if (k->note_off) {
            k->note_off(k->user, e->ch, e->a); /* no room: the note is cut at once */
        }
        break;
    case MSQ_CONTROL:
        if (k->control)
            k->control(k->user, e->ch, e->a, e->b);
        break;
    case MSQ_PROGRAM:
        if (k->program)
            k->program(k->user, e->ch, e->a);
        break;
    case MSQ_BEND:
        if (k->bend)
            k->bend(k->user, e->ch, (int)e->val);
        break;
    case MSQ_TEMPO:
        p->us_per_tick = e->val / (uint32_t)p->seq->division;
        if (!p->us_per_tick)
            p->us_per_tick = 1;
        break;
    case MSQ_LOOP_END: {
        uint8_t *c = &p->remain[e->counter];
        int jump;
        if (*c == 255) {
            jump = 1;
        } else if (*c == 0) {
            *c = e->b;
            jump = 0;
        } else {
            (*c)--;
            jump = 1;
        }
        if (jump) {
            int at = tr->loop_start_at[e->val];
            int64_t delta = (int64_t)e->tick - (int64_t)tr->ev[at].tick;
            if (delta <= 0 && e->b == 255) {
                tp->ended = 1; /* a forever loop of no length would never let time move */
                break;
            }
            tp->pos = at + 1;
            tp->offset += delta;
            p->jumps++;
        }
        break;
    }
    case MSQ_END:
        tp->ended = 1;
        break;
    default:
        break;
    }
}

int msqAdvance(MsqPlayer *p, double us, const MsqSink *k)
{
    int guard = 0;
    if (!p->seq || p->ended)
        return 0;
    for (;;) {
        double best = 0;
        int have = 0, isoff = 0, idx = -1, t, h;
        const MsqEvent *be = NULL;
        /* the earliest due; at equal due the note-offs first, then the tracks in order */
        for (h = 0; h < p->nheld; h++)
            if (!have || p->held[h].due < best) {
                have = 1;
                best = p->held[h].due;
                isoff = 1;
                idx = h;
            }
        for (t = 0; t < p->seq->ntracks; t++) {
            const MsqEvent *e = next_ev(p, t);
            double due;
            if (!e)
                continue;
            due = (double)((int64_t)e->tick + p->trk[t].offset);
            if (!have || due < best) {
                have = 1;
                best = due;
                isoff = 0;
                idx = t;
                be = e;
            }
        }
        if (!have) {
            p->ended = 1; /* every track ended, every note-off fired */
            return 0;
        }
        {
            double wait_us = (best - p->now) * p->us_per_tick;
            if (wait_us > us) {
                p->now += us / p->us_per_tick;
                return 1;
            }
            if (wait_us > 0) {
                us -= wait_us;
                p->now = best;
            }
        }
        if (++guard > MSQ_MAX_DISPATCH) {
            for (t = 0; t < p->seq->ntracks; t++)
                p->trk[t].ended = 1;
            p->nheld = 0;
            p->ended = 1;
            return 0;
        }
        if (!isoff) {
            dispatch(p, idx, be, best, k);
        } else {
            h = idx;
            if (k->note_off)
                k->note_off(k->user, p->held[h].ch, p->held[h].key);
            p->held[h] = p->held[--p->nheld];
        }
    }
}
