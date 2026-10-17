/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca core types: the track, parameters, the clock.
 * fm1-drummer (drums only, docs/drums-only.md): one track, the drum track (drums.c: its kits and voices, its
 * 64-step pattern of 16 lanes). SLOOP's three synth parts are gone; the parameter and global ids keep their numbers
 * (projects, locks, learned CCs and the editor name them by number), the synth ones unused. */
#include <stdint.h>
#define NTRK 1                   /* the drum track */
#define TRK_DRUM 0
#define NSTEP 64
#define HALF_FRAMES 256          /* I2S half buffer: 5.8 ms at 44.1 kHz */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS,
    F_SWING,                    /* 0..100: MPC swing, 50 % (straight) .. 75 % */
    F_FILT                      /* -64..63: the DJ filter, LP <- OFF -> HP */
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: the sound's level trim (1/2 dB), set by the presets so
                                                * every factory sound comes out as loud as the others (fx.c) */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* (the SLICER is gone, drums only: its ids stay until the project format
                                                * changes, docs/drums-only.md stage 8); new common parameters go just
                                                * before P_E0 (user presets and projects map by count) */
    P_CHORD,                                   /* chord mode: one key plays a chord of the scale (seq.c) */
    P_TFLT,                                    /* SLOOP 2.4: the track's filter, < 0 low-pass, > 0 high-pass (fx.c) */
    P_STRUM,                                   /* SLOOP 2.4: a chord's notes one after the other, ms each (> 0 down, < 0 up) */
    P_VLEAD,                                   /* SLOOP 2.4: chord mode, each chord voiced nearest the last (seq.c) */
    /* SLOOP 2.6, after Felucca 1.5 / 1.4 (Leo Kuroshita). Projects (format 6) keep them in a byte each: 0..127 */
    P_ITYPE, P_IA, P_IB, P_IC, P_IMIX,         /* the track's INSERT after DIST: TYPE, its three values, dry / wet (fx.c) */
    P_FTYPE,                                   /* ANALOG's filter TYPE: LP (as before) / BP / HP (eng_analog.c) */
    P_ESYNC,                                   /* ENV SYNC: ATK DEC REL as note values of the tempo (voice.c) */
    P_LSYNC, P_LTRIG, P_LPOL,                  /* LFO 2: SYNC to the tempo, TRIG (NOTE / FREE), POL (BI / UNI) (voice.c) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page (the ENGINE page is gone); a SET of G_ENGSEL switches the engine (editor) */
    G_CLRSEQ, G_INITSND,
    G_DRCH, G_DRLVL, G_DRREV,
    G_DUST, G_DUCK, G_FILT,     /* the master bus: lo-fi / vinyl, the kick ducking the parts, the DJ filter (fx.c) */
    G_ROLL,                     /* note repeat rate (ARP + key, seq.c) */
    G_NEWPRJ,                   /* TOOLS > NEW: a new project (GO) */
    G_DRDLY,                    /* SLOOP 2.5: the drums' delay send (GLO > DRUMS; a project keeps it in its own byte) */
    G_CH1, G_CH2, G_CH3,        /* SLOOP 2.6: GLO > MIDI, the MIDI channel of synth tracks 1..3 (0 OFF, 1..16; in and out) */
    G_CHOTH,                    /* .. and what the other channels play: 0 SEL (the selected track), 1 OFF. Settings of
                                 * the FM-1 (with G_DRCH): not in a project (project.c, panel.c midi_set) */
    G_TXSRC, G_TXLVL, G_TXTONE, G_TXMOVE,   /* SLOOP 2.6, GLO > TEXTURE: a looping bed under the mix (fx.c texture_*): */
    G_TXRATE, G_TXWHEN, G_TXDUCK,           /* its source, level, tone, movement; its rate, when it plays, the DUCK pump.
                                             * In a project (project.c: the spare bytes) */
    G_BEND,                     /* SLOOP 2.6, GLO > MIDI 2: the pitch bend's range, 1..12 semitones (a setting of the FM-1) */
    G_COUNT
};

