#ifndef MUSICSEQ_H
#define MUSICSEQ_H
/*
 * musicseq.h -- the sequencer of the pack music (docs/specs/audio.md "Writing a Standard MIDI File", "How a player loops
 * a file with these markers"). Pure: no synth, no game, no threads. It reads an exported Standard MIDI File (format 0 or
 * 1) with its loopStart <id> / loopEnd <id> <count|inf> markers and plays it into a sink:
 *
 *   - one read position per MIDI track and one played-time clock (in ticks); tracks loop independently;
 *   - each loop end keeps a remaining-repeats counter (from the file, reset when it falls through);
 *   - a jump moves only that track: the next event is due at (now) + (its tick - the loop start's tick);
 *   - notes end by duration (paired with their note-off in the file), in played time, so a note crossing a loop end keeps
 *     ringing into the next pass; the note-off events met while reading are ignored;
 *   - a track ends at its end-of-track; the sequence is over when every track ended and the scheduled note-offs fired.
 *
 * Tempo: one tick lasts trunc(tempo / division) microseconds; 500000 until the first tempo event; any track's tempo
 * event sets the tempo of the whole sequence.
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MSQ_NOTE_ON = 1,    /* a = key, b = velocity, dur = duration in ticks */
    MSQ_CONTROL,        /* a = controller, b = value */
    MSQ_PROGRAM,        /* a = program */
    MSQ_BEND,           /* val = 0..16383 */
    MSQ_TEMPO,          /* val = microseconds per quarter note */
    MSQ_LOOP_START,     /* val = loop start id */
    MSQ_LOOP_END,       /* val = loop start id, b = count (255 = forever), counter = index of its counter */
    MSQ_END             /* end of track */
};

typedef struct MsqEvent {
    uint32_t tick;
    uint32_t dur;
    uint32_t val;
    uint16_t counter;
    uint8_t type, ch, a, b;
} MsqEvent;

typedef struct MsqTrack {
    MsqEvent *ev;
    int n;
    int *loop_start_at;   /* event index of each loopStart id (-1 none) */
    int nloops;
} MsqTrack;

typedef struct MsqSeq {
    int division;
    int ntracks;
    MsqTrack *tracks;
    int ncounters;        /* loop end markers, all tracks */
    uint8_t *counter_init;/* their counts (255 = forever) */
    int unresolved;       /* loopEnd markers naming a loopStart id the track does not have (ignored) */
} MsqSeq;

/* Parses `len` bytes. NULL on a malformed file (err filled). Free with msqFree. */
MsqSeq *msqLoad(const unsigned char *mid, size_t len, char *err, size_t errlen);
void msqFree(MsqSeq *s);

/* What the player does to the synth. */
typedef struct MsqSink {
    void *user;
    void (*note_on)(void *user, int ch, int key, int vel);
    void (*note_off)(void *user, int ch, int key);
    void (*control)(void *user, int ch, int cc, int value);
    void (*program)(void *user, int ch, int program);
    void (*bend)(void *user, int ch, int value);
} MsqSink;

#define MSQ_MAX_HELD 1024

typedef struct MsqTrackPos {
    int pos;          /* next event index */
    int64_t offset;   /* due tick of an event = its tick + offset */
    int ended;
} MsqTrackPos;

typedef struct MsqPlayer {
    const MsqSeq *seq;
    MsqTrackPos *trk;        /* seq->ntracks */
    uint8_t *remain;         /* seq->ncounters */
    double now;              /* played time, in ticks */
    double last_due;         /* due tick of the last event dispatched */
    uint32_t us_per_tick;
    int nheld;
    struct { double due; uint8_t ch, key; } held[MSQ_MAX_HELD];   /* scheduled note-offs */
    int ended;               /* every track ended and every note-off fired */
    uint32_t jumps;          /* loop jumps taken (diagnostics, tests) */
} MsqPlayer;

/* Starts `seq` from its beginning (counters from the file). 0 on allocation failure. */
int msqStart(MsqPlayer *p, const MsqSeq *seq);
void msqStopPlayer(MsqPlayer *p);          /* frees the player's arrays (not the sequence) */
/* Advances the played time by `us` microseconds and dispatches every event and note-off now due, in due order (at equal
 * due ticks the note-offs first, then the tracks in order). Returns 1 while the sequence plays, 0 once it is over. */
int msqAdvance(MsqPlayer *p, double us, const MsqSink *sink);
/* Pause / resume (a fade-out to 0, then a fade-in): drops the scheduled note-offs (the synth cuts the notes); on resume the
 * next event is waited for in full from the last one dispatched. */
void msqPause(MsqPlayer *p);
void msqResume(MsqPlayer *p);

#ifdef __cplusplus
}
#endif
#endif
