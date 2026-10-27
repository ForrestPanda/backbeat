/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SETUP (fm1-drummer: GLO tapped; SLOOP's menu, its GLO pages and its GLO layer in one place), in sections of up to
 * four rows: PEDAL, MIDI, MIDI 2 (LEARN until 2026-10-22), TEMPO, DRUMS, MASTER, TEXTURE (two), SCREEN, LIGHTS, AUDIO, LOAD, SYSTEM. SELECT goes to
 * the section before / after (stopping at the ends), KNOB 1..4 set its rows in order (the knob's colour marks its
 * row), PRESETS moves the cursor; OCT+ steps the cursor's setting round or opens it (CALIBRATION, PROJECTS, ABOUT),
 * OCT-, GLO or HOME close (from ABOUT: back to the section). A row is a setting of its own (MI_*), a global
 * (song.g, as the GLO pages had them) or the drum track's parameter. The pedal's settings are saved with the songs
 * (sng.c arrset_save, once stopped: arrset_dirty), the others as before (settings_save, the project). */
/* ------------------------------------------------------------ menu --- */
enum { MI_PCH, MI_PCOUNT, MI_AUTO, MI_LEARN, MI_LCLEAR, MI_COLOR, MI_ZOOM, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_LOWCUT,
       MI_USB, MI_SERIAL, MI_PANEL, MI_PROJ, MI_ABOUT, MI_HUMAN, MI_CLOCK, MI_LOAD, MI_WORST, MI_LATE, MI_VOICES,
       MI_THRU, MI_SCROFF, MI_N };
enum { RK_MI, RK_G, RK_P };                        /* a row: a setting (MI_*), song.g[id], TDRUM->p[id] */
typedef struct { const char *name; uint8_t kind, id; } mrow_t;
static const mrow_t MROW[] = {
    {"PEDAL CHANNEL", RK_MI, MI_PCH}, {"COUNT-IN", RK_MI, MI_PCOUNT}, {"LOAD LAST SONG AT START", RK_MI, MI_AUTO},
    {"MIDI CLOCK IN", RK_MI, MI_CLOCK},          /* (G_SYNC: was LEARN's SYNC, INT USB TRS) */
    {"DRUMS CHANNEL", RK_G, G_DRCH}, {"IN", RK_G, G_ROUTE}, {"OTHER CHANNELS", RK_G, G_CHOTH}, {"OUT", RK_G, G_MIDI},
    {"MIDI LEARN", RK_MI, MI_LEARN}, {"MIDI LEARN CLEAR", RK_MI, MI_LCLEAR}, {"TRS TO USB", RK_MI, MI_THRU},
    {"TEMPO", RK_G, G_BPM}, {"CLICK", RK_G, G_CLOCK}, {"TUNE", RK_G, G_TUNE},
    {"SWING", RK_G, G_SWING}, {"HUMANIZE", RK_MI, MI_HUMAN},
    {"LEVEL", RK_G, G_DRLVL}, {"REVERB", RK_G, G_DRREV}, {"DELAY", RK_G, G_DRDLY}, {"PAN", RK_P, P_PAN},
    {"DUST", RK_G, G_DUST}, {"DUCK", RK_G, G_DUCK}, {"FILTER", RK_G, G_FILT}, {"ROLL RATE", RK_G, G_ROLL},
    {"SOURCE", RK_G, G_TXSRC}, {"LEVEL", RK_G, G_TXLVL}, {"TONE", RK_G, G_TXTONE}, {"MOVE", RK_G, G_TXMOVE},
    {"RATE", RK_G, G_TXRATE}, {"WHEN", RK_G, G_TXWHEN}, {"DUCK", RK_G, G_TXDUCK},
    {"COLOR", RK_MI, MI_COLOR}, {"ZOOM", RK_MI, MI_ZOOM}, {"SCREEN OFF", RK_MI, MI_SCROFF},
    {"LIGHTS", RK_MI, MI_LIGHTS}, {"KEYS", RK_MI, MI_KEYS}, {"NOTES", RK_MI, MI_NOTES},
    {"SPEAKER LOWCUT", RK_MI, MI_LOWCUT}, {"USB AUDIO", RK_MI, MI_USB}, {"USB SERIAL", RK_MI, MI_SERIAL},
    {"AUDIO LOAD", RK_MI, MI_LOAD}, {"WORST BLOCK", RK_MI, MI_WORST}, {"LATE BLOCKS", RK_MI, MI_LATE},
    {"DRUM VOICES", RK_MI, MI_VOICES},
    {"HARDWARE CALIBRATION", RK_MI, MI_PANEL}, {"JAM PROJECTS", RK_MI, MI_PROJ}, {"ABOUT", RK_MI, MI_ABOUT},
};
#define MI_COUNT (sizeof MROW / sizeof MROW[0])
enum { MS_PEDAL, MS_MIDI, MS_LEARN, MS_TEMPO, MS_FEEL, MS_DRUMS, MS_MASTER, MS_TEX, MS_TEX2, MS_SCREEN, MS_LIGHTS,
       MS_AUDIO, MS_LOAD, MS_SYSTEM, MS_COUNT };
static const char *const MS_NAME[MS_COUNT] = {"PEDAL", "MIDI", "MIDI 2", "TEMPO", "FEEL", "DRUMS", "MASTER", "TEXTURE",
                                              "TEXTURE 2", "SCREEN", "LIGHTS", "AUDIO", "LOAD",
                                              "SYSTEM"};
static const uint8_t MS_FIRST[MS_COUNT + 1] = {0, 4, 8, 11, 14, 16, 20, 24, 28, 31, 34, 37, 40, 44,
                                                MI_COUNT};   /* rows of
                                                                                                          * each */
_Static_assert(MI_COUNT == 47, "MS_FIRST follows MROW");
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH"};   /* every button lit, the labels readable */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS", "ALL KEYS"};      /* keys lit too, at the LIGHTS level */
static const char *const SCR_NAME[SCR_N] = {"NEVER", "1 MIN", "5 MIN", "15 MIN"};   /* stopped, untouched that long */
#define MI_Y0 24                                   /* the first row, under the section line */
#define MI_DY 48                                   /* a row: the label, the value in large type */

