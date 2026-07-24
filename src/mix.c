#include "bitops.h"
#include "mix.h"

/*
 * The G structure and rotation distances follow the 64-bit ARX quarter-round
 * used by BLAKE2b. FCH uses a different initialization, tweak layout,
 * message mode, and tree construction; it is not an implementation of
 * BLAKE2b and does not inherit BLAKE2b's security claims.
 */

static const uint64_t FCH_MIX_IV[8] = {
    UINT64_C(0x6A09E667F3BCC908),
    UINT64_C(0xBB67AE8584CAA73B),
    UINT64_C(0x3C6EF372FE94F82B),
    UINT64_C(0xA54FF53A5F1D36F1),
    UINT64_C(0x510E527FADE682D1),
    UINT64_C(0x9B05688C2B3E6C1F),
    UINT64_C(0x1F83D9ABFB41BD6B),
    UINT64_C(0x5BE0CD19137E2179)
};

static const uint8_t FCH_MIX_SIGMA[FCH_MIX_ROUNDS][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 }
};

static uint64_t fch_rotr64(uint64_t value, unsigned int count) {
    return fch_rotl64(value, 64u - (count & 63u));
}

static void fch_mix_g(
    uint64_t v[16],
    size_t a,
    size_t b,
    size_t c,
    size_t d,
    uint64_t x,
    uint64_t y
) {
    v[a] = v[a] + v[b] + x;
    v[d] = fch_rotr64(v[d] ^ v[a], 32u);
    v[c] += v[d];
    v[b] = fch_rotr64(v[b] ^ v[c], 24u);
    v[a] = v[a] + v[b] + y;
    v[d] = fch_rotr64(v[d] ^ v[a], 16u);
    v[c] += v[d];
    v[b] = fch_rotr64(v[b] ^ v[c], 63u);
}

static void fch_mix_round(
    uint64_t v[16],
    const uint64_t message[16],
    unsigned int round
) {
    const uint8_t *s = FCH_MIX_SIGMA[round];

    fch_mix_g(v, 0, 4, 8, 12, message[s[0]], message[s[1]]);
    fch_mix_g(v, 1, 5, 9, 13, message[s[2]], message[s[3]]);
    fch_mix_g(v, 2, 6, 10, 14, message[s[4]], message[s[5]]);
    fch_mix_g(v, 3, 7, 11, 15, message[s[6]], message[s[7]]);
    fch_mix_g(v, 0, 5, 10, 15, message[s[8]], message[s[9]]);
    fch_mix_g(v, 1, 6, 11, 12, message[s[10]], message[s[11]]);
    fch_mix_g(v, 2, 7, 8, 13, message[s[12]], message[s[13]]);
    fch_mix_g(v, 3, 4, 9, 14, message[s[14]], message[s[15]]);
}

int fch_mix_init(
    uint64_t *state,
    size_t state_words,
    uint64_t domain
) {
    if (!state || state_words != 8u)
        return 0;

    for (size_t i = 0; i < state_words; i++)
        state[i] = FCH_MIX_IV[i];

    state[0] ^= domain;
    state[1] ^= UINT64_C(0x4643482D41525831);
    state[2] ^= (uint64_t)state_words << 56u;
    state[state_words - 1u] ^= UINT64_C(0x434F52452D563031);
    return 1;
}

int fch_mix_compress(
    uint64_t *state,
    size_t state_words,
    const uint8_t block[FCH_MIX_BLOCK_SIZE],
    size_t block_length,
    uint64_t counter,
    uint64_t domain,
    uint64_t flags
) {
    if (!state || !block || block_length > FCH_MIX_BLOCK_SIZE)
        return 0;
    if (state_words != 8u)
        return 0;

    uint64_t message[16];
    uint64_t work[16];

    for (size_t i = 0; i < 16u; i++)
        message[i] = fch_load_le64(block + i * 8u);

    for (size_t i = 0; i < 8u; i++) {
        work[i] = state[i];
        work[i + 8u] = FCH_MIX_IV[i];
    }

    work[12] ^= counter;
    work[13] ^= (uint64_t)block_length;
    work[13] ^= (uint64_t)state_words << 56u;
    work[14] ^= domain;
    work[15] ^= flags;

    for (unsigned int round = 0; round < FCH_MIX_ROUNDS; round++)
        fch_mix_round(work, message, round);

    for (size_t i = 0; i < 8u; i++)
        state[i] ^= work[i] ^ work[i + 8u];

    return 1;
}

int fch_mix_finalize_output(
    uint64_t *state,
    size_t state_words,
    size_t output_words
) {
    if (!state || state_words != 8u)
        return 0;
    if (output_words != 4u && output_words != 8u)
        return 0;

    const uint64_t domain = output_words == 4u
        ? UINT64_C(0x4643484F55543235)
        : UINT64_C(0x4643484F55543531);
    uint8_t block[FCH_MIX_BLOCK_SIZE] = {0};

    fch_store_le64(block + 0u, UINT64_C(0x4643482D4F555431));
    fch_store_le64(block + 8u, (uint64_t)output_words * 64u);
    fch_store_le64(block + 16u, (uint64_t)state_words * 64u);
    fch_store_le64(block + 24u, FCH_MIX_ROUNDS);

    return fch_mix_compress(
        state,
        state_words,
        block,
        32u,
        (uint64_t)output_words * 8u,
        domain,
        FCH_MIX_FLAG_OUTPUT | FCH_MIX_FLAG_FINAL
    );
}
