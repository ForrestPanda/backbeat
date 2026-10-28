/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.0 sequencer: the transport clock and what plays on it.
 *   timing   the kick and the click stay together for 64 bars at any tempo (no drift); a tempo or DIV
 *            change mid-step plays one step, not a burst; LEN changes keep the track in phase; an odd
 *            LEN with SWING stays in phase with a 16-step track
 *   ratchet  x2 / x3 / x4 hits evenly in their step (drums and synth)
 *   roll     ARP + key: hits on the grid at G_ROLL; recorded as ratchets (1/32 -> x2 on 1/16 steps)
 *   erase    EDIT + key: the lane goes as the playhead passes (playing), all of it (stopped); undo / redo
 *   levels   OCT- / OCT+ held on the drum track: ghost / hard hits, recorded as such
 *   chords   P_CHORD: one key plays the chord of the scale (white keys walk the degrees from C4)
 *   mute     P_MUTE / solo: no new notes, the output fades
 *   nudge    (2.4) a step fires 1/64ths of a step early or late; crossing nudges keep the order
 *   locks    (2.4) a parameter lock sets p[] for its step, the base returns at the next one
 *   fill     (2.4) FILL ONLY / NO FILL steps follow GLO + key 9 (held) and key 10 (the next bar); STOP clears
 *   chain    (2.4) SAVE + two section taps: the sections in turn, each for its pattern's bars, looped
 * Exit status: the number of failed checks. */
#define FELUCCA_DRUMMER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/sng.c"

