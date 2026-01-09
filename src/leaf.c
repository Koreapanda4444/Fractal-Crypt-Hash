#include <string.h>
#include <stdlib.h>

#include "leaf.h"
#include "params.h"
#include "sbox.h"

static inline uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

void fch_leaf_compress(
    const uint8_t *data,
    size_t length,
    fch_state_t *out,
    int depth
) {
    if (!out || !out->state || out->words == 0)
        return;
    if (!data && length > 0)
        return;

    size_t S = out->words;
    uint64_t *state = out->state;

    for (size_t i = 0; i < S; i++) {
        state[i] = 0x9E3779B97F4A7C15ULL ^ (uint64_t)i;
    }

    for (size_t i = 0; i < length; i++) {
        size_t idx = i % S;
        uint64_t v = (uint64_t)data[i];

        state[idx] ^= v;
        state[idx] += rotl64(state[(idx + 1) % S], (idx + 3) * 7);
        state[(idx + 2) % S] ^= rotl64(state[idx], (idx + 5) * 11);
    }

    for (size_t i = 0; i < S; i++) {
        uint8_t *b = (uint8_t *)&state[i];
        for (int j = 0; j < 8; j++) {
            b[j] = FCH_SBOX[b[j]];
        }
    }

    int d = depth;
    if (d < 0) d = 0;

    int do_fold = 1;
    if (d < 2)
        do_fold = 0;

    if (do_fold) {
        size_t half = S / 2;
        for (size_t i = 0; i < half; i++) {
            state[i] ^= state[i + half];
        }
        out->words = half;
    }
}
