/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ISHII 2bit (EsionHsrahLatigid) */
/* EHL damage's shared helpers (fx.c includes this before perform.c): a fault generator and an IMA ADPCM codec.
 * Used by the damage INSERTs (ehl_damage.c) and the FX layer's ADPCM (perform.c). */

/* IMA_STEP, IMA_IDX: eng_sample.c's (the sample engine decodes the same IMA ADPCM) */

static inline uint32_t dmg_rng(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
/* the chance p in 0..127 as a threshold on 16 random bits, squared: gentle at first */
static inline uint32_t dmg_chance(int32_t p) { return (uint32_t)(p * p) * 4u; }

/* IMA ADPCM: one step of the decoder (pred, idx) for a 4-bit code */
static inline int32_t ima_step(int32_t *pred, int32_t *idx, uint32_t code)
{
    int32_t st = IMA_STEP[*idx], d = st >> 3;
    if (code & 4u) d += st;
    if (code & 2u) d += st >> 1;
    if (code & 1u) d += st >> 2;
    *pred = clamp(code & 8u ? *pred - d : *pred + d, -32768, 32767);
    *idx = clamp(*idx + IMA_IDX[code & 7u], 0, 88);
    return *pred;
}
/* .. and the encoder's code for x, keeping `bits` of it (2..4 bits: the low magnitude bits cleared) */
static inline uint32_t ima_code(int32_t x, int32_t pred, int32_t idx, uint32_t bits)
{
    int32_t d = x - pred, st = IMA_STEP[idx];
    uint32_t c = 0;
    if (d < 0) { c = 8u; d = -d; }
    if (d >= st) { c |= 4u; d -= st; }
    st >>= 1;
    if (d >= st) { c |= 2u; d -= st; }
    st >>= 1;
    if (d >= st) c |= 1u;
    return bits >= 4u ? c : c & (0xFu << (4u - bits)) & 0xFu;
}

