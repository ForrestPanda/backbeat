/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: a pedalboard drives the song over MIDI (the TRS jack or USB, the same queue).
 * Commands on the pedal channel (pedal.ch: 1..16, 0 = off, 17 = any channel):
 *
 *   A section change brings no fill of its own (asked 2026-10-14): a fill is CC 21's, and a footswitch that
 *   wants a fill into the change sends CC 21 with its CC 20 (either order: the fill to the bar's end, the
 *   change on the bar after it, as an arranger's AUTO FILL IN).
 *   CC 20 n    go to section n (0..15, the song's) when the section playing ends: it plays its pattern out (its
 *              bars, from where it started); a one-bar section: the next bar. Stopped: start the loop with
 *              it. The section already playing asked for again (nothing else cued): nothing; with an
 *              ending pending it cancels the ending. A fill pending (asked before or after): on the bar
 *              after the fill instead
 *   CC 27 n    now: on the next bar, without waiting for the section's end, no fill of its own (a fill
 *              pending: after it, as CC 20)
 *   CC 21 v    the fill, as an arranger's fill-in (2026-10-15, before: to the pattern's end): from now to
 *              the end of this bar (the FILL ONLY hits in, the NO FILL hits out; a section's fill bar of
 *              its own instead of the pattern's, its step i at the bar's step i, so the phrase is cut from
 *              the front), then the pattern again from its top on the next bar, or the section asked for.
 *              Asked for in the bar's last beat: the whole next bar is the fill (too late for less than a
 *              beat of it); in the last beat of a fill, the next bar is a fill too, straight on. A fill bar
 *              of more than one bar ends on the bar line that many bars on. Value 1 (asked 2026-10-16, for
 *              a fill bar that leads off its bar: its hits in its first beat, which the cut from the front
 *              never plays): the whole next bar, from anywhere in this one; playing a fill, the next bar a
 *              fill too, straight on. Any other value: from now, as above. SLOOP's fill-next-bar (fill_arm,
 *              the GLO layer's, gone) is not touched
 *   CC 22 n    ending: when the section playing ends (as CC 20; a fill pending: after it), section n (the
 *              outro) once, for the bars its longest pattern takes, then stop on the bar. n out of range or
 *              empty: the song's flagged outro, or with none the section playing ends, then stop (a fill
 *              pending: the stop after it). The outro playing already (nothing cued): it plays to its end,
 *              then stop.
 *   CC 28 n    the break (2026-10-18): section n through its break, the bars written to play before its pattern (sng.c),
 *              when the section playing ends, as CC 20 (a fill pending: after it, so one switch sends the fill and
 *              the break); the section playing too (it comes in again, through its break). n not a section (127):
 *              the section playing's (stopped: the one cued, else the first). No break written: its entry; neither:
 *              a bar of rest. Stopped: the loop starts with it. CC 20, 24, 25, 27 and 22 bring a section in
 *              through its entry, if it has one (from another section or from stopped; the section playing asked
 *              for again stays a no-op, so a switch sending a fill and its own section plays the fill alone)
 *   CC 23      stop now
 *   CC 24      next section (empty ones skipped, wraps), as CC 20; stopped: the first one
 *   CC 25      previous section; stopped: the last one
 *   CC 26      tap tempo
 *
 * The count-in (pedal.count, a setting kept with the channel: 0 off, 1 or 2 bars): a section asked for
 * while stopped first clicks that many bars at the tempo (the metronome's voice, the first beat of each
 * bar accented), then the loop starts with the section; CC 23 cancels it, another section command only
 * changes what starts. Playing, nothing changes: sections land on the bar as before.
 *   PC n       load a song: 0..15 the user's slots 1..16, 16..31 the factory grooves 1..16; stopped, its tempo
 *              and kit now and its first section cued; playing, with the next section. n >= 32: no song
 *              (sng.c): the drum track's pattern loops, there are no sections (SLOOP's A..D went 2026-10-27)
 *
 * A fill is SLOOP's: the steps marked FILL ONLY play, the NO FILL steps rest; a pattern with no
 * conditions plays as usual through a fill. The value of CC 23..26 does not matter. Section and
 * ending commands are absolute, so a footswitch that sends its message on every press (both presses
 * of a toggle) does no harm; next / previous / tap count presses. A section asked for cancels an
 * ending asked for before.
 *
 * Called from events_block (the audio ISR) between two blocks, so it only sets what the sequencer
 * applies itself on the bar: live_req, pedal_fill / pedal_at_end / pedal_end_* (seq.c live_block) and
 * transport_req (the next block). Included from seq.c after the live section code (FELUCCA_DRUMMER). */
