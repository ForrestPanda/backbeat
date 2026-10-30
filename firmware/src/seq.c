/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard and its layers, note repeat, sequencer and transport. Runs in the audio ISR, once per CTL-sample
 * block (events_block), and ends in trk_note_on / drum_on.
 *
 * Time: the transport clock of fx.c (clk_beat, clk_pos, in units of a sample at 1 BPM). The drum track reads its
 * step from it (trk_grid): the step is the clock divided by the track's DIV, swung, modulo its LEN. So the track,
 * the click, the rolls and the song arranger never drift apart, and a tempo or DIV change plays at most one step
 * a block.
 *
 * Drums only (docs/drums-only.md): the drum track's steps are 16 lanes, one per white key; each lane of a step
 * has a level (ghost .. hard) and a ratchet (x1..x4 hits in its step). The keys play the drum track; MIDI notes
 * on the drums' channel (SETUP > MIDI > DRUMS CHANNEL, default 10), and on the others unless SETUP > MIDI > OTHER is OFF, play it too.
 *
 * Layers: a function button held turns the keys into something else (TE style: hold + touch):
 *   FX   the master filter, DUST, the throws  EDIT  erase that sound (while held, as it plays)
 *   ARP  note repeat (roll) at G_ROLL     SEQ   steps 1..16 (the UI: ui_layers.c)
 * (SAVE's layer, SLOOP's sections A..D, went with JAM 2026-10-27: SEL opens SONGS, SAVE held on the drum grid writes
 * the section into its song. GLO's layer, mute / fill / tap tempo, is gone: GLO tapped opens SETUP, ui_menu.c.)
 * OCT- / OCT+ held play ghost / hard hits. */
#define SEQ_NONE 0xFFFFFFFFu

static uint32_t kb_prev;
/* per key: what its press started, so its release ends the same (whatever the layer is now) */
enum { KS_NONE, KS_DRUM, KS_ROLL, KS_ERASE, KS_FX, KS_UI };
static uint8_t kb_kind[27], kb_nt[27];         /* kb_nt: the lane, or the layer of a KS_UI key */
static uint8_t pen_lane;                       /* the last drum lane played: the SEQ layer's lane */

static volatile uint8_t pc_req[NTRK];          /* (2.6) a MIDI Program Change for the track: number + 1 (ui.c pc_poll) */
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI); 3 start from a MIDI START */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static uint32_t trk_midi_ch(uint32_t i)    /* MIDI channel 0..15 of track i (keys -> MIDI out): the drums' (SETUP > MIDI) */
{                                          /* (2.6: OFF is OFF, in and out: 0xFF, nothing sent) */
    int32_t c = song.g[G_DRCH];
    (void)i;
    return c >= 1 && c <= 16 ? (uint32_t)c - 1u : 0xFFu;
}
/* a note on (vel > 0) / off to MIDI OUT on channel ch (trk_midi_ch: 0xFF, the track's MIDI OFF: nothing) */
static void midi_out_note(uint32_t ch, uint32_t note, uint32_t vel)
{
    if (ch > 15u)
        return;
    midi_out_event(vel ? 0x09u | (0x90u | ch) << 8 | (note & 127u) << 16 | (vel & 127u) << 24
                       : 0x08u | (0x80u | ch) << 8 | (note & 127u) << 16);
}

/* MIDI OUT of what the sequencer, the arp and the rolls play (SETUP > MIDI > OUT = SEQ; the keys always
 * go out: key_down / key_up). Notes from a computer or the jack are never echoed (no MIDI loop). A set per
 * track of the notes sent on, so a note is ended once, and STOP or MIDI = KEYS end them all */
static uint32_t mo_set[NTRK][4];
static uint8_t mo_ch[NTRK];                /* (2.6) the channel the notes of mo_set went out on: their note-offs too,
                                            * whatever SETUP > MIDI says now (no note left hanging on the other gear) */
static void seq_out_off(const track_t *t, uint32_t note)
{
    uint32_t i = trk_index(t) % NTRK;
    if (note > 127u || !(mo_set[i][note >> 5] & (1u << (note & 31u))))
        return;
    mo_set[i][note >> 5] &= ~(1u << (note & 31u));
    midi_out_note(mo_ch[i], note, 0);
}
static void seq_out_track_off(const track_t *t);
static uint8_t mo_any;                     /* something was sent on since the last check (events_block) */
static void seq_out_on(const track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i = trk_index(t) % NTRK;
    if (!song.g[G_MIDI] || note > 127u)
        return;
    seq_out_off(t, note);                      /* played again while on: off first */
    if (trk_midi_ch(i) != mo_ch[i]) {          /* (its channel changed: what is on ends where it started) */
        seq_out_track_off(t);
        mo_ch[i] = (uint8_t)trk_midi_ch(i);
    }
    if (mo_ch[i] > 15u)
        return;                                /* (MIDI OFF: nothing goes out, nothing to end) */
    mo_set[i][note >> 5] |= 1u << (note & 31u);
    mo_any = 1;
    midi_out_note(mo_ch[i], note, (vel ? vel & 127u : 1u));
}
static void seq_out_track_off(const track_t *t)      /* every note of the track still on */
{
    uint32_t i = trk_index(t) % NTRK, w, b;
    for (w = 0; w < 4u; w++)
        for (b = 0; mo_set[i][w]; b++)
            if (mo_set[i][w] & (1u << b)) {
                mo_set[i][w] &= ~(1u << b);
                midi_out_note(mo_ch[i], (w * 32u + b), 0);
            }
}
static void seq_out_all_off(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        seq_out_track_off(&trk[i]);
}

/* ---------------------------------------------------------- layers --- */
enum { LY_PLAY, LY_FX, LY_ERASE, LY_ROLL, LY_STEP, LY_SCALE, LY_MIX, LY_COUNT };   /* (LY_SONG, SAVE held: JAM's,
                                                                                    * gone 2026-10-27; SAVE is a tap
                                                                                    * or a hold, ui_input.c) */
static uint32_t ly_bit[LY_COUNT];        /* the button (fm1_in.buttons bit) of each layer: the UI sets them */
static uint32_t dyn_bit[2];              /* OCT- / OCT+: ghost / hard on the drum track */
/* a layer locked open (its button held + HOME tapped: ui_input.c), LY_PLAY = none: the keys and knobs
 * stay in it with the button let go, as if it were held */
static volatile uint8_t ly_lock = LY_PLAY;
static uint32_t layer_buttons(void) { return fm1_in.buttons | (ly_lock != LY_PLAY ? ly_bit[ly_lock % LY_COUNT] : 0u); }
/* the layer the keys are in: the held function button (FX, EDIT, ARP, SEQ, SEL, GLO in that order),
 * else the locked one */
static uint32_t layer_now(void)
{
    uint32_t b = layer_buttons(), l;
    for (l = LY_FX; l < LY_COUNT; l++)
        if (b & ly_bit[l])
            return l;
    return LY_PLAY;
}
/* the DRUMS grid page shown (the UI sets it every frame): with no layer, the keys are its steps (KB_GRID
 * events of lk_q, ui_studio.c grid_key) and play nothing */
#define KB_GRID LY_COUNT
static volatile uint8_t kb_grid;
/* the keys of the layers the UI handles (steps, key, mix): key k down / up, in order */
#define LKQ 16u
#define KB_OCTUP 32u                                   /* a KB_GRID key's entry: OCT+ was held (k uses bits 0..4) */
#define KB_OCTDN 64u                                   /* ..OCT- was held (the grid page: a ghost hit; OCT+ a hard one) */
static volatile uint16_t lk_q[LKQ];
static volatile uint32_t lk_w, lk_r;
static void lk_push(uint32_t layer, uint32_t k, uint32_t down)
{
    if (lk_w - lk_r < LKQ) {
        lk_q[lk_w % LKQ] = (uint16_t)(layer << 8 | down << 7 | k);
        RING_PUBLISH();
        lk_w++;
    }
}

/* ------------------------------------------------------------- grid --- */
/* units an odd step starts late: the track's + the global SWING (MPC: 0 = 50 %, 100 = 75 %) */
static uint32_t swing_units(int32_t pct, uint32_t u)
{
    return (uint32_t)clamp(pct, 0, 100) * u / 200u;
}
/* a position on a grid of steps u units long, odd steps sw late: the step, units into it, its length */
static uint32_t grid_swing(uint32_t abs, uint32_t frac, uint32_t u, uint32_t sw, uint32_t *into, uint32_t *len)
{
    if (abs & 1u) {                                      /* an odd step: sw late */
        if (frac < sw) {
            abs--;
            frac += u;
            *len = u + sw;
        } else {
            frac -= sw;
            *len = u - sw;
        }
    } else {
        *len = u + sw;
    }
    *into = frac;
    return abs;
}
/* the clock on a grid of den steps a beat (the rolls: ROLL_DEN) */
static uint32_t grid_den(uint32_t den, uint32_t sw, uint32_t *into, uint32_t *len)
{
    uint32_t u = BEAT_U / den;
    return grid_swing(clk_beat * den + clk_pos / u, clk_pos % u, u, sw, into, len);
}
/* the clock on the grid of a division (N_SDIV: steps inside a beat, or of 2, 4, 8 whole beats) */
static uint32_t grid_at(uint32_t div, uint32_t sw, uint32_t *into, uint32_t *len)
{
    uint32_t m;
    if (div < NDIV_SHORT)
        return grid_den(DIV_DEN[div], sw, into, len);
    m = DIV_BEATS[(div - NDIV_SHORT) % 3u];              /* whole beats: the step from the beat count */
    return grid_swing(clk_beat / m, (clk_beat % m) * BEAT_U + clk_pos, BEAT_U * m, sw, into, len);
}
/* swing is for the straight grids inside a beat: off on the triplet grids (on 8T the odd steps would
 * change from one beat to the next, as on most machines) and on steps of whole beats */
static uint32_t swings(uint32_t div) { return div < 4u; }
/* a song's swing on the 8ths (sng.c SNG_H_SWING8, since 2026-10-19: a shuffle, straight 8ths played long-short) under a
 * finer grid: the 8th-note grid swings as above and the 16ths or 32nds inside each 8th share it evenly, so a pattern
 * with its hats on the 8ths shuffles, where swing on the 16ths moves only the steps between them (nothing, in such a
 * pattern). seq_swing8: set by sng_globals from the song's header, cleared with the song */
static uint8_t seq_swing8;
static __attribute__((noinline)) uint32_t grid_den8(uint32_t den, uint32_t sw8, uint32_t *into, uint32_t *len)   /* (once a block: its
                                                      * divides out of mix_block's loops, tests/target_budget.py) */
{
    uint32_t k = den / 2u, u8 = BEAT_U / 2u, into8, len8, part, sub;
    uint32_t abs8 = grid_swing(clk_beat * 2u + clk_pos / u8, clk_pos % u8, u8, sw8, &into8, &len8);
    part = len8 / k;
    sub = into8 / part;
    if (sub >= k)
        sub = k - 1u;
    *into = into8 - sub * part;
    *len = sub == k - 1u ? len8 - sub * part : part;
    return abs8 * k + sub;
}
static uint32_t trk_div(const track_t *t) { return (uint32_t)t->p[P_SDIV] % NDIV_STEP; }
static uint32_t trk_grid(const track_t *t, uint32_t *into, uint32_t *len)
{
    uint32_t div = trk_div(t);
    int32_t pct = t->p[P_SSWING] + song.g[G_SWING];
    if (seq_swing8 && (div == 2u || div == 3u) && pct > 0)   /* 1/16, 1/32 under a shuffle: the 8ths swing, shared */
        return grid_den8(DIV_DEN[div], swing_units(pct, BEAT_U / 2u), into, len);
    return grid_at(div, swings(div) ? swing_units(pct, div_units(div)) : 0u, into, len);
}
static uint32_t trk_len(const track_t *t) { return t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u; }

/* ------------------------------------------------------------- undo --- */
/* One step back (and forward again) for the pattern of one track: what it was before the last
 * recording pass, erase, step edit, tool or clear (a session: one mark). EDIT + OCT- / OCT+. */
