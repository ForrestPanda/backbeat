/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: songs. A song is up to 16 named drum sections (8 until 2026-10-14) (intro, verse, chorus, bridge, solo,
 * outro...), each a drum pattern (16 lanes, 1..64 steps, a level and a ratchet per hit, the fill
 * conditions), with the song's tempo, kit, swing, drum level and reverb. Up to 8 songs live in flash
 * (storage.c OBJ_SONG0.., A/B sectors 0xA0000..: the USR1 sample slot's place, given up: the drummer keeps
 * USR2 and the kit pair USR3+4; editor.c refuses sample uploads into USR1), packed; as the FM6 bank, a song is read in place through
 * the plain XIP window (the device has little RAM left): loading it only checks the packed form and keeps a
 * pointer and the sections' offsets. A section starts on the bar (seq.c live_block: sng_apply): the drum track
 * takes its pattern. The pedal's PC n loads song n (sng_req, taken by sng_poll in the
 * main loop: flash); stopped, the song's tempo and kit are set and its first section cued at once;
 * playing, they come with the next section.
 *
 * The packed song (little endian), "SNG2": a byte a hit, so that 8 sections of 64 steps fit in a sector (16 of the
 * usual 16..48 steps fit too; SNG_MAX is the limit, in bytes, not the sections)
 *   header (32)   "SNG2", name[12], bpm u16, kit, swing, drum level, drum reverb, nsec, flags (bit 0, since 2026-10-19:
 *                 the swing on the 8ths, SNG_H_SWING8, a shuffle: seq.c grid_den8; the other bits spare), rsv[8]
 *   per section   name[12], len (1..64), div (N_SDIV index), flags (bit 0: the outro), meter (0: 4/4; else the
 *                 numerator 1..16 in bits 0..4, the denominator in bits 5..6: SNG_DEN), size u16 (the section's
 *                 bytes, this header included); fill[(len + 3) / 4] (2 bits a step: FC_*); then each step: its lanes
 *                 u16, and a byte for each lane set, lowest first: level (bits 0..1, LV_*), ratchet (2..3: hits - 1),
 *                 mark (4..5: 1 only during a fill, 2 not during one: seq.c drum_fonly / drum_fno), flam (bit 6,
 *                 2026-10-17: a grace note before the hit, seq.c drum_flam / seq_flam; bit 7 spare)
 *   fill bar      (flags bit 1, since 2026-10-15) after the pattern's steps: its steps (a byte, 1..64, one bar of the
 *                 section's meter and division as packed), then each step as above (no marks: a fill plays it as it
 *                 is, instead of the bar the fill is in, its step i at the bar's step i: seq.c fb_dstep / drum_src)
 *   entry, break  (flags bit 2, bit 3, since 2026-10-18) after the fill bar, in that order: the same, a byte of steps
 *                 (1..64, whole bars of the section's meter) then the steps, played once before the section's
 *                 pattern as it comes in (the entry: from another section or from stopped; the break: the pedal's
 *                 CC 28), the pattern from its top after them (seq.c eb_dstep / sng_eb)
 * A section's meter is the bar the pedal counts while it plays (seq.c clk_meter): where a request lands, the fill
 * bar, the ending, the click and the count-in. (The first form, "SNG1", 10 or 14 bytes a step, never reached a
 * device: not read.)
 * tools/fm1_song.py writes one from a JSON file and sends it (EDITOR_PROTOCOL.md: BK_PUT object 16 + slot).
 * The pedal settings (the channel) are a small object of their own (OBJ_ARRSET, backup object 24).
 * Included from project.c (the firmware), or directly by the host tests. */
#define SNG_MAGIC 0x32474E53u                    /* "SNG2" (SNG_SLOTS, SNG_SECTIONS: seq.c) */
#define SNG_NAME 12u
#define SNG_HDR 32u
#define SNG_SEC_HDR 18u
#define SNG_MAX 3800u                            /* a packed song (<= proj_tmp, the staging RAM; <= a sector's payload) */
#define SNG_F_OUTRO 1u
#define SNG_F_FILLBAR 2u                         /* the section carries a fill bar of its own (after its pattern) */
#define SNG_F_ENTRY 4u                           /* ..an entry (after the fill bar) */
#define SNG_F_BREAK 8u                           /* ..a break (after the entry) */
#define SNG_F_TAIL (SNG_F_FILLBAR | SNG_F_ENTRY | SNG_F_BREAK)   /* the blocks after the pattern, in this order */
#define SNG_H_SWING8 1u                          /* the header's flags: the swing moves the 8ths, not the section's steps */

typedef struct {
    const uint8_t *xip;                          /* the packed song: flash (XIP), or a RAM blob on the host */
    uint16_t off[SNG_SECTIONS];                  /* each section's offset in it (nsec of them) */
    uint16_t bpm;
    uint8_t kit, swing, swing8, drlvl, drrev, nsec, pend;   /* swing8: SNG_H_SWING8; pend: tempo and kit wait for the next section */
} sng_t;
static sng_t sng;

static uint32_t sng_u16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }
/* n steps at q (each its lanes u16, then a byte a hit) walked: the offset after them; 0 when they run past end */
static uint32_t sng_walk(const uint8_t *b, uint32_t q, uint32_t n, uint32_t end)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t on;
        if (q + 2u > end)
            return 0;
        on = sng_u16(b + q);
        for (q += 2u; on; on &= on - 1u)
            q++;
    }
    return q > end ? 0u : q;
}

