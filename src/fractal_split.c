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
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    int depth,
    uint64_t *seed_out
) {
    if (seed_out)
        *seed_out = 0;
    if (!reader || !reader->read || length == 0 || depth < 0 || !seed_out)
        return 0;
    if (offset > SIZE_MAX - length)
        return 0;

    unsigned int normalized_depth = (unsigned int)depth;
    uint64_t seed = UINT64_C(0x243F6A8885A308D3);
    seed ^= (uint64_t)length * UINT64_C(0x9E3779B97F4A7C15);
    seed ^= (uint64_t)normalized_depth * UINT64_C(0xD6E8FEB86659FD93);

    uint8_t buffer[4096];
    size_t processed = 0;

    while (processed < length) {
        size_t chunk = length - processed;
        if (chunk > sizeof(buffer))
            chunk = sizeof(buffer);
        if (!reader->read(
                reader->context,
                offset + processed,
                buffer,
                chunk
            ))
            return 0;

        for (size_t j = 0; j < chunk; j++) {
            size_t i = processed + j;
            uint64_t input_word = (uint64_t)buffer[j];
            input_word ^= (uint64_t)i * UINT64_C(0xA24BAED4963EE407);
            seed ^= input_word + UINT64_C(0x9E3779B97F4A7C15);
            seed = fch_rotl64(seed, 27u);
            seed *= UINT64_C(0x94D049BB133111EB);
            seed ^= seed >> 29u;
        }

        processed += chunk;
    }

    seed ^= seed >> 30u;
    seed *= UINT64_C(0xBF58476D1CE4E5B9);
    seed ^= seed >> 27u;
    seed *= UINT64_C(0x94D049BB133111EB);
    seed ^= seed >> 31u;
    *seed_out = seed;

    if (length < FCH_MIN_BLOCK_SIZE * 2)
        return FCH_N_MIN;

    size_t n = (size_t)(seed % (uint64_t)(FCH_N_MAX - FCH_N_MIN + 1))
        + FCH_N_MIN;
    return n;
}

size_t fch_fractal_split_reader(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    int depth,
    fch_block_t *blocks,
    size_t max_blocks
) {
    if (!blocks || max_blocks == 0)
        return 0;
    if (depth < 0)
        return 0;
    if (length == 0) {
        blocks[0].offset = 0;
        blocks[0].length = 0;
        return 1;
    }
    if (!reader || !reader->read || offset > SIZE_MAX - length)
        return 0;

    uint64_t seed = 0;
    size_t n = determine_n(reader, offset, length, depth, &seed);
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

    size_t block_offset = 0;
    for (size_t i = 0; i < n; i++) {
        size_t remaining = length - block_offset;
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

        blocks[i].offset = block_offset;
        blocks[i].length = block_len;
        block_offset += block_len;
    }

    return block_offset == length ? n : 0;
}

size_t fch_fractal_split(
    const uint8_t *data,
    size_t length,
    int depth,
    fch_block_t *blocks,
    size_t max_blocks
) {
    if (!data && length > 0)
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
