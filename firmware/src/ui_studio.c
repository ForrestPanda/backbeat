/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE screens, 240 x 240, in a teenage-engineering-like style: black, four colours (one per
 * track and per knob: blue, green, yellow, orange), white for what you touch, red for recording,
 * big numbers, lowercase labels, four dials at the bottom that show what KNOB 1..4 do.
 * Screens: DRUMS (EDIT: GRID / KIT, pads that flash on each hit; fm1-drummer's LIVE and SONGS: ui_drummer.c), the
 * LAYERS (a function button held: what the 16 white keys and the knobs do now), HOLD (a hold to
 * confirm: clear, save), REC (armed / free take). Every band is drawn into the canvas only when its
 * signature changed. */
static uint8_t drum_page, drum_lane, drum_cursor;   /* drum_page: 0 GRID, 1 KIT */
static int on_drum_page(void) { return !ui.home && cur_page()->scope == SC_DRUM; }

/* ---------------------------------------------------------------- style --- */
#define TE_G1 RGB(26, 26, 30)            /* tiles */
#define TE_G2 RGB(54, 54, 60)            /* empty steps, dial tracks */
#define TE_G3 RGB(118, 118, 126)         /* labels */
#define TE_G4 RGB(196, 196, 204)         /* secondary text */
#define TE_RED RGB(255, 44, 52)          /* recording, erasing */
static const uint16_t TE_COL[4] = {RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 198, 24), RGB(255, 98, 26)};
static const uint16_t TE_MID[4] = {RGB(26, 82, 170), RGB(20, 136, 76), RGB(170, 132, 16), RGB(170, 66, 18)};
static const uint16_t TE_DIM[4] = {RGB(14, 40, 86), RGB(10, 66, 38), RGB(86, 66, 8), RGB(86, 32, 8)};
#define TE_DRUM TE_COL[3]

static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)     /* filled circle */
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}
/* a dial: a 270-degree ring (lit up to the value), a pointer; ratio 0..1000, -1 = a plain ring */
static void te_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t dim)
{
    int32_t i, end = ratio < 0 ? 768 : ratio * 768 / 1000;
    for (i = 0; i <= 768; i += 8) {
        uint32_t a = (uint32_t)(384 + i) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a], k;
        uint16_t col = ratio < 0 || i <= end ? c : dim;
        for (k = r - 2; k <= r; k++)
            cv_pset(cx + ((co * k) >> 15), cy + ((si * k) >> 15), col);
    }
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line(cx, cy, cx + ((co * (r - 4)) >> 15), cy + ((si * (r - 4)) >> 15), C_WHITE);
        te_disc(cx, cy, 2, C_WHITE);
    }
}
static void te_play_icon(int32_t x, int32_t y, int playing)
{
    int32_t i;
    if (playing)
        for (i = 0; i < 12; i++)
            cv_rect(x + i, y + i / 2, 1, 14 - i, C_WHITE);   /* a triangle */
    else
        cv_rect(x, y + 1, 12, 12, TE_G3);
}
static uint32_t studio_hash(uint32_t h, const char *p)
{ while (*p) h = h * 31u + (uint8_t)*p++; return h; }
static void te_lower(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}
static void te_text_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_S, s) / 2, y, &FONT_S, s, c);
}

/* the dial strip: KNOB 1..4, label + value under each (a dial with no label: an empty column); a
 * message replaces it */
static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache)
{
    uint32_t k;
    sig = studio_hash(sig, ui.msg_t ? ui.msg : "");
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, ui.msg_t ? 240u - (uint32_t)y0 : 40u, C_BLACK);
    if (ui.msg_t) {                                     /* a message: a white bar */
        cv_rect(0, 10, 240, 32, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 18, &FONT_S, ui.msg, C_BLACK);
        cv_blit(0, (uint32_t)y0);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k;
        if (!lab[k][0])
            continue;
        te_dial(cx, 12, 11, ratio[k], TE_COL[k], TE_DIM[k]);
        te_text_c(cx, 24, lab[k], TE_G3);
    }
    cv_blit(0, (uint32_t)y0);
    cv_begin(240, 16, C_BLACK);                         /* the values: white while turned */
    for (k = 0; k < 4u; k++)
        te_text_c(30 + 60 * (int32_t)k, 0, val[k], ui.hot_t && ui.hot_col == k ? C_WHITE : TE_G4);
    cv_blit(0, (uint32_t)y0 + 40u);
}

