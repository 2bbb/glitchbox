/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Glitchbox contributors */
/* Glitchbox sounds on the Mac (src/eng_glitch.c, KIT DSTR in src/eng_drum.c), through hostsim.c as noise_test.c.
 *   build/host/glitch_test [DEMODIR]          (tests/quick_tests.sh: build/demo/glitch)
 * 1. the stutter works: GLCH 127 renders differently from GLCH 0; at GLCH 0, SIZE and PJMP change nothing.
 * 2. determinism: the same note from silence renders the same samples again (the dice are seeded).
 * 3. no clipping: every WAVE at the corners of DRV / CRSH / RES / GLCH / PJMP, 2 notes at velocity 127, LEVEL 127:
 *    the voice sum within 2 x VOICE_FS a voice, no mix sample at full scale.
 * 4. no DC: every preset at GLCH 0, a note held 4 s (|mean| of the last 3 s < 1 % of a voice's full scale); with its
 *    loops under 5 % (a loop cut out of a wave is lopsided: sub-audio, not DC).
 * 5. silence after the release: every preset, the voices free and the part silent 1 s after the note-off.
 * 6. KIT DSTR: louder (RMS) than STD at the same settings (it is driven), no mix sample at full scale, voices free.
 * 7. cost: host instructions per sample and voice, GLITCH's heaviest corner against ANALOG's SAW LEAD (at most
 *    GLITCH_COST_X times it).
 * 8. the FX layer's Glitchbox effects (perform.c) on a held GLITCH note at 120 BPM: GATE silences the second half of
 *    each 1/32; ROLL halves its loop each beat down to a 1/64 and stays there; CRSH SWP reaches full CRUSH after a bar;
 *    RANDOM plays at least three different things (none included) over 4 bars; each let go: the layer idle again.
 * 9. demos into DEMODIR: every GLITCH preset playing its suggested pattern, the DESTROY kit's beat, a jam
 *    (DESTROY beat + DESTROYER line + HUM drone), and the jam under ROLL, GATE, CRSH SWP and RANDOM, as WAVs. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define GLITCH_COST_X 2.0

static int bad;
static void check(const char *what, int ok)
{
    printf("glitch: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t eng_of(const engine_t *e)
{
    uint32_t i;
    for (i = 0; i < NENGINES; i++)
        if (ENGINES[i] == e)
            return i;
    return 0;
}
static uint32_t preset_of(const engine_t *e, const char *name)
{
    uint32_t i;
    for (i = 0; i < e->npresets; i++)
        if (str_eq(e->presets[i].name, name))
            return i;
    return 0;
}

/* track 0 GLITCH with e[0..7] (0: preset pi's), a flat envelope, no sends */
static void setup(uint32_t pi, const int16_t *e)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], eng_of(&ENG_GLITCH), pi);
    if (e)
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 10;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_DIST] = 0;                             /* (the track's DIST is asymmetric on purpose: fx.c) */
}

/* the part's voice sum for note over n samples (whole blocks), from silence */
static void voices(uint32_t pi, const int16_t *e, uint32_t note, int32_t *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k;
    setup(pi, e);
    trk_note_on(&trk[0], note, 100);
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k];
    }
}

#define NS (FS / 2u / CTL * CTL)
static int32_t ya[NS], yb[NS];
#define NL (4u * FS / CTL * CTL)            /* 4 s: a low resonance's part cycle weighs little */
static int32_t yl[NL];

static void stutter(void)
{
    int16_t e[8] = {0, 80, 60, 60, 0, 0, 40, 0};
    uint32_t i, diff = 0, same = 1;
    voices(0, e, 48, ya, NS);
    e[5] = 127;                                       /* GLCH */
    e[7] = 127;                                       /* PJMP */
    voices(0, e, 48, yb, NS);
    for (i = 0; i < NS; i++)
        diff += ya[i] != yb[i];
    check("GLCH 127 chops the sound (differs from GLCH 0 in more than 20 % of the samples)", diff > NS / 5u);
    e[5] = 0;
    e[6] = 127;
    voices(0, e, 48, yb, NS);
    for (i = 0; i < NS; i++)
        same &= ya[i] == yb[i];
    check("GLCH 0: SIZE and PJMP change nothing", same);
}

