/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca user interface. Four columns map to KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "SLOOP 2.6"  /* the beat machine firmware for the FM-1 (based on Felucca) */
#endif
static void project_save(uint32_t slot);
static void arrangement_save(void);
static void panel_setup(void);
static void project_load(uint32_t slot);
static void project_erase(uint32_t slot);        /* SAVE > PROJECT > ERASE (2.6) */
static int project_used(uint32_t slot);
static uint8_t sync_reload;                  /* a kit / project loaded: editor RELOAD push */

#define ACC C_HI                   /* amber everywhere; white is the only accent */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : TE_COL[(c) & 3u])   /* LIVE: one colour per knob */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout: four 60 px columns, 4 px inset */
/* Terminus 8x16 (S) and 16x32 (L) */
#define Y_HEAD 0
#define H_HEAD 20
#define Y_LABEL 26
#define Y_VALUE 44
#define Y_GAUGE 64
#define Y_SEP_END 70
#define Y_GRAPH 74
#define H_GRAPH 124
#define G_OY 24                       /* graphs draw in the lower 100 px; the focus readout sits on top */
#define Y_FOOT 202
#define H_FOOT 38

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held) */
    uint8_t menu_sel;
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint8_t confirm;             /* 1 = "clear the sequence?" (REC held on SEQ / ARP), 2 = "clear track n?" (TRACKS) */
    uint8_t confirm_trk;         /* the track the dialog clears */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    /* layers (ui_layers.c): a function button held, the keys and knobs do something else */
    uint8_t layer;               /* the layer drawn (LY_*), LY_PLAY = none */
    uint8_t layer_btn;           /* its button (B_*), NB = none */
    uint8_t layer_used;          /* a key / knob / OCT was used while it was held: no tap on release */
    uint32_t layer_t0;           /* its press time (ms) */
    uint8_t step_page;           /* SEQ layer: the 16 steps shown (page x 16) */
    uint16_t step_held;          /* SEQ layer: the step keys held (white key index) */
    uint32_t step_sess;          /* SEQ layer: the undo session of this hold */
    uint8_t lock_par;            /* SEQ layer, a step held + KNOB 4: the parameter it locks (the last sound
                                  * parameter a knob changed on a page of the selected track; P_ED_FLT at boot) */
    uint8_t hold_kind;           /* a hold to confirm: 1 = clear the track (REC), 2 = save (SAVE) */
    uint32_t hold_t0;            /* (ms) */
    uint32_t tap_ms[4];          /* tap tempo: the last taps */
    uint8_t tap_n;
    char msg[24];
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];   /* the touched column, shown large */
    char big_l[4][8], big_v[4][10], big_u[4][8];   /* the four columns, for the big values (2.4: pages without a graph) */
    uint16_t big_c[4];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    uint8_t graph_top;           /* the graph strip's top G_OY rows hold something */
} ui;

static const page_t *cur_page(void) { return &PAGES[ui.page]; }
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* ui_input.c */
static void layer_screen_draw(void);                            /* ui_layers.c */
static void hold_screen_draw(void);
static uint8_t layer_shown;

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

/* transient message in the top bar: a + b */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static void page_entered(void)
{
    const page_t *pg = cur_page();
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    if (!ui.home && pg->fam == FAM_GLO && pg->id[0] == G_DRCH && song.g[G_ROUTE]) {   /* (2.6) GLO > MIDI while IN sets
                                                                                    * the drums' channel aside */
        if (song.g[G_ROUTE] == 1)
            ui_message("IN = CLOCK: NO NOTES");
        else {
            char b[10];
            str_cpy(b, "IN CH ", sizeof b);
            fmt_int(b + 6, song.g[G_ROUTE] - 1);
            ui_say(b, " ONLY");
        }
    }
    ui.entry_open = 0;
    ui.hot_t = 0;                                /* the white value / focus box was the old page's */
    ui.force = 1;
}

/* step i of track t has something to play (a lane) */
static int trk_step_on(const track_t *t, uint32_t i) { return dstep_mask(&t->dstep[i % NSTEP]) != 0u; }

