#include <stdlib.h>

#include "combine.h"
#include "bitops.h"
#include "mix.h"

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

    if (!fch_mix_init(out.state, state_words, domain)) {
        free(out.state);
        out.state = NULL;
        return out;
    }

    uint8_t input[FCH_MIX_BLOCK_SIZE] = {0};
    fch_store_le64(input + 0u, domain);
    fch_store_le64(input + 8u, (uint64_t)node_length);
    fch_store_le64(input + 16u, (uint64_t)normalized_depth);
    fch_store_le64(input + 24u, (uint64_t)count);
    fch_store_le64(input + 32u, (uint64_t)state_words);
    fch_store_le64(input + 40u, FCH_MIX_ROUNDS);

    if (!fch_mix_compress(
            out.state,
            state_words,
            input,
            48u,
            0,
            domain,
            FCH_MIX_FLAG_PARAMETER
        )) {
        free(out.state);
        out.state = NULL;
        return out;
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

        for (size_t i = 0; i < sizeof(input); i++)
            input[i] = 0;
        for (size_t i = 0; i < state_words; i++)
            fch_store_le64(input + i * 8u, child->state[i]);

        fch_store_le64(input + 64u, (uint64_t)child_index);
        fch_store_le64(input + 72u, (uint64_t)block->offset);
        fch_store_le64(input + 80u, (uint64_t)block->length);
        fch_store_le64(input + 88u, (uint64_t)count);
        fch_store_le64(input + 96u, (uint64_t)node_length);
        fch_store_le64(input + 104u, (uint64_t)normalized_depth);
        fch_store_le64(input + 112u, (uint64_t)state_words);
        fch_store_le64(input + 120u, domain);

        uint64_t flags = FCH_MIX_FLAG_NODE_CHILD;
        if (child_index + 1u == count)
            flags |= FCH_MIX_FLAG_FINAL;
        if (!fch_mix_compress(
                out.state,
                state_words,
                input,
                sizeof(input),
                (uint64_t)child_index + 1u,
                domain,
                flags
            )) {
            free(out.state);
            out.state = NULL;
            return out;
        }

        covered += block->length;
    }

    if (covered != node_length) {
        free(out.state);
        out.state = NULL;
        return out;
    }

    return out;
}
