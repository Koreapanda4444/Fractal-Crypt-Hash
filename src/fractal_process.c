#include <stdlib.h>

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

static fch_state_t process_subtree(
    const fch_reader_t *reader,
    const fch_tree_position_t *position,
    size_t state_words
) {
    fch_state_t result = {
        NULL,
        state_words,
        { 0, 0, 0, 0, 0 }
    };

    if (!reader || !reader->read || !position ||
        !fch_tree_position_valid(position) ||
        state_words != FCH_INTERNAL_STATE_WORDS)
        return result;

    if (position->leaf_count == 1u) {
        result.state = (uint64_t *)calloc(state_words, sizeof(uint64_t));
        if (!result.state)
            return result;

        if (!fch_leaf_compress_reader(
                reader,
                position->byte_offset,
                position->byte_length,
                &result,
                0
            )) {
            free(result.state);
            result.state = NULL;
            return result;
        }

        FCH_DEBUG_EMIT(
            FCH_HOOK_AFTER_LEAF,
            (int)result.tree.level,
            result.state,
            result.words
        );
        return result;
    }

    fch_tree_position_t child_positions[FCH_TREE_ARITY];
    if (!fch_tree_split_position(position, child_positions))
        return result;

    fch_state_t children[FCH_TREE_ARITY] = {
        { NULL, state_words, { 0, 0, 0, 0, 0 } },
        { NULL, state_words, { 0, 0, 0, 0, 0 } }
    };
    fch_block_t blocks[FCH_TREE_ARITY];

    for (size_t i = 0; i < FCH_TREE_ARITY; i++) {
        children[i] = process_subtree(
            reader,
            &child_positions[i],
            state_words
        );
        if (!children[i].state) {
            for (size_t j = 0; j < i; j++)
                free(children[j].state);
            return result;
        }

        blocks[i].offset =
            child_positions[i].byte_offset - position->byte_offset;
        blocks[i].length = child_positions[i].byte_length;
    }

    result = fch_combine(
        children,
        blocks,
        FCH_TREE_ARITY,
        position->byte_length,
        state_words,
        0
    );

    for (size_t i = 0; i < FCH_TREE_ARITY; i++)
        free(children[i].state);

    if (result.state) {
        FCH_DEBUG_EMIT(
            FCH_HOOK_AFTER_NODE,
            (int)result.tree.level,
            result.state,
            result.words
        );
    }
    return result;
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

    result = process_subtree(reader, &root_position, state_words);
    if (result.state) {
        FCH_DEBUG_EMIT(
            FCH_HOOK_AFTER_ROOT,
            (int)result.tree.level,
            result.state,
            result.words
        );
    }
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
