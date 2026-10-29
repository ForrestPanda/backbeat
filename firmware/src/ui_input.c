/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI input: LEDs, knobs and buttons, the layers (ui_layers.c), holds, SEQ step entry, panel setup. */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_HOME};   /* button of each page family (FAM_TRK, DRUMS: HOME) */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }
static uint32_t cur_btn(void)                      /* the button of the screen shown */
{
    if (!ui.home && cur_page()->scope == SC_SONG)
        return B_SAVE;
    if (!ui.home && cur_page()->scope == SC_DRUM)
        return B_SEQ;
    return FAM_BTN[cur_fam()];
}

/* PLAY flashes on every beat, as heard (the sound leaves ~9 ms after it is rendered) */
static int play_led(void)
{
    uint32_t p = clk_pos, lat = HALF_FRAMES * 3u / 2u * (uint32_t)song.g[G_BPM];
    if (!song.playing)
        return 0;
    p = p >= lat ? p - lat : p + BEAT_U - lat;
    return p < BEAT_U / 4u;
}
/* the tap tempo key's light (the last black key, F#5, on the song screen: ui_drummer.c KEY_TAP): the tempo, a flash on
 * each beat (2026-10-22, asked): playing, PLAY's flash (the beat as heard); the count-in, its clicks; stopped, free at
 * the tempo, so a tap, a MIDI clock or a song loaded shows at once */
static int tempo_led(void)
{
    uint32_t bpm = (uint32_t)song.g[G_BPM], per;
    if (ci_on)
        return ci_u % BEAT_U < BEAT_U / 4u;
    if (song.playing)
        return play_led();
    if (!bpm)
        return 0;
    per = 60000u / bpm;
    return fm1_ms % per < per / 4u;
}

/* the keys' lights: what the layer held does, else the keys down and the drum hits */
/* what sounds on the drum track, as keys: the drum hits (each lights its key a few frames). Shown at once with
 * menu NOTES on, on every screen and layer (keys_notes_dim) */
static uint32_t keys_sounding(const track_t *t)
{
    uint32_t i, m = 0;
    (void)t;
    for (i = 0; i < DRUM_LANES; i++)
        if (pad_lit[i])
            m |= 1u << key_of_white(i);
    return m;
}

static uint32_t erase_lanes(const track_t *t)   /* EDIT erase, drum track: the sounds the pattern holds */
{
    uint32_t i, m = 0;
    if (is_drum(t))
        for (i = 0; i < trk_len(t); i++) {
            uint32_t l, d = dstep_mask(&t->dstep[i]);
            for (l = 0; d; l++, d >>= 1)
                if (d & 1u)
                    m |= 1u << key_of_white(l);
        }
    return m;
}

static uint32_t keys_lit(void)
{
    uint32_t m = 0, i, blink = (fm1_ms / 125u) & 1u;
    track_t *t = TDRUM;
    switch (ui.layer) {
    case LY_FX:
        return 0u;
    case LY_STEP: {                                /* the steps that play; the playhead blinks */
        uint32_t len = trk_len(t);
        for (i = 0; i < 16u; i++) {
            uint32_t idx = ui.step_page * 16u + i, on;
            if (idx >= len)
                continue;
            on = dstep_has(&t->dstep[idx], pen_lane);
            if (song.playing && idx == t->seq_idx)
                on = !on || blink;
            if (on)
                m |= 1u << key_of_white(i);
        }
        return m | fm1_in.notes;
    }
    case LY_ERASE:                                 /* the sounds the pattern holds (NOTES: dim, the hits lit) */
        return (lights_notes ? 0u : erase_lanes(t)) | fm1_in.notes | (lights_notes ? keys_sounding(t) : 0u);
    default:                                       /* playing, ARP roll, SAVE song, every page and the menu */
        break;
    }
    if (kb_grid == 2u) {                           /* the song screen: the section playing lit, the one cued blinking */
        for (i = 0; i < SNG_SECTIONS; i++)
            if ((song.playing && (int32_t)i == live_sec) || ((int32_t)i == live_req && blink))
                m |= 1u << key_of_white(i);
        return m | (tempo_led() ? 1u << KEY_TAP : 0u);   /* the tap tempo key: the tempo */
    }
    if (kb_grid) {                                 /* the DRUMS grid page: the sound's steps of this page */
        uint32_t len = trk_len(TDRUM), b0 = drum_cursor / 16u * 16u;
        for (i = 0; i < 16u; i++) {
            uint32_t idx = b0 + i, on;
            if (idx >= len)
                continue;
            on = dstep_has(&TDRUM->dstep[idx], drum_lane);
            if (song.playing && idx == TDRUM->seq_idx)
                on = !on || blink;
            if (on)
                m |= 1u << key_of_white(i);
        }
        return m | fm1_in.notes;
    }
    m = fm1_in.notes;
    if (is_drum(t) || lights_notes)                /* the drum track: each hit lights its key */
        m |= keys_sounding(t);
    return m;
}

