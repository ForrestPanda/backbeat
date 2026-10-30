/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA boot and main loop. Boot order: WDT first, boot-loop guard, fatal
 * vectors, guards; then LCD, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
#ifndef UI_SMALL                 /* (compiled for size on the unit: core.h; a host test or the loader alone: nothing) */
#define UI_SMALL
#endif
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];


/* TIMER5 outranks ALNK0 (timer5_start): the scan keeps its pace while a half buffer renders. Nested in
 * the render it only scans and counts ms; the USB and UART polls wait for the first tick after the
 * render, so usb_poll never runs nested. The USB audio stream cannot wait (one packet per 1 ms frame, a
 * render takes up to a few ms): uac_service also runs nested. It touches only EP4 (INDEX set on every
 * access) and the consumer side of the audio ring, and usb_poll never runs nested, so the two never
 * interleave. The time spent nested is handed to the audio ISR: its load figures stay render-only.
 * (After Felucca 1.0.) */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: all USB SIE traffic lives here */
#if FELUCCA_UART
    if (sub % 5u == 2u)
        owed |= 2u;                             /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
#if FELUCCA_UAC
        if (usb_due)
            uac_service();
#endif
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
#if FELUCCA_UART
    if (owed & 2u)
        uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);

UI_SMALL static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    /* above ALNK0 (3): the tick may nest into the audio render (a few us; plain registers, the
     * MIDI rings are single-producer / single-consumer). Below it, a tick waited for the render
     * and the column it had lit stayed on longer: the LEDs shimmered, beating against the audio
     * blocks (1378 / s against the 909 / s scan) */
    fm1_timer5_start(isr_timer5, 4);
}

UI_SMALL static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

UI_SMALL static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "FELUCCA CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->sp);                                     /* (SLOOP 2.7.1) more to read on a photo of it: the stack */
    draw_text_box(10, 144, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 10000u * 1000u * FM1_TICKS_PER_US)   /* (10 s: time for a photo) */
        ;
    fm1_reboot();
}

/* core 0's two stacks (app.ld: 24 KB from _ustack_lo, 8 KB from _sstack_lo), never measured before (SLOOP 2.7.1,
 * taken 2026-10-30): painted at boot with a mark up to 512 B below whichever of sp / ssp / usp sits in each region
 * (the live frames), so the console's status can say how deep each has gone since (stack_main_free, stack_sup_free:
 * bytes above the guard band never reached) and which region the main loop runs on (stack_sp). What they say decides
 * whether _guard0 may move up and the pool grow (design.md, milestone 8) */
extern char _ustack_lo[], _ustack_top[], _sstack_lo[], _sstack_top[];
#define STACK_MARK 0x4B4D5453u                  /* "STMK" */
UI_SMALL static void stack_paint(char *lo, char *top)
{
    uint32_t a = (uint32_t)(uintptr_t)lo, lim = (uint32_t)(uintptr_t)top, p[3], i, *d;
    p[0] = fm1_sp();                            /* (fm1_guard.h) */
    p[1] = fm1_ssp();
    p[2] = fm1_usp();
    for (i = 0; i < 3u; i++)
        if (p[i] >= a + 512u && p[i] < lim + 512u && p[i] - 512u < lim)
            lim = p[i] - 512u;
    for (d = (uint32_t *)lo; (uint32_t)(uintptr_t)d < lim; d++)
        *d = STACK_MARK;
}
UI_SMALL static uint32_t stack_free(const char *lo, const char *top)   /* bytes of marks still in place, from the guard band up */
{
    const uint32_t *d = (const uint32_t *)lo;
    while ((const char *)d < top && *d == STACK_MARK)
        d++;
    return (uint32_t)((const char *)d - lo);
}
static uint32_t stack_main_free(void) { return stack_free(_ustack_lo, _ustack_top); }   /* console.c */
static uint32_t stack_sup_free(void) { return stack_free(_sstack_lo, _sstack_top); }
static uint32_t stack_sp(void) { return fm1_sp(); }

/* power-on: the tracks' defaults, the default kit, empty patterns */
UI_SMALL static void felucca_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        track_defaults_steps(t);              /* the sequencers start empty */
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;        /* the 808 kit */
    song.master_q12 = 2048;
    autosave_resume();                        /* the project as it was left (project.c) */
    song.g[G_SYNC] = (int16_t)lights_sync;    /* a setting of the FM-1 (panel.c) */
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    song.g[G_CH1] = midi_set[0], song.g[G_CH2] = midi_set[1], song.g[G_CH3] = midi_set[2];   /* (2.6: SETUP > MIDI, */
    song.g[G_DRCH] = midi_set[3], song.g[G_CHOTH] = midi_set[4], song.g[G_BEND] = midi_set[5];   /* settings of the FM-1) */
    layers_init();                            /* the panel's layer buttons for the keys (ui_layers.c) */
    go_home();
    ui.force = 1;
}

