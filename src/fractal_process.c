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

static fch_state_t workspace_view(fch_tree_node_t *entry) {
    fch_state_t view = {
        entry ? entry->words : NULL,
        FCH_INTERNAL_STATE_WORDS,
        entry ? entry->tree : (fch_tree_position_t){0, 0, 0, 0, 0}
    };
    return view;
}

static int combine_workspace_states(
    fch_tree_node_t *left,
    fch_tree_node_t *right,
    fch_tree_node_t *output
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

int fch_tree_push_leaf(
    fch_tree_node_t workspace[FCH_TREE_WORKSPACE_SLOTS],
    const fch_reader_t *reader,
    size_t offset,
    size_t length
) {
    if (!workspace)
        return 0;

    fch_tree_node_t carry = {
        {0},
        {0, 0, 0, 0, 0},
        1
    };
    fch_state_t leaf = workspace_view(&carry);
    if (!fch_leaf_compress_reader(
            reader,
            offset,
            length,
            &leaf))
        return 0;
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
        fch_tree_node_t combined = {
            {0},
            {0, 0, 0, 0, 0},
            0
        };
        if (!combine_workspace_states(
                &workspace[level],
                &carry,
                &combined
            ))
            return 0;
        workspace[level].occupied = 0;
        carry = combined;
        level++;
    }
    if (level >= FCH_TREE_WORKSPACE_SLOTS)
        return 0;
    workspace[level] = carry;

    return 1;
}

int fch_tree_fold_root(
    fch_tree_node_t workspace[FCH_TREE_WORKSPACE_SLOTS],
    const fch_tree_position_t *expected,
    fch_state_t *output
) {
    if (!workspace || !expected || !output || !output->state ||
        output->words != FCH_INTERNAL_STATE_WORDS)
        return 0;

    fch_tree_node_t root = {
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

        fch_tree_node_t combined = {
            {0},
            {0, 0, 0, 0, 0},
            0
        };
        if (!combine_workspace_states(
                &workspace[level],
                &root,
                &combined
            ))
            return 0;
        root = combined;
    }

    if (!root.occupied || !fch_tree_position_equal(&root.tree, expected))
        return 0;

    memcpy(
        output->state,
        root.words,
        output->words * sizeof(*output->state)
    );
    output->tree = root.tree;

    FCH_DEBUG_EMIT(
        FCH_HOOK_AFTER_ROOT,
        (int)output->tree.level,
        output->state,
        output->words
    );
    return 1;
}

fch_state_t fch_process_reader(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    size_t state_words
) {
    fch_state_t result = {
        NULL,
        state_words,
        { 0, 0, 0, 0, 0 }
    };

    if (state_words != FCH_INTERNAL_STATE_WORDS ||
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

    fch_tree_node_t workspace[FCH_TREE_WORKSPACE_SLOTS] = {0};
    size_t remaining = length;
    size_t leaf_offset = offset;

    for (size_t leaf_index = 0;
         leaf_index < root_position.leaf_count;
         leaf_index++) {
        size_t leaf_length = remaining;
        if (leaf_length > FCH_TREE_LEAF_BYTES)
            leaf_length = FCH_TREE_LEAF_BYTES;

        if (!fch_tree_push_leaf(workspace, reader, leaf_offset, leaf_length))
            goto fail;

        remaining -= leaf_length;
        if (leaf_offset > SIZE_MAX - leaf_length)
            goto fail;
        leaf_offset += leaf_length;
    }

    if (remaining != 0u)
        goto fail;

    if (!fch_tree_fold_root(workspace, &root_position, &result))
        goto fail;
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
    size_t state_words
) {
    fch_state_t result = {
        NULL,
        state_words,
        { 0, 0, 0, 0, 0 }
    };

    if (state_words != FCH_INTERNAL_STATE_WORDS ||
        (!data && length > 0u))
        return result;

    fch_memory_reader_t memory = { data, length };
    fch_reader_t reader = { fch_memory_read, &memory };
    return fch_process_reader(&reader, 0, length, state_words);
}
