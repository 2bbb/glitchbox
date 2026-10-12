/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Glitchbox contributors */
/* GLITCH (Glitchbox): a harsh mono/duo synth voice that chops itself up. Engine 7 (WHEEL's slot: the stored engine
 * numbers stay below 14, favorites.c / midi_learn.c keep 14 and 15 for themselves).
 *
 * Signal per voice: oscillator (WAVE) -> DRV (soft clip x1..x4 below 64, a triangle wavefolder x1..x8 above, into the
 * soft clip) -> the trapezoidal SVF low-pass (CUT, RES; RES 127 nearly self-oscillates; the ADSR / ENV DEST / LFO /
 * matrix move CUT as on ANALOG) -> CRSH (sample-and-hold 1..13 samples and 16..2 bits together) -> the micro-stutter
 * -> a DC block. From the filter on the signal runs at half scale (its soft knee: +-32383 at most), as ANALOG's.
 *   WAVE  SAW, SQR, DUAL (two saws 13 cents apart), RING (a saw ring-modulated by a sine at 2.375 x: metal), NOIS
 *
 * The micro-stutter (GLCH, SIZE, PJMP): each voice records its own output into a 3072-sample ring (70 ms) in the
 * part's engine memory. Time runs in segments of SIZE (256..3072 samples, 6..70 ms). At the end of each segment a
 * dice roll against GLCH decides: play live, or loop the last segment (recording pauses while it loops). A loop picks
 * a variation each segment: the whole segment, its half or its quarter (rolls), forwards or reversed, at the pitch or
 * jumped (PJMP: 0 none .. 127 any of -12 -7 -5 +5 +7 +12 semitones, by tape speed). Changes cross-fade over 32
 * samples (no clicks; the cuts stay hard). The first segment after a note-on always plays live (the attack). GLCH 0
 * costs nothing (no recording). Deterministic: the dice are seeded by the note and its velocity.
 *
 * Voices: at most 2 (poly), each with its own ring; MONO / LEGATO use the first. The filter state is kept on a
 * retrigger (keep), the ring is not. */
static const char *const N_GLITCH_WAVE[] = {"SAW", "SQR", "DUAL", "RING", "NOIS"};
enum { GW_SAW, GW_SQR, GW_DUAL, GW_RING, GW_NOIS };

#define GL_POLY 2u
#define GL_BUF 3072u
#define GL_XF 32                 /* cross-fade, samples */
typedef struct {
    int16_t buf[GL_BUF];         /* the voice's output, recorded while it plays live */
    uint16_t w;                  /* next write index */
    uint16_t filled;             /* samples recorded since the note-on (to GL_BUF) */
    uint16_t cnt;                /* samples left in the segment */
    uint16_t start;              /* the loop's first sample (index into buf) */
    uint16_t llen;               /* the loop's length, samples */
    uint8_t loop;                /* 1 = playing the loop, 0 = live */
    uint8_t xf;                  /* cross-fade samples left */
    int32_t rpos;                /* read position in the loop, Q8 */
    int32_t spd;                 /* read speed, Q8 (256 = 1x, negative = reversed) */
    int32_t xfrom;               /* the sample the cross-fade starts from */
    int32_t last;                /* the last sample out */
    int32_t rng;
} gl_slot_t;
ENG_MEM_FITS("GLCH", sizeof(gl_slot_t) * GL_POLY);

/* tape speeds of the jumps, Q8, mildest first (PJMP opens them in this order): +7 -5 +12 -12 +5 -7 semitones */
static const int16_t GL_JUMP[6] = {384, 192, 512, 128, 342, 171};

static gl_slot_t *gl_slot_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < GL_POLY ? (gl_slot_t *)ENG_MEM(t - trk) + i : 0;
}

/* the segment length of SIZE: 256..3072 samples */
static uint32_t gl_seg(int32_t size) { return 256u + (uint32_t)clamp(size, 0, 127) * (GL_BUF - 256u) / 127u; }