static uint64_t blk;
typedef struct { uint64_t blk; uint8_t note, vel; int32_t zid; } hit_t;   /* zid: the sample zone (the layer) */
static hit_t hits[200000];
static uint32_t nhits;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k, c = click_n;
    mix_block(out, CTL);
    if (click_n != c && nhits < 200000u) {             /* (2.4.1: the click is its own voice, drums.c click_on;
                                                        * logged as the GM wood blocks it used to play) */
        hits[nhits].blk = blk;
        hits[nhits].note = click_acc ? 77u : 76u;
        hits[nhits].vel = click_acc ? 120u : 72u;
        nhits++;
    }
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && nhits < 200000u) {
                hits[nhits].blk = blk;
                hits[nhits].note = drums.v[k].note;
                hits[nhits].vel = drums.v[k].vel;
                hits[nhits].zid = drums.v[k].s[4];
                nhits++;
            }
    blk++;
}
static void reset(uint32_t bpm)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    nhits = 0;
    blk = 0;
    song.g[G_BPM] = (int16_t)bpm;
    fm1_in.notes = fm1_in.buttons = 0;
    kb_prev = 0;
    ly_bit[LY_ROLL] = 1u << 8;                       /* (the buttons: any bits) */
    ly_bit[LY_ERASE] = 1u << 4;
    dyn_bit[0] = 1u << 0;
    dyn_bit[1] = 1u << 1;
}
static int fails;
static void check(int ok, const char *what)
{
    printf("seq2: %-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t count_note(uint32_t note, uint64_t b0, uint64_t b1)
{
    uint32_t i, n = 0;
    for (i = 0; i < nhits; i++)
        n += hits[i].note == note && hits[i].blk >= b0 && hits[i].blk < b1;
    return n;
}

/* fm1-drummer: humanize (SETUP > FEEL): on the layered kit, a ride written x / o (hard and soft samples) and a hat
 * on every 16th. LIGHT against OFF: the same hits in the same order, each on its layer's sample (the accent keeps
 * its sound), the levels a little apart (+-1/8) and the timing (+-3 ms), most hits moved, none past that; the same
 * run twice is the same (a hash of the step, not a free random) */
static uint32_t hum_run(uint32_t amt, hit_t *out)
{
    uint32_t i;
    reset(120);
    drum_hum = (uint8_t)amt;
    TDRUM->p[P_E0] = (int16_t)DRUM_REAL;
    rk_seed = 1;                                                           /* the round robins from the same point: a run
                                                                            * is the same as another only from one seed
                                                                            * (the hats have one take; the crash two) */
    memset(rk_last, 0, sizeof rk_last);
    for (i = 0; i < 16u; i++) {
        dstep_set(&TDRUM->dstep[i], 4u, LV_NORM, 0);                      /* HAT, every 16th */
        if (!(i & 1u))
            dstep_set(&TDRUM->dstep[i], 13u, i % 4u ? LV_SOFT : LV_NORM, 0);   /* RIDE x . o . */
    }
    transport_req = 1;
    while (blk < (uint64_t)(FS * 2 * 8 / CTL))        /* 8 bars at 120 */
        run_block();
    memcpy(out, hits, nhits * sizeof *hits);
    drum_hum = 0;
    return nhits;
}
static void t_humanize(void)
{
    static hit_t a[400], b[400], c[400];
    uint32_t na = hum_run(0, a), nb = hum_run(1, b), nc = hum_run(1, c), i, same_zone = 1, lvl_ok = 1, t_ok = 1,
             moved_t = 0, moved_v = 0, maxb = (uint32_t)(FS * 3 / 1000 / CTL) + 1u;
    for (i = 0; i < na && i < nb; i++) {
        uint32_t d = a[i].blk > b[i].blk ? (uint32_t)(a[i].blk - b[i].blk) : (uint32_t)(b[i].blk - a[i].blk);
        int32_t dv = (int32_t)b[i].vel - (int32_t)a[i].vel;
        same_zone &= a[i].note == b[i].note && a[i].zid == b[i].zid;
        lvl_ok &= dv <= (int32_t)a[i].vel / 8 + 1 && -dv <= (int32_t)a[i].vel / 8 + 1;
        t_ok &= d <= maxb;
        moved_t += d != 0u;
        moved_v += dv != 0;
    }
    check(na > 150u && na == nb && same_zone, "humanize: the same hits, each on its layer's sample (x hard, s soft)");
    check(lvl_ok && moved_v > na / 2u, "humanize LIGHT: most levels moved, none more than an eighth");
    if (!t_ok || moved_t <= na / 8u)
        printf("seq2:   humanize timing: %u of %u hits moved, limit %u blocks\n", moved_t, na, maxb);
    check(t_ok && moved_t > na / 8u, "humanize LIGHT: hits moved off the grid's block, none more than 3 ms");
    check(nc == nb && !memcmp(b, c, nb * sizeof *b), "humanize: the same run twice is the same (a hash of the step)");
}

static void t_drift(void)
{
    static const uint32_t BPM[5] = {87, 90, 120, 128, 174};
    uint32_t bi, worst = 0;
    for (bi = 0; bi < 5u; bi++) {
        uint32_t i, nk = 0, nc = 0, bad = 0;
        uint64_t kb[80], cb[80];
        reset(BPM[bi]);
        song.g[G_CLOCK] = 2;                         /* the click on: its downbeat (77) */
        dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);  /* a kick on the 1 */
        transport_req = 1;
        while (blk < (uint64_t)(FS * 60.0 / BPM[bi] * 4 * 66 / CTL))
            run_block();
        for (i = 0; i < nhits; i++) {
            if (hits[i].note == 36 && nk < 80u)
                kb[nk++] = hits[i].blk;
            if (hits[i].note == 77 && nc < 80u)
                cb[nc++] = hits[i].blk;
        }
        for (i = 0; i < nk && i < nc && i < 64u; i++) {
            uint32_t d = kb[i] > cb[i] ? (uint32_t)(kb[i] - cb[i]) : (uint32_t)(cb[i] - kb[i]);
            bad += d != 0u;
            worst = d > worst ? d : worst;
        }
        if (bad || nk < 64u || nc < 64u)
            printf("seq2:   %u BPM: %u kicks, %u clicks, %u apart\n", BPM[bi], nk, nc, bad);
        worst += (nk < 64u || nc < 64u) * 1000u;
    }
    check(worst == 0u, "64 bars at 87..174 BPM: every kick in the block of its downbeat click");
}

static void t_burst(void)
{
    uint32_t i, n0;
    int ok1, ok2;
    reset(90);
    TDRUM->p[P_SDIV] = 0;                            /* 1/4 */
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    while (clk_beat < 2u || clk_pos < BEAT_U * 85u / 100u)
        run_block();
    TDRUM->p[P_SDIV] = 3;                            /* DIV 1/4 -> 1/32 at 85 % of a step */
    n0 = nhits;
    run_block();
    ok1 = nhits - n0 <= 1u;
    reset(40);
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    while (clk_beat < 1u || clk_pos < BEAT_U / 4u * 3u / 4u)
        run_block();
    song.g[G_BPM] = 240;                             /* 40 -> 240 in the middle of a step */
    n0 = nhits;
    run_block();
    ok2 = nhits - n0 <= 1u;
    check(ok1 && ok2, "DIV 1/4 -> 1/32 and 40 -> 240 BPM mid-step: one step a block, no burst");
}

static void t_swing_odd(void)
{
    uint32_t i, ok, starts0 = 0, nb = 0, beat = 0;
    uint64_t at0[64], atb[64];
    reset(120);
    song.g[G_SWING] = 50;
    TDRUM->p[P_SLEN] = 3;
    transport_req = 1;
    for (i = 0; i < 20u * FS / CTL; i++) {
        uint32_t f0 = TDRUM->seq_abs;
        run_block();
        if (TDRUM->seq_abs != f0 && TDRUM->seq_idx == 0u && starts0 < 64u)
            at0[starts0++] = blk - 1u;
        if (clk_beat != beat) {                      /* the bars of the transport (4 beats) */
            beat = clk_beat;
            if (beat % 4u == 0u && nb < 64u)
                atb[nb++] = blk - 1u;
        }
    }
    /* step 48 = cycle 16 of 3 = bar 3 (beat 12); step 144 = cycle 48 = bar 9 (beat 36): atb[k] is beat 4 (k + 1) */
    ok = starts0 > 48u && nb > 8u && at0[16] + 1u >= atb[2] && at0[16] <= atb[2] + 1u && at0[48] + 1u >= atb[8] &&
         at0[48] <= atb[8] + 1u;
    if (!ok)
        printf("seq2:   %u %u starts; step 48: %llu %llu, step 144: %llu %llu\n", starts0, nb,
               (unsigned long long)at0[16], (unsigned long long)atb[2], (unsigned long long)at0[48], (unsigned long long)atb[8]);
    check(ok, "SWING 75 %, LEN 3: in phase with the bar after 15 bars (no drift)");
}

/* a song's swing on the 8ths (seq_swing8, sng.c SNG_H_SWING8) under 1/16: the 8ths long-short, the two 16ths of each 8th
 * sharing its length: 3:3:1:1 in every beat at swing 100 (Money's shuffle: the hats on the 8ths move) */
static void t_swing8(void)
{
    uint32_t i, k, n = 0, ok = 1;
    uint64_t d[64];
    reset(120);
    song.g[G_SWING] = 100;
    seq_swing8 = 1;
    TDRUM->p[P_SDIV] = 2;                             /* 1/16 */
    TDRUM->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    for (k = 0; k < 4u * (uint32_t)(FS / CTL); k++) run_block();   /* 4 s = 8 beats = 32 hits */
    for (i = 0; i < nhits && n < 64u; i++)
        if (hits[i].vel && hits[i].note != 76u && hits[i].note != 77u) d[n++] = hits[i].blk;
    for (i = 4; i + 4u < n; i += 4) {
        uint64_t a = d[i + 1] - d[i], b = d[i + 2] - d[i + 1], c = d[i + 3] - d[i + 2], e = d[i + 4] - d[i + 3];
        if (a + 1u < b || b + 1u < a || c + 1u < e || e + 1u < c || a < 5u * c / 2u || a > 7u * c / 2u) ok = 0;
    }
    if (!ok || n < 28u)
        printf("seq2:   swing8: %u hits; gaps %llu %llu %llu %llu\n", n, n > 8u ? (unsigned long long)(d[5] - d[4]) : 0ull,
               n > 8u ? (unsigned long long)(d[6] - d[5]) : 0ull, n > 8u ? (unsigned long long)(d[7] - d[6]) : 0ull, n > 8u ? (unsigned long long)(d[8] - d[7]) : 0ull);
    check(n >= 28u && ok, "swing on the 8ths (a song's shuffle) under 1/16: the 16ths 3:3:1:1 in each beat at swing 100");
    seq_swing8 = 0;
    song.g[G_SWING] = 0;
}

static void t_len_phase(void)
{
    reset(120);
    TDRUM->p[P_SLEN] = 32;
    transport_req = 1;
    while (TDRUM->seq_idx != 20u)
        run_block();
    TDRUM->p[P_SLEN] = 16;                           /* LEN 32 -> 16 at step 20 */
    while (TDRUM->seq_idx == 20u)
        run_block();
    check(TDRUM->seq_idx == TDRUM->seq_abs % 16u, "LEN 32 -> 16 while playing: in phase with the transport's 16-step grid");
}

static void t_ratchet(void)
{
    uint32_t i, n = 0, gaps_ok = 1;
    uint64_t b[8];
    reset(120);
    dstep_set(&TDRUM->dstep[0], 4, LV_NORM, 3);      /* a hat x4 on step 0 */
    dstep_set(&TDRUM->dstep[1], 0, LV_NORM, 0);      /* a kick on step 1 */
    transport_req = 1;
    while (blk < div_samples(2) * 2u / CTL + 2u)
        run_block();
    for (i = 0; i < nhits; i++)
        if (hits[i].note == 42 && n < 8u)
            b[n++] = hits[i].blk;
    for (i = 1; i < n; i++) {
        int32_t g = (int32_t)(b[i] - b[i - 1]), want = (int32_t)(div_samples(2) / 4u / CTL);
        gaps_ok &= g >= want - 1 && g <= want + 1;
    }
    check(n == 4u && gaps_ok && count_note(36, 0, blk) == 1u, "drums: a hat x4 plays 4 even hits in its step, the next step plays");
}

/* a flam (2026-10-17): a hit flagged plays a grace note first, the same lane as a ghost, FLAM_SMP (1100) samples
 * before the hit, which stays on the grid; a quarter of the step at most at a fast tempo; a ratchet's first hit only;
 * none for the first step of a bar while a section change is pending (the bar's pattern in doubt) */
static void t_flam(void)
{
    uint32_t i, n = 0, snare = 0, want = (FLAM_SMP + CTL / 2u) / CTL;
    uint64_t b[4];
    uint8_t v[4];
    reset(120);
    TDRUM->p[P_E0] = DRUM_DS0;                       /* (the 808: a voice's velocity is the hit's; MULDJORD's layers rescale it) */
    dstep_set(&TDRUM->dstep[4], 2, LV_NORM, 0);      /* a snare on step 4, a flam */
    drum_flam[4] = 1u << 2;
    dstep_set(&TDRUM->dstep[8], 2, LV_HARD, 1);      /* a snare x2 on step 8, a flam: one grace, two hits */
    drum_flam[8] = 1u << 2;
    dstep_set(&TDRUM->dstep[12], 0, LV_NORM, 0);     /* a kick on step 12, no flam */
    transport_req = 1;
    while (blk < div_samples(2) * 16u / CTL + 2u)
        run_block();
    for (i = 0; i < nhits; i++)
        if (hits[i].note == 38 && n < 4u) {
            b[n] = hits[i].blk;
            v[n++] = hits[i].vel;
        }
    snare = count_note(38, 0, blk);
    if (n != 4u)
        printf("seq2:   flam: %u snare hits\n", snare);
    check(n == 4u && v[0] == lvl_vel(LV_GHOST, 100) && v[1] == lvl_vel(LV_NORM, 100) && v[2] == lvl_vel(LV_GHOST, 100) && v[3] == lvl_vel(LV_HARD, 100)
          && b[1] - b[0] >= want - 1u && b[1] - b[0] <= want + 1u && b[3] - b[2] >= want - 1u && b[3] - b[2] <= want + 1u
          && snare == 5u && count_note(36, 0, blk) == 1u,
          "flam: a ghost grace note ~25 ms before the hit, the hit on the grid; a ratchet's first hit only; the kick plain");
    {   /* the hit's grid time is the plain step's: the grace does not move it */
        uint32_t k, plain = 0;
        for (k = 0; k < nhits; k++)
            if (hits[k].note == 36)
                plain = (uint32_t)hits[k].blk;
        check(plain > b[1] && (plain - (uint32_t)b[1]) * CTL >= div_samples(2) * 8u - CTL && (plain - (uint32_t)b[1]) * CTL <= div_samples(2) * 8u + CTL,
              "flam: the kick 8 steps after the first snare: the snare's hit stayed on the grid");
    }
    reset(240);                                      /* 240 BPM, 1/32 steps (~1378 samples): the grace a quarter of the step */
    TDRUM->p[P_SDIV] = 3;
    dstep_set(&TDRUM->dstep[4], 2, LV_NORM, 0);
    drum_flam[4] = 1u << 2;
    transport_req = 1;
    while (blk < div_samples(3) * 8u / CTL + 2u)
        run_block();
    n = 0;
    for (i = 0; i < nhits; i++)
        if (hits[i].note == 38 && n < 2u)
            b[n++] = hits[i].blk;
    check(n == 2u && (b[1] - b[0]) * CTL <= div_samples(3) / 4u + CTL && (b[1] - b[0]) * CTL >= div_samples(3) / 4u - CTL,
          "flam: at 240 BPM on 32nds the grace is a quarter of the step before the hit");
    reset(120);                                      /* a section change pending: no grace for a bar's first step */
    dstep_set(&TDRUM->dstep[0], 2, LV_NORM, 0);
    drum_flam[0] = 1u << 2;
    TDRUM->p[P_SLEN] = 16;
    transport_req = 1;
    while (blk < div_samples(2) * 12u / CTL)
        run_block();
    live_req = 1;
    while (blk < div_samples(2) * 20u / CTL)
        run_block();
    live_req = -1;
    n = 0;
    for (i = 0; i < nhits; i++)
        if (hits[i].note == 38 && hits[i].vel == lvl_vel(LV_GHOST, 100))
            n++;
    check(n == 0u, "flam: no grace for a bar's first step while a section change is pending");
}

static void t_roll(void)
{
    uint32_t i, k = 6, n;                           /* key 6 = B3 -> lane 3 (clap) */
    reset(120);
    song.g[G_ROLL] = 2;                              /* 1/32 */
    transport_req = 1;
    run_block();
    fm1_in.buttons = ly_bit[LY_ROLL];                /* ARP held */
    fm1_in.notes = 1u << k;
    for (i = 0; i < div_samples(2) * 8u / CTL; i++)  /* 8 steps */
        run_block();
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    run_block();
    n = count_note(LANE_NOTE[3], 0, blk);
    {
        uint32_t s, on = 0;
        for (s = 0; s < 16u; s++)
            on += dstep_has(&TDRUM->dstep[s], 3);
        if (n < 15u || n > 17u)
            printf("seq2:   roll: %u hits\n", n);
        check(n >= 15u && n <= 17u, "roll 1/32 held 8 steps: ~16 hits");
        check(on == 0u, "roll: nothing written into the pattern (live recording is gone)");
    }
}

static void t_erase_undo(void)
{
    uint32_t i, left = 0, kept = 0;
    reset(120);
    for (i = 0; i < 16u; i++) {
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);  /* hats everywhere */
        if (i % 4u == 0u)
            dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    }
    transport_req = 1;
    while (TDRUM->seq_idx != 4u)
        run_block();
    fm1_in.buttons = ly_bit[LY_ERASE];               /* EDIT held + the hat key (C4 = key 7) */
    fm1_in.notes = 1u << 7;
    while (TDRUM->seq_idx != 7u)
        run_block();
    for (i = 0; i < 20u; i++)                        /* (into step 7, not 8) */
        run_block();
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    run_block();
    for (i = 0; i < 16u; i++) {
        left += dstep_has(&TDRUM->dstep[i], 4);
        kept += dstep_has(&TDRUM->dstep[i], 0);
    }
    if (left != 12u)
        printf("seq2:   erase: %u hats left, %u kicks; %d %d\n", left, kept, dstep_has(&TDRUM->dstep[5], 4), dstep_has(&TDRUM->dstep[9], 4));
    check(left == 12u && kept == 4u && !dstep_has(&TDRUM->dstep[5], 4) && dstep_has(&TDRUM->dstep[9], 4),
          "EDIT + hat while playing steps 4..7: those hats gone, the rest and the kicks kept");
    {   /* undo (ui.c undo_swap): the hats back */
        dstep_t tmp[NSTEP];
        memcpy(tmp, TDRUM->dstep, sizeof tmp);
        memcpy(TDRUM->dstep, undo.st, sizeof tmp);
        memcpy(undo.st, tmp, sizeof tmp);
    }
    left = 0;
    for (i = 0; i < 16u; i++)
        left += dstep_has(&TDRUM->dstep[i], 4);
    check(left == 16u && undo.trk == TRK_DRUM, "undo: the erased hats are back");
    transport_req = 2;
    run_block();
    fm1_in.buttons = ly_bit[LY_ERASE];               /* stopped: every hat of the pattern */
    fm1_in.notes = 1u << 7;
    run_block();
    fm1_in.notes = fm1_in.buttons = 0;
    run_block();
    left = 0;
    for (i = 0; i < 16u; i++)
        left += dstep_has(&TDRUM->dstep[i], 4);
    check(left == 0u, "EDIT + hat while stopped: every hat gone");
}