static void determinism(void)
{
    uint32_t pi, i, same = 1;
    for (pi = 0; pi < ENG_GLITCH.npresets; pi++) {
        voices(pi, 0, 50, ya, NS);
        voices(pi, 0, 50, yb, NS);
        for (i = 0; i < NS; i++)
            same &= ya[i] == yb[i];
    }
    check("every preset renders the same samples again from silence", same);
}

static void clipping(void)
{
    static const int16_t DRV[3] = {0, 63, 127}, CR[2] = {0, 127};
    static int32_t o[2 * CTL], b[CTL];
    uint32_t w, d, c, i, k, worst = 0, full = 0;
    for (w = 0; w < NELEM(N_GLITCH_WAVE); w++)
        for (d = 0; d < 3u; d++)
            for (c = 0; c < 2u; c++) {
                int16_t e[8] = {(int16_t)w, 127, 127, DRV[d], CR[c], 127, 20, 127};
                setup(0, e);
                trk[0].p[P_LEVEL] = 127;
                trk_note_on(&trk[0], 36, 127);
                trk_note_on(&trk[0], 60, 127);
                for (i = 0; i < FS / CTL; i++) {
                    mix_block(o, CTL);
                    for (k = 0; k < 2u * CTL; k++)
                        full += o[k] >= 32767 || o[k] <= -32768;
                }
                setup(0, e);
                trk_note_on(&trk[0], 36, 127);
                trk_note_on(&trk[0], 60, 127);
                for (i = 0; i < FS / CTL; i++) {
                    track_render(&trk[0], b, CTL);
                    for (k = 0; k < CTL; k++) {
                        uint32_t a = (uint32_t)(b[k] < 0 ? -b[k] : b[k]);
                        if (a > worst)
                            worst = a;
                    }
                }
            }
    check("every WAVE x DRV x CRSH at RES / GLCH / PJMP 127: the voice sum within 2 x VOICE_FS a voice",
          worst <= 2u * 2u * VOICE_FS);
    check("  .. and no mix sample at full scale", !full);
}

/* the mean of a held note's last 3 s (4 s held), over a voice's full scale; GLCH forced to glch (-1: the preset's) */
static double held_mean(uint32_t pi, int32_t glch)
{
    int16_t e[8];
    uint32_t i;
    double s = 0;
    for (i = 0; i < 8u; i++)
        e[i] = ENG_GLITCH.presets[pi].e[i];
    if (glch >= 0)
        e[5] = (int16_t)glch;
    voices(pi, e, 48, yl, NL);
    for (i = NL / 4u; i < NL; i++)
        s += yl[i];
    return fabs(s / (NL - NL / 4u)) / VOICE_FS;
}
static void dc(void)
{
    uint32_t pi;
    double worst = 0, loops = 0, a, b;
    for (pi = 0; pi < ENG_GLITCH.npresets; pi++) {
        a = held_mean(pi, 0);
        b = held_mean(pi, -1);
        if (getenv("VERBOSE"))
            printf("glitch:   DC %-10s %.4f  (with its loops %.4f)\n", ENG_GLITCH.presets[pi].name, a, b);
        worst = a > worst ? a : worst;
        loops = b > loops ? b : loops;
    }
    check("no DC: every preset at GLCH 0, the mean of a held note under 1 % of a voice's full scale", worst < 0.01);
    check("  .. with its loops (a cut loop of a wave is lopsided: sub-audio, not DC) under 5 %", loops < 0.05);
}