static void glitch_note_on(track_t *t, voice_t *v)
{
    gl_slot_t *S = gl_slot_of(t, v);
    v->ph[1] = v->ph[0] + 0x40000000u;
    v->s[0] = v->s[1] = 0;                            /* filter (kept on a retrigger: keep) */
    if (!v->s[2])
        v->s[2] = 0x2545F491 ^ (int32_t)v->note;      /* noise */
    v->s[3] = 0;                                      /* CRSH: the held sample */
    v->s[4] = 0;                                      /* CRSH: the hold counter */
    v->s[5] = 0;                                      /* DC block, Q8 */
    if (S) {
        S->filled = 0;
        S->loop = 0;
        S->xf = 0;
        S->last = 0;
        S->cnt = (uint16_t)gl_seg(t->p[P_E6]);
        S->rng = (int32_t)(0x9E3779B9u ^ ((uint32_t)v->note << 8) ^ ((uint32_t)v->vel << 20));
        if (!S->rng)
            S->rng = 1;
    }
}

/* a triangle wavefolder: continuous, +-32767 out for any x (two's complement wrap of x + 32768 into 0..131071) */
static inline int32_t gl_fold(int32_t x)
{
    uint32_t u = (uint32_t)(x + 32768) & 0x1FFFFu;
    return u < 65536u ? (int32_t)u - 32768 : 98303 - (int32_t)u;
}

/* the end of a segment: live or a loop, and the loop's variation */
static void gl_segment(gl_slot_t *S, const int16_t *p, uint32_t seg)
{
    uint32_t r = noise32(&S->rng), was = S->loop;
    int32_t chance = p[P_E5] * 258;                   /* Q15 */
    S->cnt = (uint16_t)seg;
    if ((int32_t)(r >> 17) >= chance || S->filled < seg) {
        S->loop = 0;
    } else {
        uint32_t q = noise32(&S->rng), span = ((uint32_t)p[P_E7] * 6u + 126u) / 127u;   /* jumps to pick from: 0..6 */
        if (!was)                                     /* loop the last segment */
            S->start = (uint16_t)((S->w + GL_BUF - seg) % GL_BUF);
        S->loop = 1;
        S->llen = (uint16_t)(seg >> ((q >> 4) % 3u == 2u ? (q >> 6) % 2u + 1u : 0u));   /* rolls: 1/2, 1/4 */
        if (S->llen < 32u)
            S->llen = 32u;
        S->spd = 256;
        if (span && (q & 3u) != 0u)                   /* 3 in 4 loops jump when PJMP allows */
            S->spd = GL_JUMP[(q >> 8) % span];
        if ((q & 0x30u) == 0x30u)                     /* 1 in 4 reversed */
            S->spd = -S->spd;
        S->rpos = S->spd < 0 ? ((int32_t)S->llen << 8) - 1 : 0;
    }
    if (S->loop || was) {                             /* a new loop or back to live: cross-fade from here */
        S->xf = GL_XF;
        S->xfrom = S->last;
    }
}

