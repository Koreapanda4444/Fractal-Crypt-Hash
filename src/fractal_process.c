#include <stdlib.h>
#include <string.h>

#include "fractal.h"
#include "leaf.h"
#include "combine.h"
#include "params.h"

fch_state_t fch_process(
    const uint8_t *data,
    size_t length,
    int depth,
    size_t state_words
) {
    fch_state_t result;
    result.words = state_words;

    if (!data || depth >= FCH_MAX_DEPTH_CAP || length <= FCH_MIN_BLOCK_SIZE) {
        result.state = (uint64_t *)calloc(state_words, sizeof(uint64_t));
        if (result.state) {
            fch_leaf_compress(data, length, &result);
        }
        return result;
    }

    fch_block_t blocks[FCH_N_MAX];
    size_t n = fch_fractal_split(
        data, length, depth, blocks, FCH_N_MAX
    );

    if (n == 0) {
        result.state = (uint64_t *)calloc(state_words, sizeof(uint64_t));
        if (result.state) {
            fch_leaf_compress(data, length, &result);
        }
        return result;
    }

    fch_state_t *children =
        (fch_state_t *)calloc(n, sizeof(fch_state_t));

    if (!children) {
        result.state = (uint64_t *)calloc(state_words, sizeof(uint64_t));
        if (result.state) {
            fch_leaf_compress(data, length, &result);
        }
        return result;
    }

    for (size_t i = 0; i < n; i++) {
        const uint8_t *sub =
            data + blocks[i].offset;
        size_t sub_len = blocks[i].length;

        children[i] = fch_process(
            sub, sub_len, depth + 1, state_words
        );
    }

    result = fch_combine(children, n, state_words);

    for (size_t i = 0; i < n; i++) {
        free(children[i].state);
    }
    free(children);

    return result;
}
