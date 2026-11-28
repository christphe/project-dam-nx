/*
 * musicpack.c -- the game's music from the asset packs (port/include/musicpack.h). Behaviour from docs/specs/audio.md
 * ("Music playback", "How a player loops a file with these markers"); the sequencer is musicseq.c, the synth TinySoundFont
 * (port/third_party/tinysoundfont, compiled here and nowhere else in the game).
 *
 * Threads: the game thread loads (assetRead) and posts requests; the retrace runs the fades; the audio thread renders
 * (mpkMix, called by audio.c for every buffer the game hands to the device). All slot state is under one lock; the audio
 * thread never allocates a sequence or frees one (replaced ones are parked and freed by the next request).
 */
#ifndef MPK_PURE_TEST
#include <SDL.h>
#else /* tests/test_music.c: no SDL, one thread; and no real <stdlib.h> (-Iinclude gives the N64 stub, D83) */
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
char *getenv(const char *);
long strtol(const char *, char **, int);
typedef int SDL_mutex;
static SDL_mutex *SDL_CreateMutex(void) { return 0; }
static void SDL_LockMutex(SDL_mutex *m) { (void)m; }
static void SDL_UnlockMutex(SDL_mutex *m) { (void)m; }
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "musicpack.h"
#include "musicseq.h"
#include "musicfx.h"
#include "texpack.h"
#include "ge_pkg.h"
#include "system.h"

#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#define TSF_LINEAR_AMP_ENVELOPE /* N64 decay and release are straight lines in amplitude (spec "Envelope shape") */
/* the game's include/ holds an N64 math.h: the compiler's builtins instead */
#define TSF_POW __builtin_pow
#define TSF_POWF __builtin_powf
#define TSF_EXPF __builtin_expf
#define TSF_LOG __builtin_log
#define TSF_TAN __builtin_tan
#define TSF_LOG10 __builtin_log10
#define TSF_SQRT __builtin_sqrt
#define TSF_SQRTF __builtin_sqrtf
#include "../third_party/tinysoundfont/tsf.h"

#define MPK_RATE 22050
#define MPK_SLOTS 3
#define MPK_TRACKS 64             /* the default-volume table: track numbers 0..63 */
#define MPK_BLOCK 16              /* events take effect on 16-sample boundaries */
#define MPK_VOICES 64
#define MPK_STOP_SAMPLES (MPK_RATE / 20)   /* the 50 ms release of a stop */
/* Base gain: a full-scale note (velocity, CC7, sample volume, envelope, slot volume at their maximum) reaches 0.9245 of its
 * sample's peak at pan hard left or right (spec "Voice level"): 20 x log10(0.9245). The old -4 dB was a headroom guess for the
 * unsquared laws. Calibrate against GE_AUDIODUMP RMS of the ROM path (D83) by changing this one constant. */
#define MPK_BASE_GAIN_DB (-0.682f)
/* Gain of the effect bus return (spec: unity, "calibrate the total wet gain against a capture"). One constant. */
#define MPK_WET_GAIN 1.0f
#define MPK_DEAD 8

#ifdef REFRESH_PAL
#define MPK_FPS 50
#else
#define MPK_FPS 60
#endif

enum { ST_IDLE = 0, ST_PLAY, ST_STOPPING, ST_PAUSING, ST_PAUSED };

/* One slot's synth: a TinySoundFont copy plus the channel state it does not keep. CC91 (effect send) and CC10 (pan) are
 * applied per voice at render time (renderVoices): the synth has neither the N64's per-voice dry/wet send nor its pan law. */
typedef struct {
    tsf *t;
    unsigned char cc91[16];
    unsigned char cc10[16];
} Syn;

