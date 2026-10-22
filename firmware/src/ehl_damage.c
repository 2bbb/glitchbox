/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ISHII 2bit (EsionHsrahLatigid) */
/* EHL damage (fx.c includes this after ins_wreck): five more INSERT types that break the sound the way a broken codec,
 * a broken fixed-point chip or a broken driver would, ported from the EHL plug-ins Nybble, Overflow, Bitcast,
 * WordShear and Underrun to the FM-1's integer audio path. Each is the pedal's (MIX 100 % the whole effect), its
 * output DC-blocked and bounded. A per-track xorshift drives the faults (s->ph: seeded at the start, no rng() from the
 * ISR's shared state).
 *   ADPCM  IMA ADPCM, encoded and decoded in the loop. BITS (A) keeps 2..5 bits of each code; ERR (B) flips code
 *          bits and knocks the decoder's step index, so the decoder drifts away from the encoder (the stream has no
 *          resync: it stays wrong until the signal walks it back); RATE (C) holds each code 1..16 samples
 *   OVRFL  a resonant state-variable low-pass in 16-bit words that wrap instead of clipping: FREQ (A), RES (B),
 *          DRIVE (C) into it. Past the edge the states fold over and the filter falls into limit cycles of its own
 *   MISRD  the samples read as another format, a segment at a time: MODE (A: SWAP the bytes, ULAW, SIGN, HALF, NIBL,
 *          SHIFT a byte, BREV the bits, MIX of them), PROB (B) of a segment being misread, SEG (C) 16 .. 2048 samples
 *   SHEAR  a word corrupter: BITS (A) 16 .. 2, a sample-and-hold whose CLOCK (C) jitters, and FAULT (B) bursts that
 *          XOR, rotate or swap the held words
 *   UNDRN  a driver that misses its deadline: LOAD (A) the chance a buffer is late, MODE (B: 0 REPEAT the last buffer,
 *          127 half ZERO half STALE, older memory), BUF (C) 32 .. 256 samples */

/* the codec and fault helpers (dmg_rng, dmg_chance, ima_step, ima_code): ehl_codec.c, before perform.c */
static inline int32_t dmg_dc(ins_t *s, int32_t y)
{
    s->dc += ((y << 8) - s->dc) >> 7;                  /* DC blocker, ~55 Hz: the faults wander slowly */
    return y - (s->dc >> 8);
}

static __attribute__((noinline)) void ins_adpcm(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, bits = 2u + (uint32_t)(clamp(t->p[P_IA], 0, 127) >> 5);   /* 2 .. 5 (5: the full code + no clear) */
    uint32_t err = dmg_chance(clamp(t->p[P_IB], 0, 127)), hold = 1u + ((127u - (uint32_t)clamp(t->p[P_IC], 0, 127)) >> 3);
    /* x: 0 encoder pred, 1 encoder index, 2 decoder pred, 3 decoder index, 4 the held output */
    for (i = 0; i < n; i++) {
        int32_t x = b[i];
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (!s->cnt) {
            uint32_t c = ima_code(clamp(x, -32768, 32767), s->x[0], s->x[1], bits > 4u ? 4u : bits), r;
            ima_step(&s->x[0], &s->x[1], c);           /* the encoder follows its own (clean) codes */
            r = dmg_rng(&s->ph);
            if ((r & 0xFFFFu) < err)
                c ^= 1u << ((r >> 16) & 3u);           /* a bit error in the stream */
            if (((r >> 18) & 0xFFFFu) < (err >> 2))
                s->x[3] = clamp(s->x[3] + (int32_t)((r >> 8) & 15u) - 7, 0, 88);   /* the decoder's step knocked */
            s->x[4] = ima_step(&s->x[2], &s->x[3], c);
            s->cnt = (uint8_t)hold;
        }
        s->cnt--;
        b[i] = pf_mix(x, dmg_dc(s, s->x[4]), s->w);
    }
}

static __attribute__((noinline)) void ins_ovfl(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    int32_t a = clamp(t->p[P_IA], 0, 127), r = clamp(t->p[P_IB], 0, 127), d = clamp(t->p[P_IC], 0, 127);
    int32_t f = 300 + a * a * 2, q = 65535 - r * 500, g = 16 + d;   /* Q15, Q15 (2 .. 0.06), Q4 (1x .. 8.9x) */
    /* x: 0 low, 1 band (16-bit words held in int32, wrapped on every write) */
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = b[i], in, lo, hi, bd;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        in = (int16_t)((clamp(x, -262143, 262143) * g) >> 4);           /* the drive wraps too */
        lo = (int16_t)(s->x[0] + ((f * s->x[1]) >> 15));
        hi = (int16_t)(in - lo - ((q * s->x[1]) >> 15));
        bd = (int16_t)(s->x[1] + ((f * hi) >> 15));
        s->x[0] = lo;
        s->x[1] = bd;
        b[i] = pf_mix(x, dmg_dc(s, lo), s->w);
    }
}

