/* SPDX-License-Identifier: GPL-3.0-only */
/* BackBeat boot splash: the logo (tools/gen_logo.py -> backbeat_logo.h: 16 colours, RLE runs of
 * (run - 1) << 4 | colour), drawn into the canvas in bands of at most 120 rows. */
#include "backbeat_logo.h"
static void logo_draw(uint32_t y0)
{
    uint32_t r0;
    for (r0 = 0; r0 < LOGO_SPLASH_H; r0 += 120u) {
        uint32_t rows = LOGO_SPLASH_H - r0 > 120u ? 120u : LOGO_SPLASH_H - r0;
        uint32_t lo = r0 * LOGO_SPLASH_W, hi = (r0 + rows) * LOGO_SPLASH_W, pos = 0, i;
        cv_begin(LOGO_SPLASH_W, rows, C_BLACK);
        for (i = 0; i < sizeof LOGO_SPLASH_RLE && pos < hi; i++) {
            uint32_t run = (LOGO_SPLASH_RLE[i] >> 4) + 1u, c = LOGO_SPLASH_RLE[i] & 15u, p;
            if (c && pos + run > lo)
                for (p = pos < lo ? lo : pos; p < pos + run && p < hi; p++)
                    cv_px[p - lo] = swap16(LOGO_SPLASH_PAL[c]);
            pos += run;
        }
        cv_blit((240u - LOGO_SPLASH_W) / 2u, y0 + r0);
    }
}

/* the boot screen: the logo, the version, where it comes from */
static void logo_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    logo_draw(10);
    const char *v = FELUCCA_VERSION;
    if (str_len(v) > 9u && v[0] == 'B' && v[8] == ' ')
        v += 9;                                         /* "BACKBEAT 1.0" -> "1.0" under the wordmark */
    cv_begin(240, 36, C_BLACK);
    cv_text(120 - text_w(&FONT_S, v) / 2, 0, &FONT_S, v, RGB(196, 196, 204));
    cv_text(120 - text_w(&FONT_S, "based on SLOOP, Felucca") / 2, 18, &FONT_S, "based on SLOOP, Felucca", RGB(96, 96, 104));
    cv_blit(0, 202);
}
