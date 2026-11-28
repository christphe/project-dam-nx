#ifndef MUSICFX_H
#define MUSICFX_H
/*
 * musicfx.h -- the effect (reverb) bus and the level laws of the pack music. Pure arithmetic: no SDL, no game, no synth,
 * no threads, no <math.h> (the game's include/ holds an N64 stub, D83). Behaviour: docs/specs/audio.md "Synth model"
 * (the effect bus, the voice level product, the pan law).
 *
 * The bus is ONE instance shared by the three music slots: they add their wet sends, the mono return is added at unity
 * to both output channels, and the tail keeps ringing after a slot stops.
 */
#ifdef __cplusplus
extern "C" {
#endif

#define MPFX_RATE 22050
#define MPFX_LINE 6400            /* the delay line, 290 ms at 22050 Hz */

/* A full-scale note (every factor at its maximum) reaches 0.9245 of its sample's peak at pan hard left or right. */
#define MPFX_FULL_SCALE 0.9245f

typedef struct MpfxBus {
    float line[MPFX_LINE];
    float lp[6];                  /* one-pole low-pass states, per section */
    int w;                        /* index of the newest sample ("delay 0") */
    int quiet;                    /* consecutive silent samples; the line is cleared once it is long enough */
    int idle;                     /* the line is clear and nothing went in */
} MpfxBus;

void mpfxBusInit(MpfxBus *b);
/* `aux` = n interleaved stereo frames of wet send (may be NULL: silence in, the tail still plays out). Writes n mono
 * frames of return to `mono` (not scaled; the caller adds it at unity). */
void mpfxBusProcess(MpfxBus *b, const float *aux, float *mono, int n);

/* CC91 `s` (0..127): per-voice dry and wet gains, constant power (cos / sin of s * pi / 254). */
float mpfxDry(int s);
float mpfxWet(int s);

/* Pan 0..127 (64 = centre): left cos(pan * pi / 254), right sin(pan * pi / 254). */
void mpfxPan(int pan, float *left, float *right);

/* The N64 law squares velocity and CC7 (40 x log10 dB). TinySoundFont gives velocity linearly and CC7 cubed:
 *   velocity: the synth applies v/127; the player multiplies the voice by one more factor v/127 (mpfxVelocityExtra), so the
 *             product is (v/127)^2. (Not a remapped velocity: that would also move the velocity layer a sound is on.)
 *   CC7: the synth applies x^3 with x = m / 16383 for its 14-bit channel volume m; the player sends m = mpfxCc7Fine(c)
 *        = round(16383 x (c/127)^(2/3)), so the gain is (c/127)^2. */
float mpfxVelocityExtra(int vel);
int mpfxCc7Fine(int cc7);
/* The synth's own CC7 law for a 14-bit value (mirror, for tests). */
float mpfxTsfCc7Gain(int m14);

#ifdef __cplusplus
}
#endif
#endif