/* the packed song b (n bytes) checked and indexed into *out: 0 ok, else which check failed */
static int sng_check(const uint8_t *b, uint32_t n, sng_t *out)
{
    uint32_t k, pos = SNG_HDR;
    if (n < SNG_HDR || n > SNG_MAX)
        return 1;
    if (sng_u16(b) != (SNG_MAGIC & 0xFFFFu) || sng_u16(b + 2) != (SNG_MAGIC >> 16))
        return 2;
    memset(out, 0, sizeof *out);
    out->xip = b;
    out->bpm = (uint16_t)sng_u16(b + 16);
    out->kit = b[18];
    out->swing = b[19];
    out->swing8 = b[23] & SNG_H_SWING8;
    out->drlvl = b[20];
    out->drrev = b[21];
    out->nsec = b[22];
    if (out->bpm < 40u || out->bpm > 240u || out->kit >= DRUM_KITS || out->swing > 100u ||
        !out->nsec || out->nsec > SNG_SECTIONS)
        return 3;
    for (k = 0; k < out->nsec; k++) {
        uint32_t len, i, q, end;
        if (pos + SNG_SEC_HDR > n)
            return 4;
        len = b[pos + 12];
        end = pos + sng_u16(b + pos + 16);
        if (!len || len > NSTEP || b[pos + 13] >= NDIV_STEP || (b[pos + 15] & 31u) > 16u || (b[pos + 15] & 0x80u) ||
            end > n)
            return 5;
        out->off[k] = (uint16_t)pos;
        q = sng_walk(b, pos + SNG_SEC_HDR + (len + 3u) / 4u, len, end);   /* every step walked: its hits inside the
                                                     * section (a hit's byte needs no check: any value is a level, a
                                                     * ratchet, a mark) */
        if (!q)
            return 6;
        for (i = SNG_F_FILLBAR; i <= SNG_F_BREAK; i <<= 1)   /* the fill bar of its own, the entry, the break: their
                                                     * steps after the pattern's */
            if (b[pos + 14] & i) {
                if (q >= end || !b[q] || b[q] > NSTEP)
                    return 7;
                q = sng_walk(b, q + 1u, b[q], end);
                if (!q)
                    return 7;
            }
        if (q != end)
            return 6;
        pos = end;
    }
    return 0;
}

