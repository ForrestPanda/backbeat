/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Regression suite of the drum machine on the host (the firmware's own sources, through hostsim.c).
 *   build/host/regress [GOLDEN_FILE CPU_FILE]      (run_tests.sh builds and runs it)
 * Drums only (docs/drums-only.md): SLOOP's synth renders (every engine x preset, the voice modes, the sends, the
 * 4-track mix) and its voice checks went with the synth parts.
 *
 * 1. golden renders: every GM note through the drum track (drums/gm_kit), every built-in kit playing a groove
 *    through the sequencer (levels, ratchets, a fill bar, swing), the drum track's INSERT in each of its types,
 *    the master's DUST and DJ filter. Each render is reduced to a 64-bit FNV-1a hash of its output samples,
 *    compared with GOLDEN_FILE (tests/golden.txt). Every render runs in its own fork()ed child of a process that
 *    never touched the DSP state, so all state starts as at boot (the noise seeds, the FX buffers, the limiter).
 * 2. health checks on every render: no sample beyond 16 bits, none near full scale, DC offset, the peak level,
 *    every voice free after the last hit, silence at the end. Fails with the numbers.
 * 3. CPU budget: the idle mix, the idle + drums mix (= 1000), a busy pattern (every lane on 16ths, ratchets) and
 *    each built-in kit's groove: instructions counted by the kernel where there is a counter (macOS
 *    proc_pid_rusage), elsewhere CPU_VALGRIND=1 counts each job under callgrind (exact), else the best of three
 *    timed seconds. Compared with CPU_FILE (tests/cpu_baseline.txt) at +25 % (instructions) or +35 % (time).
 * 4. routing: no hanging note after random MIDI and keys on every channel (the drum voices free at the end).
 * env: GOLDEN_UPDATE=1 rewrites GOLDEN_FILE, BUDGET_UPDATE=1 rewrites CPU_FILE (on purpose: review the
 * diff), VERBOSE=1 prints every render's numbers, JOBS=n children at once (default 8). */
#define FELUCCA_DRUMMER 1                 /* (as the firmware) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/arranger_scene.c"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

/* ------------------------------------------------------------- limits --- */
#define LIM_NEAR 30000            /* |sample| at or above: "near full scale" (the limiter holds ~18000) */
#define LIM_DC 400                /* |mean| of the sounding part, 16-bit units (1.2 %; the master blocks DC) */
#define LIM_PEAK_MIN 250          /* quieter than this (-42 dBFS): something is missing */
#define LIM_TAIL 6                /* AC peak of the last 0.25 s, after the voices are free and the FX rang out */
#define LIM_TAIL_DC 2             /* its constant part: the master DC blocker settles to 0 (error feedback; it
                                   * used to leave up to +-31), what is left is the midpoint of the AC tail */
#define FREE_CAP_S 12             /* the voices must be free this long after the last note-off */
#ifndef TAIL_S
#define TAIL_S 3                  /* FX tail rendered after the voices are free */
#endif
#define CPU_TOL 0.25              /* +-25 % with an instruction counter, CPU_TOL_NS timed; and at least */
#define CPU_TOL_NS 0.35           /* CPU_SLACK (per mille of the idle + drums mix: light presets) */
#define CPU_SLACK 40

/* ------------------------------------------------------------ results --- */
typedef struct {
    uint64_t hash;
    uint32_t frames, over, near, vmax, ok;     /* ok: a scenario's own checks passed */
    int32_t peak, tail_peak, tail_dc, tmin, tmax;
    double dc, free_s;                         /* free_s: s from the last note-off until all voices free, -1 never */
    double ipc, ns, rel;                       /* CPU: instructions and ns per sample; rel: per mille of idle + drums */
    char msg[512];
} res_t;

static res_t R;
static uint32_t fpos, rel_at, dc_end, tail_from;
static int64_t dcs[2];

static int parts_free(void)                    /* every drum voice ended */
{
    uint32_t i;
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].active)
            return 0;
    return 1;
}

