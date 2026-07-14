#include <stdlib.h>

#include "combine.h"
#include "sbox.h"
#include "bitops.h"

fch_state_t fch_combine(
    fch_state_t *children,
    const fch_block_t *blocks,
    size_t count,
    size_t node_length,
    size_t state_words,
    int depth
) {
    fch_state_t out = { NULL, state_words };

    if (!children || !blocks || count == 0 || state_words == 0)
        return out;

    unsigned int normalized_depth = depth < 0 ? 0u : (unsigned int)depth;
    uint64_t domain = normalized_depth == 0u
        ? UINT64_C(0x524F4F544E4F4445)
        : UINT64_C(0x494E544E4F444531);

    out.state = (uint64_t *)calloc(state_words, sizeof(uint64_t));
    if (!out.state)
        return out;

    for (size_t i = 0; i < state_words; i++) {
        out.state[i] = UINT64_C(0xA5A5A5A5A5A5A5A5);
        out.state[i] ^= domain;
        out.state[i] ^= (uint64_t)node_length * UINT64_C(0x9E3779B97F4A7C15);
        out.state[i] ^= (uint64_t)count * UINT64_C(0xD6E8FEB86659FD93);
        out.state[i] ^= (uint64_t)state_words * UINT64_C(0xA24BAED4963EE407);
        out.state[i] ^= (uint64_t)normalized_depth << 32u;
        out.state[i] ^= (uint64_t)i * UINT64_C(0xC2B2AE3D27D4EB4F);
        out.state[i] = fch_sbox64(out.state[i]);
    }

    size_t covered = 0;
    for (size_t child_index = 0; child_index < count; child_index++) {
        fch_state_t *child = &children[child_index];
        const fch_block_t *block = &blocks[child_index];

        if (!child->state || child->words != state_words ||
            block->offset != covered || block->length == 0 ||
            block->length > node_length - covered) {
            free(out.state);
            out.state = NULL;
            return out;
        }

        uint64_t child_domain = UINT64_C(0x4348494C44535431);
        child_domain ^= (uint64_t)child_index * UINT64_C(0x9E3779B97F4A7C15);
        child_domain ^= (uint64_t)block->offset * UINT64_C(0xD6E8FEB86659FD93);
        child_domain ^= (uint64_t)block->length * UINT64_C(0xA24BAED4963EE407);

        for (size_t i = 0; i < state_words; i++) {
            size_t index = (child_index + i) % state_words;
            size_t next = (index + 1u) % state_words;
            uint64_t value = child->state[i] ^ child_domain;
            value ^= (uint64_t)i * UINT64_C(0x9FB21C651E98DF25);
            value = fch_sbox64(value);

            out.state[index] ^= fch_rotl64(
                value,
                (unsigned int)(((i * 11u + child_index * 7u) % 63u) + 1u)
            );
            out.state[next] += value ^ fch_rotl64(
                out.state[index],
                (unsigned int)(((i * 17u + child_index * 5u) % 63u) + 1u)
            );
        }

        for (size_t i = 0; i < state_words; i++) {
            uint64_t left = out.state[(i + state_words - 1u) % state_words];
            uint64_t right = out.state[(i + 1u) % state_words];
            uint64_t mixed = out.state[i] ^ child_domain;
            mixed += fch_rotl64(
                left,
                (unsigned int)(((i * 13u + child_index) % 63u) + 1u)
            );
            mixed ^= fch_rotl64(
                right,
                (unsigned int)(((i * 19u + child_index * 3u) % 63u) + 1u)
            );
            out.state[i] = fch_sbox64(mixed);
        }

        covered += block->length;
    }

    if (covered != node_length) {
        free(out.state);
        out.state = NULL;
        return out;
    }

    for (unsigned int round = 0; round < 4u; round++) {
        for (size_t i = 0; i < state_words; i++) {
            uint64_t left = out.state[(i + state_words - 1u) % state_words];
            uint64_t current = out.state[i];
            uint64_t right = out.state[(i + 1u) % state_words];
            uint64_t mixed = current ^ domain;

            mixed ^= (uint64_t)node_length * UINT64_C(0x9E3779B97F4A7C15);
            mixed ^= (uint64_t)count * UINT64_C(0xD6E8FEB86659FD93);
            mixed ^= (uint64_t)round * UINT64_C(0xA24BAED4963EE407);
            mixed += fch_rotl64(
                left,
                (unsigned int)(((i * 7u + round * 13u) % 63u) + 1u)
            );
            mixed ^= fch_rotl64(
                right,
                (unsigned int)(((i * 11u + round * 17u) % 63u) + 1u)
            );
            mixed = fch_sbox64(mixed);
            out.state[i] = fch_rotl64(
                mixed,
                (unsigned int)(((i * 19u + round * 23u) % 63u) + 1u)
            );
        }
    }

    return out;
}