/* menu NOTES, on the layers whose keys are tiles (FX effects, SEQ steps, GLO mute / solo): what
 * sounds glows dimly under the tiles, which keep their full light. SEL (the scale) and EDIT on the
 * drum track (the sounds of the pattern): those glow, and what sounds is lit (keys_lit) */
static uint32_t keys_notes_dim(void)
{
    if (!lights_notes)
        return 0u;
    switch (ui.layer) {
    case LY_FX:
    case LY_STEP:
        return keys_sounding(TDRUM);
    case LY_ERASE:
        return erase_lanes(TDRUM);
    default:
        return 0u;
    }
}

/* the keys' landmarks, dim: the first key of each row of the 4 x 4 grid on screen (white keys
 * 1, 5, 9, 13) while a layer is held, and on the drum track (its 16 sounds, 4 x 4 as on KIT) */
static uint32_t keys_guide(void)
{
    if (ui.layer == LY_PLAY && !is_drum(TDRUM))
        return 0u;
    return 1u << key_of_white(0) | 1u << key_of_white(4) | 1u << key_of_white(8) | 1u << key_of_white(12);
}

/* the keys the backlight lights (menu KEYS): the Cs, every white key, or every key; bit k = key k (0 = F3) */
static uint32_t lights_keys_mask(void)
{
    uint32_t k, m = 0;
    if (!lights_lvl || !lights_keys)
        return 0u;
    for (k = 0; k < 27u; k++) {
        uint32_t pc = (53u + k) % 12u;             /* key 0 = F3 (53) */
        if (lights_keys == KEYS_ALL || (lights_keys == KEYS_C ? pc == 0u : ((0xAB5u >> pc) & 1u) != 0u))   /* 0xAB5: C D E F G A B */
            m |= 1u << k;
    }
    return m;
}

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0}, bl[FM1_NCOL] = {0};
    uint32_t k, c, keys, guide, back;
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[ui.layer != LY_PLAY ? LAYER_BTN[ui.layer] : cur_btn()],
            ly_lock == LY_PLAY || ((fm1_ms / 300u) & 1u) != 0u || (fm1_in.buttons & ly_bit[ly_lock % LY_COUNT]) != 0u);   /* locked: blinks */
    led_put(nl, panel.btn[B_PLAY], play_led() || (ci_on && ci_u % BEAT_U < BEAT_U / 4u));   /* (the count-in's beats) */
    led_put(nl, panel.btn[B_REC], ui.hold_kind == 1u && ((fm1_ms / 60u) & 1u));   /* blinks fast: clearing */
    if (ui.layer == LY_FX) {                       /* FX: OCT- / OCT+ lit while REV / ECHO THROW (2.6) */
        led_put(nl, panel.btn[B_OCTDN], (thr_req & 1u) != 0u);
        led_put(nl, panel.btn[B_OCTUP], (thr_req & 2u) != 0u);
    } else if (is_drum(TDRUM)) {                    /* the drum track: OCT- / OCT+ lit while ghost / hard */
        led_put(nl, panel.btn[B_OCTDN], (fm1_in.buttons & dyn_bit[0]) != 0u);
        led_put(nl, panel.btn[B_OCTUP], (fm1_in.buttons & dyn_bit[1]) != 0u);
    } else {
        led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
        led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    }
    keys = keys_lit();
    guide = (keys_guide() | keys_notes_dim()) & ~keys;
    back = lights_keys_mask() & ~keys & ~guide;
    for (k = 0; k < 27u; k++) {
        led_put(nl, 14u + k, (int)((keys >> k) & 1u));
        led_put(dl, 14u + k, (int)((guide >> k) & 1u));
        led_put(bl, 14u + k, (int)((back >> k) & 1u));
    }
    if (lights_lvl)                                /* menu LIGHTS: every button glows, the lit ones stay full */
        for (k = 0; k < NB; k++)
            led_put(bl, panel.btn[k], 1);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
        fm1_led_bg[c] = (uint8_t)(bl[c] & ~nl[c]);
    }
    fm1_led_bg_ns = LIGHTS_NS[lights_lvl % LIGHTS_N];
}