static struct {
    uint8_t valid, undone, trk;
    int16_t len;
    int16_t div;                 /* its DIV (2026-10-29: a DIV change re-grids the pattern, pattern_rediv; the undo
                                  * takes the grid back first) */
    uint32_t sess;
    dstep_t st[NSTEP];
} undo;
static uint32_t undo_sess = 1;           /* UI sessions (seq.c: recording passes use the track's pass) */
static volatile uint8_t div_req;         /* a DIV from MIDI (a learned CC: midi_cc_set, the ISR) + 1, for the UI to apply
                                          * in the main loop (ui_input.c: pattern_div_set, the pattern re-gridded); 0 none */
static void undo_mark(const track_t *t, uint32_t sess)
{
    uint32_t i = trk_index(t);
    if (undo.valid && !undo.undone && undo.trk == i && undo.sess == sess)
        return;                                          /* (this session is marked already) */
    memcpy(undo.st, t->dstep, sizeof undo.st);
    undo.len = t->p[P_SLEN];
    undo.div = t->p[P_SDIV];
    undo.trk = (uint8_t)i;
    undo.sess = sess;
    undo.valid = 1;
    undo.undone = 0;
}
static uint32_t undo_erase_sess;

/* ------------------------------------------------------- count-in --- */
/* the pedal's count-in (fm1-drummer, pedal.c): a section asked for while stopped first clicks one or two bars
 * of its meter, then the loop starts with it (events_block). Live recording (REC, free takes) is gone: drums
 * only, docs/drums-only.md */
static volatile uint8_t ci_on;                /* the count-in runs */
static volatile uint8_t ci_pedal;             /* its clicks to go (the pedal's) */
static volatile uint8_t ci_beat;              /* its beats clicked so far - 1 */
static uint8_t ci_meter;                      /* it clicks this meter (clk_meter's form) */
static uint32_t ci_u;                         /* clock units since it started */
static void seq_start(void);

/* -------------------------------------------------- parameter locks --- */
/* A lock (track_t.lock, SLOOP 2.4): on its step the track's p[param] takes its value; the parameter goes back
 * to what it was at the next step without a lock on it (Elektron style: notes still ringing follow, the
 * engines read p[] every block). The locks in force are listed in lk_* (param, the base to go back to, the
 * value set). A knob turned while a lock is on wins: the value found is kept as the new base. Only the
 * sound parameters lock (p_lockable): the sequencer's, the arp's, the key's and the voice mode's do not. */
static int p_lockable(uint32_t id)
{
    return id <= P_LD_AMP || id == P_SGATE || (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
           id == P_DETUNE || (id >= P_SLCR && id <= P_SLDEPTH) || (id >= P_E0 && id <= P_E7) || id == P_TFLT ||
           (id >= P_IA && id <= P_IMIX);   /* 2.6: the INSERT's values and MIX */
}
/* the range of p[id] on track t (the drum track's P_E0: the kit) */
static const param_desc_t *lock_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7)
        return e_desc(t, id);
    return &TP[id % P_COUNT];
}
static void lock_write(track_t *t, uint32_t id, int32_t v)
{
    const param_desc_t *d = lock_desc(t, id);
    t->p[id % P_COUNT] = (int16_t)clamp(v, d->min, d->max);
}
static void locks_restore(track_t *t)        /* every lock in force let go (STOP, a load, a cleared pattern) */
{
    uint32_t i;
    for (i = 0; i < t->lk_n && i < NLOCK; i++)
        if (t->p[t->lk_param[i] % P_COUNT] == t->lk_set[i])
            lock_write(t, t->lk_param[i], t->lk_base[i]);
    t->lk_n = 0;
}
static void locks_clear(track_t *t)          /* no lock, no nudge (an empty pattern) */
{
    uint32_t i;
    locks_restore(t);
    memset(t->micro, 0, sizeof t->micro);
    for (i = 0; i < NLOCK; i++) {
        t->lock[i].step = LOCK_FREE;
        t->lock[i].param = 0;
        t->lock[i].val = 0;
    }
}
/* the sequencer enters step idx: its locks take hold, the last step's that it does not share let go */
static void lock_step(track_t *t, uint32_t idx)
{
    uint32_t i, k, n = 0;
    for (i = 0; i < t->lk_n && i < NLOCK; i++) {      /* in force, no lock here: back to the base (or the knob) */
        uint32_t p = t->lk_param[i] % P_COUNT, has = 0;
        for (k = 0; k < NLOCK; k++)
            if (t->lock[k].step == idx && t->lock[k].param == p)
                has = 1;
        if (has) {
            t->lk_param[n] = (uint8_t)p;
            t->lk_base[n] = t->lk_base[i];
            t->lk_set[n] = t->lk_set[i];
            n++;
        } else if (t->p[p] == t->lk_set[i]) {
            lock_write(t, p, t->lk_base[i]);
        }
    }
    t->lk_n = (uint8_t)n;
    for (k = 0; k < NLOCK; k++) {                     /* this step's locks */
        const plock_t *l = &t->lock[k];
        uint32_t p = l->param;
        if (l->step != idx || p >= P_COUNT || !p_lockable(p))
            continue;
        for (i = 0; i < t->lk_n && t->lk_param[i] != p; i++)
            ;
        if (i == t->lk_n) {
            if (i >= NLOCK)
                continue;
            t->lk_param[i] = (uint8_t)p;
            t->lk_base[i] = t->p[p];
            t->lk_n++;
        } else if (t->p[p] != t->lk_set[i]) {
            t->lk_base[i] = t->p[p];                  /* turned meanwhile: that is the new base */
        }
        lock_write(t, p, l->val);
        t->lk_set[i] = t->p[p];
    }
}
/* a lock of (step, param): its slot, or a free one for it (-1: none left) */
static int lock_find(const track_t *t, uint32_t step, uint32_t param, int make)
{
    uint32_t k;
    int fr = -1;
    for (k = 0; k < NLOCK; k++) {
        if (t->lock[k].step == step && t->lock[k].param == param)
            return (int)k;
        if (fr < 0 && t->lock[k].step == LOCK_FREE)
            fr = (int)k;
    }
    return make ? fr : -1;
}
/* set (or make) the lock of (step, param) at v, clamped; 0 = no slot left or not lockable. Callers hold the IRQ off */
static int lock_set(track_t *t, uint32_t step, uint32_t param, int32_t v)
{
    int k;
    const param_desc_t *d;
    if (step >= NSTEP || param >= P_COUNT || !p_lockable(param) || (k = lock_find(t, step, param, 1)) < 0)
        return 0;
    d = lock_desc(t, param);
    t->lock[k].step = (uint8_t)step;
    t->lock[k].param = (uint8_t)param;
    t->lock[k].val = (int16_t)clamp(v, d->min, d->max);
    return 1;
}
static void lock_del(track_t *t, uint32_t step, uint32_t param)   /* param P_COUNT: every lock of the step */
{
    uint32_t k;
    for (k = 0; k < NLOCK; k++)
        if (t->lock[k].step == step && (param >= P_COUNT || t->lock[k].param == param))
            t->lock[k].step = LOCK_FREE;
}
static int step_locked(const track_t *t, uint32_t step)   /* the step carries a lock or a nudge (the UI's mark) */
{
    uint32_t k;
    if (step < NSTEP && t->micro[step])
        return 1;
    for (k = 0; k < NLOCK; k++)
        if (t->lock[k].step == step)
            return 1;
    return 0;
}

/* ---- step conditions (SLOOP 2.4 fill, core.h FC_*): 2 bits a step in t->fill; a fill (fm1-drummer: the pedal's or
 * LIVE's FILL, pedal.c; SLOOP's GLO layer held it or armed it for the next bar, gone) makes FC_FILL steps play only
 * then, FC_NOFILL steps silent then */
static uint32_t step_fill(const track_t *t, uint32_t idx)
{
    idx %= NSTEP;
    return (uint32_t)(t->fill[idx / 4u] >> (2u * (idx % 4u))) & 3u;
}
static void step_fill_set(track_t *t, uint32_t idx, uint32_t v)
{
    uint32_t sh;
    idx %= NSTEP;
    sh = 2u * (idx % 4u);
    t->fill[idx / 4u] = (uint8_t)((t->fill[idx / 4u] & ~(3u << sh)) | (v & 3u) << sh);
}
static volatile uint8_t fill_held;           /* a fill while held (SLOOP's GLO + key 9; nothing sets it now) */
static volatile uint8_t fill_arm;            /* the next bar is a fill (SLOOP's GLO + key 10; nothing sets it now) */
static uint8_t fill_bar_on;                  /* that bar, while it plays (the ISR) */
static uint8_t fill_now;                     /* this block is a fill: fill_held || fill_bar_on, read once a block */
static uint32_t fill_last_bar = 0xFFFFFFFFu; /* clk_beat / 4 of the last bar seen (events_block) */
static uint32_t step_plays(const track_t *t, uint32_t idx)   /* its condition holds now (3: as normal) */
{
    uint32_t c = step_fill(t, idx);
    return c == FC_FILL ? fill_now : c == FC_NOFILL ? !fill_now : 1u;
}
#if FELUCCA_DRUMMER
/* fm1-drummer: a song's section marks the drum track's hits one by one (a lane at a step): "only during a fill"
   or "not during a fill" (sng.c: section flag 2, two 16-bit masks a step), on top of SLOOP's condition of the
   whole step. The masks are the song's: cleared with the pattern (steps_clear, a project) */
static uint16_t drum_fonly[NSTEP], drum_fno[NSTEP];
/* a flam (2026-10-17): a hit flagged so plays a grace note first, the same lane as a ghost, FLAM_SMP samples before it
   (about 25 ms, a drummer's, whatever the tempo; a quarter of the step at most), the hit itself on the grid. A bit a
   lane a step, the pattern's and the fill bar's (sng.c: the hit byte's bit 6). The grace is predicted: seq_flam plays
   it when the step due next is that close (t->flam_abs: done for it). Moved, rotated and cleared with the marks */
static uint16_t drum_flam[NSTEP], fb_flam[NSTEP];
#define FLAM_SMP 1100u
/* a section's fill bar of its own (sng.c: section flag SNG_F_FILLBAR, 2026-10-15): fb_len steps, played by a fill
   instead of the bar the fill is in (as an arranger's fill-in: from the press to the bar line, the phrase cut from
   the front, so its step i sounds at the bar's step i); a phrase of more than one bar (fb_bars, fb_bar steps each)
   ends on a bar line that many bars on. With none the fill is the marks' and the conditions', and it runs to the
   section's end (fill_bars_from; 2026-10-17): the marks sit where the writer put them, so a fill asked for in a
   4-bar verse's second bar is heard in its fourth, and the pattern goes on from its top with no restart (a section
   with no fill at all: nothing changes, nothing restarts). Cleared with the marks (the pattern's) */
static dstep_t fb_dstep[NSTEP];
static uint8_t fb_len;
static uint8_t fb_bar, fb_bars;              /* the phrase's steps a bar and its bars (sng_apply; 0 with no fill bar) */
static volatile uint8_t pedal_fill_bars;     /* bars of the pedal's fill left, this one counted (seq.c live_block) */
/* a section's entry and break (sng.c: section flags SNG_F_ENTRY, SNG_F_BREAK, 2026-10-18): bars written to play
   before the section, its pattern from its top after them (live_block restarts it at their end; a request waiting
   for the section's end lands there: eb_bars are the section's bars meanwhile). Shine On's way from P3-SYNTH into the
   ride groove is 4 bars between two 3-bar phrases (the morning's first form laid them over the pattern, its count
   running underneath: the groove then came in a bar off). The entry when the section comes in from another one or
   from stopped, the break on the pedal's CC 28 (a break of the section playing too); one at a time, so one buffer:
   eb_len steps from the step the section landed on (the drum track's seq_abs 0), decoded at the landing (sng_eb).
   Played as a fill bar is, as written (no condition, no mark); a fill cuts it (the count goes on from the landing),
   a stop or a new landing ends it */
