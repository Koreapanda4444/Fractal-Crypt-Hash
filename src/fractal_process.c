#include <stdlib.h>
#include <string.h>

#include "fractal.h"
#include "leaf.h"
#include "combine.h"
#include "params.h"
#include "debug_hooks.h"

#ifdef FCH_DEBUG_HOOKS
__attribute__((weak))
void fch_debug_hook(
    fch_hook_point_t point,
    int depth,
    const uint64_t *state,
    size_t state_words
) {
    (void)point;
    (void)depth;
    (void)state;
    (void)state_words;
}

static inline void fch_debug_emit_root_if(
    int depth,
    const uint64_t *state,
    size_t state_words
) {
    if (depth == 0) {
        FCH_DEBUG_EMIT(FCH_HOOK_AFTER_ROOT, depth, state, state_words);
    }
}
#else
static inline void fch_debug_emit_root_if(
    int depth,
    const uint64_t *state,
    size_t state_words
) {
    (void)depth;
    (void)state;
    (void)state_words;
}
#endif

fch_state_t fch_process_reader(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    int depth,
    size_t state_words
) {
    fch_state_t result = { NULL, state_words };

    if (state_words != FCH_INTERNAL_STATE_WORDS ||
        depth < 0 || !reader || !reader->read)
        return result;
    if (offset > SIZE_MAX - length)
        return result;

    if (depth >= FCH_MAX_DEPTH_CAP || length <= FCH_MIN_BLOCK_SIZE) {
        result.state = (uint64_t *)calloc(state_words, sizeof(uint64_t));
        if (result.state) {
            if (!fch_leaf_compress_reader(
                    reader,
                    offset,
                    length,
                    &result,
                    depth
                )) {
                free(result.state);
                result.state = NULL;
                return result;
            }
            FCH_DEBUG_EMIT(FCH_HOOK_AFTER_LEAF, depth, result.state, result.words);
            fch_debug_emit_root_if(depth, result.state, result.words);
        }
        return result;
    }

    fch_block_t blocks[FCH_N_MAX];
    size_t n = fch_fractal_split_reader(
        reader,
        offset,
        length,
        depth,
        blocks,
        FCH_N_MAX
    );

    if (n == 0) {
        return result;
    }

    fch_state_t *children =
        (fch_state_t *)calloc(n, sizeof(fch_state_t));

    if (!children) {
        return result;
    }

    for (size_t i = 0; i < n; i++) {
        size_t sub_offset = offset + blocks[i].offset;
        size_t sub_len = blocks[i].length;

        children[i] = fch_process_reader(
            reader,
            sub_offset,
            sub_len,
            depth + 1,
            state_words
        );

        if (!children[i].state) {
            for (size_t j = 0; j < i; j++) {
                free(children[j].state);
            }
            free(children);
            return result;
        }
    }

    result = fch_combine(
        children,
        blocks,
        n,
        length,
        state_words,
        depth
    );
    if (result.state) {
        FCH_DEBUG_EMIT(FCH_HOOK_AFTER_NODE, depth, result.state, result.words);
        fch_debug_emit_root_if(depth, result.state, result.words);
    }

    for (size_t i = 0; i < n; i++) {
        free(children[i].state);
    }
    free(children);

    return result;
}

fch_state_t fch_process(
    const uint8_t *data,
    size_t length,
    int depth,
    size_t state_words
) {
    fch_state_t result = { NULL, state_words };

    if (state_words != FCH_INTERNAL_STATE_WORDS ||
        (!data && length > 0))
        return result;

    fch_memory_reader_t memory = { data, length };
    fch_reader_t reader = { fch_memory_read, &memory };
    return fch_process_reader(&reader, 0, length, depth, state_words);
}