static void release(void)
{
    static int32_t b[CTL];
    uint32_t pi, i, k, ok = 1;
    for (pi = 0; pi < ENG_GLITCH.npresets; pi++) {
        host_preset(&trk[0], eng_of(&ENG_GLITCH), pi);
        setup(pi, 0);
        trk_note_on(&trk[0], 48, 100);
        for (i = 0; i < FS / 2u / CTL; i++)
            track_render(&trk[0], b, CTL);
        trk_note_off(&trk[0], 48);
        for (i = 0; i < FS / CTL; i++)
            track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL; k++)
            ok &= b[k] == 0;
        for (k = 0; k < NVOICE; k++)
            ok &= !trk[0].v[k].active;
    }
    check("every preset: silent and every voice free 1 s after the note-off", ok);
}

/* the DRUM part's RMS over a bar of the BEAT pattern with KIT kit and DRV drv; *full: mix samples at full scale */
static double kit_rms(int32_t kit, int32_t drv, uint32_t *full)
{
    static int32_t o[2 * CTL];
    uint32_t s, i, k, n = 0, pat = 13u;               /* PATTERNS[13]: DESTROY */
    double e = 0;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_DRUM, 0);
    trk[0].p[P_E0] = (int16_t)kit;
    trk[0].p[P_E7] = (int16_t)drv;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_DIST] = 0;
    for (s = 0; s < 16u; s++) {
        if (PATTERNS[pat].note[s])
            trk_note_on(&trk[0], PATTERNS[pat].note[s], PATTERNS[pat].flags[s] & 1u ? 127 : 100);
        for (i = 0; i < FS / 8u; i += CTL) {
            mix_block(o, CTL);
            for (k = 0; k < 2u * CTL; k++) {
                e += (double)o[k] * o[k];
                *full += o[k] >= 32767 || o[k] <= -32768;
                n++;
            }
        }
        if (PATTERNS[pat].note[s])
            trk_note_off(&trk[0], PATTERNS[pat].note[s]);
    }
    return sqrt(e / n);
}

static void destroy_kit(void)
{
    static int32_t o[2 * CTL];
    uint32_t full = 0, i, k, busy = 0;
    double std = kit_rms(DK_STD, 0, &full), dstr;
    full = 0;
    dstr = kit_rms(DK_DSTR, 127, &full);
    check("KIT DSTR at DRV 127: louder than STD at DRV 0 (driven: RMS +2 dB or more)", dstr > std * 1.26);
    check("  .. no mix sample at full scale", !full);
    for (i = 0; i < 3u * FS / CTL; i++)
        mix_block(o, CTL);
    for (k = 0; k < NVOICE; k++)
        busy += trk[0].v[k].active;
    check("  .. every voice free 3 s after the last hit", !busy);
    check("power-on (GLITCHBOX_DEFAULTS): T1 CODEC ROT, T2 BIT ROT BASS, T3 UNDERRUN, T4 DRUM DESTROY, sequencers empty",
          GLITCHBOX_DEFAULTS && TRK_DEF[0][0] == 7u && str_eq(ENG_GLITCH.presets[TRK_DEF[0][1]].name, "CODEC ROT") &&
          TRK_DEF[1][0] == 7u && str_eq(ENG_GLITCH.presets[TRK_DEF[1][1]].name, "BIT ROT BASS") &&
          TRK_DEF[2][0] == 7u && str_eq(ENG_GLITCH.presets[TRK_DEF[2][1]].name, "UNDERRUN") &&
          TRK_DEF[3][0] == ENGI_DRUM && str_eq(ENG_DRUM.presets[TRK_DEF[3][1]].name, "DESTROY") &&
          !TRK_DEF[0][2] && !TRK_DEF[1][2] && !TRK_DEF[2][2] && !TRK_DEF[3][2]);
    check("PATTERNS[13] is DESTROY and DRUM's preset 1 is DESTROY (KIT DSTR, PAT 14)",
          str_eq(PATTERNS[13].name, "DESTROY") && ENG_DRUM.npresets > 1u && str_eq(ENG_DRUM.presets[1].name, "DESTROY") &&
          ENG_DRUM.presets[1].e[0] == DK_DSTR && ENG_DRUM.presets[1].pat == 14u);
}