/* ---------------------------------------------------------- input --- */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ticks(), dt = now - ui.enc_t[role];
    ui.enc_t[role] = now;
    if (range > 40 && dt < 60u * 1000u * FM1_TICKS_PER_US)
        return s * (range > 150 ? 6 : 3);
    return s;
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    uint32_t id = pg->id[slot];
    int32_t v;
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = d->names == ESYNC_NAMES || d->names == N_IBITS || d->names == N_IRATE ? names_turn(d, *vp, steps)   /* (a name a detent) */
                                    : clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    if (pg->scope != SC_GLOBAL && id == P_SDIV)
        pattern_div_set(TDRUM, (uint32_t)v, 0);           /* the PATTERN page's DIV: the pattern follows (ui.c) */
    else
        *vp = (int16_t)v;
    if (pg->scope != SC_GLOBAL && p_lockable(id))
        ui.lock_par = (uint8_t)id;                        /* the SEQ layer's lock parameter: the last one touched */
    if (pg->scope != SC_GLOBAL)
        ml_knob(TDRUM, id);                                /* MIDI LEARN: this parameter (2.6) */
}

/* HOME: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* a layer button tapped (pressed and let go, nothing touched): its pages, as before the layers */
static void layer_tap(uint32_t layer)
{
    switch (layer) {
    case LY_FX:
        open_family(FAM_FX);
        break;
    case LY_ERASE:
    case LY_STEP:
        if (on_drum_page()) {                             /* DRUMS: GRID <-> KIT */
            drum_page = (uint8_t)((drum_page + 1u) % 2u);
            ui.force = 1;
            break;
        }
        studio_open(SC_DRUM);                             /* fm1-drummer: the grid of the section playing */
        break;
    /* (ARP, SEL: no pages, drums only; ENV, LFO: LIVE's PREV and NEXT, ui_drummer.c) */
    /* (SAVE: no layer since 2026-10-27, JAM's went; tapped it opens SONGS, held on the drum grid it saves the
     * section: holds_input) */
    default:
        break;
    }
}

/* a layer locked open: a layer button held + HOME tapped. The layer stays with the button let go (both
 * hands free for the keys and the knobs); any other button but PLAY, REC and OCT- / OCT+ lets it go
 * (and does only that: its press is eaten) */
static uint8_t home_eat;                                  /* HOME pressed to unlock: its tap is eaten */
static void layer_unlock(void)
{
    if (ly_lock != LY_PLAY) {
        ly_lock = LY_PLAY;
        ui.force = 1;
    }
}

/* #39 of Felucca: a layer that showed lets go; KNOB 1..4 belong to no page for LY_QUIET_MS after (the knob still
 * turning as the button comes up is the layer's, not the page's under it). fm1_ms | 1, 0 = none */
#define LY_QUIET_MS 250u
static uint32_t ly_quiet_t;
static int ly_quiet(void)
{
    if (ly_quiet_t && fm1_ms - (ly_quiet_t & ~1u) >= LY_QUIET_MS)
        ly_quiet_t = 0;
    return ly_quiet_t != 0;
}
/* the layers, once a frame: which one is held (or locked), the taps on release, its keys and knobs.
 * Returns 1 while one is held or locked (the page does not take the knobs then) */
