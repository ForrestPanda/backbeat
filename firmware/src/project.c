/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in .noinit RAM (the song sections A..D), so they survive resets and UBOOT
 * entry. With FELUCCA_FLASH every save also goes to flash through storage.c, and an empty RAM slot
 * is filled from flash on load. The working project is also kept in flash by itself (autosave, when
 * the transport is stopped and nothing sounds) and comes back at power-on: SLOOP starts where you
 * left it.
 *
 * Format DRM1 (written, fm1-drummer drums only): the globals, the drum track (its parameters, pattern, nudges and
 * conditions, locks), as format 6's drum track was stored. Read and converted, the drum track only (SLOOP's three
 * synth parts are gone): 6 ("FUN6", SLOOP 2.6: format 5 with the ten parameters of 2.6 in a byte each and each step's
 * nudge and fill condition in one byte), 5 ("FUN5", SLOOP 2.4 .. 2.5: per step a nudge, the parameter locks, the fill
 * conditions; by id, the 2.6 parameters at their defaults, a lock on P_E0.. moved with it), 4 ("FUN4", SLOOP 2.0 ..
 * 2.3: no nudge, no lock). Formats 1..3 (SLOOP 1.x and before: no drum lanes yet) are not read any more: SLOOP 2.x
 * converts them.
 *
 * Built on the host too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP, e_desc) and drums.c. */
#define PROJ_MAGIC 0x44524D31u                 /* "DRM1": the drum track */
#define PROJ_MAGIC_V6 0x46554E36u              /* "FUN6": SLOOP 2.6, four tracks; read only */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": SLOOP 2.4 .. 2.5; read only */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": SLOOP 2.0 .. 2.3; read only */
#define PROJ_NTRK_OLD 4u                       /* SLOOP's tracks in formats 4..6: three synth parts, */
#define PROJ_DRUM_OLD 3u                       /* then the drum track */
#define PROJ_NP_V5 61u                         /* P_COUNT of format 5 (P_E0 was 53) */
#define PROJ_NP_V4 58u                         /* P_COUNT of format 4 (P_E0 was 50) */
#define PROJ_NG 32u                            /* the globals of formats 4..6 and DRM1 (G_COUNT until 2.4). SLOOP 2.5's
                                                * G_DRDLY rides in a byte of its own */
_Static_assert(G_DRDLY == PROJ_NG && G_TXSRC == PROJ_NG + 5u && G_COUNT == G_BEND + 1u,
               "the globals a project holds, then G_DRDLY (then 2.6's SETUP > MIDI: settings of the FM-1)");