static void studio_open(uint32_t scope)
{
    uint32_t i;
    if (scope == SC_SONG)                               /* fm1-drummer: the SONGS list (ui_drummer.c) */
        drummer_open(DV_LIB);
    for (i = 0; i < NPAGES; i++) if (PAGES[i].scope == scope) {
        ui.page = (uint8_t)i; ui.home = 0; song.seq_mode = 0; ui.force = 1;
        ui.msg_t = 0; ui.entry_open = 0; return;
    }
}
/* the loop position of track t: "bar.beat" in its pattern (1.1 .. ) */
static void loop_pos(const track_t *t, char *b)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s = t->seq_idx % len, k;
    fmt_int(b, (int32_t)(s / 16u + 1u));
    k = str_len(b);
    b[k] = '.';
    b[k + 1] = (char)('1' + (s % 16u) / 4u);
    b[k + 2] = 0;
}

/* the header of the live screens: BPM, transport, the loop position, the beat lights, REC */
static void te_header(const char *title, uint16_t tc, uint32_t *cache)
{
    char b[12];
    uint32_t beat = clk_beat, k, playing = song.playing;
    uint32_t sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u +
                               (playing ? beat * 131u + TDRUM->seq_idx * 7919u : 0u) + (uint32_t)song.g[G_CLOCK] * 77u +
                               arrangement_enabled * 5u + (ui.bpm_t != 0) * 104729u, title);
    sig = sig * 31u + tc + (uint32_t)(sng_cur + 1) * 7u;
#if FELUCCA_DRUMMER
    sig = studio_hash(sig + live_bar * 17u + (uint32_t)(live_sec + 1) * 23u, sng_ui_name);   /* (fm1-drummer) */
    sig = studio_hash(sig, sng_ui_next);
#endif
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 40, C_BLACK);
    fmt_int(b, song.g[G_BPM]);
    cv_text(4, 4, &FONT_L, b, ui.bpm_t ? C_WHITE : TE_G4);
    cv_text(4 + text_w(&FONT_L, b) + 4, 20, &FONT_S, "bpm", TE_G3);
    te_play_icon(104, 6, (int)playing);
    if (playing) {                                     /* bar.beat in the selected track's loop */
        loop_pos(TDRUM, b);
        cv_text(122, 5, &FONT_S, b, C_WHITE);
    } else {
        cv_text(122, 5, &FONT_S, arrangement_enabled ? "song" : "loop", TE_G3);
    }
    for (k = 0; k < 4u; k++)                           /* the four beats of the bar */
        cv_rect(104 + (int32_t)k * 9, 26, 7, 7, playing && beat % 4u == k ? (k ? C_WHITE : TE_RED) : TE_G2);
#if FELUCCA_DRUMMER
    if (sng_ui_name[0] && live_sec >= 0) {             /* fm1-drummer: the section playing (or cued), white, in the
                                                        * title's place; under it the bar it is in */
        char nm[13], full[13], next[13];
        uint32_t n = 0;
        fm1_irq_off();                                 /* (the ISR writes them: taken whole) */
        str_cpy(full, sng_ui_name, sizeof full);
        str_cpy(next, sng_ui_next, sizeof next);
        fm1_irq_on();
        while (n < 12u && full[n]) {                   /* (the name cut to the room right of the beat lights) */
            nm[n] = full[n];
            nm[n + 1] = 0;
            if (text_w(&FONT_S, nm) > 92)
                break;
            n++;
        }
        nm[n] = 0;
        cv_text(236 - text_w(&FONT_S, nm), 5, &FONT_S, nm, playing ? C_WHITE : TE_G3);
        if (playing && next[0]) {                      /* the one asked for, coming on the bar */
            char nx[14];
            nx[0] = '>';
            str_cpy(nx + 1, next, sizeof nx - 1u);
            while (text_w(&FONT_S, nx) > 92)
                nx[str_len(nx) - 1u] = 0;
            cv_text(236 - text_w(&FONT_S, nx), 22, &FONT_S, nx, C_AMB);
        } else if (playing) {
            str_cpy(b, "bar ", 5);
            fmt_int(b + 4, (int32_t)(live_bar == 0xFFFFFFFFu ? 1u : live_bar + 1u));
            cv_text(236 - text_w(&FONT_S, b), 22, &FONT_S, b, TE_G3);
        } else if (sng_cur >= 0) {                     /* stopped: whose section (EDIT: SAVE held writes it there) */
            sng_label(b, (uint32_t)sng_cur);
            cv_text(236 - text_w(&FONT_S, b), 22, &FONT_S, b, TE_G3);
        } else {
            cv_text(236 - text_w(&FONT_S, "cued"), 22, &FONT_S, "cued", TE_G3);
        }
    } else
