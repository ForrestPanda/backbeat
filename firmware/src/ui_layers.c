/* SPDX-License-Identifier: GPL-3.0-only */
/* LAYERS, teenage-engineering style: hold a function button, touch a key (or turn a knob). While the
 * button is held the screen shows what the 16 white keys and KNOB 1..4 do now; tapped (pressed and
 * let go without touching anything) the button opens its pages as before.
 *   FX    + OCT- / OCT+: the throws        knobs: FILTER  DUST  DUCK
 *   EDIT  erase that sound (seq.c)         knobs: SHIFT  LENGTH x2 / half   OCT- undo  OCT+ redo
 *   ARP   note repeat (seq.c roll)         knobs: RATE
 *   SEQ   steps 1..16 of the page          knobs: SOUND  DIV  SWING  LENGTH;  a step key held:
 *         (black keys 1..4: pages)                SOUND  LEVEL  RATCHET  NUDGE, PRESETS the value of a
 *                                                 parameter lock, ALGORITHM its parameter (the sound parameter
 *                                                 last touched first); OCT+ cycles the step's fill condition
 *                                                 (normal, fill only, no fill), OCT- clears its nudge, locks and
 *                                                 condition
 *   SAVE  keys 1..4 play section A..D (on the next bar; two or more tapped while SAVE stays held: a chain of
 *         them, looped, each for its pattern's bars), 5..8 store the loop into A..D, 13 loop / song,
 *         14 SONG REC (the order you play becomes the song), 16 SONGS (fm1-drummer: not on the drum grid,
 *         where SAVE held writes the section into its song, ui_input.c holds_input)
 * (SEL held opened the key of the song, its scale and chords: gone with the synth parts, drums only. GLO held
 * was the mix: mute, solo, fill, tap tempo; fm1-drummer: GLO tapped opens SETUP, ui_menu.c.)
 * The keys' part runs in the audio ISR (seq.c layer_now: no lag, no lost press); the SEQ and SAVE
 * keys come to the UI through seq.c lk_q. HOLD: REC held clears the track, SAVE held on the drum grid saves
 * the section into its song (a ring fills; let go before and nothing happens). */
static const uint8_t LAYER_BTN[LY_COUNT] = {NB, B_FX, B_EDIT, B_ARP, B_SEQ, B_SCL, B_GLO, B_SAVE};
static const char *const LAYER_NAME[LY_COUNT] = {"", "fx", "erase", "roll", "steps", "key", "mix", "song"};
static void section_store(uint32_t s);                  /* project.c */
static void section_load(uint32_t s);
static uint8_t sec_armed;                               /* store over a used section: the key again within 3 s */
static uint32_t sec_armed_ms;
static uint8_t chain_tap[CHAIN_MAX], chain_taps;        /* the section keys tapped in this SAVE hold (the first
                                                         * is asked for at once; two or more: a chain on release) */
#define TAP_MS 450u                                     /* a press shorter than this, untouched: a tap */
#define SHOW_MS 140u                                    /* the layer shows after this (a tap does not flash it) */

/* the ISR's view of the panel: the layers' buttons, OCT- / OCT+, REC / PLAY of a free take */
static void layers_init(void)
{
    uint32_t l;
    for (l = LY_FX; l < LY_COUNT; l++)
        ly_bit[l] = l == LY_SCALE || l == LY_MIX ? 0u : 1u << panel.btn[LAYER_BTN[l]];   /* (SEL, GLO: no layer now;
                                                         * GLO tapped opens SETUP, ui_menu.c) */
    dyn_bit[0] = 1u << panel.btn[B_OCTDN];
    dyn_bit[1] = 1u << panel.btn[B_OCTUP];
    ui.lock_par = P_ED_FLT;                             /* the lock parameter until a sound knob is turned */
}

static uint32_t key_of_white(uint32_t w) { return key_of_lane(w & 15u); }   /* white key w -> key index */