static void t_levels(void)
{
    uint32_t i, gh = 0, hd = 0;
    reset(120);
    TDRUM->p[P_E0] = (int16_t)DRUM_DS0;              /* (the 808: a hit at its own velocity; kit 0 is MULDJORD since
                                                      * 2026-10-15, which plays a layer at the layer's velocity) */
    transport_req = 1;
    run_block();
    fm1_in.buttons = dyn_bit[0];                     /* OCT- held: a ghost snare (A3 = key 4) */
    fm1_in.notes = 1u << 4;
    run_block();
    fm1_in.notes = 0;
    run_block();
    while (TDRUM->seq_idx != 3u)
        run_block();
    fm1_in.buttons = dyn_bit[1];                     /* OCT+ held: a hard kick */
    fm1_in.notes = 1u << 0;
    run_block();
    fm1_in.notes = fm1_in.buttons = 0;
    run_block();
    for (i = 0; i < 16u; i++) {
        if (dstep_has(&TDRUM->dstep[i], 2))
            gh = dstep_lvl(&TDRUM->dstep[i], 2);
        if (dstep_has(&TDRUM->dstep[i], 0))
            hd = dstep_lvl(&TDRUM->dstep[i], 0);
    }
    check(!gh && !hd && !dstep_has(&TDRUM->dstep[0], 2), "OCT- / OCT+ held on the drums: nothing written into the pattern");
    {
        uint32_t vg = 0, vh = 0;
        for (i = 0; i < nhits; i++) {
            if (hits[i].note == 38)
                vg = hits[i].vel;
            if (hits[i].note == 36)
                vh = hits[i].vel;
        }
        check(vg == 42u && vh == 127u, "... and played at 42 / 127");
    }
}
static void t_mute(void)
{
    uint32_t i, n0;
    reset(120);
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    transport_req = 1;
    while (blk < 200u)
        run_block();
    TDRUM->p[P_MUTE] = 1;
    n0 = nhits;
    while (blk < 400u)
        run_block();
    check(nhits == n0, "drum track muted: no hits");
    check(TDRUM->att == 32767, "drum track muted: faded out");
    TDRUM->p[P_MUTE] = 0;
    while (blk < 600u)
        run_block();
    check(nhits > n0 && TDRUM->att == 0, "mute off: the drums back (one track: no solo)");
}
static void mclk_push(uint32_t pkt) { midi_in_q[mi_w % MQ] = pkt; mi_w++; }
static void mclk_run(double *t, double *next, double per, double until, uint32_t src)
{
    while (*t < until) {
        while (*next <= *t) {
            mclk_push(0xF80Fu | src << 4);
            *next += per;
        }
        fm1_ms = (uint32_t)*t;
        run_block();
        *t += (double)CTL * 1000.0 / FS;
    }
}
static void t_mclk(void)
{
    double t = 0, next = 0, per = 60000.0 / 120 / 24;
    reset(90);                                        /* the internal tempo: 90 */
    mi_r = mi_w;
    song.g[G_SYNC] = 1;                               /* USB */
    mclk_run(&t, &next, per, 700, 0);                 /* the clock runs before START (the tempo shows) */
    check(!song.playing && song.g[G_BPM] == 120, "MIDI clock: stopped, BPM follows the master (120)");
    mclk_push(0xFA0Fu);                               /* START, then the downbeat pulse */
    next = t;
    mclk_run(&t, &next, per, t + 4000.0 + per / 2, 0);   /* two bars of 120 */
    check(song.playing && clk_beat == 8u, "MIDI clock: START, 2 bars at 120 -> beat 8 exactly");
    per = 60000.0 / 100 / 24;                          /* the master slows to 100 */
    mclk_run(&t, &next, per, t + 4800.0, 0);
    check(clk_beat == 16u && song.g[G_BPM] == 100, "MIDI clock: the master at 100 -> 2 more bars, BPM 100");
    mclk_push(0xFC1Fu);                               /* STOP from the TRS jack: not the source */
    mclk_run(&t, &next, per, t + 50.0, 0);
    check(song.playing, "MIDI clock: SYNC USB ignores the TRS jack");
    mclk_push(0xFC0Fu);
    mclk_run(&t, &next, per, t + 10.0, 0);
    check(!song.playing, "MIDI clock: STOP");
    t += 1000.0;                                      /* the master is gone: PLAY on the FM-1 plays */
    fm1_ms = (uint32_t)t;
    transport_req = 1;
    run_block();
    {
        uint32_t b0 = clk_beat, k;
        for (k = 0; k < (uint32_t)(FS / CTL); k++)
            run_block();
        check(song.playing && clk_beat >= b0 + 1u, "MIDI clock: no pulse for 0.5 s -> the internal tempo plays");
    }
    transport_req = 2;
    run_block();
    song.g[G_SYNC] = 0;
    mi_r = mi_w;
}

