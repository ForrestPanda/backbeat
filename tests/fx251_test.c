/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.6, after Felucca 1.4 / 1.5 (Leo Kuroshita): the sound side of what came from there, on the drum track.
 *   insert   each INSERT type changes the drums and stays bounded; TYPE OFF and MIX 0 leave them bit for bit; TYPE OFF
 *            again: dry once its fade ran; the INSERT works on the drums' left, right and sends
 *   mutes    the drum track's live mutes: the sequencer skips a muted sound (and its ratchets), the others play
 *   learn    MIDI LEARN's map: a learned CC sets its track's parameter on any channel, not the standard map; while
 *            learning a CC is handed to the UI; the CCs with a meaning of their own are never learned
 * (drums only: the synth checks, ANALOG's filter TYPE, ENV SYNC, LFO 2, pitch bend and presets over held notes, went
 * with the synth parts.)
 * Exit status: the number of failed checks. */
#include <stdint.h>
static uint32_t host_slots[4u * 0x14000u / 4u];          /* the user sample slots (a RAM image of the flash) */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/mman.h>
static int fails;
static void check(int ok, const char *what)
{
    printf("fx251: %-88s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
#define NB 2000u                                      /* blocks rendered (1.45 s) */
static int32_t out_l[NB * CTL];
static uint32_t hit_every, rblk;                     /* render: a drum hit every hit_every blocks (0: none) */
static void render(uint32_t nb)
{
    static int32_t o[2u * CTL];
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        if (hit_every && rblk % hit_every == 0u)
            trk_note_on(TDRUM, (rblk / hit_every) & 1u ? 38u : 36u, 110);
        rblk++;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            out_l[b * CTL + i] = o[2u * i];
    }
}
static double rms(uint32_t a, uint32_t b)
{
    double e = 0;
    uint32_t i;
    for (i = a; i < b; i++) e += (double)out_l[i] * out_l[i];
    return sqrt(e / (double)(b - a));
}
static double tone(uint32_t a, uint32_t b, double hz)   /* Goertzel power */
{
    double w = 2 * M_PI * hz / FS, c = 2 * cos(w), s1 = 0, s2 = 0, s0;
    uint32_t i;
    for (i = a; i < b; i++) s0 = out_l[i] + c * s1 - s2, s2 = s1, s1 = s0;
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
/* the dry reference from this very state: a child process renders nb blocks as things are (the whole state copied:
 * the voices' phases, the buses, the random numbers) and leaves them in shared memory; this one goes on from the
 * same state */
static int32_t *shared_dry;
static void fork_dry(uint32_t nb)
{
    pid_t pid;
    if (!shared_dry)
        shared_dry = mmap(0, NB * CTL * 4u, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    fflush(stdout);
    pid = fork();
    if (!pid) {
        render(nb);
        memcpy(shared_dry, out_l, nb * CTL * 4u);
        _exit(0);
    }
    waitpid(pid, 0, 0);
}
/* a tone's level, Hann-windowed (the neighbouring harmonics leak far less than through a plain window) */
static double tonew(uint32_t a, uint32_t b, double hz)
{
    double w = 2 * M_PI * hz / FS, c = 2 * cos(w), s1 = 0, s2 = 0, s0, n = (double)(b - a), h;
    uint32_t i;
    for (i = a; i < b; i++) {
        h = 0.5 - 0.5 * cos(2 * M_PI * (double)(i - a) / n);
        s0 = h * out_l[i] + c * s1 - s2, s2 = s1, s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
/* a clean start: the defaults, the INSERT states at rest, no send, no drum hit going */
static void setup(void)
{
    uint32_t i;
    host_tracks_init();
    memset(ins, 0, sizeof ins);
    memset(&drums, 0, sizeof drums);
    for (i = 0; i < NTRK; i++) {
        trk[i].ins_run = 0;
        trk[i].tail = 0;
        trk[i].p[P_REV] = trk[i].p[P_CHOR] = trk[i].p[P_DLY] = 0;
        trk[i].p[P_LEVEL] = 100;
    }
    song.g[G_DRREV] = song.g[G_DRDLY] = 0;
    hit_every = 0, rblk = 0;
    song.playing = 0;
    song.master_q12 = 2048;
    texture_reset(0);                                   /* (GLO > TEXTURE off, at rest) */
    memset(&lim_dac, 0, sizeof lim_dac);                /* (the master's one-block delay: nothing of the last render) */
    lim_dac.env = LIM_T, lim_dac.g = 32768, lim_dac.hold = 0;
    tx.g = 0;
    song.g[G_TXSRC] = 0;
}

static void t_insert(void)
{
    static int32_t dry[NB * CTL];
    uint32_t ty, i, same;
    setup();
    hit_every = 50;                                   /* a kick, a snare.. every 50 blocks (36 ms) */
    fork_dry(400);                                    /* the same start (voices, buses, random numbers), TYPE OFF */
    memcpy(dry, shared_dry, 400u * CTL * 4u);
    TDRUM->p[P_ITYPE] = 1, TDRUM->p[P_IMIX] = 0;       /* SOFT at MIX 0: nothing runs */
    render(400);
    check(!memcmp(dry, out_l, 400u * CTL * 4u) && !TDRUM->ins_run, "INSERT at MIX 0: the drums bit for bit as with TYPE OFF");
    for (ty = 1; ty < IT_N; ty++) {
        double d = 0, r;
        int32_t mx = 0;
        setup();
        hit_every = 50;
        TDRUM->p[P_ITYPE] = (int16_t)ty, TDRUM->p[P_IMIX] = 127;
        TDRUM->p[P_IA] = 100, TDRUM->p[P_IB] = 96, TDRUM->p[P_IC] = 96;
        render(400);
        for (i = 0; i < 400u * CTL; i++) {
            d += fabs((double)out_l[i] - dry[i]);
            mx = abs(out_l[i]) > mx ? abs(out_l[i]) : mx;
        }
        r = rms(100u * CTL, 400u * CTL);
        printf("  %-5s: rms %.0f, mean |diff| %.0f, peak %d\n", N_ITYPE[ty], r, d / (400.0 * CTL), mx);
        check(d / (400.0 * CTL) > 50.0 && mx < 32767 && r > 200.0 && TDRUM->ins_run && ins[0].type == ty,
              ty == 1 ? "each INSERT type (SOFT .. CHOR) changes the drums, bounded, heard" : N_ITYPE[ty]);
        TDRUM->p[P_ITYPE] = 0;                        /* OFF: it fades out, then the drums are dry again */
        render(4);
        same = !TDRUM->ins_run;
        check(same, ty == 1 ? "TYPE OFF: the INSERT stops after its fade" : "  (fade out)");
    }
    {   /* the sequencer: a kick on every beat through FUZZ, its left, right and sends */
        double a, b;
        setup();
        song.g[G_BPM] = 120;
        for (i = 0; i < 16u; i++)
            if (!(i & 3u)) dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
        TDRUM->p[P_SLEN] = 16;
        song.g[G_DRREV] = 60;
        transport_req = 1;
        render(1400);
        a = rms(0, 1400u * CTL);
        setup();
        for (i = 0; i < 16u; i++)
            if (!(i & 3u)) dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
        TDRUM->p[P_SLEN] = 16;
        song.g[G_DRREV] = 60;
        TDRUM->p[P_ITYPE] = 4, TDRUM->p[P_IA] = 127, TDRUM->p[P_IMIX] = 127;   /* FUZZ, full drive */
        transport_req = 1;
        render(1400);
        b = rms(0, 1400u * CTL);
        printf("  drums: rms dry %.0f, FUZZ %.0f\n", a, b);
        check(a > 100 && fabs(b - a) > 0.05 * a && TDRUM->ins_run && ins[0].type == 4u && ins[2].w,
              "the drum track's INSERT (FUZZ): the kick changed, left / right / sends through it");
        transport_req = 2;
        render(4);
    }
}

static uint32_t snare;
static uint32_t kick_hits(uint32_t blocks)            /* drum voices started on the kick's note (the snare's: snare) */
{
    uint32_t b, k, n = 0, a;
    for (b = 0; b < blocks; b++) {
        a = drums.age;
        render(1);
        if (drums.age != a)
            for (k = 0; k < NDRUM; k++)
                n += drums.v[k].age > a && drums.v[k].note == LANE_NOTE[0],
                snare += drums.v[k].age > a && drums.v[k].note == LANE_NOTE[2];
    }
    return n;
}
static void t_mutes(void)
{
    uint32_t i, on, off, sn;
    setup();
    memset(&drums, 0, sizeof drums);
    for (i = 0; i < 16u; i++) {
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, i == 4u ? 1u : 0u);   /* the kick on every step, a ratchet on 5 */
        dstep_set(&TDRUM->dstep[i], 2, LV_NORM, 0);                   /* the snare too */
    }
    TDRUM->p[P_SLEN] = 16;
    transport_req = 1;
    on = kick_hits(2800);                             /* 2 s at 120 BPM: 16 steps, one with a ratchet */
    drum_mute = 1u << 0;
    snare = 0;
    off = kick_hits(2800);
    sn = snare;
    drum_mute = 0;
    printf("  kick hits: %u, muted %u (the snare meanwhile: %u)\n", on, off, sn);
    check(on >= 17u && off == 0u && sn >= 15u,
          "the drum track's live mute: the sequencer skips the muted sound (ratchets too), the others play");
    transport_req = 2;
    render(4);
}

static void cc_in(uint32_t ch, uint32_t cc, uint32_t v)
{
    midi_in_q[mi_w % MQ] = 0x0Bu | (0xB0u | ch) << 8 | cc << 16 | v << 24;
    mi_w++;
    render(2);
}
static void t_learn(void)
{
    setup();
    mi_r = mi_w;
    song.g[G_ROUTE] = 0;
    ml_tab[0] = (uint16_t)(20u | TRK_DRUM << 7 | (P_PAN + 1u) << 9);   /* CC 20 -> the drums' PAN */
    ml_tab[1] = (uint16_t)(74u | TRK_DRUM << 7 | (P_DIST + 1u) << 9);  /* CC 74 -> the drums' DST */
    TDRUM->p[P_PAN] = 0;
    cc_in(5, 20, 127);                                /* channel 6: anywhere */
    check(TDRUM->p[P_PAN] == TP[P_PAN].max, "MIDI LEARN: CC 20 on any channel sets the drums' PAN (the learned one)");
    TDRUM->p[P_TFLT] = 7;
    cc_in(9, 74, 127);
    check(TDRUM->p[P_DIST] == TP[P_DIST].max && TDRUM->p[P_TFLT] == 7,
          "MIDI LEARN: a learned CC 74 sets its parameter, not the standard map's FILTER");
    ml_arm = 1, ml_heard = 0;
    TDRUM->p[P_PAN] = 5;
    cc_in(0, 20, 99);
    check(ml_heard == 21u && TDRUM->p[P_PAN] == 5, "while learning: the CC goes to the UI (ml_heard), nothing is set");
    ml_arm = 0, ml_heard = 0;
    check(!ml_free_cc(1) && !ml_free_cc(64) && !ml_free_cc(0) && !ml_free_cc(121) && ml_free_cc(20) && ml_free_cc(74),
          "MIDI LEARN: mod wheel, sustain, bank select, the channel modes never learned");
    check(ml_id(P_DIST + 1u) == P_DIST && ml_id(ML_E0 + 7u) == P_E7 && ml_id(ML_E0 + 8u) == P_COUNT && ml_id(0) == P_COUNT,
          "MIDI LEARN: an entry's code -> its parameter (an unknown one: none)");
    memset((void *)ml_tab, 0, sizeof ml_tab);
    TDRUM->p[P_PAN] = TDRUM->p[P_DIST] = TDRUM->p[P_TFLT] = 0;
    mi_r = mi_w;
}

/* TEXTURE (GLO > TEXTURE): off it changes nothing; each source heard, bounded, no offset; WHEN; MOVE; TONE; a user slot
 * looped */
static void tx_set(int32_t src, int32_t lvl, int32_t tone, int32_t move, int32_t rate, int32_t when, int32_t duck)
{
    song.g[G_TXSRC] = (int16_t)src, song.g[G_TXLVL] = (int16_t)lvl, song.g[G_TXTONE] = (int16_t)tone;
    song.g[G_TXMOVE] = (int16_t)move, song.g[G_TXRATE] = (int16_t)rate, song.g[G_TXWHEN] = (int16_t)when;
    song.g[G_TXDUCK] = (int16_t)duck;
}
static void t_texture(void)
{
    uint32_t s, i, ok = 1, b;
    double r, m;
    int32_t pk;
    setup();
    tx_set(0, 64, 0, 0, 2, 1, 0);
    hit_every = 50;
    fork_dry(300);
    tx_set(1, 0, 0, 0, 2, 1, 0);                        /* VINYL at LEVEL 0: nothing either */
    render(300);
    check(!memcmp(shared_dry, out_l, 300u * CTL * 4u) && !tx.g, "TEXTURE OFF (or LEVEL 0): the mix bit for bit as without");
    hit_every = 0;
    for (s = 1; s <= 6u; s++) {                         /* each source alone, ALWAYS (stopped), LEVEL 64, no MOVE */
        setup();
        tx_set((int32_t)s, 64, 0, 0, 2, 1, 0);
        render(NB);
        r = rms(400u * CTL, NB * CTL);
        for (m = 0, pk = 0, i = 400u * CTL; i < NB * CTL; i++)
            m += out_l[i], pk = abs(out_l[i]) > pk ? abs(out_l[i]) : pk;
        m /= (double)(NB - 400u) * CTL;
        printf("  TEXTURE %-6s: rms %.0f (%.1f dBFS), mean %.0f, peak %d\n", N_TXSRC[s], r, 20 * log10(r / 32768), m, pk);
        ok &= r > 250 && r < 2500 && fabs(m) < 0.2 * r + 30 && pk < 30000;
    }
    check(ok, "TEXTURE: VINYL TAPE RAIN WIND RADIO ROOM heard (LEVEL 64: -40 .. -22 dBFS rms), bounded, no offset");
    setup();
    fork_dry(200);                                      /* (the same start, TEXTURE off) */
    tx_set(2, 127, 0, 0, 2, 0, 0);                      /* TAPE, WHEN PLAY, stopped: nothing; ALWAYS: heard */
    render(200);
    ok = !memcmp(shared_dry, out_l, 200u * CTL * 4u);
    song.g[G_TXWHEN] = 1;
    render(200);
    printf("  TEXTURE WHEN ALWAYS (stopped): rms %.0f\n", rms(100u * CTL, 200u * CTL));
    check(ok && rms(100u * CTL, 200u * CTL) > 500, "TEXTURE WHEN PLAY: nothing while stopped; ALWAYS: plays");
    {   /* MOVE 127 over 1BAR at 120 BPM (2 s): the level breathes */
        double lo = 1e9, hi = 0, q;
        setup();
        song.g[G_BPM] = 120;
        tx_set(2, 100, 0, 127, 0, 1, 0);
        render(NB);
        for (b = 0; b + 172u <= NB; b += 172u) {        /* (a quarter of a second) */
            q = rms(b * CTL, (b + 172u) * CTL);
            lo = q < lo ? q : lo, hi = q > hi ? q : hi;
        }
        printf("  TEXTURE MOVE 127: a quarter second's rms %.0f .. %.0f\n", lo, hi);
        check(hi > 2.0 * lo, "TEXTURE MOVE: the level breathes over RATE (x2 and more at MOVE 127)");
    }
    {   /* TONE: left a low-pass, right a high-pass (TAPE: its 8 kHz vs 200 Hz) */
        double hl[3], ll[3];
        int32_t T[3] = {-50, 0, 50};
        for (s = 0; s < 3u; s++) {
            setup();
            tx_set(2, 100, T[s], 0, 2, 1, 0);
            render(800);
            hl[s] = tonew(200u * CTL, 800u * CTL, 8000), ll[s] = tonew(200u * CTL, 800u * CTL, 150);
        }
        printf("  TEXTURE TONE -50 / 0 / +50: 8 kHz %.1f / 0 / %.1f dB, 150 Hz %.1f / 0 / %.1f dB\n", 10 * log10(hl[0] / hl[1]),
               10 * log10(hl[2] / hl[1]), 10 * log10(ll[0] / ll[1]), 10 * log10(ll[2] / ll[1]));
        check(hl[0] < 0.1 * hl[1] && ll[2] < 0.1 * ll[1], "TEXTURE TONE: left closes the highs, right the lows");
    }
    {   /* USR1: a zone of 2000 samples looped (it goes on), a slot without one silent */
        uint8_t *img = (uint8_t *)host_slots, *d = img + SMP_USER_DATA;
        smp_user_hdr_t *h = (smp_user_hdr_t *)img;
        memset(img, 0xFF, SMP_USER_SIZE);
        memset(h, 0, sizeof *h);
        h->magic = SMP_USER_MAGIC, h->version = 1, h->nz = 1;
        for (i = 0; i < 1000u; i++)
            d[i] = (i / 8u) & 1u ? 0xFFu : 0x77u;       /* up a while, then down */
        h->zone[0].off = 0, h->zone[0].n = 2000, h->zone[0].ls = 0, h->zone[0].le = 1999;
        h->zone[0].rate = 0x8000u, h->zone[0].root16 = 60 * 16, h->zone[0].lo = 0, h->zone[0].hi = 127;
        h->data_len = 1000;
        smp_user_scan(0);
        memset((uint8_t *)host_slots + SMP_USER_SIZE, 0xFF, 512);
        smp_user_scan(1);
        setup();
        tx_set(TX_USR1, 100, 0, 0, 2, 1, 0);
        render(NB);
        r = rms((NB - 300u) * CTL, NB * CTL);           /* (2000 samples at half rate: ~0.09 s; long after) */
        setup();
        tx_set(TX_USR1 + 1, 100, 0, 0, 2, 1, 0);
        render(300);
        printf("  TEXTURE USR1 looped: rms %.0f after 1.4 s; USR2 (empty) %.0f\n", r, rms(0, 300u * CTL));
        check(r > 300 && rms(0, 300u * CTL) < 30, "TEXTURE USR1: a user slot's sample looped; an empty slot silent");
    }
    tx_set(0, 64, 0, 40, 2, 0, 0);
    render(20);
}

/* 2.6, clicks: the track's FILTER and the master FILT engaging their low-pass start where the signal is (fx.c djf:
 * the state seeded) */
static double d2_of(uint32_t a, uint32_t b)
{
    double m = 0, d;
    uint32_t i;
    for (i = a + 2u; i < b; i++)
        if ((d = fabs((double)out_l[i] - 2.0 * out_l[i - 1] + out_l[i - 2])) > m)
            m = d;
    return m;
}
/* 2.6: the master limiter looks one block ahead: below LIM_T the mix comes out exactly, CTL samples late; a jump to
 * 6x is already turned down when it comes out (never above LIM_T, so never into the tanh knee hard), and the gain glides
 * (no step from one sample to the next bigger than the signal's own); it comes back to 1 */
static void t_limiter(void)
{
    static lim_t s;
    static int32_t l[CTL], r[CTL], hist[CTL * 2500];
    uint32_t b, i, nh = 0, exact = 1;
    int32_t pk = 0, pre = 0;
    memset(&s, 0, sizeof s), s.env = LIM_T, s.g = 32768;
    for (b = 0; b < 2500u; b++) {
        for (i = 0; i < CTL; i++) {
            uint32_t k = b * CTL + i;
            int32_t x = (int32_t)(10000.0 * sin(k * 0.05));
            if (b >= 100u && b < 140u)
                x *= 10;                                /* 100000: 5.5x LIM_T */
            l[i] = x, r[i] = -x;
            hist[nh++] = x;
        }
        lim_block(&s, l, r, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t k = b * CTL + i;
            int32_t want = k >= CTL ? hist[k - CTL] : 0, a = l[i] < 0 ? -l[i] : l[i];
            if (b < 99u && (l[i] != want || r[i] != -want))
                exact = 0;
            if (a > pk)
                pk = a;
            if (b == 100u && i == CTL - 1u)
                pre = a;                                /* (the last sample before the loud ones come out) */
        }
    }
    printf("  limiter: peak out %d (LIM_T %d), before the jump %d, gain at the end %d\n", pk, LIM_T, pre, s.g);
    check(exact && pk <= LIM_T + 8 && s.g == 32768,
          "master limiter: below LIM_T exact (one block late); a 5.5x jump never above LIM_T; back to 1 after");
}
/* 2.6 (a whistle on loud basses): a held bass over LIM_T, 41 Hz (a period of 34 blocks): once it has settled, one
 * gain for the whole note (the hold), and each sample exactly x * g >> 15 (no 12-bit cut) */
static void t_limiter_bass(void)
{
    static lim_t s;
    static int32_t l[CTL], r[CTL], hist[CTL * 600];
    uint32_t b, i, nh = 0, exact = 1, gmin = 32768, gmax = 0;
    int32_t g0;
    memset(&s, 0, sizeof s), s.env = LIM_T, s.g = 32768;
    for (b = 0; b < 600u; b++) {
        for (i = 0; i < CTL; i++) {
            uint32_t k = b * CTL + i;
            int32_t x = (int32_t)(36000.0 * sin(k * 2.0 * 3.14159265358979 * 41.0 / FS) + 377.0 * sin(k * 0.9));
            l[i] = x, r[i] = x;
            hist[nh++] = x;
        }
        g0 = s.g;
        lim_block(&s, l, r, CTL);
        if (b >= 200u) {
            if ((uint32_t)s.g < gmin) gmin = (uint32_t)s.g;
            if ((uint32_t)s.g > gmax) gmax = (uint32_t)s.g;
            for (i = 0; i < CTL && g0 == s.g; i++) {   /* (a block at one gain) */
                int32_t x = hist[b * CTL + i - CTL];
                if (l[i] != (int32_t)(((int64_t)x * s.g) >> 15))
                    exact = 0;
            }
        }
    }
    printf("  limiter, held bass 2x LIM_T: gain %u..%u over 400 blocks\n", gmin, gmax);
    check(gmax - gmin <= 4u && gmin < 32768u && exact,
          "master limiter: a held loud bass keeps its gain (within 0.01 %: the hold), samples at full precision");
}
/* 2.6, after Felucca 1.5.1: REV THROW / ECHO THROW. Every send at 0: the tail after a short note is the dry release
 * only; with the THROW held, the reverb (the echo) rings on after it; the song's sends and FDBK never change */
static void t_throw(void)
{
    double dry, rv, ec;
    int16_t fb0 = song.g[G_DFDBK];
    setup();
    song.g[G_DFDBK] = 0;
    thr_req = 0, thr_rev = thr_dly = 0;
    trk_note_on(TDRUM, 38, 120), render(460);
    dry = rms(300u * CTL, 400u * CTL);                 /* (the drum sends are 0: what rings after the snare is theirs) */
    setup();
    song.g[G_DFDBK] = 0;
    thr_req = 1;
    trk_note_on(TDRUM, 38, 120), render(460);
    rv = rms(300u * CTL, 400u * CTL);
    setup();
    song.g[G_DFDBK] = 0;
    thr_req = 2;
    trk_note_on(TDRUM, 38, 120), render(460);
    ec = rms(300u * CTL, 400u * CTL);
    thr_req = 0;
    render(100);
    printf("  THROW: tail rms dry %.0f, REV THROW %.0f, ECHO THROW %.0f; the glide back %d %d\n", dry, rv, ec, thr_rev, thr_dly);
    check(rv > 5 * dry + 50 && ec > 5 * dry + 50 && !thr_rev && !thr_dly && song.g[G_DRREV] == 0 && song.g[G_DRDLY] == 0 &&
          song.g[G_DFDBK] == 0, "REV / ECHO THROW: the drum sends full while held (a tail rings), back after; nothing changed");
    song.g[G_DFDBK] = fb0;
}
static void t_clicks(void)
{
    double base, lp, ms;
    setup();
    hit_every = 50;
    render(360);
    base = d2_of(260u * CTL, 360u * CTL);
    TDRUM->p[P_TFLT] = -1;
    render(5);
    lp = d2_of(0, 5u * CTL);
    song.g[G_FILT] = -1;
    render(5);
    ms = d2_of(0, 5u * CTL);
    TDRUM->p[P_TFLT] = 0, song.g[G_FILT] = 0;
    hit_every = 0;
    render(200);
    printf("  low-pass engaged under the drums: track FILTER %.0f, master FILT %.0f (the drums' own %.0f)\n", lp, ms, base);
    check(lp < 1.5 * base + 100 && ms < 1.5 * base + 100, "track FILTER / master FILT engaging their low-pass: no click");
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    t_insert();
    t_mutes();
    t_learn();
    t_texture();
    t_clicks();
    t_limiter();
    t_limiter_bass();
    t_throw();
    printf("fx251: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