/* ------------------------------------------------------------ tools --- */
/* EDIT layer knobs; each a step of one undo (the layer's hold) */
static void layer_undo_mark(track_t *t)
{
    if (!ui.step_sess)
        ui.step_sess = (undo_sess += 4u) | 3u;
    undo_mark(t, ui.step_sess);
}
static void pattern_rotate(track_t *t, int32_t d)       /* every step one later (d > 0) / earlier */
{
    uint32_t len = trk_len(t), i;
    dstep_t keep;
    layer_undo_mark(t);
    fm1_irq_off();
    if (d > 0) {
        keep = t->dstep[len - 1u];
        for (i = len - 1u; i > 0; i--)
            t->dstep[i] = t->dstep[i - 1u];
        t->dstep[0] = keep;
    } else {
        keep = t->dstep[0];
        for (i = 0; i + 1u < len; i++)
            t->dstep[i] = t->dstep[i + 1u];
        t->dstep[len - 1u] = keep;
    }
    drum_marks_rotate(len, d);                          /* (the song's marks go round with the hits) */
    fm1_irq_on();
}
static void pattern_length(track_t *t, int32_t d)       /* x2 (the pattern again after itself), or half */
{
    uint32_t len = trk_len(t), i;
    layer_undo_mark(t);
    fm1_irq_off();
    if (d > 0 && len * 2u <= NSTEP) {
        for (i = 0; i < len; i++) {
            t->dstep[len + i] = t->dstep[i];
            drum_marks_move(len + i, i);
        }
        t->p[P_SLEN] = (int16_t)(len * 2u);
    } else if (d < 0 && len >= 2u) {
        t->p[P_SLEN] = (int16_t)(len / 2u);
    }
    fm1_irq_on();
    {
        char b[8];
        fmt_int(b, t->p[P_SLEN]);
        ui_say("STEPS ", b);
    }
}
/* ------------------------------------------------------------- SEQ --- */
/* a step key (white key w) of the SEQ layer, Elektron style: an empty step is set at once (the sound's lane);
 * a set one goes when its key is let go, unless a knob changed it meanwhile (level, ratchet) */
static uint16_t step_pend_off;                         /* the step keys that clear their step when let go */
static int step_is_on(const track_t *t, uint32_t idx) { return dstep_has(&t->dstep[idx], pen_lane); }
static void step_down(uint32_t w)
{
    track_t *t = TDRUM;
    uint32_t idx = ui.step_page * 16u + w;
    if (idx >= trk_len(t))
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (step_is_on(t, idx)) {
        step_pend_off |= (uint16_t)(1u << w);
        return;
    }
    layer_undo_mark(t);
    fm1_irq_off();
    dstep_set(&t->dstep[idx], pen_lane, LV_NORM, 0);
    t->seq_active = 1;
    fm1_irq_on();
    sync_reload = 1;
}
static void step_up(uint32_t w)
{
    track_t *t = TDRUM;
    uint32_t idx = ui.step_page * 16u + w;
    if (!((step_pend_off >> w) & 1u))
        return;
    step_pend_off &= (uint16_t)~(1u << w);
    if (idx >= trk_len(t) || !step_is_on(t, idx))
        return;
    layer_undo_mark(t);
    fm1_irq_off();
    drum_hit_clr(t, idx, pen_lane);
    if (!dstep_mask(&t->dstep[idx])) {                  /* an empty step keeps no lock, no nudge, no condition */
        lock_del(t, idx, P_COUNT);
        t->micro[idx] = 0;
        step_fill_set(t, idx, FC_NORM);
    }
    fm1_irq_on();
    sync_reload = 1;
}
/* the drum sound the SEQ layer sets (KNOB 1): heard when it changes */
static void pen_lane_move(int32_t s)
{
    uint8_t l = (uint8_t)clamp(pen_lane + s, 0, DRUM_LANES - 1);
    if (l != pen_lane)
        audition_lane(l);
    pen_lane = l;
}
/* KNOB 1..4 with step keys held: their sound (the lane), level, ratchet, nudge (1/64 of a step, - early + late).
 * knob 4 = PRESETS: a lock of ui.lock_par on them, made at the track's value, then moved */
static void steps_held_edit(uint32_t knob, int32_t s)
{
    track_t *t = TDRUM;
    uint32_t w;
    /* the accelerated turn once for every step held (a second call would see no time pass: x3 on the others) */
    int32_t sk = knob == 4u ? accel(EN_PRESET, s, lock_desc(t, ui.lock_par % P_COUNT)->max -
                                                  lock_desc(t, ui.lock_par % P_COUNT)->min) : 0;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;            /* (edited: kept when let go) */
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        if (knob == 3u) {                                 /* NUDGE: the whole step (drums: every lane) */
            t->micro[idx] = (int8_t)clamp(t->micro[idx] + s, MICRO_MIN, MICRO_MAX);
            continue;
        }
        if (knob == 4u) {                                 /* LOCK (PRESETS): ui.lock_par on this step */
            uint32_t id = ui.lock_par % P_COUNT;
            int k = lock_find(t, idx, id, 0);
            int32_t v = k >= 0 ? t->lock[k].val : t->p[id];
            if (!lock_set(t, idx, id, v + sk))
                ui_message(p_lockable(id) ? "NO LOCK LEFT" : "NOT LOCKABLE");
            continue;
        }
        {
            dstep_t *d = &t->dstep[idx];
            uint32_t lv = dstep_lvl(d, pen_lane), rt = dstep_rat(d, pen_lane);
            if (!dstep_has(d, pen_lane))
                continue;
            if (knob == 1u)
                lv = LV_UP[clamp((int32_t)lvl_rank(lv) + s, 0, 3)];
            else
                rt = (uint32_t)clamp((int32_t)rt + s, 0, 3);
            dstep_set(d, pen_lane, lv, rt);
        }
    }
    fm1_irq_on();
    sync_reload = 1;
}
/* ALGORITHM with step keys held: the lock parameter, through the lockable ones (the drum track: not the
 * engine parameters it has no use for), wrapping round */