/* SLOOP 2.4 fixes: a MIDI START cuts a count-in short (the master counts); swing is off on the
 * triplet grids; a DIV change in the first beat after PLAY does not lose a step */
static void t_fixes24(void)
{
    uint32_t k, i, bpb = (uint32_t)((double)FS * 60.0 / 100.0 / CTL + 0.5), n0;
    /* MIDI START during the (pedal's) count-in */
    reset(100);
    song.g[G_SYNC] = 1;
    mi_r = mi_w;
    ci_on = 1, ci_pedal = 4, ci_meter = 0, ci_u = 0, ci_beat = 0;   /* (as pedal.c starts one: a bar of 4/4) */
    for (k = 0; k < bpb; k++) run_block();
    check(ci_on && !song.playing, "2.4: count-in running before the master starts");
    mclk_push(0xFA0Fu);                               /* the DAW starts: START */
    run_block();
    run_block();
    check(!ci_on && song.playing, "2.4: a MIDI START during the count-in starts at once");
    transport_req = 2; run_block();
    song.g[G_SYNC] = 0;
    mi_r = mi_w;

    /* swing on 8T: every beat the same */
    reset(120);
    song.g[G_SWING] = 80;
    TDRUM->p[P_SDIV] = 4;                             /* 8T: 3 a beat */
    TDRUM->p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    for (k = 0; k < 8u * (uint32_t)(FS / CTL) / 2u; k++) run_block();   /* 4 s = 8 beats */
    {
        uint64_t d[24]; uint32_t n = 0, ok = 1;
        for (i = 0; i < nhits && n < 24u; i++)
            if (hits[i].note == 36 || hits[i].note == 35 || hits[i].vel) d[n++] = hits[i].blk;
        /* 3 hits a beat, evenly spaced: consecutive gaps within one block of each other */
        for (i = 2; i < n; i++)
            if (d[i] - d[i - 1] > d[i - 1] - d[i - 2] + 1u || d[i - 1] - d[i - 2] > d[i] - d[i - 1] + 1u) ok = 0;
        check(n >= 20u && ok, "2.4: SWING on the 8T grid does nothing (triplets stay even)");
    }
    song.g[G_SWING] = 0;
    transport_req = 2; run_block();

    /* DIV 1/8 -> 1/4 in the first beat: beat 2 still plays */
    reset(120);
    TDRUM->p[P_SDIV] = 1;                             /* 1/8 */
    TDRUM->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    run_block();                                      /* (the clock is at 0 from here) */
    while (clk_beat < 1u && clk_pos < BEAT_U * 6u / 10u) run_block();   /* 60 % into beat 1: abs 1 of 1/8 */
    TDRUM->p[P_SDIV] = 0;                             /* 1/4 */
    n0 = nhits;
    while (clk_beat < 1u) run_block();                /* to the start of beat 2 */
    for (k = 0; k < 3u; k++) run_block();
    check(nhits > n0, "2.4: DIV 1/8 -> 1/4 in the first beat: the step on beat 2 plays");
    transport_req = 2; run_block();
}

