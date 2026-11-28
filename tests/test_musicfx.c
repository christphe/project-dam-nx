/* The pack music's effect bus and level laws (docs/specs/audio.md "Synth model"): the pure arithmetic of port/src/musicfx.c
 * (E(i) table, dry / wet curves, pan law, the velocity and CC7 compensation for TinySoundFont's own laws, the reverb bus
 * impulse response) and the whole chain through musicpack.c on a SoundFont built here (one DC sample, so a note's level is
 * read straight off the output): base gain, velocity, CC7, pan, CC91 dry / wet, the linear decay and release. */
#include "ge_test.h"
#include "musicpack.h"
#include "musicfx.h"
#include "texpack.h"
#include "ge_pkg.h"
#include "system.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* not <stdlib.h>: -Iinclude resolves it to the N64 stub (D83) */
void *malloc(size_t);
void *realloc(void *, size_t);
void free(void *);
char *getenv(const char *);

/* ---- a SoundFont and a MIDI file built here --------------------------------------------------------------------- */
typedef struct {
    unsigned char *d;
    size_t n, cap;
} Buf;

static void bput(Buf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->d = (unsigned char *)realloc(b->d, b->cap);
    }
    memcpy(b->d + b->n, p, n);
    b->n += n;
}
static void b8(Buf *b, int v) { unsigned char c = (unsigned char)v; bput(b, &c, 1); }
static void b16(Buf *b, int v) { b8(b, v); b8(b, v >> 8); }
static void b32(Buf *b, uint32_t v) { b16(b, (int)(v & 0xFFFF)); b16(b, (int)(v >> 16)); }
static void bname(Buf *b, const char *s)
{
    char n[20];
    memset(n, 0, sizeof n);
    strncpy(n, s, 19);
    bput(b, n, 20);
}
static void bpatch32(Buf *b, size_t at, uint32_t v)
{
    b->d[at] = (unsigned char)v; b->d[at + 1] = (unsigned char)(v >> 8); b->d[at + 2] = (unsigned char)(v >> 16); b->d[at + 3] = (unsigned char)(v >> 24);
}
static size_t chunk(Buf *b, const char *id)
{
    bput(b, id, 4);
    b32(b, 0);
    return b->n - 4;
}
static void endchunk(Buf *b, size_t at) { bpatch32(b, at, (uint32_t)(b->n - at - 4)); }

#define SF_SAMPLES 256
/* gens: {oper, amount} */
typedef struct { int oper, amount; } Gen;
enum { G_DECAY = 36, G_SUSTAIN = 37, G_RELEASE = 38, G_INSTRUMENT = 41, G_ATTENUATION = 48, G_SAMPLEID = 53, G_SAMPLEMODES = 54 };

/* Two presets (bank 0): 0 = plain, 1 = decay 0.2 s to -6 dB sustain with a 0.2 s release. One sample: a constant 16384
 * (0.5 of full scale), looped, so every note is a flat line at its level. */
