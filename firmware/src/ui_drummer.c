/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: the song screens, on the SONG page (ui_studio.c's look: black, big numbers, the knobs' colours).
 *   LIVE   the boot screen (HOME): what plays, read from the floor. The song and its tempo, the beat lights of the
 *          bar's meter, the section in big letters with its bar and a bar-ticked progress (the count-in's beats
 *          instead while it clicks), what comes (NEXT, FILL, FILL > NEXT, ENDING), the 8 sections as tiles laid out as the
 *          white keys 1..8, and a footer: the kit, the pedal channel and its MIDI dot, the last message.
 *   SONGS  SAVE tapped: the 16 song slots (name, tempo, sections; the factory songs marked) and JAM, SLOOP's
 *          sections A..D with their chain (ui_song.c: the page's own screen then, sng_view DV_JAM).
 * The keys go through pedal.c's own calls (the host tests cover both). A change of section brings no fill of
 * its own (as the pedal's CC 20 / 27): a fill is asked for by itself, so one press changes, two fill into it:
 *   white keys 1..8   the song's sections, when the one playing ends; 9..16 the same on the next bar
 *   black keys        any of them: a fill to the pattern's end (CC 21)
 *   SEL ENV LFO       fill (CC 21); previous, next (as CC 25, 24, without their fill)
 *   PLAY              stopped: the section cued (the count-in first); playing: stop
 *   KNOB 1..4         tempo, kit, drums level, swing (SELECT: tempo; PRESETS: kit)
 * With no song loaded the white keys 1..16 load songs 1..16 (LIVE shows the 16 slots). On SONGS (9 rows at a time,
 * following the row picked): KNOB 1 / SELECT a row, REC or OCT+ loads it (and LIVE again), a white key 1..16 loads that
 * song. */
static uint8_t sng_view = DV_LIVE;                  /* DV_* (ui.c) */
static uint8_t sng_pick;                            /* SONGS: the row (SNG_SLOTS: JAM) */
static uint32_t str_hash(uint32_t h, const char *s);
static int drummer_on(void) { return on_song_page() && sng_view != DV_JAM; }
static int drummer_keys_on(void) { return drummer_on() && !ui.menu; }
static void drummer_open(uint32_t view)             /* (ui.c go_home, studio_open: the page is the SONG page) */
{
    sng_view = (uint8_t)view;
    sng_pick = (uint8_t)(sng_cur >= 0 ? sng_cur : 0);
    ui.force = 1;
}
static void drummer_load(uint32_t slot)             /* a slot, or SNG_SLOTS: no song (JAM); then LIVE / JAM */
{
    sng_req = (uint8_t)(slot < SNG_SLOTS ? slot + 1u : 0xFFu);   /* (sng_poll loads it, as a PC) */
    sng_view = slot < SNG_SLOTS ? DV_LIVE : DV_JAM;
    ui.force = 1;
}

static void drummer_key(uint32_t k)                 /* a key down (ui_input.c: seq.c's KB_GRID events) */
{
    int32_t w = key_white(k);
    if (sng_cur < 0 || sng_view == DV_LIB) {
        if (w >= 0 && w < (int32_t)SNG_SLOTS)
            drummer_load((uint32_t)w);
        return;
    }
    fm1_irq_off();                                  /* (pedal.c runs in the audio ISR) */
    if (w >= 0)                                     /* a section, no fill of its own: 1..8 when the one playing ends,
                                                     * 9..16 on the next bar */
        pedal_section((uint32_t)w & 7u, w < 8 ? PS_END : PS_BAR);
    else
        pedal_cc(PEDAL_CC_FILL, 127);               /* any black key: a fill to the pattern's end (CC 21) */
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
        } else if (sng_view == DV_LIB) {
            if (b == B_REC || b == B_OCTUP)
                drummer_load(sng_pick);
        } else if (b == B_SCL || b == B_ENV || b == B_LFO) {   /* FILL, PREV, NEXT (no fill of their own, as the
                                                                 * white keys) */
            fm1_irq_off();
            if (b == B_SCL)
                pedal_cc(PEDAL_CC_FILL, 127);
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
        str_cpy(b, "song ", 6);
        fmt_int(b + 5, sng_cur + 1);
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
    uint32_t k, sig = studio_hash(playing * 3u + bar * 17u + bars * 131u + ci * 7919u + ci_n * 31u, sec);
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    cv_begin(240, 72, C_BLACK);
    if (sng_cur < 0 && !sec[0]) {                   /* no song, no section: how to get one */
        cv_text(4, 4, &FONT_L, "PICK A SONG", TE_G3);
        cv_text(4, 44, &FONT_S, "SAVE: songs, keys 1-16: load", TE_G4);
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
    uint32_t fill = fill_now || pedal_fill, hold = pedal_fill_hold, end = pedal_end_req || pedal_end_bars;
    uint32_t sig = studio_hash(playing + fill * 3u + hold * 7u + end * 11u + fill_arm * 13u, next);
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    b[0] = 0;
    if (end) {
        str_cpy(b, pedal_end_bars ? "ENDING: LAST BARS" : "ENDING NEXT", sizeof b);
        c = TE_RED;
    } else if (next[0] && (hold || fill)) {       /* a fill into the change */
        str_cpy(b, "FILL > ", 8);
        str_cpy(b + 7, next, 13);
        c = TE_DRUM;
    } else if (next[0]) {
        str_cpy(b, "NEXT: ", 7);
        str_cpy(b + 6, next, 13);
    } else if (hold) {
        str_cpy(b, "FILL TO THE END", sizeof b);
        c = TE_DRUM;
    } else if (fill) {
        str_cpy(b, "FILL", sizeof b);
        c = TE_DRUM;
    } else if (fill_arm) {
        str_cpy(b, "FILL NEXT BAR", sizeof b);
        c = TE_DRUM;
    }
    cv_begin(240, 20, C_BLACK);
    cv_text(4, 2, &FONT_S, b, c);
    cv_blit(0, 124);
}

/* the 8 sections as the white keys 1..8 (two rows of four): the one playing lit, the one cued framed amber, the
 * outro marked, a meter other than 4/4; no song: the 8 song slots */
static void live_tiles(uint32_t playing, int32_t req)
{
    static uint32_t cache;
    char nm[13], b[8];
    uint32_t i, ready = sng_ready(), outro = sng_outro(), blink = (ui.frame >> 4) & 1u;
    uint32_t sig = (uint32_t)(sng_cur + 1) * 7u + ready * 13u + (uint32_t)(live_sec + 1) * 17u +
                   (uint32_t)(req + 1) * 19u * (1u + blink) + playing * 23u + sng_lib_dirty;
    if (sng_cur < 0)
        for (i = 0; i < SNG_SLOTS; i++)
            sig = studio_hash(sig, sng_lib[i].name);
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    cv_begin(240, 72, C_BLACK);
    if (sng_cur < 0) {                              /* no song: the 16 song slots, as the white keys 1..16 load them */
        for (i = 0; i < SNG_SLOTS; i++) {
            int32_t x = 2 + 60 * (int32_t)(i & 3u), y = 1 + 18 * (int32_t)(i >> 2);
            char cut[13];
            uint32_t there = sng_lib[i].name[0] != 0;
            cv_rect(x, y, 56, 16, there ? TE_G1 : C_BLACK);
            fmt_int(b, (int32_t)i + 1);
            cv_text(x + 2, y, &FONT_S, b, TE_G3);
            if (there) {
                cut_name(cut, sng_lib[i].name, &FONT_S, 52 - 2 - text_w(&FONT_S, b) - 4);
                cv_text(x + 4 + text_w(&FONT_S, b), y, &FONT_S, cut, C_WHITE);
            }
        }
        cv_blit(0, 144);
        return;
    }
    for (i = 0; i < 8u; i++) {
        int32_t x = 2 + 60 * (int32_t)(i & 3u), y = 2 + 36 * (int32_t)(i >> 2);
        int on = sng_cur >= 0 && playing && (int32_t)i == live_sec, cued = sng_cur >= 0 && (int32_t)i == req;
        uint32_t there = sng_cur >= 0 ? (ready >> i) & 1u : sng_lib[i].name[0] != 0;
        uint16_t bg = on ? C_WHITE : there ? TE_G1 : C_BLACK, fg = on ? C_BLACK : there ? C_WHITE : TE_G2;
        cv_rect(x, y, 56, 32, bg);
        if (cued && (blink || !playing)) {          /* the cued one framed (blinking while it waits) */
            cv_rect(x, y, 56, 2, C_AMB); cv_rect(x, y + 30, 56, 2, C_AMB);
            cv_rect(x, y, 2, 32, C_AMB); cv_rect(x + 54, y, 2, 32, C_AMB);
        }
        b[0] = (char)('1' + i);
        b[1] = 0;
        cv_text(x + 4, y, &FONT_S, b, on ? C_BLACK : TE_G3);
        if (!there)
            continue;
        if (sng_cur >= 0) {
            uint32_t m = sng_meter(i);
            sng_name(i, nm);
            if (i == outro)
                cv_text(x + 52 - text_w(&FONT_S, "end"), y, &FONT_S, "end", on ? C_BLACK : TE_RED);
            else if (m) {
                meter_str(b, m);
                cv_text(x + 52 - text_w(&FONT_S, b), y, &FONT_S, b, on ? C_BLACK : TE_G3);
            }
        } else {
            str_cpy(nm, sng_lib[i].name, sizeof nm);
        }
        {
            char cut[13];
            cut_name(cut, nm, &FONT_S, 50);
            cv_text(x + 4, y + 15, &FONT_S, cut, fg);
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

/* SONGS: the 8 slots and JAM, the row picked white */
static void lib_draw(void)
{
    static uint32_t cache;
    char b[16];
    uint32_t i, sig = sng_pick * 7u + (uint32_t)(sng_cur + 1) * 13u + (ui.msg_t != 0) * 5u;
    for (i = 0; i < SNG_SLOTS; i++)
        sig = studio_hash(sig + sng_lib[i].bpm + sng_lib[i].nsec * 3u, sng_lib[i].name);
    if (!ui.force && sig == cache)
        return;
    cache = sig;
    cv_begin(240, 24, C_BLACK);
    cv_text(4, 4, &FONT_S, "SONGS", C_WHITE);
    cv_text(236 - text_w(&FONT_S, "rec: load"), 4, &FONT_S, "rec: load", TE_G3);
    cv_rect(0, 23, 240, 1, TE_G1);
    cv_blit(0, 0);
    {   /* 9 rows of 22 px over the 16 slots and JAM (the last row): the window follows the row picked */
        uint32_t top = (uint32_t)clamp((int32_t)sng_pick - 4, 0, (int32_t)SNG_SLOTS + 1 - 9), r;
        for (r = 0; r < 9u; r++) {
            const sng_lib_t *e;
            int pick, cur;
            uint16_t fg;
            i = top + r;
            e = &sng_lib[i < SNG_SLOTS ? i : 0];
            pick = i == sng_pick;
            cur = (int32_t)i == sng_cur || (i == SNG_SLOTS && sng_cur < 0);
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
                    b[0] = (char)('0' + e->nsec);
                    b[1] = 's';
                    b[2] = 0;
                    cv_text(180, 3, &FONT_S, b, pick ? C_BLACK : TE_G3);
                    if (e->factory)
                        cv_text(164, 3, &FONT_S, "f", pick ? C_BLACK : TE_G3);
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