/* SLOOP 2.4: steps of whole beats (DIV 1/2, 1BAR, 2BAR) and the dotted delay times */
static void t_longdiv(void)
{
    uint32_t i, k, n0, bpb = (uint32_t)((double)FS * 60.0 / 120.0 / CTL + 0.5);
    reset(120);
    TDRUM->p[P_SDIV] = 8;                             /* 2BAR: a step every 8 beats */
    TDRUM->p[P_SLEN] = 2;
    dstep_set(&TDRUM->dstep[0], 4, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[1], 5, LV_NORM, 0);
    transport_req = 1;
    run_block();
    n0 = nhits;
    for (k = 0; k < 16u * bpb + 4u; k++) run_block();   /* 16 beats: steps at beats 0 and 8, step 0 again at 16 */
    {
        uint32_t a = 0, b = 0;
        for (i = 0; i < nhits; i++) a += hits[i].note == 36 || hits[i].note == 35, b += hits[i].note == 38 || hits[i].note == 37;
        check(nhits - n0 + 1u == 3u && clk_beat == 16u, "2.4: DIV 2BAR: one step every 8 beats (3 hits in 16 beats)");
        (void)a; (void)b;
    }
    transport_req = 2; run_block();
    reset(120);
    TDRUM->p[P_SDIV] = 6;                             /* 1/2: every 2 beats */
    TDRUM->p[P_SLEN] = 4;
    for (i = 0; i < 4u; i++) dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    run_block();
    n0 = nhits;
    for (k = 0; k < 8u * bpb + 4u; k++) run_block();
    check(nhits - n0 + 1u == 5u, "2.4: DIV 1/2: one step every 2 beats (5 hits in 8 beats)");
    transport_req = 2; run_block();
    song.g[G_BPM] = 120;
    check(dly_samples(6) == (uint32_t)FS * 60u / 120u * 3u / 4u && dly_samples(7) == (uint32_t)FS * 60u / 120u * 3u / 8u,
          "2.4: delay TIME 1/8D = 3/4 beat, 1/16D = 3/8 beat");
    check(dly_samples(8) == (uint32_t)FS * 60u / 120u * 3u / 2u && GP[G_DTIME].max == 8 && !strcmp(GP[G_DTIME].names[8], "1/4D"),
          "2.6: delay TIME 1/4D = 1.5 beats, after 1/16D (older projects keep their value)");
    check(div_units(7) == BEAT_U * 4u && div_units(8) == BEAT_U * 8u && div_units(2) == BEAT_U / 4u,
          "2.4: div_units: 1BAR = 4 beats, 2BAR = 8, 1/16 = a quarter beat");
}

/* SLOOP 2.4: GLO > SYSTEM > MIDI = SEQ sends what the sequencer plays to MIDI OUT (note on, then off),
 * with the keys' channels; KEYS (the default) sends nothing of it; STOP ends every note sent */
static void mo_drain(uint32_t *on, uint32_t *off, uint32_t *onDrum)
{
    while (mo_r != mo_w) {
        uint32_t p = midi_out_q[mo_r % MQ], st = (p >> 8) & 0xF0u, ch = (p >> 8) & 15u;
        mo_r++;
        if (st == 0x90u && (p >> 24)) { (*on)++; if (ch == 9u) (*onDrum)++; }
        else if (st == 0x80u || st == 0x90u) (*off)++;
    }
}
static void cc_push(uint32_t ch, uint32_t cc, uint32_t v);
static void t_midiout(void)
{
    uint32_t k, i, on = 0, off = 0, od = 0, bpb = (uint32_t)((double)FS * 60.0 / 120.0 / CTL + 0.5);
    reset(120);
    usb.config = 1;
    mo_r = mo_w;
    song.g[G_MIDI] = 0;
    TDRUM->p[P_SDIV] = 0;                             /* 1/4: a hit a beat */
    TDRUM->p[P_SLEN] = 4;
    for (i = 0; i < 4u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    for (k = 0; k < 4u * bpb + 2u; k++) run_block();
    mo_drain(&on, &off, &od);
    check(on == 0 && off == 0, "2.4: MIDI = KEYS: the sequencer sends nothing to MIDI OUT");
    song.g[G_MIDI] = 1;
    for (k = 0; k < 4u * bpb; k++) run_block();
    mo_drain(&on, &off, &od);
    if (!(on >= 4u && od == on)) printf("seq2:   on %u off %u drum %u\n", on, off, od);
    check(on >= 4u && od == on, "2.4: MIDI = SEQ: the drum steps go out (on channel 10)");
    transport_req = 2; run_block();
    mo_drain(&on, &off, &od);
    check(on == off, "2.4: MIDI = SEQ: every note sent on was ended (STOP ends the rest)");
    song.g[G_MIDI] = 0;
    usb.config = 0;
    for (k = 0; k < (uint32_t)(FS / CTL); k++)
        run_block();                                  /* (the voices die down for the next test) */
}

/* SLOOP 2.4: per-step nudge (micro timing). A step nudged +16 fires a quarter step after its grid time,
 * -16 a quarter step before it (inside the previous grid step); over 8 bars, with two neighbours whose
 * nudges cross, no step is lost or played twice */
static void t_micro(void)
{
    uint32_t i, k, nk = 0, ns = 0, nc = 0, bad = 0, bpb = (uint32_t)((double)FS * 60.0 / 120.0 / CTL + 0.5);
    double sb = (double)FS * 60.0 / 120.0 / 4.0 / CTL, worst = 0;   /* blocks a 1/16 step */
    uint64_t kb[16], sbk[16], cb[16];
    reset(120);
    dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);      /* a kick on step 1: the grid reference */
    dstep_set(&TDRUM->dstep[4], 2, LV_NORM, 0);      /* a snare on step 5, nudged +16 (a quarter step late) */
    dstep_set(&TDRUM->dstep[8], 3, LV_NORM, 0);      /* a clap on step 9, nudged -16 (a quarter step early) */
    TDRUM->micro[4] = 16;
    TDRUM->micro[8] = -16;
    transport_req = 1;
    for (k = 0; k < 8u * 4u * bpb - 10u; k++)        /* 8 bars (short of bar 9's kick) */
        run_block();
    for (i = 0; i < nhits; i++) {
        if (hits[i].note == 36 && nk < 16u) kb[nk++] = hits[i].blk;
        if (hits[i].note == 38 && ns < 16u) sbk[ns++] = hits[i].blk;
        if (hits[i].note == 39 && nc < 16u) cb[nc++] = hits[i].blk;
    }
    for (i = 0; i < 8u && i < nk && i < ns && i < nc; i++) {
        double ds = (double)sbk[i] - (double)kb[i] - 4.25 * sb, dc = (double)cb[i] - (double)kb[i] - 7.75 * sb;
        if (ds < 0) ds = -ds;
        if (dc < 0) dc = -dc;
        worst = ds > worst ? ds : worst;
        worst = dc > worst ? dc : worst;
    }
    if (nk != 8u || ns != 8u || nc != 8u || worst > 1.0)
        printf("seq2:   micro: %u kicks, %u snares, %u claps, worst %.2f blocks\n", nk, ns, nc, worst);
    check(nk == 8u && ns == 8u && nc == 8u && worst <= 1.0, "2.4: nudge +16 fires a quarter step late, -16 a quarter step early (within a block)");
    transport_req = 2; run_block();

    reset(120);
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);  /* a hat on every step, nudged all over the place */
    for (i = 0; i < 16u; i++)
        TDRUM->micro[i] = (int8_t)((int32_t)(i * 23u % 64u) - 32);
    TDRUM->micro[5] = 31;                            /* two neighbours whose nudges cross */
    TDRUM->micro[6] = -32;
    TDRUM->micro[0] = -32;                           /* the first step as early as it goes (across the loop) */
    transport_req = 1;
    {
        uint32_t prev = TDRUM->seq_idx, order_ok = 1, fired = 0;
        for (k = 0; k < 8u * 4u * bpb - 100u; k++) { /* 8 bars, short of bar 9's step 1 (half a step early) */
            run_block();
            if (TDRUM->seq_idx != prev) {            /* the steps in order, each once */
                order_ok &= TDRUM->seq_idx == (prev + 1u) % 16u;
                prev = TDRUM->seq_idx;
            }
        }
        for (i = 0; i < nhits; i++)
            if (hits[i].note == 42)
                fired++;
        if (fired != 128u || !order_ok)
            printf("seq2:   micro: %u hats in 8 bars (want 128), order %s\n", fired, order_ok ? "ok" : "broken");
        bad = fired != 128u || !order_ok;
        check(!bad, "2.4: 16 nudged steps (two crossing) over 8 bars: every step once, in order, none doubled");
    }
    transport_req = 2; run_block();
    for (i = 0; i < 16u; i++) TDRUM->micro[i] = 0;
}

