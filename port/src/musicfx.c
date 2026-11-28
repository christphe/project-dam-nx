/*
 * musicfx.c -- the effect bus and the level laws of the pack music (port/include/musicfx.h). Behaviour from
 * docs/specs/audio.md "Synth model": "The effect (reverb) bus", "Voice level: the exact product", "Pan law".
 * No <math.h>: the game's include/ has an N64 stub of it (D83), so the compiler's builtins.
 */
#include <string.h>

#include "musicfx.h"

#define PI_F 3.14159265358979f

/* One section of the bus: tap delays a (shallow) and b (deep) counted from the newest sample, the all-pass coefficient
 * g (feed-back +g, feed-forward -g), the output gain og, the one-pole low-pass c (0 = none). Numerators over 32768 are
 * the spec's integers. Section 6 stores nothing at b and has no output. */
typedef struct {
    int a, b;
    float g, og, c;
} Section;

static const Section kSec[6] = {
    {0, 160, 9830 / 32768.0f, 0.0f, 0.0f},
    {160, 320, 9830 / 32768.0f, 11140 / 32768.0f, 0x2500 / 32768.0f},
    {800, 2560, 16384 / 32768.0f, 4587 / 32768.0f, 0x3000 / 32768.0f},
    {3200, 5600, 16384 / 32768.0f, 4587 / 32768.0f, 0x3500 / 32768.0f},
    {3360, 4800, 8192 / 32768.0f, 0.0f, 0x4000 / 32768.0f},
    {0, 5920, 13000 / 32768.0f, 0.0f, 0.0f},   /* the "chorus" section: a fixed tap, its modulation is inaudible */
};

#define QUIET_LEVEL 1e-9f        /* ~ -180 dB: below this the tail is gone */
#define QUIET_SAMPLES (MPFX_LINE + 2048)

void mpfxBusInit(MpfxBus *b)
{
    memset(b, 0, sizeof *b);
    b->idle = 1;
}

static inline float tap(const MpfxBus *b, int d)
{
    int i = b->w - d;
    if (i < 0)
        i += MPFX_LINE;
    return b->line[i];
}

static inline void put(MpfxBus *b, int d, float v)
{
    int i = b->w - d;
    if (i < 0)
        i += MPFX_LINE;
    b->line[i] = v;
}

static float step(MpfxBus *bus, float x)
{
    float out = 0.0f;
    int k;
    if (++bus->w == MPFX_LINE)
        bus->w = 0;
    bus->line[bus->w] = x; /* delay 0 */
    for (k = 0; k < 6; k++) {
        const Section *s = &kSec[k];
        float u = tap(bus, s->a), v = tap(bus, s->b);
        float v1 = v - s->g * u;
        float u1 = u + s->g * v1;
        float v2 = v1;
        put(bus, s->a, u1);
        if (k == 5)
            break;
        if (s->c > 0.0f) {
            v2 = (1.0f - s->c) * v1 + s->c * bus->lp[k];
            bus->lp[k] = v2;
        }
        put(bus, s->b, v2);
        out += s->og * v2;
    }
    return out;
}

void mpfxBusProcess(MpfxBus *b, const float *aux, float *mono, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        float x = aux ? 0.7071f * (aux[2 * i] + aux[2 * i + 1]) : 0.0f; /* 0.70713 L + 0.70709 R */
        float y, a;
        if (x != 0.0f)
            b->idle = 0;
        if (b->idle) {
            mono[i] = 0.0f;
            continue;
        }
        y = step(b, x);
        mono[i] = y;
        a = (y < 0 ? -y : y) + (x < 0 ? -x : x);
        if (a < QUIET_LEVEL) {
            if (++b->quiet >= QUIET_SAMPLES) { /* the tail has played out: clear it (no denormals), stop working */
                memset(b->line, 0, sizeof b->line);
                memset(b->lp, 0, sizeof b->lp);
                b->quiet = 0;
                b->idle = 1;
            }
        } else {
            b->quiet = 0;
        }
    }
}

float mpfxDry(int s)
{
    s = s < 0 ? 0 : s > 127 ? 127 : s;
    return __builtin_cosf(s * PI_F / 254.0f);
}

float mpfxWet(int s)
{
    s = s < 0 ? 0 : s > 127 ? 127 : s;
    return __builtin_sinf(s * PI_F / 254.0f);
}

void mpfxPan(int pan, float *left, float *right)
{
    *left = mpfxDry(pan);
    *right = mpfxWet(pan);
}

float mpfxVelocityExtra(int vel)
{
    return (vel < 0 ? 0 : vel > 127 ? 127 : vel) / 127.0f;
}

int mpfxCc7Fine(int cc7)
{
    float x;
    int m;
    cc7 = cc7 < 0 ? 0 : cc7 > 127 ? 127 : cc7;
    x = __builtin_powf(cc7 / 127.0f, 2.0f / 3.0f);
    m = (int)(16383.0f * x + 0.5f);
    return m > 16383 ? 16383 : m;
}

float mpfxTsfCc7Gain(int m14)
{
    float x = m14 / 16383.0f;
    return x * x * x;
}
