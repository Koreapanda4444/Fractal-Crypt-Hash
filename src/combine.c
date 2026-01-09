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
    size_t state_words
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

    for (size_t i = 0; i < state_words; i++) {
        uint8_t *b = (uint8_t *)&out.state[i];
        for (int j = 0; j < 8; j++) {
            b[j] = FCH_SBOX[b[j]];
        }
    }

    return out;
}
