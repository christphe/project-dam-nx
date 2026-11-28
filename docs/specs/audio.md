# Audio: instrument banks, compressed sequences, music table

GoldenEye's music is a set of **compressed MIDI-like sequences** played by a
sample-based synthesizer with an **instrument bank**; its sound effects are
the sounds of a second bank. This page gives the ROM locations and the byte
layouts needed to rebuild them as standard files: each sequence as a Standard
MIDI File, each bank as a SoundFont 2 file. All values big-endian. Written
from the decompilation in the lead's own words; locations measured on the
three retail ROMs.

## Where things are

| Item | US (`ntsc-final`) | EU (`pal-final`) | JP (`jpn-final`) | Size |
|---|---|---|---|---|
| SFX bank header file ("ctl") | `0x2EBDE0` | `0x2E2220` | `0x2ECB00` | `0x5BC0` |
| SFX sample data ("tbl") | `0x2F19A0` | `0x2E7DE0` | `0x2F26C0` | `0xC2AB0` |
| Instrument bank ctl | `0x3B4450` | `0x3AA890` | `0x3B5170` | `0x43A0` |
| Instrument sample data tbl | `0x3B87F0` | `0x3AEC30` | `0x3B9510` | `0x60FA0` |
| Music table | `0x419790` | `0x40FBD0` | `0x41A4B0` | `4 + 8 * 63` |

Each tbl directly follows its ctl. Both ctl files start with the bytes
`42 31 00 01` (format revision "B1", one bank). How the EU/JP values were
found: the two occurrences of `42 31 00 01` in the ROM are the two ctl files,
the music table is the one place where a 16-bit count `n` (40..80), a zero
16-bit word and `n` entries whose first offset equals `4 + 8n` all
decompress to their declared sizes. Check these invariants before use.

## Music table

| Bytes | Field |
|---|---|
| 0-1 | number of tracks `n` (63 in all three ROMs) |
| 2-3 | 0 |
| then `n` entries of 8 bytes | offset of the track's compressed data, **relative to the start of the table** (u32); uncompressed size (u16); compressed size (u16) |

Track 0 is a "no music" placeholder; a track whose compressed size is 0 has
no data. Each track's data is a rarezip block (level-decor.md §Block
compression: 2 header bytes, then raw DEFLATE) that decompresses to exactly
the declared uncompressed size: a **compressed sequence**.

The music uses the **instrument bank**: a program change to program `p`
selects instrument `p` of that bank. Every MIDI channel works the same way;
channel 10 has **no** drum meaning (when playing the exported MIDI with a
General MIDI-minded synth, force channel 10 to a melodic bank).

## Compressed sequence

### Header

| Bytes | Field |
|---|---|
| 0-63 | 16 track offsets (u32), from the start of the sequence; 0 = track absent |
| 64-67 | division: ticks per quarter note (u32) |

A track is a stream of events whose status bytes name their MIDI channel;
do not assume track `i` = channel `i`. The tracks play in parallel: at any
time the next event is the one with the smallest absolute tick over all
tracks.

### Reading a track's bytes (back-references)

The track data is compressed with back-references. A reader keeps, per track,
the current position and an optional "replay" window. To get the next byte:

1. If a replay window is active, return its next byte and shorten it by one.
   Bytes taken from a replay window are returned as they are (they are never
   interpreted as markers themselves).
2. Otherwise read the byte at the current position and advance. If it is not
   `0xFE`, return it.
3. If it is `0xFE`, read the next byte. If that byte is also `0xFE`, the pair
   stands for one literal `0xFE`: return `0xFE`.
4. Otherwise that byte and the one after it form a 16-bit distance `D`
   (high byte first), and the byte after those is a length `L`. Let `P` be
   the position of the first `0xFE` of this 4-byte marker. Start a replay
   window of `L` bytes at position `P - D`, and return its first byte (the
   window then has `L - 1` bytes left).

### Events

Delta times and note durations are MIDI variable-length numbers (7 bits per
byte, high bit = more bytes follow), read through the byte reader above.

A track is: a delta time, then an event, then a delta time, then an event,
and so on. Events:

