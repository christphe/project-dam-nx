/*
 * Compressed sequence -> Standard MIDI File (format 1).
 * See docs/specs/audio.md "Music table" and "Compressed sequence".
 */
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "ge_audio_internal.h"

#define SEQ_HEADER_SIZE 68u
#define SEQ_MAX_TRACKS 16
#define SEQ_MAX_EVENTS (1u << 22) /* per sequence track: sanity cap against runaway streams */
#define SEQ_MAX_TICK 0x0FFFFFFFu  /* keeps tick arithmetic and SMF variable-length numbers in range */

#define OFF_VELOCITY 0x40 /* note-off velocity the spec asks for */

enum { K_OFF = 0, K_CHAN, K_TEMPO, K_LOOP_START, K_LOOP_END };

typedef struct {
    uint32_t tick, seq;
    uint32_t val; /* tempo in microseconds per quarter note */
    uint8_t kind, status, d1, d2;
} seq_event;

typedef struct {
    seq_event *v;
    size_t n, cap;
} evvec;

static int ev_push(evvec *e, uint32_t tick, uint8_t kind, uint8_t status, uint8_t d1, uint8_t d2, uint32_t val)
{
    seq_event *p;
    if (e->n == e->cap) {
        size_t cap = e->cap ? e->cap * 2 : 256;
        p = (seq_event *)realloc(e->v, cap * sizeof *p);
        if (!p)
            return -1;
        e->v = p;
        e->cap = cap;
    }
    p = &e->v[e->n];
    p->tick = tick;
    p->seq = (uint32_t)e->n;
    p->kind = kind;
    p->status = status;
    p->d1 = d1;
    p->d2 = d2;
    p->val = val;
    e->n++;
    return 0;
}

/* ---- the back-reference byte reader (spec: "Reading a track's bytes") ---- */
typedef struct {
    const uint8_t *buf;
    size_t size;
    size_t pos;  /* current position */
    size_t rpos; /* replay window position */
    unsigned rlen; /* bytes left in the replay window */
    int from_win;  /* the last byte returned came from a replay window */
    const char *fail;
} byte_reader;

/* Returns 0..255, or -1 on malformed data (r->fail says why). */
static int rd_byte(byte_reader *r)
{
    for (;;) {
        size_t p0;
        unsigned b, d, len;
        if (r->rlen) {
            if (r->rpos >= r->size) {
                r->fail = "replay window runs past the end of the sequence";
                return -1;
            }
            r->rlen--;
            r->from_win = 1;
            return r->buf[r->rpos++];
        }
        if (r->pos >= r->size) {
            r->fail = "track data ends before end-of-track";
            return -1;
        }
        p0 = r->pos;
        r->from_win = 0;
        b = r->buf[r->pos++];
        if (b != 0xFE)
            return (int)b;
        if (r->pos >= r->size) {
            r->fail = "truncated 0xFE marker";
            return -1;
        }
        b = r->buf[r->pos++];
        if (b == 0xFE)
            return 0xFE;
        if (r->size - r->pos < 2) {
            r->fail = "truncated back-reference marker";
            return -1;
        }
        d = (b << 8) | r->buf[r->pos];
        len = r->buf[r->pos + 1];
        r->pos += 2;
        if (d > p0) {
            r->fail = "back-reference points before the start of the sequence";
            return -1;
        }
        r->rpos = p0 - d;
        r->rlen = len; /* a zero-length window simply falls through to the next byte */
    }
}

static int rd_vlq(byte_reader *r, uint32_t *out)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int b = rd_byte(r);
        if (b < 0)
            return -1;
        v = (v << 7) | ((uint32_t)b & 0x7F);
        if (!(b & 0x80)) {
            *out = v;
            return 0;
        }
    }
    r->fail = "variable-length number longer than 4 bytes";
    return -1;
}

/* ---- one sequence track -> events ------------------------------------------ */
#define SEQ_MAX_LOOP_STARTS 256

static void add_warning(ge_ae_music_stats *st, const char *fmt, int track, unsigned tick)
{
    size_t o = strlen(st->warning);
    if (o + 3 < sizeof st->warning)
        snprintf(st->warning + o, sizeof st->warning - o, fmt, track, tick);
}

