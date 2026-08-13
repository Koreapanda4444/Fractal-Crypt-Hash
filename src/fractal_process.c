#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "fractal.h"
#include "leaf.h"
#include "combine.h"
#include "params.h"
#include "debug_hooks.h"

#if defined(FCH_DEBUG_HOOKS) && !defined(FCH_DEBUG_HOOK_EXTERNAL)
void fch_debug_hook(
    fch_hook_point_t point,
    int level,
    const uint64_t *state,
    size_t state_words
) {
    (void)point;
    (void)level;
    (void)state;
    (void)state_words;
}
#endif

enum {
    FCH_TREE_WORKSPACE_SLOTS = sizeof(size_t) * CHAR_BIT
};

typedef struct {
    uint64_t words[FCH_INTERNAL_STATE_WORDS];
    fch_tree_position_t tree;
    int occupied;
} fch_workspace_state_t;

static fch_state_t workspace_view(fch_workspace_state_t *entry) {
    fch_state_t view = {
        entry ? entry->words : NULL,
        FCH_INTERNAL_STATE_WORDS,
        entry ? entry->tree : (fch_tree_position_t){0, 0, 0, 0, 0}
    };
    return view;
}

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

static int combine_workspace_states(
    fch_workspace_state_t *left,
    fch_workspace_state_t *right,
    fch_workspace_state_t *output
) {
    if (!left || !right || !output ||
        !left->occupied || !right->occupied ||
        left->tree.byte_length > SIZE_MAX - right->tree.byte_length)
        return 0;

    fch_state_t children[FCH_TREE_ARITY] = {
        workspace_view(left),
        workspace_view(right)
    };
    fch_block_t blocks[FCH_TREE_ARITY] = {
        {0u, left->tree.byte_length},
        {left->tree.byte_length, right->tree.byte_length}
    };
    fch_state_t combined = workspace_view(output);
    size_t node_length =
        left->tree.byte_length + right->tree.byte_length;

    if (!fch_combine_into(
            children,
            blocks,
            FCH_TREE_ARITY,
            node_length,
            FCH_INTERNAL_STATE_WORDS,
            0,
            &combined
        ))
        return 0;

    output->tree = combined.tree;
    output->occupied = 1;
    FCH_DEBUG_EMIT(
        FCH_HOOK_AFTER_NODE,
        (int)output->tree.level,
        output->words,
        FCH_INTERNAL_STATE_WORDS
    );
    return 1;
}

fch_state_t fch_process_reader(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    int depth,
    size_t state_words
) {
    fch_state_t result = {
        NULL,
        state_words,
        { 0, 0, 0, 0, 0 }
    };

    if (state_words != FCH_INTERNAL_STATE_WORDS || depth < 0 ||
        !reader || !reader->read)
        return result;

    fch_tree_position_t root_position;
    if (!fch_tree_position_for_range(offset, length, &root_position))
        return result;
    if (root_position.level >= FCH_TREE_WORKSPACE_SLOTS)
        return result;

    result.state = (uint64_t *)malloc(
        state_words * sizeof(*result.state)
    );
    if (!result.state)
        return result;

    fch_workspace_state_t workspace[FCH_TREE_WORKSPACE_SLOTS] = {0};
    size_t remaining = length;
    size_t leaf_offset = offset;

    for (size_t leaf_index = 0;
         leaf_index < root_position.leaf_count;
         leaf_index++) {
        size_t leaf_length = remaining;
        if (leaf_length > FCH_TREE_LEAF_BYTES)
            leaf_length = FCH_TREE_LEAF_BYTES;

        fch_workspace_state_t carry = {
            {0},
            {0, 0, 0, 0, 0},
            1
        };
        fch_state_t leaf = workspace_view(&carry);
        if (!fch_leaf_compress_reader(
                reader,
                leaf_offset,
                leaf_length,
                &leaf,
                0
            ))
            goto fail;
        carry.tree = leaf.tree;

        FCH_DEBUG_EMIT(
            FCH_HOOK_AFTER_LEAF,
            (int)carry.tree.level,
            carry.words,
            FCH_INTERNAL_STATE_WORDS
        );

        size_t level = 0;
        while (level < FCH_TREE_WORKSPACE_SLOTS &&
               workspace[level].occupied) {
            fch_workspace_state_t combined = {
                {0},
                {0, 0, 0, 0, 0},
                0
            };
            if (!combine_workspace_states(
                    &workspace[level],
                    &carry,
                    &combined
                ))
                goto fail;
            workspace[level].occupied = 0;
            carry = combined;
            level++;
        }
        if (level >= FCH_TREE_WORKSPACE_SLOTS)
            goto fail;
        workspace[level] = carry;

        remaining -= leaf_length;
        if (leaf_offset > SIZE_MAX - leaf_length)
            goto fail;
        leaf_offset += leaf_length;
    }

    if (remaining != 0u)
        goto fail;

    fch_workspace_state_t root = {
        {0},
        {0, 0, 0, 0, 0},
        0
    };
    for (size_t level = 0;
         level < FCH_TREE_WORKSPACE_SLOTS;
         level++) {
        if (!workspace[level].occupied)
            continue;
        if (!root.occupied) {
            root = workspace[level];
            continue;
        }

        fch_workspace_state_t combined = {
            {0},
            {0, 0, 0, 0, 0},
            0
        };
        if (!combine_workspace_states(
                &workspace[level],
                &root,
                &combined
            ))
            goto fail;
        root = combined;
    }

    if (!root.occupied || !same_position(&root.tree, &root_position))
        goto fail;

    memcpy(
        result.state,
        root.words,
        state_words * sizeof(*result.state)
    );
    result.tree = root.tree;

    FCH_DEBUG_EMIT(
        FCH_HOOK_AFTER_ROOT,
        (int)result.tree.level,
        result.state,
        result.words
    );
    return result;

fail:
    free(result.state);
    result.state = NULL;
    result.tree = (fch_tree_position_t){0, 0, 0, 0, 0};
    return result;
}

fch_state_t fch_process(
    const uint8_t *data,
    size_t length,
    int depth,
    size_t state_words
) {
    fch_state_t result = {
        NULL,
        state_words,
        { 0, 0, 0, 0, 0 }
    };

    if (state_words != FCH_INTERNAL_STATE_WORDS || depth < 0 ||
        (!data && length > 0u))
        return result;

    fch_memory_reader_t memory = { data, length };
    fch_reader_t reader = { fch_memory_read, &memory };
    return fch_process_reader(&reader, 0, length, depth, state_words);
}