typedef struct {
    int track;                 /* current track number (0 none) */
    int v;                     /* the slot's volume 0..0x7FFF */
    int state;
    MsqSeq *seq;
    Syn *syn;
    MsqPlayer pl;
    int playing;               /* the sequencer runs (has a sequence that did not end) */
    int stop_left;             /* samples left in a 50 ms release */
    MsqSeq *pend_seq;          /* to start once the release is over */
    Syn *pend_syn;
    int pend;
    float gain, gain_target;   /* (P / 32767)^2 */
    int fade_target, fade_prefade, fade_left;
    MsqSeq *dead_seq[MPK_DEAD];
    Syn *dead_syn[MPK_DEAD];
} Slot;

static Slot sSlot[MPK_SLOTS];
static SDL_mutex *sLock;
static int sInited, sRom, sLog;
static tsf *sFont;             /* the stack's SoundFont (NULL: silence) */
static unsigned sGen;
static int sGenKnown;
static int sDefVol[MPK_TRACKS];
static unsigned sRetraces;
static unsigned char sMissingLogged[MPK_TRACKS + 1];
static float sMixBuf[MPK_BLOCK * 2], sSlotBuf[MPK_BLOCK * 2];
static float sAuxBuf[MPK_BLOCK * 2], sSlotAux[MPK_BLOCK * 2], sChanBuf[MPK_BLOCK * 2], sRet[MPK_BLOCK];
static MpfxBus sBus;           /* the one effect bus, shared by the three slots; its tail outlives a stopped slot */

static void init(void)
{
    const char *e;
    int i;
    if (sInited)
        return;
    sInited = 1;
    e = getenv("GE_ROM_MUSIC");
    sRom = e && *e && *e != '0';
    e = getenv("GE_MUSICLOG");
    sLog = e && *e && *e != '0';
    sLock = SDL_CreateMutex();
    mpfxBusInit(&sBus);
    for (i = 0; i < MPK_SLOTS; i++)
        sSlot[i].v = 0x7FFF;
    for (i = 0; i < MPK_TRACKS; i++)
        sDefVol[i] = 0x7FFF;
    if (sRom)
        sysLogPrintf(LOG_INFO, "music: GE_ROM_MUSIC set, music from the ROM sequence player");
}

int mpkActive(void)
{
    init();
    return !sRom;
}

void mpkRetrace(void)
{
    sRetraces++;
}

void mpkLogRequest(const char *what, int slot, int a, int b)
{
    init();
    if (sLog)
        sysLogPrintf(LOG_NOTE, "music: retrace %u slot %d %s %d %d", sRetraces, slot + 1, what, a, b);
}

static void lock(void)
{
    if (sLock)
        SDL_LockMutex(sLock);
}
static void unlock(void)
{
    if (sLock)
        SDL_UnlockMutex(sLock);
}

/* ---- the stack: SoundFont and default volumes ----------------------------------------------------------------- */

static void loadDefaults(void)
{
    unsigned char *js = NULL;
    unsigned long len = 0;
    const char *p, *e;
    int i;
    for (i = 0; i < MPK_TRACKS; i++)
        sDefVol[i] = 0x7FFF;
    if (!assetRead("music/music.json", &js, &len, NULL)) {
        sysLogPrintf(LOG_WARNING, "music: no music/music.json in the asset packs: every track at full volume");
        return;
    }
    p = (const char *)js;
    e = p + len;
    for (; p + 16 <= e; p++)
        if (!memcmp(p, "\"default_volume\"", 16))
            break;
    while (p < e && *p != '[')
        p++;
    for (i = 0; p < e && *p != ']' && i < MPK_TRACKS;) {
        if ((*p >= '0' && *p <= '9') || *p == '-') {
            long v = strtol(p, (char **)&p, 10);
            sDefVol[i++] = v < 0 ? 0 : v > 0x7FFF ? 0x7FFF : (int)v;
        } else {
            p++;
        }
    }
    pkgFree(js);
    sysLogPrintf(LOG_INFO, "music: %d track default volumes from music/music.json", i);
}

