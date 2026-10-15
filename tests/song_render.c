/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-drummer: a song played by the pedal, rendered to a WAV on the host, to hear it before there is an
 * FM-1. The same sources as the firmware (hostsim.c): the drum engine, the sequencer, the pedal (pedal.c)
 * and the songs (sng.c). tools/fm1_render.py wraps it (a song from JSON, section names).
 *
 *   song_render SONG.bin OUT.wav "0:4 1:8 2:8 3:4 4"
 *
 * SONG.bin is a packed song (tools/fm1_song.py --out). The play list: "section:bars" entries, each played for
 * that many bars; the next entry is asked for on beat 3 of the last bar, as a foot would (no fill of its own: a
 * change brings none); with FILLS=1 the fill (CC 21) is asked for on beat 3 of the bar before, so the pattern's
 * fill hits play into the change (a fill asked in a pattern's last bar is the next cycle: pedal.c). A last entry without bars is the ending: that section once as the
 * outro, then the stop on the bar (CC 22); otherwise the stop (CC 23). Section numbers as in the song (0 the
 * first). Environment: KIT=n overrides the song's kit, BPM=n its tempo, TAIL=seconds of silence after the
 * stop (default 1.5), USRKIT=image a user kit (tools/mkkit.py --image: USR3 and USR4 as the device holds
 * them), played as KIT USR3+4 unless KIT says otherwise. Prints the timeline. */
#define FELUCCA_DRUMMER 1
#include <stdint.h>
static uint32_t host_slots[4u * 0x14000u / 4u];     /* USR1..4 as a RAM image of the flash: USRKIT=image fills USR3+4 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * 0x14000u)
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;

static uint8_t blob[4096];
static uint32_t blob_n;
#define SNG_HOST_FETCH 1
static const uint8_t *sng_fetch(uint32_t slot, uint32_t *len)
{
    *len = slot == 0u ? blob_n : 0u;
    return *len ? blob : 0;
}
#include "../firmware/src/arranger_scene.c"

static void cc(uint32_t num, uint32_t val) { um_byte(0xBFu); um_byte(num); um_byte(val); }   /* channel 16 */
static void pc(uint32_t n) { um_byte(0xCFu); um_byte(n); }

static FILE *wf;
static uint32_t now, t0, jumped, stopped;
static int32_t out[CTL * 2];
static void advance(uint32_t samples)
{
    uint32_t end = now + samples, k;
    while (now < end) {
        int8_t sec = live_sec;
        uint8_t playing = song.playing;
        mix_block(out, CTL);
        if (!playing && song.playing) t0 = now;
        if (sec != live_sec) jumped = now;
        if (playing && !song.playing) stopped = now;
        for (k = 0; k < CTL; k++)
            wav_put(wf, out[k * 2], out[k * 2 + 1]);
        now += CTL;
    }
}
static uint32_t bar_len(void) { return 4u * 60u * FS / (uint32_t)song.g[G_BPM]; }
static const char *sec_name(uint32_t k)
{
    static char n[13];
    memcpy(n, sng_sec(k), 12);
    n[12] = 0;
    return n;
}

int main(int argc, char **argv)
{
    uint32_t i, k, n, bar, ntok = 0, sec[64], bars[64], tail;
    int ending = -1;
    FILE *f;
    char *s, *tok;
    const char *env;
    if (argc < 4) {
        fprintf(stderr, "usage: song_render SONG.bin OUT.wav \"0:4 1:8 2:8 4\"\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    blob_n = f ? (uint32_t)fread(blob, 1, sizeof blob, f) : 0u;
    if (f) fclose(f);
    if (!blob_n) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    s = argv[3];
    for (tok = strtok(s, " ,"); tok && ntok < 64u; tok = strtok(0, " ,")) {
        char *colon = strchr(tok, ':');
        sec[ntok] = (uint32_t)atoi(tok);
        bars[ntok] = colon ? (uint32_t)atoi(colon + 1) : 0u;
        if (!colon)
            ending = (int)ntok;
        ntok++;
    }
    if (!ntok) {
        fprintf(stderr, "an empty play list\n");
        return 2;
    }
    host_tracks_init();
    pc(0);
    advance(0);
    mix_block(out, CTL);                            /* (the PC into sng_req; not written: silence) */
    sng_poll();
    if (sng_cur != 0) {
        fprintf(stderr, "%s is not a valid song\n", argv[1]);
        return 1;
    }
    memset(host_slots, 0xFF, sizeof host_slots);
    if ((env = getenv("USRKIT")) != 0) {            /* a user kit: USR3 and USR4 from the image, as at boot */
        FILE *kf = fopen(env, "rb");
        size_t got = kf ? fread((uint8_t *)host_slots + 2u * 0x14000u, 1, 2u * 0x14000u, kf) : 0u;
        if (kf) fclose(kf);
        if (got != 2u * 0x14000u) {
            fprintf(stderr, "USRKIT: cannot read %s (2 x 80 KiB)\n", env);
            return 2;
        }
        smp_user_scan(2);
        smp_user_scan(3);
        TDRUM->p[P_E0] = (int16_t)DRUM_PAIR;
        printf("user kit: USR3 %u sounds, USR4 %u sounds (KIT USR3+4 = %u)\n", usr_nz[2], usr_nz[3], (unsigned)DRUM_PAIR);
    }
    if ((env = getenv("COUNT_IN")) != 0)            /* the pedal's count-in: bars of clicks before the start */
        pedal.count = (uint8_t)atoi(env);
    if ((env = getenv("KIT")) != 0)
        TDRUM->p[P_E0] = (int16_t)clamp(atoi(env), DRUM_KIT_DESC.min, DRUM_KIT_DESC.max);
    if ((env = getenv("BPM")) != 0)
        song.g[G_BPM] = (int16_t)clamp(atoi(env), 40, 240);
    drum_hum = 1;                                   /* humanize: LIGHT, as the device (HUMANIZE: 0 off, 1, 2) */
    if ((env = getenv("HUMANIZE")) != 0)
        drum_hum = (uint8_t)clamp(atoi(env), 0, (int32_t)HUM_N - 1);
    tail = (uint32_t)((env = getenv("TAIL")) != 0 ? atof(env) * FS : 1.5 * FS);
    bar = bar_len();
    printf("song: %u BPM, kit %u (%s), %u sections, bar %u samples\n", (unsigned)song.g[G_BPM], (unsigned)drum_kit(),
           DRUM_KIT_NAMES[drum_kit() < sizeof DRUM_KIT_NAMES / sizeof DRUM_KIT_NAMES[0] ? drum_kit() : 0], sng.nsec, bar);
    for (i = 0; i < ntok; i++)
        if (sec[i] >= sng.nsec) {
            fprintf(stderr, "no section %u (the song has %u)\n", sec[i], sng.nsec);
            return 1;
        }
    wf = fopen(argv[2], "wb");
    if (!wf) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 2;
    }
    wav_hdr(wf, 0);
    /* the first entry starts the loop */
    cc(20, sec[0]);
    advance(CTL * 3u);
    n = 0;
    while (!song.playing && n < 3u * bar) {           /* (COUNT_IN: the clicks first; the song starts after them) */
        advance(CTL);
        n += CTL;
    }
    if (pedal.count)
        printf("%8.3f s  count-in     (%u bar%s)\n", 0.0, pedal.count, pedal.count > 1u ? "s" : "");
    printf("%8.3f s  %-12s (%u bars)\n", t0 / (double)FS, sec_name(sec[0]), bars[0]);
    for (i = 0; i < ntok; i++) {
        uint32_t start = i ? jumped : t0, target;
        if ((int)i == ending)
            break;
        if (!bars[i])
            bars[i] = sng_bars(sec[i]);
        target = start + bars[i] * bar;
        if (getenv("FILLS") && i + 1u < ntok && target > bar + bar / 2u && target - bar - bar / 2u > now) {
            advance(target - bar - bar / 2u - now); /* beat 3 of the bar before: the fill */
            cc(21, 127);
            printf("%8.3f s  fill\n", now / (double)FS);
        }
        if (target > bar / 2u && target - bar / 2u > now)
            advance(target - bar / 2u - now);       /* beat 3 of the last bar: the foot */
        if (i + 1u < ntok) {
            if ((int)(i + 1u) == ending) {
                cc(22, sec[i + 1u]);
                printf("%8.3f s  ending: fill, then %s once\n", now / (double)FS, sec_name(sec[i + 1u]));
            } else {
                cc(20, sec[i + 1u]);
                printf("%8.3f s  then %s\n", now / (double)FS, sec_name(sec[i + 1u]));
            }
            n = 0;
            if (sec[i + 1u] == sec[i] && (int)(i + 1u) != ending) {   /* the same section again: nothing to land (it
                                                   * plays already), its next entry starts on the bar, as the foot
                                                   * counted it */
                advance(target - now);
                jumped = now;
            }
            while (jumped <= start + CTL && n < 8u * bar) {   /* until the section lands */
                advance(CTL);
                n += CTL;
            }
            printf("%8.3f s  %-12s (%u bars)\n", jumped / (double)FS, sec_name(sec[i + 1u]),
                   (int)(i + 1u) == ending ? sng_bars(sec[i + 1u]) : bars[i + 1u]);
        } else {
            advance(target - now);
            cc(23, 0);
            printf("%8.3f s  stop\n", now / (double)FS);
        }
    }
    if (ending >= 0) {
        n = 0;
        while (song.playing && n < 70u * bar) {
            advance(CTL);
            n += CTL;
        }
        printf("%8.3f s  stop (on the bar)\n", stopped / (double)FS);
    }
    advance(tail);
    k = (uint32_t)(ftell(wf) - 44) / 4u;
    fseek(wf, 0, SEEK_SET);
    wav_hdr(wf, k);
    fclose(wf);
    printf("wrote %s: %.1f s\n", argv[2], k / (double)FS);
    return 0;
}