/* SLOOP 2.4: parameter locks, on the drum track. A lock on P_PAN at step 3 sets t->p[P_PAN] for that step, the base is
 * back at step 4; a knob turned during the locked step is kept as the new base; STOP restores; a lock on a parameter
 * that is not lockable is refused */
static void t_plock(void)
{
    track_t *t = TDRUM;
    uint32_t i, ok1, ok2, ok3, ok4;
    int16_t at2, at3, flt0;
    reset(120);
    t->p[P_PAN] = -20;
    flt0 = t->p[P_TFLT];
    check(lock_set(t, 2, P_PAN, 40) && lock_set(t, 2, P_TFLT, -30) && !lock_set(t, 2, P_SLEN, 8) && !lock_set(t, 2, P_AMODE, 1),
          "2.4: lock_set takes P_PAN and P_TFLT, refuses P_SLEN and P_AMODE (not lockable)");
    check(lock_set(t, 70, P_PAN, 1) == 0 && lock_set(t, 2, P_COUNT, 1) == 0, "2.4: lock_set bounds the step and the parameter");
    transport_req = 1;
    while (t->seq_idx != 2u) run_block();
    at2 = t->p[P_PAN];
    ok1 = t->p[P_TFLT] == -30;
    while (t->seq_idx != 3u) run_block();
    at3 = t->p[P_PAN];
    check(at2 == 40 && at3 == -20 && ok1 && t->p[P_TFLT] == flt0, "2.4: a lock on P_PAN / P_TFLT at step 3: set for that step, the base back at step 4");
    while (t->seq_idx != 2u) run_block();            /* the next pass: the knob is turned while the lock is on */
    ok2 = t->p[P_PAN] == 40;
    t->p[P_PAN] = 10;
    while (t->seq_idx != 3u) run_block();
    check(ok2 && t->p[P_PAN] == 10, "2.4: a knob turned during the locked step: kept as the new base");
    while (t->seq_idx != 2u) run_block();
    ok3 = t->p[P_PAN] == 40;
    transport_req = 2; run_block();
    check(ok3 && t->p[P_PAN] == 10 && t->lk_n == 0, "2.4: STOP during the locked step: the base (10) is back");
    /* the lock's value is clamped to the parameter's range */
    ok4 = lock_set(t, 5, P_PAN, 99) && t->lock[lock_find(t, 5, P_PAN, 0)].val == 63;
    lock_del(t, 2, P_COUNT);
    check(ok4 && lock_find(t, 2, P_PAN, 0) < 0 && lock_find(t, 2, P_TFLT, 0) < 0 && lock_find(t, 5, P_PAN, 0) >= 0,
          "2.4: a lock's value is clamped (PAN 99 -> 63); lock_del clears a step's locks");
    steps_clear(t);
    for (i = 0, ok4 = 1; i < NLOCK; i++) ok4 &= t->lock[i].step == LOCK_FREE;
    check(ok4 && !step_locked(t, 5), "2.4: steps_clear frees every lock and nudge");
    t->p[P_PAN] = 0;
    for (i = 0; i < (uint32_t)(FS / CTL); i++) run_block();
}

/* SLOOP 2.4: fill conditions. A FILL ONLY step is silent until GLO + key 9 is held (fill_held) and plays while
 * it is, a NO FILL step the other way round; its ratchets go with it. FILL NEXT BAR (fill_arm)
 * makes exactly the next bar a fill; STOP clears a held and an armed fill */