- **Meta** (status `0xFF`, never running status), then a type byte:
  - `0x51` tempo: 3 bytes, microseconds per quarter note (as in MIDI).
  - `0x2F` end of track: no length byte follows; the track ends.
  - `0x2E` loop start: 2 bytes follow, read through the byte reader: a loop
    number and a filler byte (`0xFF` in every US sequence). The player does
    nothing at a loop start; it only marks a place a loop end can jump back
    to. The loop number is never used by the player (see §Loops).
  - `0x2D` loop end: 6 bytes follow, read **raw** from the current position
    (not through the byte reader, so no `0xFE` handling; no US loop end has
    a `0xFE` among them): a loop count (u8), a current-count byte (u8), and
    a 32-bit distance `B`. What a loop end does: §Loops.
  - any other type: unknown, report it.
  - After any meta event (loop events included), running status is cleared.
- **Channel message**: if the byte has its high bit set it is a new status
  (and becomes the running status); otherwise it is the first data byte and
  the running status is used. Program change (`0xC_`) and channel pressure
  (`0xD_`) have one data byte; all others two. **Note-on (`0x9_`) is
  followed by a variable-length duration in ticks**: there are no note-off
  events in the stream. A note-on with velocity 0 does not occur.

### Which note a note-off ends (important)

Each note-on schedules a note-off for its channel and key at `tick +
duration`. When that note-off fires, the game's player does **not** end the
note that scheduled it: it ends the **oldest note still held on the same
channel with the same key** (notes are kept in start order; the first match
that is not already released wins). A note whose sample has simply run out
still counts as held until a note-off ends it.

This matters whenever notes of the same key overlap on a channel. Example
measured in track 2, channel 15: two notes start at tick 4 with a duration of
3692, and the same keys are struck again at ticks 242, 1141, 1395, ... with
durations of 59 to 114 ticks. In the game, the note-off scheduled by the
note at 242 (tick 356) ends the **tick-4** note; the 242 note then rings until
the next note-off of that key (tick 1200), i.e. its unlooped 1.06 s sample
plays out. Ending each note at its own `tick + duration` instead cuts these
notes after ~70 ms, which is audibly wrong.

So to compute when each note really ends, replay the note-offs in tick order
per (channel, key) against a first-in-first-out list of held notes: each
note-off ends the oldest held note of that key (ignore a note-off that finds
none). Every note's real end is the tick of the note-off that ended it (or
the end of the sequence if none did).

### Loops

Measured on the US ROM, all 63 sequences, by decoding the raw sequence bytes
(not the exported MIDI); examples name the music table entry (`track_NN`)
and the sequence track (`t0`..`t15`). Division is 384 in all 63.

**A loop end jumps by distance, not by number.** When a loop end is taken,
reading continues at the position just after its 6 bytes minus `B`. In every
reachable loop end of the US ROM that position is exactly the position just
after the 2 bytes of an earlier loop start of the same track (where the delta
time of the event following that loop start begins). So the loop body is
everything from the event after that loop start up to and including the loop
end. The loop number in the loop start does **not** pair them:

- track_04 t7: a loop start numbered 1 at tick 76800 is still open when the
  loop end at 79872 jumps back to the loop start numbered 0 at 9216.
- track_27 t6 and t8, track_52 t6, track_58 t6, track_59 t6, track_35 t10:
  two loop starts at the same tick, 5 bytes apart (both numbered 0 where
  checked); the loop end lands on one of them.
- track_12 t0: two loop ends jump to the **same** loop start (13824): one
  with count 43 at 15360, then one looping forever at 46080.

Resolve every loop end to its loop start by the distance. Loops nest
naturally: an inner loop end jumps to an inner start, an outer one to an
outer start (track_05 t2: starts at 10752 [no. 0], 35328 [no. 2], end at
36864 count 6 back to 35328, start 38400 [no. 3], end 44544 count 3 back to
38400, end 50688 forever back to 10752).

**Count.** The two count bytes are equal in every loop end of the US ROM.
The player uses the current-count byte, which it rewrites in the sequence
data as it goes:

- `0xFF`: loop forever (jump every time; the byte is never changed).
- `0`: do not jump; continue after the loop end (the body plays once).
  Occurs once (track_50 t11).