static dstep_t eb_dstep[NSTEP];
static uint16_t eb_flam[NSTEP];
static volatile uint8_t eb_len, eb_bars;     /* its steps, and the bars they take (0: none playing) */
static uint32_t drum_fill_mask(uint32_t idx)       /* the lanes that do not play at this step, now (NSTEP: a fill bar
                                                    * step, no mark of the pattern's) */
{
    if (idx >= NSTEP)
        return 0;
    return fill_now ? drum_fno[idx] : drum_fonly[idx];
}
static void drum_fill_masks_clear(void)
{
    memset(drum_fonly, 0, sizeof drum_fonly);
    memset(drum_fno, 0, sizeof drum_fno);
    memset(drum_flam, 0, sizeof drum_flam);
    memset(fb_flam, 0, sizeof fb_flam);
    eb_len = 0;
    eb_bars = 0;
    fb_len = 0;
    fb_bar = 0;
    fb_bars = 0;
}
/* the step the drum track plays at grid step abs (from the section's start; idx: its place in the pattern): the
 * entry's or break's (*fb set) over its first eb_len steps; the fill bar's (*fb set) while a fill plays, at the bar's
 * own step (the phrase ends with the fill: with bars of it left, the earlier ones); else the pattern's */
static const dstep_t *drum_src(const track_t *t, uint32_t abs, uint32_t idx, uint32_t *fb)
{
    if (eb_len && abs < eb_len && is_drum(t)) {
        *fb = 1;
        return &eb_dstep[abs];
    }
    if (fb_len && fill_now && is_drum(t)) {
        uint32_t left = pedal_fill_bars ? pedal_fill_bars : 1u;
        int32_t fi = (int32_t)fb_len - (int32_t)(left * fb_bar) + (int32_t)(idx % fb_bar);
        if (fi >= 0 && fi < (int32_t)fb_len) {
            *fb = 1;
            return &fb_dstep[fi];
        }
    }
    *fb = 0;
    return &t->dstep[idx % NSTEP];
}
static void drum_marks_move(uint32_t to, uint32_t from)   /* a step's marks go with its hits (EDIT layer x2) */
{
    drum_fonly[to % NSTEP] = drum_fonly[from % NSTEP];
    drum_fno[to % NSTEP] = drum_fno[from % NSTEP];
    drum_flam[to % NSTEP] = drum_flam[from % NSTEP];
}
static void drum_marks_clear(uint32_t i)                  /* a step emptied (pattern_rediv): no marks */
{
    drum_fonly[i % NSTEP] = 0;
    drum_fno[i % NSTEP] = 0;
    drum_flam[i % NSTEP] = 0;
}
static void drum_marks_rotate(uint32_t len, int32_t d)   /* the first len steps' marks one later (d > 0) / earlier */
{
    uint32_t i, last = len - 1u;
    uint16_t a, b, c;
    if (!len || len > NSTEP)
        return;
    if (d > 0) {
        a = drum_fonly[last], b = drum_fno[last], c = drum_flam[last];
        for (i = last; i > 0; i--)
            drum_marks_move(i, i - 1u);
        drum_fonly[0] = a, drum_fno[0] = b, drum_flam[0] = c;
    } else {
        a = drum_fonly[0], b = drum_fno[0], c = drum_flam[0];
        for (i = 0; i < last; i++)
            drum_marks_move(i, i + 1u);
        drum_fonly[last] = a, drum_fno[last] = b, drum_flam[last] = c;
    }
}
#else
static uint32_t drum_fill_mask(uint32_t idx) { (void)idx; return 0; }
static void drum_fill_masks_clear(void) {}
static const dstep_t *drum_src(const track_t *t, uint32_t abs, uint32_t idx, uint32_t *fb) { (void)abs; *fb = 0; return &t->dstep[idx % NSTEP]; }
static void drum_marks_move(uint32_t to, uint32_t from) { (void)to; (void)from; }
static void drum_marks_clear(uint32_t i) { (void)i; }
static void drum_marks_rotate(uint32_t len, int32_t d) { (void)len; (void)d; }
#endif
/* a hit of the drum pattern cleared: its mark goes with it (a hit set there later plays as any other) */
static void drum_hit_clr(track_t *t, uint32_t idx, uint32_t l)
{
    dstep_clr(&t->dstep[idx % NSTEP], l);
#if FELUCCA_DRUMMER
    if (is_drum(t)) {
        drum_fonly[idx % NSTEP] &= (uint16_t)~(1u << l);
        drum_fno[idx % NSTEP] &= (uint16_t)~(1u << l);
        drum_flam[idx % NSTEP] &= (uint16_t)~(1u << l);
    }
#endif
}

static void steps_clear(track_t *t)           /* an empty pattern (no lane); no lock, no nudge, no condition */
{
    memset(t->dstep, 0, sizeof t->dstep);
    locks_clear(t);
    memset(t->fill, 0, sizeof t->fill);
    if (is_drum(t))
        drum_fill_masks_clear();                   /* (fm1-drummer: the song's marks go with the pattern) */
}

/* ------------------------------------------------------------ erase --- */
/* EDIT + key: that sound goes from the drum pattern. Playing: from the step playing and every step the sequencer
 * enters while the key is held (MPC style); stopped: from every step. */
static uint16_t er_lanes;                     /* the lanes held */
/* the drum track's live mutes (SLOOP 2.6, after Felucca 1.4.1): EDIT + a black key turns the white keys from erasing
 * into muting (er_mute; back to erasing with a black key again, and when EDIT is let go and not locked), and a white
 * key then mutes / unmutes its sound: the sequencer skips it (its ratchets too), the keys still play it. Performance
 * state: never saved, not undone; a project load or NEW clears it */
static volatile uint16_t drum_mute;
static volatile uint8_t er_mute;
static volatile uint8_t er_flash;             /* something was erased (the UI flashes) */

static void erase_step(track_t *t, uint32_t idx)
{
    dstep_t *s = &t->dstep[idx];
    uint32_t l, m = dstep_mask(s) & er_lanes;
    for (l = 0; m; l++, m >>= 1)
        if (m & 1u) {
            drum_hit_clr(t, idx, l);
            er_flash = 1;
        }
}
static void erase_now(track_t *t)             /* a key just went down */
{
    uint32_t i, len = trk_len(t);
    undo_mark(t, undo_erase_sess);
    if (song.playing && t->seq_abs != SEQ_NONE) {
        erase_step(t, t->seq_idx % len);
    } else {
        for (i = 0; i < len; i++)
            erase_step(t, i);
    }
}
static int erasing(const track_t *t) { return is_drum(t) && er_lanes; }

/* ------------------------------------------------------- note input --- */
/* a drum hit from a key, MIDI or a roll: lane, level */
static void drum_input(uint32_t lane, uint32_t lvl)
{
    lane &= 15u;
    pen_lane = (uint8_t)lane;
    trk_note_on(TDRUM, LANE_NOTE[lane], lvl_vel(lvl, 100));
}

/* -------------------------------------------------------------- roll --- */
/* ARP + key: note repeat. The key plays its lane at G_ROLL (1/8, 1/16, 1/32, 32T, 1/64) on the transport's grid
 * (stopped: from the press) until it or ARP is up. */
static const uint8_t ROLL_DEN[5] = {2, 4, 8, 12, 16};
#define NROLL 4u
static struct {
    uint8_t on, key, lane, lvl;
    uint32_t last;                      /* playing: the roll step last played; stopped: units since */
} roll[NROLL];
static uint32_t roll_den(void) { return ROLL_DEN[(uint32_t)clamp(song.g[G_ROLL], 0, 4)]; }
/* the lanes a roll plays live: the pattern does not play them meanwhile (no flam with what the pattern holds) */
static uint32_t roll_lanes(void)
{
    uint32_t r, m = 0;
    for (r = 0; r < NROLL; r++)
        if (roll[r].on)
            m |= 1u << (roll[r].lane & 15u);
    return m;
}

static void roll_hit(uint32_t r)
{
    drum_input(roll[r].lane, roll[r].lvl);
    seq_out_on(TDRUM, LANE_NOTE[roll[r].lane & 15u], lvl_vel(roll[r].lvl, 100));
}

static void roll_start(uint32_t k, uint32_t lane, uint32_t lvl)
{
    uint32_t r, den = roll_den(), into, slen, abs;
    for (r = 0; r < NROLL && roll[r].on; r++)
        ;
    if (r == NROLL)
        return;
    roll[r].on = 1;
    roll[r].key = (uint8_t)k;
    roll[r].lane = (uint8_t)lane;
    roll[r].lvl = (uint8_t)lvl;
    if (song.playing) {
        abs = grid_den(den, 0, &into, &slen);
        roll[r].last = abs;
        if (into * 4u >= slen * 3u)
            return;                                  /* the grid is just ahead: it starts there */
    } else {
        roll[r].last = 0;
    }
    roll_hit(r);
}

static void roll_end(uint32_t r)
{
    if (roll[r].on)
        seq_out_off(TDRUM, LANE_NOTE[roll[r].lane & 15u]);
    roll[r].on = 0;
}

static void roll_block(uint32_t adv)
{
    uint32_t r, den = roll_den(), u = BEAT_U / den, into, slen;
    for (r = 0; r < NROLL; r++) {
        if (!roll[r].on)
            continue;
        if (song.playing) {
            uint32_t abs = grid_den(den, 0, &into, &slen);
            if (abs != roll[r].last) {
                roll[r].last = abs;
                roll_hit(r);
            }
        } else {
            roll[r].last += adv;
            if (roll[r].last >= u) {
                roll[r].last = roll[r].last - u < u ? roll[r].last - u : 0;
                roll_hit(r);
            }
        }
    }
}

/* ---------------------------------------------------------- keyboard --- */
/* the level of a key on the drum track: OCT- held ghost, OCT+ held hard */
static uint32_t key_lvl(void)
{
    uint32_t b = fm1_in.buttons;
    return (b & dyn_bit[0]) ? LV_GHOST : (b & dyn_bit[1]) ? LV_HARD : LV_NORM;
}

static void key_down(uint32_t k)
{
    uint32_t layer = layer_now(), lane = lane_of_key(k), lvl;
    track_t *t = TDRUM;
    kb_kind[k] = KS_NONE;
    switch (layer) {
    case LY_FX:                                       /* FX held: the keys play nothing (its knobs and the throws:
                                                       * the punch-in effects are gone, docs/drums-only.md) */
        kb_kind[k] = KS_FX;
        return;
    case LY_STEP:
    case LY_SCALE:                                    /* the UI's: steps */
        kb_kind[k] = KS_UI;
        kb_nt[k] = (uint8_t)layer;                    /* (its key-up goes to the same layer) */
        lk_push(layer, k, 1);
        return;
    case LY_ERASE:
        if (key_white(k) < 0) {                       /* (2.6) a black key: erase <-> mute */
            er_mute = (uint8_t)!er_mute;
            return;
        }
        if (er_mute) {                                /* MUTE: the white key's sound off / on in the sequencer */
            drum_mute ^= (uint16_t)(1u << lane);
            return;
        }
        kb_kind[k] = KS_ERASE;
        if (!erasing(t))
            undo_erase_sess = (undo_sess += 4u) | 2u;  /* a new erase: one undo */
        kb_nt[k] = (uint8_t)lane;
        er_lanes |= (uint16_t)(1u << lane);
        erase_now(t);
        return;
    default:
        break;
    }
    if (layer == LY_PLAY && kb_grid) {                /* the DRUMS grid page: a step key (2: the song screen's) */
        kb_kind[k] = KS_UI;
        kb_nt[k] = (uint8_t)KB_GRID;
        lk_push(KB_GRID, k | ((fm1_in.buttons & dyn_bit[1]) ? KB_OCTUP : 0u) | ((fm1_in.buttons & dyn_bit[0]) ? KB_OCTDN : 0u), 1);
                                                      /* (OCT+ held as the key went down: the song screen's "on the next
                                                       * bar", the grid's hard hit; OCT-: the grid's ghost) */
        return;
    }
    lvl = key_lvl();                                  /* the key's lane, at the level OCT- / OCT+ give */
    kb_nt[k] = (uint8_t)lane;
    if (layer == LY_ROLL) {
        kb_kind[k] = KS_ROLL;
        roll_start(k, lane, lvl);
        return;
    }
    kb_kind[k] = KS_DRUM;
    drum_input(lane, lvl);
    midi_out_note(trk_midi_ch(TRK_DRUM), (uint32_t)LANE_NOTE[lane], lvl_vel(lvl, 100));
}

