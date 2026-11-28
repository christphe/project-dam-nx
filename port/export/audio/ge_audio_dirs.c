/*
 * ge_audio_dirs.c -- the audio export written as folders: music/track_NN.mid, instruments.sf2, sfx/sfx_NNNN.wav and
 * sfx/sfx.json, with a report (ge_ae_export_dirs in ge_audio_export.h). Shared by ge007-audio-export and the base asset
 * pack (port/export/pack_export.c). Moved here unchanged from ge007-audio-export.c.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(p) mkdir((p), 0777)
#endif

#include "ge_audio_export.h"

static int g_errors;

static void fail(FILE *rep, const char *fmt, const char *what, const char *msg)
{
    g_errors++;
    fprintf(stderr, fmt, what, msg);
    fputc('\n', stderr);
    if (rep)
        fprintf(rep, fmt, what, msg), fputc('\n', rep);
}

static int make_dir(const char *path)
{
    if (MKDIR(path) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f)
        return -1;
    ok = fwrite(data, 1, n, f) == n;
    if (fclose(f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

/* The track default volume D[t] (docs/specs/audio.md "Volume chain"), one per track number 0..63. */
#define GE_AE_MUSIC_DEFAULT_VOLUMES 64
static const int s_default_volume[GE_AE_MUSIC_DEFAULT_VOLUMES] = {
    0x6665, 0x7332, 0x7332, 0x7998, 0x7332, 0x5998, 0x6665, 0x6665, 0x6665, 0x6665, /* 0..9 */
    0x7332, 0x6665, 0x7332, 0x6665, 0x6665, 0x7332, 0x7332, 0x6665, 0x3FFF, 0x6665, /* 10..19 */
    0x6665, 0x3FFF, 0x6665, 0x6665, 0x6665, 0x6665, 0x6665, 0x7998, 0x6665, 0x5998, /* 20..29 */
    0x6665, 0x6665, 0x6665, 0x6665, 0x6665, 0x6665, 0x6665, 0x5998, 0x6665, 0x3332, /* 30..39 */
    0x6665, 0x7332, 0x7332, 0x5998, 0x7332, 0x6665, 0x6665, 0x6665, 0x6665, 0x6665, /* 40..49 */
    0x6665, 0x6665, 0x6CCB, 0x6665, 0x6665, 0x6665, 0x6665, 0x6665, 0x6665, 0x6665, /* 50..59 */
    0x7332, 0x6665, 0x7332, 0x7998,                                                 /* 60..63 */
};