static void buildSf2(Buf *o)
{
    static const Gen z0[] = {{G_SAMPLEMODES, 1}, {G_SAMPLEID, 0}};
    static Gen z1[5];
    size_t riff, list, ck;
    int i;
    z1[0].oper = G_DECAY; z1[0].amount = (int)lrintf(1200.0f * log2f(0.2f));
    z1[1].oper = G_SUSTAIN; z1[1].amount = 60;
    z1[2].oper = G_RELEASE; z1[2].amount = (int)lrintf(1200.0f * log2f(0.2f));
    z1[3].oper = G_SAMPLEMODES; z1[3].amount = 1;
    z1[4].oper = G_SAMPLEID; z1[4].amount = 0;
    riff = chunk(o, "RIFF");
    bput(o, "sfbk", 4);
    list = chunk(o, "LIST");
    bput(o, "INFO", 4);
    ck = chunk(o, "ifil"); b16(o, 2); b16(o, 4); endchunk(o, ck);
    ck = chunk(o, "INAM"); bput(o, "test\0", 5); b8(o, 0); endchunk(o, ck);
    endchunk(o, list);
    list = chunk(o, "LIST");
    bput(o, "sdta", 4);
    ck = chunk(o, "smpl");
    for (i = 0; i < SF_SAMPLES + 46; i++)
        b16(o, i < SF_SAMPLES ? 16384 : 0);
    endchunk(o, ck);
    endchunk(o, list);
    list = chunk(o, "LIST");
    bput(o, "pdta", 4);
    ck = chunk(o, "phdr");
    for (i = 0; i < 2; i++) { bname(o, "p"); b16(o, i); b16(o, 0); b16(o, i); b32(o, 0); b32(o, 0); b32(o, 0); }
    bname(o, "EOP"); b16(o, 0); b16(o, 0); b16(o, 2); b32(o, 0); b32(o, 0); b32(o, 0);
    endchunk(o, ck);
    ck = chunk(o, "pbag");
    for (i = 0; i <= 2; i++) { b16(o, i); b16(o, 0); }
    endchunk(o, ck);
    ck = chunk(o, "pmod");
    for (i = 0; i < 10; i++) b8(o, 0);
    endchunk(o, ck);
    ck = chunk(o, "pgen");
    for (i = 0; i < 2; i++) { b16(o, G_INSTRUMENT); b16(o, i); }
    b16(o, 0); b16(o, 0);
    endchunk(o, ck);
    ck = chunk(o, "inst");
    bname(o, "i0"); b16(o, 0);
    bname(o, "i1"); b16(o, 1);
    bname(o, "EOI"); b16(o, 2);
    endchunk(o, ck);
    ck = chunk(o, "ibag");
    b16(o, 0); b16(o, 0);
    b16(o, 2); b16(o, 0);
    b16(o, 7); b16(o, 0);
    endchunk(o, ck);
    ck = chunk(o, "imod");
    for (i = 0; i < 10; i++) b8(o, 0);
    endchunk(o, ck);
    ck = chunk(o, "igen");
    for (i = 0; i < 2; i++) { b16(o, z0[i].oper); b16(o, z0[i].amount); }
    for (i = 0; i < 5; i++) { b16(o, z1[i].oper); b16(o, z1[i].amount); }
    b16(o, 0); b16(o, 0);
    endchunk(o, ck);
    ck = chunk(o, "shdr");
    bname(o, "s"); b32(o, 0); b32(o, SF_SAMPLES); b32(o, 16); b32(o, 240); b32(o, 22050); b8(o, 60); b8(o, 0); b16(o, 0); b16(o, 1);
    bname(o, "EOS"); for (i = 0; i < 26; i++) b8(o, 0);
    endchunk(o, ck);
    endchunk(o, list);
    endchunk(o, riff);
}

static void bvlq(Buf *b, uint32_t v)
{
    unsigned char t[5];
    int n = 0;
    t[n++] = v & 0x7F;
    while ((v >>= 7))
        t[n++] = (unsigned char)(0x80 | (v & 0x7F));
    while (n--)
        b8(b, t[n]);
}