static void key_up(uint32_t k)
{
    uint32_t i, kind = kb_kind[k];
    kb_kind[k] = KS_NONE;
    switch (kind) {
    case KS_UI:
        lk_push(kb_nt[k], k, 0);
        return;
    case KS_ERASE:
        er_lanes &= (uint16_t)~(1u << kb_nt[k]);
        return;
    case KS_ROLL:
        for (i = 0; i < NROLL; i++)
            if (roll[i].on && roll[i].key == k)
                roll_end(i);
        return;
    case KS_DRUM:
        midi_out_note(trk_midi_ch(TRK_DRUM), (uint32_t)LANE_NOTE[kb_nt[k] & 15u], 0);
        return;
    default:
        return;
    }
}

/* the UI asks to hear drum sounds (a sound or a step picked with a knob, a step set from a key):
 * aud_lanes the lanes, each at its level aud_lvl (2 bits a lane), played here, in the audio context */
static volatile uint32_t aud_lanes, aud_lvl;
static void audition_req(uint32_t lanes, uint32_t lvls)
{
    fm1_irq_off();
    aud_lvl = lvls;
    aud_lanes = lanes & 0xFFFFu;
    fm1_irq_on();
}
static void audition_lane(uint32_t lane) { audition_req(1u << (lane & 15u), 0u); }   /* (LV_NORM = 0) */
static void audition_step(const dstep_t *s)            /* every sound of a drum step, at its level */
{
    uint32_t m = dstep_mask(s), lv = 0, l;
    for (l = 0; l < 16u; l++)
        if ((m >> l) & 1u)
            lv |= dstep_lvl(s, l) << (2u * l);
    audition_req(m, lv);
}
static void audition_block(void)
{
    uint32_t m = aud_lanes, lv = aud_lvl, l;
    if (!m)
        return;
    aud_lanes = 0;
    for (l = 0; m; l++, m >>= 1)
        if (m & 1u)
            trk_note_on(TDRUM, LANE_NOTE[l], lvl_vel((lv >> (2u * l)) & 3u, 100));
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, k, r;
    audition_block();
    if (er_mute && layer_now() != LY_ERASE)
        er_mute = 0;                                  /* EDIT let go: its keys erase again (2.6) */
    if (!(layer_buttons() & ly_bit[LY_ROLL]))         /* ARP up (and not locked): the rolls end (the keys stay silent) */
        for (r = 0; r < NROLL; r++)
            if (roll[r].on)
                roll_end(r);
    if (!ch)
        return;
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u)
            key_down(k);
        else
            key_up(k);
        kb_prev ^= 1u << k;                           /* (key_down sees the keys down before it) */
    }
}

/* -------------------------------------------------------- metronome --- */
/* A wood block on every beat, louder on the first of the bar, from the drum kit's voices
 * (not recorded, not muted with the drum track; SETUP > DRUMS LVL sets its level; drums.c click_on: its own voice).
 * SETUP > TEMPO > CLICK: OFF (0, default: LIVE needs none), REC = while a track records,
 * ON = while playing. */
#define CLICK_REC 1
#define CLICK_ON 2
static uint32_t click_last = SEQ_NONE;          /* the beat it last played */
/* fm1-drummer: the bar is the meter of the section playing (a song's section, sng.c; 0 = 4/4, SLOOP's always):
 * the numerator in bits 0..4, the denominator's code in bits 5..6. Bars are counted in 16ths from the section's
 * start (seq_reset_tracks), so a 7/8 bar ends inside a beat; a beat of the click is a 1/den note */
static uint8_t clk_meter;
static const uint8_t METER_DEN[4] = {4, 8, 2, 16};
static uint32_t meter_num(uint32_t m) { return m & 31u ? m & 31u : 4u; }
static uint32_t meter_den(uint32_t m) { return METER_DEN[(m >> 5) & 3u]; }
static uint32_t meter_b16(uint32_t m) { return 16u * meter_num(m) / meter_den(m); }   /* the bar in 16ths */
static uint32_t clk_16th(void) { return clk_beat * 4u + clk_pos / (BEAT_U / 4u); }
static int clk_new_bar(uint32_t *last)          /* a bar starts in this block (not seen yet: *last, its index) */
{
    uint32_t p = clk_16th(), b = meter_b16(clk_meter);
    if (p % b || p / b == *last)
        return 0;
    *last = p / b;
    return 1;
}
static void click_tick(void)
{
    uint32_t mode = (uint32_t)song.g[G_CLOCK], p = clk_16th(), c = p / (16u / meter_den(clk_meter));
    if (!song.playing || c == click_last)
        return;
    click_last = c;
    if (mode == CLICK_ON)                           /* (REC: live recording is gone, drums only: as OFF) */
        click_on(p % meter_b16(clk_meter) == 0u);
}

/* -------------------------------------------------------- sequencer --- */
#if FELUCCA_DRUMMER
/* ---- THE SECTIONS: a song's (sng.c), asked for by the pedal, LIVE's keys or the MIDI notes; a section asked for
 * while playing starts on the bar (every track from its step 0). Without a song there are no sections: the drum
 * track's own pattern loops. (SLOOP's live sections A..D, its quick chain and SONG REC, JAM's, are gone since
 * 2026-10-27: docs/design.md) */
static volatile int8_t live_req = -1;              /* section asked for (UI), applied on the next bar */
static volatile int8_t live_sec = -1;              /* the section playing: last jumped to or loaded */
static uint32_t live_bar = 0xFFFFFFFFu;            /* clk_beat / 4 of the last bar seen (= bars the section played) */
/* fm1-drummer (pedal.c): the pedal's fill (CC 21) is an arranger's fill-in: from the press to the end of the bar
 * (FILL ONLY steps and F hits play, NO FILL and N rest; a fill bar of its own instead of the pattern's), then the
 * pattern again from its top on the next bar, or the section asked for meanwhile; asked for in the bar's last beat,
 * the whole next bar. A section change waits for the section's end (CC 20) or the next bar (CC 27), and with a fill
 * pending for the fill's end; an ending plays the outro for its bars, then stops on the bar */
static volatile uint8_t pedal_fill;                /* the pedal's fill plays, for pedal_fill_bars (this bar counted) */
static volatile uint8_t pedal_at_end;              /* live_req (or the ending's stop) waits for the section's end */
static volatile uint8_t pedal_fill_wait;           /* the fill starts on the next bar line (1): a fill bar of its own, whole
                                                    * (2026-10-30); none, CC 21 in the bar's last beat (the next bar, whole) */
static volatile uint8_t pedal_end_req;             /* bars of the outro asked for (taken when its section starts) */
static volatile uint32_t pedal_end_bars;           /* stop when the section playing has played this many (0: none) */
static volatile uint8_t pedal_end_eb;              /* the ending's outro landed through its entry: pedal_end_req is taken
                                                    * at the entry's end (2026-10-28: taken at the landing, a 1-bar outro
                                                    * stopped as its entry ended, its pattern unheard) */
static volatile uint8_t live_req_brk;              /* live_req comes in through its break (CC 28), not its entry */
/* fm1-drummer (sng.c): a song loaded from flash: its sections are what live_req names */
#define SNG_SLOTS 16u                              /* the user's songs, in flash (8 until 2026-10-14) */
#define SNG_FAC0 SNG_SLOTS                         /* songs 16..31: the factory grooves, read-only in the firmware image
                                                    * (tools/gen_songs.py; their own bank since 2026-10-15: before, a
                                                    * user slot never written played one) */
#define SNG_ALL (SNG_SLOTS * 2u)                   /* the songs a PC, the SONGS screen and the settings reach */
#define SNG_SMP_TAKEN(k) ((k) < 2u)                /* the user sample slots (USR1, USR2) whose flash the songs take */
#define SNG_SECTIONS 16u                           /* sections a song holds at most (8 until 2026-10-14) */
static int8_t sng_cur = -1;                        /* the song: 0..15 a user slot, 16..31 a groove, -1 = none (the
                                                    * drum track's own pattern) */
static volatile uint8_t sng_req;                   /* PC: song + 1 to load, 0xFF to unload (sng_poll, main loop) */
static void sng_label(char *b, uint32_t k)         /* how the screens name song k: "song 3", "factory 3" (b: 11) */
{
    str_cpy(b, k < SNG_FAC0 ? "song " : "factory ", 9);
    fmt_int(b + str_len(b), (int32_t)(k % SNG_SLOTS) + 1);
}
static void sng_apply(uint32_t k);                 /* section k of the song into the drum track (at the bar) */
static void sng_eb(uint32_t k, uint32_t brk);      /* its entry (brk 0) or break armed (eb_dstep), as it lands */
static uint32_t sng_ready(void);                   /* bit k: section k exists */
static uint32_t sng_bars(uint32_t k);              /* the bars section k takes */
static uint32_t sng_meter(uint32_t k);             /* section k's meter (clk_meter's form) */
static uint32_t sng_outro(void);                   /* the section flagged as the outro, 0xFF = none */
static int sng_pending(void);                      /* a song loaded while playing waits for its first section */
static void sng_name(uint32_t k, char *out);       /* section k's name (12 + 1 chars) */
/* the songs in flash as the SONGS screen lists them (ui_drummer.c): read again by sng_lib_refresh (sng.c, the
 * UI's loop) only when sng_lib_dirty is set (boot, a load, a write): a slot's read goes through its whole copy for
 * the CRC */
typedef struct {
    char name[13];                                 /* "": an empty slot */
    uint8_t nsec;                                  /* its sections */
    uint16_t bpm;
} sng_lib_t;
static sng_lib_t sng_lib[SNG_ALL];                 /* the user's 16, then the grooves */
static uint8_t sng_lib_dirty = 1;
static void sng_lib_refresh(void);
static int sng_save_section(void);               /* EDIT, SAVE held: the section into its song (project.c) */
static int sng_lossy(void);                        /* the drum track holds nudges or locks (sng.c) */
/* the SECTIONS screen (ui_drummer.c, 2026-10-29): the song's shape drafted on the device (sng.c packs, project.c writes) */
enum { SE_DUP, SE_DEL, SE_LEFT, SE_RIGHT, SE_OUTRO };
static int sng_edit(uint32_t op, uint32_t k);    /* op on section k of the song loaded, the grid saved with it
                                                    * (sng_save_section's codes; 0: done, the drum track holds the
                                                    * section at its new place) */
static int sng_new(uint32_t slot);               /* a song of one section, the drum track's pattern, into slot; loaded */
static int sng_same(uint32_t k);                 /* the drum track still is section k as packed */
/* SETUP > PEDAL (ui_menu.c), kept with the pedal settings (sng.c arrset): the last song loaded comes back at start */
static uint8_t sng_autoload;                       /* 1: load the last song at start */
static uint8_t sng_last;                           /* that song + 1 (a groove: 17..32), 0 none */
static uint8_t arrset_dirty;                       /* the pedal settings changed: saved once stopped (project.c) */
/* what the screen shows (ui_draw.c draw_head: the section playing and its bar; the pedal's events as messages) */
static char sng_ui_name[13];                       /* the section playing: the song's name for it */
static char sng_ui_song[13];                       /* the song loaded, or empty */
static char sng_ui_next[13];                       /* the section the pedal asked for, until it starts (empty: none) */
/* ---- the division changed (2026-10-29: the SEQ layer's DIV knob, the PATTERN page's, the editor's SET; the user: "moving
 * from 1/16 to 1/8 deletes every other 16th note, but keeps the hits in the same position on the beat"). The pattern is
 * re-gridded: the new grid's step j takes the old step at the same time, when there is one (j x the new step's units =
 * i x the old's), else it is empty. So 1/16 -> 1/8 keeps every other step, 1/8 -> 1/16 puts an empty step between,
 * 1/16 -> 8T keeps the beats (the rest goes); the pattern's length in time is kept, rounded up to a step. The steps'
 * nudges, locks and conditions, the song's marks (fonly, fno, flam) and the section's fill bar go with their steps (its
 * entry and break at the save: sng.c sng_pack_rediv, the same rule on the packed steps). A pattern that would not fit
 * (over NSTEP steps) keeps its division: pattern_rediv_ok says so first, the UI tells (ui.c pattern_div_set). A step is
 * 1/48 of a beat or more (div_units), so the times compare as whole numbers. */