/* Loop starts are remembered by the raw position just after their 2 bytes (spec: "Loops"): a loop end resolves to the
 * loop start at (position after its 6 bytes) - B, and its marker names that start's id (its index among the track's loop
 * starts). A loop end that resolves to none is dropped and counted. */
static int parse_track(const uint8_t *seq, size_t size, uint32_t start, int trk, ge_ae_music_stats *st, evvec *ev,
                       uint32_t *end_tick, char *err)
{
    byte_reader r;
    uint32_t tick = 0;
    int running = -1;
    uint32_t nev = 0;
    size_t lpos[SEQ_MAX_LOOP_STARTS];
    int nloop = 0;

    r.buf = seq;
    r.size = size;
    r.pos = start;
    r.rpos = 0;
    r.rlen = 0;
    r.from_win = 0;
    r.fail = "";

    for (;;) {
        uint32_t delta;
        int b;

        if (++nev > SEQ_MAX_EVENTS) {
            ge_seterr(err, "more than %u events in one track", SEQ_MAX_EVENTS);
            return -1;
        }
        if (rd_vlq(&r, &delta))
            goto fail;
        if (delta > SEQ_MAX_TICK - tick) {
            ge_seterr(err, "tick count exceeds %u", SEQ_MAX_TICK);
            return -1;
        }
        tick += delta;
        b = rd_byte(&r);
        if (b < 0)
            goto fail;
        st->events++;

        if (b == 0xFF) { /* meta, never running status */
            int type = rd_byte(&r);
            int i, x[6];
            running = -1;
            if (type < 0)
                goto fail;
            switch (type) {
            case 0x2F:
                *end_tick = tick;
                return 0;
            case 0x51:
                for (i = 0; i < 3; i++)
                    if ((x[i] = rd_byte(&r)) < 0)
                        goto fail;
                st->tempo_changes++;
                if (!st->first_tempo_us)
                    st->first_tempo_us = ((uint32_t)x[0] << 16) | ((uint32_t)x[1] << 8) | (uint32_t)x[2];
                if (ev_push(ev, tick, K_TEMPO, 0, 0, 0, ((uint32_t)x[0] << 16) | ((uint32_t)x[1] << 8) | (uint32_t)x[2]))
                    goto oom;
                break;
            case 0x2E:
                for (i = 0; i < 2; i++)
                    if ((x[i] = rd_byte(&r)) < 0)
                        goto fail;
                if (r.from_win || r.rlen)
                    add_warning(st, " t%d: loop start inside a back-reference window at tick %u;", trk, (unsigned)tick);
                if (nloop >= SEQ_MAX_LOOP_STARTS) {
                    ge_seterr(err, "more than %d loop starts in one track", SEQ_MAX_LOOP_STARTS);
                    return -1;
                }
                lpos[nloop] = r.pos;
                if (!st->loop_starts)
                    st->loop_start_tick = tick;
                st->loop_starts++;
                if (ev_push(ev, tick, K_LOOP_START, 0, 0, 0, (uint32_t)nloop))
                    goto oom;
                nloop++;
                break;
            case 0x2D: {
                /* 6 raw bytes from the current position, not through the byte reader */
                uint32_t dist;
                int id = -1, k;
                if (r.rlen)
                    add_warning(st, " t%d: loop end inside a back-reference window at tick %u;", trk, (unsigned)tick);
                if (r.size - r.pos < 6) {
                    r.fail = "truncated loop end";
                    goto fail;
                }
                for (i = 0; i < 6; i++)
                    x[i] = r.buf[r.pos + (size_t)i];
                r.pos += 6;
                dist = ((uint32_t)x[2] << 24) | ((uint32_t)x[3] << 16) | ((uint32_t)x[4] << 8) | (uint32_t)x[5];
                if (dist <= r.pos)
                    for (k = 0; k < nloop; k++)
                        if (lpos[k] == r.pos - dist)
                            id = k;
                if (id < 0) {
                    st->dead_loop_ends[trk]++;
                    break;
                }
                if (!st->loop_ends)
                    st->loop_end_tick = tick;
                st->loop_ends++;
                /* d1: the current-count byte (0xFF = forever), val: the loop start id */
                if (ev_push(ev, tick, K_LOOP_END, 0, (uint8_t)x[1], 0, (uint32_t)id))
                    goto oom;
                break;
            }
            default:
                /* The length of an unknown meta is unknowable: report it and
                 * end the track here. */
                st->unknown_metas++;
                snprintf(st->warning, sizeof st->warning,
                         "unknown meta 0xFF 0x%02X at tick %u: track cut here", (unsigned)type, (unsigned)tick);
                *end_tick = tick;
                return 0;
            }
        } else {
            int status, d1, d2 = 0, type;
            if (b & 0x80) {
                status = b;
                running = b;
                d1 = rd_byte(&r);
                if (d1 < 0)
                    goto fail;
            } else {
                if (running < 0) {
                    ge_seterr(err, "data byte 0x%02X at tick %u with no running status", (unsigned)b, (unsigned)tick);
                    return -1;
                }
                status = running;
                d1 = b;
            }
            if (status >= 0xF0) {
                ge_seterr(err, "unsupported status 0x%02X at tick %u", (unsigned)status, (unsigned)tick);
                return -1;
            }
            type = status & 0xF0;
            if (type != 0xC0 && type != 0xD0) {
                d2 = rd_byte(&r);
                if (d2 < 0)
                    goto fail;
            }
            if ((d1 & 0x80) || (d2 & 0x80)) {
                ge_seterr(err, "data byte with the high bit set in a 0x%02X message at tick %u", (unsigned)status,
                          (unsigned)tick);
                return -1;
            }
            st->channel_mask |= 1u << (status & 0x0F);
            if (ev_push(ev, tick, K_CHAN, (uint8_t)status, (uint8_t)d1, (uint8_t)d2, 0))
                goto oom;
            if (type == 0x90) {
                uint32_t dur;
                st->notes++;
                if (rd_vlq(&r, &dur))
                    goto fail;
                if (dur > SEQ_MAX_TICK - tick) {
                    ge_seterr(err, "note ends beyond tick %u", SEQ_MAX_TICK);
                    return -1;
                }
                if (ev_push(ev, tick + dur, K_OFF, (uint8_t)(0x80 | (status & 0x0F)), (uint8_t)d1, OFF_VELOCITY,
                            (uint32_t)(ev->n - 1))) /* val: index of the note-on that scheduled it */
                    goto oom;
            }
        }
    }
fail:
    ge_seterr(err, "%s (track at 0x%X)", r.fail, (unsigned)start);
    return -1;
oom:
    ge_seterr(err, "out of memory");
    return -1;
}