/* ------------------------------------------------------------ FX layer --- */
static void fx_setup(void)
{
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, "HUM"));
    trk[0].p[P_E5] = 0;                               /* (no loops of its own: a steady drone) */
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    song.g[G_BPM] = 120;
    perf_held = perf_latched = 0;
    trk_note_on(&trk[0], 48, 110);
}
/* nb blocks of the mix; the RMS of the left channel at positions (in a 1/32) below / above its half into *op, *cl */
static void fx_run(uint32_t nb, double *op, double *cl)
{
    static int32_t o[2 * CTL];
    uint32_t i, k, q = beat_samples() / 8u, pos = 0, no = 0, nc = 0;
    double eo = 0, ec = 0;
    for (i = 0; i < nb; i++) {
        uint32_t base = pf.ph;
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++) {
            uint32_t at = (base + k) % q;
            double v = (double)o[2 * k] * o[2 * k];
            if (at > q / 16u && at < q / 2u - q / 16u)
                eo += v, no++;
            else if (at > q / 2u + q / 16u && at < q - q / 16u)
                ec += v, nc++;
        }
        (void)pos;
    }
    if (op)
        *op = no ? sqrt(eo / no) : 0;
    if (cl)
        *cl = nc ? sqrt(ec / nc) : 0;
}
static int fx_idle(void)
{
    uint32_t i;
    perf_held = 0;
    for (i = 0; i < FS / 2u / CTL && pf.busy; i++)
        fx_run(1, 0, 0);
    return !pf.busy && !perf_act && pf.mode == BM_NONE;
}
static void fx_layer(void)
{
    double op, cl;
    uint32_t i, seen = 0, beat;
    fx_setup();
    fx_run(FS / 4u / CTL, 0, 0);
    perf_press(PF_GATE, 1);
    fx_run(FS / CTL, &op, &cl);
    perf_press(PF_GATE, 0);
    check("GATE: the second half of each 1/32 at least 30 dB under the first", op > 300.0 && cl < op / 31.6);
    check("  .. let go: the layer idle again", fx_idle());

    fx_setup();
    beat = beat_samples();
    perf_press(PF_ROLL, 1);
    fx_run(beat / 2u / CTL, 0, 0);
    i = pf.len;
    fx_run(5u * beat / CTL, 0, 0);
    perf_press(PF_ROLL, 0);
    check("ROLL: starts as a 1/8, halves each beat to a 1/64 and holds it there",
          pf.src == PF_ROLL && i == beat / 2u && pf.len == beat / 16u && pf.rls == ROLL_STEPS);
    check("  .. let go: the layer idle again", fx_idle());

    fx_setup();
    perf_press(PF_CRSW, 1);
    fx_run(2u * beat_samples() / CTL, 0, 0);          /* half a bar */
    i = (uint32_t)pf.cc;
    fx_run(3u * beat_samples() / CTL, 0, 0);
    perf_press(PF_CRSW, 0);
    check("CRSH SWP: half way after half a bar, full (100) after a bar", i > 40u && i < 60u && pf.cc >= 100);
    check("  .. let go: the layer idle again", fx_idle());

    fx_setup();
    perf_press(PF_RAND, 1);
    for (i = 0; i < 16u * beat_samples() / CTL; i++) {
        fx_run(1, 0, 0);
        seen |= pf.rpick == PF_N ? 1u : 1u << (pf.rpick + 1u);
    }
    perf_press(PF_RAND, 0);
    check("RANDOM: over 4 bars nothing, REPEAT 1/16, REPEAT 1/32, REVERSE and GATE each come up",
          seen == (1u | 1u << (PF_R16 + 1u) | 1u << (PF_R32 + 1u) | 1u << (PF_REV + 1u) | 1u << (PF_GATE + 1u)));
    check("  .. let go: the layer idle again", fx_idle());
}

