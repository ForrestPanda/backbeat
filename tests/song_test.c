/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: songs (firmware/src/sng.c) through the real sequencer and the pedal (pedal.c). Songs are
 * packed as tools/fm1_song.py packs them, into a host table that stands for the flash slots. Checks: the
 * packed form's checks; PC loads a song (stopped: tempo, kit and the first section at once); its sections
 * start on the bar with their length, division and fill conditions; next / previous over the song's
 * sections; the ending takes the flagged outro, for its bars; PC while playing waits for the next section;
 * a bad or empty slot leaves the song alone; PC 127 goes back to SLOOP's A..D; the pedal settings object. */
#define FELUCCA_DRUMMER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;

static uint8_t blob[SNG_SLOTS][3800];            /* the flash slots */
static uint32_t blob_n[SNG_SLOTS];
#define SNG_HOST_FETCH 1
static const uint8_t *sng_fetch(uint32_t slot, uint32_t *len)
{
    *len = slot < SNG_SLOTS ? blob_n[slot] : 0u;
    return *len ? blob[slot] : 0;
}
#include "../firmware/src/arranger_scene.c"

/* ---- a packed song, as the tool writes it */
static void w16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static uint32_t song_hdr(uint8_t *b, const char *name, uint32_t bpm, uint32_t kit, uint32_t nsec)
{
    memset(b, 0, SNG_HDR);
    w16(b, SNG_MAGIC & 0xFFFFu); w16(b + 2, SNG_MAGIC >> 16);
    strncpy((char *)b + 4, name, SNG_NAME);
    w16(b + 16, bpm);
    b[18] = (uint8_t)kit; b[19] = 50; b[20] = 100; b[21] = 20; b[22] = (uint8_t)nsec;
    return SNG_HDR;
}
/* a section, packed as the tool packs it ("SNG2"): any lanes from pattern strings ('.' rest, 'x' norm, 'X' hard,
 * 'g' ghost, 'o' soft), cond: '.' normal, 'F' fill only, 'N' no fill; marks: per step the lanes only during a fill
 * (fonly) and not during one (fno); meter: the section header's meter byte (0: 4/4) */
static uint32_t song_secm(uint8_t *b, uint32_t pos, const char *name, uint32_t len, uint32_t div, uint32_t flags,
                          uint32_t meter, const char *const lane[DRUM_LANES], const char *cond, const uint16_t *fonly,
                          const uint16_t *fno)
{
    uint32_t i, l, nf = (len + 3u) / 4u, q = pos + SNG_SEC_HDR + nf;
    memset(b + pos, 0, SNG_SEC_HDR + nf);
    strncpy((char *)b + pos, name, SNG_NAME);
    b[pos + 12] = (uint8_t)len; b[pos + 13] = (uint8_t)div; b[pos + 14] = (uint8_t)flags; b[pos + 15] = (uint8_t)meter;
    for (i = 0; i < len; i++) {
        uint32_t c = cond && cond[i] ? (cond[i] == 'F' ? FC_FILL : cond[i] == 'N' ? FC_NOFILL : FC_NORM) : FC_NORM, on = 0;
        b[pos + SNG_SEC_HDR + i / 4u] |= (uint8_t)(c << (2u * (i % 4u)));
        for (l = 0; l < DRUM_LANES; l++)
            if (lane[l] && lane[l][i] && lane[l][i] != '.')
                on |= 1u << l;
        w16(b + q, on);
        q += 2;
        for (l = 0; l < DRUM_LANES; l++) {
            char ch;
            if (!((on >> l) & 1u))
                continue;
            ch = lane[l][i];
            b[q++] = (uint8_t)((ch == 'X' ? LV_HARD : ch == 'g' ? LV_GHOST : ch == 'o' ? LV_SOFT : LV_NORM) |
                               (fonly && (fonly[i] >> l & 1u) ? 0x10u : 0u) | (fno && (fno[i] >> l & 1u) ? 0x20u : 0u));
        }
    }
    w16(b + pos + 16, q - pos);
    return q;
}
/* lanes 0 (kick), 2 (snare), 4 (hat) */
static uint32_t song_sec(uint8_t *b, uint32_t pos, const char *name, uint32_t len, uint32_t div, uint32_t flags,
                         const char *kick, const char *snare, const char *hat, const char *cond)
{
    const char *lane[DRUM_LANES] = {kick, 0, snare, 0, hat};
    return song_secm(b, pos, name, len, div, flags, 0, lane, cond, 0, 0);
}
static void capture(uint32_t slot) { proj_capture(&proj_slot[slot]); }
static void cc(uint32_t ch, uint32_t num, uint32_t val) { um_byte(0xB0u | (ch - 1u)); um_byte(num); um_byte(val); }
static void pc(uint32_t ch, uint32_t n) { um_byte(0xC0u | (ch - 1u)); um_byte(n); }

static int32_t out[CTL * 2];
static uint32_t now, t0, jumped, stopped;
static void advance(uint32_t samples)
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
    capture(0);                                           /* SLOOP's A: PAN 36, a 16-step beat */
    capture(1);

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
    pos = song_hdr(blob[1], "SLOW", 100, 2, 1);
    pos = song_sec(blob[1], pos, "ONLY", 16, 2, 0, "x.x.x.x.x.x.x.x.", 0, 0, 0);
    blob_n[1] = pos;
    memcpy(blob[2], blob[1], pos); blob[2][0] ^= 1; blob_n[2] = pos;

    /* the packed form's checks */
    assert(sng_check(blob[0], blob_n[0], &tmp) == 0 && tmp.nsec == 3u && tmp.bpm == 132u && tmp.kit == 7u);
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
    assert(live_req == -1 && !pedal_fill && !pedal_fill_hold);
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

    /* ---- a bad slot and an empty slot leave the song alone; PC 127 unloads: SLOOP's A..D again */
    pc(16, 2); advance(CTL); sng_poll();
    assert(sng_cur == 0);                                          /* (TEST stays loaded) */
    pc(16, 5); advance(CTL); sng_poll();
    assert(sng_cur == 0);
    pc(16, 127); advance(CTL); sng_poll();
    assert(sng_cur == -1 && sng_req == 0 && !sng_ready() && song.playing);
    bar = bar_len();
    advance(bar / 4u);
    cc(16, 20, 0); advance(bar);                                   /* A: the project slot, its PAN and beat */
    assert(live_sec == 0 && TDRUM->p[P_PAN] == 36 && !lane_on(&TDRUM->dstep[2], 0) && lane_on(&TDRUM->dstep[4], 2));
    cc(16, 23, 0); advance(CTL * 2u);
    assert(!song.playing);
    printf("song: bad / empty slot ignored, PC 127 back to A..D PASS\n");

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
        a.last = SNG_SLOTS + 1u; a.autoload = 7; arrset_take(&a);   /* (16 songs: last 1..16) */
        assert(!sng_autoload && !sng_last);
        sng_autoload = 0; arrset_dirty = 0;
    }
    printf("song: the pedal settings object PASS\n");

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
        assert(!sng_lossy());
        TDRUM->micro[5] = 3; assert(sng_lossy()); TDRUM->micro[5] = 0;
        TDRUM->p[P_SLEN] = 64;                        /* the longest section: still packs and checks */
        n = sng_pack_section(1, out, sizeof out);
        assert(n && !sng_check(out, n, &tmp) && tmp.nsec == sng.nsec);
    }
    printf("song: a section packed again from the drum track (EDIT, SAVE held): same bytes, an edit kept, no room refused PASS\n");

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
    return 0;
}