/* music/music.json: {"default_volume": [D[0], ..., D[63]]} (decimal, 0..32767). */
static int write_music_json(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i, ok;
    if (!f)
        return -1;
    fprintf(f, "{\n  \"default_volume\": [");
    for (i = 0; i < GE_AE_MUSIC_DEFAULT_VOLUMES; i++)
        fprintf(f, "%s%s%d", i ? "," : "", i % 10 ? " " : "\n    ", s_default_volume[i]);
    fprintf(f, "\n  ]\n}\n");
    ok = !ferror(f);
    if (fclose(f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

static void export_music(const uint8_t *rom, size_t size, const ge_ae_layout *lay, int ntracks, const char *out_dir,
                         FILE *rep)
{
    int i, written = 0;
    char path[1024], err[GE_AE_ERRLEN], what[32], chg[200];

    snprintf(path, sizeof path, "%s/music", out_dir);
    if (make_dir(path)) {
        fail(rep, "cannot create %s: %s", path, strerror(errno));
        return;
    }
    fprintf(rep, "== music: %d table entries ==\n", ntracks);
    for (i = 0; i < ntracks; i++) {
        ge_ae_music_stats st;
        ge_ae_buf mid;
        int rc = ge_ae_music_convert(rom, size, lay, i, &mid, &st, err);
        snprintf(what, sizeof what, "track %02d", i);
        if (rc == 1) {
            fprintf(rep, "track %02d: no data\n", i);
            continue;
        }
        if (rc < 0) {
            fail(rep, "%s: ERROR %s", what, err);
            continue;
        }
        snprintf(path, sizeof path, "%s/music/track_%02d.mid", out_dir, i);
        if (write_file(path, mid.data, mid.size)) {
            fail(rep, "%s: cannot write %s", what, path);
            ge_ae_buf_free(&mid);
            continue;
        }
        written++;
        {
            int q, o = 0;
            chg[0] = 0;
            for (q = 0; q < 16; q++)
                if (st.ends_changed[q])
                    o += snprintf(chg + o, sizeof chg - (size_t)o, "%s%d:%u", o ? "," : "", q + 1,
                                  (unsigned)st.ends_changed[q]);
            if (!o)
                strcpy(chg, "none");
        }
        fprintf(rep,
                "track %02d: usize=%u csize=%u tracks=%d division=%u events=%u notes=%u tempo_changes=%u "
                "loopStart=%u loopEnd=%u unknown_metas=%u channels=0x%04X length_ticks=%u first_tempo_us=%u "
                "loop_ticks=%u..%u smf_bytes=%zu overlaps_cut=%u ends_changed=%s%s%s\n",
                i, (unsigned)st.usize, (unsigned)st.csize, st.tracks, (unsigned)st.division, (unsigned)st.events,
                (unsigned)st.notes, (unsigned)st.tempo_changes, (unsigned)st.loop_starts, (unsigned)st.loop_ends,
                (unsigned)st.unknown_metas, (unsigned)st.channel_mask, (unsigned)st.length_ticks,
                (unsigned)st.first_tempo_us, (unsigned)st.loop_start_tick, (unsigned)st.loop_end_tick, mid.size,
                (unsigned)st.overlaps_cut, chg, st.warning[0] ? " WARNING: " : "", st.warning);
        {
            int q;
            for (q = 0; q < 16; q++) {
                if (st.overlaps_track[q])
                    fprintf(rep, "track %02d t%d: %u same-key overlaps cut\n", i, q, (unsigned)st.overlaps_track[q]);
                if (st.dead_loop_ends[q])
                    fprintf(rep, "track %02d t%d: %u unreachable loop end(s) dropped\n", i, q, (unsigned)st.dead_loop_ends[q]);
            }
        }
        ge_ae_buf_free(&mid);
    }
    fprintf(rep, "music: %d files written\n", written);
    snprintf(path, sizeof path, "%s/music/music.json", out_dir);
    if (write_music_json(path))
        fail(rep, "%s: cannot write %s", "music.json", path);
    else
        fprintf(rep, "music: music.json written (%d default volumes)\n", GE_AE_MUSIC_DEFAULT_VOLUMES);
    fprintf(rep, "\n");
}

static void export_bank(const uint8_t *rom, size_t size, const char *label, uint32_t ctl, uint32_t ctl_size,
                        uint32_t tbl, uint32_t tbl_size, const char *out_dir, int sfx, FILE *rep)
{
    char err[GE_AE_ERRLEN], path[1024];
    ge_ae_bank *b = ge_ae_bank_load(rom, size, ctl, ctl_size, tbl, tbl_size, sfx, err);
    int i, k, wavs = 0, maxpeak = 0;

    fprintf(rep, "== bank: %s ==\n", label);
    if (!b) {
        fail(rep, "%s: ERROR %s", label, err);
        fprintf(rep, "\n");
        return;
    }
    for (i = 0; i < b->ninst; i++) {
        const ge_ae_inst *in = &b->inst[i];
        fprintf(rep, "instrument %d: %s vol=%u pan=%u bend_cents=%d sounds=%d\n", i, in->present ? "present" : "ABSENT",
                in->vol, in->pan, in->bend_cents, in->nsounds);
        for (k = 0; k < in->nsounds; k++) {
            const ge_ae_sound *s = &in->sounds[k];
            const ge_ae_wave *w;
            int peak = 0;
            uint32_t j;
            if (!s->present)
                continue;
            if (!s->valid) {
                fprintf(rep, "  sound %d: INVALID\n", k);
                continue;
            }
            w = &b->waves[s->wave];
            for (j = 0; j < w->nsamples; j++) {
                int v = w->pcm[j] < 0 ? -w->pcm[j] : w->pcm[j];
                if (v > peak)
                    peak = v;
            }
            if (peak > maxpeak)
                maxpeak = peak;
            fprintf(rep, "  sound %d: wave@0x%X %s samples=%u peak=%d loop=%s", k, (unsigned)w->src_off,
                    w->type ? "raw" : "adpcm", (unsigned)w->nsamples, peak,
                    w->has_loop ? (w->loop_count == 0xFFFFFFFFu ? "forever" : "finite") : "none");
            if (sfx)
                fprintf(rep, " keymap_bytes=%u,%u,%u,%u,%u,%d", s->vel_min, s->vel_max, s->key_min, s->key_max, s->key_base,
                        s->detune);
            else
                fprintf(rep, " keys=%u..%u base=%u detune=%d vel=%u..%u", s->key_min, s->key_max, s->key_base, s->detune,
                        s->vel_min, s->vel_max);
            fprintf(rep, " env_us=%d/%d/%d env_vol=%u/%u vol=%u pan=%u\n", (int)s->attack_us, (int)s->decay_us,
                    (int)s->release_us, s->attack_vol, s->decay_vol, s->vol, s->pan);
        }
    }
    fprintf(rep, "summary %s: instruments=%d sounds=%d waves=%d adpcm=%d raw=%d looped=%d sample_rate=%d peak=%d errors=%d\n",
            label, b->ninst, b->total_sounds, b->nwaves, b->adpcm_waves, b->raw_waves, b->looped_waves, b->sample_rate,
            maxpeak, b->errors);
    for (i = 0; i < b->finite_loop_n; i++)
        fprintf(rep, "finite loop: instrument %u sound %u count %u (exported as continuous)\n",
                (unsigned)b->finite_loops[i].inst_index, (unsigned)b->finite_loops[i].sound_index,
                (unsigned)b->finite_loops[i].count);
    if (b->errors) {
        g_errors += b->errors;
        fprintf(rep, "ERRORS: %d (first: %s)\n", b->errors, b->first_error);
        fprintf(stderr, "%s: %d decode errors (first: %s)\n", label, b->errors, b->first_error);
    }

    if (!sfx) {
        ge_ae_buf sf2;
        if (ge_ae_bank_to_sf2(b, label, &sf2, err)) {
            fail(rep, "%s: SF2 ERROR %s", label, err);
        } else {
            snprintf(path, sizeof path, "%s/instruments.sf2", out_dir);
            if (write_file(path, sf2.data, sf2.size))
                fail(rep, "cannot write %s%s", path, "");
            ge_ae_buf_free(&sf2);
        }
    } else {
        ge_ae_buf json;
        ge_ae_sfx_stats st;
        snprintf(path, sizeof path, "%s/sfx", out_dir);
        if (make_dir(path)) {
            fail(rep, "cannot create %s: %s", path, strerror(errno));
        } else {
            for (k = 0; k < b->inst[0].nsounds; k++) {
                ge_ae_buf wav;
                ge_ae_sfx p;
                int peak;
                if (ge_ae_sfx_params(b, k, &p) || ge_ae_sound_to_wav(b, 0, k, p.rate_hz, &wav, &peak))
                    continue;
                snprintf(path, sizeof path, "%s/sfx/sfx_%04d.wav", out_dir, p.id);
                if (write_file(path, wav.data, wav.size))
                    fail(rep, "cannot write %s%s", path, "");
                else
                    wavs++;
                ge_ae_buf_free(&wav);
            }
            fprintf(rep, "sfx: %d wav files written\n", wavs);
            if (ge_ae_sfx_json(b, &json, &st)) {
                fail(rep, "%s: sfx.json ERROR %s", label, "out of memory");
            } else {
                int f;
                snprintf(path, sizeof path, "%s/sfx/sfx.json", out_dir);
                if (write_file(path, json.data, json.size))
                    fail(rep, "cannot write %s%s", path, "");
                ge_ae_buf_free(&json);
                fprintf(rep, "sfx effects=%d looped=%d held(decay -1)=%d retrigger=%d pitch_slide=%d\n", st.effects, st.loops,
                        st.held, st.retrigger, st.slides);
                fprintf(rep, "sfx flags (raw high nibble) counts:");
                for (f = 0; f < 16; f++)
                    if (st.flag_count[f])
                        fprintf(rep, " 0x%X0=%d", f, st.flag_count[f]);
                fprintf(rep, "\nsfx chains: links=%d heads=%d longest=%d effects bad_next=%d cycles=%d cycles_without_retrigger=%d\n",
                        st.links, st.chains, st.longest_chain, st.bad_next, st.cycles, st.cycles_without_retrigger);
                if (st.bad_next || st.cycles_without_retrigger) {
                    g_errors++;
                    fprintf(stderr, "sfx: chain problems (bad next %d, cycles without retrigger %d)\n", st.bad_next,
                            st.cycles_without_retrigger);
                }
            }
        }
    }
    fprintf(rep, "\n");
    ge_ae_bank_free(b);
}

int ge_ae_export_dirs(const uint8_t *rom, size_t size, const ge_ae_layout *lay, int ntracks, const char *music_root,
                      const char *instruments_dir, const char *sfx_root, FILE *rep)
{
    g_errors = 0;
    export_music(rom, size, lay, ntracks, music_root, rep);
    export_bank(rom, size, "instruments", lay->ins_ctl, lay->ins_ctl_size, lay->ins_tbl, lay->ins_tbl_size, instruments_dir, 0, rep);
    export_bank(rom, size, "sfx", lay->sfx_ctl, lay->sfx_ctl_size, lay->sfx_tbl, lay->sfx_tbl_size, sfx_root, 1, rep);
    return g_errors;
}
