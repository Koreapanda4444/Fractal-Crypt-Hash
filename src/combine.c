#include <stdlib.h>
#include "combine.h"
#include "params.h"
#include "sbox.h"
#include "bitops.h"

fch_state_t fch_combine(
    fch_state_t *children,
    size_t count,
    size_t state_words,
    int depth
) {
    fch_state_t out = { NULL, state_words };

    if (!children || count == 0 || state_words == 0) {
        return out;
    }

    out.words = state_words;
    out.state = (uint64_t *)calloc(state_words, sizeof(uint64_t));

    if (!out.state) {
        return out;
    }

    for (size_t i = 0; i < state_words; i++) {
        out.state[i] = 0xA5A5A5A5A5A5A5A5ULL ^ (uint64_t)(i * 0x1234567);
    }

    for (size_t c = 0; c < count; c++) {
        fch_state_t *child = &children[c];
        size_t cw = child->words;

        if (!child->state || cw == 0 || cw > state_words) {
            free(out.state);
            out.state = NULL;
            return out;
        }

        for (size_t i = 0; i < cw; i++) {
            size_t idx = (c + i) % state_words;

            out.state[idx] ^= child->state[i];
            out.state[idx] += fch_rotl64(
                child->state[(i + 1) % cw],
                (unsigned int)((i + c + 1u) * 9u)
            );
        }

        for (size_t i = 0; i < state_words; i++) {
            out.state[i] =
                fch_rotl64(out.state[i], (unsigned int)((i + c) % 31u + 1u));
        }
    }

    int d = depth;
    if (d < 0) d = 0;

    {
        const int rounds = 1;
        uint64_t acc = 0xD6E8FEB86659FD93ULL ^ (uint64_t)count;
        acc ^= ((uint64_t)state_words << 32) ^ (uint64_t)(unsigned)d;

        for (int r = 0; r < rounds; r++) {
            for (size_t i = 0; i < state_words; i++) {
                uint64_t a = out.state[i];
                uint64_t b = out.state[(i + 1) % state_words];

                acc ^= a + 0x9E3779B97F4A7C15ULL + (uint64_t)i + (uint64_t)(r * 0x10007);

                a ^= fch_rotl64(acc, (unsigned int)(((i * 7u) + (unsigned)d + (unsigned)r) % 63u + 1u));
                a += fch_rotl64(b ^ 0xA5A5A5A5A5A5A5A5ULL,
                                (unsigned int)(((i * 11u) + (unsigned)count + (unsigned)r) % 63u + 1u));
                out.state[i] = fch_rotl64(a, (unsigned int)(((i * 5u) + 17u + (unsigned)r) % 63u + 1u));
            }
        }
    }

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

            a ^= fch_rotl64(
                b + 0x9E3779B97F4A7C15ULL + (uint64_t)(d + p),
                (unsigned int)(((i + (size_t)p) * 13u) % 63u + 1u)
            );
            a += fch_rotl64(
                c ^ (0xA5A5A5A5A5A5A5A5ULL + (uint64_t)count + (uint64_t)(p * 0x10001)),
                (unsigned int)(((i + (size_t)p) * 17u) % 63u + 1u)
            );

            out.state[i] = a;
        }
    }

    for (size_t i = 0; i < state_words; i++) {
        out.state[i] = fch_sbox64(out.state[i]);
    }

    return out;
}