/* undo / redo (EDIT + OCT- / OCT+): the marked pattern and the one now swap places */
static int undo_swap(int redo)
{
    track_t *t;
    int16_t len;
    if (!undo.valid || (uint32_t)!!redo != undo.undone)
        return 0;
    t = &trk[undo.trk % NTRK];
    fm1_irq_off();
    {
        uint32_t i;
        for (i = 0; i < NSTEP; i++) {
            dstep_t x = t->dstep[i];
            t->dstep[i] = undo.st[i];
            undo.st[i] = x;
        }
    }
    len = t->p[P_SLEN];
    t->p[P_SLEN] = undo.len;
    undo.len = len;
    undo.undone = (uint8_t)!redo;
    fm1_irq_on();
    sync_reload = 1;
    ui.force = 1;
    return 1;
}

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
static void cursor_set(int32_t c)
{
    int32_t len = TDRUM->p[P_SLEN] > 0 ? TDRUM->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TDRUM->p[P_SLEN])
        cursor_set(TDRUM->p[P_SLEN] - 1);
}

static void open_family(uint32_t fam)
{
    if (PAGES[page_first(fam)].fam != fam)
        return;                                         /* (drums only: ENV, LFO, SEL, EDIT, ARP have no page) */
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t i = ui.page + 1u;
        if (i >= NPAGES || PAGES[i].fam != fam)
            i = page_first(fam);
        ui.page = (uint8_t)i;
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam ? ui.fam_last[fam] : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

/* SELECT on a page: the previous / next page of its family, as tapping the button again but both ways,
 * stopping at the ends. Not the screens of their own (SONG, DRUMS) nor a family of one page (TRACKS):
 * 0 then, and SELECT is the tempo there */
static int page_walk(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t i, fam = pg->fam, n = 0;
    int32_t cur = -1, to;
    uint8_t idx[8];
    if (ui.home || pg->scope == SC_SONG || pg->scope == SC_DRUM)
        return 0;
    for (i = 0; i < NPAGES && n < 8u; i++) {
        if (PAGES[i].fam != fam || PAGES[i].scope == SC_SONG || PAGES[i].scope == SC_DRUM)
            continue;
        if (i == ui.page)
            cur = (int32_t)n;
        idx[n++] = (uint8_t)i;
    }
    if (n < 2u || cur < 0)
        return 0;
    to = clamp(cur + s, 0, (int32_t)n - 1);
    if (to != cur) {
        ui.page = idx[to];
        ui.fam_last[fam] = ui.page;
        page_entered();
    }
    return 1;
}

static void go_home(void)
{
#if FELUCCA_DRUMMER
    ui.home = 0;
    ui.page = (uint8_t)page_first(FAM_TRK);
#else
    ui.home = 1;
#endif
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------- track setup --- */
/* LIVE: no factory sequence patterns. Loading a sound (factory or user preset) never
 * writes the sequencer: every pattern is the one the player records or enters. */

static void track_defaults_steps(track_t *t) { steps_clear(t); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* MIDI Program Change (2.6, after Felucca 1.5.1, PR #179 by @renebohne): seq.c queues it per track; here, in the main
 * loop: the drum track takes kit n. A number past the last kit does nothing (no wrap); drums only: the other tracks
 * have no sound to load */
static void pc_poll(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        uint32_t r = pc_req[i], n;
        if (!r)
            continue;
        pc_req[i] = 0;
        n = r - 1u;
        if (!is_drum(&trk[i]) || n >= DRUM_KITS)
            continue;
        trk[i].p[P_E0] = (int16_t)n;
        sync_reload = 1;
        ui_say("KIT ", DRUM_KIT_NAMES[n]);
        ui.force = 1;
    }
}
/* HOME: what KNOB k edits on the drum track: LEVEL and REV (GLO > DRUMS), PAN and LEN */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    static const uint8_t DRUM_HOME[4][2] = {{1, G_DRLVL}, {1, G_DRREV}, {0, P_PAN}, {0, P_SLEN}};
    uint32_t id = DRUM_HOME[k & 3u][1];
    if (DRUM_HOME[k & 3u][0]) {
        *vp = &song.g[id];
        return &GP[id];
    }
    *vp = &TDRUM->p[id];
    return &TP[id];
}

/* ------------------------------------------------------- MIDI LEARN --- */
/* MIDI LEARN (SLOOP 2.6, after Felucca 1.5 by Leo Kuroshita, Discussion #170; the map: seq.c ml_tab). GLO held +
 * the black key F#3 turns it on and off. On: a knob turned on a track's page (the sound's pages, HOME, TRACKS' LEVEL
 * and PAN) picks its parameter on the selected track as it edits it; the next CC that comes (any channel IN hears) is
 * set to it, and stays until something else is learned on it or HOME > SYSTEM > LEARN CLEAR. Then turn the next knob.
 * One CC sets one parameter, a parameter has one CC: learning one again replaces both. The settings keep the map */
static struct { uint8_t on, pick, trk, id; } mlu;
static uint32_t ml_code(uint32_t id) { return id >= P_E0 ? ML_E0 + id - P_E0 : id + 1u; }
static void ml_put(uint32_t i, uint32_t e)       /* (the ISR reads the entry: never half of one) */
{
    fm1_irq_off();
    ml_tab[i % ML_N] = (uint16_t)e;
    fm1_irq_on();
}
static uint32_t ml_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        n += (ml_tab[i] >> 9) != 0u;
    return n;
}
/* cc -> parameter id of track k, in place of what either had; 0 = no room, 2 = a CC that is never learned */
static uint32_t ml_learn(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i, e;
    if (!ml_free_cc(cc) || k >= NTRK || id >= P_COUNT)
        return 2;
    for (i = 0; i < ML_N; i++)
        if ((e = ml_tab[i]) >> 9 && ((e & 127u) == cc || ((e >> 9) == ml_code(id) && ((e >> 7) & 3u) == k)))
            ml_put(i, 0);
    for (i = 0; i < ML_N; i++)
        if (!(ml_tab[i] >> 9)) {
            ml_put(i, cc | k << 7 | ml_code(id) << 9);
            return 1;
        }
    return 0;
}
static int ml_clear_all(void)                    /* HOME > SYSTEM > LEARN CLEAR; 1 = there were */
{
    uint32_t i, n = ml_count();
    for (i = 0; i < ML_N; i++)
        ml_put(i, 0);
    if (n)
        settings_later = 1;                      /* (saved when quiet) */
    return n != 0u;
}
static void ml_toggle(void)                      /* GLO + F#3 */
{
    mlu.on = (uint8_t)!mlu.on;
    mlu.pick = 0;
    ml_arm = 0;
    ml_heard = 0;
    ui_message(mlu.on ? "LEARN: TURN A KNOB" : "LEARN DONE");
    ui.force = 1;
}
/* a knob turned parameter id of track t: picked while learning (ui_input.c) */
static void ml_knob(const track_t *t, uint32_t id)
{
    char b[16];
    if (!mlu.on || id >= P_COUNT)
        return;
    if (!mlu.pick || mlu.id != id || mlu.trk != (uint8_t)trk_index(t)) {
        mlu.trk = (uint8_t)trk_index(t);
        mlu.id = (uint8_t)id;
        b[0] = 'T';
        b[1] = (char)('1' + mlu.trk);
        b[2] = ' ';
        str_cpy(b + 3, track_desc(t, id)->label, 9);
        ui_say(b, ": SEND A CC");
    }
    mlu.pick = 1;
    ml_heard = 0;
    ml_arm = 1;
}
/* each frame: a CC heard for the picked parameter is learned */
static void ml_poll(void)
{
    uint32_t cc = ml_heard, r;
    char a[16], b[16];
    if (!cc)
        return;
    ml_heard = 0;
    if (!mlu.on || !mlu.pick)
        return;
    cc--;
    r = ml_learn(cc, mlu.trk, mlu.id);
    if (r != 1u) {
        ui_message(r ? "CC NOT LEARNABLE" : "LEARN FULL");
        return;
    }
    str_cpy(a, "CC", 4);
    fmt_int(a + 2, (int32_t)cc);
    str_cpy(a + str_len(a), " = T", 5);
    b[0] = (char)('1' + mlu.trk);
    b[1] = ' ';
    str_cpy(b + 2, track_desc(&trk[mlu.trk % NTRK], mlu.id)->label, 9);
    ui_say(a, b);
    mlu.pick = 0;
    ml_arm = 0;
    settings_later = 1;                          /* (saved when quiet) */
}
