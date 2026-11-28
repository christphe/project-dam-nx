/*
 * midiplay: small interactive player for the exported GoldenEye music.
 *
 *   midiplay [--rate 44100|22050] <export_dir> [track]
 *   midiplay --render <export_dir> <track> <out.wav> [seconds]
 *   midiplay --check <export_dir>
 *
 * <export_dir> is an asset pack as a folder (ge007-export <rom> <out> --folder writes
 * <out>/ge007.passets/): music/track_NN.mid, music/instruments.sf2, music/music.json. An older
 * export with instruments.sf2 at its root is read too.
 *
 * --check loads the SoundFont and every music/track_NN.mid and prints what it found: loop
 * markers, unresolved loop ends, the length of one pass (used by automated checks).
 *
 * Plays like the game: the sequence through the game's sequencer (port/src/musicseq.c: every
 * MIDI track loops on its own, loopEnd <id> <count|inf> repeats as the game does), the synth
 * TinySoundFont, each track at the game's level from music.json. Both files are watched: when
 * one changes on disk it is reloaded and playback continues at about the same position, so the
 * exporter can be re-run while listening. Channel 10 is an ordinary melodic channel (the bank
 * has no drum meaning).
 *
 * Keys: Right/N next track, Left/P previous, R restart, Space stop/play,
 *       L loop as in the game / one straight pass (restarts), Up/Down volume, Q/Esc quit.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"
#include "musicseq.h"

#define MAX_TRACKS 128
#define BLOCK_FRAMES 64          /* TSF render granularity; messages are dispatched per block */
#define BASE_GAIN_DB (-4.0f)     /* the game's synth headroom (port/src/musicpack.c MPK_BASE_GAIN_DB) */
#define MAX_VOICES 128
#define TAIL_MS 3000.0           /* let releases ring out after the last message */
#define POLL_MS 400              /* file change polling interval */

/* ---- files ------------------------------------------------------------------- */
typedef struct {
    long long mtime;
    long long size;
} filestamp;

static int stamp_of(const char *path, filestamp *fs)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    fs->mtime = (long long)st.st_mtime;
    fs->size = (long long)st.st_size;
    return 0;
}

static int stamp_eq(const filestamp *a, const filestamp *b)
{
    return a->mtime == b->mtime && a->size == b->size;
}

static unsigned char *read_file(const char *path, int *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    p = (unsigned char *)malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (int)n;
    return p;
}

/* ---- the player ------------------------------------------------------------------
 * The sequence is played by the game's own sequencer (port/src/musicseq.c, docs/specs/audio.md "How a player loops a
 * file with these markers"): every MIDI track loops on its own, each loopEnd <id> <count|inf> repeats its loop count times
 * (forever for inf), notes end by duration in played time. With looping off every loop end falls through: the file is
 * played straight once. The level of a track is the game's: (default volume / 32768)^2 from music/music.json. */
typedef struct {
    tsf *sf;
    MsqSeq *seq;
    MsqPlayer pl;
    int started;
    uint8_t *counts;         /* the file's loop counts (seq->counter_init is overwritten while looping is off) */
    double msec, length_ms;  /* played time; one straight pass */
    double tail_ms;          /* after the sequence ended: releases ring out, then it stops */
    int rate;
    int playing;
    int loop_on;
    int over;
    float volume, track_gain;
} player;

static void cb_note_on(void *u, int ch, int key, int vel) { tsf_channel_note_on((tsf *)u, ch, key, vel / 127.0f); }
static void cb_note_off(void *u, int ch, int key) { tsf_channel_note_off((tsf *)u, ch, key); }
static void cb_control(void *u, int ch, int cc, int v) { tsf_channel_midi_control((tsf *)u, ch, cc, v); }
/* Bank 0, program = instrument index. Not tsf_channel_set_presetnumber: that one applies the drum rules to channel 10
 * and the bank has no drums. */
static void cb_program(void *u, int ch, int prog) { tsf_channel_set_bank_preset((tsf *)u, ch, 0, prog); }
static void cb_bend(void *u, int ch, int v) { tsf_channel_set_pitchwheel((tsf *)u, ch, v); }

