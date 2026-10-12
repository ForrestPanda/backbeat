/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-arranger: songs. A song is up to 8 named drum sections (intro, verse, chorus, bridge, solo,
 * outro...), each a drum pattern (16 lanes, 1..64 steps, a level and a ratchet per hit, the fill
 * conditions), with the song's tempo, kit, swing, drum level and reverb. Up to 8 songs live in flash
 * (storage.c OBJ_SONG0.., A/B sectors 0xA0000..: the USR1 sample slot's place, given up: the arranger keeps
 * USR2 and the kit pair USR3+4; editor.c refuses sample uploads into USR1), packed; as the FM6 bank, a song is read in place through
 * the plain XIP window (the device has little RAM left): loading it only checks the packed form and keeps a
 * pointer and the sections' offsets. A section starts on the bar as SLOOP's live sections do (seq.c
 * live_block: sng_apply instead of arrangement_apply while a song is loaded): the drum track takes its
 * pattern, the synth tracks keep theirs. The pedal's PC n loads song n (sng_req, taken by sng_poll in the
 * main loop: flash); stopped, the song's tempo and kit are set and its first section cued at once;
 * playing, they come with the next section.
 *
 * The packed song (little endian):
 *   header (32)   "SNG1", name[12], bpm u16, kit, swing, drum level, drum reverb, nsec, rsv[9]
 *   per section   name[12], len (1..64), div (N_SDIV index), flags (bit 0: the outro; bit 1: lane marks follow), rsv;
 *                 fill[(len + 3) / 4] (2 bits a step: FC_*); len x dstep_t (10 bytes: lanes u16, lvl[4], rat[4]);
 *                 with bit 1: len x (u16 "only during a fill" lanes, u16 "not during a fill" lanes)
 * tools/fm1_song.py writes one from a JSON file and sends it (EDITOR_PROTOCOL.md: BK_PUT object 16 + slot).
 * The pedal settings (the channel) are a small object of their own (OBJ_ARRSET, backup object 24).
 * Included from arranger_scene.c (the firmware through project.c, the host tests directly). */
#define SNG_MAGIC 0x31474E53u                    /* "SNG1" (SNG_SLOTS, SNG_SECTIONS: seq.c) */
#define SNG_NAME 12u
#define SNG_HDR 32u
#define SNG_SEC_HDR 16u
#define SNG_MAX 3800u                            /* a packed song (<= proj_tmp, the staging RAM; <= a sector's payload) */
#define SNG_F_OUTRO 1u
#define SNG_F_MARKS 2u                           /* the section's steps carry lane marks (seq.c drum_fonly / drum_fno) */

typedef struct {
    const uint8_t *xip;                          /* the packed song: flash (XIP), or a RAM blob on the host */
    uint16_t off[SNG_SECTIONS];                  /* each section's offset in it (nsec of them) */
    uint16_t bpm;
    uint8_t kit, swing, drlvl, drrev, nsec, pend;   /* pend: tempo and kit wait for the next section */
} sng_t;
static sng_t sng;

static uint32_t sng_u16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }

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
    out->drlvl = b[20];
    out->drrev = b[21];
    out->nsec = b[22];
    if (out->bpm < 40u || out->bpm > 240u || out->kit >= DRUM_KITS || out->swing > 100u ||
        !out->nsec || out->nsec > SNG_SECTIONS)
        return 3;
    for (k = 0; k < out->nsec; k++) {
        uint32_t len, div;
        if (pos + SNG_SEC_HDR > n)
            return 4;
        len = b[pos + 12];
        div = b[pos + 13];
        if (!len || len > NSTEP || div >= NDIV_STEP)
            return 5;
        out->off[k] = (uint16_t)pos;
        pos += SNG_SEC_HDR + (len + 3u) / 4u + len * sizeof(dstep_t);
        if (b[out->off[k] + 14] & SNG_F_MARKS)
            pos += len * 4u;
        if (pos > n)
            return 6;
    }
    return 0;
}