/* one block of the mix into the hash and the numbers */
static int32_t last_out[2 * CTL];
static void blk(void)
{
    int32_t *o = last_out;
    uint32_t i, k;
    mix_block(o, CTL);
    for (i = 0; i < 2u * CTL; i++) {
        int32_t x = o[i], a = x < 0 ? -x : x;
        uint32_t u = (uint32_t)x;
        for (k = 0; k < 4u; k++) {
            R.hash ^= (u >> (8u * k)) & 0xFFu;
            R.hash *= 0x100000001B3ull;
        }
        if (a > R.peak)
            R.peak = a;
        R.over += a > 32767;
        R.near += a >= LIM_NEAR;
        if (fpos < dc_end)
            dcs[i & 1u] += x;
        if (tail_from && fpos >= tail_from) {
            R.tmin = x < R.tmin ? x : R.tmin;
            R.tmax = x > R.tmax ? x : R.tmax;
        }
    }
    k = busy_now();
    if (k > R.vmax)
        R.vmax = k;
    fpos += CTL;
}

static uint32_t at(double s) { return (uint32_t)(s * FS) / CTL * CTL; }
static void run_to(uint32_t f) { while (fpos < f) blk(); }

/* after the last note-off (rel_at): until the voices are free, then the FX tail */
static void finish(void)
{
    uint32_t cap = rel_at + FREE_CAP_S * FS;
    dc_end = dc_end ? dc_end : fpos;
    while (!parts_free() && fpos < cap)
        blk();
    R.free_s = parts_free() ? (double)(fpos - rel_at) / FS : -1;
    tail_from = fpos + TAIL_S * FS - FS / 4u;
    R.tmin = 0x7FFFFFFF;
    R.tmax = -0x7FFFFFFF;
    run_to(fpos + TAIL_S * FS);
    R.tail_peak = (R.tmax - R.tmin + 1) / 2;
    R.tail_dc = (R.tmax + R.tmin) / 2;
    R.frames = fpos;
    R.dc = (double)(llabs(dcs[0]) > llabs(dcs[1]) ? dcs[0] : dcs[1]) / (double)(dc_end ? dc_end : 1);
}

/* --------------------------------------------------------- the drums --- */
/* the lanes (drums.c LANE_NOTE order) */
enum { LANE_KICK = 0, LANE_SNARE = 2, LANE_HAT = 4, LANE_OHAT = 5, LANE_LTOM = 9, LANE_HTOM = 11, LANE_CRASH = 12 };
/* a hit as the keys and MIDI give it (seq.c drum_input): GM note n's lane at velocity v's level */
static void hit(uint32_t n, uint32_t v) { drum_input(lane_of_note(n), vel_lvl(v)); }

/* ------------------------------------------------------------- jobs --- */
enum { J_DRUMS, J_GROOVE, J_INSERT, J_MASTER, J_CPU, J_CHECK };
typedef struct {
    char name[64];
    uint8_t kind, kit, arg, busy;              /* busy: the CPU job's busy pattern */
    int (*check)(char *msg, uint32_t n);       /* J_CHECK */
    res_t r;
    int crashed;
} job_t;

static void job_drums(const job_t *j)           /* every GM note through the drum track, a choke, a roll */
{
    uint32_t n, k = 0;
    (void)j;
    host_tracks_init();
    for (n = 35; n <= 81u; n++, k++) {
        hit(n, 60u + (n * 7u) % 60u);
        run_to(at(0.06 * (k + 1)));
    }
    hit(46, 100);                               /* open hat, choked by the closed one */
    run_to(fpos + FS / 10u);
    hit(42, 100);
    for (k = 0; k < 12u; k++) {                /* a snare roll: more hits than voices */
        hit(38, 40u + 6u * k);
        run_to(fpos + FS / 40u / CTL * CTL);
    }
    rel_at = fpos;
    dc_end = fpos;
    finish();
}

/* a groove on the drum track through the sequencer, 1/16 at 120 BPM: kick, snare (a ghost, a hard one), hats
 * (soft off-beats, a ratchet), the open hat, toms and a crash on FILL ONLY steps (lane marks) in its last bar;
 * busy: every lane on every step, the ratchets on (the CPU's worst case) */
