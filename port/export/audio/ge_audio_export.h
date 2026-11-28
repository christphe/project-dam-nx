/*
 * ge_audio_export: ROM -> Standard MIDI Files + SoundFont 2 + WAV.
 *
 * Clean-room: written from docs/specs/audio.md and the public SMF / SoundFont
 * 2.04 specifications only. C99, libc + zlib. Everything works on memory
 * buffers; file and directory handling is the caller's business (see
 * ge007-audio-export.c). Every read of ROM data is bounds-checked: malformed
 * data yields an error return and a message, never a crash.
 */
#ifndef GE_AUDIO_EXPORT_H
#define GE_AUDIO_EXPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GE_AE_ERRLEN 256

/* ---- growable byte buffer ---------------------------------------------- */
typedef struct {
    uint8_t *data;
    size_t size, cap;
} ge_ae_buf;

void ge_ae_buf_free(ge_ae_buf *b);

/* ---- region layout ------------------------------------------------------ */
typedef enum { GE_AE_NTSC = 0, GE_AE_PAL = 1, GE_AE_JPN = 2 } ge_ae_region;

typedef struct {
    const char *name; /* "ntsc-final" ... */
    uint32_t sfx_ctl, sfx_ctl_size, sfx_tbl, sfx_tbl_size;
    uint32_t ins_ctl, ins_ctl_size, ins_tbl, ins_tbl_size;
    uint32_t music_table;
} ge_ae_layout;

/* 0 on success. Accepts "ntsc-final", "pal-final", "jpn-final". */
int ge_ae_region_from_name(const char *name, ge_ae_region *out);
const ge_ae_layout *ge_ae_layout_for(ge_ae_region r);

/* Check the invariants of the spec (big-endian ROM, both ctl magics, ctl+tbl
 * adjacency and bounds, music table header). 0 = ok; otherwise a message in
 * err (GE_AE_ERRLEN bytes). *track_count receives the music table's count. */
int ge_ae_check_rom(const uint8_t *rom, size_t size, const ge_ae_layout *lay, int *track_count, char *err);

/* ---- music -------------------------------------------------------------- */
typedef struct {
    uint32_t usize, csize;  /* declared sizes of the compressed sequence */
    int tracks;             /* sequence tracks present */
    uint32_t division;      /* ticks per quarter note */
    uint32_t events;        /* events read (all tracks, incl. metas) */
    uint32_t notes;         /* note-ons */
    uint32_t tempo_changes;
    uint32_t loop_starts, loop_ends;
    uint32_t unknown_metas;
    uint32_t channel_mask;  /* bit n = channel n carries events */
    uint32_t ends_changed[16]; /* notes whose real end differs from tick + duration, per channel */
    uint32_t overlaps_cut;     /* notes cut just before the next same channel+key note-on */
    uint32_t overlaps_track[16];   /* overlaps_cut per sequence track */
    uint32_t dead_loop_ends[16];   /* loop ends per sequence track that resolve to no loop start (dropped) */
    uint32_t length_ticks;  /* last event tick, including note-offs */
    uint32_t first_tempo_us;/* 0 if no tempo event */
    uint32_t loop_start_tick, loop_end_tick; /* first marker of each kind */
    char warning[GE_AE_ERRLEN]; /* non-fatal oddity, empty if none */
} ge_ae_music_stats;

/* Convert music table entry `index`. Returns 0 = file written to *mid,
 * 1 = no data for this entry (stats left zero), -1 = error (err filled). */
int ge_ae_music_convert(const uint8_t *rom, size_t size, const ge_ae_layout *lay, int index, ge_ae_buf *mid,
                        ge_ae_music_stats *st, char *err);

/* ---- sound banks -------------------------------------------------------- */
typedef struct {
    int16_t *pcm; /* decoded samples (owned) */
    uint32_t nsamples;
    uint8_t type; /* 0 = ADPCM, 1 = raw */
    int has_loop; /* loop present with count != 0 */
    uint32_t loop_start, loop_end, loop_count;
    uint32_t src_off; /* wave table offset in the ctl, identifies the wave */
    int valid;        /* decoded without error */
} ge_ae_wave;