#define PEDAL_CC_SECTION 20u
#define PEDAL_CC_FILL    21u
#define PEDAL_CC_ENDING  22u
#define PEDAL_CC_STOP    23u
#define PEDAL_CC_NEXT    24u
#define PEDAL_CC_PREV    25u
#define PEDAL_CC_TAP     26u
#define PEDAL_CC_SECTION_NOW 27u
#define PEDAL_CC_BREAK   28u
#define PEDAL_CH_OMNI    17u
#ifndef PEDAL_CH_DEFAULT
#define PEDAL_CH_DEFAULT 16u
#endif

#define PEDAL_COUNT_MAX  2u                       /* bars of count-in, at most */

static struct {
    uint8_t ch;                                   /* the pedal channel, 1..16; 0 off; 17 omni */
    uint8_t count;                                /* the count-in: bars of clicks before a start from stopped (0: none) */
    uint8_t tap_n;
    uint32_t clock;                               /* samples rendered (the tap clock, playing or not) */
    uint32_t tap[4];
} pedal = {PEDAL_CH_DEFAULT, 0, 0, 0, {0, 0, 0, 0}};

static volatile uint32_t pedal_seen_ms;        /* fm1_ms of the last message on the pedal channel */

static void pedal_elapse(uint32_t n) { pedal.clock += n; }

static int pedal_listens(uint32_t ch)            /* ch: 0..15 as on the wire */
{
    return pedal.ch == PEDAL_CH_OMNI || (pedal.ch && ch + 1u == (uint32_t)pedal.ch);
}

/* the sections the pedal plays: the song's (sng.c); none without a song (JAM's A..D gone 2026-10-27) */
static uint32_t pedal_nsec(void) { return SNG_SECTIONS; }
static uint32_t pedal_ready(void) { return sng_cur >= 0 ? sng_ready() : 0u; }
static uint32_t pedal_bars(uint32_t s) { return sng_bars(s); }
static int pedal_section_ok(uint32_t s)
{
    return s < pedal_nsec() && ((pedal_ready() >> s) & 1u) != 0u;
}
static void pedal_name(uint32_t s, char *out)    /* the section's name for the screen: the song's */
{
    sng_name(s, out);
}

/* CC 21: the fill. A section with a fill bar of its own, as the arrangers (Yamaha, Korg, Roland, Ketron, the
 * BeatBuddy) do it (2026-10-15, after a day of fills aligned to the pattern's end): from now to the end of this bar,
 * the pattern again from its top on the next bar (live_block), or the section asked for meanwhile, which waits for
 * the fill's end instead of the pattern's. A fill bar longer than a bar (fb_bars) ends on the section's last bar line
 * (2026-10-20; fill_wait_from): asked for earlier, it waits, then plays whole; later, its last bars from now. A section
 * without one (its fill in its marks and conditions, or none at all; 2026-10-17): fill mode from now to the
 * section's end (fill_bars_from), the marks heard where they sit, the pattern on from its top with no restart, the
 * section asked for at that end. Asked for in the bar's last beat (as Yamaha's last beat; the BeatBuddy's last half
 * beat): the whole next bar is the fill (no fill bar, in the section's last bar: the whole next cycle). A fill
 * playing asked for again: nothing new (it ends at its bar line), except in the last beat of its last bar: then the
 * next bar is a fill too, straight on (asked 2026-10-16: a two-bar fill from two presses).
 * next (CC 21 value 1; LIVE's black keys and SEL with OCT+ held; asked 2026-10-16 for a fill bar whose hits lead off
 * the bar, which the cut from the front never plays): the whole next bar from anywhere in this one, as the last beat's
 * press; playing a fill in its last bar, the next bar a fill too, straight on. The user has an Ampero patch a song, so
 * the switch says which; a rule that defers to the next bar by itself when the fill from here would sound like the
 * groove was considered and parked (design.md). */
