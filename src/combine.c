#include <stdlib.h>
#include <string.h>

#include "combine.h"
#include "bitops.h"
#include "mix.h"
#include "params.h"

static int same_position(
    const fch_tree_position_t *left,
    const fch_tree_position_t *right
) {
    return
        left && right &&
        left->level == right->level &&
        left->first_leaf == right->first_leaf &&
        left->leaf_count == right->leaf_count &&
        left->byte_offset == right->byte_offset &&
        left->byte_length == right->byte_length;
}

int fch_combine_into(
    fch_state_t *children,
    const fch_block_t *blocks,
    size_t count,
    size_t node_length,
    size_t state_words,
    int depth,
    fch_state_t *output
) {
    if (!children || !blocks || !output || !output->state || depth < 0)
        return 0;
    if (count != FCH_TREE_ARITY ||
        state_words != FCH_INTERNAL_STATE_WORDS ||
        output->words != state_words)
        return 0;

    for (size_t i = 0; i < count; i++) {
        if (!children[i].state || children[i].words != state_words ||
            !fch_tree_position_valid(&children[i].tree))
            return 0;
    }

    if (children[0].tree.byte_length >
        SIZE_MAX - children[1].tree.byte_length)
        return 0;
    size_t combined_length =
        children[0].tree.byte_length + children[1].tree.byte_length;
    if (combined_length != node_length)
        return 0;

    fch_tree_position_t parent;
    if (!fch_tree_position_for_range(
            children[0].tree.byte_offset,
            combined_length,
            &parent
        ))
        return 0;

    fch_tree_position_t expected[FCH_TREE_ARITY];
    if (!fch_tree_split_position(&parent, expected) ||
        !same_position(&children[0].tree, &expected[0]) ||
        !same_position(&children[1].tree, &expected[1]))
        return 0;

    if (blocks[0].offset != 0u ||
        blocks[0].length != children[0].tree.byte_length ||
        blocks[1].offset != children[0].tree.byte_length ||
        blocks[1].length != children[1].tree.byte_length)
        return 0;

    uint64_t working[FCH_INTERNAL_STATE_WORDS];
    if (!fch_mix_init(working, state_words, FCH_DOMAIN_NODE))
        return 0;

    uint8_t input[FCH_MIX_BLOCK_SIZE] = {0};
    fch_store_le64(input + 0u, FCH_TREE_TAG_NODE_HEADER);
    fch_store_le64(input + 8u, FCH_TREE_ENCODING_VERSION);
    fch_store_le64(input + 16u, FCH_DOMAIN_NODE);
    fch_store_le64(input + 24u, (uint64_t)parent.level);
    fch_store_le64(input + 32u, (uint64_t)parent.first_leaf);
    fch_store_le64(input + 40u, (uint64_t)parent.leaf_count);
    fch_store_le64(input + 48u, (uint64_t)parent.byte_offset);
    fch_store_le64(input + 56u, (uint64_t)parent.byte_length);
    fch_store_le64(input + 64u, FCH_TREE_ARITY);
    fch_store_le64(input + 72u, FCH_TREE_LEAF_BYTES);
    fch_store_le64(input + 80u, (uint64_t)state_words);
    fch_store_le64(input + 88u, FCH_MIX_BLOCK_SIZE);
    fch_store_le64(input + 96u, FCH_MIX_ROUNDS);

    if (!fch_mix_compress(
            working,
            state_words,
            input,
            sizeof(input),
            0,
            FCH_DOMAIN_NODE,
            FCH_MIX_FLAG_NODE_HEADER
        ))
        return 0;

    for (size_t child_index = 0; child_index < count; child_index++) {
        const fch_state_t *child = &children[child_index];

        for (size_t i = 0; i < sizeof(input); i++)
            input[i] = 0;
        fch_store_le64(input + 0u, FCH_TREE_TAG_NODE_CHILD);
        fch_store_le64(input + 8u, FCH_TREE_ENCODING_VERSION);
        fch_store_le64(input + 16u, (uint64_t)child_index);
        fch_store_le64(input + 24u, (uint64_t)child->tree.level);
        fch_store_le64(input + 32u, (uint64_t)child->tree.first_leaf);
        fch_store_le64(input + 40u, (uint64_t)child->tree.leaf_count);
        fch_store_le64(input + 48u, (uint64_t)child->tree.byte_offset);
        fch_store_le64(input + 56u, (uint64_t)child->tree.byte_length);
        for (size_t i = 0; i < state_words; i++)
            fch_store_le64(input + 64u + i * 8u, child->state[i]);

        uint64_t flags = FCH_MIX_FLAG_NODE_CHILD;
        if (child_index + 1u == count)
            flags |= FCH_MIX_FLAG_FINAL;

        if (!fch_mix_compress(
                working,
                state_words,
                input,
                sizeof(input),
                (uint64_t)child_index + 1u,
                FCH_DOMAIN_NODE,
                flags
            ))
            return 0;
    }

    memcpy(
        output->state,
        working,
        state_words * sizeof(*working)
    );
    output->tree = parent;
    return 1;
}

fch_state_t fch_combine(
    fch_state_t *children,
    const fch_block_t *blocks,
    size_t count,
    size_t node_length,
    size_t state_words,
    int depth
) {
    fch_state_t out = {
        NULL,
        state_words,
        { 0, 0, 0, 0, 0 }
    };
    uint64_t working[FCH_INTERNAL_STATE_WORDS];
    fch_state_t prepared = {
        working,
        state_words,
        { 0, 0, 0, 0, 0 }
    };

    if (!fch_combine_into(
            children,
            blocks,
            count,
            node_length,
            state_words,
            depth,
            &prepared
        ))
        return out;

    out.state = (uint64_t *)malloc(state_words * sizeof(*out.state));
    if (!out.state)
        return out;

    memcpy(
        out.state,
        prepared.state,
        state_words * sizeof(*out.state)
    );
    out.tree = prepared.tree;
    return out;
}