/* ---- SMF writing ---------------------------------------------------------------- */
static int ev_cmp(const void *pa, const void *pb)
{
    const seq_event *a = (const seq_event *)pa, *b = (const seq_event *)pb;
    int ca = a->kind == K_OFF ? 0 : 1, cb = b->kind == K_OFF ? 0 : 1; /* note-offs first at equal ticks */
    if (a->tick != b->tick)
        return a->tick < b->tick ? -1 : 1;
    if (ca != cb)
        return ca - cb;
    return a->seq < b->seq ? -1 : (a->seq > b->seq);
}


/* ---- real note ends (spec: "Which note a note-off ends") -----------------------------------
 * A scheduled note-off ends the OLDEST held note of its channel and key, not the note that
 * scheduled it. The scheduled note-offs are replayed per (channel, key) against a FIFO of held
 * notes; a note still sounding at the next same channel+key note-on is cut there. The K_OFF
 * events of `ev` are replaced by note-offs at the real ends. */
typedef struct {
    uint32_t start, sched_end, end;
    int next; /* next note of the same channel+key in start order, or -1 */
    int slot;
} note_rec;

#define NOTE_OPEN 0xFFFFFFFFu

static int resolve_note_ends(evvec *ev, uint32_t end_tick, ge_ae_music_stats *st)
{
    size_t i, k, nev = ev->n, nn = 0;
    note_rec *notes = NULL;
    int *nid = NULL;
    seq_event *sorted = NULL;
    evvec out = {0};
    int head[2048], tail[2048], rel[2048], s, rc = -1;
    uint32_t last_tick = end_tick;

    for (i = 0; i < nev; i++) {
        if (ev->v[i].tick > last_tick)
            last_tick = ev->v[i].tick;
        if (ev->v[i].kind == K_CHAN && (ev->v[i].status & 0xF0) == 0x90)
            nn++;
    }
    if (!nn)
        return 0;
    notes = (note_rec *)malloc(nn * sizeof *notes);
    nid = (int *)malloc(nev * sizeof *nid);
    sorted = (seq_event *)malloc(nev * sizeof *sorted);
    if (!notes || !nid || !sorted)
        goto done;
    for (s = 0; s < 2048; s++)
        head[s] = tail[s] = rel[s] = -1;

    memcpy(sorted, ev->v, nev * sizeof *sorted);
    qsort(sorted, nev, sizeof *sorted, ev_cmp); /* tick order, note-offs first at equal ticks */

    /* pass 1: note ids in start order, per-slot chains */
    nn = 0;
    for (k = 0; k < nev; k++) {
        const seq_event *e = &sorted[k];
        if (e->kind == K_CHAN && (e->status & 0xF0) == 0x90) {
            note_rec *n = &notes[nn];
            int slot = ((e->status & 0x0F) << 7) | (e->d1 & 0x7F);
            n->start = e->tick;
            n->sched_end = n->end = NOTE_OPEN;
            n->next = -1;
            n->slot = slot;
            nid[e->seq] = (int)nn;
            if (tail[slot] >= 0)
                notes[tail[slot]].next = (int)nn;
            else
                head[slot] = (int)nn;
            tail[slot] = (int)nn;
            nn++;
        }
    }
    for (i = 0; i < nev; i++)
        if (ev->v[i].kind == K_OFF)
            notes[nid[ev->v[i].val]].sched_end = ev->v[i].tick;

    /* pass 2: replay in tick order; rel[slot] = oldest note not yet released */
    nn = 0;
    for (k = 0; k < nev; k++) {
        const seq_event *e = &sorted[k];
        int slot = ((e->status & 0x0F) << 7) | (e->d1 & 0x7F);
        if (e->kind == K_CHAN && (e->status & 0xF0) == 0x90) {
            if (rel[slot] < 0)
                rel[slot] = (int)nn; /* nothing was held: this note is the oldest */
            nn++;
        } else if (e->kind == K_OFF) {
            int v = rel[slot];
            if (v >= 0) {
                notes[v].end = e->tick;
                rel[slot] = notes[v].next < (int)nn ? notes[v].next : -1; /* only notes already started */
            }
        }
    }

    /* overlap cut, then statistics */
    for (s = 0; s < 2048; s++) {
        int a;
        for (a = head[s]; a >= 0; a = notes[a].next) {
            int b = notes[a].next;
            if (notes[a].end == NOTE_OPEN)
                notes[a].end = last_tick;
            if (b >= 0 && notes[a].end > notes[b].start) {
                notes[a].end = notes[b].start;
                st->overlaps_cut++;
            }
            if (notes[a].end != notes[a].sched_end)
                st->ends_changed[s >> 7]++;
        }
    }

    /* rebuild: non-off events as they were, then the real note-offs */
    for (i = 0; i < nev; i++)
        if (ev->v[i].kind != K_OFF &&
            ev_push(&out, ev->v[i].tick, ev->v[i].kind, ev->v[i].status, ev->v[i].d1, ev->v[i].d2, ev->v[i].val))
            goto done;
    for (s = 0; s < 2048; s++) {
        int a;
        for (a = head[s]; a >= 0; a = notes[a].next)
            if (ev_push(&out, notes[a].end, K_OFF, (uint8_t)(0x80 | (s >> 7)), (uint8_t)(s & 0x7F), OFF_VELOCITY, 0))
                goto done;
    }
    free(ev->v);
    *ev = out;
    out.v = NULL;
    rc = 0;
done:
    free(out.v);
    free(notes);
    free(nid);
    free(sorted);
    return rc;
}

