#include "fractal.h"
#include "params.h"
#include "bitops.h"

static size_t scaled_length(
    size_t length,
    size_t weight,
    size_t total_weight
) {
    size_t quotient = length / total_weight;
    size_t remainder = length % total_weight;
    return quotient * weight + (remainder * weight) / total_weight;
}

static size_t determine_n(
    const uint8_t *data,
    size_t length,
    int depth,
    uint64_t *seed_out
) {
    if (seed_out)
        *seed_out = 0;
    if (!data || length == 0 || !seed_out)
        return 0;

    unsigned int normalized_depth = depth < 0 ? 0u : (unsigned int)depth;
    uint64_t seed = UINT64_C(0x243F6A8885A308D3);
    seed ^= (uint64_t)length * UINT64_C(0x9E3779B97F4A7C15);
    seed ^= (uint64_t)normalized_depth * UINT64_C(0xD6E8FEB86659FD93);

    for (size_t i = 0; i < length; i++) {
        uint64_t input_word = (uint64_t)data[i];
        input_word ^= (uint64_t)i * UINT64_C(0xA24BAED4963EE407);
        seed ^= input_word + UINT64_C(0x9E3779B97F4A7C15);
        seed = fch_rotl64(seed, 27u);
        seed *= UINT64_C(0x94D049BB133111EB);
        seed ^= seed >> 29u;
    }

    seed ^= seed >> 30u;
    seed *= UINT64_C(0xBF58476D1CE4E5B9);
    seed ^= seed >> 27u;
    seed *= UINT64_C(0x94D049BB133111EB);
    seed ^= seed >> 31u;
    *seed_out = seed;

    if (length < FCH_MIN_BLOCK_SIZE * 2)
        return 2;

    size_t n = (size_t)(seed % (uint64_t)(FCH_N_MAX - FCH_N_MIN + 1))
        + FCH_N_MIN;
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

    uint64_t seed = 0;
    size_t n = determine_n(data, length, depth, &seed);
    if (n == 0)
        return 0;
    if (n > max_blocks)
        n = max_blocks;
    if (n > length)
        n = length;
    if (n == 0)
        return 0;

    size_t weights[FCH_N_MAX] = {0};
    size_t total_weight = 0;

    uint64_t stream = seed ^ ((uint64_t)n * UINT64_C(0xD6E8FEB86659FD93));
    for (size_t i = 0; i < n; i++) {
        stream += UINT64_C(0x9E3779B97F4A7C15) + (uint64_t)i;
        uint64_t word = stream;
        word = (word ^ (word >> 30u)) * UINT64_C(0xBF58476D1CE4E5B9);
        word = (word ^ (word >> 27u)) * UINT64_C(0x94D049BB133111EB);
        word ^= word >> 31u;
        weights[i] = 1u + (size_t)(word & UINT64_C(0x0F));
        total_weight += weights[i];
    }

    if (total_weight == 0) {
        blocks[0].offset = 0;
        blocks[0].length = length;
        return 1;
    }

    size_t offset = 0;
    for (size_t i = 0; i < n; i++) {
        size_t remaining = length - offset;
        size_t blocks_left = n - i - 1;
        size_t block_len = scaled_length(length, weights[i], total_weight);

        if (block_len == 0)
            block_len = 1;

        if (i == n - 1) {
            block_len = remaining;
        } else {
            size_t max_len = remaining - blocks_left;
            if (block_len > max_len)
                block_len = max_len;
        }

        blocks[i].offset = offset;
        blocks[i].length = block_len;
        offset += block_len;
    }

    return offset == length ? n : 0;
}
