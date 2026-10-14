/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual sequencer, scene loader and mixer together: a drum song of two sections. */
#define FELUCCA_DRUMMER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/arranger_scene.c"

static void capture(uint32_t slot)             /* (proj_capture: with the nudges and locks, every slot free) */
{
    proj_capture(&proj_slot[slot]);
}

int main(int argc, char **argv)
{
    uint32_t k, frame, at_change = 0, at_stop = 0;
    uint32_t hits = 0, age = 0;
    int32_t out[CTL * 2];
    uint64_t energy = 0;
    FILE *f = argc > 1 ? fopen(argv[1], "wb") : 0;
    const uint32_t frames = (FS * 9u / CTL) * CTL;
    host_tracks_init();
    TDRUM->p[P_SLEN] = 16;
    TDRUM->p[P_SDIV] = 2;
    for (k = 0; k < 16; k++) {                 /* A: hats, kick on 1 and 9, snare on 5 and 13 */
        dstep_set(&TDRUM->dstep[k], 4, k % 2u ? LV_SOFT : LV_NORM, 0);
        if (k % 8u == 0u) dstep_set(&TDRUM->dstep[k], 0, LV_NORM, 0);
        if (k % 8u == 4u) dstep_set(&TDRUM->dstep[k], 2, LV_NORM, 0);
    }
    capture(0);
    dstep_set(&TDRUM->dstep[0], 3, LV_HARD, 0);   /* B: a clap on 1 too, another kit, the drums lower */
    song.g[G_DRLVL] = 80;
    TDRUM->p[P_E0] = 4;                       /* drum kit is saved with the section */
    capture(1);
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);   /* (what plays comes from the sections) */
    arr_defaults(&arrangement);
    arrangement.count = 2;
    arrangement.entry[0] = (arr_entry_t){0, 2};
    arrangement.entry[1] = (arr_entry_t){1, 2};
    arrangement_enabled = 1;
    transport_req = 1;
    if (f) wav_hdr(f, frames);
    for (frame = 0; frame < frames; frame += CTL) {
        mix_block(out, CTL);
        if (song.playing && arrangement_clock.index == 1u && !at_change) at_change = frame;
        if (!song.playing && frame && !at_stop) at_stop = frame;
        if (drums.age != age) { hits += drums.age - age; age = drums.age; }
        for (k = 0; k < CTL; k++) {
            energy += (uint64_t)(out[k*2] < 0 ? -(int64_t)out[k*2] : out[k*2]);
            if (f) wav_put(f, out[k*2], out[k*2+1]);
        }
    }
    if (f) fclose(f);
    assert(hits == 82u && energy > 1000000u);   /* 4 bars at 120: A 2 x 20 hits, B 2 x 21 */
    assert(at_change >= FS*4u && at_change < FS*4u + CTL);
    assert(at_stop >= FS*8u && at_stop < FS*8u + CTL);
    assert(!dstep_mask(&TDRUM->dstep[0]) && song.g[G_DRLVL] == 80);   /* STOP: the loop kept before PLAY back */
    assert(drum_kit() == 4);
    /* A missing later scene must prevent even the first scene from starting. */
    arrangement.entry[1].scene = 3;
    transport_req = 1; events_block(CTL);
    assert(!song.playing && arrangement_clock.error);
    {   /* LIVE: A as the loop, SONG REC armed; B asked for in bar 2 starts on bar 3; stop in B's 3rd bar */
        uint32_t bar = 4u * 60u * FS / (uint32_t)song.g[G_BPM], t, jumped = 0;
        arrangement.entry[1].scene = 1;
        arrangement_enabled = 0;
        proj_apply(&proj_slot[0], 1);
        live_sec = 0;
        srec = 1;
        transport_req = 1;
        for (t = 0; t < bar * 5u - bar / 2u; t += CTL) {
            if (t >= bar + bar / 2u && t < bar + bar / 2u + CTL) live_req = 1;
            mix_block(out, CTL);
            if (live_sec == 1 && !jumped) jumped = t;
        }
        transport_req = 2;
        mix_block(out, CTL);
        assert(jumped >= 2u * bar && jumped < 2u * bar + CTL);      /* exactly on the bar */
        assert(dstep_has(&TDRUM->dstep[0], 3) && srec == 0 && srec_done == 2u);
        assert(arrangement.count == 2u && arrangement.entry[0].scene == 0 && arrangement.entry[0].bars == 2u &&
               arrangement.entry[1].scene == 1 && arrangement.entry[1].bars == 3u);
        printf("song live: B on the bar (%u), SONG REC -> A 2 bars, B 3 bars PASS\n", jumped);
    }
    printf("song audio: two drum sections (%u hits), section at %u, stop at %u, no held sequencer notes PASS\n", hits, at_change, at_stop);
    return 0;
}