#define PROJ_NC6 ((uint32_t)P_ITYPE)           /* the parameters kept in 16 bits, P_LEVEL .. P_VLEAD (as format 5), */
#define PROJ_NX6 ((uint32_t)(P_E0 - P_ITYPE))  /* the 2.6 ones in a byte (0..127), then P_E0 .. P_E7 */
_Static_assert(PROJ_NC6 == PROJ_NP_V5 - 8u && PROJ_NX6 == 10u, "format 6: format 5's parameters, then ten bytes");
typedef struct {                               /* one track (format 6; DRM1's drum track the same) */
    int16_t p[PROJ_NC6];                       /* P_LEVEL .. P_VLEAD (pt_get, pt_set) */
    int16_t e[8];                              /* P_E0 .. P_E7 */
    uint8_t x[PROJ_NX6];                       /* P_ITYPE .. P_LPOL (2.6) */
    uint8_t engine, preset;                    /* (format 6's synth parts; DRM1: 0) */
    union {
        step_t step[NSTEP];                    /* (format 6's synth parts) */
        dstep_t dstep[NSTEP];                  /* the drum track: 16 lanes, the same size */
    };
    uint8_t mf[NSTEP];                         /* each step's nudge (core.h MICRO_*, 6 bits) | its fill condition << 6 */
    plock_t lock[NLOCK];                       /* and its parameter locks (step LOCK_FREE = none) */
} proj_trk_t;
typedef struct {                               /* DRM1 */
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t drdly, rsv[3];                     /* G_DRDLY (SLOOP 2.5); rsv[0..1]: SETUP > TEXTURE (proj_tx_*) */
    proj_trk_t t;                              /* the drum track */
    uint32_t sum;
} project_t;
static int16_t pt_get(const proj_trk_t *t, uint32_t id)   /* p[id] of a stored track */
{
    return id < PROJ_NC6 ? t->p[id] : id < P_E0 ? (int16_t)t->x[id - PROJ_NC6] : t->e[(id - P_E0) & 7u];
}
static void pt_set(proj_trk_t *t, uint32_t id, int32_t v)
{
    if (id < PROJ_NC6)
        t->p[id] = (int16_t)v;
    else if (id < P_E0)
        t->x[id - PROJ_NC6] = (uint8_t)clamp(v, 0, 255);
    else
        t->e[(id - P_E0) & 7u] = (int16_t)v;
}
static int32_t pt_micro(const proj_trk_t *t, uint32_t i) { int32_t m = t->mf[i] & 63; return m >= 32 ? m - 64 : m; }
static uint32_t pt_fill(const proj_trk_t *t, uint32_t i) { return (uint32_t)t->mf[i] >> 6; }
static void pt_set_mf(proj_trk_t *t, uint32_t i, int32_t micro, uint32_t fill)
{
    t->mf[i] = (uint8_t)(((uint32_t)micro & 63u) | (fill & 3u) << 6);
}
typedef struct {                               /* format 6 (SLOOP 2.6), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, drdly, rsv[2];                /* (the selected track); 2.5: G_DRDLY; 2.6: the TEXTURE */
    proj_trk_t t[PROJ_NTRK_OLD];
    uint32_t sum;
} project_v6_t;
typedef struct {                               /* a track of format 5 (SLOOP 2.4 .. 2.5), read only */
    int16_t p[PROJ_NP_V5];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];
    };
    int8_t micro[NSTEP];                       /* each step's nudge (core.h) */
    plock_t lock[NLOCK];                       /* and its parameter locks (step LOCK_FREE = none) */
    uint8_t fill[NSTEP / 4];                   /* and its fill condition, 2 bits a step (FC_*) */
} proj_trk_v5_t;
typedef struct {                               /* format 5, read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, drdly, rsv[2];
    proj_trk_v5_t t[PROJ_NTRK_OLD];
    uint32_t sum;
} project_v5_t;
typedef struct {                               /* a track of format 4 (SLOOP 2.0 .. 2.3), read only */
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];
    };
} proj_trk_v4_t;
typedef struct {                               /* format 4, read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, rsv[3];
    proj_trk_v4_t t[PROJ_NTRK_OLD];
    uint32_t sum;
} project_v4_t;
_Static_assert(sizeof(project_v4_t) == 3112u && sizeof(project_v5_t) == 3840u && sizeof(project_v6_t) == 3816u,
               "formats 4 / 5 / 6 as they were stored");
_Static_assert(sizeof(project_t) <= 3840u, "a project fits one flash object (storage.c ST_PAYLOAD_MAX)");
project_t proj_slot[4] __attribute__((section(".noinit")));

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }

/* ---- formats 4..6 -> DRM1: the drum track and the globals */
/* no nudge, no lock, no condition (format 4); the 2.6 parameters at their defaults (formats 4, 5) */
static void proj_trk_plain(proj_trk_t *d)
{
    uint32_t k;
    memset(d->mf, 0, sizeof d->mf);
    for (k = PROJ_NC6; k < P_E0; k++)
        pt_set(d, k, TP[k].def);
    for (k = 0; k < NLOCK; k++) {
        d->lock[k].step = LOCK_FREE;
        d->lock[k].param = 0;
        d->lock[k].val = 0;
    }
}
static void proj_new(project_t *q, const int16_t *g)   /* q: DRM1, the globals g, an empty drum track (then filled) */
{
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, g, sizeof q->g);
    proj_trk_plain(&q->t);
}

/* format 6 (n bytes in *v6) -> q: the drum track as it was, the globals, G_DRDLY and the TEXTURE */
static int proj_from_v6(project_t *q, const project_v6_t *v6, int n)
{
    if (n != (int)sizeof *v6 || v6->magic != PROJ_MAGIC_V6 || v6->size != sizeof *v6 ||
        v6->sum != proj_hash(v6, sizeof *v6 - 4u))
        return 0;
    proj_new(q, v6->g);
    q->drdly = v6->drdly;
    q->rsv[0] = v6->rsv[0];
    q->rsv[1] = v6->rsv[1];
    q->t = v6->t[PROJ_DRUM_OLD];
    q->t.engine = q->t.preset = 0;
    q->sum = proj_sum(q);
    return 1;
}

/* format 5 -> q: by id, the 2.6 parameters at their defaults, each step's nudge and condition into its byte, the locks
 * as they were (a lock on P_E0.. follows its parameter's new id) */
static int proj_from_v5(project_t *q, const project_v5_t *v5, int n)
{
    const proj_trk_v5_t *s = &v5->t[PROJ_DRUM_OLD];
    proj_trk_t *d = &q->t;
    uint32_t k;
    if (n != (int)sizeof *v5 || v5->magic != PROJ_MAGIC_V5 || v5->size != sizeof *v5 ||
        v5->sum != proj_hash(v5, sizeof *v5 - 4u))
        return 0;
    proj_new(q, v5->g);
    q->drdly = v5->drdly;
    for (k = 0; k < PROJ_NC6; k++)
        d->p[k] = s->p[k];
    for (k = 0; k < 8u; k++)
        d->e[k] = s->p[PROJ_NP_V5 - 8u + k];
    memcpy(d->dstep, s->dstep, sizeof d->dstep);
    for (k = 0; k < NSTEP; k++)
        pt_set_mf(d, k, clamp(s->micro[k], MICRO_MIN, MICRO_MAX), (uint32_t)s->fill[k / 4u] >> (2u * (k % 4u)));
    for (k = 0; k < NLOCK; k++) {
        d->lock[k] = s->lock[k];
        if (s->lock[k].step != LOCK_FREE && s->lock[k].param >= PROJ_NP_V5 - 8u)   /* P_E0.. of format 5 */
            d->lock[k].param = (uint8_t)(s->lock[k].param - (PROJ_NP_V5 - 8u) + P_E0);
    }
    q->sum = proj_sum(q);
    return 1;
}