- `N` (1..254): jump back, and lower the current count by one. When the loop
  end is reached with the current count at 0, do not jump, reset the current
  count to the loop count, and continue after the loop end. So **`N` means
  `N` repeats: the body plays `N + 1` times**, and each time the loop is
  entered again (from an outer loop) it again plays `N + 1` times.

Checked by the loop periods of track_05: once the inner loops are expanded
with "`N` repeats", every looping track has the same outer period of 67584
ticks (t0: 78336 - 10752; t2: 39936 + 6 x 1536 + 3 x 6144; t4: 56832 +
7 x 1536; t8: 47616 + 6 x 1536 + 7 x 1536). In t9 an intro loop (3072..4608,
count 3) before the outer loop delays t9's outer loop start from 6144 to
10752, the outer loop start of every other track. With "`N` passes" these
periods would not agree.

Because the counts are rewritten in the sequence data, they start from the
ROM values only when the sequence is loaded afresh; the game decompresses
the sequence again on every play (§Music playback), so each play starts with
the ROM counts.

**Tracks loop independently.** Each track has its own read position; a loop
end moves only its own track. The player's time keeps running forward (ticks
are never reset by a jump). Tracks stay in step only because the music was
written that way: track_08 t0, t2, t3 loop 6144..12288 (period 6144) while
t1, t4..t15 loop 6144..104448 (period 98304 = 16 x 6144).

**A loop start with no loop end** does nothing: the track plays through once
(track_38: every track has loop starts, no track has a loop end; track_11
t7 and t13 have a loop start and no loop end while t0 loops forever, so after
the first pass those two tracks are silent for good. That is the game's
behaviour, keep it).

**Track end.** At its end-of-track event a track stops; nothing restarts it.
When the last track of a sequence ends the sequence is over and its slot
stops (§Music playback, "natural end"). A track that loops forever never
reaches its end, so a sequence with at least one forever loop never ends.
US: 59 sequences have a forever loop; track_38 and track_60..62 (no loop
markers at all) play once and stop. Some tracks have no notes and end early
(track_17 t0, a tempo track ending at 62978); tempo set by any track applies
to the whole sequence and stays after that track ends.

**Unreachable loop ends.** In 8 tracks (track_04 t7, track_27 t6 and t8,
track_28 t1, track_46 t15, track_52 t6, track_58 t6, track_59 t6) a second
loop end follows a forever loop end; it can never be reached and its distance points before the start
of the track. Do not resolve it; drop it and report it.

**Notes across a jump.** A jump does not end held notes. Each note ends at
its note-off time (note-on time + duration, in played time, with the
"oldest held note" rule above), even when that falls after a jump back. US:
69 notes start inside a loop body and end after the loop end (34 sequences,
e.g. track_07 t5: four notes at 84480, durations 7704..7714, loop end
92160). A note-off scheduled before a jump fires after it, in the next pass.

### Writing a Standard MIDI File

Format 1, same division: one MIDI track per present sequence track, the same
events at the same absolute ticks, each note-on followed by a note-off
(`0x8_`, same key, velocity 0x40) at the note's **real end** from the rule
above (computed on the straight path through the track, ignoring jumps),
tempo events kept, loop markers as below, end-of-track appended. Loops are
**not** unrolled: read each track straight through once, continuing past
every loop end, so every byte of the track is written once. Events after a
forever loop end are written too (their note-offs may belong to notes of the
loop body).

A MIDI channel cannot hold two notes of the same key: if a note's real end
falls after the next note-on of the same channel and key, write its note-off
just before that next note-on (the older sound is cut there; report the
count of such overlaps per track).

**Loop markers** are marker meta events (`FF 06 len text`, ASCII), written in
the MIDI track of the sequence track they belong to, at the tick of the loop
event:

