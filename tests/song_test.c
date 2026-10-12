/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-arranger: songs (firmware/src/sng.c) through the real sequencer and the pedal (pedal.c). Songs are
 * packed as tools/fm1_song.py packs them, into a host table that stands for the flash slots. Checks: the
 * packed form's checks; PC loads a song (stopped: tempo, kit and the first section at once); its sections
 * start on the bar with their length, division and fill conditions; next / previous over the song's
 * sections; the ending takes the flagged outro, for its bars; PC while playing waits for the next section;
 * a bad or empty slot leaves the song alone; PC 127 goes back to SLOOP's A..D; the pedal settings object. */
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
/* a section: lanes 0 (kick), 2 (snare), 4 (hat) from pattern strings ('.' rest, 'x' norm, 'X' hard, 'g' ghost),
 * cond: '.' normal, 'F' fill only, 'N' no fill */
static uint32_t song_sec(uint8_t *b, uint32_t pos, const char *name, uint32_t len, uint32_t div, uint32_t flags,
                         const char *kick, const char *snare, const char *hat, const char *cond)
{
    uint32_t i, nf = (len + 3u) / 4u;
    const char *lane[3] = {kick, snare, hat};
    const uint32_t lane_no[3] = {0, 2, 4};
    memset(b + pos, 0, SNG_SEC_HDR + nf + len * sizeof(dstep_t));
    strncpy((char *)b + pos + 12 - 12, name, SNG_NAME);
    b[pos + 12] = (uint8_t)len; b[pos + 13] = (uint8_t)div; b[pos + 14] = (uint8_t)flags;
    for (i = 0; i < len; i++) {
        uint32_t c = cond && cond[i] ? (cond[i] == 'F' ? FC_FILL : cond[i] == 'N' ? FC_NOFILL : FC_NORM) : FC_NORM;
        dstep_t *d = (dstep_t *)(b + pos + SNG_SEC_HDR + nf + i * sizeof(dstep_t));
        uint32_t l;
        b[pos + SNG_SEC_HDR + i / 4u] |= (uint8_t)(c << (2u * (i % 4u)));
        for (l = 0; l < 3u; l++) {
            char ch = lane[l] && lane[l][i] ? lane[l][i] : '.';
            if (ch != '.')
                dstep_set(d, lane_no[l], ch == 'X' ? LV_HARD : ch == 'g' ? LV_GHOST : ch == 'o' ? LV_SOFT : LV_NORM, 0);
        }
    }
    return pos + SNG_SEC_HDR + nf + len * sizeof(dstep_t);
}