/* format 4 -> q: the same, no nudge, no lock, no condition, the parameters added since at their defaults */
static int proj_from_v4(project_t *q, const project_v4_t *v4, int n)
{
    const proj_trk_v4_t *s = &v4->t[PROJ_DRUM_OLD];
    uint32_t k;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    proj_new(q, v4->g);
    for (k = 0; k < PROJ_NC6; k++)                     /* by id up to P_CHORD, the parameters added since: */
        q->t.p[k] = k < PROJ_NP_V4 - 8u ? s->p[k] : TP[k].def;   /* their defaults; P_E0.. moved */
    for (k = 0; k < 8u; k++)
        q->t.e[k] = s->p[PROJ_NP_V4 - 8u + k];
    memcpy(q->t.dstep, s->dstep, sizeof q->t.dstep);
    q->sum = proj_sum(q);
    return 1;
}

/* n bytes of a stored project (DRM1, or formats 4..6 converted) -> slot q; 0 = not a project */
static int proj_import(project_t *q, const void *b, int n)
{
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        return 1;
    }
    return proj_from_v6(q, (const project_v6_t *)b, n) || proj_from_v5(q, (const project_v5_t *)b, n) ||
           proj_from_v4(q, (const project_v4_t *)b, n);
}

/* ---- the working project <-> a project_t */
/* 2.6: SETUP > TEXTURE in a project (a song section has its own): in the slots of two globals a project no longer
 * applies (G_DRCH: a setting of the FM-1 now; G_INFO: the CPU readout) and the spare bytes. rsv[0]: 0x80 (there) | SRC
 * | WHEN << 4 | DUCK << 5, rsv[1]: RATE, g[G_DRCH]: LEVEL | (TONE + 64) << 7, g[G_INFO]: MOVE. Without the 0x80 (a
 * project before it, converted): the defaults, OFF */
static void proj_tx_put(project_t *p)
{
    p->rsv[0] = (uint8_t)(0x80u | ((uint32_t)song.g[G_TXSRC] & 15u) | ((uint32_t)song.g[G_TXWHEN] & 1u) << 4 |
                          ((uint32_t)song.g[G_TXDUCK] & 1u) << 5);
    p->rsv[1] = (uint8_t)song.g[G_TXRATE];
    p->g[G_DRCH] = (int16_t)((song.g[G_TXLVL] & 127) | ((song.g[G_TXTONE] + 64) & 127) << 7);
    p->g[G_INFO] = song.g[G_TXMOVE];
}
static void proj_tx_get(const project_t *p)
{
    static const uint8_t ID[7] = {G_TXSRC, G_TXLVL, G_TXTONE, G_TXMOVE, G_TXRATE, G_TXWHEN, G_TXDUCK};
    int32_t v[7];
    uint32_t i;
    if (!(p->rsv[0] & 0x80u)) {
        for (i = 0; i < 7u; i++)
            v[i] = GP[ID[i]].def;
    } else {
        v[0] = p->rsv[0] & 15u, v[5] = (p->rsv[0] >> 4) & 1u, v[6] = (p->rsv[0] >> 5) & 1u, v[4] = p->rsv[1];
        v[1] = p->g[G_DRCH] & 127, v[2] = ((p->g[G_DRCH] >> 7) & 127) - 64, v[3] = p->g[G_INFO];
    }
    for (i = 0; i < 7u; i++)
        song.g[ID[i]] = (int16_t)clamp(v[i], GP[ID[i]].min, GP[ID[i]].max);
}

static void proj_capture(project_t *p)        /* what is playing now, as a project */
{
    const track_t *t = TDRUM;
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < PROJ_NG; i++)
        p->g[i] = song.g[i];
    p->drdly = (uint8_t)song.g[G_DRDLY];
    proj_tx_put(p);
    for (i = 0; i < P_COUNT; i++)
        pt_set(&p->t, i, t->p[i]);
    memcpy(p->t.dstep, t->dstep, sizeof t->dstep);
    for (i = 0; i < NSTEP; i++)
        pt_set_mf(&p->t, i, t->micro[i], (uint32_t)t->fill[i / 4u] >> (2u * (i % 4u)));
    memcpy(p->t.lock, t->lock, sizeof t->lock);
    p->sum = proj_sum(p);
}

/* a project's drum track (and its globals, all: a load; or only the drum level / reverb / delay: a song
 * section) into the working one, every value back inside its range. The audio ISR must not run
 * meanwhile (the song sections: called from it; a load: IRQ off) */
