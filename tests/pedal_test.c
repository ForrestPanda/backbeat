/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: a pedalboard's MIDI drives the sections (firmware/src/pedal.c) through the real
 * sequencer. Bytes go in through the TRS parser (midi_uart.c), as on the jack. Sections A, B, D are
 * stored, C is empty. Checks: a section from stopped starts the loop with it; a section while playing
 * lands exactly on the next bar; next / previous skip the empty section and wrap; empty sections,
 * other channels and program changes are ignored; the pedal channel can be off or omni; tap tempo;
 * stop. Milestone 2: a section asked for makes the rest of the bar a fill (SLOOP's fill_now), CC 27
 * a whole fill bar first, CC 21 fills the next bar, CC 22 plays the outro once and stops on the bar
 * (or stops on the next bar with no outro), and a section asked for cancels an ending. */
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
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = 16;
        trk[i].p[P_SDIV] = 2;
        trk[i].p[P_AMODE] = 0;
        trk[i].p[P_LEVEL] = 90;
    }
    TDRUM->p[P_PAN] = 36;                                 /* the section's marker (drums only: no bass track) */
    for (k = 0; k < 16; k++) {
        uint8_t drum[] = {42, (uint8_t)(k % 8u == 4u ? 38 : 36)};
        put_step(TDRUM, k, k % 4u ? 1 : 2, drum, ST_NOTE, 0);
    }
    capture(0);                                           /* A: PAN 36 */
    TDRUM->p[P_PAN] += 5;
    capture(1);                                           /* B: PAN 41 */
    TDRUM->p[P_PAN] += 5;
    TDRUM->p[P_E0] = 4;
    capture(3);                                           /* D: PAN 46, kit 4; C stays empty */
    assert(arrangement_ready() == 0xBu);
    bar = 4u * 60u * FS / (uint32_t)song.g[G_BPM];
    assert(pedal.ch == 16u);

    /* nothing loaded, stopped: NEXT starts A */
    assert(!song.playing && live_sec == -1);
    cc(16, 24, 0);
    advance(CTL * 3u);
    assert(song.playing && live_sec == 0 && live_req == -1 && TDRUM->p[P_PAN] == 36);
    assert(jumped == t0);                                 /* applied on bar 0 of the start */
    printf("pedal: NEXT from nothing starts A at %u PASS\n", t0);

    /* STOP */
    cc(16, 23, 0);
    advance(CTL * 2u);
    assert(!song.playing);

    /* stopped: section B starts the loop with B */
    cc(16, 20, 1);
    advance(CTL * 3u);
    assert(song.playing && live_sec == 1 && TDRUM->p[P_PAN] == 41 && jumped == t0);
    printf("pedal: section B from stopped starts with B PASS\n");

    /* playing: A asked for in bar 1 (1.5 bars in) lands exactly on bar 2 */
    advance(bar + bar / 2u - (now - t0));
    cc(16, 20, 0);
    advance(CTL);
    assert(live_req == 0 && live_sec == 1);              /* pending, not yet */
    advance(bar);
    assert(live_sec == 0 && live_req == -1 && TDRUM->p[P_PAN] == 36);
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
    assert(live_sec == 3 && TDRUM->p[P_PAN] == 46 && drum_kit() == 4 && since_start() % bar < CTL);
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
    printf("pedal: stop, stop again, restart with D PASS\n");

    /* ---- milestone 2: fills and endings ---- */
    bar = 4u * 60u * FS / (uint32_t)song.g[G_BPM];     /* (the tap test left 100 BPM) */
    /* a section asked for mid-bar: the rest of the bar is a fill, the new section's bar 0 is not */
    cc(16, 20, 0); advance(CTL * 3u);                     /* stopped: starts A, no fill */
    assert(song.playing && live_sec == 0 && !fill_now && !pedal_fill);
    advance(bar / 2u - (now - t0) % bar);                  /* half a bar in */
    assert(!fill_now);
    cc(16, 20, 1); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(!pedal_fill && !fill_now && live_sec == 0 && live_req == 1);   /* no fill of its own (2026-10-14) */
    advance(bar / 2u);                                     /* past the bar: B started */
    assert(live_sec == 1 && !pedal_fill && !fill_now && since_start() % bar < CTL);
    printf("pedal: section on the next bar of a 1-bar section, no fill PASS\n");

    /* CC 27: the section on the next bar, no fill (a fill is CC 21's) */
    {
        uint32_t jB = jumped;
        advance(bar / 4u);
        cc(16, 27, 3); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
        assert(!fill_now && !pedal_at_end && live_req == 3 && live_sec == 1);
        advance(bar);                                      /* the next bar: D, no fill */
        assert(live_sec == 3 && !fill_now && jumped - jB <= bar + CTL && (jumped - t0 + CTL / 2u) % bar < CTL);   /* (on
                                                            * the bar line, within the block) */
        advance(bar);                                      /* (the phase the tests below count on) */
    }
    printf("pedal: CC 27: the section on the next bar, no fill PASS\n");


    /* CC 21 in a 1-bar section (D): the fill from now, through the whole next bar (the next cycle), the section stays */
    advance(bar / 4u);
    cc(16, 21, 0); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(fill_now && pedal_fill_hold == 2u && !fill_arm);
    advance(bar - (now - t0) % bar + CTL);                 /* into the next bar: still the fill */
    assert(fill_now && pedal_fill_hold == 1u && live_sec == 3);
    advance(bar);                                          /* over */
    assert(!fill_now && !pedal_fill_hold && live_sec == 3);
    printf("pedal: the fill: the rest of this bar and the next cycle (1 bar) PASS\n");

    /* CC 20 with the section playing (D), nothing cued: nothing (it was a fill before 2026-10-14), D goes on; CC 27
     * the same */
    advance(bar / 4u);
    cc(16, 20, 3); advance(CTL * 2u);
    assert(!fill_now && !pedal_fill_hold && live_req == -1);
    advance(bar - (now - t0) % bar + CTL);                 /* into the next bar */
    assert(!fill_now && live_sec == 3 && live_bar >= 1);  /* (no restart: the bar count goes on) */
    advance(bar);
    assert(!fill_now && live_sec == 3);
    cc(16, 27, 3); advance(CTL * 2u);
    assert(!fill_now && live_req == -1);
    advance(bar - (now - t0) % bar + CTL);
    assert(!fill_now && live_sec == 3);
    advance(bar);
    assert(!fill_now);
    /* ..but with another section cued it is a request as any other (the last one wins) */
    cc(16, 20, 1); cc(16, 20, 3); advance(CTL * 2u);
    assert(live_req == 3 && !pedal_fill);
    advance(bar - (now - t0) % bar + CTL);                 /* into the next bar (the same phase as above: the tests
                                                            * after this one count on it) */
    assert(live_sec == 3 && live_req == -1 && !fill_now);
    advance(bar);
    printf("pedal: the section playing asked for again: nothing PASS\n");

    /* CC 22 with an outro: A once (1 bar), stop on the bar; no fill */
    advance(bar / 3u);
    cc(16, 22, 0); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(!fill_now && live_req == 0 && pedal_end_req == 1);
    advance(bar);
    assert(live_sec == 0 && song.playing && !fill_now && pedal_end_bars == 1);
    advance(bar);
    assert(!song.playing && stopped - jumped >= bar && stopped - jumped < bar + CTL);
    printf("pedal: ending with the outro A: A once, stop on the bar (%u) PASS\n", stopped - jumped);

    /* CC 22 with no outro: stop on the next bar */
    cc(16, 20, 1); advance(CTL * 3u);
    assert(song.playing && live_sec == 1);
    advance(bar + bar / 2u - (now - t0) % bar);
    {
        uint32_t at = now;
        cc(16, 22, 9); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
        assert(!fill_now && live_req == -1 && pedal_end_bars == live_bar + 1u);  /* (B is 1 bar: the next bar is its end) */
        advance(bar);
        assert(!song.playing && stopped - at >= bar / 2u - CTL && stopped - at < bar / 2u + CTL);
    }
    printf("pedal: ending with no outro: stop on the next bar PASS\n");

    /* the outro asked for again while it plays the ending: not a fill, the ending cancelled (the outro loops) */
    cc(16, 20, 1); advance(CTL * 3u);
    advance(bar / 3u - (now - t0) % bar);
    cc(16, 22, 0); advance(bar + CTL * 2u);
    assert(song.playing && live_sec == 0 && pedal_end_bars == 1);
    cc(16, 20, 0); advance(CTL * 2u);
    assert(live_req == 0 && !pedal_fill_hold && !pedal_end_req);
    advance(bar * 2u);
    assert(song.playing && live_sec == 0 && !pedal_end_bars);
    cc(16, 23, 0); advance(CTL * 2u);
    assert(!song.playing);
    printf("pedal: the outro asked for again cancels the ending PASS\n");

    /* a section asked for after an ending cancels it */
    cc(16, 20, 0); advance(CTL * 3u);
    advance(bar / 2u - (now - t0) % bar);
    cc(16, 22, 3); cc(16, 20, 1); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(live_req == 1 && pedal_end_req == 0);
    advance(bar * 3u);
    assert(song.playing && live_sec == 1 && !pedal_end_bars);
    /* ..and an ending after a section wins over it */
    cc(16, 20, 3); cc(16, 22, 9); advance(CTL * 2u);                /* (fill_now is read before the MIDI loop: one block more) */
    assert(live_req == -1 && pedal_end_bars == live_bar + 1u);      /* (a 1-bar section: its end is the next bar) */
    advance(bar);
    assert(!song.playing);
    printf("pedal: a section cancels an ending, an ending cancels a section PASS\n");

    /* the count-in (a setting): a section from stopped clicks a bar (two) first, then starts with it;
     * CC 23 cancels it; another section during it only changes what starts; playing: nothing changes */
    assert(!song.playing && !ci_on);
    pedal.count = 1;
    {
        uint32_t at = now;
        cc(16, 20, 0); advance(CTL * 3u);
        assert(!song.playing && ci_on && ci_pedal == 4u && live_req == 0 && str_eq(sng_ui_next, "A"));
        advance(bar / 2u);
        assert(!song.playing && ci_on && ci_beat == 2u);
        advance(bar / 2u + CTL * 2u);
        assert(song.playing && live_sec == 0 && !ci_on && !ci_pedal);
        assert(t0 - at >= bar && t0 - at < bar + CTL * 4u);
        cc(16, 20, 1); advance(bar + CTL * 2u);                /* playing: on the next bar, as before */
        assert(song.playing && live_sec == 1 && !ci_on);
        cc(16, 23, 0); advance(CTL * 2u);
        assert(!song.playing);
    }
    pedal.count = 2;
    {
        uint32_t at;
        cc(16, 20, 1); advance(CTL * 2u);
        assert(!song.playing && ci_on && ci_pedal == 8u);
        cc(16, 23, 0); advance(CTL * 2u);                      /* stop during the count-in: cancelled */
        assert(!song.playing && !ci_on && !ci_pedal);
        advance(bar * 2u);
        assert(!song.playing);
        at = now;
        cc(16, 20, 1); advance(bar + CTL);                     /* two bars of clicks.. */
        assert(!song.playing && ci_on && ci_beat == 4u);
        cc(16, 20, 3); advance(CTL * 2u);                      /* ..another section asked for meanwhile */
        assert(!song.playing && ci_on && live_req == 3);
        advance(bar);
        assert(song.playing && live_sec == 3 && !ci_on);
        assert(t0 - at >= 2u * bar && t0 - at < 2u * bar + CTL * 4u);
        cc(16, 23, 0); advance(CTL * 2u);
        assert(!song.playing);
    }
    pedal.count = 0;
    cc(16, 20, 0); advance(CTL * 3u);
    assert(song.playing && live_sec == 0 && !ci_on);           /* off again: at once */
    cc(16, 23, 0); advance(CTL * 2u);
    printf("pedal: the count-in: one bar, two bars, cancelled by stop, the section changed meanwhile PASS\n");

    /* a 2-bar section (D, 32 steps): a section asked for waits for D's end (no fill; with CC 21: the fill to the end) */
