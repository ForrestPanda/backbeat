/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-arranger: a pedalboard drives the song over MIDI (the TRS jack or USB, the same queue).
 * Commands on the pedal channel (pedal.ch: 1..16, 0 = off, 17 = any channel):
 *
 *   CC 20 n    go to section n (0..3 = A..D) on the next bar, the rest of this bar a fill;
 *              stopped: start the loop with it (no fill)
 *   CC 27 n    the same with a whole fill bar first: the rest of this bar and all of the next are the
 *              fill, the section starts on the bar after
 *   CC 21      fill the next bar, stay in the section (SLOOP's GLO + key 10)
 *   CC 22 n    ending: the rest of this bar a fill, then section n (the outro) once, for the bars its
 *              longest pattern takes, then stop on the bar. n out of range or empty: stop on the next bar.
 *   CC 23      stop now
 *   CC 24      next section (empty ones skipped, wraps), with the fill
 *   CC 25      previous section
 *   CC 26      tap tempo
 *   PC n       load song n (0..7) from flash: stopped, its tempo and kit now and its first section cued;
 *              playing, with the next section. n >= 8: no song, SLOOP's sections A..D again (sng.c)
 *
 * A fill is SLOOP's: the steps marked FILL ONLY play, the NO FILL steps rest; a pattern with no
 * conditions plays as usual through a fill. The value of CC 21, 23..26 does not matter. Section and
 * ending commands are absolute, so a footswitch that sends its message on every press (both presses
 * of a toggle) does no harm; next / previous / tap count presses. A section asked for cancels an
 * ending asked for before, and ends a quick chain (as a single section tap does).
 *
 * Called from events_block (the audio ISR) between two blocks, so it only sets what the sequencer
 * applies itself on the bar: live_req, pedal_fill / pedal_wait / pedal_end_* (seq.c live_block) and
 * transport_req (the next block). Included from seq.c after the live section code (FELUCCA_ARRANGER). */
#define PEDAL_CC_SECTION 20u
#define PEDAL_CC_FILL    21u
#define PEDAL_CC_ENDING  22u
#define PEDAL_CC_STOP    23u
#define PEDAL_CC_NEXT    24u
#define PEDAL_CC_PREV    25u
#define PEDAL_CC_TAP     26u
#define PEDAL_CC_SECTION_FILLBAR 27u
#define PEDAL_CH_OMNI    17u
#ifndef PEDAL_CH_DEFAULT
#define PEDAL_CH_DEFAULT 16u
#endif

static struct {
    uint8_t ch;                                   /* the pedal channel, 1..16; 0 off; 17 omni */
    uint8_t tap_n;
    uint32_t clock;                               /* samples rendered (the tap clock, playing or not) */
    uint32_t tap[4];
} pedal = {PEDAL_CH_DEFAULT, 0, 0, {0, 0, 0, 0}};

static void pedal_elapse(uint32_t n) { pedal.clock += n; }

static int pedal_listens(uint32_t ch)            /* ch: 0..15 as on the wire */
{
    return pedal.ch == PEDAL_CH_OMNI || (pedal.ch && ch + 1u == (uint32_t)pedal.ch);
}

/* the sections the pedal plays: the song's (sng.c) while one is loaded, else SLOOP's A..D */
static uint32_t pedal_nsec(void) { return sng_cur >= 0 ? SNG_SECTIONS : ARR_SCENES; }
static uint32_t pedal_ready(void) { return sng_cur >= 0 ? sng_ready() : arrangement_ready(); }
static uint32_t pedal_bars(uint32_t s) { return sng_cur >= 0 ? sng_bars(s) : section_bars(s); }
static int pedal_section_ok(uint32_t s)
{
    return s < pedal_nsec() && ((pedal_ready() >> s) & 1u) != 0u;
}
static void pedal_name(uint32_t s, char *out)    /* the section's name for the screen: the song's, or A..D */
{
    if (sng_cur >= 0) {
        sng_name(s, out);
    } else {
        out[0] = (char)('A' + (s & 3u));
        out[1] = 0;
    }
}

/* section s on the next bar (fillbar: on the bar after, with a whole fill bar); stopped: the loop starts
 * with it on its bar 0 (seq_start resets the bar count, live_block then applies the request at once).
 * An empty section, or a section while the song chain plays (as the UI: "SONG PLAYS"), is ignored. */
static void pedal_section(uint32_t s, uint32_t fillbar)
{
    if (!pedal_section_ok(s) || arrangement_clock.running)
        return;
    chain_n = 0;                                  /* a section asked for ends a quick chain */
    pedal_end_req = 0;                            /* ..and an ending asked for before */
    pedal_end_bars = 0;
    live_req = (int8_t)s;
    if (!song.playing) {
        pedal_fill = 0;
        pedal_wait = 0;
        arrangement_enabled = 0;                  /* a pedal plays sections, not the chain */
        transport_req = 1;
        return;
    }
    pedal_fill = 1;                               /* the rest of this bar is the fill */
    pedal_wait = (uint8_t)(fillbar != 0u);        /* ..and the whole next bar: the section after it */
    {
        char b[13];
        pedal_name(s, b);
        pedal_say(PU_NEXT, b);
        str_cpy(sng_ui_next, b, sizeof sng_ui_next);   /* (shown until it starts) */
    }
}

static void pedal_step(int dir)                   /* the next / previous ready section, wrapping */
{
    uint32_t ready = pedal_ready(), n = pedal_nsec(), i, s;
    if (!ready)
        return;
    s = live_req >= 0 ? (uint32_t)live_req : live_sec >= 0 ? (uint32_t)live_sec :
        dir > 0 ? n - 1u : 0u;                    /* nothing yet: next = the first, previous = the last */
    for (i = 0; i < n; i++) {
        s = (s + n + (uint32_t)dir) % n;
        if ((ready >> s) & 1u) {
            pedal_section(s, 0);
            return;
        }
    }
}

/* the ending: the outro s once (its bars: section_bars, as the quick chain counts them), then stop;
 * no outro: the fill, then stop on the next bar */
static void pedal_ending(uint32_t s)
{
    if (!song.playing || arrangement_clock.running)
        return;
    if (!pedal_section_ok(s) && sng_cur >= 0)
        s = sng_outro();                          /* the song's own outro, if one is flagged */
    if (pedal_section_ok(s)) {
        char b[13];
        pedal_section(s, 0);
        pedal_end_req = (uint8_t)pedal_bars(s);
        pedal_name(s, b);
        pedal_say(PU_ENDING, b);
    } else {
        pedal_say(PU_ENDING, "");
        str_cpy(sng_ui_next, "STOP", sizeof sng_ui_next);
        chain_n = 0;
        live_req = -1;                            /* (a section asked for before: the ending wins) */
        pedal_wait = 0;
        pedal_end_req = 0;
        pedal_fill = 1;
        pedal_end_bars = 1;                       /* live_bar is at least 1 on the next bar */
    }
}

static void pedal_tap(void)                       /* as tap_tempo (ui_layers.c), on the sample clock */
{
    uint32_t now = pedal.clock, i, n, sum = 0;
    if (pedal.tap_n && now - pedal.tap[(pedal.tap_n - 1u) & 3u] > 2u * FS)
        pedal.tap_n = 0;                          /* a pause: a new count */
    pedal.tap[pedal.tap_n & 3u] = now;
    pedal.tap_n++;
    if (pedal.tap_n < 2u)
        return;
    n = pedal.tap_n - 1u > 3u ? 3u : pedal.tap_n - 1u;
    for (i = 0; i < n; i++)
        sum += pedal.tap[(pedal.tap_n - 1u - i) & 3u] - pedal.tap[(pedal.tap_n - 2u - i) & 3u];
    if (sum)
        song.g[G_BPM] = (int16_t)clamp((int32_t)((60u * FS * n + sum / 2u) / sum), 40, 240);
}

/* a control or program change on the pedal channel, from the MIDI in queue (events_block) */
static void pedal_msg(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    if (!pedal_listens(ch))
        return;
    if (st == 0xC0u) {                            /* PC: a song (sng.c, loaded by the main loop) */
        sng_req = (uint8_t)(d1 < SNG_SLOTS ? d1 + 1u : 0xFFu);
        return;
    }
    if (st != 0xB0u)
        return;
    switch (d1) {
    case PEDAL_CC_SECTION:         pedal_section(d2, 0); break;
    case PEDAL_CC_SECTION_FILLBAR: pedal_section(d2, 1); break;
    case PEDAL_CC_FILL:            if (song.playing) { fill_arm = 1; pedal_say(PU_FILL, ""); } break;
    case PEDAL_CC_ENDING:          pedal_ending(d2); break;
    case PEDAL_CC_STOP:            transport_req = 2; pedal_say(PU_STOP, ""); break;
    case PEDAL_CC_NEXT:            pedal_step(1); break;
    case PEDAL_CC_PREV:            pedal_step(-1); break;
    case PEDAL_CC_TAP:             pedal_tap(); break;
    default: break;
    }
}
