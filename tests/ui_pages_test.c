/* SPDX-License-Identifier: GPL-3.0-only */
/* The real UI (ui.c, ui_draw.c, ui_studio.c, ui_layers.c, ui_song.c, ui_menu.c, ui_input.c) on a
 * framebuffer with panel / flash / storage doubles, the audio (mix_block) running between frames as on
 * the device. Renders every screen to PPM for review (DIR/page-*.ppm, live-*.ppm, layer-*.ppm) and
 * drives the panel:
 *   taps    a layer button tapped opens its pages; held + a key / knob it does not
 *   FX      held: knobs filter, dust, duck, track filter; + OCT-/OCT+: the throws
 *   SEQ     held: the steps on the white keys (drums: the sound played last), OCT: pages, a step key
 *           held + KNOB 1..4: sound / note, level, ratchet, nudge (2.4); PRESETS a lock of the parameter
 *           ALGORITHM picks (the sound parameter last touched first); + OCT-: its nudge, locks and fill
 *           condition go; + OCT+: the condition (normal, fill only, no fill)
 *   EDIT    held + a key: erase; OCT- / OCT+: undo / redo; KNOB 1 shift, 2 length x2
 *   ARP     held + a key: a roll; KNOB 1 the rate
 *   SEL     held + a key: the key of the song
 *   GLO     tapped: SETUP (the pedal, MIDI, tempo, drums, master, texture, screen, lights, audio, system)
 *   REC     held: the clear ring, to the end: cleared (undo brings it back); live recording is gone
 *   SAVE    tapped: the song page; held: the SONG layer (sections A..D: play / store, SONG REC; two or more
 *           section taps: a chain, "chain A B" on the sub line)
 *   DRUMS   the grid: sound / step / hit / level knobs, GRID <-> KIT
 * then 20000 frames of random use: every draw stays on the screen. */