static void proj_apply(const project_t *p, int all)
{
    track_t *t = TDRUM;
    const proj_trk_t *s = &p->t;
    uint32_t i;
    for (i = 0; i < PROJ_NG; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_SYNC && i != G_MIDI && i != G_ROUTE && i != G_DRCH &&
                      i != G_INFO
                : i == G_DRLVL || i == G_DRREV)                 /* (2.6: the drum channel a setting of the FM-1, SETUP > MIDI;
                                                                 * its slot and G_INFO's hold the TEXTURE) */
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    song.g[G_DRDLY] = (int16_t)clamp(p->drdly, GP[G_DRDLY].min, GP[G_DRDLY].max);   /* (a load and a section, as REV) */
    proj_tx_get(p);                                     /* SETUP > TEXTURE (2.6): a load and a section */
    t->lk_n = 0;                                        /* (the locks in force: the values come from the project) */
    for (i = 0; i < P_COUNT; i++) {                     /* every value back inside its range */
        const param_desc_t *d = i >= P_E0 ? e_desc(t, i) : &TP[i];   /* (P_E0: the kit) */
        t->p[i] = (int16_t)clamp(pt_get(s, i), d->min, d->max);
    }
    memcpy(t->dstep, s->dstep, sizeof t->dstep);
    memset(t->fill, 0, sizeof t->fill);
    for (i = 0; i < NSTEP; i++) {                       /* the nudges and conditions (3 means normal: 0) */
        uint32_t c = pt_fill(s, i);
        t->micro[i] = (int8_t)clamp(pt_micro(s, i), MICRO_MIN, MICRO_MAX);
        if (c != 3u)
            t->fill[i / 4u] |= (uint8_t)(c << (2u * (i % 4u)));
    }
    for (i = 0; i < NLOCK; i++) {
        const plock_t *l = &s->lock[i];
        uint32_t id = l->param;
        if (l->step < NSTEP && id < P_COUNT && p_lockable(id)) {
            const param_desc_t *d = id >= P_E0 ? e_desc(t, id) : &TP[id];
            t->lock[i].step = l->step;
            t->lock[i].param = (uint8_t)id;
            t->lock[i].val = (int16_t)clamp(l->val, d->min, d->max);
        } else {
            t->lock[i].step = LOCK_FREE;                /* no such step or parameter: the slot is free */
            t->lock[i].param = 0;
            t->lock[i].val = 0;
        }
    }
}

#ifndef PROJ_HOST
#if FELUCCA_DRUMMER
#include "arranger_scene.c"
#endif
static uint8_t sec_dirty, song_dirty;           /* live sections / the song: in RAM, not yet in flash */
#if FELUCCA_FLASH
/* slot from flash into RAM (DRM1, or a SLOOP 2.x project's drum track) */
static union {
    project_t drm;
    project_v6_t v6;
    project_v5_t v5;
    project_v4_t v4;
} proj_tmp;
#if FELUCCA_DRUMMER
/* EDIT, SAVE held (ui_input.c): the section playing written into its song, packed again in the staging RAM
 * (sng.c sng_pack_section); stopped only (a flash write stops the audio). The song's copy moves: indexed again
 * at once, the drum track keeps what it holds. 0 saved, 1 playing, 2 no room (SNG_MAX), 3 flash, 4 no song, 5 a
 * factory groove (read-only: the web editor copies one into a user slot) */
static int sng_save_section(void)
{
    uint32_t slot = (uint32_t)sng_cur, n;
    const uint8_t *p;
    sng_t tmp;
    if (song.playing || transport_req)
        return 1;
    if (sng_cur < 0 || live_sec < 0)
        return 4;
    if (slot >= SNG_FAC0)
        return 5;
    n = sng_pack_section((uint32_t)live_sec, (uint8_t *)&proj_tmp, sizeof proj_tmp);
    if (!n)
        return 2;
    if (sng_check((const uint8_t *)&proj_tmp, n, &tmp) || !flash_ok || st_save(sng_obj(slot), &proj_tmp, n))
        return 3;
    sng_lib_dirty = 1;                                  /* (the SONGS list: its tempo, sections) */
    p = sng_fetch(slot, &n);
    if (!p || sng_check(p, n, &tmp))
        return 3;
    fm1_irq_off();
    sng = tmp;
    fm1_irq_on();
    return 0;
}
#endif
static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        q->magic = 0;
}
#endif

static void project_save(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_DRUMMER
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
#endif
    proj_capture(p);
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p) ? "SAVE ERROR" : "SAVED");
        return;
    }
#endif
    ui_message("SAVED (RAM)");
}

/* SAVE > PROJECT > ERASE (SLOOP 2.6, after a YouTube comment): the slot (section A..D) empty again. In flash as the
 * editor's restore leaves an empty slot (project_restore): an object of no bytes, which proj_fetch (older SLOOP too)
 * reads as no project. The song's steps that played that section are taken out (the default song if none is left),
 * so song mode still plays. Stopped only: a flash erase stops the audio ~50 ms */
static void project_erase(uint32_t slot)
{
    slot &= 3u;
#if FELUCCA_DRUMMER
    if (song.playing || transport_req) { ui_message("STOP BEFORE ERASE"); return; }
#endif
    if (!proj_ok(&proj_slot[slot])) {
        ui_message("EMPTY SLOT");
        return;
    }
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + slot, &proj_slot[slot], 0)) {
        ui_message("ERASE ERROR");
        return;
    }
#endif
    fm1_irq_off();
    memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
    sec_dirty &= (uint8_t)~(1u << slot);
    if (live_sec == (int8_t)slot)
        live_sec = -1;
#if FELUCCA_DRUMMER
    if (arr_drop_scene(&arrangement, slot))
        song_dirty = 1;                                     /* (saved with the settings, when quiet) */
#endif
    fm1_irq_on();
    ui_message("ERASED");
}