static int layers_input(uint32_t note_edges, uint32_t *pressed, uint32_t home)
{
    static uint8_t down[LY_COUNT], used[LY_COUNT];
    static uint32_t t0[LY_COUNT];
    uint32_t l, now = fm1_ms, held = LY_PLAY, eat = 0;
    if (ly_lock != LY_PLAY) {
        uint32_t keep = 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_REC] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
        eat = *pressed & ~keep;
        if (eat) {
            layer_unlock();
            if (eat & 1u << panel.btn[B_HOME])
                home_eat = 1;
            *pressed &= ~eat;
        }
    }
    for (l = LY_FX; l < LY_COUNT; l++) {
        uint32_t d = (fm1_in.buttons & ly_bit[l]) != 0u;
        if (d && !down[l]) {
            t0[l] = now;
            used[l] = (uint8_t)((eat & ly_bit[l]) != 0u);  /* (the press that unlocked: not a tap) */
        }
        if (d && note_edges)
            used[l] = 1;                                  /* a key while held: not a tap */
        if (!d && down[l] && !used[l] && now - t0[l] < TAP_MS && !ui.menu && !ui.confirm)
            layer_tap(l);
        down[l] = (uint8_t)d;
        if (d && held == LY_PLAY)
            held = l;
    }
    if (held != LY_PLAY && home == BT_TAP && !home_eat) {  /* held + HOME: locked open */
        ly_lock = (uint8_t)held;
        used[held] = 1;
        ui.layer = (uint8_t)held;
        ui.force = 1;
    }
    if (held == LY_PLAY && ly_lock != LY_PLAY) {
        held = ly_lock;
        used[held] = 1;
    }
    if (held != LY_PLAY && held != ui.layer && ui.layer != LY_PLAY)
        ui.layer = (uint8_t)held;                         /* (from one layer straight to another) */
    if (held == LY_PLAY) {
        thr_req = 0;                                      /* (the THROWs: FX let go) */
        while (lk_r != lk_w) {                            /* a key let go after its layer: its release only */
            uint32_t e = lk_q[lk_r % LKQ];
            lk_r++;
            if ((e >> 8) == KB_GRID) {                    /* the DRUMS grid page: a step key */
                if (((e >> 7) & 1u) && grid_keys_on())
                    grid_key(e & 31u, (e & KB_OCTDN) ? LV_GHOST : (e & KB_OCTUP) ? LV_HARD : LV_NORM);
                else if (((e >> 7) & 1u) && drummer_keys_on())
                    drummer_key(e & 31u, (e & KB_OCTUP) != 0u);
            } else if (!((e >> 7) & 1u)) {
                layer_key(e >> 8, e & 31u, 0);
            }
        }
        if (ui.layer != LY_PLAY) {
            ly_quiet_t = fm1_ms | 1u;                     /* a layer shown lets go: KNOB 1..4 quiet a while (#39) */
            ui.layer = LY_PLAY;
            ui.step_held = 0;
            ui.force = 1;                                 /* the page comes back */
        }
        ui.step_sess = 0;
        return 0;
    }
    ui.layer_used = used[held];
    while (lk_r != lk_w) {                                /* the keys of SEQ, SEL, GLO (seq.c) */
        uint32_t e = lk_q[lk_r % LKQ];
        lk_r++;
        used[held] = 1;
        layer_key(e >> 8, e & 31u, (e >> 7) & 1u);
    }
    layer_knobs(held);
    if (ui.layer_used)
        used[held] = 1;
    if (ui.layer == LY_PLAY && (used[held] || now - t0[held] >= SHOW_MS)) {
        ui.layer = (uint8_t)held;                         /* show it (not for a quick tap) */
        if (held == LY_STEP && (uint32_t)ui.step_page * 16u >= trk_len(TDRUM))
            ui.step_page = 0;
    }
    {   /* FX + OCT- / OCT+, held (2.6, after Felucca 1.5.1): REV THROW / ECHO THROW (fx.c) */
        uint32_t q = 0;
        if (held == LY_FX) {
            q = ((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u) | ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u) << 1;
            if (q)
                used[held] = 1;
        }
        if (q != thr_req)
            ui.force = 1;                                 /* (the layer's sub line says it) */
        thr_req = (uint8_t)q;
    }
    if (held == LY_ERASE) {                               /* EDIT + OCT- / OCT+: undo / redo */
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev;
        prev = b;
        if (press) {
            used[held] = 1;
            if (press & ob)
                ui_message(undo_swap(0) ? "UNDO" : "NOTHING TO UNDO");
            else
                ui_message(undo_swap(1) ? "REDO" : "NOTHING TO REDO");
        }
    }
    if (held == LY_STEP) {                                /* SEQ + OCT- / OCT+: the page; a step held: OCT- its nudge, locks and
                                                           * condition go, OCT+ its fill condition (normal, fill only, no fill) */
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev, pages = (trk_len(TDRUM) + 15u) / 16u;
        prev = b;
        if (press) {
            used[held] = 1;
            if ((press & ob) && ui.step_held)
                steps_held_clear();
            else if ((press & pb) && ui.step_held)
                steps_held_fill();
            else
                ui.step_page = (uint8_t)((ui.step_page + ((press & pb) ? 1u : pages - 1u)) % pages);
        }
    }
    return 1;
}

