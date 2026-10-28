/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio gate (fx.c mix_block, 2026-10-27): stopped and silent for 3 s, the mix is skipped and the block zeroed.
 * Checks: it closes after 3 s from rest; a drum hit opens it in that block and it closes again once the hit and
 * the FX have rung out (the time printed, with the default sends and with the reverb at its largest); a key
 * (fm1_in.notes) and a note over the TRS jack open it; the transport keeps it open for the whole groove and it
 * closes after the stop; TEXTURE turned on while closed opens it and sounds within a block, and it closes again
 * after it is turned off; DUST (its bed turns with PLAY only) leaves it closed while stopped; a closed block is
 * all zeros. */
#define FELUCCA_DRUMMER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/sng.c"                       /* (the songs, as pedal_test.c) */

static int32_t out[CTL * 2];
static uint32_t nblk;                               /* blocks rendered */

static uint32_t block_peak(void)
{
    uint32_t i, pk = 0;
    for (i = 0; i < 2u * CTL; i++) {
        uint32_t a = (uint32_t)(out[i] < 0 ? -out[i] : out[i]);
        if (a > pk)
            pk = a;
    }
    return pk;
}
static void render(uint32_t blocks)
{
    while (blocks--) {
        mix_block(out, CTL);
        nblk++;
    }
}
/* blocks until the gate closes, or 0 if it stays open for max_s */
static uint32_t until_closed(uint32_t max_s)
{
    uint32_t k, lim = max_s * FS / CTL;
    for (k = 1; k <= lim; k++) {
        mix_block(out, CTL);
        nblk++;
        if (audio_gated) {
            assert(block_peak() == 0);
            return k;
        }
    }
    return 0;
}
/* blocks until the output is heard (|sample| > 64), or 0 if it stays silent for max_s */
static uint32_t until_heard(uint32_t max_s)
{
    uint32_t k, lim = max_s * FS / CTL;
    for (k = 1; k <= lim; k++) {
        mix_block(out, CTL);
        nblk++;
        if (block_peak() > 64u)
            return k;
    }
    return 0;
}
static double secs(uint32_t blocks) { return (double)blocks * CTL / FS; }

int main(void)
{
    uint32_t k;
    setvbuf(stdout, 0, _IONBF, 0);
    host_tracks_init();
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SLEN] = 16;
        trk[k].p[P_SDIV] = 2;
        trk[k].p[P_LEVEL] = 90;
    }

    /* from rest: closes after AUD_QUIET_S */
    k = until_closed(10);
    printf("gate: from rest, closed after %.2f s\n", secs(k));
    assert(k >= AUD_QUIET_BLOCKS && k <= AUD_QUIET_BLOCKS + 2u);
    render(100);
    assert(audio_gated && block_peak() == 0);

    /* a drum hit opens it in that block (the sound a block later: the limiter's look-ahead); closed again once
     * it has rung out */
    drum_on(36, 100);
    mix_block(out, CTL);
    assert(!audio_gated);
    k = until_heard(1);
    assert(k && k <= 2u);
    k = until_closed(30);
    printf("gate: a kick with the default sends, closed %.2f s after it\n", secs(k));
    assert(k);

    /* the reverb at its largest, the drums sent to it in full */
    song.g[G_RSIZE] = 127;
    song.g[G_RDAMP] = 0;
    song.g[G_DRREV] = 127;
    song.g[G_DRDLY] = 127;
    drum_on(49, 127);                               /* a crash */
    mix_block(out, CTL);
    assert(!audio_gated);
    k = until_closed(60);
    printf("gate: a crash into the largest reverb and the echo, closed %.2f s after it\n", secs(k));
    assert(k);
    song.g[G_RSIZE] = GP[G_RSIZE].def;
    song.g[G_RDAMP] = GP[G_RDAMP].def;
    song.g[G_DRREV] = GP[G_DRREV].def;
    song.g[G_DRDLY] = GP[G_DRDLY].def;
    render(10);
    assert(audio_gated);

    /* a key on the panel (the lowest white key: the kick) */
    fm1_in.notes = 1u;
    mix_block(out, CTL);
    assert(!audio_gated);
    k = until_heard(1);
    assert(k && k <= 2u);
    fm1_in.notes = 0;
    k = until_closed(30);
    printf("gate: a key, closed %.2f s after it\n", secs(k));
    assert(k);

    /* a note over the TRS jack on the drum channel */
    um_byte(0x90u | (uint8_t)(song.g[G_DRCH] - 1)); um_byte(38); um_byte(100);
    mix_block(out, CTL);
    assert(!audio_gated);
    k = until_heard(1);
    assert(k && k <= 2u);
    k = until_closed(30);
    printf("gate: a TRS note, closed %.2f s after it\n", secs(k));
    assert(k);

    /* the groove: open for the whole run, closed after the stop */
    host_groove();
    transport_req = 1;
    for (k = 0; k < 4u * FS / CTL; k++) {           /* 4 s = 2 bars at 120 */
        mix_block(out, CTL);
        assert(!audio_gated);
    }
    assert(song.playing);
    transport_req = 2;
    k = until_closed(30);
    printf("gate: the groove stopped, closed %.2f s after the stop\n", secs(k));
    assert(k && !song.playing);

    /* TEXTURE turned on while closed: heard within a block, closed again after it is turned off */
    song.g[G_TXSRC] = TX_RAIN;
    song.g[G_TXWHEN] = 1;
    song.g[G_TXLVL] = 100;
    mix_block(out, CTL);
    assert(!audio_gated);
    k = until_heard(2);
    printf("gate: TEXTURE on, heard after %.3f s\n", secs(k));
    assert(k);
    render(FS / CTL);
    assert(!audio_gated);
    song.g[G_TXSRC] = TX_OFF;
    k = until_closed(30);
    printf("gate: TEXTURE off, closed %.2f s after\n", secs(k));
    assert(k);

    /* DUST's hiss and crackle turn with PLAY only (a stopped SLOOP is silent): on while stopped, the gate stays
     * closed; with the groove they are heard, and the gate closes again after the stop */
    song.g[G_DUST] = 60;
    render(FS / CTL);
    assert(audio_gated);
    transport_req = 1;
    k = until_heard(1);
    printf("gate: DUST on and PLAY, heard after %.3f s\n", secs(k));
    assert(k);
    render(2u * FS / CTL);
    transport_req = 2;
    k = until_closed(30);
    printf("gate: DUST on, the groove stopped, closed %.2f s after the stop\n", secs(k));
    assert(k);
    song.g[G_DUST] = 0;

    /* the pedal's count-in keeps it open (CC 26 tap is tempo; a section through the pedal channel starts the loop) */
    printf("gate: %u blocks rendered, %u skipped\n", nblk, audio_gate_blocks);
    printf("audio gate OK\n");
    return 0;
}