/* A one-track SMF (division 384): `ev` = bytes of channel events, each preceded by its delta; built by the helpers below. */
static void mev(Buf *t, uint32_t delta, int st, int d1, int d2)
{
    bvlq(t, delta);
    b8(t, st);
    b8(t, d1);
    if ((st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0)
        b8(t, d2);
}
static void smf(Buf *file, const Buf *trk, uint32_t end_delta)
{
    size_t at;
    bput(file, "MThd", 4);
    b8(file, 0); b8(file, 0); b8(file, 0); b8(file, 6);
    b8(file, 0); b8(file, 0); b8(file, 0); b8(file, 1); b8(file, 1); b8(file, 0x80);
    bput(file, "MTrk", 4);
    at = file->n;
    b8(file, 0); b8(file, 0); b8(file, 0); b8(file, 0);
    bput(file, trk->d, trk->n);
    bvlq(file, end_delta);
    b8(file, 0xFF); b8(file, 0x2F); b8(file, 0);
    {
        uint32_t len = (uint32_t)(file->n - at - 4);
        file->d[at] = (unsigned char)(len >> 24); file->d[at + 1] = (unsigned char)(len >> 16);
        file->d[at + 2] = (unsigned char)(len >> 8); file->d[at + 3] = (unsigned char)len;
    }
}

/* ---- the asset stack, stubbed ------------------------------------------------------------------------------------ */
static Buf g_sf2, g_mid;
int assetRead(const char *rel, unsigned char **out, unsigned long *len, int *from)
{
    static const char kJson[] = "{\n  \"default_volume\": [\n    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767\n  ]\n}\n";
    const void *src = NULL;
    size_t n = 0;
    (void)from;
    if (!strcmp(rel, "music/instruments.sf2")) {
        src = g_sf2.d;
        n = g_sf2.n;
    } else if (!strcmp(rel, "music/music.json")) {
        src = kJson;
        n = sizeof kJson - 1;
    } else if (!strcmp(rel, "music/track_07.mid")) {
        src = g_mid.d;
        n = g_mid.n;
    } else {
        return 0;
    }
    *out = (unsigned char *)malloc(n);
    memcpy(*out, src, n);
    *len = (unsigned long)n;
    return 1;
}
int assetHas(const char *rel) { (void)rel; return 0; }
unsigned assetStackGen(void) { return 1; }
void pkgFree(void *p) { free(p); }
void sysLogPrintf(enum LogLevel level, const char *fmt, ...) { (void)level; (void)fmt; }

/* ---- measurements ------------------------------------------------------------------------------------------------ */
static double dB(double r) { return 20.0 * log10(r); }

/* Play one held note on channel 0 with the given controller values and return the peak of |L| and |R| over frames
 * [from, to) of the render (the sample is a flat line, so the peak is the note's level). */
typedef struct { int prog, cc7, cc10, cc91, vel, off_ticks; } Note;
static short g_pcm[4096 * 2];
static void playNote(const Note *nt, int frames)
{
    Buf t = {0};
    int i;
    mev(&t, 0, 0xC0, nt->prog, 0);
    mev(&t, 0, 0xB0, 10, nt->cc10);
    mev(&t, 0, 0xB0, 7, nt->cc7);
    mev(&t, 0, 0xB0, 91, nt->cc91);
    mev(&t, 0, 0x90, 60, nt->vel);
    mev(&t, (uint32_t)nt->off_ticks, 0x80, 60, 0x40);
    g_mid.n = 0;
    smf(&g_mid, &t, 0);
    free(t.d);
    mpkPlay(0, 7);
    memset(g_pcm, 0, sizeof g_pcm);
    for (i = 0; i < frames; i += 16)
        mpkMix(g_pcm + 2 * i, 16);
}
static int peak(int ch, int from, int to)
{
    int i, p = 0;
    for (i = from; i < to; i++) {
        int v = g_pcm[2 * i + ch];
        v = v < 0 ? -v : v;
        if (v > p)
            p = v;
    }
    if (getenv("GE_TEST_DEBUG"))
        printf("  peak ch%d [%d,%d) = %d\n", ch, from, to, p);
    return p;
}
static void silence(void)
{
    int st = 0, i;
    static short drain[16 * 2];
    mpkStop(0, &st);
    for (i = 0; i < 1400; i += 16) /* the stop's 50 ms release has to run before the next play starts */
        mpkMix(drain, 16);
    g_mid.n = 0;
}

int main(void)
{
    int i, s;
    GE_TEST_BEGIN("musicfx");

    /* the equal-power table E(i) = 32767 cos(i pi / 254) and the dry / wet curves */
    GE_CHECK(fabs(mpfxDry(0) * 32767.0 - 32767.0) < 1.0);
    GE_CHECK(fabs(mpfxDry(64) * 32767.0 - 23026.0) < 2.0);
    GE_CHECK(fabs(mpfxDry(63) * 32767.0 - 23313.0) < 2.0);
    GE_CHECK(fabs(mpfxDry(127) * 32767.0) < 1.0);
    GE_CHECK(fabs(mpfxDry(0) - 1.0) < 1e-6 && fabs(mpfxWet(0)) < 1e-6);                    /* CC91 = 0: all dry */
    GE_CHECK(fabs(mpfxDry(64) - 0.7027) < 1e-3 && fabs(mpfxWet(64) - 0.7115) < 1e-3);      /* 64: both about -3 dB */
    GE_CHECK(fabs(mpfxDry(127)) < 1e-6 && fabs(mpfxWet(127) - 1.0) < 1e-6);                /* 127: no dry signal */
    for (s = 0; s < 128; s++) {
        float l, r;
        mpfxPan(s, &l, &r);
        GE_CHECK(fabs(l * l + r * r - 1.0) < 1e-5);                                         /* constant power */
        GE_CHECK(fabs(l - mpfxDry(s)) < 1e-6 && fabs(r - mpfxWet(s)) < 1e-6);
    }

    /* velocity: the synth gives v/127 (dB: 20 log10), the player one more factor v/127 -> 40 log10(v/127) */
    for (i = 1; i <= 127; i++) {
        double synth = dB(i / 127.0), total = synth + dB(mpfxVelocityExtra(i));
        GE_CHECK(fabs(total - 40.0 * log10(i / 127.0)) < 1e-4);
    }
    GE_CHECK(fabs(40.0 * log10(64 / 127.0) + 11.9) < 0.05);                                /* the spec's -11.9 dB */
    /* CC7: the synth cubes its 14-bit channel volume; the compensated value gives (c/127)^2 within 0.05 dB */
    GE_CHECK_EQ_INT(mpfxCc7Fine(127), 16383);
    GE_CHECK_EQ_INT(mpfxCc7Fine(0), 0);
    for (i = 1; i <= 127; i++) {
        double want = (i / 127.0) * (i / 127.0), got = mpfxTsfCc7Gain(mpfxCc7Fine(i));
        GE_CHECK(fabs(dB(got / want)) < 0.05);
    }
    /* what the synth would do without the compensation (so the test would notice a dropped remap): cubed, 64 -> -17.9 dB */
    GE_CHECK(fabs(dB(mpfxTsfCc7Gain((64 << 7) | 127)) + 17.7) < 0.3);

    /* the effect bus: one impulse in (both channels), the response at the spec's lags */
    {
        static MpfxBus bus;
        static float h[40000];
        float aux[32], mx = 0;
        static const int lags[] = {160, 320, 1440, 1760, 2400, 5920};
        int k, j;
        mpfxBusInit(&bus);
        for (i = 0; i < 40000; i += 16) {
            memset(aux, 0, sizeof aux);
            if (!i) { aux[0] = 1.0f; aux[1] = 1.0f; }
            mpfxBusProcess(&bus, aux, h + i, 16);
        }
        for (i = 0; i < 40000; i++)
            if (fabsf(h[i]) > mx) mx = fabsf(h[i]);
        /* zero-latency path: 0.7071 (L + R) = 1.414 in, x g1 g2 = 0.09, x (1 - c) = 0.711, x 0.34 */
        GE_CHECK(fabs(h[0] - 1.4142 * 0.3 * 0.3 * (1 - 0x2500 / 32768.0) * 0.34) < 0.002);
        for (j = 0; j < 6; j++) {
            float p = 0, gap = 0;
            for (k = lags[j] - 6; k <= lags[j] + 12; k++)
                if (fabsf(h[k]) > p) p = fabsf(h[k]);
            for (k = lags[j] - 60; k < lags[j] - 30; k++) /* the line is sparse: the stretch before an echo is empty */
                if (fabsf(h[k]) > gap) gap = fabsf(h[k]);
            GE_CHECK(p > 2e-3f);
            GE_CHECK(gap < 1e-6f);
        }
        GE_CHECK(mx < 0.5f);
        { /* the tail decays on its own: nothing of note long after the 6400-sample line (feed-back 0.4 per 268 ms) */
            float tail = 0;
            for (i = 20000; i < 40000; i++)
                if (fabsf(h[i]) > tail) tail = fabsf(h[i]);
            GE_CHECK(tail < 0.02f * mx);
        }
        /* a silent bus clears itself and returns 0 */
        for (i = 0; i < 400000; i += 16) {
            float z[16];
            mpfxBusProcess(&bus, NULL, z, 16);
            if (i == 399984)
                GE_CHECK(z[0] == 0.0f && bus.idle);
        }
    }

    /* the whole chain through musicpack.c on a flat-line SoundFont */
    buildSf2(&g_sf2);
    {
        Note n = {0, 127, 0, 0, 127, 3000};
        int a, b;
        const double full = 0.9245 * 16384.0 / 32768.0 * 32767.0; /* base gain: 0.9245 of the sample, hard left */
        /* dry cases first: the bus (shared) stays silent, so no tail pollutes the next case */
        playNote(&n, 256);
        a = peak(0, 64, 256);
        GE_CHECK(fabs(a / full - 1.0) < 0.02);
        GE_CHECK(peak(1, 64, 256) < 8);                                            /* pan 0: left only */
        silence();
        n.vel = 64;
        playNote(&n, 256);
        b = peak(0, 64, 256);
        GE_CHECK(fabs(b / (double)a - (64 / 127.0) * (64 / 127.0)) < 0.004);       /* velocity squared */
        silence();
        n.vel = 127; n.cc7 = 64;
        playNote(&n, 256);
        b = peak(0, 64, 256);
        GE_CHECK(fabs(b / (double)a - (64 / 127.0) * (64 / 127.0)) < 0.004);       /* CC7 squared */
        silence();
        n.cc7 = 127; n.cc10 = 64;
        playNote(&n, 256);
        GE_CHECK(fabs(peak(0, 64, 256) / (double)a - 0.7027) < 0.004);             /* centre: cos / sin of 64 pi / 254 */
        GE_CHECK(fabs(peak(1, 64, 256) / (double)a - 0.7115) < 0.004);
        silence();
        n.cc10 = 127;
        playNote(&n, 256);
        GE_CHECK(fabs(peak(1, 64, 256) / (double)a - 1.0) < 0.004 && peak(0, 64, 256) < 8);
        silence();
        /* linear envelope: preset 1 decays 0.2 s to -6 dB (0.5), straight in amplitude: 0.75 half way. An exponential decay
         * (TinySoundFont's own) would be far lower there. */
        n.cc10 = 0; n.prog = 1; n.off_ticks = 384 * 4;
        playNote(&n, 4096);
        GE_CHECK(fabs(peak(0, 2205 - 16, 2205 + 16) / (double)a - 0.75) < 0.03);   /* 0.1 s into the decay */
        GE_CHECK(fabs(peak(0, 3500, 3516) / (double)a - 0.60) < 0.02);             /* 0.16 s: 1 - 0.5 x 0.8 */
        silence();
    }
    /* the release: a short note, then the line down to 0 */
    {
        Note n = {1, 127, 0, 0, 127, 0};
        double a_sus;
        int frames = 12288;
        static short big[12288 * 2];
        Buf t = {0};
        int k;
        /* note on at 0, off at 0.3 s (decay over, level 0.5), then 0.2 s release */
        mev(&t, 0, 0xC0, n.prog, 0);
        mev(&t, 0, 0xB0, 10, 0);
        mev(&t, 0, 0x90, 60, 127);
        mev(&t, 230, 0x80, 60, 0x40); /* 230 ticks x 1302 us = 0.2995 s */
        g_mid.n = 0;
        smf(&g_mid, &t, 384 * 2); /* the track ends a second later: the end of a sequence cuts what still rings */
        free(t.d);
        mpkPlay(0, 7);
        memset(big, 0, sizeof big);
        for (k = 0; k < frames; k += 16)
            mpkMix(big + 2 * k, 16);
        {
            int on = 0, off = (int)(0.2995 * 22050);
            int win, p = 0;
            (void)on;
            for (win = off - 200; win < off - 100; win++)
                if (abs(big[2 * win]) > p) p = abs(big[2 * win]);
            a_sus = p;
            p = 0;
            for (win = off + 2205 - 16; win < off + 2205 + 16; win++) /* 0.1 s into the 0.2 s release */
                if (abs(big[2 * win]) > p) p = abs(big[2 * win]);
            if (getenv("GE_TEST_DEBUG"))
                printf("  release: sustain %g, 0.1 s in %d\n", a_sus, p);
            GE_CHECK(fabs(a_sus / 15145.0 - 0.50) < 0.02);                         /* sustain level: 10^(-60/200) */
            GE_CHECK(fabs(p / a_sus - 0.5) < 0.05);
            p = 0;
            for (win = off + 4600; win < off + 5000; win++)
                if (abs(big[2 * win]) > p) p = abs(big[2 * win]);
            GE_CHECK(p < 8);
        }
        silence();
    }
    /* CC91: 64 -> dry at 0.7027 now; 127 -> no dry at the onset, the reverb arrives later */
    {
        Note n = {0, 127, 0, 0, 127, 3000};
        int a, b, tail;
        playNote(&n, 256);
        a = peak(0, 64, 150);
        silence();
        n.cc91 = 64;
        playNote(&n, 150);
        b = peak(0, 64, 150);
        GE_CHECK(fabs(b / (double)a - 0.7027) < 0.05);                              /* dry 0.7027 (+ the bus's first 3 %) */
        silence();
        n.cc91 = 127;
        playNote(&n, 4096);
        GE_CHECK(peak(0, 8, 150) < 0.1 * a);                                        /* no dry at the onset */
        tail = peak(0, 3000, 4000);
        GE_CHECK(tail > 0.2 * a);                                                   /* the return is there, at unity */
        silence();
    }

    /* cost: 16 channels x 4 notes, CC91 0 or 127 (GE_TEST_BENCH=1 prints it; a number, not a check) */
    if (getenv("GE_TEST_BENCH")) {
        int pass;
        for (pass = 0; pass < 2; pass++) {
            Buf t = {0};
            clock_t c0;
            int ch, k, frames = 22050 * 4;
            static short pcm[16 * 2];
            for (ch = 0; ch < 16; ch++) {
                mev(&t, 0, 0xC0 | ch, 0, 0);
                mev(&t, 0, 0xB0 | ch, 91, pass ? 127 : 0);
            }
            for (ch = 0; ch < 16; ch++)
                for (k = 0; k < 4; k++)
                    mev(&t, 0, 0x90 | ch, 48 + k * 4, 100);
            g_mid.n = 0;
            smf(&g_mid, &t, 384 * 20);
            free(t.d);
            mpkPlay(0, 7);
            c0 = clock();
            for (i = 0; i < frames; i += 16)
                mpkMix(pcm, 16);
            printf("bench: 64 voices, CC91 %s: %.1f ms per second of audio\n", pass ? "127 (split + bus)" : "0 (dry only)",
                   1000.0 * (double)(clock() - c0) / CLOCKS_PER_SEC / 4.0);
            silence();
        }
    }
    return GE_TEST_END();
}