/* Game thread. Reloads when the stack changed; slots keep their own copies of the old font until their next play. */
static void checkStack(void)
{
    unsigned char *d = NULL;
    unsigned long len = 0;
    unsigned gen = assetStackGen();
    tsf *f = NULL;
    if (sGenKnown && gen == sGen)
        return;
    if (sGenKnown)
        sysLogPrintf(LOG_INFO, "music: reloaded for asset stack generation %u", gen);
    sGen = gen;
    sGenKnown = 1;
    memset(sMissingLogged, 0, sizeof sMissingLogged);
    if (assetRead("music/instruments.sf2", &d, &len, NULL)) {
        f = tsf_load_memory(d, (int)len);
        pkgFree(d);
        if (!f)
            sysLogPrintf(LOG_ERROR, "music: music/instruments.sf2 is not a readable SoundFont: the music is silent");
        else
            sysLogPrintf(LOG_INFO, "music: music/instruments.sf2, %d presets", tsf_get_presetcount(f));
    } else {
        sysLogPrintf(LOG_WARNING, "music: no music/instruments.sf2 in the asset packs: the music is silent");
    }
    if (f)
        tsf_set_output(f, TSF_STEREO_INTERLEAVED, MPK_RATE, MPK_BASE_GAIN_DB);
    lock();
    if (sFont)
        tsf_close(sFont); /* the slots' copies keep the data alive */
    sFont = f;
    unlock();
    loadDefaults();
}

static MsqSeq *loadTrack(int t)
{
    char path[48];
    unsigned char *d = NULL;
    unsigned long len = 0;
    char err[96];
    MsqSeq *s;
    int lt = t < 0 ? 0 : t > MPK_TRACKS ? MPK_TRACKS : t;
    snprintf(path, sizeof path, "music/track_%02d.mid", t);
    if (!assetRead(path, &d, &len, NULL)) {
        if (!sMissingLogged[lt]++)
            sysLogPrintf(LOG_WARNING, "music: no %s in the asset packs: silence", path);
        return NULL;
    }
    s = msqLoad(d, len, err, sizeof err);
    pkgFree(d);
    if (!s && !sMissingLogged[lt]++)
        sysLogPrintf(LOG_ERROR, "music: %s: %s: silence", path, err);
    return s;
}

/* ---- the synth sink ------------------------------------------------------------------------------------------- */

/* The N64 law squares velocity and CC7 (spec "Voice level"); the synth applies velocity linearly and CC7 cubed.
 * velocity: the voice keeps the synth's own factor v/127 (so the velocity layer a sound is on is unchanged) and gets
 *           one more factor v/127 here: (v/127)^2 in all.
 * CC7: the synth channel volume is set to the 14-bit value m = round(16383 x (c/127)^(2/3)); its cube is (c/127)^2.
 * The pure forms are mpfxVelocityExtra / mpfxCc7Fine (musicfx.c, unit-tested). */
static void cbNoteOn(void *u, int ch, int key, int vel)
{
    Syn *y = (Syn *)u;
    tsf *t = y->t;
    unsigned before = t->voicePlayIndex;
    struct tsf_voice *v, *vEnd;
    tsf_channel_note_on(t, ch, key, vel / 127.0f);
    if (t->voicePlayIndex == before || vel <= 0) /* no note was started */
        return;
    for (v = t->voices, vEnd = v + t->voiceNum; v != vEnd; v++)
        if (v->playingPreset != -1 && v->playIndex == before)
            v->noteGainDB += tsf_gainToDecibels(mpfxVelocityExtra(vel));
}
static void cbNoteOff(void *u, int ch, int key) { tsf_channel_note_off(((Syn *)u)->t, ch, key); }
static void cbControl(void *u, int ch, int cc, int v)
{
    Syn *y = (Syn *)u;
    if (cc == 91 && ch >= 0 && ch < 16) { /* the per-voice effect send */
        y->cc91[ch] = (unsigned char)(v & 127);
    } else if (cc == 10 && ch >= 0 && ch < 16) { /* the N64 pan law at render time */
        y->cc10[ch] = (unsigned char)(v & 127);
    } else if (cc == 7) {
        int m = mpfxCc7Fine(v);
        tsf_channel_midi_control(y->t, ch, 39, m & 127);
        tsf_channel_midi_control(y->t, ch, 7, m >> 7);
    } else {
        tsf_channel_midi_control(y->t, ch, cc, v);
    }
}
/* bank 0, program = instrument; not tsf_channel_set_presetnumber, which gives channel 10 the drum rules (the bank has none) */
static void cbProgram(void *u, int ch, int p) { tsf_channel_set_bank_preset(((Syn *)u)->t, ch, 0, p); }
static void cbBend(void *u, int ch, int v) { tsf_channel_set_pitchwheel(((Syn *)u)->t, ch, v); }

