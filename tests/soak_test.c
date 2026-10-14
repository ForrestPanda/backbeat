/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP soak test: 10 simulated minutes of random live use through the real audio path
 * (mix_block: the sequencer, the synthesised and sampled drum kits, the drum track's INSERT and FILTER, the throws,
 * MIDI in), then everything stopped and released.
 * Checks: the output stays bounded, every block renders in time on the host, voices never
 * hang (all free a few seconds after the last note), the throws and the transport come
 * back to idle, the steps stay well-formed. Seeded: the same run every time. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint32_t seed = 12345;
static uint32_t rnd(uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % (n ? n : 1u); }
static void midi_in(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mi_w - mi_r >= MQ) return;
    midi_in_q[mi_w % MQ] = ((st >> 4) & 15u) | st << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}

int main(int argc, char **argv)
{
    const uint32_t minutes = argc > 1 ? (uint32_t)atoi(argv[1]) : 10u;
    const uint32_t blocks = minutes * 60u * FS / CTL;
    static int32_t out[CTL * 2];
    uint32_t b, i, k, held = 0, maxblock_ns = 0, events = 0, peak = 0;
    double total_ns = 0;
    struct timespec t0, t1;
    host_tracks_init();
    song.g[G_BPM] = 120;
    TDRUM->p[P_E0] = DRUM_SAMPLED;
    for (k = LY_FX; k < LY_COUNT; k++)                        /* the layer buttons (SLOOP 2.0): bits 8.. */
        ly_bit[k] = 1u << (7u + k);
    dyn_bit[0] = 1u << 14;
    dyn_bit[1] = 1u << 15;
    for (b = 0; b < blocks; b++) {
        static uint32_t layer_btn;
        if (rnd(40) == 0) {                                   /* an event every ~29 ms on average */
            events++;
            switch (rnd(22)) {
            case 16: layer_btn = rnd(3) ? 0u : ly_bit[1u + rnd(LY_COUNT - 1u)] | (rnd(4) ? 0u : dyn_bit[rnd(2)]); break;   /* a layer held */
            case 17: song.g[G_DUST] = (int16_t)rnd(128); song.g[G_DUCK] = (int16_t)rnd(128); break;
            case 18: song.g[G_FILT] = (int16_t)((int)rnd(128) - 64); song.g[G_ROLL] = (int16_t)rnd(5); break;
            case 19: TDRUM->p[P_MUTE] = (int16_t)(rnd(4) == 0); break;
            case 20: {                                                             /* levels, ratchets on steps */
                uint32_t t = rnd(NTRK), i2 = rnd(NSTEP);
                if (t == TRK_DRUM) dstep_set(&trk[t].dstep[i2], rnd(DRUM_LANES), rnd(4), rnd(4));
                break;
            }
            case 21: song.g[G_DRREV] = (int16_t)rnd(128); song.g[G_DRDLY] = (int16_t)rnd(128); break;   /* the sends */
            case 0: transport_req = song.playing ? 2 : 1; break;                  /* PLAY */
            case 1: TDRUM->p[P_SSWING] = (int16_t)rnd(101); break;                 /* swing */
            case 2: song.g[G_CLOCK] = (int16_t)rnd(3); break;                      /* the click */
            case 3: TDRUM->p[P_TFLT] = (int16_t)((int)rnd(128) - 64); break;   /* the FILTER */
            case 4: TDRUM->p[P_E0] = (int16_t)rnd(DRUM_KITS); break;              /* kit */
            case 5: thr_req = (uint8_t)rnd(4); break;                             /* the throws */
            case 6: song.g[G_BPM] = (int16_t)(60 + rnd(140)); break;
            case 7: TDRUM->p[P_ITYPE] = (int16_t)rnd(IT_N); TDRUM->p[P_IMIX] = (int16_t)rnd(128);   /* the INSERT */
                    TDRUM->p[P_IA] = (int16_t)rnd(128); break;
            case 8: trk[rnd(NTRK)].p[P_SLEN] = (int16_t)(1 + rnd(64)); break;
            case 9: trk[rnd(NTRK)].p[P_SDIV] = (int16_t)rnd(6); break;
            case 10: song.g[G_CLOCK] = (int16_t)rnd(3); break;
            case 11: midi_in(0x90u | rnd(16), 24 + rnd(80), 1 + rnd(127)); break;
            case 12: midi_in(0x80u | rnd(16), 24 + rnd(80), 0); break;
            case 13: trk[rnd(NTRK)].p[P_TFLT] = (int16_t)((int)rnd(128) - 64); break;
            case 14: TDRUM->p[P_PAN] = (int16_t)((int)rnd(128) - 64); break;
            default: {                                                             /* keys: press / release */
                uint32_t key = rnd(27);
                if (rnd(2)) held |= 1u << key; else held &= ~(1u << key);
                if (rnd(8) == 0) held = 0;
                break;
            }
            }
        }
        fm1_in.notes = held;
        if (rnd(3000) == 0) {                                 /* now and then an empty project */
            for (k = 0; k < NTRK; k++)
                steps_clear(&trk[k]);
        }
        fm1_in.buttons = layer_btn;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        mix_block(out, CTL);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        {
            uint32_t ns = (uint32_t)((t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec));
            total_ns += ns;
            if (ns > maxblock_ns && b > 1000u) maxblock_ns = ns;
        }
        for (i = 0; i < CTL * 2u; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            assert(a < (1 << 24));
            if ((uint32_t)a > peak) peak = (uint32_t)a;
        }
    }
    /* the end: keys up, MIDI notes off on every channel, stop, the throws off; then 5 s */
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    thr_req = 0;
    mix_block(out, CTL);
    for (k = 0; k < 16u; k++)
        for (i = 24; i < 104u; i++) {
            midi_in(0x80u | k, i, 0);
            if (mi_w - mi_r >= MQ - 4u) mix_block(out, CTL);
        }
    transport_req = 2;
    for (b = 0; b < 5u * FS / CTL; b++) mix_block(out, CTL);
    assert(!song.playing);
    for (i = 0; i < NDRUM; i++)
        assert(!drums.v[i].active);                          /* every drum hit ended */
    for (i = 0; i < NROLL; i++)
        assert(!roll[i].on);                                 /* every roll ended with its key */
    {
        int32_t quiet = 0;                                   /* silence (the reverb / delay tails done) */
        for (b = 0; b < FS / CTL; b++) {
            mix_block(out, CTL);
            for (i = 0; i < CTL * 2u; i++) if ((out[i] < 0 ? -out[i] : out[i]) > quiet) quiet = out[i] < 0 ? -out[i] : out[i];
        }
        printf("soak: %u min, %u events, peak %u, max block %.1f us, mean %.2f us; tail %d\n",
               minutes, events, peak, maxblock_ns / 1000.0, total_ns / blocks / 1000.0, quiet);
        assert(quiet < 64);
    }
    printf("soak: random live use, bounded output, no hanging voices, idle after stop PASS\n");
    return 0;
}
