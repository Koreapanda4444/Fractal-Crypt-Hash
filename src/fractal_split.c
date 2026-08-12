#include "fractal.h"
#include "params.h"

size_t fch_tree_leaf_count_for_length(size_t length) {
    if (length == 0)
        return 0;
    return 1u + (length - 1u) / FCH_TREE_LEAF_BYTES;
}

size_t fch_tree_level_for_leaves(size_t leaf_count) {
    if (leaf_count == 0)
        return SIZE_MAX;

    size_t level = 0;
    size_t value = leaf_count - 1u;
    while (value != 0u) {
        level++;
        value >>= 1u;
    }
    return level;
}

int fch_tree_position_for_range(
    size_t offset,
    size_t length,
    fch_tree_position_t *position
) {
    if (!position || length == 0)
        return 0;
    if (offset > SIZE_MAX - length)
        return 0;
    if (offset % FCH_TREE_LEAF_BYTES != 0u)
        return 0;

    size_t leaf_count = fch_tree_leaf_count_for_length(length);
    size_t level = fch_tree_level_for_leaves(leaf_count);
    size_t first_leaf = offset / FCH_TREE_LEAF_BYTES;

    if (leaf_count == 0 || level == SIZE_MAX)
        return 0;
    if (first_leaf > SIZE_MAX - leaf_count)
        return 0;

    position->level = level;
    position->first_leaf = first_leaf;
    position->leaf_count = leaf_count;
    position->byte_offset = offset;
    position->byte_length = length;
    return 1;
}

int fch_tree_position_valid(const fch_tree_position_t *position) {
    if (!position)
        return 0;

    fch_tree_position_t expected;
    if (!fch_tree_position_for_range(
            position->byte_offset,
            position->byte_length,
            &expected
        ))
        return 0;

    return
        position->level == expected.level &&
        position->first_leaf == expected.first_leaf &&
        position->leaf_count == expected.leaf_count;
}

static size_t largest_power_of_two_below(size_t value) {
    if (value < 2u)
        return 0;

    size_t power = 1u;
    size_t limit = (value - 1u) / 2u;
    while (power <= limit) {
        if (power > SIZE_MAX / 2u)
            return 0;
        power *= 2u;
    }
    return power;
}

int fch_tree_split_position(
    const fch_tree_position_t *parent,
    fch_tree_position_t children[FCH_TREE_ARITY]
) {
    if (!parent || !children || !fch_tree_position_valid(parent))
        return 0;
    if (parent->leaf_count < FCH_TREE_ARITY)
        return 0;

    size_t left_leaves =
        largest_power_of_two_below(parent->leaf_count);
    if (left_leaves == 0 ||
        left_leaves > SIZE_MAX / FCH_TREE_LEAF_BYTES)
        return 0;

    size_t left_length = left_leaves * FCH_TREE_LEAF_BYTES;
    if (left_length >= parent->byte_length)
        return 0;

    size_t right_offset = parent->byte_offset + left_length;
    size_t right_length = parent->byte_length - left_length;

    if (!fch_tree_position_for_range(
            parent->byte_offset,
            left_length,
            &children[0]
        ) ||
        !fch_tree_position_for_range(
            right_offset,
            right_length,
            &children[1]
        ))
        return 0;

    if (children[0].first_leaf != parent->first_leaf ||
        children[0].leaf_count != left_leaves ||
        children[1].first_leaf !=
            parent->first_leaf + left_leaves ||
        children[0].leaf_count + children[1].leaf_count !=
            parent->leaf_count)
        return 0;

    return 1;
}

size_t fch_fractal_split_reader(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    int depth,
    fch_block_t *blocks,
    size_t max_blocks
) {
    if (!reader || !reader->read || !blocks || max_blocks == 0u)
        return 0;
    if (depth < 0)
        return 0;

    fch_tree_position_t parent;
    if (!fch_tree_position_for_range(offset, length, &parent))
        return 0;

    if (parent.leaf_count == 1u) {
        blocks[0].offset = 0;
        blocks[0].length = length;
        return 1u;
    }

    if (max_blocks < FCH_TREE_ARITY)
        return 0;

    fch_tree_position_t children[FCH_TREE_ARITY];
    if (!fch_tree_split_position(&parent, children))
        return 0;

    for (size_t i = 0; i < FCH_TREE_ARITY; i++) {
        blocks[i].offset = children[i].byte_offset - parent.byte_offset;
        blocks[i].length = children[i].byte_length;
    }
    return FCH_TREE_ARITY;
}

size_t fch_fractal_split(
    const uint8_t *data,
    size_t length,
    int depth,
    fch_block_t *blocks,
    size_t max_blocks
) {
    if (!data && length > 0u)
        return 0;

    fch_memory_reader_t memory = { data, length };
    fch_reader_t reader = { fch_memory_read, &memory };
    return fch_fractal_split_reader(
        &reader,
        0,
        length,
        depth,
        blocks,
        max_blocks
    );
}
