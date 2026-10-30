/* SPDX-License-Identifier: GPL-3.0-only */
/* SETUP > SCREEN > SCREEN OFF (2026-10-24, as fm1-midi-bridge has it): stopped and untouched for the time set (NEVER, the
 * default, 1, 5, 15 minutes), the panel sleeps (DISPOFF, SLPIN) and the backlight goes dark with it: PA2 is also
 * the key matrix's /OE, so the scan lights it only for its reads (fm1_input.h fm1_dark). Kept on by a song
 * playing (the count-in too), a message on the top bar, a button or key held. A button, key or knob, or a
 * message on the pedal's channel, wakes it; the press or turn that wakes it does nothing else, nor does anything
 * else until every button is let go (scr_input), but the pedal's message does what it always does (pedal.c). A
 * Program Change or MIDI LEARN goes on in the dark too. Waking, a frame at a time (never a wait in the loop): 120 ms
 * after the SLPIN, SLPOUT; 120 ms after that one frame redraws everything (ui.force), then DISPON and the light.
 * The panel keeps drawing while it sleeps (its RAM works in sleep), so nothing waits for the wake. */
#ifndef UI_SMALL                 /* (compiled for size on the unit: core.h; a host test or the loader alone: nothing) */
#define UI_SMALL
#endif
enum { SCR_ON, SCR_ASLEEP, SCR_WAKING, SCR_SHOWING };
static const uint32_t SCR_MS[SCR_N] = {0u, 60000u, 300000u, 900000u};
static uint8_t scr_state, scr_eat, scr_want;
static uint32_t scr_t, scr_seen_ms, scr_pedal;

/* in place of ui_input (main.c): while dark, waking or eating, the panel's input is taken and dropped */
UI_SMALL static void scr_input(void)
{
    uint32_t e, any;
    if (scr_state == SCR_ON && !scr_eat) {
        ui_input();
        return;
    }
    any = fm1_input_edges(0) | fm1_input_note_edges() | fm1_in.buttons | fm1_in.notes;
    for (e = 0; e < FM1_NENC; e++)
        any |= (uint32_t)(fm1_enc_take(e) != 0);
    ui.home_t0 = 0;                                     /* (HOME let go later: no tap) */
    if (any)
        scr_want = scr_eat = 1;
    else if (scr_state == SCR_ON)
        scr_eat = 0;                                    /* everything let go: the panel is the page's again */
    pc_poll();
    ml_poll();
}

/* once a frame, after ui_draw */
UI_SMALL static void scr_poll(void)
{
    uint32_t now = fm1_ms, t = SCR_MS[scr_off % SCR_N], idle;
    if (song.playing || ui.msg_t || fm1_in.buttons || fm1_in.notes || pedal_seen_ms != scr_pedal || !t) {
        scr_pedal = pedal_seen_ms;
        scr_seen_ms = now;
        scr_want = 1;
    }
    idle = now - scr_seen_ms < now - ui_input_ms ? now - scr_seen_ms : now - ui_input_ms;
    switch (scr_state) {
    case SCR_ON:
        scr_want = 0;
        if (t && idle >= t) {
            lcd_cmd(0x28);                              /* DISPOFF */
            lcd_cmd(0x10);                              /* SLPIN */
            fm1_dark = 1;
            scr_state = SCR_ASLEEP;
            scr_t = now;
        }
        break;
    case SCR_ASLEEP:
        if (scr_want && now - scr_t >= 120u) {
            lcd_cmd(0x11);                              /* SLPOUT */
            scr_state = SCR_WAKING;
            scr_t = now;
        }
        break;
    case SCR_WAKING:
        if (now - scr_t >= 120u) {
            ui.force = 1;                               /* the next frame draws it all */
            scr_state = SCR_SHOWING;
        }
        break;
    default:                                            /* SCR_SHOWING: drawn */
        lcd_cmd(0x29);                                  /* DISPON */
        fm1_dark = 0;
        scr_state = SCR_ON;
        scr_seen_ms = now;
        break;
    }
}

/* UBOOT, an update: the panel on at once (blocking, ~240 ms) before they draw */
UI_SMALL static void scr_wake_now(void)
{
    if (scr_state == SCR_ON)
        return;
    fm1_delay_ms(120);
    lcd_cmd(0x11);
    fm1_delay_ms(120);
    lcd_cmd(0x29);
    fm1_dark = 0;
    scr_state = SCR_ON;
}