static const uint8_t *sng_sec(uint32_t k) { return sng.xip + sng.off[k % SNG_SECTIONS]; }
/* the block (flag f: SNG_F_FILLBAR, _ENTRY, _BREAK) of section s, checked: its offset in s (its steps byte), 0 none */
static uint32_t sng_block(const uint8_t *s, uint32_t f)
{
    uint32_t size = sng_u16(s + 16), q = sng_walk(s, SNG_SEC_HDR + ((uint32_t)s[12] + 3u) / 4u, s[12], size), i;
    for (i = SNG_F_FILLBAR; q && i <= SNG_F_BREAK; i <<= 1)
        if (s[14] & i) {
            if (i == f)
                return q;
            q = sng_walk(s, q + 1u, s[q], size);
        }
    return 0;
}
/* n packed steps at q into d (and their flams), as written (a hit's mark bits, if any, mean nothing there) */
static void sng_steps(const uint8_t *q, uint32_t n, dstep_t *d, uint16_t *flam)
{
    uint32_t i, l;
    for (i = 0; i < n && i < NSTEP; i++) {
        uint32_t on = sng_u16(q);
        memset(&d[i], 0, sizeof(dstep_t));
        flam[i] = 0;
        for (q += 2, l = 0; on; on >>= 1, l++) {
            uint32_t v;
            if (!(on & 1u))
                continue;
            v = *q++;
            dstep_set(&d[i], l, v, v >> 2);
            flam[i] |= (uint16_t)((v >> 6 & 1u) << l);
        }
    }
}
static uint32_t sng_ready(void)
{
    return sng_cur >= 0 ? (1u << sng.nsec) - 1u : 0u;
}
static uint32_t sng_meter(uint32_t k)            /* section k's meter byte (0: 4/4; seq.c meter_*) */
{
    return sng_cur >= 0 && k < sng.nsec ? sng_sec(k)[15] : 0u;
}
static uint32_t sng_bars(uint32_t k)             /* the bars its pattern takes, in its meter
                                                  * (in 1/48 of a beat: every step and bar is a whole number of them) */
{
    const uint8_t *s;
    uint32_t len, u, b, bars;
    if (sng_cur < 0 || k >= sng.nsec)
        return 1;
    s = sng_sec(k);
    len = s[12];
    u = div_units((uint32_t)s[13] % NDIV_STEP) / (BEAT_U / 48u);
    b = 12u * meter_b16(s[15]);
    bars = (len * u + b - 1u) / b;
    return !bars ? 1u : bars > 64u ? 64u : bars;
}
static void sng_name(uint32_t k, char *out)      /* (the names are ASCII, 0-padded in the packed form) */
{
    const uint8_t *s = sng_cur >= 0 && k < sng.nsec ? sng_sec(k) : 0;
    uint32_t i;
    for (i = 0; i < SNG_NAME && s && s[i]; i++)
        out[i] = (char)s[i];
    out[i] = 0;
}
static int sng_pending(void) { return sng.pend != 0u; }
static uint32_t sng_outro(void)
{
    uint32_t k;
    if (sng_cur >= 0)
        for (k = 0; k < sng.nsec; k++)
            if (sng_sec(k)[14] & SNG_F_OUTRO)
                return k;
    return 0xFFu;
}

static void sng_globals(void)                    /* the song's tempo, kit, swing, drum level and reverb */
{
    song.g[G_BPM] = (int16_t)clamp((int32_t)sng.bpm, GP[G_BPM].min, GP[G_BPM].max);
    song.g[G_SWING] = (int16_t)clamp((int32_t)sng.swing, GP[G_SWING].min, GP[G_SWING].max);
    seq_swing8 = sng.swing8;
    song.g[G_DRLVL] = (int16_t)clamp((int32_t)sng.drlvl, GP[G_DRLVL].min, GP[G_DRLVL].max);
    song.g[G_DRREV] = (int16_t)clamp((int32_t)sng.drrev, GP[G_DRREV].min, GP[G_DRREV].max);
    TDRUM->p[P_E0] = (int16_t)clamp((int32_t)sng.kit, DRUM_KIT_DESC.min, DRUM_KIT_DESC.max);
    sng.pend = 0;
}

/* section k into the drum track: at an audio-block boundary (live_block, on the bar), or from the main
 * loop with the IRQ off (sng_poll, stopped). The synth tracks are not touched. */
