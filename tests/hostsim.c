/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host-side render of the FELUCCA DSP (the drum track, FX, sequencer) to a WAV,
 * for debugging sound without hardware. Same sources as the firmware.
 *   tests/run_tests.sh builds it into build/host/;
 *   build/host/hostsim [KIT] [OUT.wav]
 * renders a groove (kick, snare, hats on 8ths, 120 BPM) on the drum kit KIT (drums.c DRUM_KIT_NAMES, default
 * the power-on kit). env: SECS=n renders n s, BENCH=1 prints the render time, SENDS=r,d the drums' REV and DLY
 * (GLO > DRUMS), FILT=f the drum track's FILTER, STEPS=n,n,... a pattern of GM notes (0 = rest) instead.
 * TRACKS=DIR: the tracks test (tracks_test below): the groove into DIR, TRS MIDI IN, the cost of the drums.
 * (fm1-drummer, drums only: the synth parts' renders, voice budget and engine costs went with them.) */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#define memmove felucca_memmove
#include "felucca_tables.h"
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#undef memmove
static struct { volatile uint32_t notes, buttons; } fm1_in;
static void fm1_irq_off(void) {}                 /* (the host: one thread) */
static void fm1_irq_on(void) {}
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#include "../firmware/src/samples.c"
#include "../firmware/src/drums.c"
#include "../firmware/src/params.c"
#include "../firmware/src/voice.c"
#include "../firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"                /* TRS MIDI IN: its parser (um_byte) feeds midi_in_q */
#include "../firmware/src/seq.c"

static void wav_hdr(FILE *f, uint32_t frames)
{
    uint32_t v;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    uint16_t a = 1, ch = 2, ba = 4, bits = 16; uint32_t sr = FS, br = FS * 4;
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}

static void wav_put(FILE *f, int32_t l, int32_t r)
{
    int16_t s[2] = {(int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l),
                    (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r)};
    fwrite(s, 2, 2, f);
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

#ifndef HOST_HUM
#define HOST_HUM 0                               /* -DHOST_HUM=1: humanize on (its CPU cost measured) */
#endif
static void host_tracks_init(void)                /* as felucca_init: defaults, empty patterns */
{
    uint32_t i, k;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    song.g[G_BPM] = 120;                         /* (the tests' tempo; SLOOP powers on at 90) */
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_E0; i++)
            trk[k].p[i] = TP[i].def;
        steps_clear(&trk[k]);
    }
    song.master_q12 = 4096;
    drum_hum = HOST_HUM;                         /* (the tests' hits on the grid, as written: humanize has its own) */
    audio_gate_reset();                          /* (the audio gate: a test starts with the mix running) */
}

/* step i of the drum track: GM notes -> its lanes (accent: hard); a TIME other than NOTE: an empty step */
static void put_step(track_t *t, uint32_t i, uint32_t n, const uint8_t *notes, uint32_t time, uint32_t flags)
{
    uint32_t k;
    memset(&t->dstep[i], 0, sizeof t->dstep[i]);
    for (k = 0; k < n && time == ST_NOTE; k++)
        dstep_set(&t->dstep[i], lane_of_note(notes[k]), (flags & SF_ACCENT) ? LV_HARD : LV_NORM, 0);
}

static uint32_t busy_now(void)                   /* sounding drum voices */
{
    uint32_t i, n = 0;
    for (i = 0; i < NDRUM; i++)
        n += drums.v[i].active;
    return n;
}

/* the groove: kick 4 on the floor, snare on 2 and 4, hats on the 8ths (an open one before the bar), 16 steps */
static void host_groove(void)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        uint8_t n[3];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        if (i % 2u == 0u)
            n[k++] = i == 14u ? 46 : 42;
        put_step(TDRUM, i, k, n, k ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    TDRUM->p[P_SLEN] = 16;
}

