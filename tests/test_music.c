/* The pack music (docs/specs/audio.md "How a player loops a file with these markers", "Fades"): the sequencer on a MIDI
 * fixture built here (nested loops, a loop start without an end, a note across a jump, pause / resume, the natural end)
 * and the slot volume / fade arithmetic. */
#include "ge_test.h"
#include "musicseq.h"
#include "musicpack.h"
#include "texpack.h"
#include "ge_pkg.h"
#include "system.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* not <stdlib.h>: -Iinclude resolves it to the N64 stub (D83) */
void *malloc(size_t);
void free(void *);

/* ---- the asset stack, stubbed (musicpack.c reads through it) ----------------------------------------------------- */
static unsigned g_gen = 1;
static const unsigned char *g_mid;
static size_t g_midlen;
static int g_sf2Reads, g_jsonReads, g_reloads, g_missing;
int assetRead(const char *rel, unsigned char **out, unsigned long *len, int *from)
{
    static const char kJson[] = "{\n  \"default_volume\": [\n    26213, 29490\n  ]\n}\n";
    const void *src = NULL;
    size_t n = 0;
    (void)from;
    if (!strcmp(rel, "music/instruments.sf2")) {
        g_sf2Reads++; /* none in this stack: the music is silent */
        return 0;
    }
    if (!strcmp(rel, "music/music.json")) {
        g_jsonReads++;
        src = kJson;
        n = sizeof kJson - 1;
    } else if (!strcmp(rel, "music/track_07.mid") && g_mid) {
        src = g_mid;
        n = g_midlen;
    } else {
        return 0;
    }
    *out = (unsigned char *)malloc(n);
    memcpy(*out, src, n);
    *len = (unsigned long)n;
    return 1;
}
int assetHas(const char *rel) { (void)rel; return 0; }
unsigned assetStackGen(void) { return g_gen; }
void pkgFree(void *p) { free(p); }
void sysLogPrintf(enum LogLevel level, const char *fmt, ...)
{
    (void)level;
    if (strstr(fmt, "reloaded"))
        g_reloads++;
    if (strstr(fmt, "music: no "))
        g_missing++;
}

/* ---- a tiny SMF writer ------------------------------------------------------------------------------------------ */
typedef struct {
    unsigned char d[4096];
    int n;
} Buf;

typedef struct {
    uint32_t tick;
    int kind; /* 0 note on, 1 note off, 2 marker, 3 tempo */
    int ch, key;
    const char *text;
} Ev;

static void put(Buf *b, int v) { b->d[b->n++] = (unsigned char)v; }
static void put32(Buf *b, uint32_t v) { put(b, v >> 24); put(b, v >> 16); put(b, v >> 8); put(b, v); }
static void vlq(Buf *b, uint32_t v)
{
    unsigned char t[5];
    int n = 0;
    t[n++] = v & 0x7F;
    while ((v >>= 7))
        t[n++] = (unsigned char)(0x80 | (v & 0x7F));
    while (n--)
        put(b, t[n]);
}

/* events already in file order (tick order, note-offs first at a tick) */
static void track(Buf *b, const Ev *e, int n, uint32_t end)
{
    int at, i;
    uint32_t last = 0;
    memcpy(b->d + b->n, "MTrk", 4);
    b->n += 4;
    at = b->n;
    put32(b, 0);
    for (i = 0; i < n; i++) {
        vlq(b, e[i].tick - last);
        last = e[i].tick;
        if (e[i].kind == 0) {
            put(b, 0x90 | e[i].ch); put(b, e[i].key); put(b, 100);
        } else if (e[i].kind == 1) {
            put(b, 0x80 | e[i].ch); put(b, e[i].key); put(b, 0x40);
        } else if (e[i].kind == 2) {
            size_t l = strlen(e[i].text);
            put(b, 0xFF); put(b, 0x06); put(b, (int)l);
            memcpy(b->d + b->n, e[i].text, l);
            b->n += (int)l;
        }
    }
    vlq(b, end - last);
    put(b, 0xFF); put(b, 0x2F); put(b, 0);
    {
        uint32_t len = (uint32_t)(b->n - at - 4);
        b->d[at] = len >> 24; b->d[at + 1] = len >> 16; b->d[at + 2] = len >> 8; b->d[at + 3] = len;
    }
}