static int put_vlq(ge_ae_buf *b, uint32_t v)
{
    uint8_t t[5];
    int n = 0;
    t[n++] = v & 0x7F;
    while ((v >>= 7) != 0)
        t[n++] = (uint8_t)((v & 0x7F) | 0x80);
    while (n--)
        if (ge_buf_u8(b, t[n]))
            return -1;
    return 0;
}

static int put_marker(ge_ae_buf *b, const char *text)
{
    size_t n = strlen(text);
    return ge_buf_u8(b, 0xFF) || ge_buf_u8(b, 0x06) || ge_buf_u8(b, (uint32_t)n) || ge_buf_put(b, text, n);
}

static int write_mtrk(ge_ae_buf *out, evvec *ev, uint32_t end_tick, uint32_t *len_ticks)
{
    size_t lenpos, i, body;
    uint32_t last = 0;

    qsort(ev->v, ev->n, sizeof *ev->v, ev_cmp);
    if (ev->n && ev->v[ev->n - 1].tick > end_tick)
        end_tick = ev->v[ev->n - 1].tick;

    if (ge_buf_put(out, "MTrk", 4) || ge_buf_be32(out, 0))
        return -1;
    lenpos = out->size - 4;
    body = out->size;
    for (i = 0; i < ev->n; i++) {
        const seq_event *e = &ev->v[i];
        if (put_vlq(out, e->tick - last))
            return -1;
        last = e->tick;
        switch (e->kind) {
        case K_OFF:
        case K_CHAN: {
            int t = e->status & 0xF0;
            if (ge_buf_u8(out, e->status) || ge_buf_u8(out, e->d1))
                return -1;
            if (t != 0xC0 && t != 0xD0 && ge_buf_u8(out, e->d2))
                return -1;
            break;
        }
        case K_TEMPO:
            if (ge_buf_u8(out, 0xFF) || ge_buf_u8(out, 0x51) || ge_buf_u8(out, 3) || ge_buf_u8(out, e->val >> 16) ||
                ge_buf_u8(out, e->val >> 8) || ge_buf_u8(out, e->val))
                return -1;
            break;
        case K_LOOP_START: {
            char txt[32];
            snprintf(txt, sizeof txt, "loopStart %u", (unsigned)e->val);
            if (put_marker(out, txt))
                return -1;
            break;
        }
        case K_LOOP_END: {
            char txt[40];
            if (e->d1 == 0xFF)
                snprintf(txt, sizeof txt, "loopEnd %u inf", (unsigned)e->val);
            else
                snprintf(txt, sizeof txt, "loopEnd %u %u", (unsigned)e->val, (unsigned)e->d1);
            if (put_marker(out, txt))
                return -1;
            break;
        }
        }
    }
    if (put_vlq(out, end_tick - last) || ge_buf_u8(out, 0xFF) || ge_buf_u8(out, 0x2F) || ge_buf_u8(out, 0))
        return -1;
    /* big-endian chunk length */
    {
        uint32_t len = (uint32_t)(out->size - body);
        out->data[lenpos] = (uint8_t)(len >> 24);
        out->data[lenpos + 1] = (uint8_t)(len >> 16);
        out->data[lenpos + 2] = (uint8_t)(len >> 8);
        out->data[lenpos + 3] = (uint8_t)len;
    }
    if (len_ticks && end_tick > *len_ticks)
        *len_ticks = end_tick;
    return 0;
}