static uint32_t bar_run(uint32_t to_beat)         /* run to the start of beat to_beat; the first block in it */
{
    while (clk_beat < to_beat)
        run_block();
    return (uint32_t)blk;
}
static void t_fill(void)
{
    uint32_t b[8], i, ok;
    reset(120);
    dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);      /* a kick on step 1: always */
    dstep_set(&TDRUM->dstep[4], 2, LV_NORM, 3);      /* a snare x4 on step 5: FILL ONLY */
    dstep_set(&TDRUM->dstep[8], 4, LV_NORM, 0);      /* a hat on step 9: NO FILL */
    step_fill_set(TDRUM, 4, FC_FILL);
    step_fill_set(TDRUM, 8, FC_NOFILL);
    step_fill_set(TDRUM, 12, FC_FILL);               /* (an empty step: nothing either way) */
    check(step_fill(TDRUM, 4) == FC_FILL && step_fill(TDRUM, 8) == FC_NOFILL && step_fill(TDRUM, 5) == FC_NORM &&
          step_fill(TDRUM, 70) == step_fill(TDRUM, 6), "2.4: step_fill / step_fill_set: 2 bits a step, the index bounded");
    transport_req = 1;
    b[0] = (uint32_t)blk;
    run_block();
    b[1] = bar_run(4);                               /* bar 1: no fill */
    fill_held = 1;                                   /* GLO + key 9 held through bar 2 */
    b[2] = bar_run(8);
    fill_held = 0;
    b[3] = bar_run(12);                              /* bar 3: no fill again */
    ok = count_note(36, b[0], b[1]) == 1u && count_note(38, b[0], b[1]) == 0u && count_note(42, b[0], b[1]) == 1u;
    check(ok, "2.4: no fill: the kick and the NO FILL hat play, the FILL ONLY snare (x4) is silent");
    ok = count_note(36, b[1], b[2]) == 1u && count_note(38, b[1], b[2]) == 4u && count_note(42, b[1], b[2]) == 0u;
    if (!ok)
        printf("seq2:   fill held: %u kicks, %u snares, %u hats\n", count_note(36, b[1], b[2]), count_note(38, b[1], b[2]), count_note(42, b[1], b[2]));
    check(ok, "2.4: fill held: the snare plays with its 4 ratchet hits, the NO FILL hat is silent");
    check(count_note(38, b[2], b[3]) == 0u && count_note(42, b[2], b[3]) == 1u, "2.4: fill let go: back to normal");
    /* FILL NEXT BAR: armed in bar 4, bar 5 is the fill, bar 6 is not */
    bar_run(13);
    fill_arm = 1;
    run_block();
    ok = fill_arm && !fill_bar_on && !fill_now;
    b[4] = bar_run(16);
    run_block();
    ok &= !fill_arm && fill_bar_on && fill_now;
    b[5] = bar_run(20);
    run_block();
    ok &= !fill_bar_on && !fill_now;
    b[6] = bar_run(24);
    check(ok && count_note(38, b[4], b[5]) == 4u && count_note(42, b[4], b[5]) == 0u,
          "2.4: FILL NEXT BAR: the whole next bar is a fill (the snare plays, the hat not)...");
    check(count_note(38, b[5], b[6]) == 0u && count_note(42, b[5], b[6]) == 1u && count_note(38, b[3], b[4]) == 0u,
          "2.4: ... and only that bar");
    fill_arm = 1;                                    /* armed, then cancelled (the key again): no fill bar */
    fill_arm = 0;
    b[6] = bar_run(28);
    b[7] = bar_run(32);
    check(count_note(38, b[6], b[7]) == 0u && count_note(42, b[6], b[7]) == 1u, "2.4: FILL NEXT BAR pressed again before the bar: cancelled");
    fill_held = 1;
    fill_arm = 1;
    transport_req = 2;
    run_block();
    check(!fill_held && !fill_arm && !fill_bar_on && !song.playing, "2.4: STOP clears the held and the armed fill");
    for (i = 0; i < (uint32_t)(FS / CTL); i++)
        run_block();
}

/* SLOOP 2.4: GLO > SYSTEM > IN = CLOCK: MIDI note-ons are ignored (the clock is not); a note-off still ends a
 * note held when IN was switched */
static void t_midiin(void)
{
    uint32_t n0;
    reset(120);
    mi_r = mi_w;
    song.g[G_ROUTE] = 0;
    n0 = nhits;
    mclk_push(0x64269009u);                             /* note on 38 (snare), vel 100, ch 1 (cable 0, CIN 9) */
    run_block(); run_block();
    check(nhits == n0 + 1u && hits[n0].note == 38, "2.4: IN = NOTES: a MIDI note plays its drum");
    song.g[G_ROUTE] = 1;                                /* CLOCK only */
    n0 = nhits;
    mclk_push(0x64249009u);                             /* 36 on: ignored */
    run_block(); run_block();
    check(nhits == n0, "2.4: IN = CLOCK: a MIDI note-on is ignored");
    {
        double t = 0, next = 0, per = 60000.0 / 120 / 24;
        song.g[G_SYNC] = 1;
        mclk_push(0xFA0Fu);
        mclk_run(&t, &next, per, 2000.0 + per / 2, 0);
        check(song.playing && clk_beat == 4u, "2.4: IN = CLOCK: the clock still runs SLOOP (START, one bar)");
        mclk_push(0xFC0Fu); mclk_run(&t, &next, per, t + 10.0, 0);
        song.g[G_SYNC] = 0;
    }
    song.g[G_ROUTE] = 0;
    mi_r = mi_w;
}

/* SLOOP 2.5: MIDI CCs set track parameters (Felucca 1.1.5's CC map), on the track the channel plays */
static void cc_push(uint32_t ch, uint32_t cc, uint32_t v);
/* SLOOP 2.6: GLO > SYSTEM > IN = CH n: only that channel plays the drums; the others are ignored */
static void t_midich(void)
{
    uint32_t n0;
    int16_t pan;
    reset(120);
    mi_r = mi_w;
    song.g[G_ROUTE] = 2 + 11;                           /* CH 12 */
    run_block();
    n0 = nhits;
    mclk_push(0x64249009u);                             /* 36 on ch 1: ignored */
    mclk_push(0x6426900Fu);                             /* 38 on ch 10 (the drums' own): ignored */
    run_block(); run_block();
    check(nhits == n0, "2.6: IN = CH 12: notes on other channels play nothing");
    mclk_push(0x64269B09u);                             /* 38 on ch 12 */
    run_block(); run_block();
    check(nhits == n0 + 1u && hits[n0].note == 38, "2.6: IN = CH 12: a note on channel 12 plays the drums");
    pan = TDRUM->p[P_PAN];
    cc_push(11, 10, 0); cc_push(0, 10, 127);
    run_block(); run_block();
    check(TDRUM->p[P_PAN] == -64 && pan != -64, "2.6: a CC on channel 12 sets the drum track; on channel 1 it is ignored");
    TDRUM->p[P_PAN] = pan;
    song.g[G_ROUTE] = 0;
    mi_r = mi_w;
}
static void cc_push(uint32_t ch, uint32_t cc, uint32_t v) { mclk_push(0x0Bu | (0xB0u | ch) << 8 | cc << 16 | v << 24); }
static void t_midicc(void)
{
    int16_t before[P_COUNT];
    uint32_t i, drch, same = 1;
    reset(120);
    mi_r = mi_w;
    song.g[G_ROUTE] = 0;
    song.g[G_CHOTH] = 0;
    drch = song.g[G_DRCH] ? (uint32_t)song.g[G_DRCH] - 1u : 9u;
    song.g[G_DRCH] = (int16_t)(drch + 1u);
    for (i = 0; i < P_COUNT; i++) before[i] = TDRUM->p[i];
    cc_push(drch, 7, 0); cc_push(drch, 91, 127); cc_push(drch, 10, 0); cc_push(drch, 74, 127); cc_push(drch, 73, 127);
    cc_push(drch, 94, 64);
    run_block(); run_block();
    check(song.g[G_DRLVL] == 0 && song.g[G_DRREV] == 127 && song.g[G_DRDLY] == 64 && TDRUM->p[P_PAN] == -64 && TDRUM->p[P_TFLT] == 63
          && TDRUM->p[P_ATK] == before[P_ATK] && TDRUM->p[P_DLY] == before[P_DLY],
          "2.5: the drum channel: 7 / 91 / 94 = DRUMS LVL / REV / DLY, 10 PAN, 74 FILTER; 73 ignored");
    cc_push(0, 10, 127); cc_push(0, 7, 127);           /* another channel (OTHER = SEL): the drums too (drums only) */
    run_block(); run_block();
    check(TDRUM->p[P_PAN] == 63 && song.g[G_DRLVL] == 127, "drums only: a CC on another channel sets the drum track");
    song.g[G_CHOTH] = 1;                               /* OTHER = OFF: only the drum channel */
    for (i = 0; i < P_COUNT; i++) before[i] = TDRUM->p[i];
    cc_push(0, 10, 0); cc_push(1, 64, 127);
    run_block(); run_block();
    for (i = 0; i < P_COUNT; i++) same &= TDRUM->p[i] == before[i];
    check(same, "drums only: OTHER = OFF: another channel's CC changes nothing; sustain (64) is not mapped");
    song.g[G_CHOTH] = 0;
    song.g[G_DRDLY] = 0;
    song.g[G_ROUTE] = 1;
    cc_push(drch, 7, 55);
    run_block(); run_block();
    check(song.g[G_DRLVL] == 127, "2.5: IN = CLOCK: CCs ignored");
    song.g[G_ROUTE] = 0;
    mi_r = mi_w;
}