static Syn *synNew(tsf *font)
{
    Syn *y = (Syn *)calloc(1, sizeof *y);
    int ch;
    if (!y)
        return NULL;
    y->t = tsf_copy(font);
    if (!y->t) {
        free(y);
        return NULL;
    }
    tsf_set_max_voices(y->t, MPK_VOICES);
    tsf_reset(y->t);
    for (ch = 0; ch < 16; ch++) {
        tsf_channel_set_bank_preset(y->t, ch, 0, 0);
        y->cc10[ch] = 64;
    }
    return y;
}

static void synClose(Syn *y)
{
    if (!y)
        return;
    tsf_close(y->t);
    free(y);
}

static void synthCut(Syn *y)
{
    int ch;
    for (ch = 0; ch < 16; ch++)
        tsf_channel_sounds_off_all(y->t, ch);
}

/* One block of the synth's voices into dry (the main mix) and aux (the effect send), channel by channel: a channel whose
 * CC91 is 0 (the common case) goes straight to dry; otherwise its voices are summed apart and split dry = cos(s pi/254),
 * wet = sin(s pi/254) (constant power, 127 = no dry). Every voice also gets the N64 pan law (cos / sin of pan x pi/254,
 * pan = CC10 + the sound's own offset, clamped) in place of the synth's square-root law. Uses the synth's voice list and
 * renderer directly (same translation unit); tsf_render_float does the same walk without the split. */
static void renderVoices(Syn *y, float *dry, float *aux, int n)
{
    tsf *t = y->t;
    struct tsf_voice *v, *vEnd = t->voices + t->voiceNum;
    int ch, i;
    memset(dry, 0, sizeof(float) * 2 * n);
    memset(aux, 0, sizeof(float) * 2 * n);
    if (!t->voices)
        return;
    for (ch = 0; ch < 16; ch++) {
        int s = y->cc91[ch], any = 0;
        float *dst = s ? sChanBuf : dry;
        for (v = t->voices; v != vEnd; v++) {
            int pan;
            if (v->playingPreset == -1 || v->playingChannel != ch)
                continue;
            if (!any && s)
                memset(sChanBuf, 0, sizeof(float) * 2 * n);
            any = 1;
            pan = y->cc10[ch] + (int)(v->region->pan * 128.0f + (v->region->pan < 0 ? -0.5f : 0.5f));
            mpfxPan(pan < 0 ? 0 : pan > 127 ? 127 : pan, &v->panFactorLeft, &v->panFactorRight);
            tsf_voice_render(t, v, dst, n);
        }
        if (any && s) {
            float dg = mpfxDry(s), wg = mpfxWet(s);
            for (i = 0; i < 2 * n; i++) {
                dry[i] += sChanBuf[i] * dg;
                aux[i] += sChanBuf[i] * wg;
            }
        }
    }
}

/* ---- slots (lock held) ---------------------------------------------------------------------------------------- */

static void applyVolume(Slot *s)
{
    int t = s->track < 0 ? 0 : s->track >= MPK_TRACKS ? 0 : s->track;
    long p = (long)s->v * sDefVol[t] / 32768;
    float g = (float)p / 32767.0f;
    s->gain_target = g * g;
}

