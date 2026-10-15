/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: a pedalboard drives the song over MIDI (the TRS jack or USB, the same queue).
 * Commands on the pedal channel (pedal.ch: 1..16, 0 = off, 17 = any channel):
 *
 *   A section change brings no fill of its own (asked 2026-10-14): a fill is CC 21's, and a footswitch that
 *   wants a fill into the change sends CC 21 with its CC 20 (either order: the fill runs to the end of the
 *   section playing, where the change lands, and ends with it).
 *   CC 20 n    go to section n (0..3 = A..D) when the section playing ends: it plays its pattern out (its
 *              bars, from where it started); a one-bar section: the next bar. Stopped: start the loop with
 *              it. The section already playing asked for again (nothing else cued): nothing; with an
 *              ending pending it cancels the ending
 *   CC 27 n    now: on the next bar, without waiting for the section's end
 *   CC 21      the fill from now until the section's pattern ends (the FILL ONLY hits in, the NO FILL
 *              hits out, wherever they sit in it), the section goes on; asked for during the pattern's
 *              last bar: through the next cycle too (a 1-bar section: the rest of this bar and all of
 *              the next); a section change landing ends it. SLOOP's fill-next-bar (fill_arm, the GLO
 *              layer's, gone) is not touched
 *   CC 22 n    ending: when the section playing ends (as CC 20), section n (the outro) once, for the bars
 *              its longest pattern takes, then stop on the bar. n out of range or empty: the song's flagged
 *              outro, or with none the section playing ends, then stop. The outro playing already (nothing
 *              cued): it plays to its end, then stop.
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
 *              and kit now and its first section cued; playing, with the next section. n >= 32: no song,
 *              SLOOP's sections A..D again (sng.c)
 *
 * A fill is SLOOP's: the steps marked FILL ONLY play, the NO FILL steps rest; a pattern with no
 * conditions plays as usual through a fill. The value of CC 21, 23..26 does not matter. Section and
 * ending commands are absolute, so a footswitch that sends its message on every press (both presses
 * of a toggle) does no harm; next / previous / tap count presses. A section asked for cancels an
 * ending asked for before, and ends a quick chain (as a single section tap does).
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

/* the drum track's fill-only hits (a step's FILL ONLY condition, a hit's own F mark) on steps from..to-1 */
static int pedal_fill_hits(uint32_t from, uint32_t to)
{
    const track_t *t = TDRUM;
    uint32_t i;
    for (i = from; i < to && i < NSTEP; i++)
        if (drum_fonly[i] || (step_fill(t, i) == FC_FILL && dstep_mask(&t->dstep[i])))
            return 1;
    return 0;
}
/* CC 21: the fill. Asked in the pattern's last bar it waits for the next cycle and plays all of it (the user's rule,
 * 2026-10-15: in the last bar a fill is for the next time round, whatever fill hits are still ahead; a one-bar verse:
 * the next bar); a section change or an ending asked for meanwhile waits for it (live_block: pedal_at_end holds while
 * a fill is pending). Asked before the last bar: the fill from now to the pattern's end, and through the next cycle
 * too when the pattern's fill hits are all behind the request (asked too late to hear them). A fill already playing
 * asked again in the last bar goes on through the next cycle. */
static void pedal_fill_req(void)
{
    uint32_t len = trk_len(TDRUM), next = (uint32_t)TDRUM->seq_idx + 1u, n = live_sec_bars();
    if (!song.playing)
        return;
    if ((live_bar + 1u) % n == 0u) {              /* the last bar: the next cycle */
        if (!pedal_fill)
            pedal_fill_wait = 1;
        if (pedal_end_bars && pedal_fill_hold < 2u)
            pedal_end_bars += n;                  /* (an ending's stop after the fill) */
        pedal_fill_hold = 2u;
    } else {
        pedal_fill = 1;
        pedal_fill_wait = 0;
        pedal_fill_hold = (uint8_t)(pedal_fill_hits(0, len) && !pedal_fill_hits(next, len) ? 2u : 1u);
    }
    pedal_say(PU_FILL, "");
}