static uint32_t mi_sec(uint32_t i)                 /* the section of row i */
{
    uint32_t k = 0;
    while (k + 1u < MS_COUNT && i >= MS_FIRST[k + 1u])
        k++;
    return k;
}

static uint32_t mi_row(uint32_t i) { return i - MS_FIRST[mi_sec(i)]; }   /* its row: KNOB 1 + row sets it */
static uint32_t mi_find(uint32_t id)               /* the row of setting MI_* id */
{
    uint32_t i;
    for (i = 0; i < MI_COUNT && !(MROW[i].kind == RK_MI && MROW[i].id == id); i++)
        ;
    return i;
}

/* LOAD (2026-10-22): what the audio interrupt takes of its time, on the device itself (audio.c measures each half
 * buffer's render: HALF_FRAMES, 256 frames, 5.8 ms): the average (song.cpu_q8), the worst half since the reset, the halves the DMA
 * had moved past before they were rendered (heard as a click), and the drum voices: the most at once, and the hits
 * that found none free (drums.c drum_voice: a quieter one cut). OCT+ on any row resets them, to measure one song. Read
 * four times a second, so the screen does not redraw every frame for the average's last digit */
static struct { uint32_t t, load, worst, late, late0, peak, full; } meter;
static void meter_read(void)
{
    if (meter.t && fm1_ms - meter.t < 250u)
        return;
    meter.t = fm1_ms ? fm1_ms : 1u;
    meter.load = song.cpu_q8 * 100u / 256u;
    meter.worst = audio_max_us * 100u / (HALF_FRAMES * 1000000u / FS);
    meter.late = felucca_dbg.late - meter.late0;
    meter.peak = drums.vpeak;
    meter.full = drums.full;
}
static void meter_reset(void)
{
    audio_max_us = 0;
    meter.late0 = felucca_dbg.late;
    drums.vpeak = 0;
    drums.full = 0;
    meter.t = 0;
    ui_message("LOAD RESET");
}

