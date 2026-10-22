/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: the song screens, on the SONG page (ui_studio.c's look: black, big numbers, the knobs' colours).
 *   LIVE   the boot screen (HOME): what plays, read from the floor. The song and its tempo, the beat lights of the
 *          bar's meter, the section in big letters with its bar and a bar-ticked progress (the count-in's beats
 *          instead while it clicks), what comes (NEXT, FILL, FILL > NEXT, ENDING), the song's sections (up to 16) as
 *          tiles, four to a row as the white keys 1..16, and a footer: the kit, the pedal channel and its MIDI dot,
 *          the last message.
 *   SONGS  SAVE tapped: two pages of 16 (name, tempo, sections) and JAM, SLOOP's sections A..D with their chain
 *          (ui_song.c: the page's own screen then, sng_view DV_JAM): USER, the slots the editor and the tools
 *          write; FACTORY, the grooves in the firmware (read-only; the pedal's PC 16..31). ENV / LFO: the page.
 *          It opens on the loaded song's page; with none, on USER, or FACTORY while USER is empty.
 * The keys go through pedal.c's own calls (the host tests cover both). A change of section brings no fill of
 * its own (as the pedal's CC 20 / 27): a fill is asked for by itself, so one press changes, two fill into it:
 *   white keys 1..16  the song's sections, when the one playing ends; with OCT+ held, on the next bar
 *   black keys        (since 2026-10-19, asked: "the first black key a fill now, the second a fill next bar, the
 *                     fourth a break") F#3, the first: the fill (CC 21: the rest of this bar; from its last beat,
 *                     the next bar); with OCT+ held, the whole next bar (CC 21 value 1: the same hold as the white
 *                     keys' "next bar"). G#3, the second: the whole next bar always (CC 21 value 1). C#4, the
 *                     fourth: the break (CC 28 value 127: the section playing comes in again through its break
 *                     when it ends; stopped, the one cued starts through it). The other black keys as the first
 *   SEL ENV LFO       fill (CC 21; with OCT+ held, the next bar); previous, next (as CC 25, 24, without their fill)
 *   PLAY              stopped: the section cued (the count-in first); playing: stop
 *   KNOB 1..4         tempo, kit, drums level, swing (SELECT: tempo; PRESETS: kit)
 * With no song loaded the white keys 1..16 load the page's songs 1..16 (LIVE shows them; ENV / LFO the page). On
 * SONGS (9 rows at a time, following the row picked): KNOB 1 / SELECT a row, REC or OCT+ loads it (and LIVE again),
 * a white key 1..16 loads that song of the page. */
static uint8_t sng_view = DV_LIVE;                  /* DV_* (ui.c) */
static uint8_t sng_pick;                            /* SONGS: the row of the page (SNG_SLOTS: JAM) */
static int8_t sng_page = -1;                        /* 0 USER, 1 FACTORY; -1 not chosen yet: lib_page */
#define SNG_JAM 0xFFu                               /* drummer_load: no song, JAM */
static uint32_t lib_page(void)                      /* the page shown: the one chosen, or FACTORY while USER is empty */
{
    uint32_t i;
    if (sng_page >= 0)
        return (uint32_t)sng_page;
    for (i = 0; i < SNG_SLOTS; i++)
        if (sng_lib[i].name[0])
            return 0;
    return 1;
}
static void lib_turn(uint32_t page)                 /* ENV / LFO: USER / FACTORY */
{
    if (page != lib_page())
        ui.force = 1;
    sng_page = (int8_t)page;
}
static uint32_t str_hash(uint32_t h, const char *s);
static int drummer_on(void) { return on_song_page() && sng_view != DV_JAM; }
static int drummer_keys_on(void) { return drummer_on() && !ui.menu; }
static void drummer_open(uint32_t view)             /* (ui.c go_home, studio_open: the page is the SONG page) */
{
    sng_view = (uint8_t)view;
    sng_pick = (uint8_t)(sng_cur >= 0 ? (uint32_t)sng_cur % SNG_SLOTS : 0u);
    if (sng_cur >= 0)                               /* (the loaded song's page) */
        sng_page = (int8_t)((uint32_t)sng_cur / SNG_SLOTS);
    ui.force = 1;
}
static void drummer_load(uint32_t k)                /* a song (0..31), or SNG_JAM: no song; then LIVE / JAM */
{
    sng_req = (uint8_t)(k < SNG_ALL ? k + 1u : 0xFFu);   /* (sng_poll loads it, as a PC) */
    sng_view = k < SNG_ALL ? DV_LIVE : DV_JAM;
    ui.force = 1;
}
static void lib_load(uint32_t row)                  /* a row of the page shown: its song, or JAM */
{
    drummer_load(row < SNG_SLOTS ? lib_page() * SNG_SLOTS + row : SNG_JAM);
}

static int32_t key_black(uint32_t k)             /* a key (0..26, from F3) -> its black key index 0.., -1 = a white key */
{
    static const int8_t B[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1};   /* from F: F# G# A# C# D# */
    int32_t i = B[k % 12u];
    return i < 0 ? -1 : (int32_t)(k / 12u) * 5 + i;
}

static void drummer_key(uint32_t k, uint32_t nextbar)   /* a key down (ui_input.c: seq.c's KB_GRID events; nextbar:
                                                     * OCT+ was held) */
{
    int32_t w = key_white(k), b = key_black(k);
    if (sng_cur < 0 || sng_view == DV_LIB) {
        if (w >= 0 && w < (int32_t)SNG_SLOTS)
            lib_load((uint32_t)w);
        return;
    }
    fm1_irq_off();                                  /* (pedal.c runs in the audio ISR) */
    if (w >= 0) {                                   /* a section, no fill of its own: when the one playing ends;
                                                     * OCT+ held: on the next bar */
        if (w < (int32_t)SNG_SECTIONS)
            pedal_section((uint32_t)w, nextbar ? PS_BAR : PS_END);
    } else if (b == 1)                              /* the second black key (G#3): the whole next bar (CC 21 value 1) */
        pedal_cc(PEDAL_CC_FILL, 1u);
    else if (b == 3)                                /* the fourth (C#4): the break (CC 28, the section playing's) */
        pedal_cc(PEDAL_CC_BREAK, 127u);
    else
        pedal_cc(PEDAL_CC_FILL, nextbar ? 1u : 0u); /* the first (F#3) and the others: a fill to the bar's end (CC 21);
                                                     * OCT+ held: the whole next bar (value 1) */
    fm1_irq_on();
}

static void live_say(const char *a, int32_t v)      /* a knob turned: "SWING 54" in the footer */
{
    char b[8];
    fmt_int(b, v);
    ui_say(a, b);
}

static void drummer_input(uint32_t pressed, uint32_t home)
{
    int32_t s;
    uint32_t id;
    if (home == 1u) {                               /* BT_TAP: LIVE (from SONGS too) */
        go_home();
        return;
    }
    for (id = 0; id < 14u; id++) {
        uint32_t b;
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        if (b == B_PLAY) {
            fm1_irq_off();
            if (song.playing || sng_cur < 0)
                transport_req = song.playing ? 2u : 1u;
            else
                pedal_cc(PEDAL_CC_SECTION, live_sec >= 0 ? (uint32_t)live_sec : 0u);
            fm1_irq_on();
        } else if (sng_view == DV_LIB || (sng_cur < 0 && (b == B_ENV || b == B_LFO))) {
            if (b == B_REC || b == B_OCTUP)
                lib_load(sng_pick);
            else if (b == B_ENV || b == B_LFO)      /* the page: USER, FACTORY (LIVE with no song: its keys' too) */
                lib_turn(b == B_LFO);
        } else if (b == B_SCL || b == B_ENV || b == B_LFO) {   /* FILL, PREV, NEXT (no fill of their own, as the
                                                                 * white keys) */
            fm1_irq_off();
            if (b == B_SCL)                         /* (OCT+ held: the whole next bar, as the keys) */
                pedal_cc(PEDAL_CC_FILL, (fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u);
            else
                pedal_step(b == B_LFO ? 1 : -1, PS_END);
            fm1_irq_on();
        }
    }
    if (sng_view == DV_LIB) {
        s = panel_enc(EN_K1) + panel_enc(EN_SELECT);
        if (s) {
            sng_pick = (uint8_t)clamp((int32_t)sng_pick + s, 0, (int32_t)SNG_SLOTS);
            ui.force = 1;
        }
        panel_enc(EN_K2); panel_enc(EN_K3); panel_enc(EN_K4);   /* (no hidden action) */
        panel_enc(EN_ALGO); panel_enc(EN_PRESET);
        return;
    }
    s = panel_enc(EN_K1) + panel_enc(EN_SELECT);    /* LIVE: KNOB 1 (and SELECT) the tempo */
    if (s) {
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_K1, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
    s = panel_enc(EN_K2) + panel_enc(EN_PRESET);    /* KNOB 2 (and PRESETS): the kit */
    if (s) {
        TDRUM->p[P_E0] = drum_kit_step(TDRUM->p[P_E0], s);
        ui_say("KIT ", DRUM_KIT_NAMES[drum_kit()]);
    }
    if ((s = panel_enc(EN_K3)) != 0) {
        song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + accel(EN_K3, s, 127), GP[G_DRLVL].min, GP[G_DRLVL].max);
        live_say("DRUMS ", song.g[G_DRLVL]);
    }
    if ((s = panel_enc(EN_K4)) != 0) {
        song.g[G_SWING] = (int16_t)clamp(song.g[G_SWING] + accel(EN_K4, s, 100), GP[G_SWING].min, GP[G_SWING].max);
        live_say("SWING ", song.g[G_SWING]);
    }
    panel_enc(EN_ALGO);
}

/* ------------------------------------------------------------------ draw --- */
static void cut_name(char *d, const char *s, const felucca_font_t *f, int32_t w)   /* s cut to w pixels */
{
    uint32_t n = 0;
    d[0] = 0;
    while (s[n] && n < 12u) {
        d[n] = s[n];
        d[n + 1] = 0;
        if (text_w(f, d) > w) {
            d[n] = 0;
            break;
        }
        n++;
    }
}
static void meter_str(char *b, uint32_t m)          /* "7/8" */
{
    fmt_int(b, (int32_t)meter_num(m));
    str_cpy(b + str_len(b), "/", 2);
    fmt_int(b + str_len(b), (int32_t)meter_den(m));
}

/* the header: the song (its slot under it), the tempo big, the transport, the beat lights of the bar's meter */
static void live_head(uint32_t playing, uint32_t beat, uint32_t beats, const char *songname)
{
    static uint32_t cache;
    char b[16];
    uint32_t k, sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u + beat * 131u + beats * 1009u +
                                  (ui.bpm_t != 0) * 104729u + (uint32_t)(sng_cur + 1) * 37u + (uint32_t)mclk_on() * 11u,
                                  songname);
    int32_t x, w, cw;
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    cv_begin(240, 52, C_BLACK);
    cut_name(b, songname[0] ? songname : "NO SONG", &FONT_S, 140);
    cv_text(4, 4, &FONT_S, b, songname[0] ? C_WHITE : TE_G3);
    if (sng_cur >= 0) {
        sng_label(b, (uint32_t)sng_cur);
    } else {
        str_cpy(b, "jam a-d", 8);
    }
    cv_text(4, 22, &FONT_S, b, TE_G3);
    fmt_int(b, song.g[G_BPM]);
    x = 236 - text_w(&FONT_L, b);
    cv_text(x, 2, &FONT_L, b, ui.bpm_t ? C_WHITE : TE_G4);
    cv_text(x - 28, 22, &FONT_S, mclk_on() ? "ext" : "bpm", TE_G3);
    te_play_icon(x - 24, 4, (int)playing);
    w = 232 / (int32_t)beats;                        /* the beats of the bar, as wide as the screen */
    cw = w - (beats > 8u ? 2 : 4);
    for (k = 0; k < beats; k++)
        cv_rect(4 + (int32_t)k * w, 42, cw, 6, k == beat ? (k ? C_WHITE : TE_RED) : TE_G2);
    cv_blit(0, 0);
}

/* the section: its name big; the bar it is in with a progress ticked per bar; the count-in's beats */
static void live_main(uint32_t playing, const char *sec, uint32_t bar, uint32_t bars, uint32_t ci, uint32_t ci_n)
{
    static uint32_t cache;
    char b[24];
    uint32_t k, sig = studio_hash(playing * 3u + bar * 17u + bars * 131u + ci * 7919u + ci_n * 31u +
                                  (sng_cur < 0) * lib_page() * 104729u, sec);
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    cv_begin(240, 72, C_BLACK);
    if (sng_cur < 0 && !sec[0]) {                   /* no song, no section: how to get one (the keys: the page's) */
        cv_text(4, 4, &FONT_L, "PICK A SONG", TE_G3);
        cv_text(4, 44, &FONT_S, lib_page() ? "FACTORY: keys 1-16, ENV: user" : "USER: keys 1-16, LFO: factory",
                TE_G4);
        cv_blit(0, 52);
        return;
    }
    cut_name(b, sec[0] ? sec : "-", &FONT_L, 232);
    cv_text(4, 2, &FONT_L, b, playing ? C_WHITE : TE_G4);
    if (ci) {                                       /* the count-in: its beats, amber */
        str_cpy(b, "COUNT ", 7);
        fmt_int(b + 6, (int32_t)ci);
        str_cpy(b + str_len(b), " / ", 4);
        fmt_int(b + str_len(b), (int32_t)ci_n);
        cv_text(4, 38, &FONT_S, b, C_AMB);
    } else if (playing) {
        str_cpy(b, "BAR ", 5);
        fmt_int(b + 4, (int32_t)bar + 1);
        str_cpy(b + str_len(b), " / ", 4);
        fmt_int(b + str_len(b), (int32_t)bars);
        cv_text(4, 38, &FONT_S, b, TE_G4);
    } else {
        cv_text(4, 38, &FONT_S, "CUED: PLAY", TE_G3);
    }
    cv_rect(4, 60, 232, 6, TE_G1);                  /* the progress through the pattern's bars */
    if (playing && bars)
        cv_rect(4, 60, (int32_t)(232u * (bar + 1u) / bars), 6, TE_DRUM);
    for (k = 1; bars <= 32u && k < bars; k++)       /* a tick between bars */
        cv_rect(4 + (int32_t)(232u * k / bars), 58, 1, 10, C_BLACK);
    cv_blit(0, 52);
}

/* what comes: the pedal's request, the fill, the ending (coloured); nothing: the section's meter and bars */
static void live_state(uint32_t playing, const char *next)
{
    static uint32_t cache;
    char b[24];
    uint16_t c = C_AMB;
    uint32_t fill = fill_now || pedal_fill, hold = pedal_fill_wait, end = pedal_end_req || pedal_end_bars;
    uint32_t oct = (fm1_in.buttons & dyn_bit[1]) != 0u;   /* OCT+ held: a white key now changes on the next bar */
    uint32_t sig = studio_hash(playing + fill * 3u + hold * 7u + end * 11u + fill_arm * 13u + oct * 17u + pedal_fill_wait * 19u +
                               live_req_brk * 23u + midi_thru * 29u, next);
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    b[0] = 0;
    if (oct && sng_cur >= 0) {                      /* (held a moment, just before a key: over the rest) */
        str_cpy(b, "KEY: ON THE NEXT BAR", sizeof b);
    } else if (end) {
        str_cpy(b, pedal_end_bars ? "ENDING: LAST BARS" : "ENDING NEXT", sizeof b);
        c = TE_RED;
    } else if (next[0] && (hold || fill)) {       /* a fill into the change (one waiting: then the change) */
        str_cpy(b, "FILL > ", 8);
        str_cpy(b + 7, next, 13);
        c = TE_DRUM;
    } else if (next[0] && live_req_brk) {         /* CC 28: the section through its break */
        str_cpy(b, "BREAK: ", 8);
        str_cpy(b + 7, next, 13);
    } else if (next[0]) {
        str_cpy(b, "NEXT: ", 7);
        str_cpy(b + 6, next, 13);
    } else if (pedal_fill_wait) {                 /* asked in the bar's last beat: the next bar, whole; a fill of
                                                   * several bars waiting for the section's end */
        str_cpy(b, pedal_fill_wait > 1u ? "FILL AT THE END" : "FILL NEXT BAR", sizeof b);
        c = TE_DRUM;
    } else if (fill) {
        str_cpy(b, "FILL", sizeof b);
        c = TE_DRUM;
    } else if (fill_arm) {
        str_cpy(b, "FILL NEXT BAR", sizeof b);
        c = TE_DRUM;
    } else if (midi_thru == THRU_ONLY) {          /* SETUP > MIDI 2 > TRS TO USB: the pedal reaches only the USB */
        str_cpy(b, "BRIDGE: PEDAL TO USB", sizeof b);
    }
    cv_begin(240, 20, C_BLACK);
    cv_text(4, 2, &FONT_S, b, c);
    cv_blit(0, 124);
}

/* a tile of one line (a short tile): the number, then the name cut to what is left */
static void tile_line(int32_t x, int32_t y, int32_t h, const char *num, const char *name, uint16_t bg, uint16_t fg,
                      uint16_t numc)
{
    char cut[13];
    int32_t ty = y + (h - 16) / 2;
    cv_rect(x, y, 56, h, bg);
    cv_text(x + 2, ty, &FONT_S, num, numc);
    if (name[0]) {
        cut_name(cut, name, &FONT_S, 52 - 2 - text_w(&FONT_S, num) - 4);
        cv_text(x + 4 + text_w(&FONT_S, num), ty, &FONT_S, cut, fg);
    }
}
/* a name of up to 12 on two lines of 6: broken at the last space of its first 7 characters, else at 6 */
static void name_split(const char *s, char *a, char *b)
{
    uint32_t n = str_len(s), k = n <= 6u ? n : 6u, i, skip = 0;
    if (n > 6u)
        for (i = 6u; i > 0u; i--)
            if (s[i] == ' ') {
                k = i;
                skip = 1;
                break;
            }
    for (i = 0; i < k; i++)
        a[i] = s[i];
    a[k] = 0;
    for (i = 0; i < 6u && s[k + skip + i]; i++)
        b[i] = s[k + skip + i];
    b[i] = 0;
}

/* the song's sections as the white keys 1..16, four to a row as many rows as the song needs (a section keeps its
 * place from song to song): 1 row (1..4 sections) tall tiles with the whole name on two lines, 2 rows (5..8) the
 * name on one, 3 and 4 rows (9..16) a line each, the number then the name cut (the section playing and the one
 * cued show whole above). The one playing lit, the one cued framed amber, the outro marked (END, or its number
 * red), a meter other than 4/4 in the tall ones; no song: the page's 16 songs, as the white keys 1..16 load them */
static void live_tiles(uint32_t playing, int32_t req)
{
    static uint32_t cache;
    char nm[13], b[8];
    uint32_t i, n, rows, ready = sng_ready(), outro = sng_outro(), blink = (ui.frame >> 4) & 1u;
    int32_t pitch, h;
    uint32_t sig = (uint32_t)(sng_cur + 1) * 7u + ready * 13u + (uint32_t)(live_sec + 1) * 17u +
                   (uint32_t)(req + 1) * 19u * (1u + blink) + playing * 23u + sng_lib_dirty;
    const sng_lib_t *lib = &sng_lib[lib_page() * SNG_SLOTS];
    if (sng_cur < 0)
        for (i = 0; i < SNG_SLOTS; i++)
            sig = studio_hash(sig, lib[i].name);
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    cv_begin(240, 72, C_BLACK);
    if (sng_cur < 0) {                              /* no song: the page's 16 songs, as the white keys 1..16 load them */
        for (i = 0; i < SNG_SLOTS; i++) {
            uint32_t there = lib[i].name[0] != 0;
            fmt_int(b, (int32_t)i + 1);
            tile_line(2 + 60 * (int32_t)(i & 3u), 1 + 18 * (int32_t)(i >> 2), 16, b, lib[i].name,
                      there ? TE_G1 : C_BLACK, C_WHITE, TE_G3);
        }
        cv_blit(0, 144);
        return;
    }
    for (n = 0; n < SNG_SECTIONS && ((ready >> n) & 1u); n++)
        ;
    rows = n ? (n + 3u) / 4u : 1u;
    pitch = 72 / (int32_t)rows;                     /* 72, 36, 24, 18 */
    h = pitch - (rows <= 2u ? 4 : 2);               /* 68, 32 (as before 16 sections), 22, 16 */
    for (i = 0; i < rows * 4u; i++) {
        int32_t x = 2 + 60 * (int32_t)(i & 3u), y = (rows <= 2u ? 2 : 1) + pitch * (int32_t)(i >> 2), f = h >= 32 ? 2 : 1;
        int on = playing && (int32_t)i == live_sec, cued = (int32_t)i == req;
        uint32_t there = (ready >> i) & 1u;
        uint16_t bg = on ? C_WHITE : there ? TE_G1 : C_BLACK, fg = on ? C_BLACK : there ? C_WHITE : TE_G2;
        nm[0] = 0;
        if (there)
            sng_name(i, nm);
        fmt_int(b, (int32_t)i + 1);
        if (h < 32) {                               /* a line: the outro's number red */
            tile_line(x, y, h, b, nm, bg, fg, on ? C_BLACK : there && i == outro ? TE_RED : TE_G3);
        } else {
            cv_rect(x, y, 56, h, bg);
            cv_text(x + 4, y, &FONT_S, b, on ? C_BLACK : TE_G3);
            if (there) {
                uint32_t m = sng_meter(i);
                char cut[13];
                if (i == outro)
                    cv_text(x + 52 - text_w(&FONT_S, "end"), y, &FONT_S, "end", on ? C_BLACK : TE_RED);
                else if (m) {
                    meter_str(cut, m);
                    cv_text(x + 52 - text_w(&FONT_S, cut), y, &FONT_S, cut, on ? C_BLACK : TE_G3);
                }
                if (h >= 48) {                      /* tall: the whole name on two lines */
                    char l2[8];
                    name_split(nm, cut, l2);
                    cv_text(x + 4, y + 22, &FONT_S, cut, fg);
                    cv_text(x + 4, y + 40, &FONT_S, l2, fg);
                } else {
                    cut_name(cut, nm, &FONT_S, 50);
                    cv_text(x + 4, y + 15, &FONT_S, cut, fg);
                }
            }
        }
        if (cued && (blink || !playing)) {          /* the cued one framed (blinking while it waits) */
            cv_rect(x, y, 56, f, C_AMB); cv_rect(x, y + h - f, 56, f, C_AMB);
            cv_rect(x, y, f, h, C_AMB); cv_rect(x + 56 - f, y, f, h, C_AMB);
        }
    }
    cv_blit(0, 144);
}

/* the footer: the kit, the pedal channel and its MIDI dot (lit 300 ms after a message); a message over it */
static void live_foot(void)
{
    static uint32_t cache;
    char b[16];
    uint32_t seen = fm1_ms - pedal_seen_ms < 300u && pedal_seen_ms, sig;
    sig = studio_hash(drum_kit() * 7u + pedal.ch * 131u + seen * 3u + (ui.msg_t != 0) * 5u,
                      ui.msg_t ? ui.msg : "");
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    cv_begin(240, 24, C_BLACK);
    if (ui.msg_t) {
        cv_rect(0, 2, 240, 22, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 5, &FONT_S, ui.msg, C_BLACK);
    } else {
        cv_rect(0, 0, 240, 1, TE_G1);
        cv_text(4, 5, &FONT_S, DRUM_KIT_NAMES[drum_kit()], TE_COL[1]);
        if (!pedal.ch)
            str_cpy(b, "pedal off", sizeof b);
        else if (pedal.ch == PEDAL_CH_OMNI)
            str_cpy(b, "pedal any", sizeof b);
        else {
            str_cpy(b, "pedal ch ", 10);
            fmt_int(b + 9, pedal.ch);
        }
        cv_text(220 - text_w(&FONT_S, b), 5, &FONT_S, b, TE_G3);
        te_disc(229, 12, 4, seen ? TE_COL[1] : TE_G2);
    }
    cv_blit(0, 216);
}

static void live_draw(void)
{
    char songname[13], sec[13], next[13];
    uint32_t playing = song.playing, p, m, beats, beat = 0xFFu, ci = 0, ci_n = 0, bar = 0, bars = 1;
    int32_t req = live_req;
    fm1_irq_off();                                  /* (the ISR writes them: taken whole) */
    p = clk_16th();
    m = clk_meter;
    str_cpy(sec, sng_ui_name, sizeof sec);
    str_cpy(next, sng_ui_next, sizeof next);
    if (ci_on && ci_pedal) {
        m = ci_meter;
        ci = (uint32_t)ci_beat + 1u;
        ci_n = ci_pedal;
    }
    fm1_irq_on();
    str_cpy(songname, sng_ui_song, sizeof songname);   /* (the main loop's, sng_load) */
    beats = meter_num(m);
    if (playing)
        beat = (p % meter_b16(m)) / (16u / meter_den(m));
    else if (ci)
        beat = (ci - 1u) % beats;
    if (live_sec >= 0) {
        bars = live_sec_bars();
        bar = live_bar == 0xFFFFFFFFu ? 0u : live_bar % bars;
    }
    live_head(playing, beat, beats, songname);
    live_main(playing, live_sec >= 0 ? sec : "", bar, bars, ci, ci_n);
    live_state(playing, next);
    live_tiles(playing, req);
    live_foot();
}

/* SONGS: a page's 16 songs and JAM, the row picked white; the pages as tabs in the header (the one shown white) */
static void lib_draw(void)
{
    static uint32_t cache;
    char b[16];
    uint32_t page = lib_page(), i, sig = sng_pick * 7u + (uint32_t)(sng_cur + 1) * 13u + (ui.msg_t != 0) * 5u + page * 17u;
    const sng_lib_t *lib = &sng_lib[page * SNG_SLOTS];
    for (i = 0; i < SNG_SLOTS; i++)
        sig = studio_hash(sig + lib[i].bpm + lib[i].nsec * 3u, lib[i].name);
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    cv_begin(240, 24, C_BLACK);
    cv_text(4, 4, &FONT_S, "USER", page ? TE_G3 : C_WHITE);
    cv_text(52, 4, &FONT_S, "FACTORY", page ? C_WHITE : TE_G3);
    cv_rect(page ? 52 : 4, 20, page ? 56 : 32, 2, C_WHITE);   /* (the page shown, underlined) */
    cv_text(236 - text_w(&FONT_S, "env/lfo: page"), 4, &FONT_S, "env/lfo: page", TE_G3);   /* (the keys that turn it) */
    cv_rect(0, 23, 240, 1, TE_G1);
    cv_blit(0, 0);
    {   /* 9 rows of 22 px over the 16 slots and JAM (the last row): the window follows the row picked */
        uint32_t top = (uint32_t)clamp((int32_t)sng_pick - 4, 0, (int32_t)SNG_SLOTS + 1 - 9), r;
        for (r = 0; r < 9u; r++) {
            const sng_lib_t *e;
            int pick, cur;
            uint16_t fg;
            i = top + r;
            e = &lib[i < SNG_SLOTS ? i : 0];
            pick = i == sng_pick;
            cur = (i < SNG_SLOTS && (int32_t)(page * SNG_SLOTS + i) == sng_cur) || (i == SNG_SLOTS && sng_cur < 0);
            fg = pick ? C_BLACK : C_WHITE;
            cv_begin(240, 22, pick ? C_WHITE : C_BLACK);
            if (i == SNG_SLOTS) {
                cv_text(4, 3, &FONT_S, "J", pick ? C_BLACK : TE_G3);
                cv_text(36, 3, &FONT_S, "JAM: SECTIONS A..D", fg);
            } else {
                fmt_int(b, (int32_t)i + 1);
                cv_text(4, 3, &FONT_S, b, pick ? C_BLACK : TE_G3);
                if (!e->name[0]) {
                    cv_text(36, 3, &FONT_S, "-", pick ? C_BLACK : TE_G2);
                } else {
                    cv_text(36, 3, &FONT_S, e->name, fg);
                    fmt_int(b, e->bpm);
                    cv_text(236 - text_w(&FONT_S, b), 3, &FONT_S, b, pick ? C_BLACK : TE_G4);
                    fmt_int(b, e->nsec);
                    str_cpy(b + str_len(b), "s", 2);
                    cv_text(180, 3, &FONT_S, b, pick ? C_BLACK : TE_G3);
                }
            }
            if (cur)
                cv_rect(25, 7, 6, 6, pick ? C_BLACK : TE_DRUM);   /* the one loaded */
            if (r == 0u && top > 0u)
                cv_rect(237, 2, 2, 6, TE_G3);                  /* more above */
            if (r == 8u && top + 9u < SNG_SLOTS + 1u)
                cv_rect(237, 14, 2, 6, TE_G3);                 /* more below */
            cv_blit(0, 24u + 22u * r);
        }
    }
    cv_begin(240, 18, C_BLACK);
    if (ui.msg_t)
        cv_text(4, 1, &FONT_S, ui.msg, C_HI);
    else                                            /* (the header has the page's keys: the load's here) */
        cv_text(236 - text_w(&FONT_S, "rec: load"), 1, &FONT_S, "rec: load", TE_G3);
    cv_blit(0, 222);
}

static void drummer_draw(void)
{
    sng_lib_refresh();                              /* (only after a load or a write: sng.c) */
    if (sng_view == DV_LIB)
        lib_draw();
    else
        live_draw();
}