static uint32_t div_u48(uint32_t div) { return div_units(div % NDIV_STEP) / (BEAT_U / 48u); }   /* a step, in 1/48 beat */
static uint32_t rediv_len(uint32_t n, uint32_t uo, uint32_t un) { return (n * uo + un - 1u) / un; }   /* n steps of uo at un */
/* the old step (uo units, n of them) at the new step j (un units): its index, or -1 (between old steps, or past them) */
static int32_t rediv_src(uint32_t j, uint32_t n, uint32_t uo, uint32_t un)
{
    uint32_t t = j * un;
    return t % uo == 0u && t / uo < n ? (int32_t)(t / uo) : -1;
}
/* n steps (and the flams beside them, if given) of uo units re-gridded in place at un units, m steps (m <= NSTEP);
 * in place: a coarser grid reads at or past where it writes (j up), a finer one at or before (j down) */
static void rediv_dsteps(dstep_t *d, uint16_t *flam, uint32_t n, uint32_t m, uint32_t uo, uint32_t un)
{
    uint32_t k;
    for (k = 0; k < m; k++) {
        uint32_t j = un >= uo ? k : m - 1u - k;
        int32_t i = rediv_src(j, n, uo, un);
        if (i < 0) {
            memset(&d[j], 0, sizeof(dstep_t));
            if (flam)
                flam[j] = 0;
        } else if ((uint32_t)i != j) {
            d[j] = d[i];
            if (flam)
                flam[j] = flam[i];
        }
    }
    for (k = m; k < n; k++) {                        /* shorter: the steps past the end cleared */
        memset(&d[k], 0, sizeof(dstep_t));
        if (flam)
            flam[k] = 0;
    }
}
static int pattern_rediv_ok(const track_t *t, uint32_t div)   /* the pattern fits at div (NSTEP steps at most) */
{
    return rediv_len(trk_len(t), div_u48(trk_div(t)), div_u48(div)) <= NSTEP;
}
#if FELUCCA_DRUMMER
/* the fill bar's phrase: a bar of meter m in the steps of division div (whole, as packed; else the phrase is one bar) */
static void fb_phrase(uint32_t div, uint32_t m)
{
    uint32_t u = div_u48(div), b = 12u * meter_b16(m);
    uint32_t bs = u && b % u == 0u && b / u <= fb_len ? b / u : fb_len;
    fb_bar = (uint8_t)bs;
    fb_bars = (uint8_t)(bs ? (fb_len + bs - 1u) / bs : 0u);
}
static void fb_rediv(uint32_t uo, uint32_t un, uint32_t div)   /* the section's fill bar follows the pattern's division */
{
    uint32_t m;
    if (!fb_len)
        return;
    m = rediv_len(fb_len, uo, un);
    if (m > NSTEP) {                                 /* the whole bars that fit */
        uint32_t b = 12u * meter_b16(clk_meter), bs = b % un == 0u ? b / un : 0u;
        m = bs && bs <= NSTEP ? NSTEP - NSTEP % bs : NSTEP;
    }
    rediv_dsteps(fb_dstep, fb_flam, fb_len, m, uo, un);
    fb_len = (uint8_t)m;
    fb_phrase(div, clk_meter);
}
#else
static void fb_rediv(uint32_t uo, uint32_t un, uint32_t div) { (void)uo; (void)un; (void)div; }
#endif
/* what goes beside the steps (the nudges, the conditions, the song's marks, the locks) re-gridded as rediv_dsteps does
 * the steps: n steps of uo units onto m of un (the IRQ off: the caller's). Also ui.c undo_swap, after the steps and the
 * length swapped with the snapshot (taken on the other grid): the snapshot's steps come back, these follow the grid */
static void rediv_extras(track_t *t, uint32_t n, uint32_t m, uint32_t uo, uint32_t un)
{
    uint32_t k;
    for (k = 0; k < m; k++) {
        uint32_t j = un >= uo ? k : m - 1u - k;
        int32_t i = rediv_src(j, n, uo, un);
        if (i < 0) {
            t->micro[j] = 0;
            step_fill_set(t, j, FC_NORM);
            drum_marks_clear(j);
        } else if ((uint32_t)i != j) {
            t->micro[j] = t->micro[(uint32_t)i];
            step_fill_set(t, j, step_fill(t, (uint32_t)i));
            drum_marks_move(j, (uint32_t)i);
        }
    }
    for (k = m; k < n; k++) {
        t->micro[k] = 0;
        step_fill_set(t, k, FC_NORM);
        drum_marks_clear(k);
    }
    for (k = 0; k < NLOCK; k++) {                    /* a lock goes with its step, or goes (one past the old length would
                                                      * come alive on a longer grid: it goes too) */
        uint32_t s = t->lock[k].step, u = s * uo;
        if (s == LOCK_FREE)
            continue;
        t->lock[k].step = s < n ? (u % un || u / un >= m ? LOCK_FREE : (uint8_t)(u / un)) : s < m ? LOCK_FREE : (uint8_t)s;
    }
}
/* the pattern of t onto the grid of div (the IRQ off meanwhile); 0: it would not fit, nothing changed. The caller marks
 * the undo (undo_swap takes the grid back: ui.c) */
static int pattern_rediv(track_t *t, uint32_t div)
{
    uint32_t old = trk_div(t), uo = div_u48(old), un, len = trk_len(t), m;
    div %= NDIV_STEP;
    un = div_u48(div);
    if (div == old)
        return 1;
    m = rediv_len(len, uo, un);
    if (!m || m > NSTEP)
        return 0;
    fm1_irq_off();
    rediv_dsteps(t->dstep, 0, len, m, uo, un);
    rediv_extras(t, len, m, uo, un);
    t->p[P_SLEN] = (int16_t)m;
    t->p[P_SDIV] = (int16_t)div;
    fb_rediv(uo, un, div);
    fm1_irq_on();
    return 1;
}

static void sng_unload(void)                       /* no song (PC 127, a project restored over the song): the drum track
                                                    * keeps its pattern, no sections */
{
    sng_cur = -1;
    seq_swing8 = 0;
    clk_meter = 0;
    fb_len = 0;                                     /* (the drum track keeps its pattern, not the song's fill bar) */
    eb_len = 0;
    eb_bars = 0;
    fb_bar = 0;
    fb_bars = 0;
    sng_ui_song[0] = 0;
    sng_ui_next[0] = 0;
}
static volatile uint8_t pedal_ui;                  /* an event to say: PU_* (the UI thread clears it) */
static char pedal_ui_name[13];                     /* its name (the section asked for, the song) */
enum { PU_NONE, PU_NEXT, PU_SONG, PU_ENDING, PU_FILL, PU_NOSONG, PU_STOP, PU_COUNT, PU_BREAK };
static void pedal_say(uint32_t ev, const char *name)
{
    uint32_t i;
    for (i = 0; i < 12u && name[i]; i++)
        pedal_ui_name[i] = name[i];
    pedal_ui_name[i] = 0;
    pedal_ui = (uint8_t)ev;
}
static void seq_stop(void);
static void seq_reset_tracks(uint32_t pos);
static uint32_t live_sec_bars(void)                /* the bars the section playing takes (its pattern's; 1 with none; its
                                                    * entry's or break's while one plays) */
{
    if (eb_bars)
        return eb_bars;
    if (live_sec < 0)
        return 1;
    return sng_cur >= 0 ? sng_bars((uint32_t)live_sec) : 1u;
}
/* the bars a fill asked for in bar `bar` of the section lasts, this one counted: its fill bar of its own (fb_bars),
 * whole (2026-10-30: it starts on the next bar line and runs past the section's end when fewer bars are left, the
 * pattern from its top after it; from 2026-10-20 to then a fill of several bars ended on the section's last bar line,
 * waiting for its last bars or cut from the front, which lost a two-bar fill asked in the last bar; before that it ran
 * from the press); with none, to the section's end (asked 2026-10-17: before, one bar and the pattern from its top,
 * which cut the phrase of a section whose fill is in its marks, or has none) */
static uint32_t fill_bars_from(uint32_t bar)
{
    uint32_t n = live_sec_bars();
    if (fb_bars)
        return fb_bars;
    if (bar == 0xFFFFFFFFu)
        bar = 0;
    return n - bar % n;
}
static uint32_t eb_bar_steps(void)                 /* a bar of the section landed, in its steps (not whole: its pattern) */
{
    uint32_t u = div_units((uint32_t)TDRUM->p[P_SDIV] % NDIV_STEP) / (BEAT_U / 48u), b = 12u * meter_b16(clk_meter);
    return u && b % u == 0u && b / u <= NSTEP ? b / u : trk_len(TDRUM);
}
static void eb_arm(uint32_t n)                     /* an entry or break of n steps decoded: it plays, its bars counted */
{
    uint32_t bs = eb_bar_steps();
    eb_len = (uint8_t)n;
    eb_bars = (uint8_t)((n + bs - 1u) / bs);
}
static __attribute__((noinline)) void eb_rest(void)                       /* the break where none is written: a bar of rest, in the section's
                                                    * meter and division (the drum track's, landed) */
{
    memset(eb_dstep, 0, sizeof eb_dstep);
    memset(eb_flam, 0, sizeof eb_flam);
    eb_arm(eb_bar_steps());
}
static void eb_cut(void)                           /* a fill cuts the entry or break playing (pedal_fill_req, live_block);
                                                    * the ending's outro in its entry: the stop at the fill's end, as
                                                    * any ending with a fill pending */
{
    eb_len = 0;
    eb_bars = 0;
    if (pedal_end_eb) {
        pedal_end_eb = 0;
        pedal_end_bars = live_bar + pedal_end_req;
        pedal_end_req = 0;
    }
}
static __attribute__((noinline)) void live_block(void)   /* once a block while playing a loop (not the song; out of line
                                                    * since 2026-10-18: its code inside mix_block made a jump table
                                                    * there read as a loop, tests/target_budget.py) */
{
    uint32_t fill_over = 0, eb_over, started = live_bar == 0xFFFFFFFFu;   /* (started: the transport's first bar, from
                                                     * stopped; a landing or a fill's restart sets live_bar itself) */
    if (!clk_new_bar(&live_bar))                    /* a new bar (in the section's meter) */
        return;
    eb_over = eb_bars && live_bar >= eb_bars;       /* the entry or break played: the pattern from its top (below) */
    if (pedal_fill) {                               /* the pedal's fill (CC 21): over at the bar line that ends it */
        if (pedal_fill_bars > 1u) {
            pedal_fill_bars--;
        } else if (pedal_fill_wait) {               /* asked again in its last beat: another one, straight on */
            pedal_fill_wait = 0;
            pedal_fill_bars = (uint8_t)fill_bars_from(live_bar);
        } else {
            pedal_fill = 0;
            pedal_fill_bars = 0;
            fill_over = 1;
        }
    } else if (pedal_fill_wait && !--pedal_fill_wait) {   /* the fill bar of its own, whole from this bar line (past the
                                                     * section's end if it must); none, asked in the last beat: this
                                                     * bar, to the section's end (at its top, the whole cycle) */
        eb_cut();                                   /* (an entry or break playing: the fill cuts it) */
        pedal_fill = 1;
        pedal_fill_bars = (uint8_t)fill_bars_from(eb_over ? 0u : live_bar);   /* (an entry or break over: the fill
                                                     * right after it, from the pattern's top, restarted below) */
    }
    if (pedal_at_end && live_bar % live_sec_bars() == 0u)   /* a request waits for the section's end: the pedal counts
                                                     * its bars since it started (live_bar), its pattern's the period */
        pedal_at_end = 0;                           /* the end: the request lands below, the ending's stop counts */
    if (pedal_end_bars && live_req < 0 && (live_bar >= pedal_end_bars || fill_over)) {   /* the ending: the outro has
                                                     * played (a fill pending pulled the stop to its end) */
        pedal_end_bars = 0;
        pedal_fill = 0;
        seq_stop();
        return;
    }
    if (live_req >= 0 && (sng_cur < 0 || !((sng_ready() >> live_req) & 1u)))
        live_req = -1;                              /* (no song, or a section the song loaded meanwhile does not have:
                                                     * dropped) */
    if (live_req >= 0 && !pedal_at_end && !pedal_fill && !pedal_fill_wait) {   /* (at_end: the section playing goes on
                                                     * to its end first; a fill pending: the change at its end) */
        uint32_t s = (uint32_t)live_req, brk = live_req_brk;
        uint32_t enter = brk || started || (int32_t)s != live_sec || sng_pending();   /* (its entry: from another
                                                     * section, from stopped, a song just loaded; never the section
                                                     * playing asked for again) */
        live_req = -1;
        live_req_brk = 0;
        {                                           /* (the UI asked for a section it checked: no hash here) */
            sng_apply(s);                           /* (fm1-drummer: the song's section, named by it) */
            live_sec = (int8_t)s;
            seq_reset_tracks(clk_pos % (BEAT_U / 4u));   /* on the bar: every track from its step 0 (a bar
                                                     * line inside a beat: the remainder past it) */
            live_bar = 0;
            if (enter)
                sng_eb(s, brk);                     /* its entry or break before its pattern (after the reset:
                                                     * counted from this step 0) */
            pedal_fill = 0;                         /* (a fill never reaches here: the change waits for its end) */
            pedal_fill_bars = 0;
            pedal_fill_wait = 0;
            if (eb_bars) {                          /* the outro through its entry: its bars from the entry's end
                                                     * (below), the entry, then the pattern once, then the stop */
                pedal_end_eb = (uint8_t)(pedal_end_req != 0u);
            } else {
                pedal_end_bars = pedal_end_req;     /* (the outro: its bars, then stop) */
                pedal_end_req = 0;
            }
            sng_ui_next[0] = 0;                     /* (the screen: it is here) */
        }
    } else if ((fill_over && fb_bars && live_req < 0) || eb_over) {   /* a fill bar of its own over, nothing asked for:
                                                     * the pattern again from its top (an arranger's variation after its
                                                     * fill-in). Without one the fill ran to the section's end: it is at
                                                     * its top, nothing to do. An entry or break over: the pattern from
                                                     * its top */
        seq_reset_tracks(clk_pos % (BEAT_U / 4u));
        live_bar = 0;
        if (pedal_end_eb) {                         /* the ending's outro, its entry played: its bars from here */
            pedal_end_eb = 0;
            pedal_end_bars = pedal_end_req;
            pedal_end_req = 0;
        }
    }
}
static int mclk_on(void);                          /* (below: the MIDI clock drives the sequencer) */
#include "pedal.c"                                 /* fm1-drummer: a pedalboard's CC / PC -> sections, transport */
#endif