static void pedal_fill_req(uint32_t next)
{
    uint32_t b16 = meter_b16(clk_meter), beat = 16u / meter_den(clk_meter), at = clk_16th() % b16;
    if (!song.playing)
        return;
    if (next)
        at = b16 - 1u;                            /* as asked in the bar's last beat */
    if (!pedal_fill && !pedal_fill_wait) {
        uint32_t late = at + beat >= b16, wait = late + fill_wait_from(live_bar + late);
        if (wait) {                               /* the bar's last beat: the next bar, whole; a fill of several bars:
                                                   * its start, to end on the section's end */
            pedal_fill_wait = (uint8_t)wait;
        } else {
            eb_cut();                             /* (an entry or break playing: the fill cuts it) */
            pedal_fill = 1;
            pedal_fill_bars = (uint8_t)fill_bars_from(live_bar);
        }
    } else if (pedal_fill && pedal_fill_bars <= 1u && at + beat >= b16) {
        pedal_fill_wait = 1;                      /* the fill's last beat: another one next bar (live_block chains it) */
    }
    if (live_req >= 0)
        pedal_at_end = 0;                         /* a section cued: on the bar after the fill (live_block) */
    pedal_say(PU_FILL, "");
}

/* how a section comes in, with no fill of its own: when the section playing ends (CC 20, 24, 25, 22; LIVE's white
 * keys 1..16, ENV, LFO) or on the next bar (CC 27; LIVE's white keys with OCT+ held) */
enum { PS_END, PS_BAR };
/* section s: how (PS_*), through its break (brk: CC 28) or its entry; stopped: the loop starts with it on its bar 0
 * (seq_start resets the bar count, live_block then applies the request at once). An empty section (no song: every
 * one) is ignored. */
static void pedal_section_to(uint32_t s, uint32_t how, uint32_t brk)
{
    if (!pedal_section_ok(s))
        return;
    if (song.playing && live_req < 0 && (int32_t)s == live_sec && !pedal_end_req && !pedal_end_bars && !brk &&
        !sng_pending())                           /* (a song just loaded waits for a section: its own, not this one's) */
        return;                                   /* the section playing, asked for again: it plays already */
    pedal_end_req = 0;                            /* an ending asked for before: the section wins */
    pedal_end_bars = 0;
    pedal_end_eb = 0;
    live_req = (int8_t)s;
    live_req_brk = (uint8_t)brk;                  /* (the last asked for wins: its break or not) */
    if (!song.playing) {
        char b[13];
        pedal_fill = 0;
        pedal_name(s, b);
        str_cpy(sng_ui_next, b, sizeof sng_ui_next);   /* (the screen: cued, in amber, until it starts) */
        if (pedal.count && !ci_on && !mclk_on()) {   /* the count-in: the clicks, the start after them
                                                   * (events_block; not with a MIDI clock master) */
            ci_meter = (uint8_t)sng_meter(s);     /* (the section's meter: its bars of clicks) */
            ci_pedal = (uint8_t)(meter_num(ci_meter) * (uint32_t)pedal.count);
            ci_on = 1;
            ci_u = 0;
            ci_beat = 0;
            click_on(1);
            pedal_say(PU_COUNT, b);
        } else if (!ci_pedal) {
            transport_req = 1;
        }                                         /* (a count-in running: it starts the section now cued) */
        return;
    }
    pedal_at_end = (uint8_t)(how == PS_END && !pedal_fill && !pedal_fill_wait);   /* (a fill pending: this lands on the
                                                   * bar after it, live_block) */
    {
        char b[13];
        pedal_name(s, b);
        pedal_say(brk ? PU_BREAK : PU_NEXT, b);
        str_cpy(sng_ui_next, b, sizeof sng_ui_next);   /* (shown until it starts) */
    }
}
static void pedal_section(uint32_t s, uint32_t how) { pedal_section_to(s, how, 0); }

/* CC 28: section s through its break; s not a section: the section playing's (stopped: the one cued, else the
 * first ready) */