/* ---------------------------------------------------------------- TRACKS --- */
/* the groove for 8 bars at 120 BPM into DIR/tracks_demo.wav: no sample near full scale, at most NDRUM voices */
static int tracks_demo(const char *dir)
{
    const uint32_t frames = 16u * FS;
    char path[512];
    FILE *w;
    uint32_t f, i, bmax = 0, clips = 0;
    snprintf(path, sizeof path, "%s/tracks_demo.wav", dir);
    if (!(w = fopen(path, "wb"))) {
        fprintf(stderr, "tracks: cannot write %s\n", path);
        return 1;
    }
    host_tracks_init();
    host_groove();
    transport_req = 1;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        if (busy_now() > bmax)
            bmax = busy_now();
        for (i = 0; i < CTL; i++) {
            int32_t l = o[2 * i];
            if (l >= 32700 || l <= -32700)
                clips++;
            wav_put(w, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(w);
    printf("tracks: the groove, 8 bars: drum voices at most %u (of %u) %s; %u samples near full scale %s\n", bmax,
           NDRUM, bmax <= NDRUM ? "ok" : "FAIL", clips, clips ? "FAIL" : "ok");
    return (bmax > NDRUM) + (clips > 0);
}

/* ns per 44.1 kHz sample of the whole mix (events, the drums, buses, master), idle or with drums on 16ths: 2 s to
 * settle, then the fastest of four 2 s stretches (the host's other load only ever adds). Runs in a child process (a
 * clean state); the result comes back through a pipe */
static double tracks_cost(int hits)
{
    int fd[2];
    pid_t pid;
    double r = -1;
    if (pipe(fd))
        return -1;
    fflush(stdout);
    if (!(pid = fork())) {
        uint64_t ns = 0, best = ~0ull, k, nblk = 8u * FS / CTL, seg = nblk / 4u;
        int32_t o[2 * CTL];
        host_tracks_init();
        for (k = 0; k < 2u * FS / CTL + nblk; k++) {
            uint64_t t0;
            if (hits && (k * CTL) % (FS / 8u) < CTL)   /* 16ths at 120 BPM */
                drum_on((k * CTL) % (FS / 2u) < CTL ? 36u : ((k * CTL) / (FS / 8u)) % 4u == 2u ? 38u : 42u, 100u);
            t0 = now_ns();
            mix_block(o, CTL);
            if (k >= 2u * FS / CTL) {
                ns += now_ns() - t0;
                if ((k - 2u * FS / CTL) % seg == seg - 1u) {
                    best = ns < best ? ns : best;
                    ns = 0;
                }
            }
        }
        r = (double)best / (double)(seg * CTL);
        if (write(fd[1], &r, sizeof r) != sizeof r)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &r, sizeof r) != sizeof r)
        r = -1;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return r;
}

/* TRS MIDI IN (midi_uart.c, untested on hardware): bytes through its parser into the same queue as USB: a note on
 * the drum channel (10) plays its drum, running status too, a clock byte in the middle changes nothing; any other
 * channel (OTHER = SEL) plays the drums as well; a note-on with velocity 0 is a note-off (no hit) */
static void trs_bytes(const uint8_t *b, uint32_t n)
{
    int32_t o[2 * CTL];
    while (n--)
        um_byte(*b++);
    mix_block(o, CTL);
}
static int trs_test(void)
{
    uint32_t d0;
    int ok_drum, ok_other, ok_off;
    host_tracks_init();
    d0 = drums.age;
    trs_bytes((const uint8_t[]){0x99, 36, 110, 0xF8, 38, 100}, 6);   /* kick, clock, snare in running status */
    ok_drum = drums.age == d0 + 2u;
    trs_bytes((const uint8_t[]){0x94, 42, 90}, 3);                   /* ch 5: the drums too */
    ok_other = drums.age == d0 + 3u;
    trs_bytes((const uint8_t[]){0x99, 36, 0, 38, 0, 0x89, 42, 64}, 8);   /* vel 0 and 0x8n: offs, no hit */
    ok_off = drums.age == d0 + 3u;
    printf("tracks: TRS MIDI IN: ch 10 -> drums (running status, a clock byte between) %s, ch 5 -> drums %s; "
           "note-offs (vel 0, 0x8n) no hit %s\n", ok_drum ? "ok" : "FAIL", ok_other ? "ok" : "FAIL", ok_off ? "ok" : "FAIL");
    return !ok_drum + !ok_other + !ok_off;
}

static int tracks_test(const char *dir)
{
    int fail = 0, st = 0;
    pid_t pid;
    double idle, hits;
    fflush(stdout);
    if (!(pid = fork())) {
        int rc = tracks_demo(dir);
        fflush(stdout);
        _exit(rc);
    }
    waitpid(pid, &st, 0);
    fail += !WIFEXITED(st) || WEXITSTATUS(st);
    fflush(stdout);
    if (!(pid = fork())) {
        int rc = trs_test();
        fflush(stdout);
        _exit(rc);
    }
    waitpid(pid, &st, 0);
    fail += !WIFEXITED(st) || WEXITSTATUS(st);
    printf("tracks: WAV in %s: tracks_demo.wav\n", dir);
    idle = tracks_cost(0);
    hits = tracks_cost(1);
    printf("tracks: the mix (host -O2, ns per sample): idle %.1f, drums on 16ths %.1f\n", idle, hits);
    printf("tracks: %s\n", fail ? "FAILED" : "all checks ok");
    return fail;
}

int main(int argc, char **argv)
{
    int kit = argc > 1 ? atoi(argv[1]) : (int)DRUM_DEFAULT_KIT;
    const char *out = argc > 2 ? argv[2] : "out.wav";
    uint32_t i, frames = FS * 4u, f;
    clock_t c0;
    FILE *w;
    if (getenv("TRACKS"))
        return tracks_test(getenv("TRACKS"));
    if (!(w = fopen(out, "wb"))) {
        fprintf(stderr, "hostsim: cannot write %s\n", out);
        return 1;
    }
    if (getenv("SECS"))
        frames = FS * (uint32_t)atoi(getenv("SECS"));
    host_tracks_init();
    TDRUM->p[P_E0] = (int16_t)clamp(kit, 0, DRUM_KITS - 1);
    if (getenv("SENDS"))                                 /* SENDS=rev,dly: GLO > DRUMS */
        sscanf(getenv("SENDS"), "%hd,%hd", &song.g[G_DRREV], &song.g[G_DRDLY]);
    if (getenv("FILT"))
        TDRUM->p[P_TFLT] = (int16_t)atoi(getenv("FILT"));
    if (getenv("STEPS")) {                              /* STEPS=n,n,...: GM notes, 0 = rest */
        const char *s = getenv("STEPS");
        int nt, k, c = 0;
        while (c < NSTEP && sscanf(s, "%d%n", &nt, &k) == 1) {
            uint8_t u = (uint8_t)nt;
            put_step(TDRUM, (uint32_t)c++, nt ? 1u : 0u, &u, nt ? ST_NOTE : ST_REST, 0);
            s += k;
            if (*s == ',')
                s++;
        }
        TDRUM->p[P_SLEN] = (int16_t)(c ? c : 1);
    } else {
        host_groove();
    }
    transport_req = 1;
    wav_hdr(w, frames);
    c0 = clock();
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(w, o[2 * i], o[2 * i + 1]);
    }
    if (getenv("BENCH"))
        fprintf(stderr, "%.1f s audio in %.1f ms (%.2f %% of real time)\n", (double)frames / FS,
                (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC,
                (double)(clock() - c0) * 100.0 / CLOCKS_PER_SEC / ((double)frames / FS));
    fclose(w);
    printf("hostsim: %s, kit %s, %.1f s\n", out, DRUM_KIT_NAMES[drum_kit()], (double)frames / FS);
    return 0;
}
