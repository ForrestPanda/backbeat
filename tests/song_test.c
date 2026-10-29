/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: songs (firmware/src/sng.c) through the real sequencer and the pedal (pedal.c). Songs are
 * packed as tools/fm1_song.py packs them, into a host table that stands for the flash slots. Checks: the
 * packed form's checks; PC loads a song (stopped: tempo, kit and the first section at once); its sections
 * start on the bar with their length, division and fill conditions; next / previous over the song's
 * sections; the ending takes the flagged outro, for its bars; PC while playing waits for the next section;
 * a bad or empty slot leaves the song alone; PC 127 unloads (no song: no sections, the pattern stays); the pedal
 * settings object. */
#define FELUCCA_DRUMMER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;

static uint8_t blob[SNG_ALL][3800];              /* the flash slots, then the grooves (the image's) */
static uint32_t blob_n[SNG_ALL];
#define SNG_HOST_FETCH 1
static const uint8_t *sng_fetch(uint32_t slot, uint32_t *len)
{
    *len = slot < SNG_ALL ? blob_n[slot] : 0u;
    return *len ? blob[slot] : 0;
}
#include "../firmware/src/sng.c"
#include "songbuild.h"                                 /* song_hdr, song_sec, song_secm, song_blk, song_fb */

static void cc(uint32_t ch, uint32_t num, uint32_t val) { um_byte(0xB0u | (ch - 1u)); um_byte(num); um_byte(val); }
static void pc(uint32_t ch, uint32_t n) { um_byte(0xC0u | (ch - 1u)); um_byte(n); }

static int32_t out[CTL * 2];
static uint32_t now, t0, jumped, stopped;
static void advance(uint32_t samples)
{
    uint32_t end = now + samples;
    while (now < end) {
        int8_t sec = live_sec, req = live_req;
        uint8_t playing = song.playing;
        mix_block(out, CTL);
        if (!playing && song.playing) t0 = now;
        if (sec != live_sec || (playing && req >= 0 && live_req < 0 && song.playing)) jumped = now;   /* (a landing: the
                                                          * same section too, through its break) */
        if (playing && !song.playing) stopped = now;
        now += CTL;
    }
}
static uint32_t bar_len(void) { return 4u * 60u * FS / (uint32_t)song.g[G_BPM]; }
static int lane_on(const dstep_t *d, uint32_t l) { return (d->on[(l >> 3) & 1u] >> (l & 7u)) & 1u; }