/* ---------------------------------------------------------------- cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
/* instructions per sample of the mix with track 0 on engine eng, preset pi (e: its E values, 0 = the preset's),
 * notes held; voice cap permitting */
static double mix_cost(uint32_t eng, uint32_t pi, const int16_t *e, uint32_t notes)
{
    static int32_t o[2 * CTL];
    uint32_t i, nb = FS * 2u / CTL;
    uint64_t i0;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], eng, pi);
    if (e)
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_VOICE] = V_POLY;
    for (i = 0; i < notes; i++)
        trk_note_on(&trk[0], 48 + 3 * i, 100);
    for (i = 0; i < 64u; i++)
        mix_block(o, CTL);
    i0 = instr_now();
    for (i = 0; i < nb; i++)
        mix_block(o, CTL);
    return (double)(instr_now() - i0) / (nb * CTL);
}
static void cost(void)
{
    static const int16_t HEAVY[8] = {GW_FAT, 127, 127, 127, 127, 127, 0, 127};
    double idle, an, gl;
    char what[160];
    if (!instr_now()) {
        printf("glitch: cost: no instruction counter on this host; not measured\n");
        return;
    }
    idle = mix_cost(eng_of(&ENG_GLITCH), 0, HEAVY, 0);
    an = (mix_cost(0, 0, 0, 2) - idle) / 2;           /* ANALOG SAW LEAD, 2 voices */
    gl = (mix_cost(eng_of(&ENG_GLITCH), 0, HEAVY, 2) - idle) / 2;
    snprintf(what, sizeof what, "cost: GLITCH's heaviest %.0f instructions a sample and voice, ANALOG SAW LEAD %.0f (at most %.1f x)",
             gl, an, GLITCH_COST_X);
    check(what, gl <= an * GLITCH_COST_X);
}

/* --------------------------------------------------------------- demos --- */
static FILE *demo_open(const char *dir, const char *name)
{
    char path[512];
    FILE *w;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if ((w = fopen(path, "wb")))
        wav_hdr(w, 0);
    return w;
}
static int32_t demo_peak;                            /* the largest |sample| the demos wrote (EHL damage checks) */
static uint32_t demo_blocks(FILE *w, uint32_t nb)
{
    static int32_t o[2 * CTL];
    uint32_t i, j;
    for (i = 0; i < nb; i++) {
        mix_block(o, CTL);
        for (j = 0; j < 2u * CTL; j++)
            demo_peak = abs(o[j]) > demo_peak ? abs(o[j]) : demo_peak;
        for (j = 0; j < CTL; j++)
            wav_put(w, o[2 * j], o[2 * j + 1]);
    }
    return nb * CTL;
}
static void demo_close(FILE *w, uint32_t frames)
{
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames);
    fclose(w);
}

/* the parts' patterns (PATTERNS[] index + 1, 0 none), 16ths at bpm for bars bars */
static uint32_t play(FILE *w, const uint32_t *pat, uint32_t bars, uint32_t bpm)
{
    uint32_t s, p, frames = 0, held[NPART] = {0}, nb = FS * 60u / bpm / 4u / CTL;
    for (s = 0; s < 16u * bars; s++) {
        for (p = 0; p < NPART; p++) {
            uint32_t k = s % 16u, note, fl;
            if (!pat[p])
                continue;
            note = PATTERNS[pat[p] - 1u].note[k];
            fl = PATTERNS[pat[p] - 1u].flags[k];
            if (!(fl & 4u) && held[p]) {
                trk_note_off(&trk[p], held[p]);
                held[p] = 0;
            }
            if (note && !(fl & 4u)) {
                trk_note_on(&trk[p], note, fl & 1u ? 120 : 90);
                held[p] = note;
            }
        }
        frames += demo_blocks(w, nb);
    }
    for (p = 0; p < NPART; p++)
        if (held[p])
            trk_note_off(&trk[p], held[p]);
    return frames + demo_blocks(w, FS * 2u / CTL);
}