/* a project into the working one: the transport stops, everything sounding is released */
static void project_apply(const project_t *p)
{
    transport_req = 2;
    drum_mute = 0;                                      /* (the live mutes: a performance's, not the project's) */
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
#if FELUCCA_DRUMMER
    sng_unload();                                       /* (fm1-drummer: a project over a song: A..D again) */
    drum_fill_masks_clear();
#endif
    proj_apply(p, 1);
    fm1_irq_on();
    sync_reload = 1;
    ui.force = 1;
}

static void project_load(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_DRUMMER
    if (song.playing || transport_req) { ui_message("STOP BEFORE LOAD"); return; }
#endif
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p))
        proj_fetch(slot);
#endif
    if (!proj_ok(p)) {
        ui_message("EMPTY SLOT");
        return;
    }
    project_apply(p);
    ui_message("LOADED");
}

/* ---- the working project, kept in flash by itself: saved when it changed, the transport is stopped,
 * nothing sounds and the panel was not touched for AUTOSAVE_IDLE (a flash erase stops the audio for
 * ~50 ms: never while something plays); loaded at power-on (autosave_resume) */
#define AUTOSAVE_IDLE 2500u                    /* ms without input */
#define AUTOSAVE_GAP 20000u                    /* ms between two saves at least */
static project_t autosave_buf __attribute__((section(".pool")));
static uint32_t autosave_hash, autosave_ms, autosave_checked;

static int audio_quiet(void)
{
    uint32_t i;
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].active)
            return 0;
    return 1;
}

static void autosave_tick(void)                /* main loop */
{
#if FELUCCA_FLASH
    uint32_t h, now = fm1_ms;
    if (!flash_ok || song.playing || transport_req || ui.menu ||
        now - ui_input_ms < AUTOSAVE_IDLE || now - autosave_ms < AUTOSAVE_GAP || now - autosave_checked < 1000u)
        return;
    autosave_checked = now;
    proj_capture(&autosave_buf);
    h = autosave_buf.sum;
    if (h == autosave_hash || !audio_quiet())
        return;
    if (st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
        autosave_hash = h;
    autosave_ms = fm1_ms;
#endif
}

static uint8_t drch_seed;                           /* a settings object as 2.5 wrote it: the drum channel was the
                                                     * project's then (autosave_resume takes it from a 2.5 autosave) */
static void autosave_resume(void)              /* power-on: the project as it was left (felucca_init) */
{
#if FELUCCA_FLASH
    project_t *q = &autosave_buf;
    int n;
    if (!flash_ok)
        return;
    n = st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        return;
    autosave_hash = q->sum;
    if (drch_seed && !(q->rsv[0] & 0x80u) && q->g[G_DRCH] >= 0 && q->g[G_DRCH] <= 16 && q->g[G_DRCH] != midi_set[3]) {
        midi_set[3] = (uint8_t)q->g[G_DRCH];          /* (2.6) updated from 2.5: SETUP > DRUMS > CH as it was there (a
                                                     * project's then, a setting of the FM-1 now), kept in the settings */
        settings_later = 1;
    }
    drch_seed = 0;
    proj_apply(q, 1);
#endif
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_DRUMMER
    arr_config_t arrangement;
#endif
    uint32_t lights;                               /* SLOOP 2.3: the backlight (panel.c lights_word); appended,
                                                    * so 2.2 still reads its part (st_load cuts at its size) */
} persist_t;
#define PERSIST_SIZE_V22 __builtin_offsetof(persist_t, lights)   /* the settings as 2.2 wrote them (no lights) */
_Static_assert(sizeof(persist_t) == PERSIST_SIZE_V22 + 4u, "lights: the last word, no padding before it");
#if FELUCCA_DRUMMER
_Static_assert(__builtin_offsetof(persist_t, arrangement) == 48u && sizeof(arr_config_t) == 36u,
               "the web editor's Song page edits bytes 48..83 of the settings (web/editor.html ARR)");
#endif
#if FELUCCA_DRUMMER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": includes the song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif
#if FELUCCA_FLASH
static persist_t persist_saved;
static void dsu_boot(void);
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_DRUMMER
    arr_defaults(&arrangement);
#endif
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            if (!SNG_SMP_TAKEN(k))                 /* (fm1-drummer: USR1's and USR2's flash hold the songs: never a slot) */
                smp_user_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (n == (int)PERSIST_SIZE_V22 && p.magic == PERSIST_MAGIC)
            p.lights = 0;                          /* from 2.2: backlight off */
        if (((n == (int)sizeof p || n == (int)PERSIST_SIZE_V22) && p.magic == PERSIST_MAGIC)
#if FELUCCA_DRUMMER
            || (n == (int)(16u + sizeof(panel_t)) && p.magic == 0x50455232u)
#endif
            ) {
            settings.magic = SETTINGS_MAGIC;
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;
            settings.zoom = p.zoom;
            if (p.panel.magic == PANEL_MAGIC)
                panel = p.panel;
            if (p.magic == PERSIST_MAGIC)
                lights_from_word(p.lights);
            else
                p.lights = 0;
#if FELUCCA_DRUMMER
            if (p.magic == PERSIST_MAGIC && arr_valid(&p.arrangement, 15u))
                arrangement = p.arrangement;
            else
                p.arrangement = arrangement;
#endif
            persist_saved = p;
        } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {   /* "PER1": palette, panel */
            const uint32_t *w = (const uint32_t *)&p;
            panel_t old;
            memcpy(&old, w + 2, sizeof old);
            settings.magic = SETTINGS_MAGIC;
            settings.palette = w[1];
            settings.lowcut = 0;
            settings.zoom = 0;
            if (old.magic == PANEL_MAGIC)
                panel = old;
        }
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on. A slot
         * still valid in RAM (a warm reset: an update, UPDATE MODE, a crash) may never have reached
         * flash (a live section stored while playing): marked to be written when quiet */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_ok(&proj_slot[i])) {
                proj_fetch(i);
            } else {
                int n = st_load(OBJ_PROJECT0 + i, &proj_tmp, sizeof proj_tmp);
                if (n != (int)sizeof proj_slot[i] || memcmp(&proj_tmp.drm, &proj_slot[i], sizeof proj_slot[i]))
                    sec_dirty |= (uint8_t)(1u << i);
            }
    }
    dsu_boot();                                    /* SLOOP 2.5: the SYN kits (after the settings) */
    arrset_boot();                                 /* fm1-drummer: the pedal channel (sng.c) */
