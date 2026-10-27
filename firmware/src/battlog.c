/* SPDX-License-Identifier: GPL-3.0-only */
/* The battery log (2026-10-27): one reading a minute, in .noinit RAM, so that a run on the battery (nothing is
 * measured over USB: the cable charges it) is read afterwards over the console (`batt`; tools/fm1_batt.py prints
 * the readings and the slope of each run). The reading is song.batt_raw (main.c: the SARADC's 10 bits on PB1, a
 * slow IIR; the bar on the screen: ui_draw.c), with what the minute was like: playing at the reading, played during
 * the minute, the screen dark, a USB host alive, the audio gate closed, the first minute after a boot. BL_N minutes
 * (17 h), the oldest overwritten; a reset or UBOOT keeps it (a power-off leaves garbage: the magic clears it). */
#define BL_N 1024u
#define BL_MAGIC 0x314C4142u                     /* "BAL1" */
#define BL_PERIOD_MS 60000u
enum { BLF_PLAYING = 1u, BLF_PLAYED = 2u, BLF_DARK = 4u, BLF_HOST = 8u, BLF_GATED = 16u, BLF_BOOT = 32u };
static struct {
    uint32_t magic, n;                           /* n: readings taken since the log was cleared */
    uint16_t v[BL_N];
    uint8_t f[BL_N];
} battlog __attribute__((section(".noinit")));
static uint32_t bl_last_ms;
static uint8_t bl_seen;                          /* flags gathered over the minute */

static void battlog_clear(void)
{
    battlog.magic = BL_MAGIC;
    battlog.n = 0;
}

static void battlog_boot(void)                   /* main.c, once: a reset keeps the log, a power-on starts it */
{
    if (battlog.magic != BL_MAGIC || battlog.n > 0x01000000u)
        battlog_clear();
    bl_last_ms = fm1_ms;
    bl_seen = BLF_BOOT;
}

static void battlog_tick(void)                   /* main.c, every frame */
{
    uint32_t i;
    if (song.playing)
        bl_seen |= BLF_PLAYED;
    if (fm1_ms - bl_last_ms < BL_PERIOD_MS)
        return;
    bl_last_ms += BL_PERIOD_MS;
    i = battlog.n % BL_N;
    battlog.v[i] = (uint16_t)(song.batt_raw < 0 ? 0 : song.batt_raw);
    battlog.f[i] = (uint8_t)(bl_seen | (song.playing ? BLF_PLAYING : 0u) | (fm1_dark ? BLF_DARK : 0u) |
                             (usb.config && !usb.suspended ? BLF_HOST : 0u) | (audio_gated ? BLF_GATED : 0u));
    battlog.n++;
    bl_seen = 0;
}