/* every track from its step 0, together, at pos units into beat 0 (a song section: the arranger's
 * remainder, so the new section starts exactly on its bar) */
static void seq_reset_tracks(uint32_t pos)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        t->seq_abs = SEQ_NONE;
        t->flam_abs = SEQ_NONE;
        t->seq_idx = 0;
    }
    for (i = 0; i < NROLL; i++)
        roll[i].last = SEQ_NONE - 1u;               /* (a roll held over the start: on the grid from here) */
    clk_beat = 0;
    clk_pos = pos;
#if FELUCCA_DRUMMER
    live_bar = 0xFFFFFFFFu;                        /* (bar 0 is a new bar) */
    eb_len = 0;                                    /* (an entry or break counts from its landing: armed after this) */
    eb_bars = 0;
#endif
    fill_last_bar = 0xFFFFFFFFu;                   /* (the same for an armed fill bar) */
    click_last = SEQ_NONE;
    song.tick = 0;
    song.playing = 1;
}


static void seq_start(void)
{
    seq_reset_tracks(0);
}

static void seq_release(track_t *t)
{
    if (is_drum(t))
        seq_out_track_off(t);                       /* the drum hits of the step (MIDI OUT): ended with it */
}

static void seq_stop(void)
{
    uint32_t i;
#if FELUCCA_DRUMMER
    live_req = -1;
#endif
    fill_held = 0;                                  /* STOP ends a fill, held or armed */
    fill_arm = 0;
    fill_bar_on = 0;
#if FELUCCA_DRUMMER
    pedal_fill = 0;                                 /* (fm1-drummer: the pedal's fill and ending too) */
    pedal_at_end = 0;
    pedal_fill_bars = 0;
    pedal_fill_wait = 0;
    pedal_end_req = 0;
    pedal_end_bars = 0;
    pedal_end_eb = 0;
    live_req_brk = 0;
    eb_len = 0;
    eb_bars = 0;
    sng_ui_next[0] = 0;
#endif
    song.playing = 0;
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        locks_restore(&trk[i]);                    /* the parameters back to their base */
    }
    seq_out_all_off();
}

/* humanize (drums.c drum_hum): a hash of the step's count, so a hit varies from bar to bar but a render is the same
 * every time; the spread triangular (most hits near the grid, few at its edge) */
static const uint8_t HUM_LVL[HUM_N] = {0, 16, 32};  /* the level's spread, of 128: about +-1 and +-2 dB */
static const uint16_t HUM_SMP[HUM_N] = {0, FS * 3 / 1000, FS * 6 / 1000};   /* the timing's, in samples: +-3, +-6 ms */
static uint32_t hum_hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
static int32_t hum_tri(uint32_t h, int32_t a)       /* -a..a, triangular (no divide: the audio block's loop) */
{
    return ((int32_t)(h & 255u) + (int32_t)((h >> 8) & 255u) - 255) * a >> 8;
}
/* (out of line, called only with humanize on: the audio block's loops stay as they were, tests/target_budget.py) */
static __attribute__((noinline)) void hum_hit(const track_t *t, uint32_t l, uint32_t n)   /* lane l's hit n (a
                                                                                            * ratchet's) */
{
    if (drum_hum < HUM_N)
        drum_hum_g = (uint8_t)(128 + hum_tri(hum_hash(t->seq_abs * 64u + l * 4u + n), HUM_LVL[drum_hum]));
}
static __attribute__((noinline)) int32_t hum_shift(uint32_t abs, uint32_t slen)   /* step abs's timing, in grid units */
{
    int32_t a = drum_hum < HUM_N ? HUM_SMP[drum_hum] * clamp(song.g[G_BPM], 20, 300) : 0, cap = (int32_t)(slen >> 3);
                                                    /* (a sample is BPM grid units: a beat is BEAT_U) */
    return hum_tri(hum_hash(abs ^ 0x5BD1E995u), a < cap ? a : cap);
}

/* play one drum step: each lane a hit (not the lanes a roll plays, muted or out of a fill) */
static void drum_step(track_t *t, const dstep_t *s, uint32_t idx)
{
    uint32_t l, m = dstep_mask(s) & ~roll_lanes() & ~(uint32_t)drum_mute & ~drum_fill_mask(idx);
    seq_out_track_off(t);                           /* the last step's hits end here */
    for (l = 0; m; l++, m >>= 1)
        if (m & 1u) {
            if (drum_hum)
                hum_hit(t, l, 0);
            trk_note_on(t, LANE_NOTE[l], lvl_vel(dstep_lvl(s, l), 100));
            drum_hum_g = 0;                         /* (a muted track takes none) */
            seq_out_on(t, LANE_NOTE[l], lvl_vel(dstep_lvl(s, l), 100));
        }
}

/* ratchets: the further hits of the playing step's lanes, each at its share of the step */
static void seq_ratchets(track_t *t, uint32_t into, uint32_t slen)
{
    uint32_t fb;
    const dstep_t *s = drum_src(t, t->seq_abs, t->seq_idx, &fb);
    uint32_t i, m = dstep_mask(s) & ~roll_lanes() & ~(uint32_t)drum_mute & ~drum_fill_mask(fb ? NSTEP : t->seq_idx);
    if (t->seq_skip)
        return;                                     /* (its condition failed: no hit at all) */
    for (i = 0; m; i++, m >>= 1) {
        uint32_t hits = 1u + dstep_rat(s, i), h, done;
        if (!(m & 1u) || hits == 1u)
            continue;
        h = into * hits / slen;
        done = (t->rat_lanes >> (2u * i)) & 3u;
        if (h > done && h < hits) {
            t->rat_lanes = (t->rat_lanes & ~(3u << (2u * i))) | h << (2u * i);
            if (drum_hum)
                hum_hit(t, i, h);
            trk_note_on(t, LANE_NOTE[i], lvl_vel(dstep_lvl(s, i), 100));
            drum_hum_g = 0;
            seq_out_on(t, LANE_NOTE[i], lvl_vel(dstep_lvl(s, i), 100));
        }
    }
}

/* the nudge of grid step abs of track t, in units of a step slen long: where in its own step it fires
 * (micro >= 0), or how far before its step (micro < 0, as a negative number); humanize adds its own (at most an
 * eighth of a step: the steps stay in order) */
static int32_t micro_units(const track_t *t, uint32_t abs, uint32_t slen)
{
    int32_t m = t->micro[abs % trk_len(t) % NSTEP];
    return (int32_t)(slen / 64u) * m + (drum_hum && is_drum(t) ? hum_shift(abs, slen) : 0);   /* |m| <= 32: fits */
}

/* a step fires: its condition (a fill bar step plays as it is, no condition), its locks, its hits (out of line: once
 * a step; the ISR's per-block code stays as it was) */
static __attribute__((noinline)) void seq_fire(track_t *t, uint32_t idx)
{
    uint32_t fb;
    const dstep_t *d = drum_src(t, t->seq_abs, idx, &fb);   /* (seq_abs: this step, idx its place in the pattern) */
    t->seq_skip = (uint8_t)(!fb && !step_plays(t, idx));
    if (t->seq_skip) {                               /* its fill condition fails: as a REST with no lock */
        lock_step(t, NSTEP);                         /* (no step has locks there: the bases are back) */
        seq_out_track_off(t);
    } else {
        lock_step(t, idx);                           /* its parameter locks, before the block renders */
        drum_step(t, d, fb ? NSTEP : idx);
    }
}
#if FELUCCA_DRUMMER
/* the grace notes of the step due next (nabs; mu its nudge, in grid units; the grid is into units into step abs of
 * slen): when it is FLAM_SMP samples away (a sample is BPM units; less at a fast tempo: a quarter of the step at
 * most), each lane flagged plays a ghost of itself now, the hit itself on the grid when the step fires. Once a step
 * (flam_abs). Not for the first step of a bar while a section change, a fill's end or its start, an ending or the
 * chain could change what that bar plays: the prediction would be the pattern as it is now */
/* the first step of a bar, with what the bar plays in doubt until live_block has seen the bar line: a section cued, a
 * fill about to start, one playing (its bar count moves at the bar line) or about to end, an entry or break playing,
 * an ending. A step nudged early into such a bar (a nudge, or humanize: a hash of the step, so the same step every
 * time) waits for the bar line's block (seq_tick) and gets no grace note (seq_flam): fired before, it would play the
 * old content (2026-10-30, the user: a two-bar fill's second bar started on the fill bar's step 0 again, a second
 * crash, its first step pulled a few ms early; the restart after a fill or an entry would double a hit the same way) */