#endif
}

static int project_used(uint32_t slot) { return proj_ok(&proj_slot[slot & 3u]); }

#if FELUCCA_FLASH
/* SLOOP 2.5: the settings object holds the SYN kits after persist_t (drum_synth.c dsu): firmware before 2.5 reads
 * its persist_t and leaves the rest (st_load cuts at its size); its own next save drops them. 2.6: then the MIDI LEARN
 * map (seq.c ml_tab, ML_N entries), only while something is learned; 2.5 reads an object with the map as one without
 * its SYN kits (their defaults), one without it as its own */
/* the settings object's third part (2.6): the MIDI LEARN map (seq.c ml_tab, ML_N entries, 2 bytes each, low first),
 * then SETUP > MIDI: CH1 CH2 CH3, the drum channel | OTHER << 7, SETUP > MIDI 2's BEND, 3 spare. Written only while it is not
 * the default (nothing learned, channels 1 2 3 10, OTHER SEL, BEND 2): else the object is exactly 2.5's. 2.6 test
 * builds wrote 32 (the map) or 36 bytes (and the channels) */
#define MIDI_PART (2u * ML_N + 8u)
_Static_assert(sizeof(persist_t) + sizeof(dsu_bank_t) + MIDI_PART <= ST_PAYLOAD_MAX, "the settings object fits its sector");
static uint8_t midi_saved[MIDI_PART];
static uint8_t drch_seed;                           /* a settings object as 2.5 wrote it: the drum channel was the
                                                     * project's then (autosave_resume takes it from a 2.5 autosave) */               /* the part as flash has it (settings_save: written when it differs) */
static void midi_part_fill(uint8_t *b)
{
    uint32_t k;
    for (k = 0; k < ML_N; k++)
        b[2u * k] = (uint8_t)ml_tab[k], b[2u * k + 1u] = (uint8_t)(ml_tab[k] >> 8);
    b[2u * ML_N] = midi_set[0], b[2u * ML_N + 1u] = midi_set[1], b[2u * ML_N + 2u] = midi_set[2];
    b[2u * ML_N + 3u] = (uint8_t)(midi_set[3] | midi_set[4] << 7);
    b[2u * ML_N + 4u] = midi_set[5];
    b[2u * ML_N + 5u] = b[2u * ML_N + 6u] = b[2u * ML_N + 7u] = 0;
}
static int midi_part_default(const uint8_t *b)
{
    static const uint8_t def[6] = MIDI_SET_DEF;
    uint32_t k;
    for (k = 0; k < 2u * ML_N; k++)
        if (b[k])
            return 0;
    return b[2u * ML_N] == def[0] && b[2u * ML_N + 1u] == def[1] && b[2u * ML_N + 2u] == def[2] &&
           b[2u * ML_N + 3u] == (uint8_t)(def[3] | def[4] << 7) && b[2u * ML_N + 4u] == def[5];
}
/* n: 32 (the map), 36 (and the channels) or MIDI_PART; every entry checked (a CC that learns, a parameter this knows, a
 * channel 0..16, a range 1..12) */