static void groove(uint32_t kit, uint32_t busy)
{
    track_t *t = TDRUM;
    uint32_t i;
    host_tracks_init();
    song.g[G_BPM] = 120;
    t->p[P_E0] = (int16_t)kit;
    t->p[P_SLEN] = 32;
    t->p[P_SDIV] = 2;
    t->p[P_SSWING] = 20;
    for (i = 0; i < 32u; i++) {
        dstep_t *d = &t->dstep[i];
        uint32_t b = i % 16u, l;
        memset(d, 0, sizeof *d);
        if (busy) {
            for (l = 0; l < DRUM_LANES; l++)
                dstep_set(d, l, (i + l) % 4u, (i + l) % 7u == 0u ? 1u : 0u);
            continue;
        }
        if (b == 0u || b == 8u || b == 10u)
            dstep_set(d, LANE_KICK, LV_NORM, 0);
        if (b == 4u || b == 12u)
            dstep_set(d, LANE_SNARE, b == 12u && i >= 16u ? LV_HARD : LV_NORM, 0);
        if (b == 7u)
            dstep_set(d, LANE_SNARE, LV_GHOST, 0);
        if (!(b & 1u))
            dstep_set(d, b == 14u ? LANE_OHAT : LANE_HAT, b % 4u ? LV_SOFT : LV_NORM, b == 6u ? 1u : 0u);
        if (i >= 28u) {                         /* the fill: toms over the last beat, only in a fill */
            dstep_set(d, i & 1u ? LANE_LTOM : LANE_HTOM, LV_NORM, 0);
            drum_fonly[i] |= (uint16_t)(1u << (i & 1u ? LANE_LTOM : LANE_HTOM));
        }
    }
    dstep_set(&t->dstep[0], LANE_CRASH, LV_HARD, 0);
}
static void job_groove(const job_t *j)          /* two passes of the groove, the second with its fill bar */
{
    groove(j->kit, 0);
    transport_req = 1;
    run_to(at(4.0));
    fill_arm = 1;                               /* the next bar: a fill (GLO + key 10) */
    run_to(at(8.0));
    transport_req = 2;
    blk();
    rel_at = fpos;
    dc_end = fpos;
    finish();
}
static void job_insert(const job_t *j)          /* the drum track's INSERT (fx.c): type arg, its knobs half up */
{
    groove(DRUM_DS0, 0);   /* (808: the renders before the layered kit became the default) */
    TDRUM->p[P_ITYPE] = j->arg;
    TDRUM->p[P_IA] = TDRUM->p[P_IB] = TDRUM->p[P_IC] = 64;
    TDRUM->p[P_IMIX] = 100;
    transport_req = 1;
    run_to(at(4.0));
    transport_req = 2;
    blk();
    rel_at = fpos;
    dc_end = fpos;
    finish();
}
static void job_master(const job_t *j)          /* arg 0: DUST, 1: the DJ filter low-pass, 2: high-pass */
{
    groove(DRUM_DS0, 0);   /* (808: the renders before the layered kit became the default) */
    if (j->arg == 0u)
        song.g[G_DUST] = 90;
    else
        song.g[G_FILT] = j->arg == 1u ? -40 : 40;
    transport_req = 1;
    run_to(at(4.0));
    transport_req = 2;
    blk();
    song.g[G_DUST] = 0;                         /* (the record's hiss fades out with STOP: then silence) */
    rel_at = fpos;
    dc_end = fpos;
    finish();
}

/* CPU: the mix with the drums idle, a groove (kit), or the busy pattern; 0.5 s to settle, then 1 s counted */
/* this thread's CPU time: unlike the wall clock it does not count the time the host gave to others */
static uint64_t cpu_ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
/* the drums of the CPU mixes: kick on the 1, snare on the 3, hats on the 8ths */
static void cpu_drums(uint32_t k)
{
    if ((k * CTL) % (FS / 8u) < CTL)
        drum_on((k * CTL) % (FS / 2u) < CTL ? 36u : ((k * CTL) / (FS / 8u)) % 4u == 2u ? 38u : 42u, 100u);
}
/* the counted second (its own function: under callgrind, --toggle-collect=cpu_counted counts just this;
 * hostsim.c empties __attribute__, so the pragma keeps it out of line) */
#pragma GCC push_options
#pragma GCC optimize("no-inline")
static void cpu_counted(uint32_t nb, uint32_t drums_on)
{
    uint32_t k;
    for (k = nb / 2u; k < nb / 2u + nb; k++) {
        if (drums_on)
            cpu_drums(k);
        mix_block(last_out, CTL);
    }
}
#pragma GCC pop_options
static void job_cpu(const job_t *j)
{
    uint32_t k, nb = FS / CTL, drums_on = j->kit != 0xFFu && j->kit != 0xFEu;
    uint64_t i0, t0;
    if (j->kit == 0xFEu)
        host_tracks_init();                     /* idle: nothing plays */
    else if (j->busy || j->kit != 0xFFu)
        groove(j->kit == 0xFFu ? DRUM_DS0 : j->kit, j->busy);
    else
        host_tracks_init();
    if (j->busy || (j->kit != 0xFFu && j->kit != 0xFEu))
        transport_req = 1;
    drums_on = j->kit == 0xFFu && !j->busy;     /* (idle + drums: drum_on on 16ths, the sequencer stopped) */
    for (k = 0; k < nb / 2u; k++) {             /* 0.5 s to settle */
        if (drums_on)
            cpu_drums(k);
        mix_block(last_out, CTL);
    }
    {
        uint32_t reps = instr_now() || getenv("CPU_JOB") ? 1u : 3u, r;   /* timed: the best of three seconds */
        double best = 0;
        for (r = 0; r < reps; r++) {
            double ns;
            i0 = instr_now();
            t0 = cpu_ns_now();
            cpu_counted(nb, drums_on);
            ns = (double)(cpu_ns_now() - t0) / (nb * CTL);
            if (!r)
                R.ipc = i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0;
            if (!r || ns < best)
                best = ns;
        }
        R.ns = best;
    }
}
/* no instruction counter but valgrind (CPU_VALGRIND=1): each CPU job again in a child under callgrind,
 * which counts the instructions of cpu_counted exactly (the same on every run, ~40 x slower) */