static int bar_in_doubt(const track_t *t, uint32_t idx)
{
    uint32_t u = div_units(trk_div(t)) / (BEAT_U / 48u), b = 12u * meter_b16(clk_meter);
    uint32_t bs = u && b % u == 0u ? b / u : 0u;
    if (!(bs ? idx % bs == 0u : idx == 0u))
        return 0;
    return live_req >= 0 || pedal_fill_wait == 1u || pedal_fill || pedal_end_bars != 0u || eb_len != 0u;
}
static void seq_flam(track_t *t, uint32_t nabs, int32_t mu, uint32_t abs, uint32_t into, uint32_t slen)
{
    uint32_t len = trk_len(t), idx = nabs % len, fb, m, l;
    int32_t until, reach;
    const dstep_t *s;
    if (!is_drum(t) || t->flam_abs == nabs)
        return;
    if (abs == nabs)
        until = mu - (int32_t)into;
    else if (abs + 1u == nabs)
        until = (int32_t)slen - (int32_t)into + mu;
    else
        return;
    reach = (int32_t)(FLAM_SMP * (uint32_t)clamp(song.g[G_BPM], 20, 300));
    if (reach > (int32_t)(slen >> 2))
        reach = (int32_t)(slen >> 2);
    if (until > reach)
        return;
    t->flam_abs = nabs;                              /* (in reach, or late already: once) */
    if (until < 0)
        return;
    if (bar_in_doubt(t, idx))                        /* the first step of a bar, with what the bar plays in doubt */
        return;
    s = drum_src(t, nabs, idx, &fb);
    if (!fb && !step_plays(t, idx))
        return;
    m = dstep_mask(s) & (!fb ? drum_flam[idx] : s >= eb_dstep && s < eb_dstep + NSTEP ? eb_flam[s - eb_dstep] :
                         fb_flam[s - fb_dstep]) & ~roll_lanes() & ~(uint32_t)drum_mute &
        ~drum_fill_mask(fb ? NSTEP : idx);
    for (l = 0; m; l++, m >>= 1)
        if (m & 1u) {
            if (drum_hum)
                hum_hit(t, l, 5);                    /* (its own hash: not the hit's) */
            trk_note_on(t, LANE_NOTE[l], lvl_vel(LV_GHOST, 100));
            drum_hum_g = 0;
            seq_out_on(t, LANE_NOTE[l], lvl_vel(LV_GHOST, 100));
        }
}
#endif
/* The steps fire in order, one a block at most, each at its nudged time (micro: 1/64 of a step early
 * or late): the step after the last one played (seq_abs) is due when the grid is in its own step past
 * its nudge, or, nudged early, in the previous grid step past (length - |nudge|). So a step is never
 * skipped or played twice, whatever its neighbours' nudges (two that cross play in order, a block
 * apart), and a step nudged late past the next one's early nudge still plays first. Ratchets stay on the
 * grid; seq_ratchets gets the time since the step fired. */
static void seq_tick(track_t *t)
{
    uint32_t len = trk_len(t), into, slen, abs, idx, nabs, fire = 0;
    int32_t rel, mu = 0;
    if (!song.playing)
        return;
    abs = trk_grid(t, &into, &slen);
    {
        uint32_t div = trk_div(t);
        if (t->seq_abs != SEQ_NONE && div != t->seq_den)
            t->seq_abs = abs;                        /* DIV changed: the next step of the new grid plays */
        t->seq_den = (uint8_t)div;
    }
    if (t->seq_abs == SEQ_NONE) {                    /* PLAY: the step the grid is in (nudged late: once there) */
        nabs = abs;
        mu = micro_units(t, nabs, slen);
        fire = mu <= (int32_t)into;
    } else {
        nabs = t->seq_abs + 1u;
        mu = micro_units(t, nabs, slen);
        if (abs == nabs)
            fire = mu <= (int32_t)into;              /* its own step: past its nudge (early: due already) */
        else if (abs + 1u == nabs) {
            fire = mu < 0 && (int32_t)slen + mu <= (int32_t)into;   /* the step before: nudged early into it */
#if FELUCCA_DRUMMER
            if (fire && is_drum(t) && bar_in_doubt(t, nabs % len))
                fire = 0;                            /* (a bar's first step the bar line decides: in its block, after
                                                      * live_block; bar_in_doubt) */
#endif
        } else if ((int32_t)(abs - nabs) > 0)
            fire = 1;                                /* the grid jumped ahead: catch up, a step a block */
        /* (abs + 1 == seq_abs: SWING turned up inside a played odd step: nothing until the grid is back) */
    }
#if FELUCCA_DRUMMER
    if (!fire)
        seq_flam(t, nabs, mu, abs, into, slen);      /* the next step's grace notes, FLAM_SMP samples before it */
#endif
    if (fire) {                                      /* a new step: one a block at most */
        t->seq_abs = nabs;
        idx = nabs % len;
        t->seq_idx = (uint16_t)idx;
        t->rat_lanes = 0;
        if (erasing(t))
            erase_step(t, idx);                      /* EDIT + key held: gone as it passes */
        seq_fire(t, idx);
    }
    /* the ratchets of the step playing, timed from where it fired (its hits ride with its nudge) */
    if (t->seq_abs == abs)
        rel = (int32_t)into - micro_units(t, abs, slen);
    else if (t->seq_abs == abs + 1u)
        rel = (int32_t)into - ((int32_t)slen + micro_units(t, abs + 1u, slen));
    else
        rel = (int32_t)into;
    if (t->seq_abs != SEQ_NONE)
        seq_ratchets(t, rel < 0 ? 0u : (uint32_t)rel, slen);
}

/* MIDI in: notes and CCs play the drum track on its channel (SETUP > DRUMS > CH), and on any other channel unless
 * SETUP > MIDI > OTHER is OFF; 0: none */
static track_t *midi_track(uint32_t ch)
{
    if (song.g[G_DRCH] && ch + 1u == (uint32_t)song.g[G_DRCH])
        return TDRUM;
    return song.g[G_CHOTH] ? 0 : TDRUM;
}

/* SLOOP 2.5: MIDI CCs set the drum track's parameters, after Felucca 1.1.5's standard CC map (#103, Leo
 * Kuroshita), as a knob would: 0..127 over the parameter's range, 64 the middle of a bipolar one. 7 SETUP > DRUMS
 * LVL, 10 PAN, 74 the track's FILTER (64 off, below a low-pass, above a high-pass), 91 / 94 SETUP > DRUMS REV and DLY */
static const uint8_t MIDI_CC_MAP[][3] = {             /* CC, the parameter, 1: a global one (song.g) */
    {7, G_DRLVL, 1}, {10, P_PAN, 0}, {74, P_TFLT, 0}, {91, G_DRREV, 1}, {94, G_DRDLY, 1},
};
/* MIDI LEARN (SLOOP 2.6, after Felucca 1.5 by Leo Kuroshita, Discussion #170): up to ML_N CCs, each set to one
 * parameter of one track, whatever channel it comes on (a channel IN ignores stays ignored) and whichever track that
 * channel plays. Kept with the settings (project.c settings_write, after the SYN kits), an entry 16 bits: bits 0..6
 * the CC, 7..8 the track, 9..15 the parameter's code, 0 = an empty entry: a common parameter its P_* id + 1 (their
 * ids never move, new ones go before P_E0), an engine parameter ML_E0 + k for P_E0 + k (P_E0 moves when common ones
 * are added). A learned CC no longer reaches the standard map on any channel: the controller is the user's now. The
 * CCs with a meaning of their own (0 / 32 bank select, 1 mod wheel, 6 / 38 data entry, 11 expression, 64 sustain,
 * 96..101, 120..127 the channel modes) are never learned. The UI (ui_input.c ml_*) writes the entries with the
 * interrupts off; ml_arm / ml_heard hand it a CC heard while learning */
#define ML_N 16u
#define ML_E0 120u
_Static_assert(P_E0 < ML_E0 && P_COUNT - P_E0 == 8, "MIDI LEARN: a code per parameter");
static volatile uint16_t ml_tab[ML_N];
static volatile uint8_t ml_arm, ml_heard;  /* the UI waits for a CC; the CC that came meanwhile + 1 (0 none) */
static uint32_t ml_id(uint32_t code)        /* an entry's code -> P_* id, P_COUNT: none this firmware knows */
{
    return code >= ML_E0 ? (code - ML_E0 < 8u ? P_E0 + code - ML_E0 : P_COUNT) : code && code <= P_E0 ? code - 1u : P_COUNT;
}
static int ml_free_cc(uint32_t cc)
{
    return cc && cc < 120u && cc != 1u && cc != 6u && cc != 11u && cc != 32u && cc != 38u && cc != 64u &&
           (cc < 96u || cc > 101u);
}
static void midi_cc_set(track_t *t, uint32_t id, uint32_t value)
{
    const param_desc_t *d = id >= P_E0 ? e_desc(t, id) : &TP[id % P_COUNT];
    int32_t v;
    if (d->max <= d->min)
        return;
    v = d->min + ((int32_t)(value & 127u) * (d->max - d->min) + 63) / 127;
    if (id == P_SDIV)                                /* the pattern's DIV re-grids the pattern (pattern_rediv): the main
                                                      * loop's job, not the ISR's */
        div_req = (uint8_t)(v + 1);
    else
        t->p[id % P_COUNT] = (int16_t)v;
}
/* 1: cc is learned (set here) or taken by MIDI LEARN waiting for one */
static int __attribute__((noinline)) midi_learned(uint32_t cc, uint32_t value)
{
    uint32_t i, e, id, hit = 0;
    if (!ml_free_cc(cc)) {
        if (ml_arm && !ml_heard)
            ml_heard = (uint8_t)(cc + 1u);              /* (the UI says CC NOT LEARNABLE; the CC does its own job) */
        return 0;
    }
    if (ml_arm) {
        if (!ml_heard)
            ml_heard = (uint8_t)(cc + 1u);
        return 1;
    }
    for (i = 0; i < ML_N; i++) {
        e = ml_tab[i];
        if ((e >> 9) && (e & 127u) == cc) {
            if ((id = ml_id(e >> 9)) < P_COUNT)
                midi_cc_set(&trk[(e >> 7) & 3u], id, value);
            hit = 1;
        }
    }
    return hit;
}

static void __attribute__((noinline)) midi_cc(track_t *t, uint32_t cc, uint32_t value)
{
    uint32_t i;
    if (midi_learned(cc, value))                       /* (a learned CC is the user's: not the map's) */
        return;
    for (i = 0; i < NELEM(MIDI_CC_MAP); i++) {
        const param_desc_t *d = MIDI_CC_MAP[i][2] ? &GP[MIDI_CC_MAP[i][1]] : &TP[MIDI_CC_MAP[i][1]];
        int16_t *slot = MIDI_CC_MAP[i][2] ? &song.g[MIDI_CC_MAP[i][1]] : &t->p[MIDI_CC_MAP[i][1]];
        if (MIDI_CC_MAP[i][0] == cc && d->max > d->min)
            *slot = (int16_t)(d->min + ((int32_t)value * (d->max - d->min) + 63) / 127);
    }
}

/* MIDI clock in (SETUP > PEDAL > MIDI CLOCK IN = USB or TRS; after Felucca 1.0's midi_clock.c, from contributions by
 * ChanceTheMaker and keremimo): 24 pulses a beat. While the clock runs, the sequencer advances by the
 * pulses (a pulse = BEAT_U / 24 units), interpolated up to the next one from the last interval but never
 * past it, so it follows the master's tempo changes and cannot drift; BPM shows the master's tempo
 * (the delay and arp follow it). START restarts from the top, CONTINUE carries on where it
 * stopped, STOP stops. With no pulse for 0.5 s, the internal tempo takes over (PLAY works as ever). */
#define MCLK_PULSE_U (BEAT_U / 24u)
#define MCLK_NB 8u                  /* the tempo: the mean of the last 8 beats (SLOOP 2.7.2, taken 2026-10-30) */
static struct {
    uint32_t pos, done;          /* units: the master's position (pulses since START), ours */
    uint32_t last_ms, iv_ms;     /* the last pulse, the interval between pulses (smoothed) */
    uint32_t beat_ms;            /* when pulse 0 of the last 24 came: the tempo */
    uint8_t have, n24;           /* a pulse since START; pulses towards the next tempo reading */
    uint8_t alive;               /* pulses are coming (from the SYNC source) */
    uint8_t nb, bi;              /* the last beats' lengths (bdt[], up to MCLK_NB, bi the next) */
    uint16_t bdt[MCLK_NB];
} mclk;

