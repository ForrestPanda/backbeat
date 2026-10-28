/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: a packed song ("SNG2", sng.c) written by a host test as tools/fm1_song.py writes it: a header, then
 * sections from pattern strings. Included by song_test.c and pedal_test.c after sng.c (SNG_* and the lane levels). */
/* ---- a packed song, as the tool writes it */
static void w16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static uint32_t song_hdr(uint8_t *b, const char *name, uint32_t bpm, uint32_t kit, uint32_t nsec)
{
    memset(b, 0, SNG_HDR);
    w16(b, SNG_MAGIC & 0xFFFFu); w16(b + 2, SNG_MAGIC >> 16);
    strncpy((char *)b + 4, name, SNG_NAME);
    w16(b + 16, bpm);
    b[18] = (uint8_t)kit; b[19] = 50; b[20] = 100; b[21] = 20; b[22] = (uint8_t)nsec;
    return SNG_HDR;
}
/* a section, packed as the tool packs it ("SNG2"): any lanes from pattern strings ('.' rest, 'x' norm, 'X' hard,
 * 'g' ghost, 's' soft), cond: '.' normal, 'F' fill only, 'N' no fill; marks: per step the lanes only during a fill
 * (fonly) and not during one (fno); meter: the section header's meter byte (0: 4/4) */
static uint32_t song_secm(uint8_t *b, uint32_t pos, const char *name, uint32_t len, uint32_t div, uint32_t flags,
                          uint32_t meter, const char *const lane[DRUM_LANES], const char *cond, const uint16_t *fonly,
                          const uint16_t *fno)
{
    uint32_t i, l, nf = (len + 3u) / 4u, q = pos + SNG_SEC_HDR + nf;
    memset(b + pos, 0, SNG_SEC_HDR + nf);
    strncpy((char *)b + pos, name, SNG_NAME);
    b[pos + 12] = (uint8_t)len; b[pos + 13] = (uint8_t)div; b[pos + 14] = (uint8_t)flags; b[pos + 15] = (uint8_t)meter;
    for (i = 0; i < len; i++) {
        uint32_t c = cond && cond[i] ? (cond[i] == 'F' ? FC_FILL : cond[i] == 'N' ? FC_NOFILL : FC_NORM) : FC_NORM, on = 0;
        b[pos + SNG_SEC_HDR + i / 4u] |= (uint8_t)(c << (2u * (i % 4u)));
        for (l = 0; l < DRUM_LANES; l++)
            if (lane[l] && lane[l][i] && lane[l][i] != '.')
                on |= 1u << l;
        w16(b + q, on);
        q += 2;
        for (l = 0; l < DRUM_LANES; l++) {
            char ch;
            if (!((on >> l) & 1u))
                continue;
            ch = lane[l][i];
            b[q++] = (uint8_t)((ch == 'X' ? LV_HARD : ch == 'g' ? LV_GHOST : ch == 's' ? LV_SOFT : LV_NORM) |
                               (fonly && (fonly[i] >> l & 1u) ? 0x10u : 0u) | (fno && (fno[i] >> l & 1u) ? 0x20u : 0u));
        }
    }
    w16(b + pos + 16, q - pos);
    return q;
}
/* a block of steps appended to the section at pos (just written, ending at end): its fill bar of its own, entry or
 * break (flag f: SNG_F_FILLBAR, _ENTRY, _BREAK, in that order), fl steps, lanes as song_secm (no marks); the
 * section's flag and size follow. Returns the new end */
static uint32_t song_blk(uint8_t *b, uint32_t pos, uint32_t end, uint32_t f, uint32_t fl, const char *const lane[DRUM_LANES])
{
    uint32_t i, l, q = end;
    b[pos + 14] |= (uint8_t)f;
    b[q++] = (uint8_t)fl;
    for (i = 0; i < fl; i++) {
        uint32_t on = 0;
        for (l = 0; l < DRUM_LANES; l++)
            if (lane[l] && lane[l][i] && lane[l][i] != '.')
                on |= 1u << l;
        w16(b + q, on);
        q += 2;
        for (l = 0; l < DRUM_LANES; l++) {
            char ch;
            if (!((on >> l) & 1u))
                continue;
            ch = lane[l][i];
            b[q++] = (uint8_t)(ch == 'X' ? LV_HARD : ch == 'g' ? LV_GHOST : ch == 's' ? LV_SOFT : LV_NORM);
        }
    }
    w16(b + pos + 16, q - pos);
    return q;
}
static uint32_t song_fb(uint8_t *b, uint32_t pos, uint32_t end, uint32_t fl, const char *const lane[DRUM_LANES])
{
    return song_blk(b, pos, end, SNG_F_FILLBAR, fl, lane);
}
/* lanes 0 (kick), 2 (snare), 4 (hat) */
static uint32_t song_sec(uint8_t *b, uint32_t pos, const char *name, uint32_t len, uint32_t div, uint32_t flags,
                         const char *kick, const char *snare, const char *hat, const char *cond)
{
    const char *lane[DRUM_LANES] = {kick, 0, snare, 0, hat};
    return song_secm(b, pos, name, len, div, flags, 0, lane, cond, 0, 0);
}