static void lock_par_step(int32_t s)
{
    const track_t *t = TDRUM;
    uint32_t id = ui.lock_par % P_COUNT, guard = P_COUNT;
    do {
        id = (id + (uint32_t)P_COUNT + (uint32_t)(s > 0 ? 1 : -1)) % P_COUNT;
    } while (guard-- && (!p_lockable(id) || lock_desc(t, id)->max <= lock_desc(t, id)->min ||
                         (id > P_E0 && id <= P_E7)));
    ui.lock_par = (uint8_t)id;
}
/* OCT- with step keys held: their nudge, locks and fill condition go */
static void steps_held_clear(void)
{
    track_t *t = TDRUM;
    uint32_t w, n = 0;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        n += step_locked(t, idx) || step_fill(t, idx);
        lock_del(t, idx, P_COUNT);
        t->micro[idx] = 0;
        step_fill_set(t, idx, FC_NORM);
    }
    fm1_irq_on();
    sync_reload = 1;
    ui_message(n ? "NUDGE, LOCKS, FILL CLEARED" : "NO NUDGE, LOCK OR FILL");
}
/* OCT+ with step keys held: their fill condition, round: normal -> fill only -> no fill (the first one's next) */
static void steps_held_fill(void)
{
    track_t *t = TDRUM;
    static const char *const MSG[3] = {"FILL: NORMAL", "FILL ONLY", "NO FILL"};
    uint32_t w, v = 0, first = 1;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        if (first)
            v = (step_fill(t, idx) + 1u) % 3u;
        first = 0;
        step_fill_set(t, idx, v);
    }
    fm1_irq_on();
    sync_reload = 1;
    ui_message(MSG[v % 3u]);
}