/* MISRD's readings of a 16-bit word u (prev: the word before it) */
enum { MR_SWAP, MR_ULAW, MR_SIGN, MR_HALF, MR_NIBL, MR_SHIFT, MR_BREV, MR_MIX };
static inline int32_t misread(uint32_t mode, uint32_t u, uint32_t prev)
{
    switch (mode) {
    case MR_SWAP:
        return (int16_t)(((u >> 8) | (u << 8)) & 0xFFFFu);
    case MR_ULAW: {                                     /* the high byte as a G.711 mu-law byte */
        uint32_t m = ~(u >> 8) & 0xFFu, e = (m >> 4) & 7u;
        int32_t mag = (int32_t)((((m & 15u) << 3) + 0x84u) << e) - 0x84;
        return m & 0x80u ? -mag : mag;
    }
    case MR_SIGN:
        return (int16_t)(u ^ 0x8000u);
    case MR_HALF: {                                     /* the word as an IEEE half: 1.0 is full scale */
        uint32_t e = (u >> 10) & 31u;
        int32_t mag = e >= 15u ? 32767 : (int32_t)(((1024u + (u & 1023u)) << e) >> 10);
        mag = mag > 32767 ? 32767 : mag;
        return u & 0x8000u ? -mag : mag;
    }
    case MR_NIBL:
        return (int16_t)(((u & 0x0F0Fu) << 4) | ((u & 0xF0F0u) >> 4));
    case MR_SHIFT:                                      /* a byte late: this word's high byte, the last word's low one */
        return (int16_t)(((prev & 0xFFu) << 8) | (u >> 8));
    default: {                                          /* BREV: the bits reversed */
        uint32_t v = u & 0xFFFFu;
        v = ((v >> 1) & 0x5555u) | ((v & 0x5555u) << 1);
        v = ((v >> 2) & 0x3333u) | ((v & 0x3333u) << 2);
        v = ((v >> 4) & 0x0F0Fu) | ((v & 0x0F0Fu) << 4);
        v = ((v >> 8) & 0x00FFu) | ((v & 0x00FFu) << 8);
        return (int16_t)v;
    }
    }
}

static __attribute__((noinline)) void ins_misrd(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, mode = (uint32_t)clamp(t->p[P_IA], 0, 127) >> 4, p = dmg_chance(clamp(t->p[P_IB], 0, 127));
    uint32_t seg = 16u << ((uint32_t)clamp(t->p[P_IC], 0, 127) >> 4);
    /* x: 0 samples left of the segment, 1 its reading (8: read right), 2 the last word */
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y;
        uint32_t u = (uint32_t)clamp(x, -32768, 32767) & 0xFFFFu;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (s->x[0] <= 0) {
            uint32_t r = dmg_rng(&s->ph);
            s->x[0] = (int32_t)seg;
            s->x[1] = (r & 0xFFFFu) < p ? (int32_t)(mode == MR_MIX ? (r >> 16) % 7u : mode) : 8;
        }
        s->x[0]--;
        y = s->x[1] == 8 ? x : misread((uint32_t)s->x[1], u, (uint32_t)s->x[2]) >> 1;   /* (at half: a misread word is loud) */
        s->x[2] = (int32_t)u;
        b[i] = pf_mix(x, dmg_dc(s, y), s->w);
    }
}

static __attribute__((noinline)) void ins_shear(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, sh = (uint32_t)clamp(t->p[P_IA], 0, 127) * 14u / 127u;           /* 0 .. 14 bits off */
    uint32_t p = dmg_chance(clamp(t->p[P_IB], 0, 127)) >> 3, base = 1u + ((127u - (uint32_t)clamp(t->p[P_IC], 0, 127)) >> 3);
    /* x: 0 the held word, 1 burst words left, 2 burst op (0 XOR 1 ROT 2 SWAP), 3 its mask / rotation, 4 the word before */
    for (i = 0; i < n; i++) {
        int32_t x = b[i];
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (!s->cnt) {
            uint32_t r = dmg_rng(&s->ph), u;
            int32_t v = clamp(x, -32768, 32767);
            v = sh ? (v >> sh) << sh : v;
            u = (uint32_t)v & 0xFFFFu;
            if (s->x[1] <= 0 && (r & 0xFFFFu) < p) {    /* a burst starts: 8 .. 256 words */
                s->x[1] = 8 << ((r >> 16) & 5u);
                s->x[2] = (int32_t)((r >> 20) % 3u);
                s->x[3] = (int32_t)((r >> 22) & 0xFFFFu) | 0x4000;
            }
            if (s->x[1] > 0) {
                s->x[1]--;
                if (s->x[2] == 0)
                    u ^= (uint32_t)s->x[3] & ~((1u << sh) - 1u);
                else if (s->x[2] == 1) {
                    uint32_t k = 1u + ((uint32_t)s->x[3] & 14u);
                    u = ((u << k) | (u >> (16u - k))) & 0xFFFFu;
                } else {
                    uint32_t o = u;
                    u = (uint32_t)s->x[4];
                    s->x[4] = (int32_t)o;
                }
            }
            if (s->x[2] != 2 || s->x[1] <= 0)
                s->x[4] = (int32_t)u;
            s->x[0] = (int16_t)u;
            s->cnt = (uint8_t)(base + (r >> 27) % base);   /* the clock jitters by up to a period */
        }
        s->cnt--;
        b[i] = pf_mix(x, dmg_dc(s, s->x[0]), s->w);
    }
}