/* row i's value as shown, and its colour */
static const char *mi_value(uint32_t i, uint16_t *c)
{
    static char b[24];                               /* (the longest: "12 OF 12, 9999 FULL") */
    const mrow_t *r = &MROW[i % MI_COUNT];
    *c = C_HI;
    if (r->kind != RK_MI) {                           /* a global or the drum track's: as its page showed it */
        const param_desc_t *d = r->kind == RK_G ? &GP[r->id] : &TP[r->id];
        const char *unit;
        param_format(d, r->kind == RK_G ? song.g[r->id] : TDRUM->p[r->id], b, &unit);
        if (unit[0] >= 'A')
            str_cpy(b + str_len(b), " ", 2);        /* ("123 BPM"; "50%") */
        str_cpy(b + str_len(b), unit, sizeof b - str_len(b));
        return b;
    }
    switch (r->id) {
    case MI_PCH:
        if (!pedal.ch || pedal.ch == PEDAL_CH_OMNI) {
            *c = pedal.ch ? C_HI : C_DIM;
            return pedal.ch ? "ANY" : "OFF";
        }
        str_cpy(b, "CH ", 4);
        fmt_int(b + 3, pedal.ch);
        return b;
    case MI_PCOUNT: return pedal.count == 0u ? "NONE" : pedal.count == 1u ? "1 BAR" : "2 BARS";
    case MI_AUTO: return sng_autoload ? "ON" : "OFF";
    case MI_CLOCK:                                    /* the tempo and START / STOP from a MIDI clock: off, or from where */
        *c = song.g[G_SYNC] ? C_HI : C_DIM;
        return song.g[G_SYNC] == 2 ? "TRS" : song.g[G_SYNC] == 1 ? "USB" : "OFF";
    case MI_HUMAN:
        *c = drum_hum ? C_HI : C_DIM;
        return drum_hum == 1u ? "LIGHT" : drum_hum == 2u ? "MORE" : "OFF";
    case MI_LEARN: return mlu.on ? "ON" : "OFF";
    case MI_COLOR: return PALETTES[settings.palette].name;
    case MI_ZOOM: return settings.zoom ? "ON" : "OFF";
    case MI_SCROFF:
        *c = scr_off ? C_HI : C_DIM;
        return SCR_NAME[scr_off % SCR_N];
    case MI_LIGHTS: return LIGHTS_NAME[lights_lvl % LIGHTS_N];
    case MI_KEYS:
        *c = lights_lvl ? C_HI : C_DIM;               /* (needs LIGHTS) */
        return KEYS_NAME[lights_keys % KEYS_N];
    case MI_NOTES: return lights_notes ? "ON" : "OFF";
    case MI_LOWCUT: return settings.lowcut ? "ON" : "OFF";
    case MI_USB: return usb_full ? "FULL" : "MASTER";
    case MI_SERIAL: return usb_serial ? "ON" : "OFF";
    case MI_THRU:                                     /* the TRS jack's messages out over USB too (usb.c midi_thru); BRIDGE
                                                       * ONLY: out over USB, and the FM-1 leaves them alone */
        *c = midi_thru ? C_HI : C_DIM;
        return midi_thru == THRU_ONLY ? "BRIDGE ONLY" : midi_thru ? "ON" : "OFF";
    case MI_LOAD:
    case MI_WORST:
        meter_read();
        fmt_int(b, (int32_t)(r->id == MI_LOAD ? meter.load : meter.worst));
        str_cpy(b + str_len(b), "%", 2);
        *c = (r->id == MI_LOAD ? meter.load : meter.worst) >= 80u ? C_AMB : C_HI;   /* (100: a click) */
        return b;
    case MI_LATE:
        meter_read();
        if (!meter.late) {
            *c = C_DIM;
            return "NONE";
        }
        *c = C_AMB;
        fmt_int(b, (int32_t)meter.late);
        return b;
    case MI_VOICES:                                   /* "9 OF 12", "12 OF 12, 37 FULL" */
        meter_read();
        fmt_int(b, (int32_t)meter.peak);
        str_cpy(b + str_len(b), " OF ", 5);
        fmt_int(b + str_len(b), NDRUM);
        if (meter.full) {
            *c = C_AMB;
            str_cpy(b + str_len(b), ", ", 3);
            fmt_int(b + str_len(b), (int32_t)(meter.full > 9999u ? 9999u : meter.full));
            str_cpy(b + str_len(b), " FULL", 6);
        }
        return b;
    case MI_LCLEAR: {                                 /* (2.6) the CCs MIDI LEARN holds; OCT+ clears them */
        uint32_t n = ml_count();
        if (!n) {
            *c = C_DIM;
            return "NONE";
        }
        fmt_int(b, (int32_t)n);
        str_cpy(b + str_len(b), " CC", 4);
        return b;
    }
    default:
        *c = C_DIM;
        return "";                                    /* (an action: OCT+ opens it) */
    }
}