/* a key of the layers the UI runs (seq.c lk_q): SEQ steps, SAVE sections */
static void layer_key(uint32_t layer, uint32_t k, uint32_t down)
{
    int32_t w = key_white(k);
    if (layer >= LY_COUNT)
        return;                                         /* (KB_GRID: the DRUMS grid page's, ui_studio.c) */
    if (!down) {
        if (w >= 0) {
            ui.step_held &= (uint16_t)~(1u << w);
            if (layer == LY_STEP)
                step_up((uint32_t)w);
        }
        return;
    }
    switch (layer) {
    case LY_STEP:
        if (w < 0) {                                    /* the first four black keys: pages 1..4 */
            static const int8_t PG[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
            if (k < 12u && PG[k] >= 0 && (uint32_t)PG[k] * 16u < trk_len(TDRUM))
                ui.step_page = (uint8_t)PG[k];
            return;
        }
        ui.step_held |= (uint16_t)(1u << w);
        step_down((uint32_t)w);
        return;
    case LY_SONG: {                                     /* sections A..D: play, store; loop / song; SONG REC */
        char b[2] = {0, 0};
        if (w < 0)
            return;
        b[0] = (char)('A' + (w & 3));
        if (w < 4) {
            if (arrangement_clock.running) {
                ui_message("SONG PLAYS");
            } else if (!((arrangement_ready() >> w) & 1u)) {
                ui_say("EMPTY ", b);
            } else if (song.playing) {
                if (!chain_taps) {                          /* the first tap: as ever, and any chain stops */
                    fm1_irq_off();
                    chain_n = 0;
                    live_req = (int8_t)w;
                    fm1_irq_on();
                    ui_say("NEXT: ", b);
                } else {
                    ui.msg_t = 0;                           /* (the sub line shows the chain) */
                }
                if (chain_taps < CHAIN_MAX && (fm1_in.buttons & ly_bit[LY_SONG]))
                    chain_tap[chain_taps++] = (uint8_t)w;   /* (SAVE held: more taps make a chain) */
            } else {
                section_load((uint32_t)w);
                ui_say("LOADED ", b);
            }
        } else if (w < 8) {
            uint32_t s = (uint32_t)w - 4u;
            if (((arrangement_ready() >> s) & 1u) && !(sec_armed == s + 1u && fm1_ms - sec_armed_ms < 3000u)) {
                sec_armed = (uint8_t)(s + 1u);
                sec_armed_ms = fm1_ms;
                ui_say("AGAIN: ", b);
            } else {
                sec_armed = 0;
                section_store(s);
                ui_say("SAVED ", b);
            }
        } else if (w == 12) {
            if (srec) {
                ui_message("REC IS ON");
            } else {
                arrangement_enabled ^= 1u;
                ui_message(arrangement_enabled ? "SONG MODE" : "LOOP MODE");
            }
        } else if (w == 13) {
            if (srec) {
                fm1_irq_off();
                srec_stop();                            /* (playing: the order so far is the song) */
                fm1_irq_on();
            } else if (arrangement_clock.running) {
                ui_message("STOP FIRST");
            } else {
                arrangement_enabled = 0;
                srec = 1;
                ui_message(live_sec < 0 ? "PICK A PART" : "REC: NEXT BAR");
            }
        } else if (w == 15) {
            studio_open(SC_SONG);
        }
        return;
    }
    default:
        return;
    }
}

/* SAVE let go (ui_input.c): two or more section taps while it was held play as a chain, from the one
 * already asked for (the first), each for the bars of its pattern, looped; one tap was a plain jump */
static void chain_release(void)
{
    uint32_t i;
    if (chain_taps >= 2u && song.playing && !arrangement_clock.running) {
        fm1_irq_off();
        for (i = 0; i < chain_taps && i < CHAIN_MAX; i++)
            chain_sec[i] = chain_tap[i];
        chain_i = 0;
        chain_bars = (uint8_t)section_bars(chain_tap[0]);   /* (the first plays already, or is asked for) */
        chain_n = (uint8_t)(chain_taps < CHAIN_MAX ? chain_taps : CHAIN_MAX);
        fm1_irq_on();
    }
    chain_taps = 0;
}
/* "chain A B B C": the chain being built (SAVE held) or playing */
static void chain_sub(char *sub, uint32_t n)
{
    uint32_t i, m = chain_taps >= 2u ? chain_taps : chain_n, k = 5;
    str_cpy(sub, "chain", n);
    for (i = 0; i < m && i < CHAIN_MAX && k + 2u < n; i++) {
        sub[k++] = ' ';
        sub[k++] = (char)('A' + ((chain_taps >= 2u ? chain_tap[i] : chain_sec[i]) & 3u));
    }
    sub[k] = 0;
}

/* KNOB 1..4 while a layer is held: what the layer gives them (the page does not see them) */
static void layer_knobs(uint32_t layer)
{
    uint32_t k;
    int32_t s;
    track_t *t = TDRUM;
    if (layer == LY_STEP && ui.step_held) {              /* a step held: PRESETS the lock's value, ALGORITHM its parameter
                                                          * (neither browses a sound nor selects a track meanwhile) */
        if ((s = panel_enc(EN_PRESET)) != 0) {
            ui.layer_used = 1;
            steps_held_edit(4, s);
        }
        if ((s = panel_enc(EN_ALGO)) != 0) {
            ui.layer_used = 1;
            lock_par_step(s);
        }
    }
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.layer_used = 1;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        switch (layer) {
        case LY_FX:
            if (k == 0u)
                song.g[G_FILT] = (int16_t)clamp(song.g[G_FILT] + accel(EN_K1, s, 127), -64, 63);
            else if (k == 1u)
                song.g[G_DUST] = (int16_t)clamp(song.g[G_DUST] + accel(EN_K2, s, 127), 0, 127);
            else if (k == 2u)
                song.g[G_DUCK] = (int16_t)clamp(song.g[G_DUCK] + accel(EN_K3, s, 127), 0, 127);
            else                                        /* the selected track's FILTER (2.4) */
                t->p[P_TFLT] = (int16_t)clamp(t->p[P_TFLT] + accel(EN_K4, s, 127), -64, 63);
            break;
        case LY_ERASE:
            if (k == 0u)
                pattern_rotate(t, s);
            else if (k == 1u)
                pattern_length(t, s);
            break;
        case LY_ROLL:
            if (k == 0u)
                song.g[G_ROLL] = (int16_t)clamp(song.g[G_ROLL] + s, 0, 4);
            break;
        case LY_STEP:
            if (ui.step_held) {                         /* SOUND  LEVEL  RATCHET  NUDGE */
                if (k == 0u)
                    pen_lane_move(s);
                else
                    steps_held_edit(k, s);
            } else if (k == 0u) {
                pen_lane_move(s);
            } else if (k == 1u) {
                t->p[P_SDIV] = (int16_t)clamp(t->p[P_SDIV] + s, 0, (int32_t)NDIV_STEP - 1);   /* (up to 2BAR) */
            } else if (k == 2u) {
                t->p[P_SSWING] = (int16_t)clamp(t->p[P_SSWING] + accel(EN_K3, s, 100), 0, 100);
            } else {
                t->p[P_SLEN] = (int16_t)clamp(t->p[P_SLEN] + accel(EN_K4, s, 63), 1, NSTEP);
                if ((uint32_t)ui.step_page * 16u >= trk_len(t))
                    ui.step_page = (uint8_t)((trk_len(t) - 1u) / 16u);
            }
            break;
        default:
            break;
        }
    }
}