static __attribute__((noinline)) void ins_undrn(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, p = dmg_chance(clamp(t->p[P_IA], 0, 127)), md = (uint32_t)clamp(t->p[P_IB], 0, 127);
    uint32_t buf = 32u << ((uint32_t)clamp(t->p[P_IC], 0, 127) >> 5);           /* 32 .. 256 */
    /* line: the input, at half; x: 0 samples left of this buffer, 1 its fate (0 on time, 1 REPEAT, 2 ZERO, 3 STALE),
     * 2 the read offset back from the write position */
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (s->x[0] <= 0) {
            uint32_t r = dmg_rng(&s->ph);
            s->x[0] = (int32_t)buf;
            if ((r & 0xFFFFu) >= p)
                s->x[1] = 0;
            else if (((r >> 16) & 127u) >= md)
                s->x[1] = 1;
            else
                s->x[1] = (r >> 23) & 1u ? 2 : 3;
            s->x[2] = s->x[1] == 3 && buf < IL_LEN ? (int32_t)(buf * (2u + (r >> 24) % (IL_LEN / buf - 1u))) : (int32_t)buf;
            if (s->x[2] > (int32_t)IL_LEN)
                s->x[2] = IL_LEN;
        }
        s->x[0]--;
        if (s->x[1] == 0)
            y = x;
        else if (s->x[1] == 2)
            y = 0;
        else
            y = s->line[(s->wp - (uint32_t)s->x[2]) & (IL_LEN - 1u)] * 2;
        s->line[s->wp] = (int16_t)clamp(x >> 1, -32767, 32767);
        s->wp = (uint16_t)((s->wp + 1u) & (IL_LEN - 1u));
        b[i] = pf_mix(x, dmg_dc(s, y), s->w);
    }
}

/* the damage types' picture for the INSERT page (ui_graph.c graph_insert): v in (a voice's full scale 32768), out */
static int32_t dmg_curve(const track_t *t, uint32_t ty, int32_t v)
{
    int32_t a = clamp(t->p[P_IA], 0, 127);
    uint32_t u = (uint32_t)v & 0xFFFFu;                 /* (no clamp: the graph's v stays within +-1.5 x; a clamp
                                                         * there folded into a compare against -32768 x 110, which
                                                         * build.py's check reads as a chip ROM address) */
    switch (ty) {
    case IT_ADPCM: {                                    /* steps as coarse as BITS leaves the code */
        int32_t st = 2048 << (3 - (a >> 5));
        return (v / st) * st;
    }
    case IT_OVFL:                                       /* the drive's wrap */
        return (int16_t)((v * (16 + clamp(t->p[P_IC], 0, 127))) >> 4);
    case IT_MISRD:
        return misread((uint32_t)a >> 4 == MR_MIX ? MR_SWAP : (uint32_t)a >> 4, u, u) >> 1;
    case IT_SHEAR: {
        uint32_t sh = (uint32_t)a * 14u / 127u;
        return (int16_t)(((u >> sh) << sh) ^ (v > 0 ? 0x2400u : 0u));
    }
    default:                                            /* UNDRN: the dry line, with the buffers it drops */
        return ((uint32_t)(v + 32768) >> 12) % 3u ? v : 0;
    }
}

static __attribute__((noinline)) void ins_damage(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    if (!s->ph)
        s->ph = 0x2545F491u ^ ((uint32_t)(t - trk) * 0x9E3779B9u);
    switch (s->type) {
    case IT_ADPCM: ins_adpcm(s, t, b, n, tw); break;
    case IT_OVFL: ins_ovfl(s, t, b, n, tw); break;
    case IT_MISRD: ins_misrd(s, t, b, n, tw); break;
    case IT_SHEAR: ins_shear(s, t, b, n, tw); break;
    default: ins_undrn(s, t, b, n, tw); break;
    }
}