static void demos(const char *dir)
{
    uint32_t pi, frames, i;
    char nm[48];
    FILE *w;
    for (pi = 0; pi < ENG_GLITCH.npresets; pi++) {
        uint32_t pat[NPART] = {0};
        const preset_t *pr = &ENG_GLITCH.presets[pi];
        snprintf(nm, sizeof nm, "glitch_%02u_", pi);
        for (i = 0; pr->name[i] && i < 20u; i++)
            nm[10 + i] = pr->name[i] == ' ' ? '_' : pr->name[i];
        nm[10 + i] = 0;
        if (!(w = demo_open(dir, nm)))
            return;
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        host_preset(&trk[0], eng_of(&ENG_GLITCH), pi);
        pat[0] = pr->pat ? pr->pat : 4u;
        demo_close(w, play(w, pat, 4, 120));
    }
    if ((w = demo_open(dir, "destroy_kit"))) {
        uint32_t pat[NPART] = {14u, 0, 0, 0};
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        host_preset(&trk[0], ENGI_DRUM, 1);
        demo_close(w, play(w, pat, 4, 110));
    }
    if ((w = demo_open(dir, "jam"))) {                /* DESTROY beat + DESTROYER line + HUM drone */
        uint32_t pat[NPART] = {14u, 4u, 8u, 0};
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        host_preset(&trk[0], ENGI_DRUM, 1);
        host_preset(&trk[1], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, "DESTROYER"));
        host_preset(&trk[2], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, "HUM"));
        trk[2].p[P_LEVEL] = 80;
        frames = play(w, pat, 8, 110);
        demo_close(w, frames);
    }
    {   /* the jam with each Glitchbox FX key held through bars 3 and 4 (of 4) */
        static const uint8_t E[4] = {PF_ROLL, PF_GATE, PF_CRSW, PF_RAND};
        static const char *const N[4] = {"jam_fx_roll", "jam_fx_gate", "jam_fx_crush_sweep", "jam_fx_random"};
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            uint32_t pat[NPART] = {14u, 4u, 8u, 0};
            if (!(w = demo_open(dir, N[k])))
                return;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], ENGI_DRUM, 1);
            host_preset(&trk[1], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, "DESTROYER"));
            host_preset(&trk[2], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, "HUM"));
            trk[2].p[P_LEVEL] = 80;
            song.g[G_BPM] = 110;
            perf_held = 0;
            frames = play(w, pat, 2, 110);
            perf_press(E[k], 1);
            frames += play(w, pat, 2, 110);
            perf_press(E[k], 0);
            frames += play(w, pat, 1, 110);
            demo_close(w, frames);
        }
    }
    {   /* EHL damage: the jam with each damage FX held through bars 3 and 4; afterwards the layer falls idle again */
        static const uint8_t E[4] = {PF_WRAP, PF_BFLP, PF_UNDR, PF_ADPC};
        static const char *const N[4] = {"jam_fx_wrap", "jam_fx_bitflip", "jam_fx_underrun", "jam_fx_adpcm"};
        uint32_t k, idle = 1;
        demo_peak = 0;
        for (k = 0; k < 4u; k++) {
            uint32_t pat[NPART] = {14u, 4u, 8u, 0};
            if (!(w = demo_open(dir, N[k])))
                return;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], ENGI_DRUM, 1);
            host_preset(&trk[1], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, "DESTROYER"));
            host_preset(&trk[2], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, "HUM"));
            trk[2].p[P_LEVEL] = 80;
            song.g[G_BPM] = 110;
            perf_held = 0;
            frames = play(w, pat, 2, 110);
            perf_press(E[k], 1);
            frames += play(w, pat, 2, 110);
            perf_press(E[k], 0);
            frames += play(w, pat, 1, 110);
            idle &= !pf.ww && !pf.bw && !pf.aw && !pf.ulen;
            demo_close(w, frames);
        }
        check("EHL FX: WRAP BITFLIP UNDERRUN ADPCM on the jam stay within full scale, and let go fall idle",
              demo_peak <= 32768 && idle);
    }
    {   /* EHL damage: the new presets in the jam's place of DESTROYER */
        static const char *const P[6] = {"CODEC ROT", "OVERFLOW", "MISREAD", "SHEARED", "UNDERRUN", "BIT ROT BASS"};
        uint32_t k;
        demo_peak = 0;
        for (k = 0; k < 6u; k++) {
            uint32_t pat[NPART] = {14u, 4u, 0, 0};
            snprintf(nm, sizeof nm, "jam_ins_%s", P[k]);
            for (i = 8; nm[i]; i++)
                nm[i] = nm[i] == ' ' ? '_' : nm[i];
            if (!(w = demo_open(dir, nm)))
                return;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], ENGI_DRUM, 1);
            host_preset(&trk[1], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, P[k]));
            pat[1] = ENG_GLITCH.presets[preset_of(&ENG_GLITCH, P[k])].pat;
            demo_close(w, play(w, pat, 4, 110));
        }
        check("EHL presets: CODEC ROT .. BIT ROT BASS over the DESTROY beat stay within full scale", demo_peak <= 32768);
    }
    printf("glitch: demos in %s: %u GLITCH presets, destroy_kit, jam, jam_fx_* (ROLL GATE CRSH SWP RANDOM WRAP BITFLIP "
           "UNDERRUN ADPCM), jam_ins_* (EHL damage)\n", dir, (uint32_t)ENG_GLITCH.npresets);
}

