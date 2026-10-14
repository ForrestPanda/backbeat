/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A track's notes. fm1-drummer: the drum track's (drums.c plays them on its own voices); SLOOP's synth voices, their
 * budget, envelopes, LFOs and engines went with the synth parts (docs/drums-only.md). Runs in the audio ISR. */

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    if (!is_drum(t) || trk_silent(t))
        return;                                         /* MUTE, or another track soloed */
    drum_on(note, vel);
}