static tsf *new_synth(tsf *loaded, int rate)
{
    int ch;
    tsf_set_output(loaded, TSF_STEREO_INTERLEAVED, rate, BASE_GAIN_DB);
    tsf_set_max_voices(loaded, MAX_VOICES);
    for (ch = 0; ch < 16; ch++)
        tsf_channel_set_bank_preset(loaded, ch, 0, 0);
    return loaded;
}

static void set_counts(player *p)
{
    if (p->seq && p->counts)
        for (int i = 0; i < p->seq->ncounters; i++)
            p->seq->counter_init[i] = p->loop_on ? p->counts[i] : 0;
}

/* One straight pass (no loop jump), in milliseconds. */
static double straight_length_ms(MsqSeq *seq)
{
    MsqPlayer q;
    MsqSink none;
    double ms = 0.0;
    uint8_t *keep = (uint8_t *)malloc((size_t)seq->ncounters + 1);
    memset(&none, 0, sizeof none);
    if (!keep)
        return 0.0;
    memcpy(keep, seq->counter_init, (size_t)seq->ncounters);
    memset(seq->counter_init, 0, (size_t)seq->ncounters);
    if (msqStart(&q, seq)) {
        while (msqAdvance(&q, 10000.0, &none) && ms < 3600000.0)
            ms += 10.0;
        msqStopPlayer(&q);
    }
    memcpy(seq->counter_init, keep, (size_t)seq->ncounters);
    free(keep);
    return ms;
}

/* Restart from the beginning, then play silently up to `ms` (programs, controllers and bends applied, no notes). */
static void seek(player *p, double ms)
{
    MsqSink state;
    memset(&state, 0, sizeof state);
    state.user = p->sf;
    state.control = cb_control;
    state.program = cb_program;
    state.bend = cb_bend;
    tsf_reset(p->sf);
    new_synth(p->sf, p->rate);
    if (p->started)
        msqStopPlayer(&p->pl);
    p->started = 0;
    p->msec = 0.0;
    p->over = 0;
    p->tail_ms = 0.0;
    if (!p->seq)
        return;
    set_counts(p);
    if (!msqStart(&p->pl, p->seq))
        return;
    p->started = 1;
    while (p->msec + 10.0 <= ms && msqAdvance(&p->pl, 10000.0, &state))
        p->msec += 10.0;
    tsf_note_off_all(p->sf);
}

/* Render `frames` stereo float frames. Called from the audio thread (device locked
 * for every state change made by the main thread). */
static void player_render(player *p, float *out, int frames)
{
    int done = 0;
    MsqSink sink;
    float g;
    if (!p->sf || !p->playing || !p->started) {
        memset(out, 0, (size_t)frames * 2 * sizeof(float));
        return;
    }
    sink.user = p->sf;
    sink.note_on = cb_note_on;
    sink.note_off = cb_note_off;
    sink.control = cb_control;
    sink.program = cb_program;
    sink.bend = cb_bend;
    while (done < frames) {
        int n = frames - done > BLOCK_FRAMES ? BLOCK_FRAMES : frames - done;
        double ms = n * 1000.0 / p->rate;
        if (!p->over) {
            if (!msqAdvance(&p->pl, ms * 1000.0, &sink))
                p->over = 1;
            p->msec += ms;
        } else if ((p->tail_ms += ms) > TAIL_MS) {
            p->playing = 0; /* the game's slot stops at the end of a sequence too */
        }
        tsf_render_float(p->sf, out + (size_t)done * 2, n, 0);
        done += n;
    }
    g = p->volume * p->track_gain;
    if (g != 1.0f)
        for (int i = 0; i < frames * 2; i++)
            out[i] *= g;
}

/* ---- loading ------------------------------------------------------------------------ */
/* The pack keeps the SoundFont in music/; an older export kept it at the root. */
static void sf2_path(const char *dir, char *path, size_t n)
{
    filestamp fs;
    snprintf(path, n, "%s/music/instruments.sf2", dir);
    if (stamp_of(path, &fs) != 0)
        snprintf(path, n, "%s/instruments.sf2", dir);
}