static double cpu_callgrind(const char *self, const char *gpath, const char *cpath, const char *job)
{
    char cmd[1024], line[512];
    double got = 0;
    FILE *f;
    snprintf(cmd, sizeof cmd,
             "CPU_JOB='%s' valgrind --tool=callgrind --collect-atstart=no '--toggle-collect=cpu_counted*' "
             "--callgrind-out-file=/dev/null '%s' '%s' '%s' 2>&1 >/dev/null", job, self, gpath, cpath);
    if (!(f = popen(cmd, "r")))
        return 0;
    while (fgets(line, sizeof line, f)) {
        const char *c = strstr(line, "Collected :");
        if (c)
            got = atof(c + 11);
    }
    pclose(f);
    return got / ((double)(FS / CTL) * CTL);
}
/* a CPU job's cost in the unit this host has: instructions, else ns */
static double cpu_cost(const job_t *j) { return j->r.ipc ? j->r.ipc : j->r.ns; }

static int run_job_body(job_t *j)
{
    memset(&R, 0, sizeof R);
    switch (j->kind) {
    case J_DRUMS:
        job_drums(j);
        break;
    case J_GROOVE:
        job_groove(j);
        break;
    case J_INSERT:
        job_insert(j);
        break;
    case J_MASTER:
        job_master(j);
        break;
    case J_CPU:
        job_cpu(j);
        break;
    case J_CHECK:
        R.ok = (uint32_t)j->check(R.msg, sizeof R.msg);
        break;
    }
    return 0;
}

/* run jobs [0, n) in children, up to nj at once; results come back through pipes */
static int only_cpu;                                 /* CPU_JOB: no other job runs (cpu_callgrind's child) */
static void run_jobs(job_t *jobs, uint32_t n, uint32_t nj)
{
    uint32_t i, k;
    if (only_cpu)
        return;
    for (i = 0; i < n; i += nj) {
        int fd[64];
        pid_t pid[64];
        uint32_t m = n - i < nj ? n - i : nj;
        for (k = 0; k < m; k++) {
            int p[2];
            if (pipe(p)) {
                perror("pipe");
                exit(2);
            }
            fflush(stdout);
            if (!(pid[k] = fork())) {
                close(p[0]);
                run_job_body(&jobs[i + k]);
                if (write(p[1], &R, sizeof R) != sizeof R)
                    _exit(1);
                _exit(0);
            }
            close(p[1]);
            fd[k] = p[0];
        }
        for (k = 0; k < m; k++) {
            size_t got = 0;
            ssize_t r;
            int st = 0;
            while (got < sizeof(res_t) && (r = read(fd[k], (char *)&jobs[i + k].r + got, sizeof(res_t) - got)) > 0)
                got += (size_t)r;
            close(fd[k]);
            waitpid(pid[k], &st, 0);
            jobs[i + k].crashed = got != sizeof(res_t) || !WIFEXITED(st) || WEXITSTATUS(st);
        }
    }
}

/* ------------------------------------------------------- routing check --- */
static uint32_t lcg = 12345u;
static uint32_t rnd(uint32_t n)
{
    lcg = lcg * 1103515245u + 12345u;
    return (lcg >> 8) % n;
}
static void midi_pkt(uint32_t st, uint32_t d1, uint32_t d2)   /* as usb.c: the queue holds MQ packets */
{
    if (mi_w - mi_r >= MQ)
        blk();
    midi_in_q[mi_w++ % MQ] = (st >> 4) | st << 8 | d1 << 16 | d2 << 24;
}