static void header(Buf *b, int ntrk)
{
    memcpy(b->d, "MThd", 4);
    b->n = 4;
    put32(b, 6);
    put(b, 0); put(b, 1);
    put(b, 0); put(b, ntrk);
    put(b, 384 >> 8); put(b, 384 & 0xFF);
}

/* ---- a recording sink ------------------------------------------------------------------------------------------------ */
#define MAXREC 256
static struct { long when; int on, key; } rec[MAXREC];
static int nrec;
static long wall; /* the test's own clock, in ticks of 1302 us (500000 / 384) */

static void onOn(void *u, int ch, int key, int vel) { (void)u; (void)ch; (void)vel; if (nrec < MAXREC) { rec[nrec].when = wall; rec[nrec].on = 1; rec[nrec++].key = key; } }
static void onOff(void *u, int ch, int key) { (void)u; (void)ch; if (nrec < MAXREC) { rec[nrec].when = wall; rec[nrec].on = 0; rec[nrec++].key = key; } }

static const MsqSink kSink = {NULL, onOn, onOff, NULL, NULL, NULL};

/* advance one tick at a time; events stamped with the tick they become due in */
static int run(MsqPlayer *p, long ticks)
{
    int r = 1;
    long i;
    for (i = 0; i < ticks && r; i++) {
        wall++;
        r = msqAdvance(p, 1302.0, &kSink);
    }
    return r;
}

static int find(int on, int key, int nth)
{
    int i;
    for (i = 0; i < nrec; i++)
        if (rec[i].on == on && rec[i].key == key && nth-- == 0)
            return (int)rec[i].when;
    return -1;
}