static const uint8_t *sng_sec(uint32_t k) { return sng.xip + sng.off[k % SNG_SECTIONS]; }
static uint32_t sng_ready(void)
{
    return sng_cur >= 0 ? (1u << sng.nsec) - 1u : 0u;
}
static uint32_t sng_bars(uint32_t k)             /* as section_bars: the bars its pattern takes */
{
    const uint8_t *s;
    uint32_t len, u, bars;
    if (sng_cur < 0 || k >= sng.nsec)
        return 1;
    s = sng_sec(k);
    len = s[12];
    u = div_units((uint32_t)s[13] % NDIV_STEP);
    bars = (len * u + 4u * BEAT_U - 1u) / (4u * BEAT_U);
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
    const uint8_t *s;
    uint32_t len, nf;
    if (sng_cur < 0 || k >= sng.nsec)
        return;
    s = sng_sec(k);
    len = s[12];
    nf = (len + 3u) / 4u;
    seq_release(t);
    trk_all_off(t);
    t->nheld = t->arp_phys = t->arp_note = t->rh_n = t->rskip_n = 0;
    t->rskip_lanes = 0;
    locks_clear(t);                              /* (no lock, no nudge, no condition: then the section's) */
    memcpy(t->dstep, s + SNG_SEC_HDR + nf, len * sizeof(dstep_t));
    memcpy(t->fill, s + SNG_SEC_HDR, nf);
    drum_fill_masks_clear();
    if (s[14] & SNG_F_MARKS) {                    /* the lane marks, little endian: only-during, not-during a fill */
        const uint8_t *m = s + SNG_SEC_HDR + nf + len * sizeof(dstep_t);
        uint32_t i;
        for (i = 0; i < len; i++) {
            drum_fonly[i] = (uint16_t)(m[4u * i] | m[4u * i + 1u] << 8);
            drum_fno[i] = (uint16_t)(m[4u * i + 2u] | m[4u * i + 3u] << 8);
        }
    }
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SDIV] = (int16_t)(s[13] % NDIV_STEP);
    sng_name(k, sng_ui_name);
    if (sng.pend)
        sng_globals();
    sync_reload = 1;
    ui.force = 1;
}

/* the song's packed form from flash: the current copy, read in place (0: none) */
#if FELUCCA_FLASH
static const uint8_t *sng_fetch(uint32_t slot, uint32_t *len)
{
    st_hdr_t h;
    int c;
    *len = 0;
    if (!flash_ok || slot >= SNG_SLOTS)
        return 0;
    c = st_current(OBJ_SONG0 + slot, &h);
    if (c < 0 || !h.len)
        return 0;
    *len = h.len;
    return fm1_xip_ptr(st_sector(OBJ_SONG0 + slot, (uint32_t)c) + ST_PAYLOAD_OFF);
}
#elif !defined(SNG_HOST_FETCH)
static const uint8_t *sng_fetch(uint32_t slot, uint32_t *len) { (void)slot; *len = 0; return 0; }
#endif

/* a song (slot) loaded, or none (0xFF): the main loop, for sng_req; also the host tests */
static int sng_load(uint32_t slot)
{
    sng_t tmp;
    const uint8_t *p;
    uint32_t n;
    if (slot >= SNG_SLOTS) {
        fm1_irq_off();
        sng_unload();                            /* SLOOP's sections A..D again; the drum track keeps its pattern */
        sng.pend = 0;
        fm1_irq_on();
        pedal_say(PU_NOSONG, "");
        return 0;
    }
    p = sng_fetch(slot, &n);
    if (!p || sng_check(p, n, &tmp)) {
        char b[4] = {'0' + (char)slot, 0, 0, 0};
        pedal_say(PU_NOSONG, b);
        return 1;
    }
    fm1_irq_off();
    if (ft_on)
        transport_req = 2;                       /* (a free take on the drum track: dropped, as a project load) */
    sng = tmp;
    sng_cur = (int8_t)slot;
    live_req = -1;                               /* a section asked for before: it was the old song's */
    pedal_wait = 0;
    pedal_end_req = 0;
    sng_ui_next[0] = 0;
    {
        uint32_t i;
        for (i = 0; i < SNG_NAME && p[4 + i]; i++)
            sng_ui_song[i] = (char)p[4 + i];
        sng_ui_song[i] = 0;
    }
    if (!song.playing) {                         /* cued: its tempo and kit now, its first section in the drum track */
        chain_n = 0;
        sng_globals();
        sng_apply(0);
        live_sec = 0;
    } else {
        sng.pend = 1;                            /* with the next section, on the bar */
    }
    fm1_irq_on();
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
    (void)sng_load(r == 0xFFu ? SNG_SLOTS : r - 1u);
}

/* ---- the pedal settings: a small object of their own (OBJ_ARRSET), set by the tool (backup object 24) */
#define ARRSET_MAGIC 0x31535241u                 /* "ARS1" */
typedef struct {
    uint32_t magic;
    uint8_t pedal_ch;                            /* 0 off, 1..16, 17 omni */
    uint8_t rsv[11];
} arrset_t;
static int arrset_valid(const arrset_t *a) { return a->magic == ARRSET_MAGIC && a->pedal_ch <= PEDAL_CH_OMNI; }
static void arrset_fill(arrset_t *a)
{
    memset(a, 0, sizeof *a);
    a->magic = ARRSET_MAGIC;
    a->pedal_ch = pedal.ch;
}
static void arrset_take(const arrset_t *a) { pedal.ch = a->pedal_ch; }
#if FELUCCA_FLASH
static void arrset_boot(void)
{
    arrset_t a;
    if (flash_ok && st_load(OBJ_ARRSET, &a, sizeof a) == (int)sizeof a && arrset_valid(&a))
        arrset_take(&a);
}
#endif
