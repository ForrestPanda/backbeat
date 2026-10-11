/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-arranger: a pedalboard's MIDI drives the sections (firmware/src/pedal.c) through the real
 * sequencer. Bytes go in through the TRS parser (midi_uart.c), as on the jack. Sections A, B, D are
 * stored, C is empty. Checks: a section from stopped starts the loop with it; a section while playing
 * lands exactly on the next bar; next / previous skip the empty section and wrap; empty sections,
 * other channels and program changes are ignored; the pedal channel can be off or omni; tap tempo;
 * stop. Milestone 2: a section asked for makes the rest of the bar a fill (SLOOP's fill_now), CC 27
 * a whole fill bar first, CC 21 fills the next bar, CC 22 plays the outro once and stops on the bar
 * (or stops on the next bar with no outro), and a section asked for cancels an ending. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? i : 0; }
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/arranger_scene.c"

static void capture(uint32_t slot)             /* (proj_capture: as song_audio_test.c in 2.6) */
{
    proj_capture(&proj_slot[slot]);
}

static void cc(uint32_t ch, uint32_t num, uint32_t val)   /* ch 1..16, as on the wire */
{
    um_byte(0xB0u | (ch - 1u)); um_byte(num); um_byte(val);
}
static void pc(uint32_t ch, uint32_t n) { um_byte(0xC0u | (ch - 1u)); um_byte(n); }

static int32_t out[CTL * 2];
static uint32_t now;                     /* samples rendered */
static uint32_t t0;                      /* when the loop last started (its bar 0) */
static uint32_t jumped;                  /* when live_sec last changed */
static uint32_t stopped;                 /* when the transport last stopped */

static void advance(uint32_t samples)    /* render, watching the transport and the section */
{
    uint32_t end = now + samples;
    while (now < end) {
        int8_t sec = live_sec;
        uint8_t playing = song.playing;
        mix_block(out, CTL);
        if (!playing && song.playing) t0 = now;
        if (sec != live_sec) jumped = now;
        if (playing && !song.playing) stopped = now;
        now += CTL;
    }
}
static uint32_t since_start(void) { return jumped - t0; }