static void midi_part_read(const uint8_t *b, uint32_t n)
{
    uint32_t k, e;
    fm1_irq_off();                                  /* (the ISR reads the map and the channels: never half of them) */
    for (k = 0; k < ML_N; k++) {
        e = (uint32_t)b[2u * k] | (uint32_t)b[2u * k + 1u] << 8;
        ml_tab[k] = (uint16_t)((e >> 9) && ml_free_cc(e & 127u) && ml_id(e >> 9) < P_COUNT ? e : 0u);
    }
    if (n >= 2u * ML_N + 4u) {
        for (k = 0; k < 3u; k++)
            midi_set[k] = (uint8_t)(b[2u * ML_N + k] <= 16u ? b[2u * ML_N + k] : k + 1u);
        midi_set[3] = (uint8_t)((b[2u * ML_N + 3u] & 31u) <= 16u ? b[2u * ML_N + 3u] & 31u : 10u);
        midi_set[4] = (uint8_t)(b[2u * ML_N + 3u] >> 7);
        song.g[G_CH1] = midi_set[0], song.g[G_CH2] = midi_set[1], song.g[G_CH3] = midi_set[2];
        song.g[G_DRCH] = midi_set[3], song.g[G_CHOTH] = midi_set[4];
    }
    if (n >= MIDI_PART) {
        midi_set[5] = (uint8_t)(b[2u * ML_N + 4u] >= 1u && b[2u * ML_N + 4u] <= 12u ? b[2u * ML_N + 4u] : 2u);
        song.g[G_BEND] = midi_set[5];
    }
    fm1_irq_on();
}
static int settings_write(const persist_t *p)
{
    uint8_t mp[MIDI_PART];
    if (!dsu_valid(&dsu))
        dsu_defaults();
    midi_part_fill(mp);
    /* the third part only when it is not the default: without, the object is exactly 2.5's (2.5 reads it whole, SYN
     * kits included; with it, it takes the object for one without them) */
    if (st_save3(OBJ_SETTINGS, p, sizeof *p, &dsu, sizeof dsu, mp, midi_part_default(mp) ? 0u : sizeof mp))
        return -1;
    dsu_dirty = 0;
    memcpy(midi_saved, mp, sizeof midi_saved);
    return 0;
}
static void dsu_boot(void)                          /* the SYN kits (and the MIDI part) from the settings object, else */
{                                                   /* the defaults */
    st_hdr_t h;
    uint32_t len = sizeof(persist_t) + sizeof(dsu_bank_t), k;
    dsu_defaults();
    for (k = 0; k < ML_N; k++)
        ml_tab[k] = 0;
    if (st_current(OBJ_SETTINGS, &h) >= 0 &&
        (h.len == len || h.len == len + 2u * ML_N || h.len == len + 2u * ML_N + 4u || h.len == len + MIDI_PART) &&
        dsu_valid((const dsu_bank_t *)(st_buf + sizeof(persist_t)))) {
        memcpy(&dsu, st_buf + sizeof(persist_t), sizeof dsu);
        for (k = 0; k < DSU_N; k++)
            dsu_fix_kit(&dsu.k[k]);
        if (h.len > len)
            midi_part_read(st_buf + len, h.len - len);
        drch_seed = h.len == len;
    }
    midi_part_fill(midi_saved);
    dsu_dirty = 0;
}
#endif