static int load_synth(const char *path, int rate, tsf **out)
{
    int n;
    unsigned char *d = read_file(path, &n);
    tsf *sf;
    if (!d)
        return -1;
    sf = tsf_load_memory(d, n);
    free(d);
    if (!sf)
        return -1;
    *out = new_synth(sf, rate);
    return 0;
}

static MsqSeq *load_seq(const char *path, char *err, size_t errlen)
{
    int n;
    unsigned char *d = read_file(path, &n);
    MsqSeq *s;
    if (!d) {
        snprintf(err, errlen, "cannot read");
        return NULL;
    }
    s = msqLoad(d, (size_t)n, err, errlen);
    free(d);
    return s;
}

/* music/music.json "default_volume": [..], one per track number; the game plays a track at (D / 32768)^2. 1 when absent. */
static float track_gain(const char *dir, int track)
{
    char path[1024];
    int n;
    unsigned char *d, *d2;
    const char *q;
    float g = 1.0f;
    snprintf(path, sizeof path, "%s/music/music.json", dir);
    d = read_file(path, &n);
    if (!d)
        return g;
    d2 = (unsigned char *)realloc(d, (size_t)n + 1);
    if (!d2) {
        free(d);
        return g;
    }
    d = d2;
    d[n] = 0;
    q = strstr((const char *)d, "\"default_volume\"");
    if (q && (q = strchr(q, '[')) != NULL) {
        q++;
        for (int i = 0; *q && *q != ']'; i++) {
            char *e;
            long v;
            while (*q == ',' || *q == ' ' || *q == '\n' || *q == '\r' || *q == '\t')
                q++;
            v = strtol(q, &e, 10);
            if (e == q)
                break;
            if (i == track) {
                g = (float)(v / 32768.0);
                g *= g;
                break;
            }
            q = e;
        }
    }
    free(d);
    return g;
}

/* Takes `seq` (NULL clears). */
static void player_set_seq(player *p, MsqSeq *seq)
{
    if (p->started)
        msqStopPlayer(&p->pl);
    p->started = 0;
    if (p->seq)
        msqFree(p->seq);
    free(p->counts);
    p->counts = NULL;
    p->seq = seq;
    p->length_ms = 0.0;
    if (seq) {
        p->counts = (uint8_t *)malloc((size_t)seq->ncounters + 1);
        if (p->counts)
            memcpy(p->counts, seq->counter_init, (size_t)seq->ncounters);
        p->length_ms = straight_length_ms(seq);
    }
}

static int count_forever(const MsqSeq *s, const uint8_t *counts)
{
    int n = 0;
    for (int i = 0; i < s->ncounters; i++)
        n += (counts ? counts[i] : s->counter_init[i]) == 255;
    return n;
}

/* ---- --check ----------------------------------------------------------------------------- */
static int check_mode(const char *dir)
{
    char path[1024];
    int t, bad = 0, nfiles = 0;
    tsf *sf = NULL;

    sf2_path(dir, path, sizeof path);
    if (load_synth(path, 44100, &sf)) {
        printf("instruments.sf2: LOAD FAILED (%s)\n", path);
        bad++;
    } else {
        printf("instruments.sf2: %d presets\n", tsf_get_presetcount(sf));
        tsf_close(sf);
    }
    for (t = 0; t < MAX_TRACKS; t++) {
        char err[160] = "";
        filestamp fs;
        MsqSeq *s;
        int loops = 0;
        snprintf(path, sizeof path, "%s/music/track_%02d.mid", dir, t);
        if (stamp_of(path, &fs) != 0)
            continue;
        nfiles++;
        s = load_seq(path, err, sizeof err);
        if (!s) {
            printf("track_%02d.mid: LOAD FAILED (%s)\n", t, err);
            bad++;
            continue;
        }
        for (int k = 0; k < s->ntracks; k++)
            loops += s->tracks[k].nloops;
        printf("track_%02d.mid: tracks %d  loop starts %d  loop ends %d (%d forever)  unresolved %d  one pass %.1f s\n", t,
               s->ntracks, loops, s->ncounters, count_forever(s, NULL), s->unresolved, straight_length_ms(s) / 1000.0);
        bad += s->unresolved != 0;
        msqFree(s);
    }
    printf("checked %d midi files, %d failures\n", nfiles, bad);
    return bad ? 1 : 0;
}

