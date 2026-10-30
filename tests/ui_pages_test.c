/* SPDX-License-Identifier: GPL-3.0-only */
/* The real UI (ui.c, ui_draw.c, ui_studio.c, ui_layers.c, ui_menu.c, ui_input.c) on a
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
 *   SEL     tapped: SONGS (2026-10-30; SAVE's tap before); on SONGS the other page; on SECTIONS the copy
 *   SAVE    tapped: nothing; held on the drum grid: the save ring, the section into its song (no
 *           layer since 2026-10-27: SLOOP's sections A..D went with JAM)
 *   EDIT    tapped: SECTIONS (2026-10-29; from SONGS: the row's song, or a new one on an empty slot): the pick into
 *           the grid or cued, the UNSAVED warning, the section / steps / div / tempo knobs, SEL copy, ENV / LFO
 *           move, OCT+ outro, REC held out (the delete ring), the refusals, SAVE held, EDIT again the grid
 *   DRUMS   the grid: sound / step / hit (off .. hard) / ratchet knobs, OCT taps (flam, mark), GRID <-> KIT
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
static struct { uint32_t magic, stage, page, home, ui_frames, late; } felucca_dbg;
static volatile uint32_t audio_max_us;                /* (audio.c's: SETUP > LOAD reads them) */
#define FELUCCA_ICONS 1
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
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
/* fm1-drummer: a song for the song screen (sng.c is not in this test): INTRO, VERSE (2 bars of 7/8), CHORUS, and up
 * to 16 sections when t_nsec says so (names of 3..12 characters: the tiles' cuts); slots 1 and 3 hold songs */