/* --------------------------------------------------------- drawing --- */
/* 16 tiles (the white keys, 4 x 4) under a title band, the knobs' dials at the bottom */
typedef struct {
    char lab[8];
    uint16_t bg, fg, top;        /* fill, text, the 3-pixel top band (0 = none) */
    uint8_t marks;               /* small marks under the label (a ratchet), 0 = none */
    uint8_t tag;                 /* a dot in the top right corner (a nudge or a lock on the step), 0 = none */
    uint8_t cond;                /* top left: the step's fill condition (FC_FILL an "F", FC_NOFILL an "x"), 0 = none */
} tile_t;

static void tiles_draw(const tile_t *tl, uint32_t *cache)
{
    uint32_t r, c, sig = 7u;
    for (r = 0; r < 16u; r++)
        sig = studio_hash(sig * 31u + tl[r].bg * 3u + tl[r].fg * 5u + tl[r].top * 7u + tl[r].marks + tl[r].tag * 11u + tl[r].cond * 13u,
                          tl[r].lab);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    for (r = 0; r < 4u; r++) {
        cv_begin(240, 36, C_BLACK);
        for (c = 0; c < 4u; c++) {
            const tile_t *t = &tl[r * 4u + c];
            int32_t x = 2 + (int32_t)c * 60;
            uint32_t m;
            cv_rect(x, 2, 56, 32, t->bg);
            if (t->top)
                cv_rect(x, 2, 56, 3, t->top);
            te_text_c(x + 28, 9, t->lab, t->fg);
            for (m = 0; m < t->marks; m++)
                cv_rect(x + 22 + (int32_t)m * 5, 27, 3, 3, t->fg);
            if (t->tag)
                cv_rect(x + 50, 7, 3, 3, t->fg);
            if (t->cond == FC_FILL) {                   /* an F, 5 x 7: plays in a fill only */
                cv_rect(x + 4, 7, 2, 7, t->fg);
                cv_rect(x + 4, 7, 5, 2, t->fg);
                cv_rect(x + 4, 10, 4, 2, t->fg);
            } else if (t->cond == FC_NOFILL) {          /* an x, 7 x 7: silent in a fill */
                int32_t d;
                for (d = 0; d < 6; d++) {
                    cv_rect(x + 4 + d, 7 + d, 2, 2, t->fg);
                    cv_rect(x + 9 - d, 7 + d, 2, 2, t->fg);
                }
            }
        }
        cv_blit(0, 40 + r * 36);
    }
}

static void layer_title(const char *name, const char *sub, uint16_t col, uint32_t *cache)
{
    uint32_t locked = ly_lock != LY_PLAY;
    uint32_t sig = studio_hash(studio_hash(col, name), sub) + (ui.msg_t ? studio_hash(3u, ui.msg) : 0u) + locked * 7919u;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 40, C_BLACK);
    cv_text(4, 2, &FONT_L, name, col);
    cv_text(4 + text_w(&FONT_L, name) + 10, 18, &FONT_S, ui.msg_t ? ui.msg : sub, ui.msg_t ? C_WHITE : TE_G3);
    if (locked) {                                         /* locked open (HOME): any button lets it go */
        int32_t w = (int32_t)text_w(&FONT_S, "LOCK") + 8;
        cv_rect(236 - w, 4, w, 15, C_WHITE);
        cv_text(240 - w, 4, &FONT_S, "LOCK", C_BLACK);
    }
    cv_rect(0, 38, 240, 1, TE_G1);
    cv_blit(0, 0);
}

static const char *lock_label(const track_t *t, uint32_t id)   /* the lock parameter's name, lower case */
{
    static char b[8];
    te_lower(b, track_desc(t, id % P_COUNT)->label, 8);
    return b;
}