typedef struct {
    int present; /* sound offset was non-zero */
    int valid;   /* all referenced structures decoded */
    int wave;    /* index into bank->waves */
    int32_t attack_us, decay_us, release_us;
    uint8_t attack_vol, decay_vol;
    uint8_t vel_min, vel_max, key_min, key_max, key_base;
    int8_t detune;
    uint8_t pan, vol;
} ge_ae_sound;

typedef struct {
    int present;
    uint8_t vol, pan;
    int16_t bend_cents;
    int nsounds;
    ge_ae_sound *sounds;
} ge_ae_inst;

typedef struct {
    uint32_t inst_index, sound_index, count;
} ge_ae_finite_loop;

typedef struct {
    int sample_rate;
    int ninst;
    ge_ae_inst *inst;
    int nwaves;
    ge_ae_wave *waves;
    /* statistics */
    int total_sounds, adpcm_waves, raw_waves, looped_waves, errors;
    int finite_loop_n;
    ge_ae_finite_loop finite_loops[64]; /* first 64 sounds with a finite loop count */
    char first_error[GE_AE_ERRLEN];
} ge_ae_bank;

/* Parse and decode a bank. The bank is returned even when individual sounds
 * failed (bank->errors > 0, sound->valid == 0); NULL only when the bank
 * structure itself is unusable (err filled). */
ge_ae_bank *ge_ae_bank_load(const uint8_t *rom, size_t size, uint32_t ctl_off, uint32_t ctl_size, uint32_t tbl_off,
                            uint32_t tbl_size, int sfx, char *err);
void ge_ae_bank_free(ge_ae_bank *b);

/* SoundFont 2.04: one preset (bank 0, number = instrument index) and one SF2
 * instrument per bank instrument; one zone per sound. 0 on success. */
int ge_ae_bank_to_sf2(const ge_ae_bank *b, const char *bank_name, ge_ae_buf *out, char *err);

/* 16-bit mono WAV of one sound's sample at the bank sample rate. 0 on success. */
int ge_ae_sound_to_wav(const ge_ae_bank *b, int inst, int sound, uint32_t rate_hz, ge_ae_buf *out, int *peak);

/* ---- SFX effects (docs/specs/audio.md, "The SFX bank: the key map is not a key map") ------ */
typedef struct {
    int id;                    /* 1-based effect number = sound index + 1 */
    uint32_t rate_hz;          /* WAV rate that plays the effect at the game's pitch */
    int pitch_cents;
    int volume_group;
    int reverb;                /* raw 0..15 send (the game multiplies it by 8) */
    int flags;                 /* raw high nibble of the key max byte */
    int retrigger;
    int has_slide, slide_cents;
    int next;                  /* 0 = end of chain */
    int next_delay_ms;
} ge_ae_sfx;

/* Decode the effect parameters of sound `sound` of instrument 0. 0 on success. */
int ge_ae_sfx_params(const ge_ae_bank *b, int sound, ge_ae_sfx *out);

typedef struct {
    int effects, held, links, chains, longest_chain, retrigger, slides, loops;
    int flag_count[16];
    /* links = effects with a next effect; chains = those links that are no other effect's target */
    int bad_next;      /* chain targets that are not an existing effect */
    int cycles;        /* chain cycles found */
    int cycles_without_retrigger;
} ge_ae_sfx_stats;

/* sfx.json for all valid effects; fills stats. 0 on success. */
int ge_ae_sfx_json(const ge_ae_bank *b, ge_ae_buf *out, ge_ae_sfx_stats *st);

/* The whole audio export as folders (ge_audio_dirs.c): music_root/music/track_NN.mid, instruments_dir/instruments.sf2,
 * sfx_root/sfx/sfx_NNNN.wav and sfx_root/sfx/sfx.json, with a line per item in `rep` (may be NULL). Returns the number of
 * errors. */
int ge_ae_export_dirs(const uint8_t *rom, size_t size, const ge_ae_layout *lay, int ntracks, const char *music_root,
                      const char *instruments_dir, const char *sfx_root, FILE *rep);

#ifdef __cplusplus
}
#endif
#endif