static void park(Slot *s, MsqSeq *q, Syn *y)
{
    int i;
    for (i = 0; i < MPK_DEAD && (q || y); i++) {
        if (q && !s->dead_seq[i]) {
            s->dead_seq[i] = q;
            q = NULL;
        }
        if (y && !s->dead_syn[i]) {
            s->dead_syn[i] = y;
            y = NULL;
        }
    }
    /* full (cannot happen: every request empties the list) */
}

/* The new sequence starts now (the slot is silent). */
static void startPending(Slot *s)
{
    park(s, s->seq, s->syn);
    msqStopPlayer(&s->pl);
    s->seq = s->pend_seq;
    s->syn = s->pend_syn;
    s->pend_seq = NULL;
    s->pend_syn = NULL;
    s->pend = 0;
    s->playing = s->seq && s->syn && msqStart(&s->pl, s->seq);
    s->state = s->playing ? ST_PLAY : ST_IDLE;
    s->gain = s->gain_target;
}

static void releaseStart(Slot *s, int st)
{
    s->state = st;
    s->stop_left = MPK_STOP_SAMPLES;
}

static void drainDead(Slot *s, MsqSeq **q, Syn **y)
{
    int i;
    for (i = 0; i < MPK_DEAD; i++) {
        q[i] = s->dead_seq[i];
        y[i] = s->dead_syn[i];
        s->dead_seq[i] = NULL;
        s->dead_syn[i] = NULL;
    }
}

static void freeDead(MsqSeq **q, Syn **y)
{
    int i;
    for (i = 0; i < MPK_DEAD; i++) {
        msqFree(q[i]);
        synClose(y[i]);
    }
}

/* ---- requests -------------------------------------------------------------------------------------------------- */

static Slot *slotOf(int i)
{
    return i >= 0 && i < MPK_SLOTS ? &sSlot[i] : NULL;
}

void mpkPlay(int slot, int track)
{
    Slot *s = slotOf(slot);
    MsqSeq *seq, *oldq[MPK_DEAD], *pq = NULL;
    Syn *syn = NULL, *oldy[MPK_DEAD], *py = NULL;
    init();
    if (!s)
        return;
    mpkLogRequest("play", slot, track, 0);
    checkStack();
    seq = loadTrack(track);
    lock();
    if (sFont)
        syn = synNew(sFont);
    unlock();
    lock();
    drainDead(s, oldq, oldy);
    if (s->pend) { /* a play still waiting for its release: replaced */
        pq = s->pend_seq;
        py = s->pend_syn;
    }
    s->pend_seq = seq;
    s->pend_syn = syn;
    s->pend = 1;
    s->track = track;
    applyVolume(s);
    if (s->state == ST_PLAY || s->state == ST_PAUSING) {
        /* stop first: the new track starts once the 50 ms release has run */
        msqPause(&s->pl);
        releaseStart(s, ST_STOPPING);
    } else if (s->state != ST_STOPPING) {
        startPending(s);
    }
    unlock();
    freeDead(oldq, oldy);
    msqFree(pq);
    synClose(py);
}

void mpkStop(int slot, int *state)
{
    Slot *s = slotOf(slot);
    init();
    if (!s)
        return;
    mpkLogRequest("stop", slot, s->track, 0);
    lock();
    if (state)
        *state = 0;
    if (s->track && (s->state == ST_PLAY || s->state == ST_PAUSING)) {
        msqPause(&s->pl);
        s->playing = 0;
        releaseStart(s, ST_STOPPING);
    } else if (s->state == ST_PAUSED) {
        s->playing = 0;
        s->state = ST_IDLE;
    }
    if (s->pend) { /* a play waiting for its release: stopped before it started */
        park(s, s->pend_seq, s->pend_syn);
        s->pend_seq = NULL;
        s->pend_syn = NULL;
        s->pend = 0;
    }
    s->track = 0;
    unlock();
}

unsigned mpkGetVolume(int slot)
{
    Slot *s = slotOf(slot);
    return s ? (unsigned)s->v : 0;
}