#define FELUCCA_DRUMMER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240*240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ uint32_t i,j; assert(x+w<=240 && y+h<=240); for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i]; }
#include "../firmware/src/gfx.c"
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c)
{ uint32_t i,j; assert(x+w<=240&&y+h<=240); for(j=0;j<h;j++)for(i=0;i<w;i++)screen[(y+j)*240+x+i]=swap16(c); }
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_bg[16];
static volatile uint16_t fm1_led_bg_ns;
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static uint32_t wdt_tick_ms;                       /* (stress_test: 1, the blocking PANEL setup's clock runs out) */
static void fm1_wdt_feed(void) { fm1_ms += wdt_tick_ms; }
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define FELUCCA_ICONS 1
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static uint32_t saves, loads, erases;
static int project_used(uint32_t i) { return i < 2; }
static void project_save(uint32_t i) { (void)i; saves++; ui_message("SAVED"); }
static void project_load(uint32_t i) { (void)i; loads++; }
static void project_erase(uint32_t i) { (void)i; erases++; ui_message("ERASED"); }
static void arrangement_save(void) {}
static uint32_t arrangement_ready(void) { return 3; }
static void arrangement_apply(uint32_t s) { (void)s; }
static uint32_t section_bars(uint32_t s) { return s == 1u ? 2u : 1u; }   /* (B: two bars) */
static void song_backup(void) {}
static void song_restore(void) {}
static uint32_t sec_stores, sec_loads;
static void section_store(uint32_t s) { sec_stores++; live_sec = (int8_t)s; }
static void section_load(uint32_t s) { sec_loads++; live_sec = (int8_t)s; }
static int up_used(uint32_t k) { return k < 2; }
static int up_load(uint32_t k) { (void)k; return 0; }
static int up_load_to(track_t *t, uint32_t k) { (void)t; (void)k; return 0; }
static uint32_t up_count(void) { return 2; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { str_cpy(b, k ? "MY PAD" : "MY LEAD", 13); }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
static void settings_save(void) {}
/* fm1-drummer: a song for the song screen (sng.c is not in this test): INTRO, VERSE (2 bars of 7/8), CHORUS;
 * slots 1 and 3 hold songs */
static void sng_name(uint32_t k, char *out) { static const char *const N[3] = {"INTRO", "VERSE", "CHORUS"}; str_cpy(out, k < 3u ? N[k] : "", 13); }
static uint32_t sng_ready(void) { return sng_cur >= 0 ? 7u : 0u; }
static uint32_t sng_bars(uint32_t k) { return k == 1u ? 2u : 1u; }
static uint32_t sng_meter(uint32_t k) { return k == 1u ? 7u | 1u << 5 : 0u; }
static uint32_t sng_outro(void) { return 0xFFu; }
static int sng_pending(void) { return 0; }
static void sng_apply(uint32_t k) { sng_name(k, sng_ui_name); }
static uint32_t sec_saves;
static int sng_save_section(void) { if (song.playing) return 1; sec_saves++; return 0; }   /* (project.c's packs and writes) */
static int sng_lossy(void) { return 0; }
static void sng_lib_refresh(void)                  /* (sng.c's reads the flash: here slot 1 TIME, factory, slot 3 BLACK) */
{
    if (!sng_lib_dirty) return;
    sng_lib_dirty = 0; memset(sng_lib, 0, sizeof sng_lib);
    str_cpy(sng_lib[0].name, "TIME", 13); sng_lib[0].bpm = 123; sng_lib[0].nsec = 3; sng_lib[0].factory = 1;
    str_cpy(sng_lib[2].name, "BLACK", 13); sng_lib[2].bpm = 86; sng_lib[2].nsec = 5;
    str_cpy(sng_lib[12].name, "THIRTEEN", 13); sng_lib[12].bpm = 140; sng_lib[12].nsec = 4;   /* (16 slots since 2026-10-14) */
}
#include "../firmware/src/ui_song.c"
#include "../firmware/src/ui_studio.c"
#include "../firmware/src/ui_drummer.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_layers.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/splash.c"
static const char *outdir;
static void ppm(const char *name) {
    char path[512]; snprintf(path,sizeof path,"%s/%s.ppm",outdir,name);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n240 240\n255\n");
    for(unsigned i=0;i<240*240;i++) { uint16_t p=swap16(screen[i]); uint8_t rgb[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31}; fwrite(rgb,1,3,f); }
    fclose(f);
}
/* one UI frame (~16 ms): the audio between (as the ISR does), then input, LEDs, draw */
static void frame(void)
{
    uint32_t q;
    static int32_t o[CTL * 2];
    for (q = 0; q < 22u; q++) {
        uint32_t i;
        mix_block(o, CTL);
    }
    ui_input(); ui_leds(); ui_draw(); fm1_ms += 16;
}
static void frames(uint32_t n) { while (n--) frame(); }
static uint32_t BT(uint32_t b) { return 1u << panel.btn[b]; }
static void press(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void release(uint32_t b) { fm1_in.buttons &= ~BT(b); frame(); }
static void tap(uint32_t b) { press(b); release(b); }
static void plain_page(void) { ui.page = (uint8_t)page_first(FAM_FX); ui.home = 0; page_entered(); frame(); }   /* a page whose keys
                                                   * play the drums (FILTER; the FX button's page memory untouched) */
static void key(uint32_t k) { fm1_in.notes |= 1u << k; frame(); fm1_in.notes &= ~(1u << k); frame(); }
static int fails;
static void check(int ok, const char *what) { printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
static const char *sub_line(void) { return layer_sub_shown; }   /* the layer's sub line as last drawn (ui_layers.c) */

#ifndef UI_PAGES_HARNESS_ONLY                       /* (tests/stress_test.c: the harness, its own main) */
int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    logo_splash(); ppm("page-splash");
    ui.menu = 2; ui.force = 1; frame(); ppm("page-about"); ui.menu = 0;
    go_home(); ui.force = 1; frame(); ppm("live-boot");
    check(drummer_on() && sng_view == DV_LIVE && kb_grid == 2u, "boot (HOME): the LIVE screen");
    {   /* fm1-drummer: a song's section playing, the pedal asking for the next (said in the footer) */
        sng_cur = 0; str_cpy(sng_ui_song, "TIME", sizeof sng_ui_song);
        str_cpy(sng_ui_name, "VERSE", sizeof sng_ui_name);
        live_sec = 1; live_bar = 2; song.playing = 1;
        ui.force = 1; frame(); ppm("live-song");
        pedal_say(PU_NEXT, "SOLO"); str_cpy(sng_ui_next, "SOLO", sizeof sng_ui_next);
        frame(); ppm("live-next");
        check(ui.msg_t > 40u && !strcmp(ui.msg, "NEXT: SOLO"), "drummer: the pedal's request said (NEXT: SOLO)");
        ui.msg_t = 0; sng_ui_next[0] = 0; song.playing = 0;
        sng_cur = -1; sng_ui_song[0] = 0; live_sec = -1; sng_ui_name[0] = 0; ui.force = 1; frame();
    }
    open_family(FAM_ENV); ui.force = 1; ui.hot_col = 1; ui.hot_t = 30; frame(); ppm("page-env");
    open_family(FAM_EDIT); ui.force = 1; frame(); ppm("page-edit");
    open_family(FAM_FX); ui.force = 1; frame(); ppm("page-fx");
    open_family(FAM_SEQ); ui.force = 1; frame(); ppm("page-step");
    open_family(FAM_SCL); ui.force = 1; frame(); ppm("page-scale");
    {   /* fm1-drummer: SETUP (GLO tapped): PEDAL first; MIDI: OUT and IN (2.4, 2.6), kept in the settings word */
        uint32_t m0 = (uint32_t)song.g[G_MIDI];
        go_home(); frames(16);
        tap(B_GLO); frame();
        check(ui.menu == 1 && mi_sec(ui.menu_sel) == MS_PEDAL, "GLO tapped: SETUP, on PEDAL");
        pedal.ch = 16; pedal.count = 0; arrset_dirty = 0;
        encs[panel.enc[EN_K1]] = -3; frame();
        check(pedal.ch == 13 && arrset_dirty, "SETUP > PEDAL: KNOB 1 the pedal channel (saved with the songs)");
        encs[panel.enc[EN_K1]] = 10; frame();
        check(pedal.ch == PEDAL_CH_OMNI, "SETUP > PEDAL: past 16: ANY");
        encs[panel.enc[EN_K2]] = 1; frame();
        check(pedal.count == 1u, "SETUP > PEDAL: KNOB 2 the count-in (1 bar)");
        encs[panel.enc[EN_K3]] = 1; frame();
        check(sng_autoload == 1u, "SETUP > PEDAL: KNOB 3 loads the last song at start");
        ui.force = 1; frame(); ppm("setup-pedal");
        pedal.ch = 16; pedal.count = 0; sng_autoload = 0; arrset_dirty = 0;
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(mi_sec(ui.menu_sel) == MS_MIDI, "SETUP: SELECT right: MIDI");
        song.g[G_MIDI] = 0;
        encs[panel.enc[EN_K4]] = 1; frame();
        check(song.g[G_MIDI] == 1 && !strcmp(mi_value(ui.menu_sel, &(uint16_t){0}), "SEQ"), "SETUP > MIDI: KNOB 4 sets OUT = SEQ");
        {   /* (2.4) KNOB 2: IN = CLOCK (notes ignored), kept in the settings word; back to NOTES */
            int16_t r0 = song.g[G_ROUTE];
            encs[panel.enc[EN_K2]] = 1; frame();
            check(song.g[G_ROUTE] == 1 && !strcmp(mi_value(ui.menu_sel, &(uint16_t){0}), "CLOCK"), "SETUP > MIDI: KNOB 2 sets IN = CLOCK");
            ui.force = 1; frame(); ppm("setup-midi");
            lights_min = 1;
            check((lights_word() >> 15 & 1u) == 1u, "IN = CLOCK kept in the settings word (bit 15)");
            lights_from_word(lights_word() & ~(1u << 15));
            check(lights_min == 0u, "IN read back from the settings word");
            encs[panel.enc[EN_K2]] = 12; frame();             /* (2.6) on to CH 12 */
            lights_min = (uint8_t)song.g[G_ROUTE];
            {
                uint32_t w = lights_word();
                lights_min = 0;
                lights_from_word(w);
                check(song.g[G_ROUTE] == 13 && !strcmp(mi_value(ui.menu_sel, &(uint16_t){0}), "CH 12") && lights_min == 13u &&
                      !(w >> 15 & 1u) && (w >> 21 & 31u) == 12u, "2.6: IN = CH 12 (CH 1..16 after CLOCK), kept in the settings word (bits 21-25), read back");
                lights_from_word(w & ~(31u << 21));
                check(lights_min == 0u, "2.6: a settings word from 2.5 (no channel bits): IN = NOTES");
            }
            song.g[G_ROUTE] = r0;
        }
        encs[panel.enc[EN_SELECT]] = 1; frame(); encs[panel.enc[EN_SELECT]] = 1; frame();
        {
            int16_t bpm = song.g[G_BPM];
            check(mi_sec(ui.menu_sel) == MS_TEMPO, "SETUP: SELECT on: TEMPO");
            encs[panel.enc[EN_K1]] = 3; frame();
            check(song.g[G_BPM] == bpm + 3, "SETUP > TEMPO: KNOB 1 the tempo");
            ui.force = 1; frame(); ppm("setup-tempo");
            song.g[G_BPM] = bpm;
        }
        tap(B_GLO); frame();
        check(ui.menu == 0 && drummer_on(), "GLO again: SETUP closed, LIVE");
        tap(B_GLO); tap(B_HOME); frame();
        check(ui.menu == 0 && drummer_on(), "HOME: SETUP closed, LIVE");
        press(B_HOME); frames(50); release(B_HOME); frames(2);
        check(ui.menu == 0, "HOME held: no menu (SETUP is GLO's)");
        song.g[G_MIDI] = (int16_t)m0; go_home(); frame();
    }

    /* ---- taps open pages, holds are layers */
    go_home(); ui.force = 1; frame();
    tap(B_SEQ); check(on_drum_page() && cur_btn() == B_SEQ, "SEQ tapped (the drum track): the DRUMS screen");
    { uint8_t dp = drum_page; tap(B_SEQ); check(on_drum_page() && drum_page != dp, "SEQ tapped again: GRID <-> KIT"); tap(B_SEQ); }
    go_home(); frame();
    tap(B_FX); check(cur_fam() == FAM_FX, "FX tapped: the FX pages");
    go_home(); frame();
    press(B_FX); frames(12); check(ui.layer == LY_FX, "FX held: the FX layer shows");
    check(keys_guide() == (1u << key_of_white(0) | 1u << key_of_white(4) | 1u << key_of_white(8) | 1u << key_of_white(12)),
          "FX held: keys 1, 5, 9, 13 lit dim (the rows of the grid)");
    ppm("layer-fx");
    fm1_in.notes = 1u << 4; frame(); check(keys_lit() == 0u, "FX + a key: nothing lit (the punch-ins are gone)");
    fm1_in.notes = 0; frame();
    encs[panel.enc[EN_K2]] = 10; frame(); check(song.g[G_DUST] > 0, "FX + KNOB 2: DUST");
    encs[panel.enc[EN_K1]] = -10; frame(); check(song.g[G_FILT] < 0, "FX + KNOB 1: the filter (low-pass)");
    encs[panel.enc[EN_K4]] = 12; frame(); check(TDRUM->p[P_TFLT] > 0 && song.g[G_FILT] < 0, "FX + KNOB 4: the selected track's FILTER (high-pass)");
    ui.force = 1; frame(); ppm("layer-fx-trkflt");
    TDRUM->p[P_TFLT] = 0;
    release(B_FX); check(drummer_on() && ui.layer == LY_PLAY, "FX used then let go: no FX page (LIVE still)");
    song.g[G_DUST] = 0; song.g[G_FILT] = 0;

    /* ---- a layer locked open: held + HOME tapped; any other button (not PLAY, REC, OCT) lets it go */
    go_home(); frame();
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    check(ly_lock == LY_FX && ui.layer == LY_FX, "FX held + HOME: locked open, FX let go");
    check(drummer_on(), "FX + HOME: no FX page, no HOME jump");
    encs[panel.enc[EN_K2]] = 6; frame(); check(song.g[G_DUST] > 0, "locked FX + KNOB 2: DUST");
    ui.force = 1; frame(); ppm("layer-locked");
    { uint8_t was = song.playing; tap(B_PLAY); frames(2);
      check(ly_lock == LY_FX && song.playing != was, "locked: PLAY plays and keeps the lock"); tap(B_PLAY); frames(2); }
    tap(B_ENV); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && drummer_on(),
          "locked, ENV pressed: unlocked, and only that (no ENV page)");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    tap(B_HOME); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && drummer_on(), "locked, HOME tapped: unlocked");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    tap(B_FX); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && cur_fam() != FAM_FX, "locked, FX tapped: unlocked, no FX page");
    go_home(); frame();
    press(B_SEQ); frames(3); tap(B_HOME); release(B_SEQ); frames(3);
    press(B_FX); frames(12);
    check(ly_lock == LY_PLAY && ui.layer == LY_FX, "locked SEQ, FX held: unlocked, the FX layer");
    release(B_FX); frames(2); check(ui.layer == LY_PLAY, "and FX let go: back to playing");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    tap(B_GLO); frames(2);
    check(!ui.menu && ly_lock == LY_PLAY, "locked, GLO tapped: unlocked, and only that (no SETUP)");
    ui.force = 1; frames(2);
    song.g[G_DUST] = 0; song.g[G_FILT] = 0;

    /* ---- SEQ layer: drum steps on the white keys */
    plain_page();                                    /* (LIVE's keys are the pedal's: a page whose keys play) */
    key(4);                                          /* A3: the snare, played: the layer's sound */
    check(pen_lane == 2, "a drum key played: the SEQ layer's sound (snare)");
    press(B_SEQ); frames(10);
    key(0); key(7); key(14);                          /* steps 1, 5, 9 */
    check(dstep_has(&TDRUM->dstep[0], 2) && dstep_has(&TDRUM->dstep[4], 2) && dstep_has(&TDRUM->dstep[8], 2) &&
          !dstep_has(&TDRUM->dstep[1], 2), "SEQ + white keys 1, 5, 9: snare steps");
    ppm("layer-steps");
    fm1_in.notes = 1u << 7; frame();                  /* step 5 held + KNOB 2 / 3: level, ratchet */
    encs[panel.enc[EN_K2]] = 1; frame();
    encs[panel.enc[EN_K3]] = 2; frame();
    fm1_in.notes = 0; frame();
    check(dstep_lvl(&TDRUM->dstep[4], 2) == LV_HARD && dstep_rat(&TDRUM->dstep[4], 2) == 2u,
          "step 5 held + KNOB 2 / 3: hard, x3");
    key(0); check(!dstep_has(&TDRUM->dstep[0], 2), "step 1 again: off");
    release(B_SEQ); check(ui.layer == LY_PLAY && !on_drum_page(), "SEQ used then let go: no page change");

    /* ---- EDIT: undo / redo, erase, length */
    press(B_EDIT); frames(10);
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(!dstep_has(&TDRUM->dstep[4], 2) && !dstep_has(&TDRUM->dstep[8], 2), "EDIT + OCT-: undo (the SEQ hold's steps gone)");
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    check(!dstep_has(&TDRUM->dstep[0], 2) && dstep_lvl(&TDRUM->dstep[4], 2) == LV_HARD, "EDIT + OCT+: redo (back, as left)");
    ppm("layer-erase");
    key(4);                                           /* stopped: every snare goes */
    check(!dstep_has(&TDRUM->dstep[4], 2) && !dstep_has(&TDRUM->dstep[8], 2), "EDIT + snare (stopped): every snare erased");
    encs[panel.enc[EN_K2]] = 1; frame();
    check(TDRUM->p[P_SLEN] == 32, "EDIT + KNOB 2: length x2");
    release(B_EDIT);
    edges_btn |= BT(B_EDIT); fm1_in.buttons |= BT(B_EDIT); frame();      /* undo the length too */
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    release(B_EDIT);
    check(TDRUM->p[P_SLEN] == 16, "EDIT + OCT-: the length back to 16");

    /* ---- ARP layer: a roll, rate knob */
    press(B_ARP); frames(10);
    encs[panel.enc[EN_K1]] = 1; frame();
    check(song.g[G_ROLL] == 2, "ARP + KNOB 1: the roll rate (1/32)");
    fm1_in.notes = 1u << 7; frames(3); check(roll[0].on, "ARP + a key: it rolls");
    ppm("layer-roll");
    fm1_in.notes = 0; frame(); check(!roll[0].on, "key up: the roll ends");
    release(B_ARP);

    /* ---- SEQ layer (2.4), a drum step: held + KNOB 4 nudges it, PRESETS locks the lock parameter (ALGORITHM picks it),
     * OCT+ its fill condition, OCT- clears nudge, locks and condition; PRESETS / ALGORITHM browse nothing meanwhile */
    check(ui.lock_par == P_ED_FLT, "the lock parameter starts as ENV > FLT");
    ui.lock_par = P_SGATE;
    plain_page(); frames(16);    /* (KNOB 1..4 quiet ~250 ms after a layer: #39) */
    press(B_SEQ); frames(10);
    key(7);                                           /* step 5 set with the pen */
    fm1_in.notes = 1u << 7; frame();                  /* step 5 held */
    {
        uint32_t ln = pen_lane;
        encs[panel.enc[EN_K4]] = -3; frame();
        encs[panel.enc[EN_K4]] = -4; frame();
        check(TDRUM->micro[4] == -7 && dstep_has(&TDRUM->dstep[4], ln), "step 5 held + KNOB 4: nudged -7 (the step kept)");
        encs[panel.enc[EN_PRESET]] = 1; frame();
        check(lock_find(TDRUM, 4, P_SGATE, 0) >= 0 && TDRUM->p[P_E0] == DRUM_DEFAULT_KIT, "step 5 held + PRESETS: a GATE lock, the kit untouched");
        ui.force = 1; frame(); ppm("layer-steps-lock");
        check(step_locked(TDRUM, 4) && !step_locked(TDRUM, 5), "the step shows its mark (a nudge or a lock)");
        for (i = 0; i < 80u; i++) { encs[panel.enc[EN_ALGO]] = 1; frame(); if (ui.lock_par == P_SGATE) break; }
        check(i > 0 && i < 80u && ui.lock_par == P_SGATE, "ALGORITHM wraps round through the lockable parameters");
        encs[panel.enc[EN_K4]] = -40; frame();
        check(TDRUM->micro[4] == MICRO_MIN, "KNOB 4 bounded at -32");
        edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
        check(TDRUM->micro[4] == 0 && lock_find(TDRUM, 4, P_SGATE, 0) < 0 && dstep_has(&TDRUM->dstep[4], ln) && ui.step_page == 0,
              "step held + OCT-: its nudge and locks cleared, the step kept, no page change");
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        check(step_fill(TDRUM, 4) == FC_FILL && ui.step_page == 0 && dstep_has(&TDRUM->dstep[4], ln), "step held + OCT+: FILL ONLY (no page change, the step kept)");
        ui.force = 1; frame(); ppm("layer-steps-fill");
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        check(step_fill(TDRUM, 4) == FC_NOFILL, "OCT+ again: NO FILL");
        ui.force = 1; frame(); ppm("layer-steps-nofill");
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        check(step_fill(TDRUM, 4) == FC_NORM, "OCT+ again: normal");
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        encs[panel.enc[EN_K4]] = 3; frame();
        edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
        check(step_fill(TDRUM, 4) == FC_NORM && TDRUM->micro[4] == 0, "FILL ONLY + a nudge, OCT-: the condition and the nudge reset");
        fm1_in.notes = 0; frame();
        check(dstep_has(&TDRUM->dstep[4], ln), "the step edited is kept when let go");
        fm1_in.notes = 1u << 7; frame(); encs[panel.enc[EN_PRESET]] = 1; frame();
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        fm1_in.notes = 0; frame();
        check(lock_find(TDRUM, 4, P_SGATE, 0) >= 0 && step_fill(TDRUM, 4) == FC_FILL, "a set step held + PRESETS, OCT+: a lock, FILL ONLY");
        key(7);                                       /* a set step pressed and let go: cleared, with its locks and condition */
        check(!dstep_mask(&TDRUM->dstep[4]) && lock_find(TDRUM, 4, P_SGATE, 0) < 0 && step_fill(TDRUM, 4) == FC_NORM,
              "a step cleared: its locks and fill condition go too");
    }
    { encs[panel.enc[EN_ALGO]] = 1; frame(); check(ui.lock_par == P_SGATE, "no step held, SEQ held: ALGORITHM does nothing"); }
    release(B_SEQ);
    plain_page();
    press(B_SEQ); frames(10);
    key(0);                                           /* a drum step: KNOB 1 the sound, KNOB 4 nudges the whole step */
    fm1_in.notes = 1u << 0; frame();
    { uint32_t l0 = pen_lane; encs[panel.enc[EN_K1]] = 1; frame(); check(pen_lane == l0 + 1u, "the drum track: step 1 held + KNOB 1: the sound (as in 2.3)"); }
    encs[panel.enc[EN_K4]] = 5; frame();
    encs[panel.enc[EN_PRESET]] = 1; frame();
    check(TDRUM->micro[0] == 5 && lock_find(TDRUM, 0, P_SGATE, 0) >= 0 && TDRUM->p[P_E0] == DRUM_DEFAULT_KIT,
          "the drum track: step 1 held + KNOB 4 / PRESETS: nudge +5, a lock, the kit untouched");
    fm1_in.notes = 0; frame();
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(TDRUM->micro[0] == 5 && ui.step_page == 0, "no step held + OCT-: the page (not a clear)");
    release(B_SEQ);
    fm1_irq_off(); steps_clear(TDRUM); fm1_irq_on();
    ui.lock_par = P_ED_FLT;
    go_home(); frame();

    /* ---- REC: held: the ring; to the end: cleared (a tap does nothing: live recording is gone) */
    plain_page();
    song.playing = 0;
    tap(B_REC); check(!song.playing && ui.hold_kind == 0u, "REC tapped: nothing");
    dstep_set(&TDRUM->dstep[3], 2, LV_NORM, 0);
    press(B_REC); frames(50);                         /* 0.8 s: the ring */
    check(ui.hold_kind == 1u, "REC held: the clear ring");
    ppm("hold-clear");
    frames(90);                                       /* to the end */
    check(!dstep_mask(&TDRUM->dstep[3]) && ui.hold_kind == 0, "REC held to the end: the pattern cleared");
    release(B_REC);
    press(B_EDIT); frames(10);
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    release(B_EDIT);
    check(dstep_has(&TDRUM->dstep[3], 2), "EDIT + OCT-: the cleared pattern back");
    press(B_REC); frames(60); release(B_REC);         /* let go before the end: nothing */
    check(dstep_has(&TDRUM->dstep[3], 2), "REC let go before the end: nothing cleared");
    TDRUM->dstep[3] = (dstep_t){0};

    /* ---- SAVE: tap = the song page; held = the SONG layer (live sections, SONG REC) */
    go_home(); frame();
    song.playing = 0; live_sec = -1; live_req = -1; srec = 0; arrangement_enabled = 0;
    press(B_SAVE); frames(15); check(ui.layer == LY_SONG, "SAVE held: the song layer");
    key(key_of_white(4)); check(sec_stores == 0 && sec_armed == 1u, "store over a used A: asks again");
    key(key_of_white(4)); check(sec_stores == 1 && live_sec == 0, "again: the loop stored in A");
    key(key_of_white(6)); check(sec_stores == 2 && live_sec == 2, "an empty C: stored at once");
    key(key_of_white(1)); check(sec_loads == 1 && live_sec == 1, "stopped: B loaded as the loop");
    key(key_of_white(3)); check(sec_loads == 1, "an empty D: not played");
    song.playing = 1; clk_beat = 1; clk_pos = 0;
    key(key_of_white(0)); check(live_req == 0, "playing: A asked for the next bar");
    release(B_SAVE); frames(2);
    check(chain_n == 0u && chain_taps == 0u && live_req == 0, "one tap, SAVE let go: no chain (as before)");
    live_req = -1;
    press(B_SAVE); frames(15);                        /* (2.4) SAVE held: A, B, B tapped: a chain on release */
    key(key_of_white(0)); key(key_of_white(1));
    check(live_req == 0 && chain_taps == 2u && chain_n == 0u, "SAVE held, A then B tapped: A asked for, the chain building");
    ui.force = 1; frame();
    check(str_eq(sub_line(), "chain A B"), "the sub line reads \"chain A B\"");
    key(key_of_white(1));
    ui.force = 1; frame(); ppm("layer-song-chain");
    check(chain_taps == 3u && str_eq(sub_line(), "chain A B B"), "B again: \"chain A B B\"");
    release(B_SAVE); frames(2);
    check(chain_n == 3u && chain_i == 0u && chain_sec[0] == 0 && chain_sec[1] == 1 && chain_sec[2] == 1 && chain_bars == 1u && live_req == 0,
          "SAVE let go: the chain A B B plays (A asked for, 1 bar)");
    press(B_SAVE); frames(15);
    check(str_eq(sub_line(), "chain A B B"), "SAVE held again: the chain shown");
    key(key_of_white(1)); release(B_SAVE); frames(2);
    check(chain_n == 0u && live_req == 1, "a single tap of B: the chain ends, B asked for");
    press(B_SAVE); frames(15);
    live_req = -1; song.playing = 0;
    key(key_of_white(13)); check(srec == 1u && !arrangement_enabled, "SONG REC armed (loop mode)");
    ui.force = 1; frame(); ppm("layer-song");
    key(key_of_white(13)); check(srec == 0u, "SONG REC again: off");
    key(key_of_white(12)); check(arrangement_enabled == 1u, "loop / song: song mode");
    key(key_of_white(12)); check(arrangement_enabled == 0u, "again: loop mode");
    release(B_SAVE);
    check(drummer_on() && sng_view == DV_LIVE && saves == 0, "SAVE held and let go: LIVE still (no SONGS), no save");
    tap(B_SAVE); check(drummer_on() && sng_view == DV_LIB, "SAVE tapped on LIVE: SONGS");
    {   /* fm1-drummer: SONGS lists the slots (cached), KNOB 1 picks, REC loads and goes back to LIVE; the last row
         * (JAM) is SLOOP's chain on the page; HOME: LIVE again; there, with a song, the keys are the pedal's */
        sng_lib_dirty = 1;
        ui.force = 1; frames(16); ppm("page-songs");       /* (after SAVE: the knobs quiet, #39) */
        check(sng_lib[0].factory && !strcmp(sng_lib[2].name, "BLACK") && !sng_lib_dirty, "SONGS: the slots read once (TIME factory, BLACK)");
        check(kb_grid == 2u, "SONGS: the keys are the screen's");
        encs[panel.enc[EN_K1]] = 2; frame(); tap(B_REC); frame();
        check(sng_req == 3u && drummer_on() && sng_view == DV_LIVE, "SONGS: KNOB 1 to slot 3, REC: song 3 asked for, LIVE");
        sng_req = 0;
        tap(B_SAVE); encs[panel.enc[EN_K1]] = 15; frame(); ui.force = 1; frame(); ppm("page-songs-16");
        check(sng_pick == 15u, "SONGS: the 16th slot reached (the list scrolls)");
        tap(B_REC); frame();
        check(sng_req == 16u, "SONGS: REC on slot 16: song 16 asked for");
        sng_req = 0;
        tap(B_SAVE); encs[panel.enc[EN_K1]] = 20; frame(); tap(B_REC); frame();
        check(!drummer_on() && on_song_page() && sng_req == 0xFFu && kb_grid == 0u, "SONGS: the last row (JAM), REC: SLOOP's chain on the page");
        sng_req = 0;
        ui.force = 1; frame(); ppm("page-song");
        tap(B_HOME); frame();
        check(drummer_on() && sng_view == DV_LIVE, "HOME: LIVE");
        ui.force = 1; frame(); ppm("live-nosong");
        key(key_of_white(2)); check(sng_req == 3u, "LIVE, no song: white key 3 loads song 3");
        sng_req = 0;
        key(key_of_white(12)); check(sng_req == 13u, "LIVE, no song: white key 13 loads song 13 (16 songs)");
        sng_req = 0;
        tap(B_SAVE); key(key_of_white(0)); check(sng_req == 1u && sng_view == DV_LIVE, "SONGS: white key 1 loads song 1, LIVE");
        sng_req = 0;
        /* a song loaded (as sng_load would): the keys are the pedal's */
        sng_cur = 0; str_cpy(sng_ui_song, "TIME", sizeof sng_ui_song); live_sec = 0; live_req = -1; pedal.count = 0;
        sng_name(0, sng_ui_name);
        song.playing = 0; ui.force = 1; frame(); ppm("live-cued");
        key(key_of_white(1)); frames(3);
        check(song.playing && live_sec == 1 && !strcmp(sng_ui_name, "VERSE"), "LIVE: white key 2, stopped: VERSE starts the loop");
        pedal_fill = 0; pedal_fill_hold = 0;
        key(key_of_white(2)); frames(2);
        check(live_req == 2 && pedal_at_end && !pedal_fill && !pedal_fill_hold, "LIVE: white key 3, playing: CHORUS cued for VERSE's end, no fill");
        str_cpy(sng_ui_next, "CHORUS", sizeof sng_ui_next);
        ui.force = 1; frame(); ppm("live-playing");
        check((keys_lit() >> key_of_white(1)) & 1u, "LIVE: the section playing's key lit");
        key(key_of_white(9)); frames(2);
        check(live_req == 1 && !pedal_at_end && !pedal_fill, "LIVE: white key 10: VERSE on the next bar, no fill");
        live_req = -1;
        key(key_of_white(1)); frames(2);
        check(live_req < 0 && !pedal_fill && !pedal_fill_hold, "LIVE: the section playing's key again: nothing (no fill)");
        sng_ui_next[0] = 0;
        ui.force = 1; frame(); ppm("live-fill");
        {   /* every black key: a fill to the pattern's end (CC 21); then a white key: the fill into the change */
            static const uint8_t BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
            uint32_t b, all = 1;
            for (b = 0; b < 11u; b++) {
                pedal_fill = 0; pedal_fill_hold = 0;
                key(BLACK[b]); frames(2);
                all &= pedal_fill_hold != 0u && pedal_fill && song.playing;
            }
            check(all, "LIVE: every black key: the fill to the pattern's end (CC 21), playing on");
            key(key_of_white(2)); frames(2);
            check(live_req == 2 && pedal_at_end && pedal_fill_hold != 0u, "LIVE: a black key, then white key 3: the fill goes on, CHORUS at the end");
            ui.force = 1; frame(); ppm("live-fill-next");     /* "FILL > CHORUS" */
            live_req = -1; pedal_at_end = 0; pedal_fill = 0; pedal_fill_hold = 0;
        }
        tap(B_PLAY); frames(3);
        check(!song.playing, "LIVE: PLAY: stop");
        tap(B_PLAY); frames(3);
        check(song.playing && live_sec >= 0, "LIVE: PLAY: the section cued plays");
        {   /* SEL / ENV / LFO: FILL, PREV, NEXT (CC 21, 25, 24) */
            pedal_fill_hold = 0; pedal_fill = 0; live_req = -1; pedal_at_end = 0;
            tap(B_SCL); frames(2);
            check(pedal_fill_hold != 0u, "LIVE: SEL: the fill (CC 21)");
            pedal_fill_hold = 0; pedal_fill = 0; live_req = -1; pedal_at_end = 0;
            tap(B_LFO); frames(2);
            check(live_req == (int8_t)(live_sec + 1) && !pedal_fill, "LIVE: LFO: the next section (as CC 24, no fill)");
            live_req = -1; pedal_at_end = 0;
            tap(B_ENV); frames(2);
            check(live_req == (int8_t)(live_sec - 1) && !pedal_fill, "LIVE: ENV: the previous section (as CC 25, no fill)");
            live_req = -1; pedal_at_end = 0; pedal_fill = 0;
        }
        {   /* the knobs: tempo, kit, drums level, swing */
            int16_t bpm = song.g[G_BPM], kit = TDRUM->p[P_E0], lv = song.g[G_DRLVL], sw = song.g[G_SWING];
            frames(16);
            encs[panel.enc[EN_K1]] = 2; frame(); check(song.g[G_BPM] == bpm + 2, "LIVE: KNOB 1: the tempo");
            encs[panel.enc[EN_K2]] = 1; frame(); check(TDRUM->p[P_E0] != kit && !strncmp(ui.msg, "KIT ", 4), "LIVE: KNOB 2: the kit (said)");
            encs[panel.enc[EN_K3]] = -3; frame(); check(song.g[G_DRLVL] == lv - 3, "LIVE: KNOB 3: the drums' level");
            encs[panel.enc[EN_K4]] = 4; frame(); check(song.g[G_SWING] == sw + 4, "LIVE: KNOB 4: the swing");
            ui.force = 1; frame(); ppm("live-knob");
            song.g[G_BPM] = bpm; TDRUM->p[P_E0] = kit; song.g[G_DRLVL] = lv; song.g[G_SWING] = sw; ui.msg_t = 0;
        }
        {   /* the count-in, the pedal's MIDI dot */
            tap(B_PLAY); frames(3);
            ci_on = 1; ci_pedal = 8; ci_beat = 2; ci_meter = 0; pedal_seen_ms = fm1_ms;
            ui.force = 1; frame(); ppm("live-countin");
            ci_on = 0; ci_pedal = 0;
        }
        check(!song.playing, "LIVE: PLAY again: stop");
        sng_cur = -1; sng_ui_song[0] = 0; live_sec = -1; live_req = -1; sng_ui_name[0] = 0;
        studio_open(SC_SONG); sng_view = DV_JAM; frame();  /* (SLOOP's page for what follows) */
    }

    /* ---- the drum screen */
    studio_open(SC_DRUM); ui.force = 1; frames(16);   /* (after SAVE: the knobs quiet, #39) */
    check(on_drum_page(), "the drum screen");
    {   /* fm1-drummer: SAVE on the grid (EDIT): tapped, SONGS; held to the end, the section into its song (stopped) */
        uint32_t s0 = sec_saves, p0 = saves;
        sng_cur = 1; live_sec = 1; str_cpy(sng_ui_name, "VERSE", sizeof sng_ui_name); song.playing = 0;
        tap(B_SAVE); frames(2);
        check(drummer_on() && sng_view == DV_LIB && ui.layer == LY_PLAY, "EDIT: SAVE tapped: SONGS (no SONG layer on the grid)");
        studio_open(SC_DRUM); frames(16);
        press(B_SAVE); frames(50);
        check(ui.hold_kind == 2u && on_drum_page(), "EDIT: SAVE held 0.8 s: the save ring");
        ui.force = 1; frame(); ppm("hold-save-section");
        frames(70);
        check(ui.hold_kind == 0u && sec_saves == s0 + 1u && !strcmp(ui.msg, "SAVED INTO THE SONG"), "EDIT: SAVE held to the end: the section into its song");
        release(B_SAVE); frames(2);
        check(on_drum_page() && sec_saves == s0 + 1u, "EDIT: SAVE let go after: nothing more (no SONGS)");
        press(B_SAVE); frames(60); release(B_SAVE); frames(2);
        check(sec_saves == s0 + 1u && on_drum_page() && !ui.hold_kind, "EDIT: SAVE let go before the end: nothing saved");
        song.playing = 1; press(B_SAVE); frames(120); release(B_SAVE); frames(2);
        check(!strcmp(ui.msg, "STOP TO SAVE") && sec_saves == s0 + 1u, "EDIT, playing: STOP TO SAVE");
        song.playing = 0; sng_cur = -1; live_sec = -1; sng_ui_name[0] = 0;
        press(B_SAVE); frames(120); release(B_SAVE); frames(2);
        check(saves == p0 + 1u, "EDIT, no song (JAM): SAVE held saves the project, as SLOOP did");
        studio_open(SC_DRUM); ui.msg_t = 0; frames(16);
    }
    encs[panel.enc[EN_K3]] = 1; frame(); check(dstep_has(&TDRUM->dstep[0], 0), "DRUMS: KNOB 3 adds the kick on step 1");
    encs[panel.enc[EN_K4]] = -1; frame(); check(dstep_lvl(&TDRUM->dstep[0], 0) == LV_SOFT, "DRUMS: KNOB 4 softer");
    encs[panel.enc[EN_K1]] = 100; encs[panel.enc[EN_K2]] = 100; frame();
    check(drum_lane == 15 && drum_cursor == 15, "DRUMS: KNOB 1 / 2 bounded (16 sounds, 16 steps)");
    {   /* a scene to look at */
        static const uint8_t BEAT[16] = {0x11, 0x10, 0x10, 0x10, 0x14, 0x10, 0x10, 0x21, 0x11, 0x10, 0x01, 0x10, 0x1C, 0x10, 0x10, 0x20};
        uint32_t j;
        for (j = 0; j < 16u; j++) {
            memset(&TDRUM->dstep[j], 0, sizeof(dstep_t));
            if (BEAT[j] & 1u) dstep_set(&TDRUM->dstep[j], 0, LV_HARD, 0);
            if (BEAT[j] & 4u) dstep_set(&TDRUM->dstep[j], 2, LV_NORM, 0);
            if (BEAT[j] & 8u) dstep_set(&TDRUM->dstep[j], 3, LV_GHOST, 0);
            if (BEAT[j] & 0x10u) dstep_set(&TDRUM->dstep[j], 4, j % 4u ? LV_SOFT : LV_NORM, j == 14u ? 2u : 0u);
            if (BEAT[j] & 0x20u) dstep_set(&TDRUM->dstep[j], 5, LV_NORM, 0);
        }
        drum_lane = 4; drum_cursor = 6; drum_page = 0; ui.force = 1; ui.msg_t = 0;
        drums.hits = 1u | 1u << 4; frame(); ppm("live-grid");
        drum_page = 1; ui.force = 1; drums.hits = 1u | 1u << 4; frame(); ppm("live-kit");
        drum_page = 0; go_home(); ui.force = 1;
        transport_req = 1; frames(30); ui.force = 1; frame(); ppm("live-tracks");
        transport_req = 2; frames(2);
        
    }
    for (i = 0; i < DRUM_KITS; i++) { TDRUM->p[P_E0] = (int16_t)i; ui.force = 1; drum_page = 1; frame(); }
    drum_page = 0;
    go_home(); frame();
    TDRUM->p[P_E0] = (int16_t)(DRUM_USR - 1u);
    encs[panel.enc[EN_PRESET]] = 1; frame();
    {
        int16_t up = TDRUM->p[P_E0];
        encs[panel.enc[EN_PRESET]] = -1; frame();
        check(up == (int16_t)(DRUM_USR + 2u) && TDRUM->p[P_E0] == (int16_t)(DRUM_USR - 1u),
              "PRESETS: the kits step over USR1 and USR2 (the songs' flash), both ways; the numbers stay");
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;

    {   /* menu NOTES (PR #11 by @renebohne): the setting (it lit the synths' sounding notes; drums only: no synth) */
        go_home(); ui.force = 1; frame();
        lights_notes = 0;
        ui.menu = 1; ui.menu_sel = (uint8_t)mi_find(MI_NOTES); ui.force = 1; frame();
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_notes == 1u, "menu NOTES: its knob (KNOB 3) right: ON");
        ui.force = 1; frame(); ppm("page-menu-notes");
        ui.menu = 0; ui.force = 1; go_home(); frame();
        check((lights_word() >> 8 & 1u) == 1u, "NOTES: saved with the light settings");
        ui.menu = 1; ui.menu_sel = (uint8_t)mi_find(MI_USB); ui.force = 1; frame(); ppm("menu-usb");
        usb_full = 0;
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(usb_full == 1u && (lights_word() >> 11 & 1u) == 1u, "menu USB AUDIO: its knob (KNOB 2) -> FULL, saved with the settings");
        tap(B_OCTUP);
        check(usb_full == 0u && ui.menu == 1, "menu USB AUDIO: OCT+ toggles back to MASTER");
        ui.menu_sel = (uint8_t)mi_find(MI_SERIAL); ui.force = 1; frame();
        check(usb_serial == 0u && !(lights_word() >> 16 & 1u), "menu USB SERIAL: OFF by default (2.3 settings read so)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(usb_serial == 1u && (lights_word() >> 16 & 1u), "menu USB SERIAL: its knob (KNOB 3) -> ON, saved with the settings");
        ui.force = 1; frame(); ppm("menu-serial");
        lights_from_word(lights_word());
        check(usb_serial == 1u, "menu USB SERIAL: read back");
        tap(B_OCTUP);
        check(usb_serial == 0u && ui.menu == 1, "menu USB SERIAL: OCT+ toggles back to OFF");
        ui.menu = 0; ui.force = 1; go_home(); frame();
        ui.menu = 1; ui.menu_sel = (uint8_t)mi_find(MI_NOTES); ui.force = 1; frame();
        tap(B_OCTUP);
        check(lights_notes == 0u && ui.menu == 1, "menu NOTES: OCT+ toggles it off");
        ui.menu = 0; ui.force = 1; go_home(); frame();
    }

    {   /* menu LIGHTS / KEYS: the backlight for playing in the dark */
        uint32_t m, nbits;
        go_home(); ui.force = 1; frame();
        check(lights_lvl == LIGHTS_OFF && fm1_led_bg_ns == 0u && lights_keys_mask() == 0u, "LIGHTS: off by default");
        ui.menu = 1; ui.menu_sel = (uint8_t)mi_find(MI_LIGHTS); ui.force = 1; frame();
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_lvl == LIGHTS_LOW && fm1_led_bg_ns == 500u, "LIGHTS: KNOB 1 right: LOW (a 0.5 us pulse a frame)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_lvl == LIGHTS_MID && fm1_led_bg_ns == 1000u, "LIGHTS: MID (1 us)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame(); encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_lvl == LIGHTS_HIGH && fm1_led_bg_ns == 2000u, "LIGHTS: HIGH (2 us, under the 4 us glow), and it stops there");
        ui.force = 1; frame(); ppm("page-menu-lights");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = -1; frame();
        check(lights_lvl == LIGHTS_MID, "LIGHTS: KNOB 1 left: dimmer");
        tap(B_OCTUP);
        check(lights_lvl == LIGHTS_HIGH && ui.menu == 1, "LIGHTS: OCT+ steps round, the menu stays");
        tap(B_OCTUP);
        check(lights_lvl == LIGHTS_OFF && fm1_led_bg_ns == 0u, "LIGHTS: ... back to OFF");
        ui.menu_sel = (uint8_t)mi_find(MI_KEYS); ui.force = 1; frame();
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        m = lights_keys_mask();
        check(lights_keys == KEYS_C && lights_lvl == LIGHTS_LOW && m == (1u << 7 | 1u << 19),
              "KEYS (KNOB 2): C KEYS (C4, C5), LIGHTS raised to LOW");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        for (m = lights_keys_mask(), nbits = 0; m; m &= m - 1u)
            nbits++;
        check(lights_keys == KEYS_WHITE && nbits == 16u && !(lights_keys_mask() & (1u << 1)),
              "KEYS: WHITE KEYS (16 of 27, F#3 dark)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame(); encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_keys == KEYS_ALL && lights_keys_mask() == 0x7FFFFFFu, "KEYS: ALL KEYS (every one of the 27 keys), and it stops there");
        ui.force = 1; frame(); ppm("page-menu-keys-all");
        tap(B_OCTUP);
        check(lights_keys == KEYS_OFF, "KEYS: OCT+ from ALL KEYS steps round to OFF");
        lights_keys = KEYS_ALL;
        check(lights_word() >> 4 & 15u, "KEYS: ALL KEYS kept in the settings word");
        lights_from_word(lights_word());
        check(lights_keys == KEYS_ALL, "KEYS: ALL KEYS read back");
        lights_lvl = LIGHTS_OFF; frame();
        check(lights_keys_mask() == 0u && fm1_led_bg_ns == 0u, "KEYS: nothing lit while LIGHTS is OFF");
        lights_keys = KEYS_OFF;
        ui.menu = 0; ui.force = 1; go_home(); frame();
    }

    {   /* the menu in sections (2.4): SELECT the section, KNOB 1..3 its rows, PRESETS the cursor, OCT+ opens */
        uint32_t sec;
        ui.menu = 1; ui.menu_sel = (uint8_t)mi_find(MI_COLOR); ui.force = 1; frame(); ppm("menu-screen");
        check(mi_sec(ui.menu_sel) == MS_SCREEN, "menu: SCREEN (COLOR, ZOOM)");
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(mi_sec(ui.menu_sel) == MS_LIGHTS && ui.menu_sel == mi_find(MI_LIGHTS), "menu: SELECT right: LIGHTS, the cursor on its first row");
        encs[panel.enc[EN_K2]] = 1; frame(); encs[panel.enc[EN_K2]] = 1; frame();
        check(lights_keys == KEYS_WHITE && ui.menu_sel == mi_find(MI_KEYS), "menu LIGHTS: KNOB 2 sets KEYS (row 2), the cursor follows");
        encs[panel.enc[EN_K3]] = 1; frame();
        check(lights_notes == 1u && ui.menu_sel == mi_find(MI_NOTES), "menu LIGHTS: KNOB 3 sets NOTES (row 3)");
        encs[panel.enc[EN_K4]] = 1; frame();
        check(ui.menu_sel == mi_find(MI_NOTES), "menu: KNOB 4 on a section of three rows does nothing");
        ui.force = 1; frame(); ppm("menu-lights");
        encs[panel.enc[EN_PRESET]] = 1; frame();
        check(ui.menu_sel == mi_find(MI_LIGHTS), "menu: PRESETS moves the cursor round the section's rows");
        encs[panel.enc[EN_SELECT]] = 1; frame(); ui.force = 1; frame(); ppm("menu-audio");
        check(mi_sec(ui.menu_sel) == MS_AUDIO, "menu: SELECT right: AUDIO (LOWCUT, USB AUDIO, USB SERIAL)");
        encs[panel.enc[EN_SELECT]] = 5; frame();
        sec = mi_sec(ui.menu_sel);
        check(sec == MS_SYSTEM, "menu: SELECT stops at the last section, SYSTEM");
        encs[panel.enc[EN_K2]] = 1; frame();
        check(ui.menu == 1 && ui.menu_sel == mi_find(MI_PROJ), "menu SYSTEM: KNOB 2 is JAM PROJECTS, nothing opened by a turn");
        ui.force = 1; frame(); ppm("menu-system");
        encs[panel.enc[EN_K3]] = 1; frame();
        check(ui.menu == 1 && ui.menu_sel == mi_find(MI_ABOUT), "menu SYSTEM: a knob does not open ABOUT, it moves the cursor");
        tap(B_OCTUP);
        check(ui.menu == 2, "menu SYSTEM: OCT+ on ABOUT opens it");
        tap(B_OCTDN);
        check(ui.menu == 1 && ui.menu_sel == mi_find(MI_ABOUT), "menu: OCT- from ABOUT: back to SYSTEM");
        tap(B_OCTDN);
        check(ui.menu == 0, "menu: OCT- closes");
        lights_keys = KEYS_OFF; lights_notes = 0; lights_lvl = LIGHTS_OFF;
        go_home(); ui.force = 1; frame();
    }

    {   /* SELECT (2.4): the pages of the family shown; the tempo on HOME (TRACKS), in a layer, on REC */
        int16_t bpm;
        #define PT() (PAGES[ui.page].title)
        #define SEL(n) do { encs[panel.enc[EN_SELECT]] = (n); frame(); } while (0)
        song.playing = 0; go_home(); ui.force = 1; frame();
        bpm = song.g[G_BPM];
        SEL(1);
        check(song.g[G_BPM] > bpm, "SELECT on LIVE: the tempo");
        song.g[G_BPM] = bpm;
        {   /* drums only: ENV, LFO, SEL, EDIT, ARP have no pages; their taps open nothing */
            uint32_t pg0 = ui.page, home0 = ui.home;
            tap(B_LFO); tap(B_ENV);
            check(ui.page == pg0 && ui.home == home0, "LFO, ENV tapped: no page (drums only)");
        }
        tap(B_FX);
        check(!strcmp(PT(), "FILTER"), "SELECT: FX opens its first page, the track FILTER");
        TDRUM->p[P_TFLT] = -32; ui.force = 1; frames(2); ppm("page-filter");
        check(!strcmp(ui.big_l[0], "FILT") && !strcmp(ui.big_v[0], "LP50") && !ui.big_l[1][0],
              "FILTER page: its one value drawn large (LP50 %)");
        {   /* the big value in the graph strip: white pixels in the middle of the screen */
            uint32_t x, y, lit = 0;
            for (y = Y_GRAPH + 30; y < Y_GRAPH + 90; y++)
                for (x = 40; x < 200; x++)
                    lit += screen[y * 240u + x] != 0;
            check(lit > 300u, "FILTER page: the graph strip shows it");
        }
        TDRUM->p[P_TFLT] = 0;
        SEL(1);
        check(!strcmp(PT(), "INSERT") && song.g[G_BPM] == bpm, "SELECT on FX: next page INSERT, the tempo untouched");
        SEL(1);
        check(!strcmp(PT(), "INSERT 2") && song.g[G_BPM] == bpm, "SELECT: then INSERT 2");
        SEL(9);
        check(!strcmp(PT(), "REV") && song.g[G_BPM] == bpm, "SELECT: stops at the last page of the family (REV)");
        SEL(-1);
        check(!strcmp(PT(), "DLY"), "SELECT left: the page before");
        tap(B_SEQ);
        check(on_drum_page(), "SEQ tapped on FX: the drum grid (EDIT)");
        tap(B_FX);
        check(!strcmp(PT(), "DLY"), "the button still opens its family (the page last shown)");
        tap(B_FX);
        check(!strcmp(PT(), "REV"), "... and a second tap still steps the page");
        press(B_ARP); frames(3);
        SEL(1);
        check(!strcmp(PT(), "REV") && song.g[G_BPM] > bpm, "SELECT with a layer held (ARP): the tempo");
        release(B_ARP); frames(2);
        song.g[G_BPM] = bpm;
        go_home(); frame(); SEL(-1);
        check(song.g[G_BPM] < bpm, "back on HOME: SELECT is the tempo again");
        song.g[G_BPM] = bpm;
        #undef SEL
        #undef PT
    }

    {   /* Felucca #39 / #102 (2.4): a knob turned as a layer lets go, or while PLAY is pressed in a layer, never
         * edits the page under the layer */
        int16_t r0, w0;
        uint32_t j;
        song.playing = 0; go_home(); ui.force = 1; frames(3);
        for (j = 0; j < NPAGES; j++) if (PAGES[j].fam == FAM_FX && !strcmp(PAGES[j].title, "FILTER")) break;
        open_family(FAM_FX); ui.page = (uint8_t)j; ui.fam_last[FAM_FX] = (uint8_t)j; page_entered(); ui.force = 1; frames(2);
        r0 = TDRUM->p[P_TFLT]; w0 = song.g[G_DUST];
        press(B_FX); frames(10);
        encs[panel.enc[EN_K2]] = 3; frame();
        encs[panel.enc[EN_K1]] = 4;                         /* turned in the frame FX comes up */
        fm1_in.buttons &= ~BT(B_FX); frame();
        check(TDRUM->p[P_TFLT] == r0, "#39: KNOB 1 turned as FX lets go: the FILTER page under it is untouched");
        frames(5); encs[panel.enc[EN_K1]] = 2; frame();
        check(TDRUM->p[P_TFLT] == r0, "#39: ... and for a moment after (the knob still turning)");
        frames(20); encs[panel.enc[EN_K1]] = 2; frame();
        check(TDRUM->p[P_TFLT] > r0, "#39: then KNOB 1 is the page's again");
        TDRUM->p[P_TFLT] = r0; song.g[G_DUST] = w0;
        press(B_FX); frames(10);
        edges_btn |= BT(B_PLAY); fm1_in.buttons |= BT(B_PLAY);
        encs[panel.enc[EN_K2]] = 2; frame();
        fm1_in.buttons &= ~BT(B_PLAY);
        check(TDRUM->p[P_TFLT] == r0 && song.g[G_DUST] > 0 && transport_req, "#102: PLAY pressed in the FX layer plays, KNOB 2 is DUST only");
        transport_req = 0; song.g[G_DUST] = 0;
        release(B_FX); frames(20);
        song.g[G_BPM] = 120;
    }

    {   /* DRUMS grid (2.4): the keys are the steps of the sound; the sound and the step heard; SELECT grid / kit */
        uint32_t i;
        song.playing = 0; go_home(); frame();
        memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
        TDRUM->p[P_SLEN] = 32;
        studio_open(SC_DRUM); drum_page = 0; drum_lane = 0; drum_cursor = 0; frames(2);
        memset(pad_lit, 0, sizeof pad_lit);
        key(key_of_white(2)); frame();
        check(dstep_has(&TDRUM->dstep[2], 0) && drum_cursor == 2 && pad_lit[0], "grid: white key 3 sets step 3 of the sound (heard), the cursor on it");
        check((keys_lit() >> key_of_white(2)) & 1u, "grid: its key lit");
        key(key_of_white(2));
        check(!dstep_mask(&TDRUM->dstep[2]), "grid: the key again clears it");
        key(3);                                                     /* (G#3: the second black key) */
        check(drum_cursor / 16u == 1u, "grid: the black keys pick the page of steps (17-32)");
        key(key_of_white(0));
        check(dstep_has(&TDRUM->dstep[16], 0), "grid: ... and the white keys are its steps (step 17)");
        key(8);                                                     /* (C#4: the fourth: steps 49-64, none) */
        check(drum_cursor / 16u == 1u, "grid: a page past LENGTH is not picked");
        memset(pad_lit, 0, sizeof pad_lit);
        encs[panel.enc[EN_K1]] = 3; frames(2);
        check(drum_lane == 3 && pad_lit[3], "grid: KNOB 1 picks the sound and plays it");
        dstep_set(&TDRUM->dstep[20], 5, LV_NORM, 0);
        dstep_set(&TDRUM->dstep[20], 7, LV_SOFT, 0);
        memset(pad_lit, 0, sizeof pad_lit);
        drum_cursor = 19; encs[panel.enc[EN_K2]] = 1; frames(2);
        check(drum_cursor == 20 && pad_lit[5] && pad_lit[7] && !pad_lit[3], "grid: KNOB 2 onto a step plays what it holds");
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(drum_page == 1, "DRUMS: SELECT right: kit");
        frame();
        memset(pad_lit, 0, sizeof pad_lit);
        key(key_of_white(9)); frame();
        check(pad_lit[9] && !dstep_has(&TDRUM->dstep[25], 3), "kit: the keys play the pads again, no step set");
        encs[panel.enc[EN_SELECT]] = -1; frame();
        check(drum_page == 0, "DRUMS: SELECT left: grid");
        go_home(); frame();
        memset(pad_lit, 0, sizeof pad_lit);
        pen_lane = 0;
        press(B_SEQ); frames(3);
        encs[panel.enc[EN_K1]] = 2; frames(2);
        check(pen_lane == 2 && pad_lit[2], "SEQ layer, drum track: KNOB 1 picks the sound and plays it");
        release(B_SEQ); frames(2);
        TDRUM->p[P_SDIV] = 5;
        press(B_SEQ); frames(3);
        for (i = 0; i < 5u; i++) { encs[panel.enc[EN_K2]] = 1; frame(); }
        check(TDRUM->p[P_SDIV] == (int16_t)(NDIV_STEP - 1u), "SEQ layer: KNOB 2 DIV goes on to 1/2, 1BAR, 2BAR");
        release(B_SEQ); frames(2);
        TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 16; memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
        go_home(); frame();
    }

    {   /* 2.6, after Felucca 1.4 / 1.5 (Leo Kuroshita): STEP LEN, SEQ VEL, the drum mutes (EDIT + a black key), MIDI
         * LEARN (GLO + F#3, a knob, a CC; LEARN CLEAR in the menu), the INSERT pages (the drum track too), ANALOG's FILTER */
        uint32_t j, w, ins_d = 0, flt_a = 0, flt_o = 0;
        #define K(n, v) do { encs[panel.enc[n]] = (v); frames(5); } while (0)
        #define WALK(fa_, ti_, hit_) do { open_family(fa_); frame(); \
            for (j = 0; j < 12u; j++) { encs[panel.enc[EN_SELECT]] = -1; frame(); } \
            for (j = 0; j < 12u; j++) { if (PAGES[ui.page].fam == (fa_) && !strcmp(PAGES[ui.page].title, ti_)) hit_ = 1; \
                encs[panel.enc[EN_SELECT]] = 1; frame(); } } while (0)
        song.playing = 0;
        {   /* 2.6, after Felucca 1.5.1: MIDI Program Change (queued by seq.c, loaded here) and FX + OCT- / OCT+: the THROWs */
            int16_t k0 = TDRUM->p[P_E0];
            uint32_t k;
            go_home(); frame();
            pc_req[TRK_DRUM] = 3;                         /* PC 2 on the drum channel: kit 2 */
            frame();
            check(TDRUM->p[P_E0] == 2, "MIDI Program Change on the drum channel: kit 2");
            TDRUM->p[P_E0] = k0;
            frames(4);
            press(B_FX); frames(10);
            fm1_in.buttons |= BT(B_OCTDN); edges_btn |= BT(B_OCTDN); frame(); frame();
            k = thr_req;
            ui.force = 1; frame(); ppm("layer-fx-throw");
            fm1_in.buttons &= ~BT(B_OCTDN); frame();
            fm1_in.buttons |= BT(B_OCTUP); edges_btn |= BT(B_OCTUP); frame(); frame();
            check(k == 1u && thr_req == 2u && !strcmp(sub_line(), "echo throw"), "FX + OCT-: REV THROW while held, FX + OCT+: ECHO THROW");
            fm1_in.buttons &= ~BT(B_OCTUP); frame();
            release(B_FX); frames(4);
            check(thr_req == 0u && song.octave == 0, "  let go: no THROW, the octave untouched");
        }
        go_home(); frames(16);
        dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);
        press(B_EDIT); frames(10);
        key(1);                                           /* a black key: mute mode */
        check(er_mute == 1u, "EDIT + a black key (drum track): MUTE mode");
        key(0);                                           /* the kick's key */
        check(drum_mute == 1u && dstep_has(&TDRUM->dstep[0], 0), "MUTE mode + the kick's key: the kick muted, its steps kept");
        ui.force = 1; frame(); ppm("layer-erase-mute");
        key(0);
        check(drum_mute == 0u, "the kick's key again: unmuted");
        key(0);
        release(B_EDIT); frames(2);
        check(er_mute == 0u && drum_mute == 1u, "EDIT let go: out of MUTE mode, the mute stays");
        drum_mute = 0;
        TDRUM->dstep[0] = (dstep_t){0};
        WALK(FAM_FX, "INSERT", ins_d);
        check(ins_d, "the drum track: the INSERT page in FX");
        go_home(); frames(16);

        for (w = 0; w < ML_N; w++) ml_tab[w] = 0;
        #define SETUP_AT(sec_) do { ui.menu = 1; ui.menu_sel = MS_FIRST[sec_]; ui.force = 1; frame(); } while (0)
        SETUP_AT(MS_LEARN);
        K(EN_K2, 1);                                      /* SETUP > LEARN > MIDI LEARN: ON */
        tap(B_GLO); frames(16);
        check(mlu.on == 1u && !mlu.pick && !ui.menu, "SETUP > LEARN: MIDI LEARN on (SETUP closed with GLO)");
        for (j = 0; j < NPAGES; j++) if (PAGES[j].fam == FAM_FX && !strcmp(PAGES[j].title, "FILTER")) break;
        open_family(FAM_FX); ui.page = (uint8_t)j; ui.fam_last[FAM_FX] = (uint8_t)j; page_entered();   /* KNOB 1 = FILTER */
        frames(16);
        ui.force = 1; frame(); ppm("header-learn");
        K(EN_K1, 1);
        check(mlu.pick && mlu.id == P_TFLT && mlu.trk == TRK_DRUM && ml_arm, "LEARN: a knob turned picks its parameter (FILTER)");
        midi_cc(TDRUM, 20, 10); frame();
        check(ml_count() == 1u && (ml_tab[0] & 127u) == 20u && (ml_tab[0] >> 9) == ml_code(P_TFLT) && !mlu.pick,
              "LEARN: a CC sent: CC 20 = the drums' FILTER");
        go_home(); frames(16);
        SETUP_AT(MS_LEARN); ui.menu_sel = (uint8_t)mi_find(MI_LEARN); tap(B_OCTUP); frames(4);   /* (OCT+ toggles) */
        check(!mlu.on, "SETUP > LEARN: OCT+ on MIDI LEARN: off");
        midi_cc(TDRUM, 20, 127);
        check(TDRUM->p[P_TFLT] == TP[P_TFLT].max, "CC 20: the drums' FILTER to its top");
        TDRUM->p[P_TFLT] = 0;
        ui.force = 1; frame(); ppm("setup-learn");
        K(EN_K3, 1);
        tap(B_OCTUP);
        check(ui.menu_sel == mi_find(MI_LCLEAR) && ml_count() == 0u, "SETUP > LEARN > MIDI LEARN CLEAR, OCT+: nothing learned any more");
        ui.menu = 0; ui.force = 1; go_home(); frames(16);
        {   /* SETUP > MIDI: CH IN OTHER OUT (drums only: the drums' channel) */
            const char *u;
            char vv[12];
            SETUP_AT(MS_MIDI);
            K(EN_K1, 1);                                  /* CH: 10 -> 11 */
            check(song.g[G_DRCH] == 11 && MROW[MS_FIRST[MS_MIDI]].id == G_DRCH && MROW[MS_FIRST[MS_MIDI] + 1u].id == G_ROUTE &&
                  MROW[MS_FIRST[MS_MIDI] + 2u].id == G_CHOTH && MROW[MS_FIRST[MS_MIDI] + 3u].id == G_MIDI,
                  "SETUP > MIDI: CH IN OTHER OUT; KNOB 1 sets the drums' channel (11)");
            param_format(&GP[G_DRCH], 0, vv, &u);
            check(!strcmp(vv, "OFF"), "SETUP > MIDI: CH 0 reads OFF");
            song.g[G_DRCH] = 10;
            ui.menu = 0; go_home(); frames(16);
        }
        {   /* SETUP > TEXTURE, TEXTURE 2 (2.6) */
            SETUP_AT(MS_TEX);
            K(EN_K1, 1);                                  /* SRC: OFF -> VINYL */
            check(song.g[G_TXSRC] == 1 && MS_FIRST[MS_TEX2 + 1u] - MS_FIRST[MS_TEX2] == 3u, "SETUP > TEXTURE: KNOB 1 the source (VINYL); TEXTURE 2 its three");
            ui.force = 1; frame(); ppm("setup-texture");
            song.g[G_TXSRC] = 0;
            ui.menu = 0; go_home(); frames(16);
        }
        #undef SETUP_AT
        #undef K
        #undef WALK
    }

    {   /* 2.6 (a YouTube comment): SAVE > PROJECT, KNOB 2 ERASE, a GO button: one detent arms (AGAIN: ERASE), a second
         * one erases the slot KNOB 1 picks; a turn to the left does nothing */
        uint32_t i, e0 = erases;
        song.playing = 0;
        go_home(); frame();
        for (i = 0; i < NPAGES; i++) if (!strcmp(PAGES[i].title, "PROJECT")) break;
        open_family(PAGES[i].fam); ui.page = (uint8_t)i; ui.fam_last[PAGES[i].fam] = (uint8_t)i; page_entered(); ui.force = 1; frame();
        check(PAGES[ui.page].graph == GR_SLOTS && PAGES[ui.page].id[1] == 0xFFu, "SAVE > PROJECT: KNOB 2 free for ERASE (no parameter)");
        song.g[G_SLOT] = 2;
        encs[panel.enc[EN_K2]] = -1; frames(2);
        check(erases == e0 && ui.arm != 0xE8u, "PROJECT, KNOB 2 left: nothing");
        encs[panel.enc[EN_K2]] = 1; frames(2);
        check(erases == e0 && ui.arm == 0xE8u && !strcmp(ui.msg, "AGAIN: ERASE"), "PROJECT, KNOB 2 one detent: AGAIN: ERASE, nothing erased");
        ui.force = 1; frame(); ppm("page-project-erase");
        encs[panel.enc[EN_K2]] = 1; frames(2);
        check(erases == e0 + 1u && !ui.arm && !strcmp(ui.msg, "ERASED"), "PROJECT, KNOB 2 again: the slot erased");
        encs[panel.enc[EN_K2]] = 1; frames(100);
        encs[panel.enc[EN_K2]] = 1; frames(2);
        check(erases == e0 + 1u && ui.arm == 0xE8u, "  the arm runs out (~1.5 s): a detent after it arms again");
        ui.arm = 0; song.g[G_SLOT] = 1;
        go_home(); frames(4);
    }
    {   /* fuzz: 20000 frames of random buttons (held or tapped), knobs and keys, with the audio running
         * between frames; every draw stays on the screen (lcd_blit / lcd_fill assert it) */
        uint32_t f, seed = 777, held = 0;
        #define R(n) ((seed = seed * 1664525u + 1013904223u) >> 8) % (n)
        go_home(); ui.force = 1;
        for (f = 0; f < 20000u; f++) {
            if (R(6) == 0) {
                uint32_t bt = R(NB);
                edges_btn |= 1u << panel.btn[bt];
                if (bt != B_HOME && R(3) == 0) held ^= 1u << panel.btn[bt];
            }
            if (R(30) == 0) held = 0;
            if (R(700) == 0) ly_lock = (uint8_t)R(LY_COUNT);              /* (a layer locked open, at random) */
            fm1_in.buttons = held & ~(1u << panel.btn[B_HOME]);
            if (R(4) == 0) encs[R(7)] += (int32_t)R(5) - 2;
            fm1_in.notes = R(10) == 0 ? (1u << R(27)) : (R(3) ? fm1_in.notes : 0);
            frame();
            if (ui.menu) { ui.menu = 0; ui.force = 1; }               /* (the menu is tested above) */
        }
        fm1_in.buttons = 0; fm1_in.notes = 0; frames(4);
        #undef R
    }
    printf("ui: %s\n", fails ? "FAILED" : "pages, layers (fx, steps, erase, roll, key, mix), layer lock, song layer, REC hold, drums, 20000-frame fuzz PASS");
    return fails;
}
#endif