static void sng_apply(uint32_t k)
{
    track_t *t = TDRUM;
    const uint8_t *s, *q;
    uint32_t len, nf, i, l;
    if (sng_cur < 0 || k >= sng.nsec)
        return;
    s = sng_sec(k);
    len = s[12];
    nf = (len + 3u) / 4u;
    seq_release(t);
    locks_clear(t);                              /* (no lock, no nudge, no condition: then the section's) */
    undo.valid = 0;                              /* (EDIT + OCT- would bring the section before back) */
    memcpy(t->fill, s + SNG_SEC_HDR, nf);
    drum_fill_masks_clear();
    q = s + SNG_SEC_HDR + nf;
    for (i = 0; i < len; i++) {                   /* each step: its lanes, then a byte a hit (level, ratchet, mark) */
        uint32_t on = sng_u16(q);
        memset(&t->dstep[i], 0, sizeof(dstep_t));
        for (q += 2, l = 0; on; on >>= 1, l++) {
            uint32_t v;
            if (!(on & 1u))
                continue;
            v = *q++;
            dstep_set(&t->dstep[i], l, v, v >> 2);
            drum_fonly[i] |= (uint16_t)((v >> 4 & 1u) << l);
            drum_fno[i] |= (uint16_t)((v >> 5 & 1u) << l);
            drum_flam[i] |= (uint16_t)((v >> 6 & 1u) << l);
        }
    }
    if (s[14] & SNG_F_FILLBAR) {                  /* its fill bar of its own (seq.c drum_src plays it) */
        uint32_t fl = *q++;
        sng_steps(q, fl, fb_dstep, fb_flam);
        fb_len = (uint8_t)(fl <= NSTEP ? fl : NSTEP);
        fb_phrase(s[13], s[15]);                  /* the phrase's bars: a bar of the section's meter in its steps (seq.c) */
    }
    clk_meter = s[15];
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SDIV] = (int16_t)(s[13] % NDIV_STEP);
    sng_name(k, sng_ui_name);
    if (sng.pend)
        sng_globals();
    sync_reload = 1;
    ui.force = 1;
}

/* the entry (brk 0) or the break of section k armed before its pattern, as it lands (live_block, after the reset:
 * eb_dstep's step i at the drum track's step i). A break asked for where none is written: the entry; neither: a bar of
 * rest */
static __attribute__((noinline)) void sng_eb(uint32_t k, uint32_t brk)
{
    const uint8_t *s = sng_cur >= 0 && k < sng.nsec ? sng_sec(k) : 0;
    uint32_t at = s ? sng_block(s, brk ? SNG_F_BREAK : SNG_F_ENTRY) : 0u;
    eb_len = 0;
    if (!at && brk && s)
        at = sng_block(s, SNG_F_ENTRY);
    if (at) {
        sng_steps(s + at + 1u, s[at], eb_dstep, eb_flam);
        eb_arm(s[at]);
    } else if (brk) {
        eb_rest();                                /* a bar of rest */
    }
}

/* song k's packed form, read in place (0: none): a user slot (0..15) its current copy in flash, a slot never written
 * or erased empty; a groove (16..31) from the firmware image (tools/gen_songs.py, grooves/factory.txt's order) */
#if FELUCCA_FLASH
#include "felucca_songs.h"
static const uint8_t *sng_fetch(uint32_t slot, uint32_t *len)
{
    st_hdr_t h;
    int c;
    *len = 0;
    if (slot >= SNG_FAC0) {
        if (slot >= SNG_FAC0 + SNG_NFACTORY || slot >= SNG_ALL)
            return 0;
        *len = SNG_FACTORY[slot - SNG_FAC0].n;
        return SNG_FACTORY[slot - SNG_FAC0].b;
    }
    if (!flash_ok)
        return 0;
    c = st_current(sng_obj(slot), &h);
    if (c < 0 || !h.len)
        return 0;
    *len = h.len;
    return fm1_xip_ptr(st_sector(sng_obj(slot), (uint32_t)c) + ST_PAYLOAD_OFF);
}
#elif !defined(SNG_HOST_FETCH)
static const uint8_t *sng_fetch(uint32_t slot, uint32_t *len) { (void)slot; *len = 0; return 0; }
#endif
static void sng_lib_refresh(void)                /* seq.c sng_lib: every song read again (the UI's loop) */
{
    uint32_t k, n, i;
    sng_t tmp;
    if (!sng_lib_dirty)
        return;
    sng_lib_dirty = 0;
    for (k = 0; k < SNG_ALL; k++) {
        sng_lib_t *e = &sng_lib[k];
        const uint8_t *p = sng_fetch(k, &n);
        memset(e, 0, sizeof *e);
        if (!p || sng_check(p, n, &tmp))
            continue;
        for (i = 0; i < SNG_NAME && p[4 + i]; i++)
            e->name[i] = (char)p[4 + i];
        if (!i)
            e->name[i++] = '?';                  /* (a song with no name is still there) */
        e->name[i] = 0;
        e->nsec = tmp.nsec;
        e->bpm = tmp.bpm;
    }
}

