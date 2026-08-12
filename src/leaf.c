#include "leaf.h"
#include "params.h"
#include "bitops.h"
#include "mix.h"

int fch_leaf_compress_reader(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    fch_state_t *out,
    int depth
) {
    if (!out || !out->state ||
        out->words != FCH_INTERNAL_STATE_WORDS)
        return 0;
    if (!reader || !reader->read || depth < 0)
        return 0;

    fch_tree_position_t position;
    if (!fch_tree_position_for_range(offset, length, &position) ||
        position.leaf_count != 1u || position.level != 0u)
        return 0;

    size_t state_words = out->words;
    uint64_t *state = out->state;

    if (!fch_mix_init(state, state_words, FCH_DOMAIN_LEAF))
        return 0;

    uint8_t buffer[FCH_MIX_BLOCK_SIZE] = {0};
    fch_store_le64(buffer + 0u, FCH_TREE_TAG_LEAF_HEADER);
    fch_store_le64(buffer + 8u, FCH_TREE_ENCODING_VERSION);
    fch_store_le64(buffer + 16u, FCH_DOMAIN_LEAF);
    fch_store_le64(buffer + 24u, (uint64_t)position.first_leaf);
    fch_store_le64(buffer + 32u, (uint64_t)position.byte_offset);
    fch_store_le64(buffer + 40u, (uint64_t)position.byte_length);
    fch_store_le64(buffer + 48u, FCH_TREE_LEAF_BYTES);
    fch_store_le64(buffer + 56u, (uint64_t)state_words);
    fch_store_le64(buffer + 64u, FCH_MIX_BLOCK_SIZE);
    fch_store_le64(buffer + 72u, FCH_MIX_ROUNDS);
    fch_store_le64(buffer + 80u, FCH_PADDING_FORMAT_VERSION);
    fch_store_le64(buffer + 88u, FCH_TREE_ARITY);

    if (!fch_mix_compress(
            state,
            state_words,
            buffer,
            sizeof(buffer),
            0,
            FCH_DOMAIN_LEAF,
            FCH_MIX_FLAG_LEAF_HEADER
        ))
        return 0;

    size_t processed = 0;
    while (processed < length) {
        size_t chunk = length - processed;
        if (chunk > sizeof(buffer) - 8u)
            chunk = sizeof(buffer) - 8u;

        for (size_t i = 0; i < sizeof(buffer); i++)
            buffer[i] = 0;
        fch_store_le64(buffer + 0u, FCH_TREE_TAG_LEAF_DATA);
        if (!reader->read(
                reader->context,
                offset + processed,
                buffer + 8u,
                chunk
            ))
            return 0;

        processed += chunk;
        uint64_t flags = FCH_MIX_FLAG_LEAF_DATA;
        if (processed == length)
            flags |= FCH_MIX_FLAG_FINAL;

        if (!fch_mix_compress(
                state,
                state_words,
                buffer,
                chunk + 8u,
                (uint64_t)processed,
                FCH_DOMAIN_LEAF,
                flags
            ))
            return 0;
    }

    out->tree = position;
    return 1;
}

void fch_leaf_compress(
    const uint8_t *data,
    size_t length,
    fch_state_t *out,
    int depth
) {
    if (!data && length > 0u)
        return;

    fch_memory_reader_t memory = { data, length };
    fch_reader_t reader = { fch_memory_read, &memory };
    (void)fch_leaf_compress_reader(&reader, 0, length, out, depth);
}