int main(void)
{
    uint32_t i, k, bar;
    setvbuf(stdout, 0, _IONBF, 0);
    host_tracks_init();
    host_preset(&trk[0], 0, 4);
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = 16;
        trk[i].p[P_SDIV] = 2;
        trk[i].p[P_AMODE] = 0;
        trk[i].p[P_LEVEL] = 90;
    }
    for (k = 0; k < 16; k++) {
        uint8_t bass[] = {36}, drum[] = {42, (uint8_t)(k % 8u == 4u ? 38 : 36)};
        if (!(k % 4u)) put_step(&trk[0], k, 1, bass, ST_NOTE, 0);
        put_step(TDRUM, k, k % 4u ? 1 : 2, drum, ST_NOTE, 0);
    }
    capture(0);                                           /* A: bass on 36 */
    for (k = 0; k < 16; k++)
        for (i = 0; i < trk[0].step[k].n; i++) trk[0].step[k].note[i] += 5;
    capture(1);                                           /* B: bass on 41 */
    for (k = 0; k < 16; k++)
        for (i = 0; i < trk[0].step[k].n; i++) trk[0].step[k].note[i] += 5;
    TDRUM->p[P_E0] = 4;
    capture(3);                                           /* D: bass on 46, kit 4; C stays empty */
    assert(arrangement_ready() == 0xBu);
    bar = 4u * 60u * FS / (uint32_t)song.g[G_BPM];
    assert(pedal.ch == 16u);

    /* nothing loaded, stopped: NEXT starts A */
    assert(!song.playing && live_sec == -1);
    cc(16, 24, 0);
    advance(CTL * 3u);
    assert(song.playing && live_sec == 0 && live_req == -1 && trk[0].step[0].note[0] == 36);
    assert(jumped == t0);                                 /* applied on bar 0 of the start */
    printf("pedal: NEXT from nothing starts A at %u PASS\n", t0);

    /* STOP */
    cc(16, 23, 0);
    advance(CTL * 2u);
    assert(!song.playing);

    /* stopped: section B starts the loop with B */
    cc(16, 20, 1);
    advance(CTL * 3u);
    assert(song.playing && live_sec == 1 && trk[0].step[0].note[0] == 41 && jumped == t0);
    printf("pedal: section B from stopped starts with B PASS\n");

    /* playing: A asked for in bar 1 (1.5 bars in) lands exactly on bar 2 */
    advance(bar + bar / 2u - (now - t0));
    cc(16, 20, 0);
    advance(CTL);
    assert(live_req == 0 && live_sec == 1);              /* pending, not yet */
    advance(bar);
    assert(live_sec == 0 && live_req == -1 && trk[0].step[0].note[0] == 36);
    assert(since_start() >= 2u * bar && since_start() < 2u * bar + CTL);
    printf("pedal: A on the bar (%u) PASS\n", since_start());

    /* an empty section, another channel, a program change: ignored */
    cc(16, 20, 2); advance(CTL);
    assert(live_req == -1);
    cc(5, 20, 1); advance(CTL);
    assert(live_req == -1);
    pc(16, 3); advance(CTL);
    assert(live_req == -1 && song.playing && live_sec == 0);
    cc(16, 20, 9); advance(CTL);
    assert(live_req == -1);
    printf("pedal: empty C, channel 5, PC, section 9 ignored PASS\n");

    /* NEXT: A -> B -> D (C skipped) -> A (wrap); PREV: A -> D */
    cc(16, 24, 0); advance(bar);
    assert(live_sec == 1 && since_start() % bar < CTL);
    cc(16, 24, 0); advance(bar);
    assert(live_sec == 3 && trk[0].step[0].note[0] == 46 && drum_kit() == 4 && since_start() % bar < CTL);
    cc(16, 24, 0); advance(bar);
    assert(live_sec == 0 && since_start() % bar < CTL);
    cc(16, 25, 0); advance(bar);
    assert(live_sec == 3 && since_start() % bar < CTL);
    /* two presses in one bar: the last one counts (a toggle switch sending twice: the second is a no-op) */
    cc(16, 25, 0); cc(16, 20, 1); advance(bar);
    assert(live_sec == 1);
    printf("pedal: NEXT skips C and wraps, PREV wraps, last request wins PASS\n");

    /* the channel: off ignores all, omni takes any */
    pedal.ch = 0;
    cc(16, 20, 0); advance(bar);
    assert(live_sec == 1);
    pedal.ch = 17;
    cc(3, 20, 0); advance(bar);
    assert(live_sec == 0);
    pedal.ch = 16;
    printf("pedal: channel off / omni PASS\n");

    /* tap tempo: four taps at 120 BPM */
    for (i = 0; i < 4u; i++) {
        cc(16, 26, 127);
        advance(60u * FS / 120u);
    }
    assert(song.g[G_BPM] >= 119 && song.g[G_BPM] <= 121);
    /* a pause of more than 2 s, then two taps at 100 BPM: a new count */
    advance(3u * FS);
    cc(16, 26, 0); advance(60u * FS / 100u);
    cc(16, 26, 0); advance(CTL);
    assert(song.g[G_BPM] >= 99 && song.g[G_BPM] <= 101);
    printf("pedal: tap tempo 120 then 100 PASS\n");

    /* STOP, then STOP again (a toggle): harmless; section while stopped starts again */
    cc(16, 23, 0); advance(CTL * 2u);
    assert(!song.playing);
    cc(16, 23, 0); advance(CTL * 2u);
    assert(!song.playing);
    cc(16, 20, 3); advance(CTL * 3u);
    assert(song.playing && live_sec == 3 && jumped == t0);
    cc(16, 23, 0); advance(CTL * 2u);
    for (i = 0; i < NPART; i++) {
        assert(trk[i].seq_n == 0);
        for (k = 0; k < NVOICE; k++) assert(!trk[i].v[k].gate);
    }
    printf("pedal: stop, stop again, restart with D, no held notes PASS\n");

    /* ---- milestone 2: fills and endings ---- */
    bar = 4u * 60u * FS / (uint32_t)song.g[G_BPM];     /* (the tap test left 100 BPM) */
    /* a section asked for mid-bar: the rest of the bar is a fill, the new section's bar 0 is not */
    cc(16, 20, 0); advance(CTL * 3u);                     /* stopped: starts A, no fill */
    assert(song.playing && live_sec == 0 && !fill_now && !pedal_fill);
    advance(bar / 2u - (now - t0) % bar);                  /* half a bar in */
    assert(!fill_now);
    cc(16, 20, 1); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(pedal_fill && fill_now && live_sec == 0);
    advance(bar / 2u);                                     /* past the bar: B started, the fill over */
    assert(live_sec == 1 && !pedal_fill && !fill_now && since_start() % bar < CTL);
    printf("pedal: section with the rest of the bar a fill, none on the new section's bar 0 PASS\n");

    /* CC 27: the rest of this bar and the whole next bar a fill, the section on the bar after */
    {
        uint32_t jB = jumped;
        advance(bar / 4u);
        cc(16, 27, 3); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
        assert(fill_now && pedal_wait && live_sec == 1);
        advance(bar);                                      /* the next bar: still B, still a fill */
        assert(live_sec == 1 && fill_now && !pedal_wait && live_req == 3);
        advance(bar);                                      /* the bar after: D, no fill */
        assert(live_sec == 3 && !fill_now && jumped - jB >= 2u * bar && jumped - jB < 2u * bar + CTL);
    }
    printf("pedal: section with a whole fill bar first PASS\n");

    /* CC 21: the next bar is a fill, the section stays */
    advance(bar / 4u);
    cc(16, 21, 0); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(fill_arm && !fill_now);
    advance(bar - (now - t0) % bar + CTL);                 /* into the next bar */
    assert(fill_now && !fill_arm && live_sec == 3);
    advance(bar);                                          /* over */
    assert(!fill_now && live_sec == 3);
    printf("pedal: fill the next bar PASS\n");

    /* CC 22 with an outro: the fill, A once (1 bar), stop on the bar */
    advance(bar / 3u);
    cc(16, 22, 0); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(fill_now && live_req == 0 && pedal_end_req == 1);
    advance(bar);
    assert(live_sec == 0 && song.playing && !fill_now && pedal_end_bars == 1);
    advance(bar);
    assert(!song.playing && stopped - jumped >= bar && stopped - jumped < bar + CTL);
    for (i = 0; i < NPART; i++) {
        assert(trk[i].seq_n == 0);
        for (k = 0; k < NVOICE; k++) assert(!trk[i].v[k].gate);
    }
    printf("pedal: ending with the outro A: fill, A once, stop on the bar (%u) PASS\n", stopped - jumped);

    /* CC 22 with no outro: the fill, stop on the next bar */
    cc(16, 20, 1); advance(CTL * 3u);
    assert(song.playing && live_sec == 1);
    advance(bar + bar / 2u - (now - t0) % bar);
    {
        uint32_t at = now;
        cc(16, 22, 9); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
        assert(fill_now && live_req == -1 && pedal_end_bars == 1);
        advance(bar);
        assert(!song.playing && stopped - at >= bar / 2u - CTL && stopped - at < bar / 2u + CTL);
    }
    printf("pedal: ending with no outro: fill, stop on the next bar PASS\n");

    /* a section asked for after an ending cancels it */
    cc(16, 20, 0); advance(CTL * 3u);
    advance(bar / 2u - (now - t0) % bar);
    cc(16, 22, 3); cc(16, 20, 1); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(live_req == 1 && pedal_end_req == 0);
    advance(bar * 3u);
    assert(song.playing && live_sec == 1 && !pedal_end_bars);
    /* ..and an ending after a section wins over it */
    cc(16, 20, 3); cc(16, 22, 9); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(live_req == -1 && pedal_end_bars == 1);
    advance(bar);
    assert(!song.playing);
    printf("pedal: a section cancels an ending, an ending cancels a section PASS\n");
    return 0;
}
