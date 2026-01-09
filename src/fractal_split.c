#include "fractal.h"
#include "params.h"

static size_t determine_n(
    const uint8_t *data,
    size_t length,
    int depth
) {
    if (!data || length == 0)
        return 0;

    if (length < FCH_MIN_BLOCK_SIZE * 2)
        return 2;

    uint8_t seed = 0;
    seed ^= data[0];
    seed ^= data[length / 2];
    seed ^= data[length - 1];
    seed ^= (uint8_t)depth;

    size_t n = (seed % (FCH_N_MAX - FCH_N_MIN + 1)) + FCH_N_MIN;
    return n;
}

size_t fch_fractal_split(
    const uint8_t *data,
    size_t length,
    int depth,
    fch_block_t *blocks,
    size_t max_blocks
) {
    if (!blocks || max_blocks == 0)
        return 0;
    if (!data || length == 0) {
        blocks[0].offset = 0;
        blocks[0].length = length;
        return 1;
    }

    size_t n = determine_n(data, length, depth);
    if (n == 0)
        return 0;
    if (n > max_blocks)
        n = max_blocks;

    size_t weights[FCH_N_MAX] = {0};
    size_t total_weight = 0;

    for (size_t i = 0; i < n; i++) {
        size_t pos = (i * length) / n;
        if (pos >= length)
            pos = length - 1;
        weights[i] = 1 + (data[pos] & 0x0F);
        total_weight += weights[i];
    }

    if (total_weight == 0) {
        blocks[0].offset = 0;
        blocks[0].length = length;
        return 1;
    }

    size_t offset = 0;
    for (size_t i = 0; i < n; i++) {
        size_t block_len = (length * weights[i]) / total_weight;

        if (block_len == 0)
            block_len = 1;

        if (i == n - 1)
            block_len = length - offset;

        blocks[i].offset = offset;
        blocks[i].length = block_len;
        offset += block_len;
    }

    return n;
}
