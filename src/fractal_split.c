#include "fractal.h"
#include "params.h"
#include "bitops.h"
#include "mix.h"

static size_t scaled_length(
    size_t length,
    size_t weight,
    size_t total_weight
) {
    size_t quotient = length / total_weight;
    size_t remainder = length % total_weight;
    size_t fractional = (size_t)(
        ((uint64_t)remainder * (uint64_t)weight) /
        (uint64_t)total_weight
    );
    return quotient * weight + fractional;
}

static size_t determine_n(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    int depth,
    uint64_t material_out[FCH_INTERNAL_STATE_WORDS]
) {
    if (material_out) {
        for (size_t i = 0; i < FCH_INTERNAL_STATE_WORDS; i++)
            material_out[i] = 0;
    }
    if (!reader || !reader->read || length == 0 || depth < 0 ||
        !material_out)
        return 0;
    if (offset > SIZE_MAX - length)
        return 0;

    unsigned int normalized_depth = (unsigned int)depth;
    if (!fch_mix_init(
            material_out,
            FCH_INTERNAL_STATE_WORDS,
            FCH_SPLIT_DOMAIN
        ))
        return 0;

    uint8_t buffer[FCH_MIX_BLOCK_SIZE] = {0};
    fch_store_le64(buffer + 0u, FCH_SPLIT_TAG_HEADER);
    fch_store_le64(buffer + 8u, FCH_SPLIT_DERIVATION_VERSION);
    fch_store_le64(buffer + 16u, FCH_SPLIT_DOMAIN);
    fch_store_le64(buffer + 24u, (uint64_t)length);
    fch_store_le64(buffer + 32u, (uint64_t)normalized_depth);
    fch_store_le64(buffer + 40u, FCH_INTERNAL_STATE_WORDS);
    fch_store_le64(buffer + 48u, FCH_N_MIN);
    fch_store_le64(buffer + 56u, FCH_N_MAX);
    fch_store_le64(buffer + 64u, FCH_SPLIT_WEIGHT_MIN);
    fch_store_le64(buffer + 72u, FCH_SPLIT_WEIGHT_MAX);
    fch_store_le64(buffer + 80u, FCH_MIN_BLOCK_SIZE);
    fch_store_le64(buffer + 88u, FCH_MAX_DEPTH_CAP);
    fch_store_le64(buffer + 96u, FCH_MIX_BLOCK_SIZE);
    fch_store_le64(buffer + 104u, FCH_MIX_ROUNDS);
    fch_store_le64(buffer + 112u, FCH_TREE_ENCODING_VERSION);

    if (!fch_mix_compress(
            material_out,
            FCH_INTERNAL_STATE_WORDS,
            buffer,
            sizeof(buffer),
            0,
            FCH_SPLIT_DOMAIN,
            FCH_MIX_FLAG_SPLIT_HEADER
        ))
        return 0;

    size_t processed = 0;
    uint64_t record_count = 0;

    while (processed < length) {
        size_t chunk = length - processed;
        if (chunk > sizeof(buffer) - 8u)
            chunk = sizeof(buffer) - 8u;

        for (size_t i = 0; i < sizeof(buffer); i++)
            buffer[i] = 0;
        fch_store_le64(buffer + 0u, FCH_SPLIT_TAG_DATA);
        if (!reader->read(
                reader->context,
                offset + processed,
                buffer + 8u,
                chunk
            ))
            return 0;

        processed += chunk;
        record_count++;

        uint64_t flags = FCH_MIX_FLAG_SPLIT_DATA;
        if (processed == length)
            flags |= FCH_MIX_FLAG_FINAL;
        if (!fch_mix_compress(
                material_out,
                FCH_INTERNAL_STATE_WORDS,
                buffer,
                chunk + 8u,
                (uint64_t)processed,
                FCH_SPLIT_DOMAIN,
                flags
            ))
            return 0;
    }

    const uint64_t range =
        (uint64_t)(FCH_N_MAX - FCH_N_MIN + 1);
    const uint64_t rejection_threshold =
        (UINT64_C(0) - range) % range;
    uint64_t draw_counter = 0;

    do {
        for (size_t i = 0; i < sizeof(buffer); i++)
            buffer[i] = 0;
        fch_store_le64(buffer + 0u, FCH_SPLIT_TAG_OUTPUT);
        fch_store_le64(buffer + 8u, FCH_SPLIT_DERIVATION_VERSION);
        fch_store_le64(buffer + 16u, draw_counter);
        fch_store_le64(buffer + 24u, (uint64_t)length);
        fch_store_le64(buffer + 32u, (uint64_t)normalized_depth);
        fch_store_le64(buffer + 40u, record_count);
        fch_store_le64(buffer + 48u, FCH_N_MIN);
        fch_store_le64(buffer + 56u, FCH_N_MAX);
        fch_store_le64(buffer + 64u, FCH_SPLIT_WEIGHT_MIN);
        fch_store_le64(buffer + 72u, FCH_SPLIT_WEIGHT_MAX);
        fch_store_le64(buffer + 80u, FCH_INTERNAL_STATE_WORDS);
        fch_store_le64(buffer + 88u, FCH_MIX_BLOCK_SIZE);
        fch_store_le64(buffer + 96u, FCH_MIX_ROUNDS);
        fch_store_le64(buffer + 104u, FCH_TREE_ENCODING_VERSION);

        if (!fch_mix_compress(
                material_out,
                FCH_INTERNAL_STATE_WORDS,
                buffer,
                sizeof(buffer),
                draw_counter,
                FCH_SPLIT_DOMAIN,
                FCH_MIX_FLAG_SPLIT_OUTPUT | FCH_MIX_FLAG_FINAL
            ))
            return 0;

        if (material_out[0] >= rejection_threshold)
            break;
        if (draw_counter == UINT64_MAX)
            return 0;
        draw_counter++;
    } while (1);

    if (length < FCH_MIN_BLOCK_SIZE * 2)
        return FCH_N_MIN;

    size_t n = (size_t)(material_out[0] % range) + FCH_N_MIN;
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

    uint64_t material[FCH_INTERNAL_STATE_WORDS] = {0};
    size_t n = determine_n(reader, offset, length, depth, material);
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

    for (size_t i = 0; i < n; i++) {
        weights[i] = FCH_SPLIT_WEIGHT_MIN +
            (size_t)(material[i + 1u] & UINT64_C(0x7F));
        total_weight += weights[i];
    }

    if (total_weight == 0) {
        return 0;
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