static void persist_fill(persist_t *p)              /* the settings as they are now */
{
    memset(p, 0, sizeof *p);
    p->magic = PERSIST_MAGIC;
    p->palette = settings.palette;
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom;
    p->panel = panel;
    p->lights = lights_word();
#if FELUCCA_DRUMMER
    p->arrangement = arrangement;
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    persist_fill(&p);
    {
        uint8_t mp[MIDI_PART];
        midi_part_fill(mp);
        if (!memcmp(&p, &persist_saved, sizeof p) && !memcmp(mp, midi_saved, sizeof mp))
            return;                                /* unchanged: no erase cycle */
    }
    if (settings_write(&p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");

/* ---- backup restore (editor.c BK_PUT): each object checked as a load checks it, then written through the
 * same A/B commit as a save. rc: 0 ok, 2 not a valid object, 3 stop the song first, 4 flash */
static int panel_valid(const panel_t *q)           /* a permutation of the buttons and of the knobs */
{
    uint32_t i, b = 0, e = 0;
    if (q->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (q->btn[i] >= 14u || (b >> q->btn[i]) & 1u)
            return 0;
        b |= 1u << q->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (q->enc[i] >= 7u || (e >> q->enc[i]) & 1u || (q->dir[i] != 1 && q->dir[i] != -1))
            return 0;
        e |= 1u << q->enc[i];
    }
    return 1;
}

static uint32_t settings_restore(const void *raw, uint32_t n)
{
    persist_t p;
    if (n != sizeof p && n != PERSIST_SIZE_V22)
        return 2;
    memset(&p, 0, sizeof p);
    memcpy(&p, raw, n);
    if (p.magic != PERSIST_MAGIC || p.palette >= NPALETTES || p.lowcut > 1u || p.zoom > 1u || !panel_valid(&p.panel))
        return 2;
#if FELUCCA_DRUMMER
    if (!arr_valid(&p.arrangement, 15u))
        return 2;
    if ((song.playing || transport_req) && memcmp(&p.arrangement, &arrangement, sizeof arrangement))
        return 3;                                    /* SLOOP 2.5: a new song order only when stopped (as on the SONG screen) */
#endif
    if (!flash_ok || settings_write(&p))
        return 4;
    persist_saved = p;
    settings.palette = p.palette;
    settings.lowcut = p.lowcut;
    settings.zoom = p.zoom;
    panel = p.panel;
#if FELUCCA_DRUMMER
    arrangement = p.arrangement;
#endif
    lights_from_word(p.lights);
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
    ui.force = 1;
    return 0;
}

/* slot 0..3 (n 0: empty), or 4: the working project (loaded now) */
static uint32_t project_restore(uint32_t slot, const void *raw, uint32_t n)
{
    if (song.playing || transport_req)
        return 3;
    if (slot < 4u && !n) {
        if (!flash_ok || st_save(OBJ_PROJECT0 + slot, raw, 0))
            return 4;
        memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (!proj_import(&autosave_buf, raw, (int)n))
        return 2;
    if (slot == 4u) {
        project_apply(&autosave_buf);
        return 0;
    }
    if (!flash_ok || st_save(OBJ_PROJECT0 + slot, &autosave_buf, sizeof autosave_buf))
        return 4;
    memcpy(&proj_slot[slot], &autosave_buf, sizeof proj_slot[slot]);
    sec_dirty &= (uint8_t)~(1u << slot);
    return 0;
}
#endif
#if FELUCCA_DRUMMER
static void arrangement_save(void)
{
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
    settings_save();
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(memcmp(&persist_saved.arrangement, &arrangement, sizeof arrangement) ? "SAVE ERROR" : "SONG SAVED");
        return;
    }
#endif
    ui_message("SONG IN RAM ONLY");
}

/* ---- live sections (SAVE + key, ui_layers.c). A section is a project slot (A..D = 1..4): stored into RAM
 * at once (playing too), written to flash once the transport is stopped and nothing sounds (an erase
 * stops the audio for ~50 ms); a song recorded with SONG REC is saved the same way. */
static void section_store(uint32_t s)
{
    s &= 3u;
    fm1_irq_off();                                      /* (the audio ISR may be applying a section) */
    proj_capture(&proj_slot[s]);
    live_sec = (int8_t)s;
    fm1_irq_on();
    sec_dirty |= (uint8_t)(1u << s);
}
static void section_load(uint32_t s)                    /* stopped: the section is the loop now */
{
    s &= 3u;
    project_apply(&proj_slot[s]);
    live_sec = (int8_t)s;
}
static void sections_write(void)                        /* the dirty sections and song into flash */
{
    uint32_t i;
#if FELUCCA_FLASH
    if (flash_ok)
        for (i = 0; i < 4u; i++)
            if (((sec_dirty >> i) & 1u) && st_save(OBJ_PROJECT0 + i, &proj_slot[i], sizeof proj_slot[i]) == 0)
                sec_dirty &= (uint8_t)~(1u << i);       /* (a failed write stays dirty: tried again later) */
    if (!flash_ok)
#endif
        sec_dirty = 0;
    (void)i;
    if (song_dirty) {
        song_dirty = 0;
        settings_save();
    }
}
/* before an intentional reset (an update, UPDATE MODE, UBOOT from the host): the audio is stopped, so
 * whatever is only in RAM goes to flash now: the live sections, the song, the working project */
static void persist_flush_now(void)
{
    sections_write();
#if FELUCCA_FLASH
    if (flash_ok && !arrangement_clock.running) {     /* (a song playing: the tracks hold a section) */
        proj_capture(&autosave_buf);
        if (autosave_buf.sum != autosave_hash && st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
            autosave_hash = autosave_buf.sum;
    }
#endif
}
static void sections_flush(void)                        /* main loop */
{
    static uint32_t tried;
    if (srec_done) {
        song_dirty = srec_done != 0xFFu;
        if (song_dirty) {
            char b[8];
            fmt_int(b, srec_done);
            ui_say("SONG PARTS ", b);
        } else {
            ui_message("NO SONG");
        }
        srec_done = 0;
    }
    if ((uint32_t)song.g[G_SYNC] != lights_sync) {      /* SETUP > LEARN > SYNC: kept with the settings */
        lights_sync = (uint8_t)song.g[G_SYNC];
        settings_later = 1;
    }
    if ((uint32_t)(song.g[G_MIDI] != 0) != lights_mout) {   /* SETUP > MIDI > OUT: the same */
        lights_mout = (uint8_t)(song.g[G_MIDI] != 0);
        settings_later = 1;
    }
    if ((uint32_t)song.g[G_ROUTE] != lights_min) {        /* SETUP > MIDI > IN: the same (2.6: CH n too) */
        lights_min = (uint8_t)song.g[G_ROUTE];
        settings_later = 1;
    }
    {   /* 2.6: SETUP > MIDI (CH1 CH2 CH3 OTHER) and SETUP > DRUMS > CH: the same */
        static const uint8_t gid[6] = {G_CH1, G_CH2, G_CH3, G_DRCH, G_CHOTH, G_BEND};
        uint32_t k;
        for (k = 0; k < 6u; k++)
            if ((uint32_t)song.g[gid[k]] != midi_set[k]) {
                midi_set[k] = (uint8_t)song.g[gid[k]];
                settings_later = 1;
            }
    }
#if FELUCCA_FLASH
    if (arrset_dirty && !song.playing && !transport_req && fm1_ms - ui_input_ms >= 1500u) {
        arrset_dirty = 0;                               /* fm1-drummer: SETUP > PEDAL, the last song (sng.c) */
        arrset_save();
    }
#endif
    if (settings_later) {                               /* the menu closed while playing */
        settings_later = 0;
        song_dirty = 1;                                 /* (settings_save when quiet, with the song) */
    }
    if ((!sec_dirty && !song_dirty) || song.playing || transport_req || !audio_quiet() || fm1_ms - ui_input_ms < 1500u ||
        fm1_ms - tried < 5000u)
        return;
    tried = fm1_ms;                                     /* (a failed write: again in 5 s, not every frame) */
    sections_write();
    if (sec_dirty)
        ui_message("SAVE ERROR: RETRYING");
}
#endif
#endif /* PROJ_HOST */