void mpkSetVolume(int slot, unsigned v)
{
    Slot *s = slotOf(slot);
    init();
    if (!s)
        return;
    mpkLogRequest("volume", slot, (int)v, 0);
    lock();
    s->v = v > 0x7FFF ? 0x7FFF : (int)v;
    applyVolume(s);
    unlock();
}

void mpkSaveVolumeAsDefault(int slot)
{
    Slot *s = slotOf(slot);
    init();
    if (!s)
        return;
    mpkLogRequest("savedefault", slot, s->track, 0);
    lock();
    if (s->track >= 0 && s->track < MPK_TRACKS)
        sDefVol[s->track] = s->v;
    unlock();
}

static int framesOf(float seconds)
{
    int n = (int)(seconds * MPK_FPS);
    return n < 1 ? 1 : n; /* the game never asks for less than one frame */
}

void mpkFadeOut(int slot, float seconds, int *state)
{
    Slot *s = slotOf(slot);
    init();
    if (!s)
        return;
    mpkLogRequest("fadeout", slot, (int)(seconds * 1000), 0);
    lock();
    if (*state != -1) {
        s->fade_prefade = s->v;
        s->fade_target = 0;
        s->fade_left = framesOf(seconds);
        *state = -1;
    }
    unlock();
}

void mpkFadeIn(int slot, float seconds, unsigned target, int *state)
{
    Slot *s = slotOf(slot);
    init();
    if (!s)
        return;
    mpkLogRequest("fadein", slot, (int)(seconds * 1000), (int)target);
    lock();
    if (*state != 1) {
        if (s->state == ST_PAUSED || s->state == ST_PAUSING) {
            if (s->state == ST_PAUSED)
                msqResume(&s->pl);
            s->state = s->playing ? ST_PLAY : ST_IDLE;
        }
        s->fade_target = target == 0xFFFF ? s->fade_prefade : (int)(target > 0x7FFF ? 0x7FFF : target);
        s->fade_prefade = 0;
        s->fade_left = framesOf(seconds);
        *state = 1;
    }
    unlock();
}

void mpkFadeTick(int *const state[3])
{
    int i;
    init();
    lock();
    for (i = 0; i < MPK_SLOTS; i++) {
        Slot *s = &sSlot[i];
        if (!*state[i])
            continue;
        if (s->fade_left < 1)
            s->fade_left = 1;
        s->v += (int)((float)(s->fade_target - s->v) / (float)s->fade_left);
        if (s->v < 0)
            s->v = 0;
        if (s->v > 0x7FFF)
            s->v = 0x7FFF;
        applyVolume(s);
        if (--s->fade_left == 0) {
            *state[i] = 0;
            if (sLog)
                sysLogPrintf(LOG_NOTE, "music: retrace %u slot %d fade done volume %d", sRetraces, i + 1, s->v);
            if (s->v == 0 && s->state == ST_PLAY) {
                msqPause(&s->pl); /* the slot keeps its track and position */
                releaseStart(s, ST_PAUSING);
            }
        }
    }
    unlock();
}

/* ---- audio thread ---------------------------------------------------------------------------------------------- */

static const MsqSink *sinkFor(Syn *y)
{
    static MsqSink k;
    k.user = y;
    k.note_on = cbNoteOn;
    k.note_off = cbNoteOff;
    k.control = cbControl;
    k.program = cbProgram;
    k.bend = cbBend;
    return &k;
}

/* One block of slot s: dry into sMixBuf, its effect send into sAuxBuf. The slot's volume (P squared) and the stop ramp sit
 * in front of the dry/wet split, as on the N64, so both scale with them. */