- `loopStart <id>`: one per loop start event. `<id>` is the 0-based index of
  this loop start among the loop starts of this MIDI track, in stream order.
  (The game's loop number is not written: the player never uses it.)
- `loopEnd <id> <count>`: one per loop end whose target resolves to a loop
  start; `<id>` is that loop start's id; `<count>` is the decimal number of
  repeats (`0`..`254`, from the current-count byte) or `inf` for `0xFF`.
  A loop end that does not resolve is not written (report it).

Example (track_05 t2): `loopStart 0` @10752, `loopStart 1` @35328,
`loopEnd 1 6` @36864, `loopStart 2` @38400, `loopEnd 2 3` @44544,
`loopEnd 0 inf` @50688.

Order of events at the same tick: note-offs first; then every other event
(note-ons, controllers, tempo, loop markers) in **stream order**, i.e. the
order the sequence track gives them. Stream order matters at loop ticks: an
event at tick T before the loop start (delta 0) is not part of the body, one
after it is; an event at the loop end's tick before the loop end is played
on every pass, one after it only when the loop falls through.

**How a player loops a file with these markers** (this is the game's
behaviour from §Loops):

1. Keep a read position per MIDI track, and one "played time" clock.
2. On `loopEnd <id> <count>`: keep a remaining-repeats counter per loop end
   marker, set to `<count>` when the file is loaded. If `inf`, jump; if the
   counter is 0, reset it to `<count>` and continue; otherwise lower it by
   one and jump. A jump moves this track's read position to the event just
   after `loopStart <id>`; the next event is due at (played time now) +
   (its tick - the loopStart tick). Other tracks are not touched.
3. End notes by duration, not by reading note-offs past the jump: at load,
   pair each note-on with its note-off in the same track (unique, since a
   channel never holds two notes of one key in the file), take duration =
   note-off tick - note-on tick, and on playback end each note at its own
   start + duration in played time. Note-off events met while reading are
   then ignored. This keeps the notes that cross a loop end ringing into the
   next pass as in the game.
4. A track ends at its end-of-track event; the sequence ends when every track
   has ended.
5. Counters restart from the file values whenever the slot starts the
   sequence from the beginning (§Music playback).

A plain MIDI player that ignores the markers plays every track once, straight
through, which is the straight path the note ends were computed on.

## Music playback (what the game asks of the player)

From the decompilation; times and rates are the US (NTSC) values unless
stated.

### Three slots

The game drives **three independent music slots** (slot 1, 2, 3). Each has
its own sequence player, its own copy of the sequence data, its own current
track number (0 = none), its own volume `v` (0..`0x7FFF`, starts at
`0x7FFF`) and its own fade state. All three use the instrument bank. How the
game uses them (for orientation): slot 1 = the level or menu music; slot 2 =
a second piece that crossfades with slot 1 (the pause-watch track 24 while
the watch is up, a level's "X" track, the end themes); slot 3 = a level's
background/ambient track. Track numbers are music table indices.

### Volume chain

- **Track default volume** `D[t]`: a table with one signed 16-bit entry per
  track number 0..63 (one more than the 63 table entries), followed by -1
  which ends the table. Initial values, index: value (hex): 0: 6665, 1: 7332, 2: 7332,
  3: 7998, 4: 7332, 5: 5998, 6..9: 6665, 10: 7332, 11: 6665, 12: 7332,
  13: 6665, 14: 6665, 15: 7332, 16: 7332, 17: 6665, 18: 3FFF, 19: 6665,
  20: 6665, 21: 3FFF, 22..26: 6665, 27: 7998, 28: 6665, 29: 5998,
  30..36: 6665, 37: 5998, 38: 6665, 39: 3332, 40: 6665, 41: 7332, 42: 7332,
  43: 5998, 44: 7332, 45..51: 6665, 52: 6CCB, 53..59: 6665, 60: 7332,
  61: 6665, 62: 7332, 63: 7998. Same in all regions.
- **Set volume** `(slot, v)`: store `v` as the slot's volume, and give the
  slot's player the volume `P = floor(v x D[current track] / 32768)`
  (current track 0 uses `D[0]`). It works whether the slot is playing or
  not, and does not touch the fade state.
- **The player volume `P` reaches the sound linearly, then is squared.**
  Each voice's level is the product of `P` with the note velocity, the
  channel volume (controller 7), the sound's sample volume, the envelope
  and tremolo, so it is linear in `P`. The mixer then squares the voice
  level ("closer to loudness"). Net: a slot's output amplitude scales with
  `(P / 32767)^2`. (Velocity, channel volume, sample volume and envelope
  level are squared the same way; how far a SoundFont synth already applies
  such curves is the player's tuning question.) A change of `P` reaches
  voices that are already sounding: a voice in its sustain moves to the new
  level over 1 ms; a voice in an attack or decay segment reaches it at the
  end of that segment.
- **Save the current volume as the track default** `(slot)`: `D[current
  track] = v` (the slot's stored volume, not `P`). Later set-volume calls
  for that track number, on any slot, scale by the new value. It does not
  change the sound until the next set volume. Only a debug menu calls it;
  the table is not saved.
- **Options.** The music volume option `M` (0..`0x7FFF`, default `0x7FFF`)
  is the watch menu's music slider; moving it sets slot 2's volume to `M` at
  once. In missions, slots 1 and 2 are started or faded in with volume `M`;
  slot 3 is started or faded in with the **sound-effect** volume option
  instead. At level load all three slot volumes are set to `0x7FFF`; the
  title, folder and intro screens set slot 1 to `0x7FFF` (ignoring `M`)
  before playing their music, the multiplayer menu uses `M`. The save file
  keeps the top 8 bits (`M >> 7`) and restores `(b << 7) | (b >> 1)`. There
  is no master volume beyond these two options.

### Play `(slot, t)`

1. If the slot has a current track (non-zero), **stop** it first (below,
   which also cancels any fade), then wait until the slot's player is fully
   stopped (the stop's 50 ms release has run).
2. Current track = `t`. Decompress track `t` from the ROM into the slot's
   buffer afresh (so the loop counters start from the ROM values) and start
   it **from its beginning**.
3. Set volume with the slot's stored `v` (unchanged by play: whatever the
   slot had last), scaled by `D[t]`.
4. Start playing.

Play does not otherwise change the fade state; the game's callers clear it
just before calling play. Playing the track that is already playing
restarts it from the start. Playing track 0 plays sequence 0 (a short
placeholder sequence of 97 bytes); the code has a fallback to track 1 for a
missing track, never taken with retail data.

### Stop `(slot)`

Clear the fade state (no fade). If the slot has a current track and its
player is playing: drop all its pending sequence events and note-offs,
release every sounding note over 50 ms, then free the voices. Current track
= 0. The slot's volume `v` is kept.

**Natural end** (the last track of the sequence reached end-of-track, see
§Loops): the player stops by itself, but only after the note-offs already
scheduled have fired; at that point any sound still releasing is cut. The
slot keeps its current track number (the game does not notice); a later
play waits for that stop to complete, then restarts as usual.

### Fades

The fade runs once per **video frame** (60 Hz NTSC, 50 Hz PAL), from the
vertical retrace, independent of the game's frame rate. Durations are given
in seconds and converted to a whole number of frames
`n = trunc(seconds x 60)` (x 50 on PAL).

- **Fade out** `(slot, seconds)`: ignored if a fade-out is already running.
  Otherwise remember the current volume `v` as the "pre-fade volume", target
  = 0, `n` frames, state = fading out. A fade-out replaces a running
  fade-in.
- **Fade in** `(slot, seconds, target)`: ignored if a fade-in is already
  running. Otherwise **start the slot's player playing** (if it is stopped
  it resumes where it stopped, see below; if it is playing nothing changes),
  target = the given volume (the value `0xFFFF` means "the pre-fade volume
  remembered by the last fade-out"), pre-fade volume = 0, `n` frames,
  state = fading in. A fade-in replaces a running fade-out. The fade starts
  from the slot's **current** volume, not from 0 (it is 0 when a fade-out
  ran to the end before).
- **Each frame**, for each slot that is fading: `v = v + trunc((target - v)
  / remaining)` (the division in floating point, the result truncated toward
  zero), set volume `v`, then `remaining = remaining - 1`. So the volume
  moves in a straight line (linear in `v`; perceived through the square
  above) and lands exactly on the target on the last frame. When `remaining`
  reaches 0 the fade ends (state = none); if the volume is then 0 the slot's
  player is stopped (50 ms release, as above) but the slot **keeps its
  current track and the sequence's position**.
- **Cancel**: stop clears the fade state; there is no other cancel. Setting
  the volume during a fade does not cancel it (the next frame continues from
  the new volume). A fade of 0 frames (`seconds x 60 < 1`) divides by 0 on
  its first frame: avoid; the shortest fade the game asks for is 0.02 s
  (1 frame).

**Pause and resume through fades.** Fade-out to 0 then fade-in on the same
slot resumes the sequence where it stopped: the next pending sequence event
is waited for in full from the moment of resuming (at most one gap between
events is stretched), the notes that were cut by the stop are not
re-struck, and the loop counters are as they were. The game uses this for
the pause watch: slot 2 plays track 24 while slot 1 fades out over 0.5 s;
on closing the watch slot 1 fades in over 0.5 s to `M` and slot 2 fades out.

### Timing

- Output sample rate 22050 Hz (stereo), both banks recorded at 22050 Hz.
- The sequence player is driven in microseconds: one tick lasts
  `trunc(tempo / division)` whole microseconds (tempo in microseconds per
  quarter note; division 384 in every US sequence). Until the first tempo
  event the tempo is 500000 (120 BPM). The truncation makes the game up to
  ~0.1 % faster than exact MIDI timing at the tempos used; matching it is
  optional.
- Events take effect on 16-sample boundaries of the output (0.73 ms).
- A tempo event from any track sets the tempo of the whole sequence.

## Bank file ("ctl") and samples ("tbl")

Every pointer inside a ctl file is a 32-bit offset **from the start of the
ctl file**; the sample `base` of a wave table is an offset **from the start
of the tbl file**. A 0 offset means "absent".

| Structure | Layout |
|---|---|
| Bank file | revision (s16, `0x4231`), bank count (s16, 1), then that many bank offsets (u32) |
| Bank | instrument count (s16), flags (u8), pad (u8), sample rate (s32, 22050 in both banks), percussion instrument offset (u32, unused here), then `count` instrument offsets (u32) |
| Instrument | volume (u8, 0-127), pan (u8, 0 = left, 64 = centre, 127 = right), priority (u8), flags (u8), tremolo type, rate, depth, delay (4 x u8), vibrato type, rate, depth, delay (4 x u8), bend range in cents (s16), sound count (s16), then that many sound offsets (u32) |
| Sound | envelope offset (u32), key map offset (u32), wave table offset (u32), sample pan (u8), sample volume (u8), flags (u8), pad |
| Envelope | attack time, decay time, release time (3 x s32, microseconds), attack volume (u8), decay volume (u8) |
| Key map | velocity min, velocity max, key min, key max, key base (5 x u8), detune (s8, cents) |
| Wave table | sample data offset in tbl (u32), length in bytes (s32), type (u8: 0 = ADPCM, 1 = raw 16-bit), flags (u8), pad (2), then for ADPCM: loop offset (u32), codebook offset (u32); for raw: loop offset (u32) |
| ADPCM codebook | order (s32), predictor count (s32), then `order * count * 8` coefficients (s16) |
| ADPCM loop | start, end, count (3 x u32, in samples; count `0xFFFFFFFF` = loop forever), then 16 s16 of decoder state at the loop start |
| Raw loop | start, end, count (3 x u32) |

The SFX bank has **one** instrument whose sounds are the individual sound
effects (sound `k` = effect `k`). The instrument bank has one instrument per
program (75 in the US ROM).

Pitch: a note with key `k` on a sound plays its sample at the ratio
`2 ^ (((k - key_base) * 100 + detune) / 1200)` relative to the bank sample
rate (22050 Hz). So in SoundFont terms: sample rate 22050, root key =
key base, fine tune = detune cents, key range and velocity range from the key
map.

Envelope (per voice): the gain goes from 0 to `attack volume` over the
attack time, then to `decay volume` over the decay time, holds there while
the note is held, and goes to 0 over the release time after the note ends.
Volumes are 0-127. The final level of a voice also scales with the note
velocity, the instrument volume and the sound volume (all 0-127), and its
pan combines the instrument pan and the sound pan. Mapping these to SoundFont
generators (attack, decay, sustain level as attenuation, release, initial
attenuation, pan) is an approximation: the player exists to tune it by ear.

### The SFX bank: the key map is not a key map

The game does not play sound effects through MIDI notes, and it reuses the
six key-map bytes of each SFX sound as effect parameters. Velocity and key
ranges are meaningless there (hence inverted or > 127 "ranges").

- **Effect numbers** are 1-based: effect `N` is sound `N-1` of the bank's
  single instrument (effect 0 means "no sound").
- **Pitch**: the effect plays at `2 ^ (cents / 1200)` times the bank rate
  (22050 Hz), with `cents = key_base * 100 + detune - 6000` (so key base 60
  and detune 0 play at the recorded rate). When flag `0x20` (below) is set,
  the detune is **not** part of the starting pitch: it is a pitch-slide step
  (cents) applied repeatedly while the effect plays.
- **Key min byte**: low 6 bits = volume group (0..63; the game has one volume
  per group, e.g. music vs effects); top 2 bits = high bits of the next
  effect in a chain (below).
- **Key max byte**: low 4 bits = reverb send (0..15, the game multiplies it
  by 8); high 4 bits = flags: `0x10` retrigger (the chain is re-posted),
  `0x20` pitch slide (see Pitch). `0x40` occurs on effects 169-182 (all but
  175 and 176) and is never read by the game: carried in the effect's state,
  tested nowhere. `0x80` does not occur. Record the nibble raw.
- **Chains**: an effect can start another one: next effect =
  `velocity_min + (key_min & 0xC0) * 4` (0 = end of chain), started
  `velocity_max * 33.333 ms` after this one.
- **Envelope**: as for instruments; a decay time of `-1` (`0xFFFFFFFF`) means
  "held until stopped" (looping effects such as alarms and engines), and
  those get a higher priority.
- **Volume / pan**: final volume scales with attack/decay volume x the
  sound's sample volume / 127 x the group volume; pan = sound pan (64 =
  centre).

For export, an SFX is a WAV plus metadata, not a SoundFont preset.

The data is a series of 9-byte frames, each giving 16 samples (16-bit
signed). With the codebook of order 2 (the case in these banks):

1. The frame's first byte: high nibble = `scale`, low nibble = predictor
   index `p`. Predictor `p` has two rows of 8 coefficients: `c0[0..7]` then
   `c1[0..7]` (the 16 coefficients of predictor `p` start at index `16p` of
   the codebook).
2. The 8 following bytes are 16 signed 4-bit values (high nibble first),
   each multiplied by `2^scale` (i.e. shifted left by `scale`).
3. The 16 samples are produced as two halves of 8. For a half, let `s1` and
   `s2` be the last and second-to-last output samples so far (0 at the very
   start, or the loop state when restarting a loop), and `x[0..7]` that
   half's 8 scaled values. Output sample `j` (0..7) is

   ```
   ( c0[j]*s2 + c1[j]*s1 + x[j]*2048 + sum over k < j of c1[j-1-k]*x[k] ) >> 11
   ```

   (arithmetic shift), clamped to -32768..32767.

The number of samples of a wave table is `length / 9 * 16`. A raw 16-bit wave
table is big-endian 16-bit samples.

SoundFont loops: if the wave table has a loop with count other than 0, set
the sample's loop points to `start` / `end` and the loop mode to "loop
continuously" (the bank's finite counts are rare; treat them as continuous
and report them); otherwise no loop.

## Open questions / uncertain

- **Regions.** The loop and playback rules come from the player code and
  hold for all three regions. The measured facts (counts, positions, the
  lists of tracks above, division 384) are US only; EU/JP sequences were not
  decoded. Confidence the rules carry over: high.
- **Note ends across a jump.** The exported real ends apply the "oldest held
  note" rule on the straight path. In the game, on a second pass the notes
  still ringing from the previous pass take part in that rule, and a
  note-off that belongs to a note before the loop start is not repeated on
  later passes. The duration-based player (step 3 above) is exact except for
  these same-key interactions at a loop boundary. Confidence that this is
  inaudible: medium-high (69 notes cross a loop end in the whole US set).
- **Loop events inside a back-reference window.** The jump target is a raw
  data position; if a loop start or loop end were read from a replay window
  the game would compute it from the position after the window marker. This
  never happens in the US data (checked on every loop event); an exporter
  should report it if it does.
- **The cut at a natural end.** Read from the player: the final stop is
  queued behind the last pending event, so voices still in their release
  then are cut. Not listened to. Confidence: medium-high.
- **Slot roles** in §Three slots are an orientation summary of the mission
  music code, not a full map of which track plays when. Confidence: medium.