/* EHL damage at MASTER 0: the FX layer runs after the MASTER level, and BITFLIP / ADPCM make sound from silence; what
 * they add follows MASTER (perform.c dmg_master), so MASTER 0 with all four latched over a playing note is silent.
 * (master_out's ~2 Hz DC blocker lets the level from before MASTER 0 fall over seconds: not zero samples, but no
 * sound. The check is on what is heard: the largest step between samples after 50 ms) */
static int32_t ehl_master0_step(uint32_t latched)
{
    static int32_t o[2 * CTL];
    uint32_t i, j, m0 = song.master_q12;
    int32_t pl = 0, pr = 0, step = 0;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[1], eng_of(&ENG_GLITCH), preset_of(&ENG_GLITCH, "DESTROYER"));
    trk_note_on(&trk[1], 48, 110);
    song.master_q12 = 0;
    perf_latched = latched;
    for (i = 0; i < FS / CTL; i++) {
        mix_block(o, CTL);
        for (j = 0; j < CTL; j++) {
            int32_t dl = abs(o[2 * j] - pl), dr = abs(o[2 * j + 1] - pr);
            if (i >= FS / 20u / CTL)
                step = dl > step ? dl : dr > step ? dr : step;
            pl = o[2 * j];
            pr = o[2 * j + 1];
        }
    }
    perf_latched = 0;
    song.master_q12 = m0;
    for (i = 0; i < FS / 4u / CTL; i++)
        mix_block(o, CTL);
    trk_note_off(&trk[1], 48);
    return step;
}
static void ehl_master0(void)
{
    int32_t base = ehl_master0_step(0), dmg = ehl_master0_step(PF_DMG);
    printf("glitch:   MASTER 0, a note held: largest sample step %d without the FX layer, %d with WRAP BITFLIP UNDERRUN "
           "ADPCM latched\n", base, dmg);
    check("EHL FX: MASTER 0 with WRAP BITFLIP UNDERRUN ADPCM latched adds no sound (steps no larger than without them)",
          dmg <= base);
}

int main(int argc, char **argv)
{
    stutter();
    determinism();
    clipping();
    dc();
    release();
    destroy_kit();
    fx_layer();
    ehl_master0();
    if (!getenv("NOCOST"))
        cost();
    if (argc > 1)
        demos(argv[1]);
    printf("glitch_test: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