/* the tempo shown and played from the clock (SLOOP 2.8's "steadier external MIDI clock"): the mean of the last
 * MCLK_NB beats, in tenths, taken when it moved 0.8 BPM or more (or while the window fills), so a master whose beats
 * jitter a few ms no longer flickers BPM; a beat 5 BPM or more away from the tempo set starts the window over (a real
 * tempo change lands on the next beat, not eight later). dt: the beat's length in ms, 250 .. 1500 */
static void mclk_tempo(uint32_t dt)
{
    uint32_t k, sum = 0, t10;
    int32_t one = (int32_t)((60000u + dt / 2u) / dt), cur = song.g[G_BPM];
    if (mclk.nb && (one - cur >= 5 || cur - one >= 5))
        mclk.nb = 0;
    mclk.bdt[mclk.bi] = (uint16_t)dt;
    mclk.bi = (uint8_t)((mclk.bi + 1u) % MCLK_NB);
    if (mclk.nb < MCLK_NB)
        mclk.nb++;
    for (k = 0; k < mclk.nb; k++)
        sum += mclk.bdt[(mclk.bi + MCLK_NB - 1u - k) % MCLK_NB];
    t10 = (600000u * mclk.nb + sum / 2u) / sum;    /* (<= 8 x 600000 + 6000: fits 32 bits) */
    if (mclk.nb < MCLK_NB || (int32_t)t10 - cur * 10 >= 8 || cur * 10 - (int32_t)t10 >= 8)
        song.g[G_BPM] = (int16_t)clamp((int32_t)((t10 + 5u) / 10u), 40, 240);
}

static int mclk_on(void)                          /* the clock drives the sequencer */
{
    return song.g[G_SYNC] && mclk.alive && fm1_ms - mclk.last_ms < 500u;
}

static void mclk_event(uint32_t st, uint32_t src)  /* a realtime message; src 1 USB, 2 TRS */
{
    uint32_t now = fm1_ms;
    if (!song.g[G_SYNC] || src != (uint32_t)song.g[G_SYNC])
        return;
    if (st == 0xFAu || st == 0xFBu) {              /* START: from the top; CONTINUE: on from where it stopped */
        mclk.pos = mclk.done = 0;
        mclk.have = 0;
        if (st == 0xFAu)
            transport_req = 3;                     /* (not 1: the master counts, never a count-in) */
        else if (!song.playing)
            song.playing = 1;
        return;
    }
    if (st == 0xFCu) {                             /* STOP */
        transport_req = 2;
        return;
    }
    if (st != 0xF8u)
        return;
    if (mclk.alive && now - mclk.last_ms < 200u) { /* the interval, smoothed (a gap is not a tempo) */
        uint32_t iv = now - mclk.last_ms;
        mclk.iv_ms = mclk.iv_ms ? (mclk.iv_ms * 3u + iv + 2u) / 4u : iv;
    }
    if (!mclk.alive || now - mclk.last_ms >= 500u) {   /* (re)started: count a fresh beat */
        mclk.n24 = 0;
        mclk.nb = 0;
        mclk.beat_ms = now;
    } else if (++mclk.n24 == 24u) {                /* a beat: the tempo (the mean of the last beats) */
        uint32_t dt = now - mclk.beat_ms;
        mclk.n24 = 0;
        mclk.beat_ms = now;
        if (dt >= 250u && dt <= 1500u)             /* 40..240 BPM */
            mclk_tempo(dt);
    }
    mclk.alive = 1;
    mclk.last_ms = now;
    if (song.playing || transport_req == 1u || transport_req == 3u) {   /* (a START queued with it: the next block starts) */
        if (mclk.have)
            mclk.pos += MCLK_PULSE_U;
        mclk.have = 1;                             /* the first pulse after START is the downbeat */
    }
}

static uint32_t mclk_adv(uint32_t n)               /* units to advance this block (mclk_on) */
{
    uint32_t el, off = 0, tgt, adv, cap = n * 2u * (uint32_t)song.g[G_BPM];
    if (!mclk.have)
        return 0;                                  /* START seen: wait for the downbeat */
    el = fm1_ms - mclk.last_ms;
    if (mclk.iv_ms) {
        if (el > mclk.iv_ms)
            el = mclk.iv_ms;
        off = el * MCLK_PULSE_U / mclk.iv_ms;      /* (<= 200 x 110250: fits 32 bits) */
        if (off >= MCLK_PULSE_U)
            off = MCLK_PULSE_U - 1u;
    }
    tgt = mclk.pos + off;
    adv = (int32_t)(tgt - mclk.done) > 0 ? tgt - mclk.done : 0u;
    if (adv > cap)
        adv = cap;                                 /* behind: catch up at twice the tempo, no burst */
    mclk.done += adv;
    return adv;
}

/* everything that happens between two rendered blocks: transport, input, the steps of every
 * track at the clock, the click, the rolls and the arps; then the clock moves on by n samples */
static uint32_t seq_quiet(void)             /* the audio gate (fx.c): stopped, nothing asked, no count-in, no roll */
{
    uint32_t r;
    if (song.playing || ci_on || transport_req)
        return 0;
    for (r = 0; r < NROLL; r++)
        if (roll[r].on)
            return 0;
    return 1;
}

/* SETUP > MIDI 2 > MIDI CLOCK OUT (SLOOP 2.7's CLK OUT, taken 2026-10-30): BackBeat the clock master on USB, for a
 * looper or a DAW on the cable. The MIDI clock, 24 pulses a beat at the tempo, sent stopped too (the other gear knows
 * the tempo before PLAY); START as the sequencer starts (after a count-in: when it starts), with the pulse of its
 * downbeat; STOP when it stops (the setting or SYNC changed while playing: STOP too). Only while MIDI CLOCK IN is INT:
 * a BackBeat that follows a clock never sends one back (no loop through a computer). A pulse goes out in the block it
 * falls in: < 1 ms late. The setting is the FM-1's (project.c midi_part, byte 2 x ML_N + 5; ui_menu.c MI_CLKOUT) */
static uint8_t midi_clkout;
static uint32_t mco_u;                            /* units since the last pulse */
static uint8_t mco_play;                          /* the other gear was told START */
static void clkout_block(uint32_t n)
{
    if (!midi_clkout || song.g[G_SYNC]) {
        if (mco_play)
            midi_out_event(0x0Fu | 0xFCu << 8);       /* STOP */
        mco_play = 0;
        mco_u = 0;
        return;
    }
    if (song.playing && !mco_play) {
        midi_out_event(0x0Fu | 0xFAu << 8);           /* START, then the downbeat's pulse */
        midi_out_event(0x0Fu | 0xF8u << 8);
        mco_u = 0;
    } else if (!song.playing && mco_play) {
        midi_out_event(0x0Fu | 0xFCu << 8);           /* STOP */
    }
    mco_play = (uint8_t)(song.playing != 0);
    mco_u += n * (uint32_t)song.g[G_BPM];
    while (mco_u >= MCLK_PULSE_U) {
        mco_u -= MCLK_PULSE_U;
        midi_out_event(0x0Fu | 0xF8u << 8);           /* a pulse */
    }
}

static void events_block(uint32_t n)
{
    uint32_t adv;
#if FELUCCA_DRUMMER
    pedal_elapse(n);
#endif
    if (mo_any && !song.g[G_MIDI]) {            /* MIDI = KEYS again: end what the sequencer had sent */
        seq_out_all_off();
        mo_any = 0;
    }
    if (transport_req == 1u || transport_req == 3u) {
        uint32_t ext = transport_req == 3u;         /* a MIDI START: the master counts; cut a count-in short */
        transport_req = 0;
        if (ext && ci_on)
            ci_on = 0;
        if (ci_on) {
            ci_on = 0;                              /* PLAY again during the count-in: cancelled, */
            ci_pedal = 0;                           /* the section stays cued */
        } else {
            seq_start();
        }
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
        ci_pedal = 0;                               /* (and the pedal's count-in) */
    }
    if (ci_on) {                                    /* the pedal's count-in: one or two bars of clicks before the
                                                     * section it cued */
        if (!ci_pedal || song.playing) {
            ci_on = 0;                              /* (stopped, or it started some other way) */
            ci_pedal = 0;
        } else {
            uint32_t b, m = ci_meter;                   /* (the clicks of the section cued's meter) */
            ci_u += n * (uint32_t)song.g[G_BPM];
            b = ci_u / (4u * BEAT_U / meter_den(m));
            if (b >= (uint32_t)ci_pedal) {
                ci_on = 0;
                ci_pedal = 0;
                seq_start();
            } else if (b != ci_beat) {
                ci_beat = (uint8_t)b;
                click_on(b % meter_num(m) == 0u);   /* (the second bar's first beat accented too) */
            }
        }
    }
    adv = mclk_on() && song.playing ? mclk_adv(n) : n * (uint32_t)song.g[G_BPM];   /* (after a START) */
    clkout_block(n);                                /* SETUP > MIDI 2 > MIDI CLOCK OUT */
#if FELUCCA_DRUMMER
    if (song.playing)
        live_block();
#endif
    if (song.playing && clk_new_bar(&fill_last_bar)) {   /* a new bar: the armed fill bar (after a section: its bar 0) */
        fill_bar_on = fill_arm;
        fill_arm = 0;
    }
#if FELUCCA_DRUMMER
    fill_now = (uint8_t)(fill_held || fill_bar_on || pedal_fill);   /* (pedal_fill: fm1-drummer) */
#else
    fill_now = (uint8_t)(fill_held || fill_bar_on);
#endif
    keyboard_block();
    drum_audition_poll();                             /* (the editor's DRUM SYNTH page) */
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        track_t *t;
        mi_r++;
        if ((pkt & 15u) == 0xFu) {                    /* clock / transport: cable 0 USB, 1 TRS */
            mclk_event((pkt >> 8) & 0xFFu, ((pkt >> 4) & 15u) ? 2u : 1u);
            continue;
        }
#if FELUCCA_DRUMMER
        if ((st == 0xC0u || (st == 0xB0u && d1 >= PEDAL_CC_SECTION && d1 <= PEDAL_CC_BREAK)) &&
            pedal_listens(ch)) {                      /* the pedalboard's channel: its CCs (20..28) and PC (a song)
                                                       * go to pedal.c before any track or CC map sees them; the
                                                       * other CCs (sustain, learned ones) pass as usual */
            pedal_msg(st, ch, d1, d2);
            continue;
        }
#endif
        if (song.g[G_ROUTE] >= 2 && ch != (uint32_t)song.g[G_ROUTE] - 2u)
            continue;                                 /* 2.6: IN = CH n: that channel only; every other ignored */
        t = song.g[G_ROUTE] >= 2 ? TDRUM : midi_track(ch);
        if (st == 0xB0u) {                            /* a CC (IN = CLOCK: none) */
            if (song.g[G_ROUTE] != 1 && t)
                midi_cc(t, d1, d2);
            else if (song.g[G_ROUTE] != 1)
                (void)midi_learned(d1, d2);           /* (OTHER = OFF: a learned CC still works, on any channel) */
            continue;
        }
        if (st == 0xC0u) {                            /* Program Change (2.6): the drum track's kit (IN = CLOCK,
                                                       * OTHER = OFF and not the drum channel: none) */
            if (song.g[G_ROUTE] != 1 && t)
                pc_req[TRK_DRUM] = (uint8_t)(d1 + 1u);
            continue;
        }
        if (st == 0x90u && d2 && song.g[G_ROUTE] != 1 && t)   /* a note: its lane (IN = CLOCK: no notes) */
            drum_input(lane_of_note(d1), vel_lvl(d2));
    }
    seq_tick(TDRUM);
    click_tick();
    roll_block(adv);
    if (song.playing) {
        song.tick++;
        clk_pos += adv;
        while (clk_pos >= BEAT_U) {
            clk_pos -= BEAT_U;
            clk_beat++;
        }
    }
}
