/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A track's notes. fm1-drummer: the drum track's (drums.c plays them on its own voices); SLOOP's synth voices, their
 * budget, envelopes, LFOs and engines went with the synth parts (docs/drums-only.md). Runs in the audio ISR. */

/* the visualiser (ui_vis.c): its four voices are the kit's groups, kicks, snares (and clap, rim), hats and cymbals, toms
 * and percussion: the groups a hit started in (a bit each), each one's last note, a kick */
static volatile uint8_t vis_hit, vis_kick_hit, vis_note[4];
static const uint8_t VIS_GROUP[DRUM_LANES] = {0, 0, 1, 1, 2, 2, 2, 1, 1, 3, 3, 2, 2, 3, 3, 3};   /* drums.c LANE_NAME */

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    if (!is_drum(t) || trk_silent(t))
        return;                                         /* MUTE, or another track soloed */
    {
        uint32_t g = VIS_GROUP[lane_of_note(note) & (DRUM_LANES - 1u)];
        vis_hit |= (uint8_t)(1u << g);
        vis_note[g] = (uint8_t)note;
    }
    if (note == 35u || note == 36u)
        vis_kick_hit = 1;
    drum_on(note, vel);
}
