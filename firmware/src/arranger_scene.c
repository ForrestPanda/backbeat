/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared scene application for firmware and host audio integration tests. */
static uint32_t arrangement_ready(void)
{
    uint32_t i, ready = 0;
    for (i = 0; i < ARR_SCENES; i++)
        if (proj_ok(&proj_slot[i])) ready |= 1u << i;
    return ready;
}

/* the bars a section's loop takes (the drum pattern: ceil(LEN x step / bar), at least 1; drums only: the other
 * tracks are not played, so their lengths do not count): the quick chain (seq.c chain_*) plays each section this long */
static uint32_t section_bars(uint32_t s)
{
    const project_t *p = &proj_slot[s & 3u];
    uint32_t len, u, bars;
    if (!proj_ok(p))
        return 1;
    len = (uint32_t)clamp(p->t.p[P_SLEN], 1, NSTEP), u = div_units((uint32_t)p->t.p[P_SDIV] % NDIV_STEP);
    bars = (len * u + 4u * BEAT_U - 1u) / (4u * BEAT_U);   /* (64 x 8 beats fits 32 bits) */
    if (bars < 1u)
        bars = 1;
    return bars > 64u ? 64u : bars;
}

/* Called only at an audio-block boundary, after validation of all slots at
 * start. The song keeps one tempo and global FX across every section (only the drum
 * level and reverb come from it). */
static void arrangement_apply(uint32_t scene)
{
    seq_release(TDRUM);
    proj_apply(&proj_slot[scene], 0);
    sync_reload = 1;
    ui.force = 1;
}

/* ---- song mode keeps the loop you made: PLAY in song mode puts it aside (each section then plays
 * over the tracks), STOP (or the song's end) brings it back */
static project_t song_keep __attribute__((section(".pool")));
static uint8_t song_kept;
static void song_backup(void)                  /* (audio ISR: seq_start) */
{
    proj_capture(&song_keep);
    song_kept = 1;
}
static void song_restore(void)                 /* (audio ISR: seq_stop) */
{
    if (!song_kept)
        return;
    song_kept = 0;
    proj_apply(&song_keep, 1);
    sync_reload = 1;
    ui.force = 1;
}
#include "sng.c"                                   /* fm1-drummer: songs (their sections into the drum track) */
