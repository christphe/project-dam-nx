#ifndef MUSICPACK_H
#define MUSICPACK_H
/*
 * musicpack.h -- the game's music from the asset packs: music/track_NN.mid (NN = track number) rendered with
 * music/instruments.sf2 through TinySoundFont, honouring music/music.json's track default volumes. Behaviour:
 * docs/specs/audio.md "Music playback". GE_ROM_MUSIC=1 keeps the ROM sequence player.
 *
 * Three slots (0, 1, 2 = the game's music tracks 1..3). Game thread: mpkPlay .. mpkFadeIn (they read the stack).
 * Retrace: mpkFadeTick. Audio thread: mpkMix. A missing MIDI or SoundFont is silence and one log line.
 */
#ifdef __cplusplus
extern "C" {
#endif

int mpkActive(void);                         /* 1 when the pack path is in use (GE_ROM_MUSIC unset) */
void mpkPlay(int slot, int track);
void mpkStop(int slot, int *state);           /* state: the slot's fade state (-1 out, 0 none, 1 in), cleared */
unsigned mpkGetVolume(int slot);
void mpkSetVolume(int slot, unsigned v);     /* v 0..0x7FFF */
void mpkSaveVolumeAsDefault(int slot);
void mpkFadeOut(int slot, float seconds, int *state);
void mpkFadeIn(int slot, float seconds, unsigned target, int *state);   /* target 0xFFFF = the pre-fade volume */
/* Once per video retrace. state[i] points at slot i's fade state (-1 out, 0 none, 1 in), which the game also writes
 * (0 cancels). */
void mpkFadeTick(int *const state[3]);
/* GE_MUSICLOG=1: one log line per music request (both paths), with the retrace count, for comparisons. */
void mpkLogRequest(const char *what, int slot, int a, int b);
void mpkRetrace(void);                       /* counts retraces for mpkLogRequest (called by the fade tick, both paths) */
/* Audio thread: adds the music into `frames` interleaved stereo s16 frames at 22050 Hz (saturating). */
void mpkMix(short *buf, int frames);

#ifdef __cplusplus
}
#endif
#endif