int main(void)
{
    static Buf b;
    char err[96];
    MsqSeq *s;
    static MsqPlayer p;

    GE_TEST_BEGIN("music");

    /* t0: outer forever loop 0..384 with an inner loop 96..192 repeated once (count 1: the body plays twice).
     * t1: a loop start with no loop end: plays once.
     * t2: a forever loop 0..384 with a note 300..500 crossing the loop end. */
    {
        static const Ev t0[] = {
            {0, 2, 0, 0, "loopStart 0"}, {0, 0, 0, 60, NULL},
            {96, 1, 0, 60, NULL}, {96, 2, 0, 0, "loopStart 1"}, {96, 0, 0, 62, NULL},
            {192, 1, 0, 62, NULL}, {192, 2, 0, 0, "loopEnd 1 1"}, {192, 0, 0, 64, NULL},
            {288, 1, 0, 64, NULL}, {384, 2, 0, 0, "loopEnd 0 inf"},
        };
        static const Ev t1[] = {
            {0, 2, 1, 0, "loopStart 0"}, {100, 0, 1, 70, NULL}, {110, 1, 1, 70, NULL},
        };
        static const Ev t2[] = {
            {0, 2, 2, 0, "loopStart 0"}, {300, 0, 2, 72, NULL}, {384, 2, 2, 0, "loopEnd 0 inf"}, {500, 1, 2, 72, NULL},
        };
        header(&b, 3);
        track(&b, t0, (int)(sizeof t0 / sizeof t0[0]), 384);
        track(&b, t1, (int)(sizeof t1 / sizeof t1[0]), 200);
        track(&b, t2, (int)(sizeof t2 / sizeof t2[0]), 600);
    }
    s = msqLoad(b.d, (size_t)b.n, err, sizeof err);
    GE_CHECK(s != NULL);
    if (!s)
        return GE_TEST_END();
    GE_CHECK_EQ_INT(s->ntracks, 3);
    GE_CHECK_EQ_INT(s->ncounters, 3);
    GE_CHECK_EQ_INT(s->unresolved, 0);

    nrec = 0;
    wall = 0;
    GE_CHECK(msqStart(&p, s));
    GE_CHECK_EQ_INT(run(&p, 1300), 1); /* forever loops: never ends */

    /* t0: 60@0, 62@96, 62@192 (the repeat), 64@288, then the outer loop: 60@480, 62@576, 62@672, 64@768, 60@960 */
    GE_CHECK_EQ_INT(find(1, 60, 0), 1);   /* due 0, stamped in the first step */
    GE_CHECK_EQ_INT(find(1, 62, 0), 96);
    GE_CHECK_EQ_INT(find(1, 62, 1), 192);
    GE_CHECK_EQ_INT(find(1, 64, 0), 288);
    GE_CHECK_EQ_INT(find(1, 62, 2), 576); /* the inner counter was reset when it fell through */
    GE_CHECK_EQ_INT(find(1, 62, 3), 672);
    GE_CHECK_EQ_INT(find(1, 64, 1), 768);
    GE_CHECK_EQ_INT(find(1, 60, 1), 480);
    GE_CHECK_EQ_INT(find(1, 60, 2), 960);
    GE_CHECK_EQ_INT(find(1, 62, 4), 1056);
    /* notes end by their duration */
    GE_CHECK_EQ_INT(find(0, 62, 0), 192);
    GE_CHECK_EQ_INT(find(0, 64, 0), 384);
    /* t1: once */
    GE_CHECK_EQ_INT(find(1, 70, 0), 100);
    GE_CHECK_EQ_INT(find(1, 70, 1), -1);
    GE_CHECK_EQ_INT(find(0, 70, 0), 110);
    /* t2: the note crossing the loop end rings on into the next pass and ends at 500; the next one starts at 684 */
    GE_CHECK_EQ_INT(find(1, 72, 0), 300);
    GE_CHECK_EQ_INT(find(0, 72, 0), 500);
    GE_CHECK_EQ_INT(find(1, 72, 1), 684);
    GE_CHECK_EQ_INT(find(0, 72, 1), 884);

    /* pause at 150 (the last event dispatched: 70@100), 1000 ticks of silence, resume: the next event (62@192) waits its
     * whole gap (92) from the resume; the held 62 gets no note-off (the synth cut it) */
    nrec = 0;
    wall = 0;
    GE_CHECK(msqStart(&p, s));
    run(&p, 150);
    msqPause(&p);
    wall += 1000;
    msqResume(&p);
    run(&p, 200);
    GE_CHECK_EQ_INT(find(1, 62, 1), 1150 + 92);
    GE_CHECK_EQ_INT(find(0, 62, 0), 1150 + 92 + 96); /* only the new note's own note-off */
    GE_CHECK_EQ_INT(find(1, 70, 0), 100);

    /* stop + start again: the counters come back from the file */
    nrec = 0;
    wall = 0;
    GE_CHECK(msqStart(&p, s));
    run(&p, 300);
    GE_CHECK_EQ_INT(find(1, 62, 1), 192);
    msqStopPlayer(&p);
    msqFree(s);

    /* no loop at all: the sequence ends once the last note-off fired */
    {
        static const Ev t0[] = {{0, 0, 0, 50, NULL}, {40, 1, 0, 50, NULL}};
        header(&b, 1);
        track(&b, t0, 2, 40);
        s = msqLoad(b.d, (size_t)b.n, err, sizeof err);
        GE_CHECK(s != NULL);
        if (s) {
            nrec = 0;
            wall = 0;
            GE_CHECK(msqStart(&p, s));
            GE_CHECK_EQ_INT(run(&p, 100), 0);
            GE_CHECK_EQ_INT(find(0, 50, 0), 40);
            GE_CHECK_EQ_INT((int)wall, 40);
            msqStopPlayer(&p);
            msqFree(s);
        }
    }

    /* an older pack's bare markers: played straight through, no loop */
    {
        static const Ev t0[] = {{0, 2, 0, 0, "loopStart"}, {0, 0, 0, 50, NULL}, {40, 1, 0, 50, NULL}, {48, 2, 0, 0, "loopEnd"}};
        header(&b, 1);
        track(&b, t0, 4, 48);
        s = msqLoad(b.d, (size_t)b.n, err, sizeof err);
        GE_CHECK(s != NULL);
        if (s) {
            GE_CHECK_EQ_INT(s->ncounters, 0);
            msqFree(s);
        }
    }

    /* slot volume and fades (no stack: the slots are silent, the arithmetic is the same) */
    {
        int st1 = 0, st2 = 0, st3 = 0, i;
        int *const st[3] = {&st1, &st2, &st3};
        mpkSetVolume(0, 0x7FFF);
        mpkFadeOut(0, 0.5f, &st1);
        GE_CHECK_EQ_INT(st1, -1);
        mpkFadeTick(st);
        GE_CHECK_EQ_INT((int)mpkGetVolume(0), 32767 - 32767 / 30);
        mpkFadeOut(0, 2.0f, &st1); /* ignored: a fade-out is running */
        for (i = 1; i < 30; i++)
            mpkFadeTick(st);
        GE_CHECK_EQ_INT((int)mpkGetVolume(0), 0);
        GE_CHECK_EQ_INT(st1, 0);
        mpkFadeIn(0, 0.5f, 0xFFFF, &st1); /* back to the pre-fade volume */
        GE_CHECK_EQ_INT(st1, 1);
        for (i = 0; i < 30; i++)
            mpkFadeTick(st);
        GE_CHECK_EQ_INT((int)mpkGetVolume(0), 0x7FFF);
        GE_CHECK_EQ_INT(st1, 0);
        /* the game cancels a fade by clearing the state */
        mpkFadeOut(0, 1.0f, &st1);
        st1 = 0;
        mpkFadeTick(st);
        GE_CHECK_EQ_INT((int)mpkGetVolume(0), 0x7FFF);
        mpkStop(1, &st2);
        GE_CHECK_EQ_INT(st2, 0);
    }

    /* the stack: loaded at the first play, reloaded when its generation changes; a missing track or SoundFont is silence
     * and one log line (again after a reload); rendering a slot with no SoundFont is a no-op */
    {
        static short pcm[512 * 2];
        static const Ev t0[] = {{0, 2, 0, 0, "loopStart 0"}, {0, 0, 0, 50, NULL}, {40, 1, 0, 50, NULL}, {48, 2, 0, 0, "loopEnd 0 inf"}};
        int st1 = 0;
        header(&b, 1);
        track(&b, t0, 4, 48);
        g_mid = b.d;
        g_midlen = (size_t)b.n;
        mpkPlay(0, 7);
        GE_CHECK_EQ_INT(g_sf2Reads, 1);
        GE_CHECK_EQ_INT(g_jsonReads, 1);
        GE_CHECK_EQ_INT(g_reloads, 0);
        mpkPlay(2, 9); /* no track_09.mid: one warning */
        mpkPlay(2, 9); /* not twice */
        GE_CHECK_EQ_INT(g_missing, 2); /* the SoundFont, then track 9 */
        GE_CHECK_EQ_INT(g_sf2Reads, 1); /* same generation: not reloaded */
        mpkMix(pcm, 512);
        GE_CHECK_EQ_INT(pcm[0], 0);
        g_gen = 2; /* a pack enabled / moved, or another mod */
        mpkPlay(0, 7);
        GE_CHECK_EQ_INT(g_reloads, 1);
        GE_CHECK_EQ_INT(g_sf2Reads, 2);
        GE_CHECK_EQ_INT(g_jsonReads, 2);
        mpkPlay(2, 9);
        GE_CHECK_EQ_INT(g_missing, 4); /* both warned again for the new stack */
        mpkMix(pcm, 512);
        mpkStop(0, &st1);
        mpkStop(2, &st1);
        mpkMix(pcm, 512);
        GE_CHECK_EQ_INT(pcm[100], 0);
    }
    return GE_TEST_END();
}