static uint8_t layer_shown;                              /* the screen holds a layer (or a hold) */
static const char *layer_sub_shown = "";                 /* the sub line last drawn (the host tests read it) */
static void layer_screen_draw(void)
{
    static uint32_t head, tiles, foot;
    static tile_t tl[16];
    static char v[4][10], sub[24];
    const char *lab[4] = {"", "", "", ""}, *val[4] = {v[0], v[1], v[2], v[3]};
    int32_t ratio[4] = {-1, -1, -1, -1};
    uint32_t i, layer = ui.layer;
    track_t *t = TDRUM;
    uint16_t col = TE_COL[0];
    if (!layer_shown) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
        layer_shown = 1;
    }
    for (i = 0; i < 4u; i++)
        v[i][0] = 0;
    memset(tl, 0, sizeof tl);
    for (i = 0; i < 16u; i++) {
        tl[i].bg = TE_G1;
        tl[i].fg = TE_G3;
    }
    str_cpy(sub, "drums", sizeof sub);
    switch (layer) {
    case LY_FX:                                         /* the master filter, DUST, DUCK, the track filter; the throws */
        col = TE_DRUM;
        str_cpy(sub, thr_req == 3u ? "rev + echo throw" : thr_req == 2u ? "echo throw" : thr_req ? "rev throw" : "oct-/+: throws",
                sizeof sub);
        lab[0] = "filter", lab[1] = "dust", lab[2] = "duck", lab[3] = "trk flt";
        {
            const char *u;
            param_format(&GP[G_FILT], song.g[G_FILT], v[0], &u);
            te_lower(v[0], v[0], 8);
            fmt_int(v[1], song.g[G_DUST] * 100 / 127);
            fmt_int(v[2], song.g[G_DUCK] * 100 / 127);
            param_format(&TP[P_TFLT], t->p[P_TFLT], v[3], &u);
            te_lower(v[3], v[3], 8);
        }
        ratio[0] = (song.g[G_FILT] + 64) * 1000 / 127;
        ratio[1] = song.g[G_DUST] * 1000 / 127;
        ratio[2] = song.g[G_DUCK] * 1000 / 127;
        ratio[3] = (t->p[P_TFLT] + 64) * 1000 / 127;
        break;
    case LY_ERASE:
    case LY_ROLL: {                                     /* the keys' sounds: lit = held */
        uint32_t held = fm1_in.notes;
        int mute = layer == LY_ERASE && er_mute;      /* (2.6) EDIT + a black key: the mutes */
        col = layer == LY_ERASE ? TE_RED : col;
        str_cpy(sub, mute ? "mute: key = sound" : layer == LY_ERASE ? (song.playing ? "as it plays" : "every step") : "hold + key", sizeof sub);
        for (i = 0; i < 16u; i++) {
            uint32_t k = key_of_white(i), down = (held >> k) & 1u, present = 0, j;
            str_cpy(tl[i].lab, LANE_SHORT[i], 8);
            for (j = 0; j < trk_len(t); j++)
                present |= dstep_has(&t->dstep[j], i);
            tl[i].bg = down ? (layer == LY_ERASE ? TE_RED : C_WHITE) : TE_G1;
            tl[i].fg = down ? C_BLACK : present ? C_WHITE : TE_G3;
            tl[i].top = present && !down ? (layer == LY_ERASE ? TE_RED : col) : 0;
            if (mute) {                                 /* MUTE: a muted sound white on red, "off" */
                uint32_t off = (drum_mute >> i) & 1u;
                tl[i].bg = off ? TE_RED : TE_G1;
                tl[i].fg = off ? C_WHITE : present ? C_WHITE : TE_G3;
                tl[i].top = 0;
                if (off)
                    str_cpy(tl[i].lab, "off", 8);
            }
        }
        if (layer == LY_ERASE && !mute && !undo.valid)
            str_cpy(sub, "black key: mute", sizeof sub);
        if (layer == LY_ERASE) {
            lab[0] = "shift", lab[1] = "length";
            str_cpy(v[0], "<  >", 8);
            fmt_int(v[1], t->p[P_SLEN]);
            if (!mute)
                str_cpy(sub, undo.valid ? (undo.undone ? "oct+ redo" : "oct- undo") : sub, sizeof sub);
        } else {
            lab[0] = "rate";
            str_cpy(v[0], N_ROLL[clamp(song.g[G_ROLL], 0, 4)], 8);
            ratio[0] = song.g[G_ROLL] * 250;
        }
        break;
    }
    case LY_STEP: {                                     /* the 16 steps of the page */
        uint32_t len = trk_len(t), page = ui.step_page;
        for (i = 0; i < 16u; i++) {
            uint32_t idx = page * 16u + i, on, lv = LV_NORM, rt = 0;
            if (idx >= len) {
                tl[i].bg = C_BLACK;
                continue;
            }
            {
                const dstep_t *d = &t->dstep[idx];
                on = dstep_has(d, pen_lane);
                if (on) {
                    lv = dstep_lvl(d, pen_lane);
                    rt = dstep_rat(d, pen_lane);
                }
            }
            if (!tl[i].lab[0])
                fmt_int(tl[i].lab, (int32_t)idx + 1);
            tl[i].bg = on ? (lv == LV_GHOST ? TE_DIM[0] : lv == LV_SOFT ? TE_MID[0] : lv == LV_HARD ? C_WHITE : col)
                          : TE_G1;
            tl[i].fg = on ? C_BLACK : TE_G3;
            tl[i].marks = (uint8_t)(on ? rt : 0u);
            tl[i].tag = (uint8_t)step_locked(t, idx);   /* a nudge or a lock on it */
            tl[i].cond = (uint8_t)(step_fill(t, idx) % 3u);   /* its fill condition */
            if (song.playing && idx == t->seq_idx)
                tl[i].top = C_WHITE;
            if ((ui.step_held >> i) & 1u)
                tl[i].top = TE_RED;
        }
        if (len > 16u) {
            char p[4] = {'1', '/', '1', 0};
            p[0] = (char)('1' + page);
            p[2] = (char)('0' + (len + 15u) / 16u);
            str_cpy(sub, "page ", sizeof sub);
            str_cpy(sub + 5, p, 4);
        }
        lab[0] = "sound";
        str_cpy(v[0], LANE_SHORT[pen_lane], 8);
        ratio[0] = pen_lane * 1000 / 15;
        if (ui.step_held) {                             /* the first step held: its nudge; the title: its lock */
            uint32_t id = ui.lock_par % P_COUNT, w, idx;
            int k;
            for (w = 0; w < 16u && !((ui.step_held >> w) & 1u); w++)
                ;
            idx = (page * 16u + w) % NSTEP;
            lab[1] = "level", lab[2] = "ratchet", lab[3] = "nudge";
            str_cpy(v[1], "-  +", 8);
            str_cpy(v[2], "x1 x4", 8);
            {
                int32_t m = t->micro[idx];
                if (m > 0) {
                    v[3][0] = '+';
                    fmt_int(v[3] + 1, m);
                } else {
                    fmt_int(v[3], m);
                }
                ratio[3] = (m - MICRO_MIN) * 1000 / (MICRO_MAX - MICRO_MIN);
            }
            str_cpy(sub, "lock ", sizeof sub);          /* "lock flt +12" / "lock flt --" (PRESETS, ALGORITHM) */
            str_cpy(sub + 5, lock_label(t, id), 8);
            str_cpy(sub + str_len(sub), " ", 2);
            k = lock_find(t, idx, id, 0);
            if (k >= 0) {
                const char *u;
                char b[8];
                param_format(lock_desc(t, id), t->lock[k].val, b, &u);
                te_lower(sub + str_len(sub), b, 8);
            } else {
                str_cpy(sub + str_len(sub), "--", 3);
            }
        } else {
            lab[1] = "div", lab[2] = "swing", lab[3] = "steps";
            str_cpy(v[1], N_SDIV[t->p[P_SDIV] % NDIV_STEP], 8);
            swing_str(v[2], t->p[P_SSWING]);
            fmt_int(v[3], t->p[P_SLEN]);
            ratio[1] = t->p[P_SDIV] * 1000 / (NDIV_STEP - 1);
            ratio[2] = t->p[P_SSWING] * 10;
            ratio[3] = (t->p[P_SLEN] - 1) * 1000 / 63;
        }
        break;
    }
    case LY_SONG: {                                     /* A..D (playing lit, next one framed), store, modes */
        uint32_t ready = arrangement_ready();
        static const char *const SL[4] = {"A", "B", "C", "D"};
        col = C_WHITE;
        if (srec == 2u) {
            char b[8];
            str_cpy(sub, "rec ", sizeof sub);
            str_cpy(sub + 4, SL[srec_e[srec_n ? srec_n - 1u : 0u].scene & 3u], 2);
            str_cpy(sub + str_len(sub), " bar ", 6);
            fmt_int(b, srec_n ? srec_e[srec_n - 1u].bars + 1 : 1);
            str_cpy(sub + str_len(sub), b, 6);
        } else if (chain_taps >= 2u || chain_n) {
            chain_sub(sub, sizeof sub);                 /* "chain A B B C" */
        } else {
            str_cpy(sub, arrangement_enabled ? "song mode" : srec ? "rec armed" : "play  store", sizeof sub);
        }
        for (i = 0; i < 4u; i++) {                      /* (the chain's next entry framed as the one asked for) */
            int used = (ready >> i) & 1u, playing = live_sec == (int8_t)i && !arrangement_clock.running;
            int next = live_req == (int8_t)i ||
                       (chain_n && live_req < 0 && (chain_sec[((chain_i + 1u) % chain_n) % CHAIN_MAX] & 3u) == i);
            str_cpy(tl[i].lab, SL[i], 8);
            tl[i].bg = used ? (playing ? TE_COL[i] : TE_DIM[i]) : TE_G1;
            tl[i].fg = used ? C_BLACK : TE_G3;
            tl[i].top = next ? C_WHITE : 0;
            str_cpy(tl[4 + i].lab, "save A", 8);
            tl[4 + i].lab[5] = (char)('A' + i);
            tl[4 + i].bg = sec_armed == i + 1u ? TE_RED : TE_G1;
            tl[4 + i].fg = sec_armed == i + 1u ? C_BLACK : TE_G4;
            tl[4 + i].top = TE_DIM[i];
        }
        str_cpy(tl[12].lab, arrangement_enabled ? "song" : "loop", 8);
        tl[12].bg = arrangement_enabled ? C_WHITE : TE_G2;
        tl[12].fg = arrangement_enabled ? C_BLACK : C_WHITE;
        str_cpy(tl[13].lab, "rec", 8);
        tl[13].bg = srec == 2u ? TE_RED : TE_G1;
        tl[13].fg = srec == 2u ? C_BLACK : TE_RED;
        tl[13].top = srec == 1u ? TE_RED : 0;
        str_cpy(tl[15].lab, "songs", 8);                /* (key 16 opens SONGS; it said "chain": a chain is taps) */
        tl[15].bg = TE_G1;
        tl[15].fg = TE_G4;
        break;
    }
    default:
        break;
    }
    layer_sub_shown = sub;
    layer_title(LAYER_NAME[layer % LY_COUNT], sub, col, &head);
    tiles_draw(tl, &tiles);
    {   /* (a message shows in the title: the dials stay) */
        uint8_t m = ui.msg_t;
        ui.msg_t = 0;
        te_dials(184, lab, val, ratio, layer * 7919u, &foot);
        ui.msg_t = m;
    }
}