/* no hanging notes: 6 s of random MIDI note-ons / offs on channels 1, 2, 3, 10, 5 and 16, the keys up and down,
 * the routing (GLO > MIDI > OTHER, GLO > SYSTEM > IN) changing, a groove playing; then everything off: every
 * drum voice free within FREE_CAP_S */
static int chk_hang(char *msg, uint32_t n)
{
    static const uint8_t CHS[6] = {0, 1, 2, 9, 4, 15};
    uint8_t on[6][128] = {{0}};
    uint32_t keys = 0, k, c, ev = 0, round;
    char who[96] = "";
    int bad = 0;
    for (round = 0; round < 3u && !bad; round++) {
        groove(DRUM_DS0, 0);   /* (808: the renders before the layered kit became the default) */
        transport_req = 1;
        for (k = 0; k < 6u * FS / CTL; k++) {
            uint32_t r = rnd(16);
            if (r < 6u) {                          /* MIDI */
                uint32_t ci = rnd(6), note = 35u + rnd(47);
                if (on[ci][note]) {
                    midi_pkt(rnd(2) ? 0x80u | CHS[ci] : 0x90u | CHS[ci], note, 0);
                    on[ci][note] = 0;
                } else {
                    midi_pkt(0x90u | CHS[ci], note, 1u + rnd(127));
                    on[ci][note] = 1;
                }
                ev++;
            } else if (r < 9u) {                   /* a key down or up */
                keys ^= 1u << rnd(27);
                fm1_in.notes = keys;
                ev++;
            } else if (r == 9u) {
                song.g[G_CHOTH] = (int16_t)rnd(2);  /* the routing changes */
                song.g[G_ROUTE] = (int16_t)(rnd(4) == 0 ? 2u + rnd(16) : 0u);
            }
            blk();
        }
        fm1_in.notes = keys = 0;                   /* everything off */
        song.g[G_ROUTE] = 0;
        for (c = 0; c < 6u; c++)
            for (k = 0; k < 128u; k++)
                if (on[c][k]) {
                    midi_pkt(0x80u | CHS[c], k, 64);
                    on[c][k] = 0;
                }
        transport_req = 2;
        rel_at = fpos;
        {
            uint32_t cap = fpos + FREE_CAP_S * FS;
            while (!parts_free() && fpos < cap)
                blk();
            if (!parts_free()) {
                bad = 1;
                snprintf(who, sizeof who, "a drum voice still active %d s after the note-offs", FREE_CAP_S);
            }
        }
    }
    snprintf(msg, n, "%u random MIDI / key events on ch 1-3, 10, 5, 16 and the keys, the routing changing, a groove "
             "playing, 3 rounds: %s", ev, bad ? who : "every voice free");
    return !bad;
}

/* ------------------------------------------------------------ files --- */
typedef struct { char name[64]; char val[64]; } kv_t;
static uint32_t load_kv(const char *path, kv_t *kv, uint32_t max)
{
    FILE *f = fopen(path, "r");
    char line[256];
    uint32_t n = 0;
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f) && n < max) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%63s %63s", kv[n].name, kv[n].val) == 2)
            n++;
    }
    fclose(f);
    return n;
}
static const char *kv_get(const kv_t *kv, uint32_t n, const char *name)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (!strcmp(kv[i].name, name))
            return kv[i].val;
    return 0;
}

static void slug(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; *s && i + 1 < n; s++)
        d[i++] = (*s == ' ' || *s == '/') ? '_' : *s;
    d[i] = 0;
}

/* ------------------------------------------------------------- main --- */
#define MAXJ 256
static job_t J[MAXJ];
static uint32_t nj;
static job_t *add(uint32_t kind, const char *name)
{
    job_t *j = &J[nj++];
    memset(j, 0, sizeof *j);
    j->kind = (uint8_t)kind;
    snprintf(j->name, sizeof j->name, "%s", name);
    return j;
}