/* menu USB AUDIO = FULL (2.3): the USB input at the level of MASTER all the way up, whatever the knob;
 * the DAC path keeps following the knob */
static void t_usbfull(void)
{
    uint32_t k, i;
    int32_t out[CTL * 2], pk_dac = 0, pk_usb = 0;
    reset(120);
    song.master_q12 = 512;                            /* MASTER low */
    master_cur = -1;
    usb_full_now = 1;
    for (k = 0; k < 600u; k++) {
        if (!(k % 12u)) trk_note_on(TDRUM, k % 24u ? 38u : 36u, 90);   /* kicks and snares (90: twelve voices since the layered kit, their tails
                                                                           * add up: under the limiter) */
        mix_block(out, CTL);
        if (k > 100u)
            for (i = 0; i < 2u * CTL; i++) {
                int32_t a = out[i] < 0 ? -out[i] : out[i], u = usb_out[i] < 0 ? -usb_out[i] : usb_out[i];
                if (a > pk_dac) pk_dac = a;
                if (u > pk_usb) pk_usb = u;
            }
    }
    usb_full_now = 0;
    song.master_q12 = 2048;
    check(pk_usb > pk_dac * 3 && pk_usb <= 32767, "USB AUDIO FULL: the USB level stays up when MASTER is low, no clipping");
}

/* the track FILTER (2.4, P_TFLT): low-pass darkens the drum track, high-pass thins it; 0 is
 * bypassed again once open; it can be locked on a step */
static double tf_hf, tf_rms;
static void tf_measure(uint32_t blocks)
{
    int32_t out[CTL * 2];
    double e = 0, d = 0, prev = 0;
    uint32_t k, i;
    for (k = 0; k < blocks; k++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            double x = out[2u * i];
            e += x * x;
            d += (x - prev) * (x - prev);
            prev = x;
        }
    }
    tf_hf = e > 0 ? d / e : 0;
    tf_rms = e / (blocks * CTL);
}
static void t_tflt(void)
{
    double dhf0, dhf_lp;
    uint32_t k;
    reset(120);
    {   /* the drum track: hats and snares through the filter */
        {
            double e0 = 0, e1 = 0;
            TDRUM->p[P_TFLT] = 0;
            for (k = 0; k < 240u; k++) { if (!(k % 12u)) trk_note_on(TDRUM, 42u, 110); tf_measure(1); e0 += tf_hf; }
            TDRUM->p[P_TFLT] = -60;
            for (k = 0; k < 120u; k++) { if (!(k % 12u)) trk_note_on(TDRUM, 42u, 110); tf_measure(1); }
            for (k = 0; k < 240u; k++) { if (!(k % 12u)) trk_note_on(TDRUM, 42u, 110); tf_measure(1); e1 += tf_hf; }
            dhf0 = e0; dhf_lp = e1;
            check(dhf_lp < dhf0 * 0.5 && tflt[TRK_DRUM].mode == -1, "track FILTER on the drum track: LP darkens the hats");
            TDRUM->p[P_TFLT] = 0;
            tf_measure(300);
            check(tflt[TRK_DRUM].mode == 0, "track FILTER on the drum track: open again, bypassed");
        }
    }
    check(p_lockable(P_TFLT) && lock_set(TDRUM, 3, P_TFLT, -40) && lock_find(TDRUM, 3, P_TFLT, 0) >= 0,
          "track FILTER: a step can lock it");
    lock_del(TDRUM, 3, P_COUNT);
}

/* SLOOP 2.5: GLO > DRUMS > DLY sends the drums to the tempo delay: echoes after a hit has died; 0 none
 * (the drums' path bit for bit as 2.4); through the drum track's FILTER too */
static double drdly_echo(int32_t dly, int32_t flt)
{
    double e;
    reset(120);
    song.g[G_DRREV] = 0;
    song.g[G_DRDLY] = (int16_t)dly;
    TDRUM->p[P_TFLT] = (int16_t)flt;
    tf_measure(40);
    trk_note_on(TDRUM, 38u, 120);
    tf_measure(120);                                     /* the hit */
    tf_measure(1600);                                    /* then its echoes (or silence) */
    e = tf_rms;
    song.g[G_DRDLY] = 0;
    TDRUM->p[P_TFLT] = 0;
    return e;
}
static void t_drdly(void)
{
    double off = drdly_echo(0, 0), on = drdly_echo(127, 0), onf = drdly_echo(127, -30), half = drdly_echo(64, 0);
    printf("  drum DLY: echo rms off %.0f, 64 %.0f, 127 %.0f, 127 + filter %.0f\n", off, half, on, onf);
    check(on > off * 10.0 + 1.0 && half < on && half > off * 3.0 && onf > off * 10.0 + 1.0,
          "2.5: GLO > DRUMS > DLY: the drums echo in the tempo delay (more with more DLY, through the FILTER too); 0: none");
}

int main(void)
{
    t_midich();
    t_drdly();
    t_tflt();
    t_usbfull();
    t_micro();
    t_plock();
    t_fill();
    t_mclk();
    t_fixes24();
    t_longdiv();
    t_midiout();
    t_midiin();
    t_midicc();
    t_drift();
    t_burst();
    t_swing_odd();
    t_swing8();
    t_len_phase();
    t_ratchet();
    t_flam();
    t_roll();
    t_erase_undo();
    t_levels();
    t_mute();
    t_humanize();
    printf("seq2: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