int main(int argc, char **argv)
{
    uint32_t i, k, pos, bar;
    sng_t tmp;
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1) {                                       /* a packed song from tools/fm1_song.py: the firmware's check */
        static uint8_t file[4096];
        FILE *f = fopen(argv[1], "rb");
        uint32_t n = f ? (uint32_t)fread(file, 1, sizeof file, f) : 0u;
        int rc = n ? sng_check(file, n, &tmp) : 9;
        if (f) fclose(f);
        printf("song: %s: %u bytes, check %d%s\n", argv[1], n, rc, rc ? " FAIL" : " ok");
        if (!rc) {
            for (k = 0; k < tmp.nsec; k++)
                printf("  %u: %-12.12s %2u steps div %u flags %u at %u\n", k, (const char *)file + tmp.off[k],
                       file[tmp.off[k] + 12], file[tmp.off[k] + 13], file[tmp.off[k] + 14], tmp.off[k]);
            host_tracks_init();                           /* and each section through the drum track and packed again
                                                           * (EDIT, SAVE held): the same bytes */
            memcpy(blob[0], file, n); blob_n[0] = n;
            if (sng_load(0))
                rc = 8;
            for (k = 0; !rc && k < sng.nsec; k++) {
                static uint8_t out[SNG_MAX];
                uint32_t m;
                sng_apply(k);
                m = sng_pack_section(k, out, sizeof out);
                if (m != n || memcmp(out, file, n)) {
                    uint32_t b = 0;
                    while (b < n && b < m && out[b] == file[b]) b++;
                    printf("  section %u packs again as %u bytes, not %u (first difference at %u)\n", k, m, n, b);
                    rc = 7;
                }
            }
            printf("song: %s: each section packed again from the drum track:%s\n", argv[1], rc ? " FAIL" : " the same bytes");
        }
        return rc ? 1 : 0;
    }
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = 16; trk[i].p[P_SDIV] = 2; trk[i].p[P_AMODE] = 0; trk[i].p[P_LEVEL] = 90;
    }
    TDRUM->p[P_PAN] = 36;                                 /* the section's marker (drums only: no bass track) */
    for (k = 0; k < 16; k++) {
        uint8_t drum[] = {42, (uint8_t)(k % 8u == 4u ? 38 : 36)};
        put_step(TDRUM, k, k % 4u ? 1 : 2, drum, ST_NOTE, 0);
    }

    /* ---- the songs: slot 0 three sections (INTRO 1 bar, VERSE 2 bars with FILL ONLY steps, OUTRO flagged),
     * slot 1 one section at 100 BPM, slot 2 a bad magic, the rest empty */
    pos = song_hdr(blob[0], "TEST", 132, 7, 3);
    pos = song_sec(blob[0], pos, "INTRO", 16, 2, 0, "x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x.x.", 0);
    pos = song_sec(blob[0], pos, "VERSE", 32, 2, 0, "x.......x.......x.......x.......", "....X.......X.......X.......X...",
                   "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "............................FFFF");
    pos = song_sec(blob[0], pos, "OUTRO", 16, 2, SNG_F_OUTRO, "x...............", "........X.......", 0, 0);
    blob_n[0] = pos;
    {   /* slot 3: one section with lane marks: step 12's tom (lane 10) only during a fill, step 12's hat (lane 4)
         * not during one; step 0 plain */
        uint16_t fonly[16] = {0}, fno[16] = {0};
        const char *lane[DRUM_LANES] = {"x...............", 0, "....x.......x...", 0, "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0,
                                        "............x..."};
        fonly[12] = 1u << 10; fno[12] = 1u << 4;
        blob_n[3] = song_secm(blob[3], song_hdr(blob[3], "MARKS", 120, 0, 1), "ONE", 16, 2, 0, 0, lane, 0, fonly, fno);
    }
    {   /* slot 4: a 7/8 groove (14 sixteenths, meter byte 7 | 1 << 5) and a 4/4 section after it */
        const char *seven[DRUM_LANES] = {"x.....x.......", 0, "....x.....x...", 0, "x.x.x.x.x.x.x."};
        uint32_t p4 = song_hdr(blob[4], "SEVEN", 120, 0, 2);
        p4 = song_secm(blob[4], p4, "SEVEN", 14, 2, 0, 7u | 1u << 5, seven, 0, 0, 0);
        blob_n[4] = song_sec(blob[4], p4, "FOUR", 16, 2, 0, "x.......x.......", 0, 0, 0);
    }
    {   /* slot 6: a 2-bar VERSE with a fill bar of its own (16 steps: a tom run, lane 11, and a crash on its last
         * step, lane 12): a fill plays it instead of the pattern's last bar (steps 16..31) */
        const char *fb[DRUM_LANES] = {"x...x...x...x...", 0, "....x.......x...", 0, 0, 0, 0, 0, 0, 0, 0,
                                      "........x.x.x.x.", "...............X"};
        uint32_t p6 = song_hdr(blob[6], "FILLBAR", 120, 0, 2), s6 = p6;
        p6 = song_sec(blob[6], p6, "VERSE", 32, 2, 0, "x.......x.......x.......x.......", "....x.......x.......x.......x...",
                      "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", 0);
        p6 = song_fb(blob[6], s6, p6, 16, fb);
        blob_n[6] = song_sec(blob[6], p6, "PLAIN", 16, 2, 0, "x...x...x...x...", 0, 0, 0);
    }
    {   /* slot 7: entries and breaks (2026-10-18). ONE: a 1-bar groove with a break of its own (a hat on step 0, lane 4);
         * GROOVE: 3 bars (a kick, a snare, a hat on each bar's step 0: which bar plays) with a 4-bar entry (a crash on
         * step 0, lane 12; a low tom on step 62, lane 9) and no break; PLAIN: neither */
        const char *brk[DRUM_LANES] = {0, 0, 0, 0, "x..............."};
        const char *ent[DRUM_LANES] = {0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       "..............................................................x.",
                                       0, 0, "X..............................................................."};
        uint32_t p7 = song_hdr(blob[7], "BREAKS", 120, 0, 3), s7 = p7;
        p7 = song_sec(blob[7], p7, "ONE", 16, 2, 0, "x...x...x...x...", 0, 0, 0);
        p7 = song_blk(blob[7], s7, p7, SNG_F_BREAK, 16, brk);
        s7 = p7;
        p7 = song_sec(blob[7], p7, "GROOVE", 48, 2, 0, "x...............................................",
                      "................x...............................", "................................x...............", 0);
        p7 = song_blk(blob[7], s7, p7, SNG_F_ENTRY, 64, ent);
        blob_n[7] = song_sec(blob[7], p7, "PLAIN", 16, 2, 0, "x.x.x.x.x.x.x.x.", 0, 0, 0);
    }
    pos = song_hdr(blob[1], "SLOW", 100, 2, 1);
    pos = song_sec(blob[1], pos, "ONLY", 16, 2, 0, "x.x.x.x.x.x.x.x.", 0, 0, 0);
    blob_n[1] = pos;
    memcpy(blob[2], blob[1], pos); blob[2][0] ^= 1; blob_n[2] = pos;

    /* the packed form's checks */
    assert(sng_check(blob[0], blob_n[0], &tmp) == 0 && tmp.nsec == 3u && tmp.bpm == 132u && tmp.kit == 7u && !tmp.swing8);
    blob[0][23] = SNG_H_SWING8;                      /* the header's flags: the swing on the 8ths (a shuffle) */
    assert(sng_check(blob[0], blob_n[0], &tmp) == 0 && tmp.swing8 == SNG_H_SWING8);
    blob[0][23] = 0;
    /* (a step: 2 bytes of lanes and a byte a hit: INTRO 18 + 4 + 32 + 14 hits, VERSE 18 + 8 + 64 + 24 hits) */
    assert(tmp.off[0] == SNG_HDR && tmp.off[1] == SNG_HDR + 68u && tmp.off[2] == tmp.off[1] + 114u);
    assert(sng_check(blob[2], blob_n[2], &tmp) == 2);
    assert(sng_check(blob[0], blob_n[0] - 1u, &tmp) == 5);     /* truncated: the last section's size goes past it */
    assert(sng_check(blob[0], SNG_HDR + 8u, &tmp) == 4);
    {
        uint8_t bad[3800];
        memcpy(bad, blob[0], blob_n[0]);
        bad[22] = 0;  assert(sng_check(bad, blob_n[0], &tmp) == 3);
        bad[22] = 17; assert(sng_check(bad, blob_n[0], &tmp) == 3);   /* (16 sections at most) */
        bad[22] = 3;  bad[SNG_HDR + 12] = 0;  assert(sng_check(bad, blob_n[0], &tmp) == 5);
        bad[SNG_HDR + 12] = 16; bad[SNG_HDR + 13] = 9;  assert(sng_check(bad, blob_n[0], &tmp) == 5);
        bad[SNG_HDR + 13] = 2; bad[18] = (uint8_t)DRUM_KITS;  assert(sng_check(bad, blob_n[0], &tmp) == 3);
        bad[18] = 7; bad[SNG_HDR + 15] = 17;  assert(sng_check(bad, blob_n[0], &tmp) == 5);   /* a meter of 17 */
        bad[SNG_HDR + 15] = 0; bad[SNG_HDR + 16]--;  assert(sng_check(bad, blob_n[0], &tmp) == 6);   /* size short */
        bad[SNG_HDR + 18 + 4] |= 2;  bad[SNG_HDR + 16]++;  assert(sng_check(bad, blob_n[0], &tmp) == 6);   /* a hit more than the size */
        bad[SNG_HDR + 18 + 4] &= (uint8_t)~2u;  assert(sng_check(bad, blob_n[0], &tmp) == 0);
    }
    printf("song: packed form checked (offsets, magic, sizes, ranges) PASS\n");

    /* ---- PC 0 while stopped: the song is cued: tempo, kit, its INTRO in the drum track */
    assert(!song.playing && sng_cur == -1 && !sng_ready());
    pc(16, 0); advance(CTL);
    assert(sng_req == 1u && sng_cur == -1);
    sng_poll();
    assert(sng_cur == 0 && sng_ready() == 7u && song.g[G_BPM] == 132 && drum_kit() == 7u && !sng.pend);
    assert(TDRUM->p[P_SLEN] == 16 && lane_on(&TDRUM->dstep[0], 0) && !lane_on(&TDRUM->dstep[0], 2) &&
           lane_on(&TDRUM->dstep[4], 2) && live_sec == 0);
    assert(sng_bars(0) == 1u && sng_bars(1) == 2u && sng_bars(2) == 1u && sng_outro() == 2u);
    printf("song: PC 0 stopped: 132 BPM, kit 7, INTRO cued PASS\n");

    /* ---- CC 20 1: the VERSE starts the loop: 32 steps, its fill conditions, 2 bars */
    cc(16, 20, 1); advance(CTL * 3u);
    bar = bar_len();
    assert(song.playing && live_sec == 1 && TDRUM->p[P_SLEN] == 32 && jumped == t0);
    assert(step_fill(TDRUM, 28) == FC_FILL && step_fill(TDRUM, 27) == FC_NORM && lane_on(&TDRUM->dstep[4], 2));
    assert(((TDRUM->dstep[4].lvl[0] >> 4) & 3u) == LV_HARD);       /* lane 2: byte 0, bits 4..5 */
    printf("song: VERSE: 32 steps, FILL ONLY on 28..31, a hard snare PASS\n");

    /* ---- NEXT: OUTRO when the VERSE's 2 bars have played (asked for in its first bar: cued, no fill of its own,
     * the OUTRO on the bar after the second); NEXT again wraps to INTRO */
    advance(bar / 2u);
    cc(16, 24, 0); advance(bar);
    assert(live_sec == 1 && live_req == 2 && !fill_now);
    advance(bar);
    assert(live_sec == 2 && TDRUM->p[P_SLEN] == 16 && (jumped - t0) % bar < CTL && jumped - t0 >= 2u * bar);
    cc(16, 24, 0); advance(bar);
    assert(live_sec == 0);
    cc(16, 25, 0); advance(bar);
    assert(live_sec == 2);
    cc(16, 20, 5); advance(CTL * 2u);                              /* no section 5: ignored */
    assert(live_req == -1);
    printf("song: NEXT / PREV over the song's three sections, section 5 ignored PASS\n");

    /* ---- the ending with no value: the VERSE plays its 2 bars out (no fill), the flagged OUTRO once
     * (1 bar), then stop on the bar */
    cc(16, 20, 1); advance(bar);                                   /* back in the VERSE */
    assert(live_sec == 1);
    advance(bar / 3u);
    cc(16, 22, 127); advance(CTL * 2u);
    assert(!fill_now && live_req == 2 && pedal_end_req == 1u && pedal_at_end);
    advance(bar);
    assert(live_sec == 1 && !fill_now);
    advance(bar);
    assert(live_sec == 2 && song.playing && pedal_end_bars == 1u);
    advance(bar);
    assert(!song.playing && stopped - jumped >= bar && stopped - jumped < bar + CTL);
    printf("song: ending: the OUTRO once, stop on the bar PASS\n");

    /* ---- the ending naming the 2-bar VERSE: two bars, then stop */
    cc(16, 20, 0); advance(CTL * 3u);
    advance(bar / 2u);
    cc(16, 22, 1); advance(bar);
    assert(live_sec == 1 && pedal_end_bars == 2u);
    advance(bar);
    assert(song.playing);
    advance(bar);
    assert(!song.playing && stopped - jumped >= 2u * bar && stopped - jumped < 2u * bar + CTL);
    printf("song: ending with the 2-bar VERSE as the outro PASS\n");

    /* ---- the ending while the flagged OUTRO plays already (the reported case): it plays to its end, then stop */
    cc(16, 20, 2); advance(CTL * 3u);                              /* the OUTRO (1 bar) from stopped */
    assert(song.playing && live_sec == 2);
    advance(bar / 3u);
    cc(16, 22, 127); advance(CTL * 2u);
    assert(live_req == -1 && pedal_at_end && pedal_end_bars == 1u);
    advance(bar);
    assert(!song.playing && stopped - t0 >= bar && stopped - t0 < bar + CTL);
    printf("song: the ending while the OUTRO plays: to its end, then stop PASS\n");

    /* ---- PC while playing: the song waits for the next section; its tempo comes with it */
    cc(16, 20, 0); advance(CTL * 3u);
    assert(song.playing && song.g[G_BPM] == 132);
    pc(16, 1); advance(CTL); sng_poll();
    assert(sng_cur == 1 && sng.pend && song.g[G_BPM] == 132 && TDRUM->p[P_SLEN] == 16 && lane_on(&TDRUM->dstep[4], 2));
    advance(bar / 4u);
    cc(16, 20, 0); advance(bar);
    assert(live_sec == 0 && song.g[G_BPM] == 100 && drum_kit() == 2u && !sng.pend && !lane_on(&TDRUM->dstep[4], 2) &&
           lane_on(&TDRUM->dstep[2], 0));
    printf("song: PC while playing: SLOW's tempo and kit with its first section PASS\n");

    /* ---- (review) a request pending when another song lands is dropped; a request the song has no section for
     * is dropped at the bar; a PC on the pedal channel takes nothing else (CC 64 passes to the tracks) */
    bar = bar_len();                                               /* (SLOW's tempo) */
    advance(bar / 4u);
    cc(16, 20, 0); advance(CTL);                                   /* SLOW has one section, the one playing: nothing */
    assert(live_req == -1 && !pedal_fill && !pedal_fill_wait);
    live_req = 0;                                                  /* (as the UI asks for it: SLOW's 0..) */
    pc(16, 0); advance(CTL); sng_poll();                           /* ..but TEST lands: the request was SLOW's */
    assert(sng_cur == 0 && live_req == -1 && sng.pend);
    live_req = 7;                                                  /* (as the UI or a chain could ask: no section 7) */
    advance(bar);
    assert(live_req == -1 && live_sec == 0 && sng.pend);           /* dropped at the bar, nothing reset */
    cc(16, 20, 1); advance(bar);
    assert(live_sec == 1 && !sng.pend && song.g[G_BPM] == 132);
    bar = bar_len();                                               /* (TEST's tempo) */
    cc(16, 64, 127); advance(CTL * 2u);                            /* sustain on the pedal channel: not the pedal's */
    assert(live_req == -1 && song.playing);
    printf("song: a request dropped when another song lands or has no such section; CC 64 passes PASS\n");

    /* ---- (review) a project loaded over the song unloads it */
    sng_unload();
    assert(sng_cur == -1 && !sng_ready());
    pc(16, 0); advance(CTL); sng_poll();
    assert(sng_cur == 0);
    cc(16, 20, 0); advance(bar);

    /* ---- a bad slot and an empty slot leave the song alone; PC 127 unloads: no song, the drum track keeps the
     * section's pattern, a section asked for is ignored (no sections without a song since 2026-10-27) */
    pc(16, 2); advance(CTL); sng_poll();
    assert(sng_cur == 0);                                          /* (TEST stays loaded) */
    pc(16, 5); advance(CTL); sng_poll();
    assert(sng_cur == 0);
    pc(16, 127); advance(CTL); sng_poll();
    assert(sng_cur == -1 && sng_req == 0 && !sng_ready() && song.playing);
    bar = bar_len();
    advance(bar / 4u);
    cc(16, 20, 1); advance(bar);                                   /* a section: nothing to ask for */
    assert(live_req == -1 && song.playing && TDRUM->p[P_PAN] == 36 && !lane_on(&TDRUM->dstep[2], 0) &&
           lane_on(&TDRUM->dstep[4], 2));                           /* (INTRO's pattern stays in the drum track) */
    cc(16, 23, 0); advance(CTL * 2u);
    assert(!song.playing);
    printf("song: bad / empty slot ignored, PC 127 unloads (no sections, the pattern stays) PASS\n");

    /* ---- (lane marks) a section whose hits are marked one by one: the masks reach the sequencer, the lanes
     * follow them in and out of a fill; a plain section clears them */
    pc(16, 3); advance(CTL); sng_poll();
    assert(sng_cur == 3 && drum_fonly[12] == (1u << 10) && drum_fno[12] == (1u << 4) && !drum_fonly[0] && !drum_fno[0]);
    fill_now = 0;
    assert(drum_fill_mask(12) == (1u << 10) && drum_fill_mask(0) == 0);    /* normal play: the tom stays out */
    fill_now = 1;
    assert(drum_fill_mask(12) == (1u << 4));                                /* a fill: the hat stays out, the tom in */
    fill_now = 0;
    assert(sng_check(blob[3], blob_n[3], &tmp) == 0 && tmp.off[0] == SNG_HDR);
    assert(sng_check(blob[3], blob_n[3] - 1u, &tmp) == 5);                  /* the marks counted in the size */
    assert(lane_on(&TDRUM->dstep[12], 10) && !lane_on(&TDRUM->dstep[13], 10));
    {   /* CC 21 in this one-bar section (its tom only during a fill, on step 12): asked on step 2, the rest of this
         * bar is the fill (the tom plays), over at the bar line; asked on step 13 (the bar's last beat), the whole
         * next bar (an arranger's fill-in, 2026-10-15) */
        uint32_t bar = bar_len();
        cc(16, 20, 0); advance(CTL * 2u);
        advance(bar + bar / 8u);                                            /* the second cycle, step 2 */
        cc(16, 21, 127); advance(CTL * 2u);
        assert(pedal_fill && !pedal_fill_wait && pedal_fill_bars == 1u && fill_now);   /* this bar */
        while (TDRUM->seq_idx != 1u)
            advance(CTL);
        assert(!pedal_fill && !fill_now);                                   /* over at the bar line */
        while (TDRUM->seq_idx != 13u)                                       /* the last beat (steps 12..15) */
            advance(CTL);
        cc(16, 21, 127); advance(CTL * 2u);
        assert(!pedal_fill && pedal_fill_wait);                             /* not this bar */
        while (TDRUM->seq_idx != 1u)
            advance(CTL);
        assert(pedal_fill && !pedal_fill_wait && fill_now);                 /* the next bar, whole */
        advance(bar);
        assert(!pedal_fill && !fill_now);                                   /* over at the bar line */
        cc(16, 23, 0); advance(bar * 2u);
    }
    pc(16, 0); advance(CTL); sng_poll();                                    /* a song without marks: cleared */
    assert(sng_cur == 0 && !drum_fonly[12] && !drum_fno[12]);
    printf("song: lane marks: only-during and not-during a fill, per lane and step; cleared by a plain section PASS\n");

    /* ---- a section in 7/8: its bar is 14 sixteenths. A section asked for lands on its bar (the bar line inside a
     * beat: 3.5 beats after the start), the bars count in its meter, the click ticks eighths with the bar's first
     * accented; the 4/4 section after it is 4/4 again */
    cc(16, 23, 0); advance(CTL * 2u);
    pc(16, 4); advance(CTL); sng_poll();
    assert(sng_cur == 4 && clk_meter == (7u | 1u << 5) && sng_bars(0) == 1u && sng_bars(1) == 1u && meter_b16(clk_meter) == 14u);
    {
        uint32_t beat = 60u * FS / 120u, b78 = beat * 7u / 2u, clicks = 0, i, start;
        song.g[G_CLOCK] = CLICK_ON;
        cc(16, 20, 0); advance(CTL * 2u);
        start = t0;
        assert(song.playing && live_sec == 0);
        for (i = 0; i < 2u * b78 / CTL; i++) {                      /* two 7/8 bars: 14 eighths clicked */
            uint32_t c = click_last;
            advance(CTL);
            clicks += click_last != c;
        }
        assert(clicks >= 13u && clicks <= 15u && live_bar == 1u);
        cc(16, 20, 1);                                              /* FOUR: on the end of SEVEN (a 1-bar pattern) */
        advance(b78 + CTL * 2u);
        assert(live_sec == 1 && clk_meter == 0u);
        assert(jumped > start + b78 && (jumped - start) % b78 < 2u * CTL);   /* on a 7/8 bar line (not a 4/4 one) */
        song.g[G_CLOCK] = 0;
    }
    cc(16, 23, 0); advance(CTL * 2u);
    pedal.count = 1;                                                /* the count-in: one bar of the 7/8 section */
    cc(16, 20, 0); advance(CTL);
    assert(ci_on && ci_pedal == 7u && ci_meter == (7u | 1u << 5));
    advance(7u * 30u * FS / 120u + CTL * 2u);                       /* 7 eighths at 120 */
    assert(song.playing && !ci_on && live_sec == 0);
    pedal.count = 0;
    cc(16, 23, 0); advance(CTL * 2u);
    printf("song: a 7/8 section: its bars, the click's eighths, the count-in, then 4/4 again PASS\n");

    /* ---- a fill bar of its own (2026-10-15): checked, loaded beside the pattern, played by a fill instead of the bar
     * the fill is in (its step i at the bar's step i), kept by the section save; a section without one clears it */
    {
        static uint8_t bad[3800];
        uint32_t fb, bar, p1 = SNG_HDR + 18u + 8u + 64u + 24u;   /* VERSE: 18 + 8 + 2 x 32 steps + 24 hits; then the fill bar */
        assert(sng_check(blob[6], blob_n[6], &tmp) == 0 && tmp.nsec == 2u && blob[6][SNG_HDR + 14] == SNG_F_FILLBAR);
        assert(blob[6][p1] == 16u && tmp.off[1] == p1 + 1u + 32u + 11u);   /* 16 steps: 2 bytes each, 11 hits */
        memcpy(bad, blob[6], blob_n[6]);
        bad[p1] = 0;  assert(sng_check(bad, blob_n[6], &tmp) == 7);        /* no steps */
        bad[p1] = 65; assert(sng_check(bad, blob_n[6], &tmp) == 7);        /* too many */
        bad[p1] = 17; assert(sng_check(bad, blob_n[6], &tmp) == 7);        /* 17 steps: past the section's size */
        bad[p1] = 15; assert(sng_check(bad, blob_n[6], &tmp) == 6);        /* 15: the size is not met */
        bad[p1] = 16; bad[SNG_HDR + 14] = 0; assert(sng_check(bad, blob_n[6], &tmp) == 6);   /* no flag: bytes left over */
        cc(16, 23, 0); advance(CTL * 2u);
        pc(16, 6); advance(CTL); sng_poll();
        assert(sng_cur == 6 && fb_len == 16u && fb_bar == 16u && fb_bars == 1u && TDRUM->p[P_SLEN] == 32 && lane_on(&fb_dstep[8], 11) && !lane_on(&fb_dstep[7], 11) &&
               lane_on(&fb_dstep[15], 12) && dstep_lvl(&fb_dstep[15], 12) == LV_HARD && lane_on(&fb_dstep[0], 0) && !lane_on(&fb_dstep[0], 4));
        fill_now = 0;
        assert(drum_src(TDRUM, 28, (28) % trk_len(TDRUM), &fb) == &TDRUM->dstep[28] && !fb);     /* no fill: the pattern */
        fill_now = 1;
        assert(drum_src(TDRUM, 15, (15) % trk_len(TDRUM), &fb) == &fb_dstep[15] && fb);          /* a fill: the fill bar at the bar's own step */
        assert(drum_src(TDRUM, 16, (16) % trk_len(TDRUM), &fb) == &fb_dstep[0] && fb);           /* (the second bar the same) */
        assert(drum_src(TDRUM, 31, (31) % trk_len(TDRUM), &fb) == &fb_dstep[15] && fb && drum_fill_mask(NSTEP) == 0u);
        fill_now = 0;
        bar = bar_len();
        cc(16, 20, 0); advance(CTL * 3u);                                  /* the VERSE from stopped */
        assert(song.playing && live_sec == 0);
        advance(bar / 2u);
        cc(16, 21, 127); advance(CTL * 2u);                                /* CC 21 in bar 0, beat 3: the fill to its end */
        assert(pedal_fill && fill_now && pedal_fill_bars == 1u);
        while (TDRUM->seq_idx != 8u)                                       /* the bar's third beat: the fill bar's step 8, the tom */
            advance(CTL);
        assert(fill_now && drum_src(TDRUM, 8, (8) % trk_len(TDRUM), &fb) == &fb_dstep[8] && fb && !TDRUM->seq_skip);
        while (TDRUM->seq_idx != 15u)
            advance(CTL);
        assert(fill_now && drum_src(TDRUM, 15, (15) % trk_len(TDRUM), &fb) == &fb_dstep[15] && fb);   /* ..its last step: the crash */
        while (TDRUM->seq_idx != 2u)                                       /* over at the bar line: the pattern from its top */
            advance(CTL);
        assert(!pedal_fill && !fill_now && live_bar == 0 && drum_src(TDRUM, 2, (2) % trk_len(TDRUM), &fb) == &TDRUM->dstep[2] && !fb);
        {   /* a fill bar of two bars (32 steps) in this 2-bar section, ending on its last bar line (2026-10-20; before, it
             * ran two bars from the press, past the end): asked in bar 1, its last bar from now (cut from the front);
             * asked in bar 0, both; then the pattern from its top */
            fb_len = 32; fb_bar = 16; fb_bars = 2;                         /* (as sng_apply would set them) */
            while (TDRUM->seq_idx != 20u)
                advance(CTL);
            cc(16, 21, 127); advance(CTL * 2u);
            assert(pedal_fill && pedal_fill_bars == 1u && !pedal_fill_wait && drum_src(TDRUM, 20, (20) % trk_len(TDRUM), &fb) == &fb_dstep[20] && fb);
            while (TDRUM->seq_idx != 2u)
                advance(CTL);
            assert(!pedal_fill && live_bar == 0 && drum_src(TDRUM, 2, (2) % trk_len(TDRUM), &fb) == &TDRUM->dstep[2] && !fb);
            while (TDRUM->seq_idx != 4u)
                advance(CTL);
            cc(16, 21, 127); advance(CTL * 2u);
            assert(pedal_fill && pedal_fill_bars == 2u && drum_src(TDRUM, 4, (4) % trk_len(TDRUM), &fb) == &fb_dstep[4] && fb);
            while (TDRUM->seq_idx != 18u)
                advance(CTL);
            assert(pedal_fill && pedal_fill_bars == 1u && drum_src(TDRUM, 18, (18) % trk_len(TDRUM), &fb) == &fb_dstep[18] && fb);
            while (TDRUM->seq_idx != 2u)
                advance(CTL);
            assert(!pedal_fill && live_bar == 0 && drum_src(TDRUM, 2, (2) % trk_len(TDRUM), &fb) == &TDRUM->dstep[2] && !fb);
            /* three bars (48 steps), longer than the pattern: the first section end it reaches whole, bar 4, so
             * asked in bar 0 it waits a bar, then plays whole over the cycle going round (no restart mid-fill) */
            fb_len = 48; fb_bars = 3;
            while (TDRUM->seq_idx != 4u)
                advance(CTL);
            cc(16, 21, 127); advance(CTL * 2u);
            assert(!pedal_fill && pedal_fill_wait == 1u);
            while (TDRUM->seq_idx != 18u)
                advance(CTL);
            assert(pedal_fill && pedal_fill_bars == 3u && !pedal_fill_wait && drum_src(TDRUM, 18, (18) % trk_len(TDRUM), &fb) == &fb_dstep[2] && fb);
            while (TDRUM->seq_idx != 2u)
                advance(CTL);
            assert(pedal_fill && pedal_fill_bars == 2u && live_bar == 2 && drum_src(TDRUM, 2, (2) % trk_len(TDRUM), &fb) == &fb_dstep[18] && fb);
            while (TDRUM->seq_idx != 18u)
                advance(CTL);
            assert(pedal_fill && pedal_fill_bars == 1u && drum_src(TDRUM, 18, (18) % trk_len(TDRUM), &fb) == &fb_dstep[34] && fb);
            while (TDRUM->seq_idx != 2u)
                advance(CTL);
            assert(!pedal_fill && live_bar == 0 && drum_src(TDRUM, 2, (2) % trk_len(TDRUM), &fb) == &TDRUM->dstep[2] && !fb);
            fm1_irq_off(); sng_apply(0); fm1_irq_on();                     /* (the real fill bar back) */
            assert(fb_len == 16u && fb_bars == 1u);
        }
        cc(16, 20, 1); advance(bar * 2u + CTL * 4u);                       /* PLAIN, no fill bar of its own */
        assert(live_sec == 1 && fb_len == 0u);
        cc(16, 23, 0); advance(CTL * 2u);
        {   /* the section save keeps the fill bar as it is (the pattern edited) */
            static uint8_t out[SNG_MAX];
            uint32_t n;
            fm1_irq_off(); sng_apply(0); fm1_irq_on();
            dstep_set(&TDRUM->dstep[1], 3, LV_NORM, 0);                    /* a clap on step 1 */
            n = sng_pack_section(0, out, sizeof out);
            assert(n == blob_n[6] + 1u && !sng_check(out, n, &tmp) && (out[SNG_HDR + 14] & SNG_F_FILLBAR) &&
                   !memcmp(out + p1 + 1u, blob[6] + p1, 1u + 32u + 11u));
            assert(!sng_pack_section(0, out, p1 + 1u + 20u));             /* no room for the fill bar: refused */
        }
        pc(16, 0); advance(CTL); sng_poll();
        assert(sng_cur == 0 && fb_len == 0u);
    }
    printf("song: a fill bar of its own: checked, loaded, played by a fill for the pattern's last bar, saved back, cleared PASS\n");

    /* ---- entries and breaks (2026-10-18): bars before a section's pattern, the pattern from its top after them; the
     * entry from another section or from stopped, the break on CC 28 (the section playing too); none written: the
     * entry, then a bar of rest; a fill cuts one; a stop ends it */
    {
        static uint8_t bad[3800], out[SNG_MAX];
        uint32_t fb, bar = 0, p1 = SNG_HDR + 18u + 4u + 32u + 4u;   /* ONE: 18 + 4 + 16 steps + 4 hits; then its break */
        assert(sng_check(blob[7], blob_n[7], &tmp) == 0 && tmp.nsec == 3u && blob[7][SNG_HDR + 14] == SNG_F_BREAK &&
               blob[7][tmp.off[1] + 14] == SNG_F_ENTRY && blob[7][p1] == 16u && tmp.off[1] == p1 + 1u + 32u + 1u);
        memcpy(bad, blob[7], blob_n[7]);
        bad[p1] = 0;  assert(sng_check(bad, blob_n[7], &tmp) == 7);       /* no steps */
        bad[p1] = 16; bad[SNG_HDR + 14] = SNG_F_ENTRY | SNG_F_BREAK; assert(sng_check(bad, blob_n[7], &tmp) == 7);   /* a
                                                                             * block too many: past the size */
        bad[SNG_HDR + 14] = 0; assert(sng_check(bad, blob_n[7], &tmp) == 6);   /* none: bytes left over */
        cc(16, 23, 0); advance(CTL * 2u);
        pc(16, 7); advance(CTL); sng_poll();
        assert(sng_cur == 7 && eb_len == 0u);
        for (k = 0; k < 3u; k++) {                                     /* each section packed again: the same bytes */
            fm1_irq_off(); sng_apply(k); fm1_irq_on();
            assert(sng_pack_section(k, out, sizeof out) == blob_n[7] && !memcmp(out, blob[7], blob_n[7]));
        }
        fm1_irq_off(); sng_apply(0); live_sec = 0; fm1_irq_on();
        bar = bar_len();
        /* GROOVE from stopped: its 4-bar entry, then the pattern from its top (Shine On: 4 bars between two 3-bar
         * phrases) */
        cc(16, 20, 1); advance(CTL * 3u);
        assert(song.playing && live_sec == 1 && eb_len == 64u && lane_on(&eb_dstep[0], 12) && lane_on(&eb_dstep[62], 9) &&
               !lane_on(&eb_dstep[0], 0));
        assert(drum_src(TDRUM, 0, (0) % trk_len(TDRUM), &fb) == &eb_dstep[0] && fb && drum_src(TDRUM, 63, (63) % trk_len(TDRUM), &fb) == &eb_dstep[63] && fb);
        assert(drum_src(TDRUM, 64, (64) % trk_len(TDRUM), &fb) == &TDRUM->dstep[16] && !fb);   /* (bar 4: the pattern's bar 1, the snare) */
        cc(16, 20, 1); advance(CTL * 2u);                              /* the section playing again: nothing */
        assert(live_req < 0);
        while (TDRUM->seq_abs != 16u)
            advance(CTL);
        assert(lane_on(drum_src(TDRUM, TDRUM->seq_abs, (TDRUM->seq_abs) % trk_len(TDRUM), &fb), 12) == 0 && fb && !TDRUM->seq_skip);   /* (the entry's
                                                                         * bar 1: rest, not the pattern's snare) */
        assert(eb_bars == 4u && live_sec_bars() == 4u);                 /* (meanwhile the section's bars are the entry's) */
        while (eb_len)
            advance(CTL);
        assert(live_bar == 0u && eb_bars == 0u && live_sec_bars() == 3u && now - t0 >= 4u * bar - CTL && now - t0 < 4u * bar + 2u * CTL);
        advance(CTL * 4u);
        assert(TDRUM->seq_abs < 4u && drum_src(TDRUM, TDRUM->seq_abs, TDRUM->seq_idx, &fb) == &TDRUM->dstep[TDRUM->seq_idx] && !fb);   /* (the
                                                                         * pattern's top, after 4 bars) */
        /* CC 28 127, GROOVE playing: its break, none: its entry, at the section's end (3 bars after the entry) */
        cc(16, 28, 127); advance(CTL * 2u);
        assert(live_req == 1 && live_req_brk && pedal_at_end && !strcmp(sng_ui_next, "GROOVE"));
        while (live_req >= 0)
            advance(CTL);
        assert(live_sec == 1 && live_bar == 0u && eb_len == 64u && !live_req_brk);
        assert(jumped - t0 >= 7u * bar - 2u * CTL && jumped - t0 < 7u * bar + 2u * CTL);
        /* a section asked for during it lands at its end, its 4 bars (not at the 3-bar pattern's) */
        advance(bar / 2u);
        cc(16, 20, 0); advance(CTL * 2u);
        assert(live_req == 0 && pedal_at_end);
        {
            uint32_t from = jumped;
            while (live_req >= 0)
                advance(CTL);
            assert(live_sec == 0 && jumped - from >= 4u * bar - 2u * CTL && jumped - from < 4u * bar + 2u * CTL);
        }
        cc(16, 20, 1); advance(CTL * 2u);
        while (live_req >= 0)
            advance(CTL);
        while (eb_len)
            advance(CTL);
        advance(bar / 2u);
        /* ONE (no entry): the pattern at once; CC 28 0 then its break on its next bar (a 1-bar section) */
        cc(16, 27, 0);  advance(CTL * 2u);
        while (live_req >= 0)
            advance(CTL);
        assert(live_sec == 0 && eb_len == 0u);
        cc(16, 28, 0); advance(CTL * 2u);
        assert(live_req == 0 && live_req_brk);
        while (live_req >= 0)
            advance(CTL);
        assert(live_sec == 0 && eb_len == 16u && lane_on(&eb_dstep[0], 4) && !lane_on(&eb_dstep[0], 0));
        advance(bar / 2u);
        assert(drum_src(TDRUM, TDRUM->seq_abs, (TDRUM->seq_abs) % trk_len(TDRUM), &fb) == &eb_dstep[TDRUM->seq_abs] && fb);
        advance(bar / 2u + CTL * 4u);
        assert(eb_len == 0u && TDRUM->seq_abs < 8u && drum_src(TDRUM, TDRUM->seq_abs, (TDRUM->seq_abs) % trk_len(TDRUM), &fb) == &TDRUM->dstep[TDRUM->seq_abs % 16u] && !fb);
        /* the fill and the break on one switch: ONE has no fill bar (its fill to its end, one bar), then PLAIN, which has
         * no break nor entry: a bar of rest */
        cc(16, 21, 127); cc(16, 28, 2); advance(CTL * 2u);
        assert(pedal_fill && live_req == 2 && live_req_brk && !pedal_at_end);
        while (live_req >= 0)
            advance(CTL);
        assert(live_sec == 2 && eb_len == 16u && !pedal_fill);
        for (k = 0; k < 16u; k++)
            assert(!dstep_mask(&eb_dstep[k]) && !eb_flam[k]);
        /* GROOVE through its entry, a fill in it: the fill cuts it */
        cc(16, 20, 1);  advance(CTL * 2u);
        while (live_req >= 0)
            advance(CTL);
        assert(live_sec == 1 && eb_len == 64u);
        advance(bar + bar / 4u);
        cc(16, 21, 127); advance(CTL * 2u);
        assert(pedal_fill && eb_len == 0u && drum_src(TDRUM, TDRUM->seq_abs, (TDRUM->seq_abs) % trk_len(TDRUM), &fb) == &TDRUM->dstep[TDRUM->seq_idx]);
        /* a stop ends it; CC 28 127 stopped: the section last played (or cued) starts through its break (here the
         * entry), after the count-in */
        cc(16, 23, 0); advance(CTL * 2u);
        assert(!song.playing && eb_len == 0u && !live_req_brk);
        pedal.count = 1;
        cc(16, 28, 127); advance(CTL * 2u);
        assert(ci_on && live_req == 1 && live_req_brk);
        advance(bar + CTL * 4u);
        assert(song.playing && !ci_on && live_sec == 1 && eb_len == 64u);
        pedal.count = 0;
        cc(16, 23, 0); advance(CTL * 2u);
        /* the ending naming GROOVE, an outro with a 4-bar entry (the user, 2026-10-28: the stop came as the entry
         * ended, the pattern unheard): ONE playing, CC 22 1: ONE to its end, GROOVE's entry, its pattern once (3 bars),
         * the stop on the bar: 7 bars from the landing */
        {
            uint32_t bl = bar_len();
            cc(16, 20, 0); advance(CTL * 3u);
            assert(song.playing && live_sec == 0 && eb_len == 0u);   /* ONE from stopped: no entry written */
            advance(bl / 3u);
            cc(16, 22, 1); advance(CTL * 2u);
            assert(live_req == 1 && pedal_end_req == 3u && pedal_at_end && !pedal_end_bars);
            advance(bl);                                               /* GROOVE at ONE's end: its entry first */
            assert(live_sec == 1 && eb_bars == 4u && pedal_end_eb && !pedal_end_bars && pedal_end_req == 3u);
            advance(4u * bl);                                          /* the entry over: the pattern, its bars counted */
            assert(song.playing && live_sec == 1 && !eb_bars && !pedal_end_eb && pedal_end_bars == 3u && !pedal_end_req);
            advance(2u * bl);
            assert(song.playing);
            advance(bl);
            assert(!song.playing && stopped - jumped + CTL >= 7u * bl && stopped - jumped < 7u * bl + CTL);
            printf("song: the ending with an outro that has an entry: the entry, the pattern once, then the stop PASS\n");
            /* the same, a fill during the entry: the fill cuts it and pulls the stop to the fill's end (as any ending
             * with a fill pending), the pattern not played */
            cc(16, 20, 0); advance(CTL * 3u);
            advance(bl / 3u);
            cc(16, 22, 1); advance(CTL * 2u);
            advance(bl + bl / 4u);
            assert(live_sec == 1 && eb_bars == 4u && pedal_end_eb);
            cc(16, 21, 127); advance(CTL * 2u);
            assert(pedal_fill && !eb_bars && !pedal_end_eb && pedal_end_bars && !pedal_end_req);
            advance(4u * bl);
            assert(!song.playing);
            printf("song: the ending with an outro that has an entry, a fill in the entry: the stop at the fill's end PASS\n");
        }
        /* no song (PC 127): CC 20 and CC 28 ask for nothing (no sections), the pattern loops as it is */
        pc(16, 127); advance(CTL); sng_poll();
        assert(sng_cur == -1);
        cc(16, 20, 0);  advance(CTL * 2u);
        assert(live_req == -1 && !song.playing);
        transport_req = 1; advance(CTL * 2u);
        assert(song.playing && eb_len == 0u);
        cc(16, 28, 127); advance(CTL * 2u);
        assert(live_req == -1 && !live_req_brk && eb_len == 0u);
        cc(16, 23, 0); advance(CTL * 2u);
        pc(16, 0); advance(CTL); sng_poll();
        assert(sng_cur == 0);
    }
    /* a fill right after a break (asked 2026-10-18): CC 21 value 1 in its last bar (or a plain one in its last beat): the
     * break plays out, the next bar is the fill (from the pattern's top), then the pattern */
    {
        uint32_t fb, bar;
        cc(16, 23, 0); advance(CTL * 2u);
        pc(16, 7); advance(CTL); sng_poll();
        bar = bar_len();                                               /* (the song's tempo) */
        cc(16, 20, 1); advance(CTL * 3u);                              /* GROOVE from stopped: its 4-bar entry */
        assert(song.playing && live_sec == 1 && eb_bars == 4u);
        advance(3u * bar + bar / 4u);                                  /* its last bar */
        assert(live_bar == 3u && eb_len);
        cc(16, 21, 1); advance(CTL * 2u);
        assert(pedal_fill_wait && !pedal_fill && eb_len == 64u);      /* (the break plays out) */
        while (eb_len)
            advance(CTL);
        assert(pedal_fill && fill_now && live_bar == 0u && pedal_fill_bars == 3u && now - t0 >= 4u * bar - CTL);   /* no fill bar
                                                                         * of its own: fill mode to the section's end, the
                                                                         * whole cycle from its top (not 2: the break's bars) */
        cc(16, 23, 0); advance(CTL * 2u);
        pc(16, 6); advance(CTL); sng_poll();                          /* VERSE: a fill bar of its own, no break: a bar of rest */
        cc(16, 28, 127); advance(CTL * 3u);
        assert(song.playing && live_sec == 0 && eb_bars == 1u && fb_len == 16u);
        advance(bar / 2u + bar / 4u + bar / 8u);                       /* its last beat: a plain CC 21 */
        cc(16, 21, 127); advance(CTL * 2u);
        assert(pedal_fill_wait && eb_len);
        while (eb_len)
            advance(CTL);
        assert(pedal_fill && pedal_fill_bars == 1u && live_bar == 0u);
        advance(CTL * 4u);
        assert(drum_src(TDRUM, TDRUM->seq_abs, TDRUM->seq_idx, &fb) == &fb_dstep[TDRUM->seq_idx % 16u] && fb);   /* the fill
                                                                         * bar, at the section's top */
        while (pedal_fill)
            advance(CTL);
        advance(CTL * 4u);
        assert(live_bar == 0u && TDRUM->seq_abs < 8u && drum_src(TDRUM, TDRUM->seq_abs, TDRUM->seq_idx, &fb) == &TDRUM->dstep[TDRUM->seq_idx] && !fb);
        cc(16, 23, 0); advance(CTL * 2u);
        pc(16, 0); advance(CTL); sng_poll();
    }
    printf("song: a fill right after a break: the break plays out, the fill from the pattern's top (fill mode for the "
           "cycle, or the fill bar), then the pattern PASS\n");
    printf("song: entries and breaks: checked, saved back, from stopped and from another section, never a re-press, the "
           "pattern from its top after them (a change waits for their end), CC 28 (the section playing, fallback, a bar of rest, with a fill, from stopped), a fill "
           "cuts one, none without a song PASS\n");

    /* ---- the pedal settings object */
    {
        arrset_t a;
        arrset_fill(&a);
        assert(arrset_valid(&a) && a.pedal_ch == 16u && a.count_in == 0u && sizeof a == 16u);
        a.pedal_ch = 17; a.count_in = 2; arrset_take(&a);
        assert(pedal.ch == 17u && pedal.count == 2u);
        a.pedal_ch = 18; assert(!arrset_valid(&a));
        a.pedal_ch = 16; a.count_in = 3; assert(!arrset_valid(&a));
        a.count_in = 0; arrset_take(&a);
        assert(pedal.ch == 16u && pedal.count == 0u && !sng_autoload);
        /* SETUP > PEDAL: the last song loaded at start; kept in the object's spare bytes (0 in the tool's and the
         * editor's: off) */
        sng_autoload = 1; sng_last = 0; arrset_dirty = 0;
        transport_req = 2; advance(CTL);
        pc(16, 0); advance(CTL); sng_poll();
        assert(sng_cur == 0 && sng_last == 1u && arrset_dirty);
        arrset_fill(&a);
        assert(a.autoload == 1u && a.last == 1u && arrset_valid(&a));
        sng_autoload = 0; sng_last = 0; arrset_take(&a);
        assert(sng_autoload == 1u && sng_last == 1u);
        a.last = SNG_ALL; arrset_take(&a);                         /* (a groove: last 17..32) */
        assert(sng_last == SNG_ALL);
        a.last = SNG_ALL + 1u; a.autoload = 7; arrset_take(&a);    /* (32 songs: last 1..32) */
        assert(!sng_autoload && !sng_last);
        sng_autoload = 0; arrset_dirty = 0;
    }
    printf("song: the pedal settings object PASS\n");

    /* ---- the factory grooves: songs 16..31, PC 16..31 (groove 2 here: the test song); PC 32 and on: no song */
    memcpy(blob[SNG_FAC0 + 1u], blob[0], blob_n[0]); blob_n[SNG_FAC0 + 1u] = blob_n[0];
    sng_autoload = 1; sng_last = 0;
    pc(16, 17); advance(CTL); sng_poll();
    assert(sng_cur == (int8_t)(SNG_FAC0 + 1u) && sng_ready() == 7u && sng_last == 18u);
    pc(16, 16); advance(CTL); sng_poll();
    assert(sng_cur == (int8_t)(SNG_FAC0 + 1u));                    /* (groove 1: none here, the song stays) */
    pc(16, 32); advance(CTL); sng_poll();
    assert(sng_cur == -1 && !sng_ready());
    blob_n[SNG_FAC0 + 1u] = 0; sng_autoload = 0; sng_last = 0; arrset_dirty = 0;
    printf("song: PC 16..31 the grooves, PC 32 no song PASS\n");

    /* ---- EDIT, SAVE held: a section packed again from the drum track (sng_pack_section, the reverse of sng_apply) */
    {
        static uint8_t out[SNG_MAX];
        uint32_t n0, n, k, slot;
        sng_t tmp;
        transport_req = 2; advance(CTL);
        for (slot = 0; slot < SNG_SLOTS; slot++) {   /* every song the test wrote: each section, unedited, packs the same */
            const uint8_t *src = sng_fetch(slot, &n0);
            if (!src || sng_check(src, n0, &tmp))
                continue;
            pc(16, slot); advance(CTL); sng_poll();
            assert(sng_cur == (int8_t)slot);
            for (k = 0; k < sng.nsec; k++) {
                fm1_irq_off(); sng_apply(k); fm1_irq_on();
                n = sng_pack_section(k, out, sizeof out);
                assert(n == n0 && !memcmp(out, src, n));
            }
        }
        pc(16, 0); advance(CTL); sng_poll();
        fm1_irq_off(); sng_apply(1); live_sec = 1; fm1_irq_on();
        undo_mark(TDRUM, 77); assert(undo.valid);
        fm1_irq_off(); sng_apply(1); fm1_irq_on();
        assert(!undo.valid);                          /* (a section applied: no undo back into the one before) */
        dstep_set(&TDRUM->dstep[2], 9, LV_HARD, 2);   /* an edit: a hard x3 hit, a mark taken off with its hit */
        drum_fonly[3] |= 1u << 6; dstep_set(&TDRUM->dstep[3], 6, LV_NORM, 0);
        drum_hit_clr(TDRUM, 3, 6);
        assert(!(drum_fonly[3] >> 6 & 1u) && !dstep_has(&TDRUM->dstep[3], 6));
        n = sng_pack_section(1, out, sizeof out);
        assert(n && !sng_check(out, n, &tmp));
        memcpy(blob[0], out, n); blob_n[0] = n;      /* (as st_save writes it), loaded again */
        pc(16, 0); advance(CTL); sng_poll();
        fm1_irq_off(); sng_apply(1); fm1_irq_on();
        assert(dstep_has(&TDRUM->dstep[2], 9) && dstep_lvl(&TDRUM->dstep[2], 9) == LV_HARD && dstep_rat(&TDRUM->dstep[2], 9) == 2u);
        assert(!sng_pack_section(1, out, 40));        /* no room: refused */
        {   /* the header as the knobs left it (2026-10-29): tempo, kit, swing, drum level and reverb; the name and
             * the swing grid kept */
            int16_t b0 = song.g[G_BPM], k0 = TDRUM->p[P_E0], s0 = song.g[G_SWING], l0 = song.g[G_DRLVL], r0 = song.g[G_DRREV];
            song.g[G_BPM] = 173; TDRUM->p[P_E0] = 3; song.g[G_SWING] = 61; song.g[G_DRLVL] = 99; song.g[G_DRREV] = 42;
            n = sng_pack_section(1, out, sizeof out);
            assert(n && !sng_check(out, n, &tmp) && tmp.bpm == 173u && tmp.kit == 3u && tmp.swing == 61u && tmp.drlvl == 99u && tmp.drrev == 42u &&
                   !memcmp(out + 4, sng.xip + 4, SNG_NAME) && out[23] == sng.xip[23]);
            song.g[G_BPM] = b0; TDRUM->p[P_E0] = k0; song.g[G_SWING] = s0; song.g[G_DRLVL] = l0; song.g[G_DRREV] = r0;
        }
        assert(!sng_lossy());
        TDRUM->micro[5] = 3; assert(sng_lossy()); TDRUM->micro[5] = 0;
        TDRUM->p[P_SLEN] = 64;                        /* the longest section: still packs and checks */
        n = sng_pack_section(1, out, sizeof out);
        assert(n && !sng_check(out, n, &tmp) && tmp.nsec == sng.nsec);
    }
    printf("song: a section packed again from the drum track (EDIT, SAVE held): same bytes, an edit kept, no room refused PASS\n");

    /* ---- the division changed on the device (2026-10-29, seq.c pattern_rediv): the hits keep their place on the beat,
     * the section's fill bar in RAM follows, and the save packs its fill bar and entry on the new grid (sng_pack_rediv) */
    {
        static uint8_t out[SNG_MAX];
        sng_t tmp;
        uint32_t p, e, n, q, size;
        const uint8_t *s;
        const char *fbl[DRUM_LANES] = {"x.x.x.x.x.x.x.x.", 0, "....x.......x...", 0, "xxxxxxxxxxxxxxxx"};   /* kick, snare, hat */
        const char *ebl[DRUM_LANES] = {"x...............", 0, ".X.X............"};
        transport_req = 2; advance(CTL);
        p = song_hdr(blob[8], "REDIV", 120, 0, 1);
        e = song_sec(blob[8], p, "VERSE", 32, 2, 0, "x...x...x...x...x...x...x...x...", "....x.......x.......x.......x...",
                     "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", 0);
        e = song_fb(blob[8], p, e, 16, fbl);
        e = song_blk(blob[8], p, e, SNG_F_ENTRY, 16, ebl);
        blob_n[8] = e;
        pc(16, 8); advance(CTL); sng_poll();
        fm1_irq_off(); sng_apply(0); live_sec = 0; fm1_irq_on();
        assert(sng_cur == 8 && fb_len == 16u && fb_bar == 16u && fb_bars == 1u && TDRUM->p[P_SDIV] == 2 && TDRUM->p[P_SLEN] == 32);
        assert(pattern_rediv_ok(TDRUM, 1) && pattern_rediv_ok(TDRUM, 3));   /* 16 steps of 1/8, 64 of 1/32: both fit */
        assert(pattern_rediv(TDRUM, 1) && TDRUM->p[P_SDIV] == 1 && TDRUM->p[P_SLEN] == 16);
        assert(lane_on(&TDRUM->dstep[0], 0) && lane_on(&TDRUM->dstep[2], 0) && lane_on(&TDRUM->dstep[2], 2) && !lane_on(&TDRUM->dstep[1], 2) &&
               lane_on(&TDRUM->dstep[1], 4) && lane_on(&TDRUM->dstep[15], 4) && !lane_on(&TDRUM->dstep[1], 0));   /* every other 16th */
        assert(fb_len == 8u && fb_bar == 8u && fb_bars == 1u && lane_on(&fb_dstep[2], 2) && lane_on(&fb_dstep[6], 2) && !lane_on(&fb_dstep[1], 2) &&
               lane_on(&fb_dstep[7], 4) && lane_on(&fb_dstep[1], 0));   /* the fill bar: one bar of 1/8 */
        n = sng_pack_section(0, out, sizeof out);
        assert(n && !sng_check(out, n, &tmp) && n < blob_n[8]);
        s = out + SNG_HDR;
        size = sng_u16(s + 16);
        assert(s[12] == 16u && s[13] == 1u && (s[14] & SNG_F_TAIL) == (SNG_F_FILLBAR | SNG_F_ENTRY));
        q = sng_walk(s, SNG_SEC_HDR + (16u + 3u) / 4u, 16, size);
        assert(q && s[q] == 8u && sng_u16(s + q + 1) == 0x11u && sng_u16(s + q + 1 + 4) == 0x11u && sng_u16(s + q + 1 + 8) == 0x15u);   /* the
                                                       * fill bar: 8 steps, kick + hat each, the snare on 2 */
        q = sng_walk(s, q + 1u, 8, size);
        assert(q && s[q] == 8u && sng_u16(s + q + 1) == 0x01u && sng_u16(s + q + 1 + 3) == 0u && sng_u16(s + q + 1 + 5) == 0u);   /* the entry:
                                                       * the kick on 0; the snare's 16ths 1 and 3 fall between 8ths: gone */
        assert(sng_walk(s, q + 1u, 8, size) == size);
        memcpy(blob[8], out, n); blob_n[8] = n;      /* loaded again: as packed */
        pc(16, 8); advance(CTL); sng_poll();
        fm1_irq_off(); sng_apply(0); fm1_irq_on();
        assert(TDRUM->p[P_SDIV] == 1 && TDRUM->p[P_SLEN] == 16 && fb_len == 8u && fb_bar == 8u && fb_bars == 1u && lane_on(&fb_dstep[2], 2) && !lane_on(&fb_dstep[1], 2));
        s = sng_sec(0); q = sng_block(s, SNG_F_ENTRY); assert(q && s[q] == 8u);
        /* back to 1/16: an empty step between, the fill bar too (the hits dropped stay dropped) */
        assert(pattern_rediv(TDRUM, 2) && TDRUM->p[P_SLEN] == 32 && lane_on(&TDRUM->dstep[4], 0) && lane_on(&TDRUM->dstep[4], 2) && !lane_on(&TDRUM->dstep[1], 4) &&
               lane_on(&TDRUM->dstep[2], 4) && fb_len == 16u && fb_bar == 16u && !lane_on(&fb_dstep[1], 4) && lane_on(&fb_dstep[4], 2));
        /* 1/16 -> 8T: the beats stay (steps 0, 4, 8..), the rest goes; 24 steps */
        assert(pattern_rediv(TDRUM, 4) && TDRUM->p[P_SLEN] == 24 && lane_on(&TDRUM->dstep[0], 0) && lane_on(&TDRUM->dstep[3], 0) && lane_on(&TDRUM->dstep[3], 2) &&
               !lane_on(&TDRUM->dstep[1], 4) && !lane_on(&TDRUM->dstep[2], 4) && lane_on(&TDRUM->dstep[3], 4) && fb_len == 12u && fb_bar == 12u);
        /* a 4-bar fill bar (64 steps of 1/16) onto 1/32: the whole bars that fit, 2 (64 steps) */
        assert(pattern_rediv(TDRUM, 2));
        fb_len = 64; fb_phrase(2, 0); assert(fb_bar == 16u && fb_bars == 4u);
        assert(pattern_rediv(TDRUM, 3) && TDRUM->p[P_SLEN] == 64 && fb_len == 64u && fb_bar == 32u && fb_bars == 2u);
        assert(!pattern_rediv_ok(TDRUM, 5) == 0 && pattern_rediv(TDRUM, 2) && TDRUM->p[P_SLEN] == 32);   /* (64 of 1/32 -> 16T: 48, fits) */
        pc(16, 32); advance(CTL); sng_poll();
        assert(sng_cur == -1 && fb_len == 0u);
        blob_n[8] = 0;
    }
    printf("song: the division changed on the device: the hits keep their beat, the fill bar follows, the save re-grids the blocks PASS\n");

    {   /* 16 sections a song (8 until 2026-10-14): sixteen 16-step sections fit; CC 20 / 27 reach 9..16; 17 refused */
        static const char *const NM[16] = {"INTRO", "VERSE", "CHORUS", "VERSE 2", "PRE CHORUS", "CHORUS 2", "SOLO",
                                           "BRIDGE", "BREAKDOWN", "CHORUS 3", "OUTRO JAM", "TAG", "HALF TIME",
                                           "BUILD UP", "LAST CHORUS", "OUTRO"};
        static uint8_t out[SNG_MAX];
        uint32_t p = song_hdr(blob[5], "SIXTEEN", 120, 0, 16), k, n;
        char nm[13];
        for (k = 0; k < 16u; k++)
            p = song_sec(blob[5], p, NM[k], 16, 2, k == 15u ? SNG_F_OUTRO : 0, "x...x...x...x...", "....x.......x...",
                         "x.x.x.x.x.x.x.x.", 0);
        blob_n[5] = p;
        assert(p <= SNG_MAX && sng_check(blob[5], p, &tmp) == 0 && tmp.nsec == 16u);
        blob[5][22] = 17; assert(sng_check(blob[5], p, &tmp) == 3); blob[5][22] = 16;
        cc(16, 23, 0); advance(bar_len() * 2u);
        pedal.count = 0;
        pc(16, 5); advance(CTL); sng_poll();
        assert(sng_cur == 5 && sng_ready() == 0xFFFFu && sng_outro() == 15u);
        sng_name(14, nm); assert(!strcmp(nm, "LAST CHORUS"));
        cc(16, 20, 9); advance(CTL * 3u);                          /* stopped: section 10 starts */
        assert(song.playing && live_sec == 9);
        cc(16, 20, 15); advance(CTL * 2u);                         /* section 16 at the section's end */
        assert(live_req == 15 && pedal_at_end);
        advance(bar_len() + CTL * 4u);
        assert(live_sec == 15);
        cc(16, 27, 12); advance(CTL * 2u);                         /* section 13 on the next bar */
        assert(live_req == 12 && !pedal_at_end);
        advance(bar_len() + CTL * 4u);
        assert(live_sec == 12);
        cc(16, 24, 0); advance(bar_len() + CTL * 4u);              /* NEXT: 14 */
        assert(live_sec == 13);
        n = sng_pack_section(13, out, sizeof out);                 /* section 14 packs back into the 16 (EDIT, SAVE held) */
        assert(n && !sng_check(out, n, &tmp) && tmp.nsec == 16u);
        cc(16, 23, 0); advance(bar_len() * 2u);
    }
    printf("song: 16 sections: packed, checked (17 refused), CC 20 15 / CC 27 12 / NEXT reach them, a section saved back PASS\n");

    /* ---- the SECTIONS screen (2026-10-29): the song's shape changed in the staging image (sng_image_edit: a copy
     * after, taken out, moved, the outro), the grid as the song (sng_same), a new song from the drum track
     * (sng_pack_new) */
    {
        static uint8_t img[SNG_MAX], out[SNG_MAX];
        uint32_t n, m, at, p, e, s0, s1, s2;
        p = song_hdr(blob[9], "SHAPE", 100, 1, 3);
        e = song_sec(blob[9], p, "INTRO", 16, 2, 0, "x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x.x.", 0);
        s0 = e - p;
        e = song_sec(blob[9], e, "VERSE", 8, 1, 0, "x...x...", "..x...x.", "xxxxxxxx", 0);
        s1 = e - p - s0;
        blob_n[9] = song_sec(blob[9], e, "END", 16, 2, SNG_F_OUTRO, "x...............", "........x.......", "x...x...x...x...", 0);
        s2 = blob_n[9] - e;
        assert(!sng_check(blob[9], blob_n[9], &tmp));
        pc(16, 9); advance(CTL); sng_poll();
        assert(sng_cur == 9 && live_sec == 0 && !song.playing && sng_same(0));
        n = sng_pack_section(0, img, sizeof img);
        assert(n == blob_n[9] && !memcmp(img, blob[9], n));
        /* a copy after: INTRO, INTRO 2 (not the outro), VERSE, END */
        m = sng_image_edit(img, n, SNG_MAX, SE_DUP, 0, &at);
        assert(m == n + s0 && at == 1u && !sng_check(img, m, &tmp) && tmp.nsec == 4u && tmp.off[1] == SNG_HDR + s0 &&
               !memcmp(img + tmp.off[1] + 12, blob[9] + SNG_HDR + 12, s0 - 12u) && !memcmp(img + tmp.off[1], "INTRO 2\0\0\0\0\0", 12) &&
               !memcmp(img + tmp.off[2], blob[9] + SNG_HDR + s0, s1 + s2));
        /* taken out: the copy; the bytes as before */
        m = sng_image_edit(img, m, SNG_MAX, SE_DEL, 1, &at);
        assert(m == n && at == 1u && !memcmp(img, blob[9], n));
        /* moved right: VERSE, INTRO, END; left again: as before */
        m = sng_image_edit(img, n, SNG_MAX, SE_RIGHT, 0, &at);
        assert(m == n && at == 1u && !sng_check(img, n, &tmp) && !memcmp(img + tmp.off[0], blob[9] + SNG_HDR + s0, s1) &&
               !memcmp(img + tmp.off[1], blob[9] + SNG_HDR, s0) && !memcmp(img + tmp.off[2], blob[9] + SNG_HDR + s0 + s1, s2));
        m = sng_image_edit(img, n, SNG_MAX, SE_LEFT, 1, &at);
        assert(m == n && at == 0u && !memcmp(img, blob[9], n));
        /* the outro: VERSE made it (END no longer); VERSE again: none; END again: as before */
        m = sng_image_edit(img, n, SNG_MAX, SE_OUTRO, 1, &at);
        assert(m == n && at == 1u && !sng_check(img, n, &tmp) && (img[tmp.off[1] + 14] & SNG_F_OUTRO) &&
               !(img[tmp.off[2] + 14] & SNG_F_OUTRO));
        m = sng_image_edit(img, n, SNG_MAX, SE_OUTRO, 1, &at);
        assert(m == n && !(img[tmp.off[1] + 14] & SNG_F_OUTRO) && !(img[tmp.off[2] + 14] & SNG_F_OUTRO));
        m = sng_image_edit(img, n, SNG_MAX, SE_OUTRO, 2, &at);
        assert(m == n && !memcmp(img, blob[9], n));
        /* refused: the ends, no room, a section that is not there, 16 sections */
        assert(!sng_image_edit(img, n, SNG_MAX, SE_LEFT, 0, &at) && !sng_image_edit(img, n, SNG_MAX, SE_RIGHT, 2, &at) &&
               !sng_image_edit(img, n, n + s0 - 1u, SE_DUP, 0, &at) && !sng_image_edit(img, n, SNG_MAX, SE_DUP, 3, &at));
        img[22] = 16; assert(!sng_image_edit(img, n, SNG_MAX, SE_DUP, 0, &at)); img[22] = 3;
        /* taken out at the end: the one before it is picked; the last one left: refused */
        m = sng_image_edit(img, n, SNG_MAX, SE_DEL, 2, &at); assert(m == n - s2 && at == 1u);
        m = sng_image_edit(img, m, SNG_MAX, SE_DEL, 1, &at); assert(m == n - s2 - s1 && at == 0u && img[22] == 1u);
        assert(!sng_image_edit(img, m, SNG_MAX, SE_DEL, 0, &at));
        /* as the device does it: the image written into the slot and the song loaded again */
        m = sng_pack_section(0, img, sizeof img);
        m = sng_image_edit(img, m, SNG_MAX, SE_DUP, 2, &at);     /* END, END 2 */
        assert(m == n + s2 && at == 3u);
        memcpy(blob[9], img, m); blob_n[9] = m;
        pc(16, 9); advance(CTL); sng_poll();
        assert(sng_cur == 9 && sng.nsec == 4u && sng_outro() == 2u);
        fm1_irq_off(); sng_apply(at); live_sec = (int8_t)at; fm1_irq_on();
        assert(!strcmp(sng_ui_name, "END 2") && sng_same(3));
        /* the grid as the song: a hit, a level, the length, the division, a mark, a condition: each a difference */
        dstep_set(&TDRUM->dstep[1], 3, LV_NORM, 0); assert(!sng_same(3)); dstep_clr(&TDRUM->dstep[1], 3); assert(sng_same(3));
        dstep_set(&TDRUM->dstep[0], 0, LV_HARD, 0); assert(!sng_same(3)); dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0); assert(sng_same(3));
        TDRUM->p[P_SLEN] = 15; assert(!sng_same(3)); TDRUM->p[P_SLEN] = 16; assert(sng_same(3));
        TDRUM->p[P_SDIV] = 1; assert(!sng_same(3)); TDRUM->p[P_SDIV] = 2; assert(sng_same(3));
        drum_flam[0] = 1; assert(!sng_same(3)); drum_flam[0] = 0; assert(sng_same(3));
        TDRUM->fill[0] = 1; assert(!sng_same(3)); TDRUM->fill[0] = 0; assert(sng_same(3));
        /* a new song from the drum track (END 2 as it plays; 100 bpm, kit 1 from SHAPE): SONG 11, one section PART 1 */
        m = sng_pack_new(10, out, sizeof out);
        assert(m == SNG_HDR + s2 && !sng_check(out, m, &tmp) && tmp.nsec == 1u && tmp.bpm == 100u && tmp.kit == 1u &&
               !memcmp(out + 4, "SONG 11\0\0\0\0\0", 12) && !memcmp(out + SNG_HDR, "PART 1\0\0\0\0\0\0", 12) &&
               out[SNG_HDR + 14] == 0u && !memcmp(out + SNG_HDR + 12, blob[9] + sng.off[3] + 12, s2 - 12u));
        assert(!sng_pack_new(10, out, 40));
        memcpy(blob[10], out, m); blob_n[10] = m;
        pc(16, 10); advance(CTL); sng_poll();
        assert(sng_cur == 10 && !strcmp(sng_ui_song, "SONG 11") && !strcmp(sng_ui_name, "PART 1") && sng_same(0));
    }
    printf("song: the SECTIONS screen's image edits (copy, out, move, outro; refusals), the grid as the song, a new song PASS\n");
    return 0;
}