static uint32_t t_nsec = 3, t_outro = 0xFFu;
static void sng_name(uint32_t k, char *out)
{
    static const char *const N[16] = {"INTRO", "VERSE", "CHORUS", "VERSE 2", "PRE CHORUS", "CHORUS 2", "GUITAR SOLO",
                                      "BRIDGE", "BREAKDOWN", "CHORUS 3", "OUTRO JAM", "TAG", "HALF TIME", "BUILD UP",
                                      "LAST CHORUS", "OUTRO"};
    str_cpy(out, k < t_nsec ? N[k] : "", 13);
}
static uint32_t sng_ready(void) { return sng_cur >= 0 ? (1u << t_nsec) - 1u : 0u; }
static uint32_t sng_bars(uint32_t k) { return k == 1u ? 2u : 1u; }
static uint32_t sng_meter(uint32_t k) { return k == 1u ? 7u | 1u << 5 : 0u; }
static uint32_t sng_outro(void) { return t_outro; }
static int sng_pending(void) { return 0; }
static void sng_apply(uint32_t k) { sng_name(k, sng_ui_name); }
static void sng_eb(uint32_t k, uint32_t brk) { (void)k; (void)brk; }
static uint32_t sec_saves;
static int sng_save_section(void) { if (song.playing) return 1; sec_saves++; return 0; }   /* (project.c's packs and writes) */
static int sng_lossy(void) { return 0; }
/* the SECTIONS screen's writes (project.c sng_edit, sng_new; sng.c sng_same): counted; t_same: the grid as the song */
static uint32_t sec_edits, sec_news, sec_last_op = 99, t_same = 1;
static int sng_edit(uint32_t op, uint32_t k)
{
    (void)k;
    if (song.playing) return 1;
    if (sng_cur >= (int8_t)SNG_FAC0) return 5;
    sec_edits++; sec_last_op = op;
    if (op == SE_DUP) t_nsec++;
    if (op == SE_DEL) t_nsec--;
    return 0;
}
static int sng_new(uint32_t slot)
{
    if (song.playing) return 1;
    sec_news++; sng_cur = (int8_t)slot; t_nsec = 1; live_sec = 0;
    str_cpy(sng_ui_song, "SONG 6", 13); str_cpy(sng_ui_name, "PART 1", 13);
    return 0;
}
static int sng_same(uint32_t k) { (void)k; return (int)t_same; }
static uint32_t t_user = 1;                        /* 0: the user's slots empty (SONGS opens on FACTORY) */
static void sng_lib_refresh(void)                  /* (sng.c's reads the flash: here slot 1 TIME, slot 3 BLACK; the
                                                    * grooves 1, 2) */
{
    if (!sng_lib_dirty) return;
    sng_lib_dirty = 0; memset(sng_lib, 0, sizeof sng_lib);
    str_cpy(sng_lib[SNG_FAC0].name, "CLASSIC ROCK", 13); sng_lib[SNG_FAC0].bpm = 120; sng_lib[SNG_FAC0].nsec = 2;
    str_cpy(sng_lib[SNG_FAC0 + 1].name, "BLUES", 13); sng_lib[SNG_FAC0 + 1].bpm = 108; sng_lib[SNG_FAC0 + 1].nsec = 2;
    if (!t_user) return;
    str_cpy(sng_lib[0].name, "TIME", 13); sng_lib[0].bpm = 123; sng_lib[0].nsec = 3;
    str_cpy(sng_lib[2].name, "BLACK", 13); sng_lib[2].bpm = 86; sng_lib[2].nsec = 5;
    str_cpy(sng_lib[12].name, "THIRTEEN", 13); sng_lib[12].bpm = 140; sng_lib[12].nsec = 4;   /* (16 slots since 2026-10-14) */
}
#include "../firmware/src/ui_studio.c"
#include "../firmware/src/ui_drummer.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_layers.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/splash.c"
static volatile uint8_t fm1_dark;                  /* (fm1_input.h's: the scan's PA2) */
static uint8_t lcd_log[16];                        /* the panel commands screen.c sent, the last 16 */
static uint32_t lcd_n;
static void lcd_cmd(uint8_t c) { lcd_log[lcd_n++ % 16u] = c; }
#define FM1_NENC 7u
#include "../firmware/src/screen.c"
static const char *outdir;
static void ppm(const char *name) {
    char path[512]; snprintf(path,sizeof path,"%s/%s.ppm",outdir,name);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n240 240\n255\n");
    for(unsigned i=0;i<240*240;i++) { uint16_t p=swap16(screen[i]); uint8_t rgb[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31}; fwrite(rgb,1,3,f); }
    fclose(f);
}
/* one UI frame (~16 ms): the audio between (as the ISR does), then input, LEDs, draw, the screen off (as main.c) */
static void frame(void)
{
    uint32_t q;
    static int32_t o[CTL * 2];
    for (q = 0; q < 22u; q++) {
        uint32_t i;
        mix_block(o, CTL);
    }
    scr_input(); ui_leds(); ui_draw(); scr_poll(); fm1_ms += 16;
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
        song.g[G_SYNC] = 0;
        encs[panel.enc[EN_K4]] = 2; frame();
        check(song.g[G_SYNC] == 2 && !strcmp(mi_value(mi_find(MI_CLOCK), &(uint16_t){0}), "TRS") && mi_sec(mi_find(MI_CLOCK)) == MS_PEDAL,
              "SETUP > PEDAL: KNOB 4 MIDI CLOCK IN: TRS (the tempo and START / STOP from the jack)");
        ui.force = 1; frame(); ppm("setup-pedal");
        encs[panel.enc[EN_K4]] = -5; frame();
        check(song.g[G_SYNC] == 0 && !strcmp(mi_value(mi_find(MI_CLOCK), &(uint16_t){0}), "OFF"), "SETUP > PEDAL: MIDI CLOCK IN back to OFF");
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

    /* ---- SAVE: no layer since 2026-10-27 (JAM's sections A..D went) and no tap since 2026-10-30 (SEL opens SONGS,
     * the user: "sel should actually open the page instead of save"); a long press on LIVE or EDIT saves the
     * section (on SONGS it does nothing); the keys while it is held are LIVE's own */
    go_home(); frame();
    song.playing = 0; live_sec = -1; live_req = -1; sng_cur = -1;
    press(B_SAVE); frames(15); check(ui.layer == LY_PLAY && drummer_on() && sng_view == DV_LIVE, "SAVE held on LIVE: no layer, LIVE still");
    key(key_of_white(2)); check(sng_req == 3u && sng_view == DV_LIVE, "SAVE held, no song: a white key loads a song, as without SAVE");
    sng_req = 0;
    release(B_SAVE); frames(2);
    check(drummer_on() && sng_view == DV_LIVE, "SAVE let go within 0.7 s: a tap, nothing (LIVE still)");
    tap(B_SCL); frames(2);
    check(drummer_on() && sng_view == DV_LIB, "SEL tapped on LIVE: SONGS");
    press(B_SAVE); frames(60); release(B_SAVE); frames(2);
    check(drummer_on() && sng_view == DV_LIB, "SAVE held ~1 s on SONGS and let go: SONGS still");
    tap(B_SAVE); check(drummer_on() && sng_view == DV_LIB, "SAVE tapped on SONGS: nothing, SONGS still");
    tap(B_HOME); frame(); check(drummer_on() && sng_view == DV_LIVE, "HOME on SONGS: LIVE");
    {   /* held on LIVE to the end: the section into its song (the user, 2026-10-27: on the unit it did nothing there,
         * the hold was EDIT's only) */
        uint32_t s0 = sec_saves;
        press(B_SAVE); frames(120); release(B_SAVE); frames(2);
        check(!strcmp(ui.msg, "NO SONG TO SAVE INTO") && sec_saves == s0 && drummer_on() && sng_view == DV_LIVE,
              "LIVE, no song: SAVE held: nothing to save into, LIVE still");
        sng_cur = 1; live_sec = 1; str_cpy(sng_ui_name, "VERSE", sizeof sng_ui_name); song.playing = 0;
        press(B_SAVE); frames(50); check(ui.hold_kind == 2u && drummer_on(), "LIVE: SAVE held 0.8 s: the save ring");
        frames(70); release(B_SAVE); frames(2);
        check(sec_saves == s0 + 1u && !strcmp(ui.msg, "SAVED INTO THE SONG") && drummer_on() && sng_view == DV_LIVE,
              "LIVE: SAVE held to the end: the section into its song, LIVE still");
        sng_cur = -1; live_sec = -1; sng_ui_name[0] = 0; ui.msg_t = 0; frames(2);
    }
    tap(B_SCL); check(drummer_on() && sng_view == DV_LIB, "SEL tapped on LIVE: SONGS");
    {   /* fm1-drummer: SONGS lists the slots (cached), KNOB 1 picks, REC loads and goes back to LIVE (its last row,
         * JAM, SLOOP's chain page, went 2026-10-27); HOME: LIVE again; there, with a song, the keys are the pedal's */
        sng_lib_dirty = 1;
        ui.force = 1; frames(16); ppm("page-songs");       /* (after SAVE: the knobs quiet, #39) */
        check(!strcmp(sng_lib[0].name, "TIME") && !strcmp(sng_lib[2].name, "BLACK") && !sng_lib_dirty, "SONGS: the slots read once (TIME, BLACK)");
        check(lib_page() == 0u, "SONGS: the user's songs there: the USER page");
        check(kb_grid == 2u, "SONGS: the keys are the screen's");
        encs[panel.enc[EN_K1]] = 2; frame(); tap(B_REC); frame();
        check(sng_req == 3u && drummer_on() && sng_view == DV_LIVE, "SONGS: KNOB 1 to slot 3, REC: song 3 asked for, LIVE");
        sng_req = 0;
        tap(B_SCL); encs[panel.enc[EN_K1]] = 15; frame(); ui.force = 1; frame(); ppm("page-songs-16");
        check(sng_pick == 15u, "SONGS: the 16th slot reached (the list scrolls)");
        tap(B_REC); frame();
        check(sng_req == 16u, "SONGS: REC on slot 16: song 16 asked for");
        sng_req = 0;
        tap(B_SCL); encs[panel.enc[EN_K1]] = 20; frame();
        check(sng_pick == 15u && drummer_on() && sng_view == DV_LIB, "SONGS: KNOB 1 stops at slot 16 (no JAM row after it)");
        tap(B_REC); frame();
        check(sng_req == 16u && sng_view == DV_LIVE, "SONGS: REC there: song 16 asked for, LIVE");
        sng_req = 0;
        tap(B_SCL); frame();
        tap(B_HOME); frame();
        check(drummer_on() && sng_view == DV_LIVE, "HOME: LIVE");
        ui.force = 1; frame(); ppm("live-nosong");
        key(key_of_white(2)); check(sng_req == 3u, "LIVE, no song: white key 3 loads song 3");
        sng_req = 0;
        key(key_of_white(12)); check(sng_req == 13u, "LIVE, no song: white key 13 loads song 13 (16 songs)");
        sng_req = 0;
        tap(B_SCL); key(key_of_white(0)); check(sng_req == 1u && sng_view == DV_LIVE, "SONGS: white key 1 loads song 1, LIVE");
        sng_req = 0;
        /* the FACTORY page: the grooves, songs 16..31 (as the pedal's PC 16..31) */
        tap(B_SCL); tap(B_LFO); frame();
        check(lib_page() == 1u && sng_view == DV_LIB, "SONGS: LFO: the FACTORY page");
        ui.force = 1; frame(); ppm("page-songs-factory");
        /* SEL on SONGS: the other page (2026-10-30; it did nothing there); on LIVE with no song: SONGS, as with one */
        tap(B_SCL); frame(); check(lib_page() == 0u && sng_view == DV_LIB, "SONGS: SEL on FACTORY: USER");
        tap(B_SCL); frame(); check(lib_page() == 1u && sng_view == DV_LIB, "SONGS: SEL on USER: FACTORY");
        tap(B_ENV); frame(); check(lib_page() == 0u, "SONGS: ENV: USER outright");
        tap(B_ENV); frame(); check(lib_page() == 0u, "SONGS: ENV on USER: USER still");
        tap(B_HOME); frame(); check(sng_view == DV_LIVE && sng_cur < 0, "HOME: LIVE, no song");
        tap(B_LFO); frame(); check(lib_page() == 1u && sng_view == DV_LIVE, "LIVE, no song: LFO: the FACTORY page shown");
        tap(B_SCL); frame(); check(lib_page() == 1u && sng_view == DV_LIB, "LIVE, no song: SEL: SONGS, the page kept");
        key(key_of_white(1)); check(sng_req == 18u && sng_view == DV_LIVE, "FACTORY: white key 2 loads groove 2 (PC 17)");
        sng_req = 0;
        tap(B_SCL); encs[panel.enc[EN_K1]] = -20; frame(); tap(B_REC); frame();
        check(lib_page() == 1u && sng_req == 17u, "FACTORY: the page kept, REC on row 1: groove 1");
        sng_req = 0;
        ui.force = 1; frame(); ppm("live-nosong-factory");
        key(key_of_white(2)); check(sng_req == 19u, "LIVE, no song, FACTORY: white key 3 loads groove 3");
        sng_req = 0;
        tap(B_ENV); frame();
        check(lib_page() == 0u && sng_view == DV_LIVE && !sng_req, "LIVE, no song: ENV: the USER page (no section step)");
        key(key_of_white(2)); check(sng_req == 3u, "LIVE, no song, USER: white key 3 loads song 3");
        sng_req = 0;
        sng_page = -1; t_user = 0; sng_lib_dirty = 1; frame();
        check(lib_page() == 1u, "no user songs: the FACTORY page first");
        sng_page = -1; t_user = 1; sng_lib_dirty = 1; frame();
        check(lib_page() == 0u, "user songs: USER first");
        sng_cur = (int8_t)(SNG_FAC0 + 4u); tap(B_SCL); frame();
        check(lib_page() == 1u && sng_pick == 4u, "SONGS with groove 5 loaded: its page, its row");
        tap(B_HOME); frame(); ui.force = 1; frame(); ppm("live-factory-head");   /* "factory 5" */
        sng_cur = -1; sng_page = 0;
        /* a song loaded (as sng_load would): the keys are the pedal's */
        sng_cur = 0; str_cpy(sng_ui_song, "TIME", sizeof sng_ui_song); live_sec = 0; live_req = -1; pedal.count = 0;
        sng_name(0, sng_ui_name);
        song.playing = 0; ui.force = 1; frame(); ppm("live-cued");
        key(key_of_white(1)); frames(3);
        check(song.playing && live_sec == 1 && !strcmp(sng_ui_name, "VERSE"), "LIVE: white key 2, stopped: VERSE starts the loop");
        pedal_fill = 0; pedal_fill_wait = 0;
        key(key_of_white(2)); frames(2);
        check(live_req == 2 && pedal_at_end && !pedal_fill && !pedal_fill_wait, "LIVE: white key 3, playing: CHORUS cued for VERSE's end, no fill");
        str_cpy(sng_ui_next, "CHORUS", sizeof sng_ui_next);
        ui.force = 1; frame(); ppm("live-playing");
        check((keys_lit() >> key_of_white(1)) & 1u, "LIVE: the section playing's key lit");
        {   /* HOME on LIVE: the other layout, the beat big (kept in the settings; from SONGS HOME is LIVE as before) */
            check(live_view == 0u, "LIVE: the tiles at boot");
            settings_later = 0;
            tap(B_HOME); frame();
            check(live_view == 1u && sng_view == DV_LIVE && settings_later, "LIVE: HOME: the beat big (to be saved)");
            ui.force = 1; frame(); ppm("live-big");             /* VERSE, bar 1/2 big, beat 1/4, NEXT CHORUS */
            sng_ui_next[0] = 0; ui.force = 1; frame(); ppm("live-big-nocue");   /* nothing coming: the beat lights */
            str_cpy(sng_ui_next, "CHORUS", sizeof sng_ui_next);
            check((keys_lit() >> key_of_white(1)) & 1u, "LIVE, the beat big: the keys still the sections");
            tap(B_SCL); frame(); tap(B_HOME); frame();
            check(sng_view == DV_LIVE && live_view == 1u, "SONGS: HOME: LIVE, the layout kept");
            live_view = 1; lights_from_word(lights_word());
            check(live_view == 1u, "the layout in the settings word");
            tap(B_HOME); frame();
            check(live_view == 0u, "LIVE: HOME again: the tiles");
            ui.force = 1; frame();
        }
        key(key_of_white(9)); frames(2);
        check(live_req == 2 && pedal_at_end, "LIVE: white key 10, a song of 3 sections: nothing (CHORUS still cued)");
        live_req = -1; pedal_at_end = 0;
        press(B_OCTUP); frames(2);
        ui.force = 1; frame(); ppm("live-nextbar");          /* "KEY: ON THE NEXT BAR" while OCT+ is held */
        key(key_of_white(0)); frames(2);
        release(B_OCTUP); frames(2);
        check(live_req == 0 && !pedal_at_end && !pedal_fill, "LIVE: OCT+ held + white key 1: INTRO on the next bar, no fill");
        live_req = -1;
        t_nsec = 16; t_outro = 15;                            /* 16 sections: keys 1..16 are sections 1..16 */
        key(key_of_white(9)); frames(2);
        check(live_req == 9 && pedal_at_end, "LIVE, 16 sections: white key 10: section 10 at the section's end");
        press(B_OCTUP); key(key_of_white(15)); release(B_OCTUP); frames(2);
        check(live_req == 15 && !pedal_at_end, "LIVE, 16 sections: OCT+ held + white key 16: section 16 on the next bar");
        check((keys_lit() >> key_of_white(1)) & 1u, "LIVE, 16 sections: the section playing's key lit");
        ui.force = 1; frame(); ppm("live-rows4");
        t_nsec = 11; t_outro = 10; live_req = 9; ui.force = 1; frame(); ppm("live-rows3");
        t_nsec = 6; t_outro = 0xFFu; ui.force = 1; frame(); ppm("live-rows2");
        t_nsec = 4; t_outro = 3; ui.force = 1; frame(); ppm("live-rows1");   /* "VERSE 2" on two lines, at its space */
        {
            char l1[13], l2[8];
            name_split("VERSE 2", l1, l2);
            check(!strcmp(l1, "VERSE") && !strcmp(l2, "2"), "LIVE tiles: a long name on two lines, at the space");
            name_split("BREAKDOWNXYZ", l1, l2);
            check(!strcmp(l1, "BREAKD") && !strcmp(l2, "OWNXYZ"), "LIVE tiles: no space: 6 + 6");
            name_split("PRE CHORUS", l1, l2);
            check(!strcmp(l1, "PRE") && !strcmp(l2, "CHORUS"), "LIVE tiles: PRE / CHORUS");
        }
        t_nsec = 3; t_outro = 0xFFu; live_req = -1; pedal_at_end = 0;
        key(key_of_white(1)); frames(2);
        check(live_req < 0 && !pedal_fill && !pedal_fill_wait, "LIVE: the section playing's key again: nothing (no fill)");
        sng_ui_next[0] = 0;
        ui.force = 1; frame(); ppm("live-fill");
        {   /* the black keys (2026-10-19): the first the fill (CC 21: this bar, or the next one from its last beat), the
             * second the whole next bar (value 1), the fourth the break (CC 28), the others as the first; then a white
             * key: the fill into the change, on the bar after it */
            static const uint8_t BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
            uint32_t b, all = 1;
            for (b = 0; b < 11u; b++) {
                if (b == 1u || b == 3u || b == 9u || b == 10u)   /* (D#5 the ending since 2026-10-30; the last, F#5: the
                                                                 * tap tempo since 2026-10-22, below) */
                    continue;
                pedal_fill = 0; pedal_fill_wait = 0;
                key(BLACK[b]); frames(2);
                all &= (pedal_fill || pedal_fill_wait) && song.playing;
            }
            check(all, "LIVE: the first black key and the others: the fill (CC 21), playing on");
            pedal_fill = 0; pedal_fill_wait = 0;
            key(BLACK[1]); frames(2);
            check(!pedal_fill && pedal_fill_wait && song.playing, "LIVE: the second black key (G#3): the fill on the next bar (CC 21 value 1), no hold");
            pedal_fill = 0; pedal_fill_wait = 0; live_req = -1; live_req_brk = 0;
            key(BLACK[3]); frames(2);
            check(live_req == live_sec && live_req_brk && !pedal_fill && !pedal_fill_wait && song.playing,
                  "LIVE: the fourth black key (C#4): the break (CC 28): the section playing again through its break, no fill");
            live_req = -1; live_req_brk = 0;
            pedal_fill = 0; pedal_fill_wait = 0; pedal_at_end = 0; sng_ui_next[0] = 0;
            key(BLACK[9]); frames(2);                       /* D#5 (2026-10-30): the ending, CC 22 value 127 (the stub's song has
                                                             * no outro: the section playing to its end, then the stop) */
            check(pedal_at_end && !strcmp(sng_ui_next, "STOP") && !pedal_fill && !pedal_fill_wait && song.playing,
                  "LIVE: the tenth black key (D#5): the ending (CC 22): the section ends, then the stop, no fill");
            pedal_at_end = 0; sng_ui_next[0] = 0; live_req = -1;
            pedal_fill = 0; pedal_fill_wait = 0;
            press(B_OCTUP); key(BLACK[0]); release(B_OCTUP); frames(2);
            check(!pedal_fill && pedal_fill_wait && song.playing, "LIVE: OCT+ held + the first black key: the fill on the next bar (CC 21 value 1)");
            key(key_of_white(2)); frames(2);                /* (that fill pending) */
            check(live_req == 2 && !pedal_at_end && (pedal_fill || pedal_fill_wait), "LIVE: a black key, then white key 3: the fill, CHORUS on the bar after it");
            ui.force = 1; frame(); ppm("live-fill-next");     /* "FILL > CHORUS" */
            live_req = -1; pedal_at_end = 0; pedal_fill = 0; pedal_fill_wait = 0;
        }
        {   /* the last black key, F#5 (2026-10-22): the tap tempo (as CC 26), no fill; its light the tempo, PLAY's flash
             * while playing, free at the tempo stopped (ui_input.c tempo_led, in keys_lit on the song screen) */
            int16_t bpm = song.g[G_BPM];
            uint32_t k, bad = 0, on = 0;
            pedal.tap_n = 0; pedal_fill = 0; pedal_fill_wait = 0;
            for (k = 0; k < 4u; k++) {                    /* four taps half a second apart on the sample clock: 120 BPM */
                pedal.clock = 2000000u + k * (FS / 2u);
                drummer_key(25u, 0u);
            }
            check(song.g[G_BPM] == 120 && !pedal_fill && !pedal_fill_wait && song.playing,
                  "LIVE: the last black key (F#5): the tap tempo (four taps 500 ms apart: 120 BPM), no fill");
            for (k = 0; k < 48u; k++) {
                clk_pos = k * (BEAT_U / 24u);
                bad += (uint32_t)((keys_lit() >> 25 & 1u) != (uint32_t)play_led());
                on += keys_lit() >> 25 & 1u;
            }
            check(kb_grid == 2u && bad == 0u && on > 0u, "LIVE: the tap tempo key's light flashes with PLAY (the beat as heard)");
            song.g[G_BPM] = bpm; pedal.tap_n = 0;
        }
        tap(B_PLAY); frames(3);
        check(!song.playing, "LIVE: PLAY: stop");
        tap(B_PLAY); frames(3);
        check(song.playing && live_sec >= 0, "LIVE: PLAY: the section cued plays");
        {   /* ENV / LFO: PREV, NEXT (CC 25, 24); SEL: SONGS, no fill (it was the fill, CC 21, until 2026-10-30) */
            pedal_fill_wait = 0; pedal_fill = 0; live_req = -1; pedal_at_end = 0;
            tap(B_SCL); frames(2);
            check(!pedal_fill && !pedal_fill_wait && sng_view == DV_LIB, "LIVE, playing: SEL: SONGS, no fill");
            tap(B_HOME); frames(2);
            check(sng_view == DV_LIVE && song.playing, "HOME: LIVE, still playing");
            pedal_fill_wait = 0; pedal_fill = 0; live_req = -1; pedal_at_end = 0;
            tap(B_LFO); frames(2);
            check(live_req == (int8_t)(live_sec + 1) && !pedal_fill, "LIVE: LFO: the next section (as CC 24, no fill)");
            live_req = -1; pedal_at_end = 0;
            tap(B_ENV); frames(2);
            check(live_req == (int8_t)(live_sec - 1) && !pedal_fill, "LIVE: ENV: the previous section (as CC 25, no fill)");
            live_req = -1; pedal_at_end = 0; pedal_fill = 0;
        }
        {   /* ALGORITHM: the page's songs in turn (2026-10-30); the stub's USER slots: 1 TIME, 3 BLACK, 13 THIRTEEN;
             * FACTORY: 1 CLASSIC ROCK, 2 BLUES */
            int8_t c0 = sng_cur, p0 = sng_page;
            uint8_t r0 = sng_req;
            sng_cur = -1; sng_req = 0; sng_page = 0;
            encs[panel.enc[EN_ALGO]] = 1; frame(); check(sng_req == 1u, "LIVE, no song, USER: ALGORITHM up: song 1 loaded");
            encs[panel.enc[EN_ALGO]] = 1; frame(); check(sng_req == 3u, "LIVE: ALGORITHM up again before it landed: song 3 (slot 2 empty, skipped)");
            sng_req = 0; sng_cur = 2;
            encs[panel.enc[EN_ALGO]] = 1; frame(); check(sng_req == 13u, "LIVE, song 3: ALGORITHM up: song 13 (4..12 empty)");
            sng_req = 0; sng_cur = 12;
            encs[panel.enc[EN_ALGO]] = 1; frame(); check(sng_req == 0u, "LIVE, song 13: ALGORITHM up: the last with a song, nothing");
            encs[panel.enc[EN_ALGO]] = -1; frame(); check(sng_req == 3u, "LIVE, song 13: ALGORITHM down: song 3");
            sng_req = 0; sng_cur = -1;
            encs[panel.enc[EN_ALGO]] = -1; frame(); check(sng_req == 13u, "LIVE, no song: ALGORITHM down: the last song, 13");
            sng_req = 0; sng_cur = -1; sng_page = 1;
            encs[panel.enc[EN_ALGO]] = 1; frame(); check(sng_req == 17u, "LIVE, no song, FACTORY: ALGORITHM up: groove 1 (PC 16)");
            sng_req = 0; sng_cur = (int8_t)SNG_FAC0;
            encs[panel.enc[EN_ALGO]] = 1; frame(); check(sng_req == 18u, "LIVE, groove 1: ALGORITHM up: groove 2");
            sng_req = 0; sng_cur = 2; sng_page = 1;
            encs[panel.enc[EN_ALGO]] = 1; frame(); check(sng_req == 17u, "LIVE, song 3 loaded but FACTORY shown: ALGORITHM up: groove 1");
            sng_cur = c0; sng_page = p0; sng_req = r0; frame();
        }
        {   /* the knobs: tempo, kit, drums level, swing */
            int16_t bpm = song.g[G_BPM], kit = TDRUM->p[P_E0], lv = song.g[G_DRLVL], sw = song.g[G_SWING];
            frames(16);
            encs[panel.enc[EN_K1]] = 2; frame(); check(song.g[G_BPM] == bpm + 2, "LIVE: KNOB 1: the tempo");
            encs[panel.enc[EN_K2]] = 1; frame(); check(TDRUM->p[P_E0] != kit && !strncmp(ui.msg, "KIT ", 4), "LIVE: KNOB 2: the kit (said)");   /* (up: the power-on kit, MULDJORD, is the first since 2026-10-15) */
            encs[panel.enc[EN_K3]] = -3; frame(); check(song.g[G_DRLVL] == lv - 3, "LIVE: KNOB 3: the drums' level");
            encs[panel.enc[EN_K4]] = 4; frame(); check(song.g[G_SWING] == sw + 4, "LIVE: KNOB 4: the swing");
            ui.force = 1; frame(); ppm("live-knob");
            song.g[G_BPM] = bpm; TDRUM->p[P_E0] = kit; song.g[G_DRLVL] = lv; song.g[G_SWING] = sw; ui.msg_t = 0;
        }
        {   /* the count-in, the pedal's MIDI dot */
            tap(B_PLAY); frames(3);
            ci_on = 1; ci_pedal = 8; ci_beat = 2; ci_meter = 0; pedal_seen_ms = fm1_ms;
            ui.force = 1; frame(); ppm("live-countin");
            live_view = 1; ui.force = 1; frame(); ppm("live-big-countin");   /* the count big, amber, the beat beside */
            ci_on = 0; ci_pedal = 0;
        }
        check(!song.playing, "LIVE: PLAY again: stop");
        ui.force = 1; frame(); ppm("live-big-cued");             /* stopped: the bar dim, CUED: PLAY */
        live_view = 0;
        sng_cur = -1; sng_ui_song[0] = 0; live_sec = -1; live_req = -1; sng_ui_name[0] = 0;
    }

    /* ---- SECTIONS (2026-10-29): EDIT tapped on LIVE with a song; the section picked goes to the grid (stopped) or is
     * cued (playing); the knobs; the edits write the song (stubbed: counted); EDIT on SONGS: a song's sections, or a
     * new song on an empty slot */
    {
        uint32_t e0 = sec_edits, s0;
        int16_t l0, d0, b0;
        go_home(); frames(2);
        tap(B_EDIT); frames(2);
        check(drummer_on() && sng_view == DV_LIB && !strcmp(ui.msg, "EMPTY SLOT + EDIT: NEW"), "EDIT tapped on LIVE with no song: SONGS, how to make one");
        go_home(); sng_cur = 0; live_sec = 0; live_req = -1; t_nsec = 3; str_cpy(sng_ui_song, "TIME", sizeof sng_ui_song);
        sng_name(0, sng_ui_name); song.playing = 0; transport_req = 0; ui.msg_t = 0; frames(2);
        tap(B_EDIT); frames(16);
        check(drummer_on() && sng_view == DV_SEC, "EDIT tapped on LIVE with a song: SECTIONS");
        ui.force = 1; frame(); ppm("sections");
        key(key_of_white(1)); frames(2);
        check(live_sec == 1 && !strcmp(sng_ui_name, "VERSE"), "SECTIONS, stopped: white key 2 puts section 2 in the grid");
        t_same = 0; encs[panel.enc[EN_K1]] = 1; frame();
        check(live_sec == 1 && !strcmp(ui.msg, "UNSAVED: AGAIN TO DROP"), "SECTIONS: the grid edited: the first move away warns");
        encs[panel.enc[EN_K1]] = 1; frame();
        check(live_sec == 2, "SECTIONS: the next move within 3 s drops the edits: section 3");
        t_same = 1; encs[panel.enc[EN_K1]] = 5; frame();
        check(live_sec == 2, "SECTIONS: KNOB 1 stops at the last section");
        l0 = TDRUM->p[P_SLEN]; encs[panel.enc[EN_K2]] = 1; frame();
        check(TDRUM->p[P_SLEN] == (int16_t)clamp(l0 + 1, 1, NSTEP) && !strncmp(ui.msg, "STEPS ", 6), "SECTIONS: KNOB 2 the steps");
        encs[panel.enc[EN_K2]] = -1; frame(); TDRUM->p[P_SLEN] = l0;   /* (the turn back accelerates: as before) */
        d0 = TDRUM->p[P_SDIV]; l0 = TDRUM->p[P_SLEN]; encs[panel.enc[EN_K3]] = 1; frame();
        check(TDRUM->p[P_SDIV] == d0 + 1 && !strncmp(ui.msg, "DIV ", 4) && TDRUM->p[P_SLEN] != l0, "SECTIONS: KNOB 3 the division (the pattern re-gridded)");
        encs[panel.enc[EN_K3]] = -1; frame();
        check(TDRUM->p[P_SDIV] == d0 && TDRUM->p[P_SLEN] == l0, "SECTIONS: KNOB 3 back: the length as before");
        b0 = song.g[G_BPM]; encs[panel.enc[EN_K4]] = 1; frame();
        check(song.g[G_BPM] == b0 + 1, "SECTIONS: KNOB 4 the tempo");
        encs[panel.enc[EN_K4]] = -1; frame(); song.g[G_BPM] = b0;
        tap(B_SCL); frames(2);
        check(sec_edits == e0 + 1u && sec_last_op == SE_DUP && t_nsec == 4u && !strcmp(ui.msg, "COPIED AFTER IT"), "SECTIONS: SEL copies the section after itself (the song written)");
        tap(B_LFO); frames(2);
        check(sec_last_op == SE_RIGHT && !strcmp(ui.msg, "MOVED"), "SECTIONS: LFO moves it right");
        tap(B_ENV); frames(2);
        check(sec_last_op == SE_LEFT && sec_edits == e0 + 3u, "SECTIONS: ENV moves it left");
        tap(B_OCTUP); frames(2);
        check(sec_last_op == SE_OUTRO && !strcmp(ui.msg, "NOT THE OUTRO"), "SECTIONS: OCT+ turns the outro mark (the stub's song has none)");
        press(B_REC); frames(50);
        check(ui.hold_kind == 1u && drummer_on() && sng_view == DV_SEC, "SECTIONS: REC held 0.8 s: the ring");
        ui.force = 1; frame(); ppm("hold-out-section");
        frames(100);
        check(sec_last_op == SE_DEL && t_nsec == 3u && !strcmp(ui.msg, "SECTION TAKEN OUT") && !ui.hold_kind, "SECTIONS: REC held to the end: the section out of the song");
        release(B_REC); frames(2);
        check(sng_view == DV_SEC, "SECTIONS: REC let go: still SECTIONS (no load)");
        s0 = sec_edits; t_nsec = 1; live_sec = 0;
        press(B_REC); frames(150); release(B_REC); frames(2);
        check(sec_edits == s0 && !strcmp(ui.msg, "THE ONLY SECTION"), "SECTIONS: the only section: not taken out");
        t_nsec = 16; tap(B_SCL); frames(2);
        check(sec_edits == s0 && !strcmp(ui.msg, "16 SECTIONS ALREADY"), "SECTIONS: 16 sections: no copy");
        t_nsec = 3; live_sec = 0; tap(B_ENV); frames(2);
        check(sec_edits == s0 && !strcmp(ui.msg, "AT THE END ALREADY"), "SECTIONS: the first section: not moved left");
        song.playing = 1; tap(B_SCL); frames(2);
        check(sec_edits == s0 && !strcmp(ui.msg, "STOP TO EDIT"), "SECTIONS, playing: STOP TO EDIT");
        key(key_of_white(2)); frames(2);
        check(live_req == 2 && pedal_at_end && live_sec == 0, "SECTIONS, playing: a white key cues the section for the end, as LIVE");
        live_req = -1; pedal_at_end = 0; sng_ui_next[0] = 0; song.playing = 0;
        sng_cur = (int8_t)SNG_FAC0; tap(B_SCL); frames(2);
        check(sec_edits == s0 && !strcmp(ui.msg, "FACTORY: READ-ONLY"), "SECTIONS on a groove: read-only");
        sng_cur = 0;
        s0 = sec_saves; press(B_SAVE); frames(120); release(B_SAVE); frames(2);
        check(sec_saves == s0 + 1u && sng_view == DV_SEC && !strcmp(ui.msg, "SAVED INTO THE SONG"), "SECTIONS: SAVE held saves the grid into the song");
        tap(B_EDIT); frames(2);
        check(on_drum_page(), "SECTIONS: EDIT tapped again: the grid");
        tap(B_SCL); frames(2); sng_page = 0; sng_pick = 5; tap(B_EDIT); frames(2);
        check(sec_news == 1u && sng_cur == 5 && sng_view == DV_SEC && !strcmp(ui.msg, "NEW SONG: PART 1"), "SONGS: EDIT on an empty slot: a new song there, its SECTIONS");
        ui.force = 1; frame(); ppm("sections-new");
        tap(B_SCL); frames(2);
        check(sng_view == DV_SEC && sec_last_op == SE_DUP, "SECTIONS: SEL is the copy there, not SONGS");
        tap(B_HOME); frames(2); tap(B_SCL); frames(2); sng_pick = 2; tap(B_EDIT); frames(2);
        check(sng_req == 3u && sng_view == DV_SEC, "SONGS (HOME, SEL from SECTIONS): EDIT on a song: loaded, its SECTIONS");
        sng_req = 0;
        tap(B_HOME); frames(2);
        check(sng_view == DV_LIVE, "SECTIONS: HOME: LIVE");
        sng_cur = -1; sng_ui_song[0] = 0; live_sec = -1; live_req = -1; sng_ui_name[0] = 0; t_nsec = 3; ui.msg_t = 0;
        go_home(); frames(2);
    }

    /* ---- the drum screen */
    studio_open(SC_DRUM); ui.force = 1; frames(16);   /* (after SAVE: the knobs quiet, #39) */
    check(on_drum_page(), "the drum screen");
    {   /* fm1-drummer: SAVE on the grid (EDIT): tapped, nothing (SEL: SONGS); held to the end, the section into its
         * song (stopped) */
        uint32_t s0 = sec_saves;
        sng_cur = 1; live_sec = 1; str_cpy(sng_ui_name, "VERSE", sizeof sng_ui_name); song.playing = 0;
        tap(B_SAVE); frames(2);
        check(on_drum_page() && ui.layer == LY_PLAY, "EDIT: SAVE tapped: nothing (no SONG layer on the grid, no SONGS)");
        tap(B_SCL); frames(2);
        check(drummer_on() && sng_view == DV_LIB && ui.layer == LY_PLAY, "EDIT: SEL tapped: SONGS");
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
        check(!strcmp(ui.msg, "NO SONG TO SAVE INTO") && sec_saves == s0 + 1u, "EDIT, no song: SAVE held: nothing to save into (JAM's slots went)");
        studio_open(SC_DRUM); ui.msg_t = 0; frames(16);
    }
    /* the HIT knob (2026-10-29): off, ghost, soft, norm, hard on KNOB 3; the ratchet on KNOB 4; OCT- tapped a flam,
     * OCT+ tapped the hit's mark (always, fill only, not in a fill); OCT held with a key still a ghost / hard hit */
    encs[panel.enc[EN_K3]] = 1; frame(); check(dstep_has(&TDRUM->dstep[0], 0) && dstep_lvl(&TDRUM->dstep[0], 0) == LV_GHOST, "DRUMS: KNOB 3 up from off: a ghost kick on step 1");
    encs[panel.enc[EN_K3]] = 2; frame(); check(dstep_lvl(&TDRUM->dstep[0], 0) == LV_NORM, "DRUMS: KNOB 3 two more: norm");
    encs[panel.enc[EN_K3]] = -1; frame(); check(dstep_lvl(&TDRUM->dstep[0], 0) == LV_SOFT, "DRUMS: KNOB 3 down: soft");
    encs[panel.enc[EN_K4]] = 2; frame(); check(dstep_rat(&TDRUM->dstep[0], 0) == 2u && dstep_lvl(&TDRUM->dstep[0], 0) == LV_SOFT, "DRUMS: KNOB 4: ratchet x3, the level kept");
    encs[panel.enc[EN_K3]] = 5; frame(); check(dstep_lvl(&TDRUM->dstep[0], 0) == LV_HARD && dstep_rat(&TDRUM->dstep[0], 0) == 2u, "DRUMS: KNOB 3 bounded at hard, the ratchet kept");
    tap(B_OCTDN); frame();
    check((drum_flam[0] & 1u) && !strcmp(ui.msg, "FLAM ON"), "DRUMS: OCT- tapped: the hit's flam on");
    tap(B_OCTUP); frame(); check((drum_fonly[0] & 1u) && !strcmp(ui.msg, "MARK: FILL ONLY"), "DRUMS: OCT+ tapped: the mark FILL ONLY");
    tap(B_OCTUP); frame(); check(!(drum_fonly[0] & 1u) && (drum_fno[0] & 1u), "DRUMS: OCT+ again: NOT IN A FILL");
    tap(B_OCTUP); frame(); check(!(drum_fno[0] & 1u) && !strcmp(ui.msg, "MARK: ALWAYS"), "DRUMS: OCT+ again: ALWAYS");
    press(B_OCTDN); key(key_of_white(2)); release(B_OCTDN); frame();
    check(dstep_has(&TDRUM->dstep[2], 0) && dstep_lvl(&TDRUM->dstep[2], 0) == LV_GHOST && (drum_flam[0] & 1u) && !(drum_flam[2] & 1u),
          "DRUMS: OCT- held + a key: a ghost hit, no flam toggled");
    press(B_OCTUP); key(key_of_white(3)); release(B_OCTUP); frame();
    check(dstep_lvl(&TDRUM->dstep[3], 0) == LV_HARD && !(drum_fonly[3] & 1u) && !(drum_fno[3] & 1u), "DRUMS: OCT+ held + a key: a hard hit, no mark cycled");
    drum_hit_clr(TDRUM, 2, 0); drum_hit_clr(TDRUM, 3, 0); drum_cursor = 0;   /* (the key moved the cursor) */
    tap(B_OCTDN); frame(); check(!(drum_flam[0] & 1u), "DRUMS: OCT- tapped again: the flam off");
    drum_cursor = 1; tap(B_OCTDN); frame(); check(!strcmp(ui.msg, "NO HIT ON THIS STEP"), "DRUMS: OCT- on an empty step: says so");
    drum_cursor = 0;
    encs[panel.enc[EN_K3]] = -9; frame(); check(!dstep_has(&TDRUM->dstep[0], 0), "DRUMS: KNOB 3 down to off: the hit goes");
    encs[panel.enc[EN_K3]] = 3; frame(); check(dstep_lvl(&TDRUM->dstep[0], 0) == LV_NORM && dstep_rat(&TDRUM->dstep[0], 0) == 0u, "DRUMS: ..and back: a plain norm hit, no ratchet");
    encs[panel.enc[EN_K3]] = -2; frame(); check(dstep_lvl(&TDRUM->dstep[0], 0) == LV_GHOST, "DRUMS: KNOB 3: ghost (as the old KNOB 4 softer)");
    encs[panel.enc[EN_K3]] = 1; frame(); check(dstep_lvl(&TDRUM->dstep[0], 0) == LV_SOFT, "DRUMS: KNOB 3: soft");
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
        drum_flam[4] |= 1u << 2; drum_fonly[14] |= 1u << 4; drum_fno[12] |= 1u << 4;   /* (a flam, the marks: drawn) */
        drums.hits = 1u | 1u << 4; frame(); ppm("live-grid");
        drum_flam[4] = 0; drum_fonly[14] = 0; drum_fno[12] = 0;
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
        settings_later = 0;
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(usb_serial == 1u && (lights_word() >> 16 & 1u), "menu USB SERIAL: its knob (KNOB 3) -> ON, saved with the settings");
        check(settings_later == 1u && ui.menu == 1, "a setting changed marks the settings for the save when quiet, the menu still open (2026-10-27)");
        ui.force = 1; frame(); ppm("menu-serial");
        lights_from_word(lights_word());
        check(usb_serial == 1u, "menu USB SERIAL: read back");
        tap(B_OCTUP);
        check(usb_serial == 0u && ui.menu == 1, "menu USB SERIAL: OCT+ toggles back to OFF");
        ui.menu_sel = (uint8_t)mi_find(MI_THRU); ui.force = 1; frame();
        check(mi_sec(ui.menu_sel) == MS_LEARN && !strcmp(MS_NAME[MS_LEARN], "MIDI 2") && midi_thru == 0u &&
              !(lights_word() >> 28 & 1u), "menu TRS TO USB (2026-10-22): on MIDI 2, OFF by default (an older word reads so)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        ui.force = 1; frame(); ppm("menu-midi2");
        lights_from_word(lights_word());
        check(midi_thru == THRU_ON && (lights_word() >> 28 & 1u), "menu TRS TO USB: its knob (KNOB 3) -> ON, saved with the settings, read back");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        ui.force = 1; frame(); ppm("menu-bridge");
        lights_from_word(lights_word());
        check(midi_thru == THRU_ONLY && (lights_word() >> 28 & 3u) == 3u && !strcmp(mi_value(mi_find(MI_THRU), &(uint16_t){0}), "BRIDGE ONLY"),
              "menu TRS TO USB: the knob on -> BRIDGE ONLY (and stops there), saved and read back");
        tap(B_OCTUP);
        check(midi_thru == 0u && ui.menu == 1, "menu TRS TO USB: OCT+ steps round, BRIDGE ONLY -> OFF");
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
        ui.menu_sel = (uint8_t)mi_find(MI_SCROFF); ui.force = 1; frame();
        check(mi_sec(ui.menu_sel) == MS_SCREEN && scr_off == SCR_NEVER, "SCREEN OFF: in SCREEN, NEVER by default");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(scr_off == SCR_1MIN, "SCREEN OFF: KNOB 3 right: 1 MIN");
        for (m = 0; m < 3u; m++) {
            encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        }
        ui.force = 1; frame(); ppm("page-menu-screen-off");
        check(scr_off == SCR_15MIN, "SCREEN OFF: 15 MIN, and it stops there");
        tap(B_OCTUP);
        check(scr_off == SCR_NEVER, "SCREEN OFF: OCT+ steps round to NEVER");
        scr_off = SCR_5MIN;
        lights_from_word(lights_word());
        check(scr_off == SCR_5MIN && lights_word() >> 30 == SCR_5MIN, "SCREEN OFF: kept in the settings word (bits 30, 31) and read back");
        lights_from_word(lights_word() & 0x3FFFFFFFu);
        check(scr_off == SCR_NEVER, "SCREEN OFF: a word saved before it reads NEVER");
        ui.menu = 0; ui.force = 1; go_home(); frame();
        /* the screen off itself (screen.c: scr_input / scr_poll, in frame() as in main.c) */
        scr_off = SCR_1MIN; frame();
        frames(50000u / 16u);
        check(scr_state == SCR_ON && !fm1_dark, "SCREEN OFF: 1 MIN, 50 s untouched: still on");
        frames(12000u / 16u);
        check(scr_state == SCR_ASLEEP && fm1_dark && lcd_log[(lcd_n - 2u) % 16u] == 0x28 && lcd_log[(lcd_n - 1u) % 16u] == 0x10,
              "SCREEN OFF: after 1 MIN the panel asleep (DISPOFF, SLPIN), the backlight dark");
        tap(B_GLO); frames(20);
        check(scr_state == SCR_ON && !fm1_dark && lcd_log[(lcd_n - 2u) % 16u] == 0x11 && lcd_log[(lcd_n - 1u) % 16u] == 0x29,
              "SCREEN OFF: a button wakes it (SLPOUT, DISPON), the light back");
        check(ui.menu == 0, "SCREEN OFF: ... and the press that woke it did nothing else (GLO: no SETUP)");
        tap(B_GLO);
        check(ui.menu == 1, "SCREEN OFF: the next GLO opens SETUP");
        ui.menu = 0; ui.force = 1; go_home(); frame();
        frames(62000u / 16u);
        check(scr_state == SCR_ASLEEP, "SCREEN OFF: asleep again");
        encs[panel.enc[EN_K1]] = 1; frames(20);
        check(scr_state == SCR_ON && encs[panel.enc[EN_K1]] == 0, "SCREEN OFF: a knob turned wakes it, its turn taken");
        frames(62000u / 16u);
        pedal_seen_ms = fm1_ms; frames(20);
        check(scr_state == SCR_ON && !scr_eat, "SCREEN OFF: a message on the pedal's channel wakes it (and the panel stays the page's)");
        ui_message("HELLO");
        for (m = 0; m < 62000u / 16u && scr_state == SCR_ON; m++)
            frame();
        check(m * 16u >= 60000u, "SCREEN OFF: the minute counts from the last thing that kept it on");
        scr_off = SCR_NEVER; frames(20);
        check(scr_state == SCR_ON && !fm1_dark, "SCREEN OFF: NEVER wakes it and keeps it on");
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
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(mi_sec(ui.menu_sel) == MS_LOAD && ui.menu_sel == mi_find(MI_LOAD), "menu: SELECT right: LOAD (2026-10-22)");
        {   /* LOAD: the audio interrupt's average and worst half buffer, the late ones, the drum voices; OCT+ resets */
            uint16_t c;
            song.cpu_q8 = 64; audio_max_us = 2902; felucca_dbg.late = 3; drums.vpeak = NDRUM; drums.full = 37;
            meter.t = 0; ui.force = 1; frame(); ppm("menu-load");
            check(!strcmp(mi_value(mi_find(MI_LOAD), &c), "25%") && c == C_HI &&
                  !strcmp(mi_value(mi_find(MI_WORST), &c), "50%") && c == C_HI &&
                  !strcmp(mi_value(mi_find(MI_LATE), &c), "3") && c == C_AMB &&
                  !strcmp(mi_value(mi_find(MI_VOICES), &c), "12 OF 12, 37 FULL") && c == C_AMB,
                  "menu LOAD: 25%, worst 50%, 3 late, 12 of 12 voices with 37 hits that found none free");
            audio_max_us = 5400; meter.t = 0;
            check(!strcmp(mi_value(mi_find(MI_WORST), &c), "93%") && c == C_AMB, "menu LOAD: a worst block of 80 % or more in amber");
            fm1_ms += 100; audio_max_us = 100;
            check(!strcmp(mi_value(mi_find(MI_WORST), &c), "93%"), "menu LOAD: read four times a second, not every frame");
            tap(B_OCTUP);
            check(ui.menu == 1 && !strcmp(mi_value(mi_find(MI_WORST), &c), "0%") &&
                  !strcmp(mi_value(mi_find(MI_LATE), &c), "NONE") && c == C_DIM &&
                  !strcmp(mi_value(mi_find(MI_VOICES), &c), "0 OF 12") && c == C_HI && felucca_dbg.late == 3u,
                  "menu LOAD: OCT+ resets the worst, the late (from now) and the voices");
            song.cpu_q8 = 0; felucca_dbg.late = 0; meter.late0 = 0;
        }
        encs[panel.enc[EN_SELECT]] = 5; frame();
        sec = mi_sec(ui.menu_sel);
        check(sec == MS_SYSTEM, "menu: SELECT stops at the last section, SYSTEM");
        encs[panel.enc[EN_K2]] = 1; frame();
        check(ui.menu == 1 && ui.menu_sel == mi_find(MI_ABOUT), "menu SYSTEM: KNOB 2 is ABOUT (no JAM PROJECTS since 2026-10-27), nothing opened by a turn");
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
        {   /* DIV re-grids the pattern (2026-10-29, seq.c pattern_rediv): 1/16 -> 1/8 keeps every other step, where it
             * falls on the beat, its level, ratchet, nudge, lock, condition and marks with it; 1/8 -> 1/16 puts an empty
             * step between; the hold is one undo; a pattern that would not fit keeps its division; the PATTERN page's
             * knob does the same */
            memset(TDRUM->micro, 0, sizeof TDRUM->micro); memset(TDRUM->fill, 0, sizeof TDRUM->fill);
            for (i = 0; i < NLOCK; i++) TDRUM->lock[i].step = LOCK_FREE;
            drum_fill_masks_clear();
            dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0); dstep_set(&TDRUM->dstep[2], 0, LV_HARD, 2); dstep_set(&TDRUM->dstep[3], 2, LV_NORM, 0);
            dstep_set(&TDRUM->dstep[14], 4, LV_GHOST, 0);
            TDRUM->micro[2] = 5; step_fill_set(TDRUM, 2, FC_FILL); step_fill_set(TDRUM, 3, FC_NOFILL); drum_fonly[2] = 1u; drum_flam[14] = 1u << 4;
            fm1_irq_off(); lock_set(TDRUM, 2, P_TFLT, 20); lock_set(TDRUM, 3, P_TFLT, -20); fm1_irq_on();
            press(B_SEQ); frames(3);
            encs[panel.enc[EN_K2]] = -1; frame();
            check(TDRUM->p[P_SDIV] == 1 && TDRUM->p[P_SLEN] == 8, "SEQ layer: DIV 1/16 -> 1/8: 8 steps");
            check(dstep_has(&TDRUM->dstep[0], 0) && dstep_lvl(&TDRUM->dstep[1], 0) == LV_HARD && dstep_rat(&TDRUM->dstep[1], 0) == 2u &&
                  !dstep_mask(&TDRUM->dstep[2]) && !dstep_mask(&TDRUM->dstep[3]) && dstep_lvl(&TDRUM->dstep[7], 4) == LV_GHOST && !dstep_mask(&TDRUM->dstep[14]),
                  "..every other 16th kept in its place on the beat, the one between gone");
            check(TDRUM->micro[1] == 5 && !TDRUM->micro[2] && step_fill(TDRUM, 1) == FC_FILL && step_fill(TDRUM, 2) == FC_NORM && step_fill(TDRUM, 3) == FC_NORM &&
                  drum_fonly[1] == 1u && !drum_fonly[2] && drum_flam[7] == (1u << 4) && !drum_flam[14], "..its nudge, condition and marks with it");
            check(lock_find(TDRUM, 1, P_TFLT, 0) >= 0 && lock_find(TDRUM, 2, P_TFLT, 0) < 0 && lock_find(TDRUM, 3, P_TFLT, 0) < 0,
                  "..its lock with it, the dropped step's gone");
            encs[panel.enc[EN_K2]] = 1; frame();
            check(TDRUM->p[P_SDIV] == 2 && TDRUM->p[P_SLEN] == 16 && dstep_lvl(&TDRUM->dstep[2], 0) == LV_HARD && !dstep_mask(&TDRUM->dstep[1]) &&
                  !dstep_mask(&TDRUM->dstep[3]) && dstep_has(&TDRUM->dstep[14], 4) && TDRUM->micro[2] == 5 && step_fill(TDRUM, 2) == FC_FILL &&
                  drum_fonly[2] == 1u && lock_find(TDRUM, 2, P_TFLT, 0) >= 0,
                  "SEQ layer: DIV 1/8 -> 1/16: an empty step between, the hits where they were (the 16th dropped stays lost)");
            release(B_SEQ); frames(2);
            press(B_EDIT); frames(10);
            edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
            release(B_EDIT); frames(2);
            check(TDRUM->p[P_SDIV] == 2 && TDRUM->p[P_SLEN] == 16 && dstep_has(&TDRUM->dstep[3], 2) && dstep_lvl(&TDRUM->dstep[2], 0) == LV_HARD,
                  "EDIT + OCT-: the hold's DIV turns undone, the lost 16th back");
            press(B_SEQ); frames(3);
            encs[panel.enc[EN_K2]] = -1; frame();
            release(B_SEQ); frames(2);
            press(B_EDIT); frames(10);
            edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
            check(TDRUM->p[P_SDIV] == 2 && TDRUM->p[P_SLEN] == 16 && dstep_has(&TDRUM->dstep[3], 2) && TDRUM->micro[2] == 5 && lock_find(TDRUM, 2, P_TFLT, 0) >= 0 &&
                  drum_fonly[2] == 1u, "EDIT + OCT-: a DIV change undone: the grid back with the nudge, lock and marks, the steps back");
            edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
            check(TDRUM->p[P_SDIV] == 1 && TDRUM->p[P_SLEN] == 8 && dstep_lvl(&TDRUM->dstep[1], 0) == LV_HARD && !dstep_mask(&TDRUM->dstep[2]) && TDRUM->micro[1] == 5,
                  "EDIT + OCT+: redone, onto 1/8 again");
            edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
            release(B_EDIT); frames(2);
            TDRUM->p[P_SLEN] = 64;
            press(B_SEQ); frames(3);
            encs[panel.enc[EN_K2]] = 1; frame();
            check(TDRUM->p[P_SDIV] == 2 && TDRUM->p[P_SLEN] == 64 && !strcmp(ui.msg, "DIV: OVER 64 STEPS"), "SEQ layer: 64 steps of 1/16 -> 1/32 refused, said");
            encs[panel.enc[EN_K2]] = -1; frame();
            check(TDRUM->p[P_SDIV] == 1 && TDRUM->p[P_SLEN] == 32 && dstep_lvl(&TDRUM->dstep[1], 0) == LV_HARD, "..-> 1/8 fits: 32 steps");
            release(B_SEQ); frames(2);
            {   /* the PATTERN page (SEQ's family): KNOB 2 is DIV there too */
                uint32_t j, hit = 0;
                frames(20);                                   /* (the knobs are quiet a while after a layer: LY_QUIET_MS) */
                open_family(FAM_SEQ); frame();
                for (j = 0; j < 12u && !hit; j++) {
                    if (!strcmp(PAGES[ui.page].title, "PATTERN")) hit = 1;
                    else { encs[panel.enc[EN_SELECT]] = 1; frame(); }
                }
                encs[panel.enc[EN_K2]] = 1; frames(2);
                check(hit && TDRUM->p[P_SDIV] == 2 && TDRUM->p[P_SLEN] == 64 && dstep_lvl(&TDRUM->dstep[2], 0) == LV_HARD && !dstep_mask(&TDRUM->dstep[1]),
                      "PATTERN page, KNOB 2: DIV 1/8 -> 1/16, the pattern follows");
                go_home(); frame();
            }
            div_req = 1u + 1u;                                /* a learned CC set DIV 1/8 (seq.c midi_cc_set defers it to the UI) */
            frame();
            check(TDRUM->p[P_SDIV] == 1 && TDRUM->p[P_SLEN] == 32 && !div_req && dstep_lvl(&TDRUM->dstep[1], 0) == LV_HARD,
                  "a MIDI-learned CC on DIV: applied in the main loop, the pattern follows");
            TDRUM->p[P_SDIV] = 2; TDRUM->p[P_SLEN] = 16; memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
            memset(TDRUM->micro, 0, sizeof TDRUM->micro); memset(TDRUM->fill, 0, sizeof TDRUM->fill);
            for (i = 0; i < NLOCK; i++) TDRUM->lock[i].step = LOCK_FREE;
            drum_fill_masks_clear();
            go_home(); frame();
        }
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
        K(EN_K1, 1);                                      /* SETUP > LEARN > MIDI LEARN: ON (KNOB 1: SYNC went to PEDAL) */
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
        K(EN_K2, 1);
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
    printf("ui: %s\n", fails ? "FAILED" : "pages, layers (fx, steps, erase, roll, key, mix), layer lock, SAVE's tap and hold, REC hold, drums, 20000-frame fuzz PASS");
    return fails;
}
#endif
