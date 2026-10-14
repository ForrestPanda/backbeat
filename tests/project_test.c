/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part). Format DRM1 (fm1-drummer, drums only:
 * the globals and the drum track) is written; SLOOP 2.x's four-track formats 6 ("FUN6"), 5 ("FUN5": per-step nudges,
 * parameter locks, fill conditions) and 4 ("FUN4": 10-byte steps with levels and ratchets, the drum track's 16 lanes),
 * built byte for byte as SLOOP stored them, give their drum track and their globals: every value at its parameter,
 * the parameters added since at their defaults, a lock on P_E0.. moved with it, the drum DLY and the TEXTURE where the
 * format had them; damaged ones are refused. Then the working project's capture / apply round trip and what apply
 * clamps. Run by tests/run_tests.sh (needs build/gen). */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* a drum pattern to find again: lanes, levels and ratchets */
static void drum_steps(dstep_t *d)
{
    uint32_t k;
    memset(d, 0, NSTEP * sizeof *d);
    for (k = 0; k < NSTEP; k++) {
        if (!(k % 4u)) dstep_set(&d[k], 0, LV_HARD, 0);
        if (k % 8u == 4u) dstep_set(&d[k], 2, LV_NORM, 1);
        dstep_set(&d[k], 4, k & 1u ? LV_GHOST : LV_SOFT, k % 3u);
    }
}

int main(void)
{
    static project_t q, q2;
    static project_v4_t v4;
    static project_v5_t v5;
    static project_v6_t v6;
    static dstep_t pat[NSTEP];
    static union {
        project_t drm;
        project_v6_t v6;
        project_v5_t v5;
        project_v4_t v4;
    } buf;
    uint32_t i, t;
    int bad = 0, ok;

    bad += check("layout: P_CHORD, P_TFLT, P_STRUM, P_VLEAD, then the ten of 2.6 just before P_E0 (63), P_COUNT = format 5's + 10",
                 P_CHORD + 1 == P_TFLT && P_TFLT + 1 == P_STRUM && P_STRUM + 1 == P_VLEAD && P_VLEAD + 1 == P_ITYPE &&
                 P_LPOL + 1 == P_E0 && P_E0 == 63 && P_COUNT == PROJ_NP_V5 + 10u && PROJ_NP_V5 == PROJ_NP_V4 + 3u &&
                 P_SLDEPTH + 1 == P_CHORD);
    bad += check("DRM1 fits one flash object; 4 slots fit .noinit (with panel, settings, dbg, bootguard)",
                 sizeof(project_t) <= 3840u && 4u * sizeof(project_t) <= 0x3D50u - 256u &&
                 offsetof(project_t, t) == 8u + 2u * PROJ_NG + 4u && offsetof(project_t, sum) >= offsetof(project_t, t) + sizeof(proj_trk_t) &&
                 offsetof(project_t, sum) + 4u == sizeof(project_t));
    bad += check("formats 4 / 5 / 6 as SLOOP stored them (3112, 3840, 3816 bytes)",
                 sizeof v4 == 3112u && sizeof v5 == 3840u && sizeof v6 == 3816u);
    drum_steps(pat);

    /* FUN4 (SLOOP 2.0 .. 2.3): the drum track by id up to P_CHORD, P_E0.. moved, the parameters since at defaults */
    memset(&v4, 0, sizeof v4);
    v4.magic = PROJ_MAGIC_V4;
    v4.size = sizeof v4;
    for (i = 0; i < PROJ_NG; i++)
        v4.g[i] = (int16_t)(400 + i);
    v4.sel = 1;
    for (t = 0; t < PROJ_NTRK_OLD; t++)
        for (i = 0; i < PROJ_NP_V4; i++)
            v4.t[t].p[i] = (int16_t)(t * 100 + i);
    v4.t[0].engine = 7, v4.t[PROJ_DRUM_OLD].engine = 0;
    memcpy(v4.t[PROJ_DRUM_OLD].dstep, pat, sizeof pat);
    v4.t[1].step[3].n = 2;                             /* (a synth part's steps: left behind) */
    v4.sum = proj_hash(&v4, sizeof v4 - 4u);
    memcpy(&buf, &v4, sizeof v4);
    ok = proj_import(&q, &buf, (int)sizeof v4) && proj_ok(&q) && q.magic == PROJ_MAGIC && !memcmp(q.g, v4.g, sizeof q.g) &&
         !memcmp(q.t.dstep, pat, sizeof pat) && q.t.engine == 0 && q.drdly == 0 && !(q.rsv[0] & 0x80u);
    for (i = 0; i < PROJ_NC6; i++)
        ok &= q.t.p[i] == (i < PROJ_NP_V4 - 8u ? (int16_t)(PROJ_DRUM_OLD * 100u + i) : TP[i].def);
    for (i = 0; i < 8u; i++)
        ok &= q.t.e[i] == (int16_t)(PROJ_DRUM_OLD * 100u + PROJ_NP_V4 - 8u + i);
    for (i = P_ITYPE; i < P_E0; i++)
        ok &= pt_get(&q.t, i) == TP[i].def;
    for (i = 0; i < NLOCK; i++)
        ok &= q.t.lock[i].step == LOCK_FREE;
    for (i = 0; i < NSTEP; i++)
        ok &= q.t.mf[i] == 0;
    bad += check("FUN4 -> DRM1: the drum track (lanes, levels, ratchets; P_E0.. moved, FILTER off), globals, no nudge / lock / fill", ok);
    v4.t[PROJ_DRUM_OLD].dstep[5].on[0] ^= 1u;
    memcpy(&buf, &v4, sizeof v4);
    bad += check("FUN4 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v4));

    /* FUN5 (SLOOP 2.4 .. 2.5): by id, the 2.6 ten at their defaults, the nudges and conditions into a byte a step, a lock
     * on P_E0.. moved with it (format 5's 53.. -> 63..), G_DRDLY */
    memset(&v5, 0, sizeof v5);
    v5.magic = PROJ_MAGIC_V5;
    v5.size = sizeof v5;
    for (i = 0; i < PROJ_NG; i++)
        v5.g[i] = (int16_t)(500 + i);
    v5.sel = 2;
    v5.drdly = 55;
    for (t = 0; t < PROJ_NTRK_OLD; t++) {
        for (i = 0; i < PROJ_NP_V5; i++)
            v5.t[t].p[i] = i < PROJ_NP_V5 - 8u ? (int16_t)(t * 10 + i) : (int16_t)(100 + t * 10 + i);
        for (i = 0; i < NLOCK; i++)
            v5.t[t].lock[i].step = LOCK_FREE;
    }
    memcpy(v5.t[PROJ_DRUM_OLD].dstep, pat, sizeof pat);
    v5.t[PROJ_DRUM_OLD].micro[3] = -32, v5.t[PROJ_DRUM_OLD].micro[4] = 31, v5.t[PROJ_DRUM_OLD].micro[9] = -7;
    v5.t[PROJ_DRUM_OLD].fill[0] = 0x09;            /* steps 1 and 2: FILL ONLY, NO FILL */
    v5.t[PROJ_DRUM_OLD].fill[15] = 0x40;           /* step 64: FILL ONLY */
    v5.t[PROJ_DRUM_OLD].lock[0].step = 3, v5.t[PROJ_DRUM_OLD].lock[0].param = 53, v5.t[PROJ_DRUM_OLD].lock[0].val = 9;   /* format 5's P_E0 */
    v5.t[PROJ_DRUM_OLD].lock[1].step = 4, v5.t[PROJ_DRUM_OLD].lock[1].param = P_PAN, v5.t[PROJ_DRUM_OLD].lock[1].val = -20;
    v5.t[0].micro[0] = 5;                          /* (a synth part's: left behind) */
    v5.sum = proj_hash(&v5, sizeof v5 - 4u);
    memcpy(&buf, &v5, sizeof v5);
    ok = proj_import(&q, &buf, (int)sizeof v5) && proj_ok(&q) && q.drdly == 55 && !memcmp(q.g, v5.g, sizeof q.g) &&
         !memcmp(q.t.dstep, pat, sizeof pat);
    for (i = 0; i < PROJ_NP_V5 - 8u; i++)
        ok &= pt_get(&q.t, i) == (int16_t)(PROJ_DRUM_OLD * 10u + i);
    for (i = 0; i < 8u; i++)
        ok &= pt_get(&q.t, P_E0 + i) == (int16_t)(100 + PROJ_DRUM_OLD * 10u + PROJ_NP_V5 - 8u + i);
    for (i = P_ITYPE; i < P_E0; i++)
        ok &= pt_get(&q.t, i) == TP[i].def;
    ok &= pt_micro(&q.t, 3) == -32 && pt_micro(&q.t, 4) == 31 && pt_micro(&q.t, 9) == -7 && pt_micro(&q.t, 0) == 0 &&
          pt_fill(&q.t, 0) == FC_FILL && pt_fill(&q.t, 1) == FC_NOFILL && pt_fill(&q.t, 2) == FC_NORM &&
          pt_fill(&q.t, 63) == FC_FILL && q.t.lock[0].param == P_E0 && q.t.lock[0].val == 9 &&
          q.t.lock[1].param == P_PAN && q.t.lock[2].step == LOCK_FREE;
    bad += check("FUN5 -> DRM1: the drum track by id, the 2.6 parameters default, nudges, conditions, a lock on E1 moved with it", ok);
    v5.t[PROJ_DRUM_OLD].micro[0] ^= 1;
    memcpy(&buf, &v5, sizeof v5);
    bad += check("FUN5 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v5));

    /* FUN6 (SLOOP 2.6): the drum track as stored, G_DRDLY, the TEXTURE */
    memset(&v6, 0, sizeof v6);
    v6.magic = PROJ_MAGIC_V6;
    v6.size = sizeof v6;
    for (i = 0; i < PROJ_NG; i++)
        v6.g[i] = (int16_t)(600 + i);
    v6.sel = 3, v6.drdly = 66, v6.rsv[0] = 0x80u | 3u, v6.rsv[1] = 2;
    for (t = 0; t < PROJ_NTRK_OLD; t++) {
        proj_trk_plain(&v6.t[t]);
        for (i = 0; i < P_COUNT; i++)
            pt_set(&v6.t[t], i, i >= P_ITYPE && i < P_E0 ? (int32_t)(t + i) : (int32_t)(t * 100 + i));
        v6.t[t].engine = (uint8_t)t;
    }
    memcpy(v6.t[PROJ_DRUM_OLD].dstep, pat, sizeof pat);
    pt_set_mf(&v6.t[PROJ_DRUM_OLD], 3, -32, FC_FILL);
    pt_set_mf(&v6.t[PROJ_DRUM_OLD], 4, 31, FC_NOFILL);
    v6.t[PROJ_DRUM_OLD].lock[23].step = 63, v6.t[PROJ_DRUM_OLD].lock[23].param = P_TFLT, v6.t[PROJ_DRUM_OLD].lock[23].val = -30;
    v6.sum = proj_hash(&v6, sizeof v6 - 4u);
    memcpy(&buf, &v6, sizeof v6);
    ok = proj_import(&q, &buf, (int)sizeof v6) && proj_ok(&q) && q.drdly == 66 && q.rsv[0] == (0x80u | 3u) && q.rsv[1] == 2 &&
         !memcmp(q.g, v6.g, sizeof q.g) && !memcmp(q.t.dstep, pat, sizeof pat) && q.t.engine == 0 &&
         pt_micro(&q.t, 3) == -32 && pt_fill(&q.t, 3) == FC_FILL && pt_micro(&q.t, 4) == 31 &&
         q.t.lock[23].step == 63 && q.t.lock[23].param == P_TFLT && q.t.lock[23].val == -30;
    for (i = 0; i < P_COUNT; i++)
        ok &= pt_get(&q.t, i) == pt_get(&v6.t[PROJ_DRUM_OLD], i);
    bad += check("FUN6 -> DRM1: the drum track as stored (lanes, nudges, conditions, locks, the 2.6 ones), DLY, TEXTURE", ok);
    v6.t[PROJ_DRUM_OLD].mf[7] ^= 1u;
    memcpy(&buf, &v6, sizeof v6);
    bad += check("FUN6 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v6));

    /* DRM1 as is; damaged, wrong size, another format's magic */
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    bad += check("DRM1 -> DRM1: as stored", proj_import(&q2, &buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q));
    bad += check("DRM1 with a wrong length: refused", !proj_import(&q2, &buf, (int)sizeof q - 2));
    buf.drm.t.dstep[2].on[0] ^= 4u;
    bad += check("DRM1 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof q));
    memcpy(&buf, &q, sizeof q);
    buf.drm.magic = PROJ_MAGIC_V6;
    bad += check("DRM1 size with a FUN6 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));
    memcpy(&buf, &v6, sizeof v6);
    buf.v6.magic = PROJ_MAGIC_V5;
    bad += check("FUN6 size with a FUN5 magic: refused", !proj_import(&q2, &buf, (int)sizeof v6));
    memset(&buf, 0, sizeof buf);
    buf.drm.magic = 0x46554E33u;                   /* "FUN3": SLOOP 1.x, not read any more */
    bad += check("an older format (FUN1..3): not read", !proj_import(&q2, &buf, 2584));

    /* capture / apply: the working project round trip (nudges, locks, conditions, the 2.6 parameters) */
    host_tracks_init();
    TDRUM->p[P_SLEN] = 7;
    dstep_set(&TDRUM->dstep[9], 4, LV_SOFT, 1);
    song.g[G_DUST] = 33;
    song.g[G_DRDLY] = 77;
    TDRUM->micro[9] = 12;
    lock_set(TDRUM, 9, P_DIST, 64);
    lock_set(TDRUM, 2, P_TFLT, -30);
    step_fill_set(TDRUM, 9, FC_NOFILL);
    step_fill_set(TDRUM, 63, FC_FILL);
    TDRUM->p[P_ITYPE] = 4, TDRUM->p[P_IA] = 11, TDRUM->p[P_IMIX] = 77;   /* 2.6 */
    TDRUM->p[P_E0] = 6;                            /* a kit */
    proj_capture(&q);
    host_tracks_init();
    song.g[G_DRDLY] = 0;
    proj_apply(&q, 1);
    ok = q.drdly == 77 && song.g[G_DRDLY] == 77 && TDRUM->p[P_SLEN] == 7 && song.g[G_DUST] == 33 &&
         dstep_has(&TDRUM->dstep[9], 4) && dstep_lvl(&TDRUM->dstep[9], 4) == LV_SOFT && dstep_rat(&TDRUM->dstep[9], 4) == 1u &&
         TDRUM->micro[9] == 12 && TDRUM->micro[3] == 0 && lock_find(TDRUM, 9, P_DIST, 0) >= 0 &&
         lock_find(TDRUM, 2, P_TFLT, 0) >= 0 && TDRUM->lock[lock_find(TDRUM, 2, P_TFLT, 0)].val == -30 &&
         lock_find(TDRUM, 9, P_E0, 0) < 0 && step_fill(TDRUM, 9) == FC_NOFILL && step_fill(TDRUM, 63) == FC_FILL &&
         step_fill(TDRUM, 8) == FC_NORM && TDRUM->p[P_ITYPE] == 4 && TDRUM->p[P_IA] == 11 && TDRUM->p[P_IMIX] == 77 &&
         TDRUM->p[P_E0] == 6;
    bad += check("the working project: capture -> apply round trip (lanes, DUST, drum DLY, nudges, locks, fill conditions, the 2.6 parameters, the kit)", ok);
    q2 = q;
    q2.drdly = 0;                                  /* a 2.4 project: the byte was 0 */
    proj_apply(&q2, 0);                            /* (as a song section) */
    ok = song.g[G_DRDLY] == 0;
    q2.drdly = 200;
    proj_apply(&q2, 1);
    ok &= song.g[G_DRDLY] == 127;
    bad += check("2.5 drum DLY: kept in its byte, a 2.4 project: 0, a section sets it, 200 -> 127",
                 ok && G_DRDLY == PROJ_NG && offsetof(project_t, drdly) == 8u + 2u * PROJ_NG);
    {   /* 2.6: GLO > TEXTURE in the project (and in a section); a project before it: OFF; the drum channel (a setting of
         * the FM-1 now, GLO > MIDI) not taken from a project */
        song.g[G_TXSRC] = 5, song.g[G_TXLVL] = 101, song.g[G_TXTONE] = -37, song.g[G_TXMOVE] = 90;
        song.g[G_TXRATE] = 3, song.g[G_TXWHEN] = 1, song.g[G_TXDUCK] = 1, song.g[G_DRCH] = 4;
        proj_capture(&q2);
        song.g[G_TXSRC] = 0, song.g[G_TXLVL] = 0, song.g[G_TXTONE] = 0, song.g[G_TXMOVE] = 0;
        song.g[G_TXRATE] = 0, song.g[G_TXWHEN] = 0, song.g[G_TXDUCK] = 0;
        proj_apply(&q2, 1);
        ok = song.g[G_TXSRC] == 5 && song.g[G_TXLVL] == 101 && song.g[G_TXTONE] == -37 && song.g[G_TXMOVE] == 90 &&
             song.g[G_TXRATE] == 3 && song.g[G_TXWHEN] == 1 && song.g[G_TXDUCK] == 1 && song.g[G_DRCH] == 4;
        song.g[G_DRCH] = 9;
        song.g[G_TXSRC] = 0;
        proj_apply(&q2, 0);                        /* a section: its texture too */
        ok &= song.g[G_TXSRC] == 5 && song.g[G_DRCH] == 9;
        q2.rsv[0] = 0;                             /* a project before 2.6 (converted): no texture */
        proj_apply(&q2, 1);
        ok &= song.g[G_TXSRC] == 0 && song.g[G_TXLVL] == GP[G_TXLVL].def && song.g[G_TXMOVE] == GP[G_TXMOVE].def && song.g[G_DRCH] == 9;
        bad += check("2.6 TEXTURE: kept in a project (SRC LEVEL TONE MOVE RATE WHEN DUCK), a section sets it, an older one: OFF; "
                     "the drum channel not taken from a project", ok);
    }
    {   /* what the format packs in fewer bits holds every value: the byte parameters (P_ITYPE .. P_LPOL: 0..255), TEXTURE's
         * SRC (4 bits), WHEN and DUCK (1), RATE (a byte), LEVEL and TONE + 64 (7 bits each) */
        uint32_t k;
        int ok2 = 1;
        for (k = PROJ_NC6; k < P_E0; k++)
            ok2 &= TP[k].min >= 0 && TP[k].max <= 255;
        ok2 &= GP[G_TXSRC].min >= 0 && GP[G_TXSRC].max <= 15 && GP[G_TXWHEN].min >= 0 && GP[G_TXWHEN].max <= 1 &&
               GP[G_TXDUCK].min >= 0 && GP[G_TXDUCK].max <= 1 && GP[G_TXRATE].min >= 0 && GP[G_TXRATE].max <= 255 &&
               GP[G_TXLVL].min >= 0 && GP[G_TXLVL].max <= 127 && GP[G_TXTONE].min >= -64 && GP[G_TXTONE].max <= 63;
        bad += check("the byte parameters and TEXTURE's packed fields hold every value of their range", ok2);
    }
    /* a damaged image: a lock on a parameter that cannot lock, on a step past the end, with a value past the range, on
     * an unknown parameter: freed, freed, clamped, freed; a condition 3: normal; a kit past the list: clamped */
    q.t.lock[5].step = 4, q.t.lock[5].param = P_SLEN, q.t.lock[5].val = 8;
    q.t.lock[6].step = 64, q.t.lock[6].param = P_PAN, q.t.lock[6].val = 1;
    q.t.lock[7].step = 4, q.t.lock[7].param = P_PAN, q.t.lock[7].val = 999;
    q.t.lock[8].step = 4, q.t.lock[8].param = 200, q.t.lock[8].val = 1;
    pt_set_mf(&q.t, 4, 0, FC_FILL), pt_set_mf(&q.t, 5, 0, FC_NOFILL);   /* steps 5, 6: fill only, no fill; */
    pt_set_mf(&q.t, 6, 0, 3), pt_set_mf(&q.t, 7, 0, 3);                /* 7, 8: 3 (-> normal) */
    pt_set(&q.t, P_ITYPE, 200);                    /* an INSERT type past the list: clamped */
    pt_set(&q.t, P_E0, 999);                       /* a kit past the list: the last */
    proj_apply(&q, 1);
    ok = TDRUM->p[P_ITYPE] == TP[P_ITYPE].max && TDRUM->lock[5].step == LOCK_FREE && TDRUM->lock[6].step == LOCK_FREE &&
         TDRUM->lock[8].step == LOCK_FREE && TDRUM->lock[7].step == 4 && TDRUM->lock[7].val == TP[P_PAN].max &&
         step_fill(TDRUM, 4) == FC_FILL && step_fill(TDRUM, 5) == FC_NOFILL && TDRUM->fill[1] == 0x09 &&
         TDRUM->p[P_E0] == DRUM_KITS - 1;
    bad += check("apply: INSERT 200 -> the last type, locks on LEN / step 64 / param 200 freed, PAN 999 -> 63, condition 3 -> normal, kit clamped", ok);

    q2 = q;
    bad += check("2.6 SAVE > PROJECT > ERASE: an object of no bytes is no project (an empty slot, older SLOOP too)",
                 !proj_import(&q2, &q, 0));
    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