static void renderSlot(Slot *s, int n)
{
    int i;
    float g0, g1, r0 = 1.0f, r1 = 1.0f;
    if (!s->syn || s->state == ST_IDLE || s->state == ST_PAUSED)
        return;
    if (s->state == ST_PLAY && s->playing) {
        if (!msqAdvance(&s->pl, n * 1000000.0 / MPK_RATE, sinkFor(s->syn))) {
            /* natural end: once the last note-offs fired; what is still releasing is cut */
            s->playing = 0;
            synthCut(s->syn);
            s->state = ST_IDLE;
            if (sLog)
                sysLogPrintf(LOG_NOTE, "music: retrace %u slot %d track %d ended", sRetraces, (int)(s - sSlot) + 1, s->track);
            return;
        }
    }
    renderVoices(s->syn, sSlotBuf, sSlotAux, n);
    if (s->state == ST_STOPPING || s->state == ST_PAUSING) {
        r0 = (float)s->stop_left / MPK_STOP_SAMPLES;
        s->stop_left -= n;
        if (s->stop_left < 0)
            s->stop_left = 0;
        r1 = (float)s->stop_left / MPK_STOP_SAMPLES;
    }
    /* a volume change reaches the sounding voices over about a millisecond: one block */
    g0 = s->gain;
    g1 = s->gain_target;
    for (i = 0; i < n; i++) {
        float f = (float)(i + 1) / n;
        float g = (g0 + (g1 - g0) * f) * (r0 + (r1 - r0) * f);
        sMixBuf[2 * i] += sSlotBuf[2 * i] * g;
        sMixBuf[2 * i + 1] += sSlotBuf[2 * i + 1] * g;
        sAuxBuf[2 * i] += sSlotAux[2 * i] * g;
        sAuxBuf[2 * i + 1] += sSlotAux[2 * i + 1] * g;
    }
    s->gain = g1;
    if ((s->state == ST_STOPPING || s->state == ST_PAUSING) && s->stop_left == 0) {
        synthCut(s->syn);
        if (s->state == ST_PAUSING) {
            s->state = ST_PAUSED;
        } else {
            s->playing = 0;
            s->state = ST_IDLE;
            if (s->pend)
                startPending(s);
        }
    }
}

void mpkMix(short *buf, int frames)
{
    int done = 0, i, k;
    if (!sInited || sRom || !buf || frames <= 0)
        return;
    lock();
    while (done < frames) {
        int n = frames - done > MPK_BLOCK ? MPK_BLOCK : frames - done;
        memset(sMixBuf, 0, sizeof sMixBuf);
        memset(sAuxBuf, 0, sizeof sAuxBuf);
        for (k = 0; k < MPK_SLOTS; k++)
            renderSlot(&sSlot[k], n);
        /* the bus runs every block, also with every slot silent: the tail of a stopped slot keeps ringing */
        mpfxBusProcess(&sBus, sAuxBuf, sRet, n);
        for (i = 0; i < n; i++) {
            float r = sRet[i] * MPK_WET_GAIN;  /* added at unity to both channels */
            sMixBuf[2 * i] += r;
            sMixBuf[2 * i + 1] += r;
        }
        for (i = 0; i < n * 2; i++) {
            int v = buf[done * 2 + i] + (int)(sMixBuf[i] * 32767.0f);
            buf[done * 2 + i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        done += n;
    }
    unlock();
}

/* GE_MUSICONLY=<track>: a music-only test mode for comparing the pack player with the ROM player
 * (GE_ROM_MUSIC=1), both through the game's own play requests. The first slot-1 play request of the
 * run is replaced by <track>; every other play request, on any slot, is dropped. Run with
 * GE_AUDIODUMP=1 and GE_QUITFRAME=<n>, once with and once without GE_ROM_MUSIC=1. Returns 1 when
 * the request must be ignored; otherwise *track is the track to play. */
int mpkMusicOnly(int slot, int *track)
{
    static int sMode = -2, sTrack, sStarted;
    if (sMode == -2) {
        const char *e = getenv("GE_MUSICONLY");
        sMode = (e && *e) ? 1 : 0;
        if (sMode) for (sTrack = 0; *e >= '0' && *e <= '9'; e++) sTrack = sTrack * 10 + (*e - '0');
    }
    if (!sMode)
        return 0;
    if (slot != 0 || sStarted)
        return 1;
    sStarted = 1;
    *track = sTrack;
    return 0;
}
