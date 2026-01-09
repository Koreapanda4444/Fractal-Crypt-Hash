#include <stdlib.h>
#include <string.h>

#include "combine.h"
#include "params.h"
#include "sbox.h"

static inline uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

fch_state_t fch_combine(
    fch_state_t *children,
    size_t count,
    size_t state_words,
    int depth
) {
    fch_state_t out;
    out.words = state_words;
    out.state = (uint64_t *)calloc(state_words, sizeof(uint64_t));

    if (!out.state || !children || count == 0 || state_words == 0) {
        return out;
    }

    for (size_t i = 0; i < state_words; i++) {
        out.state[i] = 0xA5A5A5A5A5A5A5A5ULL ^ (uint64_t)(i * 0x1234567);
    }

    for (size_t c = 0; c < count; c++) {
        fch_state_t *child = &children[c];
        size_t cw = child->words;

        if (!child->state || cw == 0)
            continue;

        for (size_t i = 0; i < cw; i++) {
            size_t idx = (c + i) % state_words;

            out.state[idx] ^= child->state[i];
            out.state[idx] += rotl64(
                child->state[(i + 1) % cw],
                (int)((i + c + 1) * 9)
            );
        }

        for (size_t i = 0; i < state_words; i++) {
            out.state[i] =
                rotl64(out.state[i], (int)((i + c) % 31 + 1));
        }
    }

    int d = depth;
    if (d < 0) d = 0;

    int extra_passes = 0;
    if (d >= 2) extra_passes = 1;
    if (d >= 4) extra_passes = 2;
    if (d >= 6) extra_passes = 3;
    if (extra_passes > 3) extra_passes = 3;

    for (int p = 0; p < extra_passes; p++) {
        for (size_t i = 0; i < state_words; i++) {
            uint64_t a = out.state[i];
            uint64_t b = out.state[(i + 1) % state_words];
            uint64_t c = out.state[(i + state_words - 1) % state_words];

            a ^= rotl64(
                b + 0x9E3779B97F4A7C15ULL + (uint64_t)(d + p),
                (int)(((i + (size_t)p) * 13) % 63 + 1)
            );
            a += rotl64(
                c ^ (0xA5A5A5A5A5A5A5A5ULL + (uint64_t)count + (uint64_t)(p * 0x10001)),
                (int)(((i + (size_t)p) * 17) % 63 + 1)
            );

            out.state[i] = a;
        }
    }

    for (size_t i = 0; i < state_words; i++) {
        uint8_t *b = (uint8_t *)&out.state[i];
        for (int j = 0; j < 8; j++) {
            b[j] = FCH_SBOX[b[j]];
        }
    }

    return out;
}