/* how a section comes in, with no fill of its own: when the section playing ends (CC 20, 24, 25, 22; LIVE's white
 * keys 1..16, ENV, LFO) or on the next bar (CC 27; LIVE's white keys with OCT+ held) */
enum { PS_END, PS_BAR };
/* section s: how (PS_*); stopped: the loop starts with it on its bar 0 (seq_start resets the bar count,
 * live_block then applies the request at once). An empty section, or a section while the song chain plays
 * (as the UI: "SONG PLAYS"), is ignored. */
static void pedal_section(uint32_t s, uint32_t how)
{
    if (!pedal_section_ok(s) || arrangement_clock.running)
        return;
    if (song.playing && live_req < 0 && (int32_t)s == live_sec && !pedal_end_req && !pedal_end_bars &&
        !(sng_cur >= 0 && sng_pending()))      /* (a song just loaded waits for a section: its own, not this one's) */
        return;                                   /* the section playing, asked for again: it plays already */
    chain_n = 0;                                  /* a section asked for ends a quick chain */
    pedal_end_req = 0;                            /* ..and an ending asked for before */
    pedal_end_bars = 0;
    live_req = (int8_t)s;
    if (!song.playing) {
        char b[13];
        pedal_fill = 0;
        arrangement_enabled = 0;                  /* a pedal plays sections, not the chain */
        pedal_name(s, b);
        str_cpy(sng_ui_next, b, sizeof sng_ui_next);   /* (the screen: cued, in amber, until it starts) */
        if (pedal.count && !ci_on && !mclk_on()) {   /* the count-in: the clicks, the start after them
                                                   * (events_block; not with a MIDI clock master) */
            ci_meter = (uint8_t)(sng_cur >= 0 ? sng_meter(s) : 0u);   /* (the section's meter: its bars of clicks) */
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
    pedal_at_end = (uint8_t)(how == PS_END);      /* (a fill asked for already goes on: it ends when this lands) */
    {
        char b[13];
        pedal_name(s, b);
        pedal_say(PU_NEXT, b);
        str_cpy(sng_ui_next, b, sizeof sng_ui_next);   /* (shown until it starts) */
    }
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

/* the ending: the outro s once (its bars: section_bars, as the quick chain counts them), then stop;
 * no outro, or the outro playing already: the section playing ends, then stop */
static void pedal_ending(uint32_t s)
{
    if (!song.playing || arrangement_clock.running)
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
        chain_n = 0;
        live_req = -1;                            /* (a section asked for before: the ending wins) */
        pedal_end_req = 0;
        {                                         /* the section playing ends, then the stop (a fill pending:
                                                   * after its cycle) */
            uint32_t n = live_sec_bars();
            pedal_at_end = 1;
            pedal_end_bars = (live_bar / n + (pedal_fill_hold > 1u ? 2u : 1u)) * n;
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

/* a pedal's control change (CC 20..27, value d2): from the MIDI in queue, or the song screen's keys (ui_drummer.c,
 * with the IRQ off) */
static void pedal_cc(uint32_t d1, uint32_t d2)
{
    switch (d1) {
    case PEDAL_CC_SECTION:         pedal_section(d2, PS_END); break;
    case PEDAL_CC_SECTION_NOW:     pedal_section(d2, PS_BAR); break;
    case PEDAL_CC_FILL:            pedal_fill_req(); break;
    case PEDAL_CC_ENDING:          pedal_ending(d2); break;
    case PEDAL_CC_STOP:            transport_req = 2; pedal_say(PU_STOP, ""); break;
    case PEDAL_CC_NEXT:            pedal_step(1, PS_END); break;
    case PEDAL_CC_PREV:            pedal_step(-1, PS_END); break;
    case PEDAL_CC_TAP:             pedal_tap(); break;
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