/* ---- offline render ------------------------------------------------------------------- */
static void put_le32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static int render_mode(const char *dir, int track, const char *wav_path, double seconds, int rate)
{
    char path[1024];
    player p;
    float *buf;
    short *pcm;
    long total, i, clipped = 0, second_frames = rate;
    FILE *f;
    unsigned char hdr[44];
    double rms_acc = 0.0, peak = 0.0;
    long sec_n = 0;
    int sec = 0;

    memset(&p, 0, sizeof p);
    p.rate = rate;
    p.volume = 1.0f;
    p.loop_on = 1;
    p.playing = 1;
    sf2_path(dir, path, sizeof path);
    if (load_synth(path, rate, &p.sf)) {
        fprintf(stderr, "cannot load %s\n", path);
        return 1;
    }
    snprintf(path, sizeof path, "%s/music/track_%02d.mid", dir, track);
    {
        char err[160] = "";
        MsqSeq *seq = load_seq(path, err, sizeof err);
        if (!seq) {
            fprintf(stderr, "cannot load %s: %s\n", path, err);
            return 1;
        }
        player_set_seq(&p, seq);
    }
    p.track_gain = track_gain(dir, track);
    seek(&p, 0.0);
    printf("track %d: %d presets, one pass %.1f s, %d loop ends (%d forever), track gain %.3f\n", track,
           tsf_get_presetcount(p.sf), p.length_ms / 1000.0, p.seq->ncounters, count_forever(p.seq, p.counts), p.track_gain);

    total = (long)(seconds * rate);
    buf = (float *)malloc((size_t)total * 2 * sizeof(float));
    pcm = (short *)malloc((size_t)total * 2 * sizeof(short));
    if (!buf || !pcm)
        return 1;
    player_render(&p, buf, (int)total);
    for (i = 0; i < total * 2; i++) {
        float v = buf[i];
        double a = fabs((double)v);
        if (a >= 1.0)
            clipped++;
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        pcm[i] = (short)lrintf(v * 32767.0f);
        rms_acc += (double)buf[i] * buf[i];
        if (a > peak)
            peak = a;
        if (++sec_n == second_frames * 2) {
            if (sec < 10)
                printf("  second %2d: rms %.4f  peak %.4f\n", sec + 1, sqrt(rms_acc / sec_n), peak);
            sec++;
            sec_n = 0;
            rms_acc = 0.0;
            peak = 0.0;
        }
    }
    printf("clipped samples: %ld of %ld (%.3f%%)\n", clipped, total * 2, 100.0 * clipped / (double)(total * 2));

    f = fopen(wav_path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", wav_path);
        return 1;
    }
    memcpy(hdr, "RIFF", 4);
    put_le32(hdr + 4, 36 + (unsigned)(total * 4));
    memcpy(hdr + 8, "WAVEfmt ", 8);
    put_le32(hdr + 16, 16);
    hdr[20] = 1; hdr[21] = 0; hdr[22] = 2; hdr[23] = 0;
    put_le32(hdr + 24, (unsigned)rate);
    put_le32(hdr + 28, (unsigned)rate * 4);
    hdr[32] = 4; hdr[33] = 0; hdr[34] = 16; hdr[35] = 0;
    memcpy(hdr + 36, "data", 4);
    put_le32(hdr + 40, (unsigned)(total * 4));
    fwrite(hdr, 1, 44, f);
    fwrite(pcm, 2, (size_t)total * 2, f);
    fclose(f);
    printf("wrote %s\n", wav_path);
    player_set_seq(&p, NULL);
    tsf_close(p.sf);
    free(buf);
    free(pcm);
    return 0;
}