/* ---- a section edited on the device (EDIT, SAVE held) back into its song: the reverse of sng_apply */
/* n packed steps at q (a block's: the fill bar's, the entry's, the break's) re-gridded from uo to un units (1/48 beat)
 * into out (room bytes): the count byte, then the steps; the section's division changed on the device (seq.c
 * pattern_rediv: the pattern's own steps were re-gridded in RAM, the same rule: the new grid's step j takes the old
 * step at its time, else it is empty); the whole bars that fit (bar: a bar's steps at un, 0 when not whole). The bytes
 * written, 0: no room */
static uint32_t sng_pack_rediv(const uint8_t *q, uint32_t n, uint32_t uo, uint32_t un, uint32_t bar, uint8_t *out, uint32_t room)
{
    uint16_t off[NSTEP];
    uint32_t i, j, m = rediv_len(n, uo, un), w = 1;
    if (!n || n > NSTEP || !room)
        return 0;
    for (i = 0, j = 0; i < n; i++) {             /* each old step's offset */
        off[i] = (uint16_t)j;
        j += 2u + (uint32_t)__builtin_popcount(sng_u16(q + j));
    }
    if (m > NSTEP)
        m = bar && bar <= NSTEP ? NSTEP - NSTEP % bar : NSTEP;
    for (j = 0; j < m; j++) {
        int32_t s = rediv_src(j, n, uo, un);
        uint32_t sz = s >= 0 ? 2u + (uint32_t)__builtin_popcount(sng_u16(q + off[s])) : 2u;
        if (w + sz > room)
            return 0;
        if (s >= 0)
            memcpy(out + w, q + off[s], sz);
        else
            out[w] = out[w + 1] = 0;
        w += sz;
    }
    out[0] = (uint8_t)m;
    return w;
}
/* the song loaded, packed again with its header as the knobs left it (tempo, kit, swing, drum level and reverb) and
 * section k taken from the drum track (its hits, levels, ratchets and marks, its
 * length, division and fill conditions; its name, flags and meter kept; its fill bar of its own, entry and break kept
 * as they are, or on the new grid when the division changed: sng_pack_rediv), into out (room bytes): its size, 0 = it
 * does not fit (SNG_MAX). Nudges and locks are not kept: SNG2 has no room for them (sng_lossy says so) */