#endif
    {
        cv_text(240 - text_w(&FONT_S, title) - 4, 5, &FONT_S, title, tc);
    }
    cv_rect(0, 39, 240, 1, TE_G1);
    cv_blit(0, 0);
}

/* swing, MPC style: "62%" */
static void swing_str(char *b, int32_t v)            /* 0 straight .. 100 the most (params.c F_SWING) */
{
    fmt_int(b, clamp(v, 0, 100));
}

/* ---------------------------------------------------------------- DRUMS --- */
static const char *const LV_NAME[4] = {"norm", "ghost", "soft", "hard"};
static uint32_t lvl_rank(uint32_t lvl);                 /* (below) ghost 0, soft 1, norm 2, hard 3 */
static uint16_t lvl_col(uint32_t lvl)                   /* a hit's colour by its level */
{
    return lvl == LV_GHOST ? TE_DIM[3] : lvl == LV_SOFT ? TE_MID[3] : lvl == LV_HARD ? C_WHITE : TE_DRUM;
}
static uint16_t pad_lit[DRUM_LANES];                   /* pads and key LEDs: frames left lit */
static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits;
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
}

static void drum_screen_draw(void)
{
    static uint32_t head, title_sig, body_sig, footer;
    uint32_t i, j, len = (uint32_t)clamp(TDRUM->p[P_SLEN], 1, 64), kit = drum_kit(), sig, bank;
    if (drum_cursor >= len) drum_cursor = (uint8_t)(len - 1u);
    bank = drum_cursor / 16u;
    {
        char st[16], nm[9];
        const char *style = DRUM_KIT_STYLES[kit];
        if (kit >= DRUM_SYN && kit < DRUM_SYN + DSU_N) {   /* SYN1..4: the name you gave it (editor DRUM SYNTH; not
                                                          * MULDJORD after them) */
            memcpy(nm, dsu_kit(kit - DRUM_SYN)->name, 8);
            nm[8] = 0;
            if (nm[0])
                style = nm;
        }
        te_lower(st, style, sizeof st);
        st[12] = 0;
        te_header(st, TE_DRUM, &head);
    }
    sig = kit * 131u + drum_page * 7u + drum_lane * 977u + bank * 31u + len;   /* title band */
    if (ui.force || sig != title_sig) {
        char b[8];
        title_sig = sig;
        cv_begin(240, 44, C_BLACK);
        cv_rect(2, 4, 34, 34, TE_DRUM);
        fmt_int(b, (int32_t)kit + 1);
        cv_text(19 - text_w(&FONT_S, b) / 2, 13, &FONT_S, b, C_BLACK);
        cv_text(44, 6, &FONT_L, DRUM_KIT_NAMES[kit], C_WHITE);
        cv_text(202, 4, &FONT_S, "grid", drum_page == 0 ? C_WHITE : TE_G3);
        cv_text(202, 22, &FONT_S, "kit", drum_page == 1 ? C_WHITE : TE_G3);
        cv_rect(196, drum_page ? 26 : 8, 3, 9, TE_DRUM);
        cv_blit(0, 40);
    }
    sig = drum_page + drum_lane * 7u + drum_cursor * 101u + song.playing * 71u + len * 3u;
    if (song.playing) sig = sig * 31u + TDRUM->seq_idx;
    for (i = 0; i < DRUM_LANES; i++) sig = sig * 3u + (pad_lit[i] != 0);
    for (i = 0; i < len; i++) {
        const dstep_t *s = &TDRUM->dstep[i];
        sig = sig * 31u + dstep_mask(s);
        sig = sig * 31u + s->lvl[0] + s->lvl[1] * 7u + s->lvl[2] * 49u + s->lvl[3] * 343u;
        sig = sig * 31u + s->rat[0] + s->rat[1] * 7u + s->rat[2] * 49u + s->rat[3] * 343u;
    }
    if (ui.force || sig != body_sig) {
        body_sig = sig;
        cv_begin(240, 100, C_BLACK);
        if (!drum_page) {                              /* GRID: the 16 lanes x 16 steps of this bank */
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t y = (int32_t)i * 6 + 2;
                cv_rect(2, y, 6, 5, pad_lit[i] ? C_WHITE : i == drum_lane ? TE_DRUM : TE_G2);
                for (j = 0; j < 16u; j++) {
                    uint32_t p = bank * 16u + j;
                    const dstep_t *s = &TDRUM->dstep[p < NSTEP ? p : 0];
                    uint16_t c;
                    if (p >= len) continue;
                    c = dstep_has(s, i) ? lvl_col(dstep_lvl(s, i)) : i == drum_lane ? TE_G2 : TE_G1;
                    if (song.playing && p == TDRUM->seq_idx && c == TE_G1) c = TE_G2;
                    cv_rect(12 + (int32_t)j * 14, y, 12, 5, c);
                    if (dstep_has(s, i) && dstep_rat(s, i)) {   /* a ratchet: a notch per extra hit */
                        uint32_t r;
                        for (r = 0; r < dstep_rat(s, i); r++)
                            cv_rect(13 + (int32_t)j * 14 + (int32_t)r * 3, y + 2, 2, 1, C_BLACK);
                    }
                    if (p == drum_cursor && i == drum_lane) {
                        cv_rect(11 + (int32_t)j * 14, y - 1, 14, 1, C_WHITE);
                        cv_rect(11 + (int32_t)j * 14, y + 5, 14, 1, C_WHITE);
                    }
                }
            }
        } else {                                       /* KIT: 16 pads, lit on each hit */
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t x = 2 + (int32_t)(i % 4u) * 60, y = (int32_t)(i / 4u) * 25;
                cv_rect(x, y, 56, 23, pad_lit[i] ? TE_DRUM : TE_G1);
                te_text_c(x + 28, y + 4, LANE_SHORT[i], pad_lit[i] ? C_BLACK : i == drum_lane ? C_WHITE : TE_G3);
            }
        }
        cv_blit(0, 84);
    }
    {
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        static const char *const LG[4] = {"sound", "step", "hit", "level"}, *const LK[4] = {"kit", "level", "reverb", "pan"};
        int32_t ratio[4];
        if (!drum_page) {
            const dstep_t *s = &TDRUM->dstep[drum_cursor];
            str_cpy(v[0], LANE_SHORT[drum_lane], 8);
            fmt_int(v[1], drum_cursor + 1);
            str_cpy(v[2], dstep_has(s, drum_lane) ? "on" : "--", 4);
            str_cpy(v[3], dstep_has(s, drum_lane) ? LV_NAME[dstep_lvl(s, drum_lane)] : "--", 8);
            ratio[0] = (int32_t)drum_lane * 1000 / (DRUM_LANES - 1);
            ratio[1] = (int32_t)drum_cursor * 1000 / (int32_t)(len > 1u ? len - 1u : 1u);
            ratio[2] = v[2][0] == 'o' ? 1000 : 0;
            ratio[3] = dstep_has(s, drum_lane) ? (int32_t)lvl_rank(dstep_lvl(s, drum_lane)) * 333 : 0;   /* ghost left ..
                                                                     * hard right (2.5: it followed the stored order) */
            te_dials(184, LG, val, ratio, 1u, &footer);
        } else {
            fmt_int(v[0], (int32_t)kit + 1);
            fmt_int(v[1], song.g[G_DRLVL] * 100 / 127);
            fmt_int(v[2], song.g[G_DRREV] * 100 / 127);
            fmt_int(v[3], TDRUM->p[P_PAN]);
            ratio[0] = (int32_t)kit * 1000 / (int32_t)(DRUM_KITS - 1u);
            ratio[1] = song.g[G_DRLVL] * 1000 / 127;
            ratio[2] = song.g[G_DRREV] * 1000 / 127;
            ratio[3] = (TDRUM->p[P_PAN] + 64) * 1000 / 127;
            te_dials(184, LK, val, ratio, 2u, &footer);
        }
    }
}