/* REC (live recording is gone, drums only): held 0.7 s a ring fills: held on to the end, the selected track is
 * cleared (EDIT + OCT- brings it back); let go before, nothing happens.
 * SAVE (fm1-drummer; SLOOP's SAVE layer, JAM's sections A..D, went 2026-10-27): tapped, the SONGS list (from SONGS,
 * LIVE); on LIVE or the drum grid (EDIT) held 0.7 s a ring fills, and held on to the end the section playing is
 * written into its song (no song loaded: it says so). LIVE too since 2026-10-27 (the user: held there, on the unit,
 * it did nothing; the section is the same one, LIVE is where it plays). SONGS: a long press does nothing. */
static int on_live_page(void) { return drummer_on() && sng_view == DV_LIVE; }
static void section_keep(void)
{
    static const char *const MSG[6] = {"SAVED INTO THE SONG", "STOP TO SAVE", "SONG FULL", "SAVE ERROR", "",
                                       "FACTORY: READ-ONLY"};
    int r;
    if (sng_cur < 0 || live_sec < 0) {
        ui_message("NO SONG TO SAVE INTO");
        return;
    }
    r = sng_save_section();
    if (!r && sng_lossy())
        ui_message("SAVED, NOT THE NUDGES");               /* (a packed section has no room for nudges and locks) */
    else
        ui_message(MSG[r % 6]);
}
static void holds_input(uint32_t pressed, uint32_t now_ms)
{
    static uint8_t rec_on, save_on;                       /* *_on: pressed, *_t0 its time */
    static uint32_t rec_t0, save_t0;
    uint32_t rb = 1u << panel.btn[B_REC], sb = 1u << panel.btn[B_SAVE];
    uint32_t rec_down = (fm1_in.buttons & rb) != 0u, save_down = (fm1_in.buttons & sb) != 0u;
    if (on_song_page())
        rec_down = 0, pressed &= ~rb;                     /* (the song page: REC stores a section) */
    /* REC */
    if (pressed & rb) {
        rec_t0 = now_ms;
        rec_on = 1;
    }
    if (rec_down && rec_on && !ui.hold_kind && now_ms - rec_t0 >= 700u) {
        ui.msg_t = 0;
        ui.hold_kind = 1;
        ui.hold_t0 = now_ms;
        ui.force = 1;
    }
    if (ui.hold_kind == 1u && rec_down && now_ms - ui.hold_t0 >= HOLD_CLEAR_MS) {
        track_t *t = TDRUM;
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        track_defaults_steps(t);
        fm1_irq_on();
        ui_message("PATTERN CLEARED");
        ui.hold_kind = 0;
        rec_on = 0;                                       /* (until REC is up: nothing more) */
        ui.force = 1;
    }
    if (!rec_down) {
        if (ui.hold_kind == 1u) {
            ui.hold_kind = 0;                             /* let go before the end: nothing */
            ui.force = 1;
        }
        rec_on = 0;
    }
    /* SAVE: a tap anywhere opens SONGS (from SONGS, LIVE); the hold is LIVE's and the drum grid's */
    if (pressed & sb) {
        save_t0 = now_ms;
        save_on = 1;
    }
    if (save_down && save_on && !ui.hold_kind && (on_drum_page() || on_live_page()) && now_ms - save_t0 >= 700u) {
        ui.msg_t = 0;
        ui.hold_kind = 2;
        ui.hold_t0 = now_ms;
        ui.force = 1;
    }
    if (ui.hold_kind == 2u && save_down && now_ms - ui.hold_t0 >= HOLD_SAVE_MS) {
        ui.hold_kind = 0;
        save_on = 0;                                      /* (until SAVE is up: nothing more) */
        section_keep();
        ui.force = 1;
    }
    if (!save_down) {
        if (ui.hold_kind == 2u) {
            ui.hold_kind = 0;                             /* let go before the end: nothing */
            ui.force = 1;
        } else if (save_on && now_ms - save_t0 < 700u) {
            if (drummer_on())                             /* a tap: SONGS (from SONGS: LIVE) */
                drummer_open(sng_view == DV_LIB ? DV_LIVE : DV_LIB);
            else
                studio_open(SC_SONG);
        }
        save_on = 0;
    }
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    int32_t s;
    int layered;
    enc_hold = 0;                                       /* (panel.c: every knob readable again this pass) */
    if (pressed || notes)
        ui_input_ms = fm1_ms;
    pc_poll();                                          /* MIDI Program Change: a sound (2.6, ui.c) */
    ml_poll();                                          /* MIDI LEARN: a CC came for the picked parameter (2.6) */
    if (div_req) {                                      /* a learned CC set DIV (seq.c midi_cc_set): the pattern follows */
        uint32_t d = div_req - 1u;
        div_req = 0;
        pattern_div_set(TDRUM, d, 0);
    }
    kb_grid = (uint8_t)(grid_keys_on() ? 1u : drummer_keys_on() ? 2u : 0u);   /* (seq.c: the keys are the grid's
                                                         * steps; 2: the song screen's) */
    if (ui.menu) {                                      /* SETUP (ui_menu.c): GLO, OCT- or HOME close it */
        thr_req = 0;
        menu_input(pressed, home);
        return;
    }
    if (((pressed >> panel.btn[B_GLO]) & 1u) && !ui.hold_kind && ly_lock == LY_PLAY && ui.layer == LY_PLAY) {
        menu_open();                                    /* fm1-drummer: GLO tapped, SETUP (the GLO pages and layer gone) */
        return;
    }
    layered = layers_input(notes, &pressed, home);
    if (home_eat && !((fm1_in.buttons >> panel.btn[B_HOME]) & 1u)) {   /* (the HOME that unlocked: let go) */
        if (home == BT_TAP)
            home = BT_NONE;
        home_eat = 0;
    }
    pressed &= ~(ly_bit[LY_FX] | ly_bit[LY_ERASE] | ly_bit[LY_ROLL] | ly_bit[LY_STEP]);
    holds_input(pressed, fm1_ms);
    pressed &= ~((on_song_page() ? 0u : 1u << panel.btn[B_REC]) | (1u << panel.btn[B_SAVE]));
    if (layered || ui.hold_kind) {                      /* a layer / a hold: the rest waits */
        if ((s = panel_enc(EN_SELECT)) != 0) {           /* (the tempo always) */
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;
        }
        panel_enc(EN_ALGO);                             /* (track and sound wait: no jump afterwards) */
        panel_enc(EN_PRESET);
        if (ui.hold_kind)
            for (k = 0; k < 4u; k++)
                panel_enc(EN_K1 + k);
        pressed &= ~((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]));   /* (undo, pages, ghost / hard) */
        if (!(pressed & (1u << panel.btn[B_PLAY])))
            return;
        pressed &= 1u << panel.btn[B_PLAY];             /* PLAY still plays */
        enc_hold = (1u << NE) - 1u;                     /* #102: the knobs the layer took are not read again this
                                                         * pass (a detent counted since went to the page too) */
    } else if (ly_quiet()) {                            /* #39: the turns as a layer lets go are dropped */
        for (k = 0; k < 4u; k++)
            panel_enc(EN_K1 + k);
        enc_hold |= 15u << EN_K1;
    }