static void draw_about(void)
{
    uint32_t i;
    cv_text(4, 4, &FONT_L, "BACKBEAT", C_WHITE);
    for (i = 0; i < 4u; i++)                          /* the logo's beat bars: the 2 and the 4 tall */
        cv_rect(196 + i * 10u, (i & 1u) ? 6u : 16u, 7, (i & 1u) ? 24u : 14u, TE_COL[i]);
    cv_text(4, 36, &FONT_S, "BASED ON SLOOP, FELUCCA", C_AMB);
    cv_text(4, 54, &FONT_S, FELUCCA_VERSION, C_HI);
    cv_text(236 - text_w(&FONT_S, __DATE__), 54, &FONT_S, __DATE__, C_GRAY);   /* build date */
    cv_text(cv_text(4, 72, &FONT_S, "LEO KUROSHITA", C_HI) + 8, 72, &FONT_S, "@KUROGEDELIC", C_AMB);
    cv_text(4, 88, &FONT_S, "H\xDCGELTON INSTRUMENTS", C_HI);   /* Latin-1 U-umlaut */
    cv_text(4, 104, &FONT_S, "HUGELTON.COM", C_AMB);
    cv_text(4, 119, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
    cv_text(4, 132, &FONT_S, "GITHUB.COM/ISOD89/SLOOP-FM1", C_AMB);   /* (the source it forks) */
    cv_text(4, 146, &FONT_S, "FONT: TERMINUS (OFL)", C_DIM);
    cv_text(4, 159, &FONT_S, "SAMPLES: VERSILIAN (CC0)", C_DIM);
    cv_text(4, 172, &FONT_S, "+ SONIC PI (CC0)", C_DIM);
}

static void draw_menu(void)
{
    uint32_t i, pass, sec = mi_sec(ui.menu_sel % MI_COUNT);
    uint32_t sig = ui.menu * 7u + ui.menu_sel * 131u + ml_count() * 2654435u + mlu.on * 5u;
    for (i = MS_FIRST[sec]; i < MS_FIRST[sec + 1u]; i++) {
        uint16_t c;
        sig = str_hash(sig * 31u, mi_value(i, &c)) + c;
    }
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..239 */
        lcd_fill(0, H_HEAD + 1 + 124 + 95, 240, 240 - (H_HEAD + 1 + 124 + 95), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, ui.menu == 2 ? "ABOUT" : "SETUP", C_HI);
    if (ui.menu != 2) {                             /* the section and its number, as a page's "ENV DEST 2/2" */
        char t[20];
        str_cpy(t, MS_NAME[sec], sizeof t);
        str_cpy(t + str_len(t), " ", 2);
        fmt_int(t + str_len(t), (int32_t)sec + 1);
        str_cpy(t + str_len(t), "/", 2);
        fmt_int(t + str_len(t), MS_COUNT);
        cv_text(236 - text_w(&FONT_S, t), 1, &FONT_S, t, C_WHITE);
    }
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: draw in two bands */
        cv_begin(240, pass ? 95u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        if (ui.menu == 2) {
            draw_about();
        } else {
            char t[16];
            if (sec) {                              /* the sections either side, SELECT's way there */
                t[0] = '<';
                t[1] = ' ';
                str_cpy(t + 2, MS_NAME[sec - 1u], sizeof t - 2u);
                cv_text(4, 4, &FONT_S, t, C_DIM);
            }
            if (sec + 1u < MS_COUNT) {
                str_cpy(t, MS_NAME[sec + 1u], sizeof t - 2u);
                str_cpy(t + str_len(t), " >", 3);
                cv_text(236 - text_w(&FONT_S, t), 4, &FONT_S, t, C_DIM);
            }
            for (i = MS_FIRST[sec]; i < MS_FIRST[sec + 1u]; i++) {
                uint32_t r = i - MS_FIRST[sec];
                int32_t y = MI_Y0 + (int32_t)r * MI_DY, xv;
                int cur = i == ui.menu_sel;
                uint16_t vc;
                const char *v = mi_value(i, &vc);
                char k[4] = {'K', (char)('1' + r), 0, 0};
                cv_rect(4, y + 2, 4, MI_DY - 6, cur ? C_WHITE : TE_COL[r & 3u]);   /* the knob's colour */
                xv = cv_text(14, y, &FONT_S, k, TE_COL[r & 3u]);
                cv_text(xv + 6, y, &FONT_S, MROW[i].name, cur ? C_WHITE : C_GRAY);
                if (v[0] && text_w(&FONT_L, v) <= 222) {
                    xv = cv_text(14, y + 15, &FONT_L, v, cur && vc == C_HI ? C_WHITE : vc);
                } else if (v[0]) {                      /* (too wide for the large type: WHITE KEYS) */
                    xv = cv_text(14, y + 22, &FONT_S, v, cur && vc == C_HI ? C_WHITE : vc);
                } else {
                    xv = cv_text(14, y + 22, &FONT_S, "OCT+ OPENS", cur ? C_WHITE : C_DIM);
                }
                if (MROW[i].kind == RK_MI && MROW[i].id == MI_COLOR) {   /* the palette's colours */
                    uint32_t q;
                    for (q = 0; q < 5u; q++)
                        cv_rect(xv + 10 + (int32_t)q * 14, y + 26, 10, 10, pal[q]);
                }
                if (MROW[i].kind == RK_MI && MROW[i].id == MI_SERIAL && usb_serial != usb_cdc_now())
                    cv_text(xv + 8, y + 28, &FONT_S, "RESTART", C_AMB);   /* (usb.c: at the next start) */
            }
        }
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void layer_unlock(void);                        /* ui_input.c */
static void menu_open(void)                            /* GLO tapped: SETUP, where it was left */
{
    layer_unlock();
    ui.menu = 1;
    ui.menu_sel = (uint8_t)(ui.menu_sel % MI_COUNT);
    ui.confirm = 0;
    ui.force = 1;
    song.seq_mode = 0;
}

static void menu_close(void)
{
    if (song.playing || transport_req)
        settings_later = 1;                            /* (a flash write stops the audio: once stopped) */
    else
        settings_save();                               /* palette / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* row i: a knob turned s (> 0 right, < 0 left), or 0: OCT+ (steps round, toggles, or opens) */
static void mi_set(uint32_t i, int32_t s)
{
    const mrow_t *r = &MROW[i % MI_COUNT];
    if (r->kind != RK_MI) {                            /* a value: the knob (OCT+ nothing) */
        const param_desc_t *d = r->kind == RK_G ? &GP[r->id] : &TP[r->id];
        int16_t *vp = r->kind == RK_G ? &song.g[r->id] : &TDRUM->p[r->id];
        if (!s)
            return;
        *vp = (int16_t)(d->fmt != F_ENUM && d->names ? names_turn(d, *vp, s)   /* (a 0-terminated name list: a name a
                                                                                * detent; F_ENUM: a value a detent) */
                                                    : clamp(*vp + accel(EN_K1 + mi_row(i), s, d->max - d->min), d->min, d->max));
        if (r->kind == RK_G && r->id == G_BPM)
            ui.bpm_t = 40;
        return;
    }
    settings_later = 1;                                /* saved when quiet (project.c autosave_tick), not only when the
                                                        * menu closes: USB SERIAL set ON and the unit powered off from
                                                        * the menu lost it (2026-10-27). Unchanged settings write nothing */
    switch (r->id) {
    case MI_PCH:                                       /* OFF, CH 1..16, ANY; OCT+ round */
        pedal.ch = (uint8_t)(s ? clamp((int32_t)pedal.ch + s, 0, PEDAL_CH_OMNI) : (pedal.ch + 1u) % (PEDAL_CH_OMNI + 1u));
        arrset_dirty = 1;
        break;
    case MI_PCOUNT:
        pedal.count = (uint8_t)(s ? clamp((int32_t)pedal.count + s, 0, PEDAL_COUNT_MAX) : (pedal.count + 1u) % (PEDAL_COUNT_MAX + 1u));
        arrset_dirty = 1;
        break;
    case MI_AUTO:
        sng_autoload = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !sng_autoload);
        if (sng_autoload && sng_cur >= 0)
            sng_last = (uint8_t)(sng_cur + 1);         /* (the song loaded now is the last one) */
        arrset_dirty = 1;
        break;
    case MI_CLOCK:                                     /* OFF, USB, TRS; OCT+ round (kept with the settings: project.c) */
        song.g[G_SYNC] = (int16_t)(s ? clamp(song.g[G_SYNC] + s, 0, 2) : (song.g[G_SYNC] + 1) % 3);
        break;
    case MI_HUMAN:                                     /* OFF, LIGHT, MORE; OCT+ round (kept with the settings) */
        drum_hum = (uint8_t)(s ? clamp((int32_t)drum_hum + s, 0, (int32_t)HUM_N - 1) : (drum_hum + 1u) % HUM_N);
        break;
    case MI_LEARN:                                     /* (2.6) on: a knob turned on a page, then a CC */
        if (!s || (s > 0) != (mlu.on != 0))
            ml_toggle();
        break;
    case MI_COLOR:                                     /* both ways round; OCT+ the next */
        settings.palette = (settings.palette + (s < 0 ? NPALETTES - 1u : 1u)) % NPALETTES;
        palette_set(settings.palette);
        break;
    case MI_ZOOM:
    case MI_LOWCUT: {                                  /* right ON, left OFF; OCT+ toggles */
        uint32_t *v = r->id == MI_LOWCUT ? &settings.lowcut : &settings.zoom;
        *v = s > 0 ? 1u : s < 0 ? 0u : !*v;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
        break;
    }
    case MI_USB:                                       /* right FULL, left MASTER */
        usb_full = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_full);
        break;
    case MI_SERIAL:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
    case MI_NOTES:
        lights_notes = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !lights_notes);
        break;
    case MI_THRU:                                      /* OFF, ON, BRIDGE ONLY: the knob along (stops at the ends), OCT+
                                                        * round (kept with the settings) */
        midi_thru = (uint8_t)(s ? clamp((int32_t)midi_thru + (s > 0 ? 1 : -1), 0, THRU_ONLY) : (midi_thru + 1u) % (THRU_ONLY + 1u));
        break;
    case MI_SCROFF:                                    /* NEVER, 1, 5, 15 MIN: the knob along (stops at the ends), OCT+
                                                        * round (kept with the settings) */
        scr_off = (uint8_t)(s ? clamp((int32_t)scr_off + (s > 0 ? 1 : -1), 0, SCR_N - 1) : (scr_off + 1u) % SCR_N);
        break;
    case MI_LIGHTS:
    case MI_KEYS: {                                    /* brighter / dimmer (stops at the ends); OCT+ steps round */
        uint8_t *v = r->id == MI_LIGHTS ? &lights_lvl : &lights_keys;
        uint32_t n = r->id == MI_LIGHTS ? LIGHTS_N : KEYS_N;
        if (s > 0 && *v + 1u < n)
            (*v)++;
        else if (s < 0 && *v > 0u)
            (*v)--;
        else if (!s)
            *v = (uint8_t)((*v + 1u) % n);
        if (r->id == MI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                   /* keys lit need a level: the lowest */
        break;
    }
    case MI_LCLEAR:                                    /* (2.6) every learned CC: OCT+ */
        if (!s)
            ui_message(ml_clear_all() ? "LEARN CLEARED" : "NOTHING LEARNED");
        break;
    case MI_PANEL:                                     /* actions: OCT+ only */
        if (!s) {
            panel_setup();
            ui.force = 1;
        }
        break;
    case MI_PROJ:                                      /* SLOOP's project slots (JAM): their pages */
        if (!s) {
            menu_close();
            open_family(FAM_SAVE);
        }
        break;
    case MI_ABOUT:
        if (!s) {
            ui.menu = 2;
            ui.force = 1;
        }
        break;
    case MI_LOAD:                                      /* LOAD: read only; OCT+ resets the worst, the late, the voices */
    case MI_WORST:
    case MI_LATE:
    case MI_VOICES:
        if (!s)
            meter_reset();
        break;
    default:
        break;
    }
}

/* SETUP: SELECT the section, KNOB 1..4 its rows, PRESETS the cursor, OCT+ ok, OCT- / GLO / HOME close (ABOUT ->
 * the section) */
static void menu_input(uint32_t pressed, uint32_t home)
{
    int32_t s;
    uint32_t k, ok = (pressed >> panel.btn[B_OCTUP]) & 1u;
    uint32_t back = ((pressed >> panel.btn[B_OCTDN]) | (pressed >> panel.btn[B_GLO])) & 1u;
    uint32_t sel = ui.menu_sel % MI_COUNT, sec = mi_sec(sel), n = MS_FIRST[sec + 1u] - MS_FIRST[sec];
    if (home == 1u) {                                  /* BT_TAP */
        menu_close();
        return;
    }
    if (back) {
        if (ui.menu == 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if (ui.menu != 1) {
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {             /* the section before / after (stops at the ends) */
        int32_t to = clamp((int32_t)sec + (s > 0 ? 1 : -1), 0, MS_COUNT - 1);
        if ((uint32_t)to != sec) {
            ui.menu_sel = MS_FIRST[to];
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0)               /* the cursor, round the section's rows */
        ui.menu_sel = (uint8_t)(MS_FIRST[sec] + (sel - MS_FIRST[sec] + (s > 0 ? 1u : n - 1u)) % n);
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0 && k < n) {
            ui.menu_sel = (uint8_t)(MS_FIRST[sec] + k);   /* (the cursor follows the knob turned) */
            mi_set(MS_FIRST[sec] + k, s);
        }
    if (ok)
        mi_set(ui.menu_sel % MI_COUNT, 0);
    panel_enc(EN_ALGO);                                /* (the one knob SETUP has no use for; not enc_drop: a step of
                                                        * SELECT landing after its read above, the input IRQ coming as
                                                        * a read turns it back on, was lost; read next pass now) */
}