static void glitch_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    gl_slot_t *S = gl_slot_of(t, v);
    uint32_t wave = (uint32_t)p[P_E0], i, seg = gl_seg(p[P_E6]);
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], inc = m->inc, inc2;
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2], held = v->s[3], hc = v->s[4], dc = v->s[5];
    int32_t drv = p[P_E3], crsh = p[P_E4], g = 4096 + drv * 7 * 4096 / 127;   /* drive Q12: x1..x8 */
    int32_t hold = 1 + crsh / 10, sh = crsh * 14 / 127;                        /* CRSH: 1..13 samples, 0..14 bits off */
    int glitch = S && p[P_E5] > 0;
    tsvf_t flt;
    tsvf_coef(&flt, (p[P_E1] << 8) + m->cutoff, p[P_E2]);
    inc2 = wave == GW_RING ? (inc >> 3) * 19u : inc + (inc >> 7);   /* RING 2.375 x, DUAL +13 cents */
    for (i = 0; i < n; i++) {
        int32_t s, y;
        switch (wave) {
        case GW_SQR:
            s = osc_pulse(ph0, inc, 0x80000000u) << 1;
            break;
        case GW_DUAL:
            s = (osc_saw(ph0, inc) + osc_saw(ph1, inc2)) >> 1;
            break;
        case GW_RING:
            s = mulq15(osc_saw(ph0, inc), osc_sine(ph1));
            s += s >> 1;
            break;
        case GW_NOIS:
            s = (int32_t)(noise32(&nst) >> 17) - 16384;
            s <<= 1;
            break;
        default:
            s = osc_saw(ph0, inc);
            break;
        }
        ph0 += inc;
        ph1 += inc2;
        if (drv) {                                    /* soft clip, or the folder above 64 */
            int32_t x = (clamp(s, -32767, 32767) * (g >> 2)) >> 10;
            s = drv < 64 ? softclip(x) : softclip(gl_fold(x) + (gl_fold(x) >> 2));
        }
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);        /* the filter at half scale, then a soft knee */
        {
            int32_t a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
        }
        s = y;                                        /* (half scale from here: +-32383 at most, no clamp) */
        if (crsh) {                                   /* rate and bits */
            if (--hc <= 0) {
                hc = hold;
                held = (s >> sh) << sh;
            }
            s = held;
        }
        if (glitch) {
            if (!S->loop) {
                S->buf[S->w] = (int16_t)s;
                if (++S->w >= GL_BUF)
                    S->w = 0;
                if (S->filled < GL_BUF)
                    S->filled++;
            } else {                                  /* the loop, read between samples */
                int32_t L8 = (int32_t)S->llen << 8, k = S->rpos >> 8, fr = S->rpos & 255, a, b;
                uint32_t ia = S->start + (uint32_t)k, ib = (uint32_t)k + 1u < S->llen ? ia + 1u : S->start;
                if (ia >= GL_BUF)                     /* (the next sample: the loop's own, wrapping inside it) */
                    ia -= GL_BUF;
                if (ib >= GL_BUF)
                    ib -= GL_BUF;
                a = S->buf[ia];
                b = S->buf[ib];
                s = a + (((b - a) * fr) >> 8);
                S->rpos += S->spd;
                if (S->rpos >= L8)
                    S->rpos -= L8;
                else if (S->rpos < 0)
                    S->rpos += L8;
            }
            if (S->xf) {
                s += ((S->xfrom - s) * S->xf) / GL_XF;
                S->xf--;
            }
            S->last = s;
            if (!--S->cnt)
                gl_segment(S, p, seg);
        }
        dc += s - (dc >> 8);                          /* DC block, ~27 Hz (the folder, the bits, the loops); dc Q8: */
        s -= dc >> 8;                                 /* the shift's floor costs 1/256 of a step, not 255 steps */
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = held;
    v->s[4] = hc;
    v->s[5] = dc;
}

/* {WAVE, CUT, RES, DRV, CRSH, GLCH, SIZE, PJMP} */
static const preset_t GLITCH_PRESETS[] = {
    {"DESTROYER", {GW_SAW, 70, 100, 90, 30, 70, 40, 60}, {0, 60, 100, 30}, 30, 1, FX(40, 0, 30, 20), PAT(4)},
    {"FUZZ BASS", {GW_SQR, 50, 60, 110, 0, 0, 64, 0}, {0, 50, 90, 20}, 40, 1, FX(30, 0, 0, 5), PAT(2)},
    {"BROKEN", {GW_RING, 90, 80, 70, 60, 100, 20, 100}, {0, 70, 80, 40}, 20, 0, FX(0, 0, 40, 30), PAT(13)},
    {"SCREAM", {GW_DUAL, 60, 120, 80, 20, 40, 60, 30}, {5, 80, 110, 50}, 50, 1, FX(20, 10, 40, 30), PAT(4)},
    {"NOIZ HIT", {GW_NOIS, 80, 110, 100, 70, 90, 10, 0}, {0, 40, 0, 20}, 60, 0, FX(20, 0, 30, 20), PAT(12)},
    {"HUM", {GW_DUAL, 40, 90, 60, 10, 20, 127, 20}, {60, 90, 127, 90}, 10, 1, FX(0, 30, 30, 60), PAT(8)},
};

static const engine_t ENG_GLITCH = {
    .name = "GLCH",
    .page_title = {"TONE", "CHOP"},
    .edit = {
        {"WAVE", F_ENUM, 0, 4, 0, N_GLITCH_WAVE, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 60, 0, 0},
        {"DRV", F_PCT, 0, 127, 60, 0, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},
        {"GLCH", F_PCT, 0, 127, 40, 0, 0},
        {"SIZE", F_PCT, 0, 127, 48, 0, 0},
        {"PJMP", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = GLITCH_PRESETS,
    .npresets = NELEM(GLITCH_PRESETS),
    .note_on = glitch_note_on,
    .render = glitch_render,
    .knob = {P_E1, P_E2, P_E3, P_E5},
    .poly = GL_POLY,
    .keep = 0x03,                /* the filter */
    .mem = sizeof(gl_slot_t) * GL_POLY,
};