/* lane marks appended to a section (flag 2): per step the "only during a fill" and "not during a fill" lanes */
static uint32_t song_marks(uint8_t *b, uint32_t secpos, uint32_t end, const uint16_t *fonly, const uint16_t *fno)
{
    uint32_t len = b[secpos + 12], i;
    b[secpos + 14] |= 2;
    for (i = 0; i < len; i++) {
        b[end + 4 * i] = (uint8_t)fonly[i]; b[end + 4 * i + 1] = (uint8_t)(fonly[i] >> 8);
        b[end + 4 * i + 2] = (uint8_t)fno[i]; b[end + 4 * i + 3] = (uint8_t)(fno[i] >> 8);
    }
    return end + 4 * len;
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
        }
        return rc ? 1 : 0;
    }
    host_tracks_init();
    host_preset(&trk[0], 0, 4);
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = 16; trk[i].p[P_SDIV] = 2; trk[i].p[P_AMODE] = 0; trk[i].p[P_LEVEL] = 90;
    }
    for (k = 0; k < 16; k++) {
        uint8_t bass[] = {36}, drum[] = {42, (uint8_t)(k % 8u == 4u ? 38 : 36)};
        if (!(k % 4u)) put_step(&trk[0], k, 1, bass, ST_NOTE, 0);
        put_step(TDRUM, k, k % 4u ? 1 : 2, drum, ST_NOTE, 0);
    }
    capture(0);                                           /* SLOOP's A: bass on 36, a 16-step beat */
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
        uint32_t p3 = song_hdr(blob[3], "MARKS", 120, 0, 1), secpos = p3;
        fonly[12] = 1u << 10; fno[12] = 1u << 4;
        p3 = song_sec(blob[3], p3, "ONE", 16, 2, 0, "x...............", "....x.......x...", "x.x.x.x.x.x.x...", 0);
        dstep_set((dstep_t *)(blob[3] + secpos + SNG_SEC_HDR + 4 + 12 * sizeof(dstep_t)), 10, LV_NORM, 0);   /* the tom */
        blob_n[3] = song_marks(blob[3], secpos, p3, fonly, fno);
    }
    pos = song_hdr(blob[1], "SLOW", 100, 2, 1);
    pos = song_sec(blob[1], pos, "ONLY", 16, 2, 0, "x.x.x.x.x.x.x.x.", 0, 0, 0);
    blob_n[1] = pos;
    memcpy(blob[2], blob[1], pos); blob[2][0] ^= 1; blob_n[2] = pos;

    /* the packed form's checks */
    assert(sng_check(blob[0], blob_n[0], &tmp) == 0 && tmp.nsec == 3u && tmp.bpm == 132u && tmp.kit == 7u);
    assert(tmp.off[0] == SNG_HDR && tmp.off[1] == SNG_HDR + 16u + 4u + 160u && tmp.off[2] == tmp.off[1] + 16u + 8u + 320u);
    assert(sng_check(blob[2], blob_n[2], &tmp) == 2);
    assert(sng_check(blob[0], blob_n[0] - 1u, &tmp) == 6);     /* truncated */
    assert(sng_check(blob[0], SNG_HDR + 8u, &tmp) == 4);
    {
        uint8_t bad[3800];
        memcpy(bad, blob[0], blob_n[0]);
        bad[22] = 0;  assert(sng_check(bad, blob_n[0], &tmp) == 3);
        bad[22] = 9;  assert(sng_check(bad, blob_n[0], &tmp) == 3);
        bad[22] = 3;  bad[SNG_HDR + 12] = 0;  assert(sng_check(bad, blob_n[0], &tmp) == 5);
        bad[SNG_HDR + 12] = 16; bad[SNG_HDR + 13] = 9;  assert(sng_check(bad, blob_n[0], &tmp) == 5);
        bad[SNG_HDR + 13] = 2; bad[18] = (uint8_t)DRUM_KITS;  assert(sng_check(bad, blob_n[0], &tmp) == 3);
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

    /* ---- NEXT: OUTRO on the bar (after VERSE's 2 bars? no: NEXT asks now, the bar comes first); NEXT again wraps to INTRO */
    advance(bar / 2u);
    cc(16, 24, 0); advance(bar);
    assert(live_sec == 2 && TDRUM->p[P_SLEN] == 16 && (jumped - t0) % bar < CTL);
    cc(16, 24, 0); advance(bar);
    assert(live_sec == 0);
    cc(16, 25, 0); advance(bar);
    assert(live_sec == 2);
    cc(16, 20, 5); advance(CTL * 2u);                              /* no section 5: ignored */
    assert(live_req == -1);
    printf("song: NEXT / PREV over the song's three sections, section 5 ignored PASS\n");

    /* ---- the ending with no value: the flagged OUTRO, once (1 bar), then stop on the bar */
    cc(16, 20, 1); advance(bar);                                   /* back in the VERSE */
    assert(live_sec == 1);
    advance(bar / 3u);
    cc(16, 22, 127); advance(CTL * 2u);
    assert(fill_now && live_req == 2 && pedal_end_req == 1u);
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
    cc(16, 20, 0); advance(CTL);                                   /* SLOW has one section: 0 is fine.. */
    assert(live_req == 0);
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
    cc(16, 20, 0); advance(bar);                                   /* A: the project slot, its bass and beat */
    assert(live_sec == 0 && trk[0].step[0].note[0] == 36 && !lane_on(&TDRUM->dstep[2], 0) && lane_on(&TDRUM->dstep[4], 2));
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
    assert(sng_check(blob[3], blob_n[3] - 1u, &tmp) == 6);                  /* the marks counted in the size */
    pc(16, 0); advance(CTL); sng_poll();                                    /* a song without marks: cleared */
    assert(sng_cur == 0 && !drum_fonly[12] && !drum_fno[12]);
    printf("song: lane marks: only-during and not-during a fill, per lane and step; cleared by a plain section PASS\n");

    /* ---- the pedal settings object */
    {
        arrset_t a;
        arrset_fill(&a);
        assert(arrset_valid(&a) && a.pedal_ch == 16u);
        a.pedal_ch = 17; arrset_take(&a);
        assert(pedal.ch == 17u);
        a.pedal_ch = 18; assert(!arrset_valid(&a));
        a.pedal_ch = 16; arrset_take(&a);
    }
    printf("song: the pedal settings object PASS\n");
    return 0;
}