/* ---- interactive ------------------------------------------------------------------------- */
typedef struct {
    player p;
    SDL_AudioDeviceID dev;
    char dir[900];
    int tracks[MAX_TRACKS], ntracks, ti; /* existing track numbers, index into them */
    filestamp sf_stamp, mid_stamp;
} app;

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    player_render((player *)ud, (float *)stream, len / (int)(2 * sizeof(float)));
}

static void midi_path(const app *a, char *path, size_t n)
{
    snprintf(path, n, "%s/music/track_%02d.mid", a->dir, a->tracks[a->ti]);
}

static void scan_tracks(app *a)
{
    int t, keep = a->ntracks ? a->tracks[a->ti] : -1;
    char path[1024];
    a->ntracks = 0;
    for (t = 0; t < MAX_TRACKS; t++) {
        filestamp fs;
        snprintf(path, sizeof path, "%s/music/track_%02d.mid", a->dir, t);
        if (stamp_of(path, &fs) == 0)
            a->tracks[a->ntracks++] = t;
    }
    a->ti = 0;
    for (t = 0; t < a->ntracks; t++)
        if (a->tracks[t] == keep)
            a->ti = t;
}

/* Load the current track from disk; restart from `ms` (0 = from the beginning). */
static int open_track(app *a, double ms, int keep_playing)
{
    char path[1024], err[160] = "";
    MsqSeq *seq;
    filestamp fs;

    midi_path(a, path, sizeof path);
    if (stamp_of(path, &fs) || (seq = load_seq(path, err, sizeof err)) == NULL)
        return -1;
    SDL_LockAudioDevice(a->dev);
    player_set_seq(&a->p, seq);
    a->p.track_gain = track_gain(a->dir, a->tracks[a->ti]);
    seek(&a->p, ms);
    a->p.playing = keep_playing;
    SDL_UnlockAudioDevice(a->dev);
    a->mid_stamp = fs;
    return 0;
}

static void poll_reload(app *a)
{
    char path[1024];
    filestamp fs;

    sf2_path(a->dir, path, sizeof path);
    if (stamp_of(path, &fs) == 0 && !stamp_eq(&fs, &a->sf_stamp)) {
        tsf *sf = NULL;
        if (load_synth(path, a->p.rate, &sf) == 0) {
            tsf *old;
            SDL_LockAudioDevice(a->dev);
            old = a->p.sf;
            a->p.sf = sf;
            seek(&a->p, a->p.msec);
            SDL_UnlockAudioDevice(a->dev);
            tsf_close(old);
            a->sf_stamp = fs;
            printf("reloaded instruments.sf2 (%d presets)\n", tsf_get_presetcount(sf));
        } /* else: probably mid-write; the stamp stays old so it is retried */
    }
    midi_path(a, path, sizeof path);
    if (stamp_of(path, &fs) == 0 && !stamp_eq(&fs, &a->mid_stamp)) {
        double at = a->p.msec; /* approximately current; read without the lock on purpose, only a hint */
        if (open_track(a, at, a->p.playing) == 0)
            printf("reloaded track_%02d.mid\n", a->tracks[a->ti]);
    }
}

static void fmt_time(char *s, size_t n, double ms)
{
    int sec = (int)(ms / 1000.0);
    snprintf(s, n, "%d:%02d", sec / 60, sec % 60);
}

static void update_title(app *a, SDL_Window *w)
{
    char title[256], t1[16], t2[16];
    int voices;
    double ms, len;
    SDL_LockAudioDevice(a->dev);
    voices = a->p.sf ? tsf_active_voice_count(a->p.sf) : 0;
    ms = a->p.msec;
    len = a->p.length_ms;
    SDL_UnlockAudioDevice(a->dev);
    fmt_time(t1, sizeof t1, ms);
    fmt_time(t2, sizeof t2, len);
    snprintf(title, sizeof title, "midiplay  track_%02d.mid [%d/%d]  %s / %s  voices %d  loop %s%s  vol %d%%  %s",
             a->tracks[a->ti], a->ti + 1, a->ntracks, t1, t2, voices, a->p.loop_on ? "on" : "off",
             a->p.seq && count_forever(a->p.seq, a->p.counts) ? "" : " (plays once)", (int)lrintf(a->p.volume * 100.0f), a->p.playing ? "playing" : "stopped");
    SDL_SetWindowTitle(w, title);
}