#if FELUCCA_DRUMMER
    if (drummer_on()) {
        drummer_input(pressed, home);
        return;
    }
    if (on_drum_page()) {
        drum_screen_input(pressed, home);
        return;
    }
#endif
    if (home == BT_TAP) {                               /* HOME acts on release: a hold opens the menu */
        go_home();                                      /* (fm1-drummer: LIVE) */
    }
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            transport_req = song.playing ? 2 : 1;
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if (is_drum(TDRUM))
                break;                                  /* the drum track: ghost / hard while held (seq.c) */
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        default:
            break;
        }
    }

    if ((s = panel_enc(EN_PRESET)) != 0 && (ui.home || cur_fam() == FAM_TRK))
        TDRUM->p[P_E0] = drum_kit_step(TDRUM->p[P_E0], s);   /* PRESETS: the kit (a page with no use for it) */
    panel_enc(EN_ALGO);                                 /* ALGORITHM: nothing here (one track; in the SEQ layer, with a
                                                         * step held: the lock parameter, ui_layers.c) */
    if ((s = panel_enc(EN_SELECT)) != 0 && !page_walk(s)) {   /* SELECT: the pages of the family shown, else
                                                     * the tempo */
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.home || page_desc(pg, k, &hv)) {   /* (not an empty column) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max);
        } else {
            edit_param(k, s);
        }
    }
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_WHITE, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_GRAY, 1);
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_WHITE, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_WHITE, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    layers_init();
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