#define NEXT_BAR() advance(bar - (now - t0) % bar + CTL)   /* just past the next bar line */
    TDRUM->p[P_SLEN] = 32;
    capture(3);
    assert(section_bars(3) == 2u);
    cc(16, 20, 3); advance(CTL * 3u);                      /* D from stopped */
    assert(song.playing && live_sec == 3 && live_bar == 0);
    advance(bar / 4u);
    cc(16, 20, 0); advance(CTL * 2u);                      /* A asked for in D's first bar: cued, no fill yet */
    assert(live_req == 0 && pedal_at_end && !pedal_fill && !fill_now);
    NEXT_BAR();                                            /* D's second bar, its last: no fill of its own */
    assert(live_sec == 3 && live_req == 0 && !fill_now);
    NEXT_BAR();                                            /* D's end: A */
    assert(live_sec == 0 && live_req == -1 && !fill_now && !pedal_at_end);
    assert(since_start() >= 2u * bar && since_start() < 2u * bar + CTL);
    printf("pedal: a section waits for the 2-bar section's end, no fill (%u) PASS\n", since_start());
    /* asked for during the last bar: the section on the bar after (as a 1-bar section) */
    cc(16, 20, 3); NEXT_BAR();                             /* A (1 bar) -> D on the next bar */
    assert(live_sec == 3 && live_bar == 0);
    NEXT_BAR();                                            /* D's last bar */
    advance(bar / 4u);
    {
        uint32_t jD = jumped;
        cc(16, 20, 1); advance(CTL * 2u);
        assert(live_req == 1 && !fill_now && pedal_at_end);
        NEXT_BAR();
        assert(live_sec == 1 && !fill_now && jumped - jD >= 2u * bar && jumped - jD < 2u * bar + CTL);
    }
    printf("pedal: asked for in the last bar: the section on the bar PASS\n");
    /* CC 27 does not wait for the section's end: the section on the next bar */
    cc(16, 20, 3); NEXT_BAR();                             /* B (1 bar) -> D */
    assert(live_sec == 3 && live_bar == 0);
    advance(bar / 4u);
    cc(16, 27, 0); advance(CTL * 2u);
    assert(live_req == 0 && !fill_now && !pedal_at_end);
    NEXT_BAR();
    assert(live_sec == 0 && !fill_now && live_req == -1);   /* (D's first bar only: A) */
    NEXT_BAR();
    assert(live_sec == 0 && !fill_now);
    printf("pedal: CC 27 does not wait for the section's end PASS\n");
    /* the ending with no outro: D plays to its end, then the stop */
    cc(16, 20, 3); NEXT_BAR();
    assert(live_sec == 3 && live_bar == 0);
    advance(bar / 4u);
    cc(16, 22, 9); advance(CTL * 2u);
    assert(pedal_end_bars == 2u && pedal_at_end && !fill_now && live_req == -1);
    NEXT_BAR();
    assert(song.playing && live_sec == 3 && !fill_now);
    NEXT_BAR();
    assert(!song.playing);
    printf("pedal: the ending with no outro waits for the section's end PASS\n");
    /* CC 21 and CC 20 sent together (a footswitch's two messages, either order): the fill to the section's end, the
     * change there, the fill over */
    {
        uint32_t order;
        for (order = 0; order < 2u; order++) {
            cc(16, 20, 3); advance(CTL * 3u);              /* D (2 bars) from stopped */
            advance(bar / 4u);
            if (order) { cc(16, 20, 0); cc(16, 21, 0); } else { cc(16, 21, 0); cc(16, 20, 0); }
            advance(CTL * 2u);
            assert(fill_now && pedal_at_end && live_req == 0);
            NEXT_BAR();
            assert(fill_now && live_sec == 3);             /* D's last bar: still the fill */
            NEXT_BAR();
            assert(live_sec == 0 && !fill_now && !pedal_fill_hold);   /* A, the fill over */
            cc(16, 23, 0); advance(CTL * 2u);
        }
        /* in the section's last bar: the fill for the rest of it, the change on the bar, the fill over (not the
         * next cycle CC 21 alone gives) */
        cc(16, 20, 3); advance(CTL * 3u);
        NEXT_BAR();                                        /* D's last bar */
        advance(bar / 4u);
        cc(16, 21, 0); cc(16, 20, 0); advance(CTL * 2u);
        assert(fill_now && pedal_fill_hold == 2u && live_req == 0);
        NEXT_BAR();
        assert(live_sec == 0 && !fill_now && !pedal_fill_hold);
        cc(16, 23, 0); advance(CTL * 2u);
    }
    printf("pedal: CC 21 with CC 20: the fill to the section's end, into the change PASS\n");
    /* the ending with the outro A: after D's end (no fill), A once, the stop */
    cc(16, 20, 3); advance(CTL * 3u);                      /* D from stopped */
    advance(bar / 4u);
    cc(16, 22, 0); advance(CTL * 2u);
    assert(live_req == 0 && pedal_end_req == 1u && pedal_at_end && !fill_now);
    NEXT_BAR();
    assert(live_sec == 3 && !fill_now);
    NEXT_BAR();
    assert(live_sec == 0 && pedal_end_bars == 1u && !fill_now && song.playing);
    NEXT_BAR();
    assert(!song.playing);
    printf("pedal: the ending with an outro waits for the section's end PASS\n");
    /* CC 21 in the 2-bar D: asked for in its first bar, the fill runs to the pattern's end and stops there, D goes on;
     * asked for in its last bar, through the next cycle (two more bars) */
    cc(16, 20, 3); advance(CTL * 3u);                      /* D from stopped */
    advance(bar / 4u);
    cc(16, 21, 0); advance(CTL * 2u);
    assert(fill_now && pedal_fill_hold == 1u && live_sec == 3);
    NEXT_BAR();
    assert(fill_now && live_sec == 3 && live_bar == 1);
    NEXT_BAR();
    assert(!fill_now && live_sec == 3 && live_bar == 2 && !pedal_fill_hold);   /* (D's second cycle, no fill) */
    NEXT_BAR();
    advance(bar / 4u);                                     /* D's last bar */
    cc(16, 21, 0); advance(CTL * 2u);
    assert(fill_now && pedal_fill_hold == 2u);
    NEXT_BAR();
    assert(fill_now && live_bar == 4);
    NEXT_BAR();
    assert(fill_now && live_bar == 5);
    NEXT_BAR();
    assert(!fill_now && live_sec == 3 && song.playing);
    cc(16, 23, 0); advance(CTL * 2u);
    printf("pedal: the fill lasts to the pattern's end; in its last bar, through the next cycle PASS\n");

    /* LIVE's keys (ui_drummer.c): a section with no fill of its own, on the next bar (PS_BAR) or when the one playing
     * ends (PS_END); a fill asked for first (CC 21) goes on into the change */
    {
        uint32_t k, filled = 0, t;
        cc(16, 20, 3); advance(CTL * 3u);                  /* D from stopped */
        advance(bar / 4u);
        t = now;
        pedal_section(0, PS_BAR);
        for (k = 0; k < 2u * bar / CTL && live_sec == 3; k++) { advance(CTL); filled |= fill_now; }
        assert(live_sec == 0 && !filled && now - t < bar && since_start() % bar < 2u * CTL);   /* the next bar: A */
        advance(bar / 4u);
        pedal_section(3, PS_END);
        assert(pedal_at_end && !pedal_fill && live_req == 3);
        for (k = 0; k < 16u * bar / CTL && live_sec == 0; k++) { advance(CTL); filled |= fill_now; }
        assert(live_sec == 3 && !filled && since_start() % bar < 2u * CTL && live_bar == 0);   /* A played out: D, no fill */
        advance(bar / 4u);
        cc(16, 21, 0); advance(CTL * 2u);
        pedal_section(0, PS_END);
        assert(fill_now && pedal_at_end && live_req == 0);
        for (k = 0; k < 16u * bar / CTL && live_sec == 3; k++) { advance(CTL); assert(fill_now || live_sec != 3); }
        assert(live_sec == 0);                             /* the fill to D's end, then A */
        pedal_section(0, PS_END); advance(CTL * 2u);
        assert(live_req < 0 && !pedal_fill_hold);         /* the section playing again: nothing, no fill */
        cc(16, 23, 0); advance(CTL * 2u);
    }
    printf("pedal: LIVE's keys: a section on the next bar or at the end with no fill; a fill first goes into it PASS\n");

    /* the ending asked for while its outro plays already (nothing cued): the outro plays to its end, then the stop */
    cc(16, 20, 3); advance(CTL * 3u);                      /* D (2 bars) from stopped */
    advance(bar / 4u);
    cc(16, 22, 3); advance(CTL * 2u);                      /* the outro: D, the one playing */
    assert(live_req == -1 && pedal_at_end && pedal_end_bars == 2u && song.playing);
    NEXT_BAR();
    assert(song.playing && live_sec == 3);                 /* D's last bar */
    NEXT_BAR();
    assert(!song.playing);                                 /* D's end: the stop */
    printf("pedal: the ending while its outro plays: the outro to its end, then the stop PASS\n");
    return 0;
}