static uint32_t sng_pack_section(uint32_t k, uint8_t *out, uint32_t room)
{
    const track_t *t = TDRUM;
    uint32_t j, n = SNG_HDR, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), nf = (len + 3u) / 4u;
    if (room > SNG_MAX)
        room = SNG_MAX;
    if (sng_cur < 0 || k >= sng.nsec || room < SNG_HDR)
        return 0;
    memcpy(out, sng.xip, SNG_HDR);
    out[16] = (uint8_t)song.g[G_BPM];                /* the song's header as the knobs left it (2026-10-29: LIVE's tempo,
                                                      * kit, drum level and swing were never saved; the name and the
                                                      * swing grid as they were) */
    out[17] = (uint8_t)(song.g[G_BPM] >> 8);
    out[18] = (uint8_t)clamp(TDRUM->p[P_E0], 0, DRUM_KITS - 1);
    out[19] = (uint8_t)clamp(song.g[G_SWING], 0, 100);
    out[20] = (uint8_t)clamp(song.g[G_DRLVL], 0, 127);
    out[21] = (uint8_t)clamp(song.g[G_DRREV], 0, 127);
    for (j = 0; j < sng.nsec; j++) {
        const uint8_t *s = sng_sec(j);
        uint32_t size = sng_u16(s + 16), i, at = n;
        if (j != k) {                            /* the other sections as they are */
            if (n + size > room)
                return 0;
            memcpy(out + n, s, size);
            n += size;
            continue;
        }
        if (n + SNG_SEC_HDR + nf > room)
            return 0;
        memcpy(out + n, s, SNG_NAME);            /* name */
        out[n + 12] = (uint8_t)len;
        out[n + 13] = (uint8_t)(t->p[P_SDIV] % NDIV_STEP);
        out[n + 14] = s[14];                     /* flags (the outro) */
        out[n + 15] = s[15];                     /* meter */
        memcpy(out + n + SNG_SEC_HDR, t->fill, nf);
        n += SNG_SEC_HDR + nf;
        for (i = 0; i < len; i++) {              /* each step: its lanes, then a byte a hit */
            const dstep_t *d = &t->dstep[i];
            uint32_t on = dstep_mask(d), l;
            if (n + 2u + (uint32_t)__builtin_popcount(on) > room)
                return 0;
            out[n++] = (uint8_t)on;
            out[n++] = (uint8_t)(on >> 8);
            for (l = 0; on; on >>= 1, l++)
                if (on & 1u)
                    out[n++] = (uint8_t)(dstep_lvl(d, l) | dstep_rat(d, l) << 2 | ((drum_fonly[i] >> l) & 1u) << 4 |
                                         ((drum_fno[i] >> l) & 1u) << 5 | ((drum_flam[i] >> l) & 1u) << 6);
        }
        if (s[14] & SNG_F_TAIL) {                /* its fill bar of its own, entry and break, as they are (the device
                                                  * edits the pattern), or on the new grid */
            uint32_t from = sng_walk(s, SNG_SEC_HDR + ((uint32_t)s[12] + 3u) / 4u, s[12], size);
            uint32_t odiv = s[13] % NDIV_STEP, ndiv = (uint32_t)t->p[P_SDIV] % NDIV_STEP;
            if (!from || from >= size)
                return 0;
            if (odiv == ndiv) {
                if (n + size - from > room)
                    return 0;
                memcpy(out + n, s + from, size - from);
                n += size - from;
            } else {                             /* the division changed on the device: each block re-gridded */
                uint32_t f, uo = div_u48(odiv), un = div_u48(ndiv), b = 12u * meter_b16(s[15]), bar = b % un ? 0u : b / un;
                for (f = SNG_F_FILLBAR; f <= SNG_F_BREAK; f <<= 1) {
                    uint32_t w;
                    if (!(s[14] & f))
                        continue;
                    w = sng_pack_rediv(s + from + 1u, s[from], uo, un, bar, out + n, room - n);
                    if (!w)
                        return 0;
                    n += w;
                    from = sng_walk(s, from + 1u, s[from], size);   /* (checked at the load) */
                }
            }
        }
        out[at + 16] = (uint8_t)(n - at);        /* its size */
        out[at + 17] = (uint8_t)((n - at) >> 8);
    }
    return n;
}
static int sng_lossy(void)                       /* the drum track holds what a packed section cannot keep */
{
    const track_t *t = TDRUM;
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (t->micro[i])
            return 1;
    for (i = 0; i < NLOCK; i++)
        if (t->lock[i].step != LOCK_FREE)
            return 1;
    return 0;
}

/* a song (0..15 a user slot, 16..31 a groove) loaded, or none (SNG_ALL and on): the main loop, for sng_req; also
 * the host tests */
