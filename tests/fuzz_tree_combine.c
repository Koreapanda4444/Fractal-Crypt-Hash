#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "combine.h"
#include "fractal.h"
#include "params.h"

enum {
    FCH_FUZZ_TREE_MAX_INPUT = 65536
};

static void require_or_abort(int condition) {
    if (!condition)
        abort();
}

static int same_position(
    const fch_tree_position_t *left,
    const fch_tree_position_t *right
) {
    return
        left->level == right->level &&
        left->first_leaf == right->first_leaf &&
        left->leaf_count == right->leaf_count &&
        left->byte_offset == right->byte_offset &&
        left->byte_length == right->byte_length;
}

static uint8_t byte_at(
    const uint8_t *data,
    size_t size,
    size_t index
) {
    if (size == 0u)
        return (uint8_t)(index * 73u + 19u);
    return data[index % size];
}

static uint64_t word_at(
    const uint8_t *data,
    size_t size,
    size_t index
) {
    uint64_t value = UINT64_C(0x9E3779B97F4A7C15) ^ (uint64_t)index;
    for (size_t i = 0u; i < 8u; i++) {
        value ^= (uint64_t)byte_at(data, size, index * 8u + i)
            << (i * 8u);
    }
    return value;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if ((!data && size > 0u) || size > FCH_FUZZ_TREE_MAX_INPUT)
        return 0;

    size_t leaf_count = 2u + byte_at(data, size, 0u) % 15u;
    size_t tail_length = 1u + (
        (size_t)byte_at(data, size, 1u) |
        ((size_t)byte_at(data, size, 2u) << 8u)
    ) % FCH_TREE_LEAF_BYTES;
    size_t parent_length =
        (leaf_count - 1u) * FCH_TREE_LEAF_BYTES + tail_length;
    size_t parent_offset =
        (size_t)(byte_at(data, size, 3u) & 7u) *
        FCH_TREE_LEAF_BYTES;

    fch_tree_position_t parent;
    fch_tree_position_t positions[FCH_TREE_ARITY];
    require_or_abort(fch_tree_position_for_range(
        parent_offset,
        parent_length,
        &parent
    ));
    require_or_abort(fch_tree_split_position(&parent, positions));

    uint64_t left_words[FCH_INTERNAL_STATE_WORDS];
    uint64_t right_words[FCH_INTERNAL_STATE_WORDS];
    for (size_t i = 0u; i < FCH_INTERNAL_STATE_WORDS; i++) {
        left_words[i] = word_at(data, size, i);
        right_words[i] = word_at(
            data,
            size,
            i + FCH_INTERNAL_STATE_WORDS
        );
    }

    fch_state_t children[FCH_TREE_ARITY] = {
        {left_words, FCH_INTERNAL_STATE_WORDS, positions[0]},
        {right_words, FCH_INTERNAL_STATE_WORDS, positions[1]}
    };
    fch_block_t blocks[FCH_TREE_ARITY] = {
        {0u, positions[0].byte_length},
        {positions[0].byte_length, positions[1].byte_length}
    };
    uint64_t output_words[FCH_INTERNAL_STATE_WORDS];
    uint64_t repeat_words[FCH_INTERNAL_STATE_WORDS];
    fch_state_t output = {
        output_words,
        FCH_INTERNAL_STATE_WORDS,
        {0, 0, 0, 0, 0}
    };
    fch_state_t repeat = {
        repeat_words,
        FCH_INTERNAL_STATE_WORDS,
        {0, 0, 0, 0, 0}
    };
    int depth = (int)(byte_at(data, size, 4u) & 31u);

    require_or_abort(fch_combine_into(
        children,
        blocks,
        FCH_TREE_ARITY,
        parent_length,
        FCH_INTERNAL_STATE_WORDS,
        depth,
        &output
    ));
    require_or_abort(fch_combine_into(
        children,
        blocks,
        FCH_TREE_ARITY,
        parent_length,
        FCH_INTERNAL_STATE_WORDS,
        depth,
        &repeat
    ));
    require_or_abort(memcmp(
        output_words,
        repeat_words,
        sizeof(output_words)
    ) == 0);
    require_or_abort(same_position(&output.tree, &parent));
    require_or_abort(same_position(&repeat.tree, &parent));

    fch_state_t allocated = fch_combine(
        children,
        blocks,
        FCH_TREE_ARITY,
        parent_length,
        FCH_INTERNAL_STATE_WORDS,
        depth
    );
    require_or_abort(allocated.state != NULL);
    require_or_abort(memcmp(
        allocated.state,
        output_words,
        sizeof(output_words)
    ) == 0);
    require_or_abort(same_position(&allocated.tree, &parent));
    free(allocated.state);

    fch_state_t invalid_children[FCH_TREE_ARITY] = {
        children[0], children[1]
    };
    fch_block_t invalid_blocks[FCH_TREE_ARITY] = {
        blocks[0], blocks[1]
    };
    fch_state_t invalid_output = output;
    size_t invalid_count = FCH_TREE_ARITY;
    size_t invalid_length = parent_length;
    size_t invalid_words = FCH_INTERNAL_STATE_WORDS;
    int invalid_depth = depth;

    switch (byte_at(data, size, 5u) % 14u) {
    case 0u:
        invalid_blocks[0].offset = 1u;
        break;
    case 1u:
        invalid_blocks[0].length++;
        break;
    case 2u:
        invalid_blocks[1].offset++;
        break;
    case 3u:
        invalid_blocks[1].length--;
        break;
    case 4u:
        invalid_children[0].tree.byte_offset++;
        break;
    case 5u:
        invalid_children[1].tree.first_leaf++;
        break;
    case 6u:
        invalid_children[0].tree.level++;
        break;
    case 7u:
        invalid_children[1].words--;
        break;
    case 8u:
        invalid_output.words--;
        break;
    case 9u:
        invalid_depth = -1;
        break;
    case 10u:
        invalid_count = 1u;
        break;
    case 11u:
        invalid_length++;
        break;
    case 12u:
        invalid_children[0].state = NULL;
        break;
    default: {
        fch_state_t temporary = invalid_children[0];
        invalid_children[0] = invalid_children[1];
        invalid_children[1] = temporary;
        break;
    }
    }

    require_or_abort(!fch_combine_into(
        invalid_children,
        invalid_blocks,
        invalid_count,
        invalid_length,
        invalid_words,
        invalid_depth,
        &invalid_output
    ));
    return 0;
}