/* ------------------------------------------------------------ voices --- */
typedef struct {                 /* a drum voice (drums.c): a sample or a synthesised sound playing */
    uint8_t note, vel, active;
    uint32_t ph[3];              /* the sample position (samples.c sample_next) */
    int32_t s[8];                /* its state (ADPCM, filters, envelopes) */
    uint32_t age;
} voice_t;

/* ------------------------------------------------------------ track --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
/* the dynamics of a note or a drum hit (2 bits each in the steps): played without velocity
 * (OCT- / OCT+ held on the drum track, the step page) or from a MIDI velocity */
enum { LV_NORM, LV_GHOST, LV_SOFT, LV_HARD };
typedef struct {                 /* a step of a SLOOP synth part (10 bytes; drums only: old projects' steps, read) */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE */
    uint8_t vel;
    uint8_t lvl;                 /* 2 bits per note (note k: bits 2k..2k+1): LV_* */
    uint8_t rat;                 /* 2 bits per note: the hits it plays in its step - 1 (ratchet x1..x4) */
} step_t;
#define DRUM_LANES 16            /* the drum track: 16 sounds, one per white key (drums.c LANE_*) */
typedef struct {                 /* a step of the drum track (10 bytes, the size of a step_t) */
    uint8_t on[2];               /* bit per lane */
    uint8_t lvl[4];              /* 2 bits per lane (lane k: byte k / 4, bits 2 (k % 4)..): LV_* */
    uint8_t rat[4];              /* 2 bits per lane: ratchet */
} dstep_t;
_Static_assert(sizeof(step_t) == 10 && sizeof(dstep_t) == 10, "a step is 10 bytes on every track");
/* SLOOP 2.4: per-step nudge (micro timing) and parameter locks (seq.c seq_tick / lock_step) */
#define MICRO_MIN (-32)          /* a step's nudge in 1/64 of its length: half a step early .. */
#define MICRO_MAX 31             /* .. just under half a step late (0 = on the grid) */
#define NLOCK 24                 /* parameter locks per track (several may share a step: other params) */
typedef struct {                 /* a lock: on step `step` the track's p[param] is `val` (4 bytes) */
    uint8_t step;                /* 0xFF = a free slot */
    uint8_t param;               /* P_* (a lockable one: p_lockable) */
    int16_t val;
} plock_t;
#define LOCK_FREE 0xFFu
/* a step's condition (SLOOP 2.4 fill, GLO + key 9 / 10): plays always, only during a fill, never during one */
enum { FC_NORM, FC_FILL, FC_NOFILL };     /* (3: as FC_NORM) */