/* ---- entry point ------------------------------------------------------------------ */
int ge_ae_music_convert(const uint8_t *rom, size_t size, const ge_ae_layout *lay, int index, ge_ae_buf *mid,
                        ge_ae_music_stats *st, char *err)
{
    ge_view tab;
    int bad = 0, n, i, rc = -1;
    uint32_t off, usize, csize, offsets[SEQ_MAX_TRACKS], division;
    uint8_t *seq = NULL;
    ge_ae_buf out = {0};
    evvec ev[SEQ_MAX_TRACKS];
    uint32_t end_tick[SEQ_MAX_TRACKS];
    z_stream zs;
    int zr;

    memset(st, 0, sizeof *st);
    memset(ev, 0, sizeof ev);
    mid->data = NULL;
    mid->size = mid->cap = 0;

    if (lay->music_table > size || size - lay->music_table < 4) {
        ge_seterr(err, "music table outside the ROM");
        return -1;
    }
    tab.p = rom + lay->music_table;
    tab.n = size - lay->music_table;
    n = (int)ge_v_u16(tab, 0, &bad);
    if (bad || index < 0 || index >= n) {
        ge_seterr(err, "track index %d out of range (table has %d)", index, n);
        return -1;
    }
    off = ge_v_u32(tab, 4 + 8 * (size_t)index, &bad);
    usize = ge_v_u16(tab, 4 + 8 * (size_t)index + 4, &bad);
    csize = ge_v_u16(tab, 4 + 8 * (size_t)index + 6, &bad);
    if (bad) {
        ge_seterr(err, "music table entry outside the ROM");
        return -1;
    }
    if (csize == 0)
        return 1;
    st->usize = usize;
    st->csize = csize;
    if (csize < 3 || off > tab.n || tab.n - off < csize) {
        ge_seterr(err, "compressed block 0x%X + 0x%X is outside the ROM", (unsigned)off, (unsigned)csize);
        return -1;
    }
    if (usize < SEQ_HEADER_SIZE) {
        ge_seterr(err, "declared size %u is smaller than the sequence header", (unsigned)usize);
        return -1;
    }

    /* rarezip block: 2 header bytes, then raw DEFLATE */
    seq = (uint8_t *)malloc(usize);
    if (!seq) {
        ge_seterr(err, "out of memory");
        return -1;
    }
    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, -15) != Z_OK) {
        ge_seterr(err, "zlib init failed");
        goto done;
    }
    zs.next_in = (Bytef *)(uintptr_t)(tab.p + off + 2);
    zs.avail_in = csize - 2;
    zs.next_out = seq;
    zs.avail_out = usize;
    zr = inflate(&zs, Z_FINISH);
    if (zr != Z_STREAM_END || zs.total_out != usize) {
        ge_seterr(err, "inflate: %s (got %lu of %u bytes)", zs.msg ? zs.msg : (zr == Z_STREAM_END ? "size mismatch" : "bad stream"),
                  (unsigned long)zs.total_out, (unsigned)usize);
        inflateEnd(&zs);
        goto done;
    }
    inflateEnd(&zs);

    {
        ge_view sv;
        sv.p = seq;
        sv.n = usize;
        for (i = 0; i < SEQ_MAX_TRACKS; i++) {
            offsets[i] = ge_v_u32(sv, 4 * (size_t)i, &bad);
            if (offsets[i]) {
                if (offsets[i] < SEQ_HEADER_SIZE || offsets[i] >= usize) {
                    ge_seterr(err, "track %d offset 0x%X outside the sequence", i, (unsigned)offsets[i]);
                    goto done;
                }
                st->tracks++;
            }
        }
        division = ge_v_u32(sv, 64, &bad);
    }
    if (st->tracks == 0) {
        ge_seterr(err, "sequence has no tracks");
        goto done;
    }
    if (division == 0 || division > 0x7FFF) {
        ge_seterr(err, "division %u is not a ticks-per-quarter value", (unsigned)division);
        goto done;
    }
    st->division = division;

    for (i = 0; i < SEQ_MAX_TRACKS; i++) {
        if (!offsets[i])
            continue;
        uint32_t cut0 = st->overlaps_cut;
        if (parse_track(seq, usize, offsets[i], i, st, &ev[i], &end_tick[i], err)) {
            char tmp[GE_AE_ERRLEN];
            snprintf(tmp, sizeof tmp, "sequence track %d: %s", i, err);
            memcpy(err, tmp, GE_AE_ERRLEN);
            goto done;
        }
        if (resolve_note_ends(&ev[i], end_tick[i], st)) {
            ge_seterr(err, "out of memory");
            goto done;
        }
        st->overlaps_track[i] = st->overlaps_cut - cut0;
    }

    /* SMF header + one MTrk per present sequence track */
    if (ge_buf_put(&out, "MThd", 4) || ge_buf_be32(&out, 6) || ge_buf_be16(&out, 1) || ge_buf_be16(&out, (uint32_t)st->tracks) ||
        ge_buf_be16(&out, division))
        goto oom;
    for (i = 0; i < SEQ_MAX_TRACKS; i++) {
        if (!offsets[i])
            continue;
        if (write_mtrk(&out, &ev[i], end_tick[i], &st->length_ticks))
            goto oom;
    }
    *mid = out;
    out.data = NULL;
    rc = 0;
    goto done;
oom:
    ge_seterr(err, "out of memory");
done:
    for (i = 0; i < SEQ_MAX_TRACKS; i++)
        free(ev[i].v);
    free(seq);
    ge_ae_buf_free(&out);
    return rc;
}