static int sng_load(uint32_t slot)
{
    sng_t tmp;
    const uint8_t *p;
    uint32_t n;
    if (slot >= SNG_ALL) {
        fm1_irq_off();
        sng_unload();                            /* no song: the drum track keeps its pattern, no sections */
        sng.pend = 0;
        fm1_irq_on();
        pedal_say(PU_NOSONG, "");
        return 0;
    }
    p = sng_fetch(slot, &n);
    if (!p || sng_check(p, n, &tmp)) {
        char b[4];
        b[0] = 'F';                              /* "NO SONG 12": the slot as the screens count it, 1..16 (it said
                                                  * '0' + slot: 0-based, and ':' and on past 9); a groove "F12" */
        fmt_int(b + (slot >= SNG_FAC0), (int32_t)(slot % SNG_SLOTS) + 1);
        pedal_say(PU_NOSONG, b);
        return 1;
    }
    fm1_irq_off();
    sng = tmp;
    sng_cur = (int8_t)slot;
    live_req = -1;                               /* a section asked for before: it was the old song's */
    pedal_end_req = 0;
    sng_ui_next[0] = 0;
    {
        uint32_t i;
        for (i = 0; i < SNG_NAME && p[4 + i]; i++)
            sng_ui_song[i] = (char)p[4 + i];
        sng_ui_song[i] = 0;
    }
    if (!song.playing) {                         /* cued: its tempo and kit now, its first section in the drum track */
        sng_globals();
        sng_apply(0);
        live_sec = 0;
    } else {
        sng.pend = 1;                            /* with the next section, on the bar */
    }
    fm1_irq_on();
    sng_lib_dirty = 1;                           /* (the SONGS list: its copy may have moved, editor.c BK_PUT) */
    if (sng_autoload && sng_last != slot + 1u) {
        sng_last = (uint8_t)(slot + 1u);         /* (the one to load at the next start: saved once stopped) */
        arrset_dirty = 1;
    }
    pedal_say(PU_SONG, sng_ui_song);
    return 0;
}
static void sng_poll(void)                       /* main loop */
{
    uint32_t r;
    if (!sng_req)
        return;
    fm1_irq_off();                               /* (a PC landing between the read and the clear is kept) */
    r = sng_req;
    sng_req = 0;
    fm1_irq_on();
    (void)sng_load(r == 0xFFu ? SNG_ALL : r - 1u);
}

/* ---- the pedal settings: a small object of their own (OBJ_ARRSET), set by the tool (backup object 24) */
#define ARRSET_MAGIC 0x31535241u                 /* "ARS1" */
typedef struct {
    uint32_t magic;
    uint8_t pedal_ch;                            /* 0 off, 1..16, 17 omni */
    uint8_t count_in;                            /* bars of count-in before a start from stopped: 0, 1, 2 */
    uint8_t autoload;                            /* SETUP: the last song loaded at start (0 in the tool's and the editor's) */
    uint8_t last;                                /* that song + 1 (a groove 17..32), 0 none */
    uint8_t rsv[8];
} arrset_t;
static int arrset_valid(const arrset_t *a)
{
    return a->magic == ARRSET_MAGIC && a->pedal_ch <= PEDAL_CH_OMNI && a->count_in <= PEDAL_COUNT_MAX;
}
static void arrset_fill(arrset_t *a)
{
    memset(a, 0, sizeof *a);
    a->magic = ARRSET_MAGIC;
    a->pedal_ch = pedal.ch;
    a->count_in = pedal.count;
    a->autoload = sng_autoload;
    a->last = sng_last;
}
static void arrset_take(const arrset_t *a)
{
    pedal.ch = a->pedal_ch;
    pedal.count = a->count_in;
    sng_autoload = (uint8_t)(a->autoload == 1u);
    sng_last = (uint8_t)(a->last <= SNG_ALL ? a->last : 0u);
}
#if FELUCCA_FLASH
static void arrset_boot(void)                    /* the pedal settings; the last song asked for (sng_poll loads it) */
{
    arrset_t a;
    if (flash_ok && st_load(OBJ_ARRSET, &a, sizeof a) == (int)sizeof a && arrset_valid(&a))
        arrset_take(&a);
    if (sng_autoload && sng_last)
        sng_req = sng_last;
}
static void arrset_save(void)                    /* main loop, stopped (project.c sections_flush) */
{
    arrset_t a;
    arrset_fill(&a);
    if (flash_ok)
        (void)st_save(OBJ_ARRSET, &a, sizeof a);
}
#endif