UI_SMALL static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t healthy_since;
    stack_paint(_ustack_lo, _ustack_top);           /* (the console's stack_main_free / stack_sup_free) */
    stack_paint(_sstack_lo, _sstack_top);
    persist_boot();
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();                             /* staging area left by an update */
#endif
    settings_init();
    lcd_init();
    logo_splash();                                      /* the BackBeat logo (splash.c) */
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.in_audio = 0;                       /* .noinit: a reset inside the audio ISR left it set, and
                                                       TIMER5 would treat every tick as nested (no USB poll) */
    felucca_dbg.prev_stage = felucca_dbg.stage;     /* a WDT reset leaves the last breadcrumb here */
    felucca_dbg.prev_page = felucca_dbg.page;
    felucca_dbg.prev_home = felucca_dbg.home;
    felucca_dbg.prev_frames = felucca_dbg.ui_frames;
    felucca_dbg.prev_rst = fm1_boot.p3_rst;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();
    audio_init();
#if FELUCCA_CDC
    usb_cdc_on = usb_serial;                            /* menu USB SERIAL (persist_boot read it): before USB starts */
#endif
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u) {
        panel_setup();                        /* OCT- + OCT+ held at power-on */
        settings_save();
    }
    fm1_delay_ms(900);                                  /* (the logo stays a moment) */
    lcd_fill(0, 0, 240, 240, C_BLACK);

    healthy_since = fm1_ms;
    battlog_boot();                                     /* the battery log goes on across a reset (battlog.c) */
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        sng_poll();                                     /* fm1-drummer: a song asked for by the pedal (PC) */
        if ((uint32_t)(m - healthy_since) >= 30000u && bootguard.pending) {
            bootguard_clear(&bootguard);     /* 30 s of this boot, even if TIMER4 survived reset */
        }
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* battery: slow IIR */
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                song.master_q12 = (k10 * k10) >> 8;            /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s, stopped: enter UBOOT with RAM intact (debug / update); a countdown
             * shows from 2 s, letting go cancels it. Not while playing: on the drum track these are the
             * ghost / hard modifiers, held for a long time */
            static uint32_t t0, shown;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both || song.playing) {
                if (shown)
                    ui_say("UPDATE MODE ", "CANCELLED");
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    ui_say("UPDATE MODE IN ", d);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
#if FELUCCA_DRUMMER
                persist_flush_now();                    /* (the settings, the project: RAM-only so far) */
#endif
                scr_wake_now();
                lcd_fill(0, 0, 240, 240, C_BLACK);
                draw_text_box(0, 110, 240, &FONT_S, "UBOOT", RGB(80, 120, 255), 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;                  /* intentional reset: not a failed boot */
                fm1_enter_uboot();
            }
        }
#if FELUCCA_OTA
        ed_service();                                   /* web editor SysEx */
        ota_service();                                  /* M-UPGRADE handshake */
        if (usb.ota_req) {                              /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
#if FELUCCA_DRUMMER
            persist_flush_now();                        /* an update ends in a reset: RAM-only work first */
#endif
            scr_wake_now();
            if (flash_ok)
                ota_session();                          /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
#if FELUCCA_DRUMMER
            persist_flush_now();                    /* (the settings, the project: RAM-only so far) */
#endif
            scr_wake_now();
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
#if FELUCCA_CDC
        cdc_task();
#endif
        felucca_dbg.ui_frames++;
        felucca_dbg.page = ui.page;
        felucca_dbg.home = ui.home;
        felucca_dbg.stage = 1;
        scr_input();
        felucca_dbg.stage = 2;
        ui_leds();
        ui_draw();
        scr_poll();
        felucca_dbg.stage = 8;
        autosave_tick();                                /* the working project into flash, when quiet */
        battlog_tick();                                 /* the battery, once a minute (battlog.c) */
#if FELUCCA_DRUMMER
        settings_flush();                               /* the settings changed while playing, when quiet */
#endif
        felucca_dbg.stage = 9;
        while (fm1_ms - m < 15u) {                               /* ~60 UI frames/s at most */
            scr_input();
#if FELUCCA_OTA
            ed_service();                       /* editor replies without waiting for the next frame */
#endif
            fm1_idle();                         /* the core halts until the next interrupt (TIMER5's tick, 100 us;
                                                 * the audio block): the battery (2026-10-27; hal/fm1_sys.h) */
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt, boot_mode;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if ((p3 & 1u) && !(p3 & (4u | 0x40u)))
        bootguard_clear(&bootguard);           /* a power-on (not a watchdog or soft reset): no failed boot to count */
    boot_mode = bootguard_begin(&bootguard);
    if (boot_mode == BOOT_ROM) {
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
#if FELUCCA_OTA
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();
#else
    if (boot_mode == BOOT_RECOVERY) fm1_enter_uboot();
#endif
    fm1_main();
    for (;;)
        ;
}