typedef struct track {
    int16_t p[P_COUNT];
    dstep_t dstep[NSTEP];        /* the pattern: 16 lanes a step (drums.c LANE_*) */
    int8_t micro[NSTEP];         /* each step's nudge, MICRO_MIN..MICRO_MAX (1/64 of a step; - early, + late) */
    plock_t lock[NLOCK];         /* parameter locks, unsorted (step LOCK_FREE = a free slot) */
    uint8_t fill[NSTEP / 4];     /* each step's condition, 2 bits (seq.c step_fill): FC_* */
    /* the locks in force (seq.c lock_step): the params overridden now, what they were, what the lock set */
    uint8_t lk_n;
    uint8_t lk_param[NLOCK];
    int16_t lk_base[NLOCK];
    int16_t lk_set[NLOCK];
    uint32_t seq_abs;            /* the step of the transport grid last played (seq.c trk_grid), SEQ_NONE */
    uint16_t seq_idx;            /* its index in the pattern */
    uint8_t seq_den;             /* the DIV it was played on (a DIV change waits for the next step) */
    uint8_t seq_skip;            /* the step playing failed its fill condition: nothing of it sounds */
    uint8_t seq_active;          /* any step programmed */
    uint32_t rat_lanes;          /* ratchet hits played in this step, 2 bits a lane */
    uint32_t flam_abs;           /* the step whose grace notes played (fm1-drummer seq_flam), SEQ_NONE */
    /* mix runtime (fx.c) */
    int32_t att;                 /* mute fade: attenuation, Q15 (0 = heard; fx.c drums_mix) */
    uint8_t tail;                /* blocks to mix after the last hit (the INSERT's tail) */
    uint8_t ins_run;             /* 2.6: the INSERT runs (or fades out) (fx.c track_insert) */
    uint16_t ins_res;            /* 2.6: |the INSERT's last output| (fx.c: its tail runs until it is ~0) */
    uint8_t ins_xt;              /* 2.6: the blocks it ran past its tail for that (at most 255) */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: the drum track */
static song_t song;

/* The transport clock (seq.c runs it). One unit = one sample at 1 BPM: a beat is BEAT_U units at any
 * tempo, and every division of it (1/4 .. 1/64, the triplets) is a whole number of units. The steps
 * of every track, the arp, the rolls, the click, the SLICER and the song arranger (which counts the
 * same units) all follow it: no rounding, no drift between them, a tempo change moves them together.
 * clk_beat: beats since PLAY, clk_pos: units into that beat, at the start of the block being rendered
 * (events_block advances it by CTL x BPM after the block's events). */
#ifndef FS
#define FS 44100                 /* (as build/gen/felucca_tables.h: tools/gen_tables.py) */
#endif
#define BEAT_U ((uint32_t)FS * 60u)
static volatile uint32_t clk_beat, clk_pos;
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* N_DIV 0..5: beats = 1 / DEN */
#define NDIV_SHORT 6u            /* the divisions inside a beat (N_DIV, the arp's RATE) */
#define NDIV_STEP 9u             /* N_SDIV: + 1/2 note, a bar, two bars (whole beats: DIV_BEATS) */
#define NDIV_DLY 8u              /* N_DLY: + 1/8 and 1/16 dotted */
static const uint8_t DIV_BEATS[3] = {2, 4, 8};
/* a step's length in clock units (N_SDIV order; the arp's RATE uses 0..5) */
static uint32_t div_units(uint32_t div)
{
    return div < NDIV_SHORT ? BEAT_U / DIV_DEN[div] : BEAT_U * DIV_BEATS[(div - NDIV_SHORT) % 3u];
}
/* the delay's TIME in clock units (N_DLY order) */
static uint32_t dly_units(uint32_t d)
{
    return d < NDIV_SHORT ? BEAT_U / DIV_DEN[d] : d == NDIV_SHORT ? BEAT_U * 3u / 4u : d == NDIV_SHORT + 1u ? BEAT_U * 3u / 8u
                                                                                         : BEAT_U * 3u / 2u;   /* 1/4D (2.6) */
}
/* length of one step (N_SDIV order) in samples at the song tempo (rounded down) */
static uint32_t div_samples(uint32_t div) { return div_units(div) / (uint32_t)song.g[G_BPM]; }
/* length of the delay's TIME (N_DLY order) in samples at the song tempo (rounded down) */
static uint32_t dly_samples(uint32_t d) { return dly_units(d) / (uint32_t)song.g[G_BPM]; }
/* SLOOP 2.6, after Felucca 1.5.1: REV THROW / ECHO THROW (FX held + OCT- / OCT+, ui_input.c): while held, every
 * track's send to the reverb / the delay goes full, gliding in and out (~12 ms); ECHO THROW also takes the delay's FDBK
 * up to 70 % if it is lower. The song's values never change, nothing is saved or recorded. thr_req: bit 0 the reverb,
 * bit 1 the echo (the UI); thr_rev, thr_dly: the glide now, Q15 (fx.c mix_block, a block at a time) */
static volatile uint8_t thr_req;
static int32_t thr_rev, thr_dly;
static inline int32_t send_thr(int32_t s, int32_t g) { return g ? s + (((127 * 258 - s) * g) >> 15) : s; }   /* s 0..32766 */
#define TDRUM (&trk[TRK_DRUM])
static int is_drum(const track_t *t) { return t == TDRUM; }
/* silent: MUTE */
static int trk_silent(const track_t *t) { return t->p[P_MUTE] != 0; }
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* Two early failed boots -> USB recovery; recovery reset -> mask-ROM UBOOT. */
#include "bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));