static void pedal_break(uint32_t s)
{
    if (!pedal_section_ok(s)) {
        uint32_t ready = pedal_ready();
        s = song.playing && live_sec >= 0 ? (uint32_t)live_sec : live_req >= 0 ? (uint32_t)live_req :
            live_sec >= 0 && ((ready >> live_sec) & 1u) ? (uint32_t)live_sec : ready ? (uint32_t)__builtin_ctz(ready) : 0u;
    }
    pedal_section_to(s, PS_END, 1);
}

static void pedal_step(int dir, uint32_t how)     /* the next / previous ready section, wrapping; how: PS_* */
{
    uint32_t ready = pedal_ready(), n = pedal_nsec(), i, s;
    if (!ready)
        return;
    s = live_req >= 0 ? (uint32_t)live_req : song.playing && live_sec >= 0 ? (uint32_t)live_sec :
        dir > 0 ? n - 1u : 0u;                    /* stopped (nothing cued): next = the first, previous = the last */
    for (i = 0; i < n; i++) {
        s = (s + n + (uint32_t)dir) % n;
        if ((ready >> s) & 1u) {
            pedal_section(s, how);
            return;
        }
    }
}

/* the ending: the outro s once (its bars: sng_bars), then stop;
 * no outro, or the outro playing already: the section playing ends, then stop */
static void pedal_ending(uint32_t s)
{
    if (!song.playing)
        return;
    if (!pedal_section_ok(s) && sng_cur >= 0)
        s = sng_outro();                          /* the song's own outro, if one is flagged */
    if (pedal_section_ok(s) && !(live_req < 0 && (int32_t)s == live_sec)) {   /* (the outro playing already, nothing
                                                   * cued: as no outro below, it plays out, then the stop) */
        char b[13];
        pedal_section(s, PS_END);
        pedal_end_req = (uint8_t)pedal_bars(s);
        pedal_name(s, b);
        pedal_say(PU_ENDING, b);
    } else {
        pedal_say(PU_ENDING, "");
        str_cpy(sng_ui_next, "STOP", sizeof sng_ui_next);
        live_req = -1;                            /* (a section asked for before: the ending wins) */
        pedal_end_req = 0;
        {                                         /* the section playing ends, then the stop (a fill pending: the
                                                   * stop at its end, live_block) */
            uint32_t n = live_sec_bars();
            pedal_at_end = 1;
            pedal_end_bars = (live_bar / n + 1u) * n;
        }
    }
}

static void pedal_tap(void)                       /* the beat from the last taps, on the sample clock */
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

/* a pedal's control change (CC 20..28, value d2): from the MIDI in queue, or the song screen's keys (ui_drummer.c,
 * with the IRQ off) */
static void pedal_cc(uint32_t d1, uint32_t d2)
{
    switch (d1) {
    case PEDAL_CC_SECTION:         pedal_section(d2, PS_END); break;
    case PEDAL_CC_SECTION_NOW:     pedal_section(d2, PS_BAR); break;
    case PEDAL_CC_FILL:            pedal_fill_req(d2 == 1u); break;
    case PEDAL_CC_ENDING:          pedal_ending(d2); break;
    case PEDAL_CC_STOP:            transport_req = 2; pedal_say(PU_STOP, ""); break;
    case PEDAL_CC_NEXT:            pedal_step(1, PS_END); break;
    case PEDAL_CC_PREV:            pedal_step(-1, PS_END); break;
    case PEDAL_CC_TAP:             pedal_tap(); break;
    case PEDAL_CC_BREAK:           pedal_break(d2); break;
    default: break;
    }
}

/* a control or program change on the pedal channel, from the MIDI in queue (events_block) */
static void pedal_msg(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    if (!pedal_listens(ch))
        return;
    pedal_seen_ms = fm1_ms;                       /* (the LIVE screen's MIDI dot) */
    if (st == 0xC0u)                              /* PC: a song, 16..31 a groove (sng.c, loaded by the main loop) */
        sng_req = (uint8_t)(d1 < SNG_ALL ? d1 + 1u : 0xFFu);
    else if (st == 0xB0u)
        pedal_cc(d1, d2);
}
