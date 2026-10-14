/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: the song screen. SLOOP's SONG page (SAVE tapped on TRACKS) shows it while a song is loaded, or
 * while none is and KNOB 1 was not turned to SLOOP's chain (studio_open brings it back). The song, its sections
 * (the one playing lit, the one cued underlined in amber, a meter other than 4/4), the pedal's messages.
 * The keys are the pedal, through pedal.c's own calls (the host tests cover both):
 *   white keys 1..8   the song's sections, as CC 20 (the one playing again: a fill); 9..16 the same now (CC 27)
 *   black keys        F#3 previous, G#3 next, A#3 fill, C#4 the ending, D#4 stop, F#4 tap
 *   PLAY              stopped: the section cued (the count-in first); playing: stop
 *   KNOB 1, REC       a slot, then load it (the last: no song, SLOOP's sections A..D and chain)
 * With no song loaded the white keys 1..8 load songs 1..8. Small on purpose: the flash is nearly full. */
static uint8_t sng_view = 1;                        /* the SONG page shows the drummer's (no song loaded) */
static uint8_t sng_pick;                            /* KNOB 1: a slot; SNG_SLOTS: SLOOP's */
static uint32_t str_hash(uint32_t h, const char *s);
static int drummer_on(void) { return on_song_page() && (sng_cur >= 0 || sng_view); }
static int drummer_keys_on(void) { return drummer_on() && !ui.menu; }

static void drummer_key(uint32_t k)                 /* a key down (ui_input.c: seq.c's KB_GRID events) */
{
    static const uint8_t CC[14] = {0, PEDAL_CC_PREV, 0, PEDAL_CC_NEXT, 0, PEDAL_CC_FILL, 0, 0, PEDAL_CC_ENDING, 0,
                                   PEDAL_CC_STOP, 0, 0, PEDAL_CC_TAP};   /* the black keys F#3 .. F#4 */
    int32_t w = key_white(k);
    if (sng_cur < 0) {
        if (w >= 0 && w < (int32_t)SNG_SLOTS)
            sng_req = (uint8_t)(w + 1);             /* (sng_poll loads it, as a PC) */
        return;
    }
    fm1_irq_off();                                  /* (pedal.c runs in the audio ISR) */
    if (w >= 0)
        pedal_cc(w < 8 ? PEDAL_CC_SECTION : PEDAL_CC_SECTION_FILLBAR, (uint32_t)w & 7u);
    else if (k < 14u)
        pedal_cc(CC[k], 127);                       /* (the ending's 127: the song's outro) */
    fm1_irq_on();
}

static void drummer_input(uint32_t pressed, uint32_t home)
{
    int32_t s = panel_enc(EN_K1);
    if (home == 1u) {                               /* BT_TAP */
        go_home();
        return;
    }
    if (s) {
        sng_pick = (uint8_t)clamp((int32_t)sng_pick + s, 0, (int32_t)SNG_SLOTS);
        ui.force = 1;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        fm1_irq_off();
        if (song.playing || sng_cur < 0)
            transport_req = song.playing ? 2u : 1u;
        else
            pedal_cc(PEDAL_CC_SECTION, live_sec >= 0 ? (uint32_t)live_sec : 0u);
        fm1_irq_on();
    }
    if ((pressed >> panel.btn[B_REC]) & 1u) {
        sng_req = (uint8_t)(sng_pick < SNG_SLOTS ? sng_pick + 1u : 0xFFu);
        sng_view = sng_pick < SNG_SLOTS;            /* (SLOOP's: its chain on this page, until it is opened again) */
    }
    panel_enc(EN_K2); panel_enc(EN_K3); panel_enc(EN_K4);   /* (no hidden action) */
    panel_enc(EN_SELECT); panel_enc(EN_ALGO); panel_enc(EN_PRESET);
}

static void drummer_draw(void)
{
    static uint32_t previous;
    char b[28], nm[13];
    uint32_t i, ready = sng_ready();
    uint32_t sig = str_hash(5u, sng_ui_song) + (uint32_t)(live_sec + 1) * 7u + (uint32_t)(live_req + 1) * 11u +
                   live_bar * 13u + song.playing * 17u + sng_pick * 19u + (uint32_t)song.g[G_BPM] * 23u +
                   (uint32_t)(sng_cur + 1) * 29u;
    if (!ui.force && !ui.msg_t && sig == previous)
        return;
    previous = ui.msg_t ? ~sig : sig;               /* redraw after a message expires */
    cv_begin(240, 40, C_BLACK);
    cv_text(4, 2, &FONT_S, sng_cur >= 0 ? sng_ui_song : "NO SONG", C_WHITE);
    fmt_int(b, song.g[G_BPM]);
    str_cpy(b + str_len(b), " BPM", 5);
    cv_text(236 - text_w(&FONT_S, b), 2, &FONT_S, b, C_GRAY);
    str_cpy(b, sng_pick < SNG_SLOTS ? "REC: SONG " : "REC: SLOOP A-D", 16);
    if (sng_pick < SNG_SLOTS) {                     /* "REC: SONG 3 BLACK" (an empty slot: no name) */
        fmt_int(b + 10, (int32_t)sng_pick + 1);
        b[str_len(b) + 1] = 0;
        b[str_len(b)] = ' ';
        sng_slot_name(sng_pick, b + str_len(b));
    }
    cv_text(4, 20, &FONT_S, b, (int32_t)sng_pick == sng_cur ? C_GRAY : C_AMB);
    cv_blit(0, 0);
    for (i = 0; i < 8u; i++) {                      /* the sections */
        int on = song.playing && (int32_t)i == live_sec, cued = (int32_t)i == live_req;
        cv_begin(240, 20, on ? C_WHITE : C_BLACK);
        b[0] = (char)('1' + i);
        b[1] = 0;
        if ((ready >> i) & 1u) {
            uint32_t m = sng_meter(i);
            sng_name(i, nm);
            cv_text(4, 2, &FONT_S, b, on ? C_BLACK : C_GRAY);
            cv_text(24, 2, &FONT_S, nm, on ? C_BLACK : cued ? C_AMB : C_WHITE);
            if (cued)
                cv_rect(24, 18, 96, 2, C_AMB);
            if (m) {                                /* its meter: "7/8" (4/4 says nothing) */
                fmt_int(b, (int32_t)meter_num(m));
                str_cpy(b + str_len(b), "/", 2);
                fmt_int(b + str_len(b), (int32_t)meter_den(m));
                cv_text(200, 2, &FONT_S, b, on ? C_BLACK : C_GRAY);
            }
        }
        cv_blit(0, 40 + 20 * i);
    }
    cv_begin(240, 40, C_BLACK);
    if (ui.msg_t)
        cv_text(4, 4, &FONT_S, ui.msg, C_HI);
    cv_text(4, 22, &FONT_S, sng_cur >= 0 ? "BLACK < > FILL END STOP TAP" : "KEYS 1-8: LOAD", C_GRAY);
    cv_blit(0, 200);
}