/* HOLD to confirm: a ring fills while REC (clear the track) or SAVE (save the project) is held */
#define HOLD_CLEAR_MS 1300u                             /* after the 0.7 s that make REC a hold */
#define HOLD_SAVE_MS 1000u
static void hold_screen_draw(void)
{
    static uint32_t cache;
    uint32_t span = ui.hold_kind == 1u ? HOLD_CLEAR_MS : HOLD_SAVE_MS, el = fm1_ms - ui.hold_t0;
    int32_t ratio = el >= span ? 1000 : (int32_t)(el * 1000u / span);
    uint32_t sig = ui.hold_kind * 7u + (uint32_t)ratio / 20u;
    char b[24];
    if (!layer_shown) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        layer_shown = 1;
        cache = ~sig;
    }
    if (sig == cache && !ui.force)
        return;
    cache = sig;
    cv_begin(240, 124, C_BLACK);
    cv_text(120 - text_w(&FONT_L, ui.hold_kind == 1u ? "clear" : "save") / 2, 4, &FONT_L,
            ui.hold_kind == 1u ? "clear" : "save", ui.hold_kind == 1u ? TE_RED : C_WHITE);
    {   /* the ring: 36 px, 7 thick, filling clockwise from the top */
        int32_t a, rr, end = ratio * 1024 / 1000;
        for (a = 0; a < 1024; a += 2) {
            int32_t co = SINE[((uint32_t)a + 768u) & 1023u], si = SINE[(uint32_t)a & 1023u];   /* from 12 o'clock */
            uint16_t c = a <= end ? (ui.hold_kind == 1u ? TE_RED : C_WHITE) : TE_G2;
            for (rr = 30; rr <= 36; rr++)
                cv_pset(120 + ((si * rr) >> 15), 80 + ((co * rr) >> 15), c);
        }
    }
    cv_blit(0, 20);
    cv_begin(240, 60, C_BLACK);
    if (ui.hold_kind == 1u) {
        str_cpy(b, "the pattern", sizeof b);
    } else if (sng_cur >= 0 && live_sec >= 0) {          /* fm1-drummer: the section into its song */
        char nm[13];
        fm1_irq_off();
        str_cpy(nm, sng_ui_name, sizeof nm);
        fm1_irq_on();
        te_lower(b, nm, 13);
        if (sng_cur >= (int8_t)SNG_FAC0) {
            str_cpy(b, "factory: read-only", 19);
        } else {
            str_cpy(b + str_len(b), " into song ", 12);
            fmt_int(b + str_len(b), sng_cur + 1);
        }
    } else {
        str_cpy(b, "project in slot 1", sizeof b);
        b[16] = (char)('0' + clamp(song.g[G_SLOT], 1, 4));
    }
    te_text_c(120, 4, b, TE_G4);
    te_text_c(120, 30, "keep holding", TE_G3);
    cv_blit(0, 160);
}