int main(int argc, char **argv)
{
    const char *gpath = argc > 1 ? argv[1] : "tests/golden.txt";
    const char *cpath = argc > 2 ? argv[2] : "tests/cpu_baseline.txt";
    int gupd = getenv("GOLDEN_UPDATE") != 0, cupd = getenv("BUDGET_UPDATE") != 0, verbose = getenv("VERBOSE") != 0;
    uint32_t jobs_at_once = getenv("JOBS") ? (uint32_t)atoi(getenv("JOBS")) : 8u;
    static const char *const MN[3] = {"dust", "filter_lp", "filter_hp"};
    static kv_t gold[MAXJ], cpu[MAXJ];
    uint32_t ng, nc, i, g0, g1, c0, c1, k0, ci_idle, ci_ref;
    uint32_t g_changed = 0, g_new = 0, g_gone = 0, h_fail = 0, c_fail = 0, c_warn = 0, k_fail = 0, crash = 0;
    uint64_t t_start = now_ns();
    char name[64], s[64];
    if (jobs_at_once < 1u || jobs_at_once > 64u)
        jobs_at_once = 8;
    only_cpu = getenv("CPU_JOB") != 0;

    /* 1 + 2: the golden renders */
    g0 = nj;
    for (i = DRUM_DS0; i < DRUM_USR; i++) {      /* the synthesised kits (the user slots and SYN kits are the user's;
                                                  * MULDJORD, kit 0, below with its own groove) */
        job_t *j;
        slug(s, DRUM_KIT_NAMES[i], sizeof s);
        snprintf(name, sizeof name, "drums/groove/%02u_%s", i, s);
        j = add(J_GROOVE, name);
        j->kit = (uint8_t)i;
    }
    {                                           /* the layered kit (its ghosts and hards: the layers; the takes in turn) */
        job_t *j;
        slug(s, DRUM_KIT_NAMES[DRUM_REAL], sizeof s);
        snprintf(name, sizeof name, "drums/groove/%02u_%s", (unsigned)DRUM_REAL, s);
        j = add(J_GROOVE, name);
        j->kit = (uint8_t)DRUM_REAL;
    }
    for (i = IT_SOFT; i < IT_N; i++) {
        snprintf(name, sizeof name, "drums/insert/%u", i);
        add(J_INSERT, name)->arg = (uint8_t)i;
    }
    for (i = 0; i < 3u; i++) {
        snprintf(name, sizeof name, "drums/master/%s", MN[i]);
        add(J_MASTER, name)->arg = (uint8_t)i;
    }
    g1 = nj;
    {   /* determinism: the first render once more */
        job_t *j = add(J_GROOVE, "repeat");
        *j = J[g0];
        snprintf(j->name, sizeof j->name, "repeat");
    }

    /* 4: the routing check */
    k0 = nj;
    add(J_CHECK, "routing: no hanging notes")->check = chk_hang;
    run_jobs(J, nj, jobs_at_once);

    /* 3: CPU, one child at a time (the ns are for information; the instruction counts do not care) */
    c0 = nj;
    ci_idle = nj;
    add(J_CPU, "cpu/mix/idle")->kit = 0xFEu;
    ci_ref = nj;
    add(J_CPU, "cpu/mix/idle_drums")->kit = 0xFFu;
    {
        job_t *j = add(J_CPU, "cpu/drums/busy");
        j->kit = 0xFFu;
        j->busy = 1;
    }
    for (i = DRUM_DS0; i < DRUM_USR; i++) {
        slug(s, DRUM_KIT_NAMES[i], sizeof s);
        snprintf(name, sizeof name, "cpu/groove/%02u_%s", i, s);
        add(J_CPU, name)->kit = (uint8_t)i;
    }
    {                                                 /* the layered kit: its groove, and the busy pattern on it (every
                                                       * lane, long tails: the twelve voices' worst) */
        job_t *j;
        slug(s, DRUM_KIT_NAMES[DRUM_REAL], sizeof s);
        snprintf(name, sizeof name, "cpu/groove/%02u_%s", (unsigned)DRUM_REAL, s);
        add(J_CPU, name)->kit = (uint8_t)DRUM_REAL;
        j = add(J_CPU, "cpu/drums/busy_real");
        j->kit = (uint8_t)DRUM_REAL;
        j->busy = 1;
    }
    c1 = nj;
    if (getenv("CPU_JOB")) {                          /* one CPU job in this process (cpu_callgrind's child) */
        for (i = c0; i < c1; i++)
            if (!strcmp(J[i].name, getenv("CPU_JOB"))) {
                job_cpu(&J[i]);
                return 0;
            }
        return 2;
    }
    run_jobs(J + c0, c1 - c0, 1);
    if (!J[c0].r.ipc && getenv("CPU_VALGRIND") && !system("valgrind --version >/dev/null 2>&1")) {
        printf("regress: CPU: counting instructions under callgrind (%u jobs)...\n", c1 - c0);
        fflush(stdout);
        for (i = c0; i < c1; i++) {
            double ipc = cpu_callgrind(argv[0], gpath, cpath, J[i].name);
            if (ipc > 0)
                J[i].r.ipc = ipc;
        }
    }

    /* ---- report: goldens and health ---- */
    ng = load_kv(gpath, gold, MAXJ);
    for (i = g0; i < g1; i++) {
        job_t *j = &J[i];
        const res_t *r = &j->r;
        const char *want = kv_get(gold, ng, j->name);
        char hs[24], why[256] = "";
        snprintf(hs, sizeof hs, "%016llx", (unsigned long long)r->hash);
        if (j->crashed) {
            printf("regress: CRASH  %s\n", j->name);
            crash++;
            continue;
        }
        if (!want)
            g_new++;
        else if (strcmp(want, hs)) {
            if (!gupd)
                printf("regress: GOLDEN CHANGED  %s  (%s -> %s)\n", j->name, want, hs);
            g_changed++;
        }
        if (r->over)
            snprintf(why + strlen(why), sizeof why - strlen(why), " %u samples beyond 16 bits;", r->over);
        if (r->near)
            snprintf(why + strlen(why), sizeof why - strlen(why), " %u samples >= %d;", r->near, LIM_NEAR);
        if (fabs(r->dc) > LIM_DC)
            snprintf(why + strlen(why), sizeof why - strlen(why), " DC %.1f (limit %d);", r->dc, LIM_DC);
        if (r->peak < LIM_PEAK_MIN)
            snprintf(why + strlen(why), sizeof why - strlen(why), " peak %d (min %d);", r->peak, LIM_PEAK_MIN);
        if (r->free_s < 0)
            snprintf(why + strlen(why), sizeof why - strlen(why), " voices not free %d s after the note-offs;", FREE_CAP_S);
        if (abs(r->tail_dc) > LIM_TAIL_DC)
            snprintf(why + strlen(why), sizeof why - strlen(why), " DC %d left at the end (limit %d);", r->tail_dc,
                     LIM_TAIL_DC);
        if (r->tail_peak > LIM_TAIL)
            snprintf(why + strlen(why), sizeof why - strlen(why), " not silent at the end: peak %d (limit %d);",
                     r->tail_peak, LIM_TAIL);
        if (r->vmax > NDRUM)
            snprintf(why + strlen(why), sizeof why - strlen(why), " %u voices (of %u);", r->vmax, NDRUM);
        if (why[0]) {
            printf("regress: HEALTH FAIL  %s:%s\n", j->name, why);
            h_fail++;
        }
        if (verbose)
            printf("  %-44s %s  %5.1f s  peak %5d  dc %6.1f  free %5.2f s  tail %3d dc %3d  voices %u\n", j->name,
                   hs, (double)r->frames / FS, r->peak, r->dc, r->free_s, r->tail_peak, r->tail_dc, r->vmax);
    }
    for (i = 0; i < ng; i++) {                      /* in the file, not rendered any more */
        uint32_t k, found = 0;
        for (k = g0; k < g1 && !found; k++)
            found = !strcmp(J[k].name, gold[i].name);
        if (!found) {
            if (!gupd)
                printf("regress: GOLDEN GONE  %s (no such render now)\n", gold[i].name);
            g_gone++;
        }
    }
    if (J[g1].r.hash != J[g0].r.hash || J[g1].crashed) {
        printf("regress: NOT DETERMINISTIC  %s rendered twice: %016llx, %016llx\n", J[g0].name,
               (unsigned long long)J[g0].r.hash, (unsigned long long)J[g1].r.hash);
        h_fail++;
    }
    if (gupd) {
        FILE *f = fopen(gpath, "w");
        if (!f) {
            perror(gpath);
            return 2;
        }
        fprintf(f, "# FELUCCA golden renders (tests/regress.c): name, FNV-1a 64 of the output samples.\n"
                   "# Rewritten by GOLDEN_UPDATE=1 tests/run_tests.sh -- only for an intended change of the sound.\n");
        for (i = g0; i < g1; i++)
            fprintf(f, "%s %016llx\n", J[i].name, (unsigned long long)J[i].r.hash);
        fclose(f);
        printf("regress: golden file %s rewritten (%u renders; %u had changed, %u new, %u gone)\n", gpath, g1 - g0,
               g_changed, g_new, g_gone);
    } else if (g_new) {
        printf("regress: %u renders not in %s (new: GOLDEN_UPDATE=1 adds them)\n", g_new, gpath);
    }

    /* ---- routing ---- */
    for (i = k0; i < c0; i++) {
        int ok = !J[i].crashed && J[i].r.ok;
        printf("regress: %s: %s %s\n", J[i].name, J[i].crashed ? "crashed" : J[i].r.msg, ok ? "ok" : "FAIL");
        k_fail += !ok;
    }

    /* ---- CPU: every cost per mille of the idle + drums mix of this run (comparable across hosts and units) ---- */
    nc = load_kv(cpath, cpu, MAXJ);
    {
        double ref = cpu_cost(&J[ci_ref]), tol = J[c0].r.ipc ? CPU_TOL : CPU_TOL_NS;
        const char *unit = J[c0].r.ipc ? "instructions" : "time";
        if (ref <= 0)
            ref = 1;
        for (i = c0; i < c1; i++) {
            job_t *j = &J[i];
            const char *want = kv_get(cpu, nc, j->name);
            double v = cpu_cost(j) / ref * 1000.0, b = want ? atof(want) : 0;
            j->r.rel = v;
            if (j->crashed) {
                printf("regress: CRASH  %s\n", j->name);
                crash++;
                continue;
            }
            if (cupd)
                continue;
            if (!want)
                printf("regress: CPU %s: %.0f per mille of the idle + drums mix, no baseline (BUDGET_UPDATE=1 adds it)\n", j->name, v);
            else if (v > b * (1 + tol) && v > b + CPU_SLACK) {
                printf("regress: CPU OVER BUDGET  %s: %.0f (%s), baseline %.0f (+%.0f %%, limit +%.0f %%)%s\n",
                       j->name, v, unit, b, (v / b - 1) * 100, tol * 100, j->r.ipc ? "" : " -- timed: a note, not a failure");
                if (j->r.ipc)
                    c_fail++;                         /* timed costs are too noisy on a shared host to fail on */
                else
                    c_warn++;
            } else if (v < b * (1 - tol) && v < b - CPU_SLACK) {
                printf("regress: CPU note  %s: %.0f (%s), baseline %.0f (%.0f %%): faster? "
                       "BUDGET_UPDATE=1 to keep it\n", j->name, v, unit, b, (v / b - 1) * 100);
                c_warn++;
            }
        }
        if (!J[c0].r.ipc)
            printf("regress: CPU: no instruction counter on this host: timed (best of three seconds, limit +%.0f %%;"
                   " CPU_VALGRIND=1 counts them exactly under callgrind)\n", CPU_TOL_NS * 100);
    }
    if (cupd) {
        FILE *f = fopen(cpath, "w");
        if (!f) {
            perror(cpath);
            return 2;
        }
        fprintf(f, "# fm1-drummer host CPU baseline (tests/regress.c): cost per sample, per mille of the idle + drums mix\n"
                   "# (cpu/mix/idle_drums = 1000), cc -O2; %s on the host that wrote it. The check allows +%.0f %%\n"
                   "# (instructions) or +%.0f %% (timed). Rewritten by BUDGET_UPDATE=1.\n",
                J[c0].r.ipc ? "instructions counted" : "timed, best of three seconds", CPU_TOL * 100, CPU_TOL_NS * 100);
        for (i = c0; i < c1; i++)
            fprintf(f, "%s %.0f\n", J[i].name, J[i].r.rel);
        fclose(f);
        printf("regress: CPU baseline %s rewritten (%u entries)\n", cpath, c1 - c0);
    }
    for (i = c0; i < c1; i++)
        if (i == ci_idle || i == ci_ref || J[i].busy)
            printf("regress:   %-23s %6.0f %s  %6.1f ns\n", J[i].name + 4, J[i].r.ipc ? J[i].r.ipc : J[i].r.rel,
                   J[i].r.ipc ? "instr" : "/1000", J[i].r.ns);

    printf("regress: %u golden renders (%u changed, %u gone), %u health failures, %u routing checks failed, "
           "%u CPU entries over budget (%u notes), %u crashes; %.1f s\n", g1 - g0, gupd ? 0 : g_changed,
           gupd ? 0 : g_gone, h_fail, k_fail, cupd ? 0 : c_fail, c_warn, crash, (double)(now_ns() - t_start) / 1e9);
    if (!gupd && (g_changed || g_gone))
        printf("regress: the sound changed. If that is intended: GOLDEN_UPDATE=1 sh tests/run_tests.sh, "
               "then review and commit tests/golden.txt\n");
    return (!gupd && (g_changed || g_gone)) || h_fail || k_fail || (!cupd && c_fail) || crash;
}