static void draw(app *a, SDL_Renderer *r)
{
    int w, h;
    double frac;
    SDL_Rect bar, fill;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_SetRenderDrawColor(r, 24, 26, 32, 255);
    SDL_RenderClear(r);
    SDL_LockAudioDevice(a->dev);
    frac = a->p.length_ms > 0 ? fmod(a->p.msec, a->p.length_ms) / a->p.length_ms : 0;
    SDL_UnlockAudioDevice(a->dev);
    if (frac > 1)
        frac = 1;
    bar.x = 16; bar.y = 16; bar.w = w - 32; bar.h = 16;
    SDL_SetRenderDrawColor(r, 56, 60, 72, 255);
    SDL_RenderFillRect(r, &bar);
    fill = bar;
    fill.w = (int)(bar.w * frac);
    SDL_SetRenderDrawColor(r, 90, 170, 255, 255);
    SDL_RenderFillRect(r, &fill);
    bar.y = 44; bar.h = 10;
    SDL_SetRenderDrawColor(r, 56, 60, 72, 255);
    SDL_RenderFillRect(r, &bar);
    fill = bar;
    fill.w = (int)(bar.w * (a->p.volume > 1.0f ? 1.0f : a->p.volume));
    SDL_SetRenderDrawColor(r, 120, 210, 120, 255);
    SDL_RenderFillRect(r, &fill);
    SDL_RenderPresent(r);
}