/* the level order of the knobs: ghost < soft < norm < hard */
static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }

/* the GRID page shown, no layer held: its keys are steps (seq.c kb_grid) */
static int grid_keys_on(void) { return on_drum_page() && !drum_page && !ui.menu; }
/* a key down on the GRID page: the white keys are the 16 steps of this page of steps for the sound of
 * KNOB 1 — an empty one is set (NORM, the sound heard), a set one cleared; the first four black keys
 * pick the page (steps 1-16, 17-32, 33-48, 49-64), as in the SEQ layer. The cursor follows */
static void grid_key(uint32_t k)
{
    static const int8_t PG[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
    uint32_t len = trk_len(TDRUM), idx;
    int32_t w = key_white(k);
    dstep_t *st;
    if (w < 0) {
        if (k < 12u && PG[k] >= 0 && (uint32_t)PG[k] * 16u < len) {
            idx = (uint32_t)PG[k] * 16u + drum_cursor % 16u;
            drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
            ui.force = 1;
        }
        return;
    }
    idx = drum_cursor / 16u * 16u + (uint32_t)w;
    if (idx >= len)
        return;
    drum_cursor = (uint8_t)idx;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    st = &TDRUM->dstep[idx];
    fm1_irq_off();
    if (dstep_has(st, drum_lane)) {
        drum_hit_clr(TDRUM, idx, drum_lane);
        if (!dstep_mask(st)) {                          /* an empty step keeps no lock, no nudge, no condition */
            lock_del(TDRUM, idx, P_COUNT);
            TDRUM->micro[idx] = 0;
            step_fill_set(TDRUM, idx, FC_NORM);
        }
    } else {
        dstep_set(st, drum_lane, LV_NORM, 0);
        TDRUM->seq_active = 1;
    }
    fm1_irq_on();
    if (dstep_has(st, drum_lane))
        audition_lane(drum_lane);
    sync_reload = 1;
}

static void drum_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k, b;
    int32_t s;
    if (home == 1u) {
        go_home();
        return;
    }
    for (k = 0; k < NB; k++) if ((pressed >> panel.btn[k]) & 1u) {
        b = k;
        if (b == B_PLAY) {
            if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) ui_message("EMPTY SECTION: REC");
            else transport_req = song.playing ? 2 : 1;
        } else if (b == B_SEQ || b == B_EDIT) {
            drum_page = (uint8_t)((drum_page + 1u) % 2u);
            ui.msg_t = 0;
            ui.force = 1;
        }
    }
    if ((s = panel_enc(EN_SELECT)) && (uint32_t)(s > 0) != drum_page) {   /* SELECT: grid <- -> kit (its pages) */
        drum_page = (uint8_t)(s > 0);
        ui.msg_t = 0;
        ui.force = 1;
    }
    panel_enc(EN_ALGO);                                 /* (drums only: no other track to select) */
    if ((s = panel_enc(EN_PRESET))) TDRUM->p[P_E0] = drum_kit_step(TDRUM->p[P_E0], s);
    for (k = 0; k < 4u; k++) if ((s = panel_enc(EN_K1 + k))) {
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (!drum_page) {
            dstep_t *st = &TDRUM->dstep[drum_cursor];
            if (k == 0) {                              /* the sound: heard */
                uint8_t l = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
                if (l != drum_lane) audition_lane(l);
                drum_lane = l;
            }
            if (k == 1) {                              /* the step: what it holds, heard */
                uint8_t c = (uint8_t)clamp(drum_cursor + s, 0, TDRUM->p[P_SLEN] - 1);
                if (c != drum_cursor) audition_step(&TDRUM->dstep[c]);
                drum_cursor = c;
            }
            if (k >= 2) {
                if (song.playing && arrangement_enabled) { ui_message("STOP THE SONG FIRST"); continue; }
                undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
                fm1_irq_off();
                if (k == 2) {
                    if (s > 0) dstep_set(st, drum_lane, LV_NORM, 0);
                    else drum_hit_clr(TDRUM, drum_cursor, drum_lane);
                } else if (dstep_has(st, drum_lane)) {
                    uint32_t r = (uint32_t)clamp((int32_t)lvl_rank(dstep_lvl(st, drum_lane)) + (s > 0 ? 1 : -1), 0, 3);
                    dstep_set(st, drum_lane, LV_UP[r], dstep_rat(st, drum_lane));
                }
                fm1_irq_on();
                sync_reload = 1;
            }
        } else {
            if (k == 0) TDRUM->p[P_E0] = drum_kit_step(TDRUM->p[P_E0], s);
            if (k == 1) song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, 0, 127);
            if (k == 2) song.g[G_DRREV] = (int16_t)clamp(song.g[G_DRREV] + s, 0, 127);
            if (k == 3) TDRUM->p[P_PAN] = (int16_t)clamp(TDRUM->p[P_PAN] + s, -64, 63);
        }
    }
}