static int interactive(const char *dir, int first_track, int rate)
{
    app a;
    SDL_AudioSpec want, have;
    SDL_Window *win;
    SDL_Renderer *ren;
    char path[1024];
    int running = 1, t;
    Uint32 last_poll = 0, last_title = 0;

    memset(&a, 0, sizeof a);
    snprintf(a.dir, sizeof a.dir, "%s", dir);
    a.p.volume = 0.8f;
    a.p.loop_on = 1;

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    scan_tracks(&a);
    if (!a.ntracks) {
        fprintf(stderr, "no music/track_NN.mid in %s (run ge007-audio-export first)\n", dir);
        return 1;
    }
    for (t = 0; t < a.ntracks; t++)
        if (a.tracks[t] == first_track)
            a.ti = t;
    if (first_track < 0)
        for (t = 0; t < a.ntracks; t++)
            if (a.tracks[t] >= 1) { /* track 0 is the "no music" placeholder */
                a.ti = t;
                break;
            }

    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    want.userdata = &a.p;
    a.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!a.dev) {
        fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return 1;
    }
    a.p.rate = have.freq;

    sf2_path(dir, path, sizeof path);
    if (stamp_of(path, &a.sf_stamp) || load_synth(path, a.p.rate, &a.p.sf)) {
        fprintf(stderr, "cannot load %s\n", path);
        return 1;
    }
    printf("%s: %d presets, audio %d Hz %s\n", path, tsf_get_presetcount(a.p.sf), have.freq,
           have.format == AUDIO_F32SYS ? "float" : "other format");
    if (open_track(&a, 0.0, 1)) {
        fprintf(stderr, "cannot load the track\n");
        return 1;
    }
    printf("playing track_%02d.mid  (one pass %.1f s, %d loop ends)\n", a.tracks[a.ti], a.p.length_ms / 1000.0,
           a.p.seq->ncounters);
    printf("keys: Right/N next  Left/P previous  R restart  Space stop/play  L loop  Up/Down volume  Q quit\n");

    win = SDL_CreateWindow("midiplay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 560, 72, 0);
    ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    if (!win || !ren) {
        fprintf(stderr, "window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_PauseAudioDevice(a.dev, 0);

    while (running) {
        SDL_Event e;
        Uint32 now = SDL_GetTicks();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                int step = 0;
                switch (e.key.keysym.sym) {
                case SDLK_q:
                case SDLK_ESCAPE:
                    running = 0;
                    break;
                case SDLK_RIGHT:
                case SDLK_n:
                    step = 1;
                    break;
                case SDLK_LEFT:
                case SDLK_p:
                    step = -1;
                    break;
                case SDLK_r:
                    open_track(&a, 0.0, 1);
                    break;
                case SDLK_SPACE:
                    SDL_LockAudioDevice(a.dev);
                    if (!a.p.playing && a.p.over)
                        seek(&a.p, 0.0); /* the sequence had ended: play it again */
                    a.p.playing = !a.p.playing;
                    if (!a.p.playing)
                        tsf_note_off_all(a.p.sf);
                    SDL_UnlockAudioDevice(a.dev);
                    break;
                case SDLK_l: /* the loop counts apply from the start: restart the track */
                    SDL_LockAudioDevice(a.dev);
                    a.p.loop_on = !a.p.loop_on;
                    seek(&a.p, 0.0);
                    a.p.playing = 1;
                    SDL_UnlockAudioDevice(a.dev);
                    printf("loop %s\n", a.p.loop_on ? "on (as in the game)" : "off (one straight pass)");
                    break;
                default:
                    break;
                }
                if (step) {
                    a.ti = (a.ti + step + a.ntracks) % a.ntracks;
                    if (open_track(&a, 0.0, 1) == 0)
                        printf("track_%02d.mid  %.1f s\n", a.tracks[a.ti], a.p.length_ms / 1000.0);
                }
            }
        }
        {
            const Uint8 *ks = SDL_GetKeyboardState(NULL);
            float v = a.p.volume;
            if (ks[SDL_SCANCODE_UP])
                v += 0.01f;
            if (ks[SDL_SCANCODE_DOWN])
                v -= 0.01f;
            if (v < 0.0f)
                v = 0.0f;
            if (v > 2.0f)
                v = 2.0f;
            if (v != a.p.volume) {
                SDL_LockAudioDevice(a.dev);
                a.p.volume = v;
                SDL_UnlockAudioDevice(a.dev);
            }
        }
        if (now - last_poll >= POLL_MS) {
            last_poll = now;
            poll_reload(&a);
        }
        if (now - last_title >= 100) {
            last_title = now;
            update_title(&a, win);
        }
        draw(&a, ren);
        SDL_Delay(15);
    }

    SDL_CloseAudioDevice(a.dev);
    player_set_seq(&a.p, NULL);
    tsf_close(a.p.sf);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv)
{
    int rate = 44100, i = 1;

    setvbuf(stdout, NULL, _IONBF, 0); /* log lines show up at once when redirected */

    if (i + 1 < argc && strcmp(argv[i], "--rate") == 0) {
        rate = atoi(argv[i + 1]);
        i += 2;
        if (rate != 44100 && rate != 22050) {
            fprintf(stderr, "--rate must be 44100 or 22050\n");
            return 2;
        }
    }
    if (i + 1 < argc && strcmp(argv[i], "--check") == 0)
        return check_mode(argv[i + 1]);
    if (i < argc && strcmp(argv[i], "--render") == 0) {
        if (argc - i < 4) {
            fprintf(stderr, "usage: midiplay [--rate N] --render <export_dir> <track> <out.wav> [seconds]\n");
            return 2;
        }
        return render_mode(argv[i + 1], atoi(argv[i + 2]), argv[i + 3], argc - i > 4 ? atof(argv[i + 4]) : 30.0, rate);
    }
    if (i >= argc) {
        fprintf(stderr, "usage: midiplay [--rate 44100|22050] <export_dir> [track]\n"
                        "       midiplay [--rate N] --render <export_dir> <track> <out.wav> [seconds]\n");
        return 2;
    }
    return interactive(argv[i], i + 1 < argc ? atoi(argv[i + 1]) : -1, rate);
}
