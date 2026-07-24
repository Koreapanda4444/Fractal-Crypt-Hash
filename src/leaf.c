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
    if (!out || !out->state || out->words == 0)
        return 0;
    if (!reader || !reader->read)
        return 0;
    if (offset > SIZE_MAX - length)
        return 0;

    size_t S = out->words;
    uint64_t *state = out->state;
    unsigned int normalized_depth = depth < 0 ? 0u : (unsigned int)depth;
    uint64_t domain = normalized_depth == 0u
        ? UINT64_C(0x524F4F544C454146)
        : UINT64_C(0x494E544C45414631);

    if (!fch_mix_init(state, S, domain))
        return 0;

    uint8_t buffer[FCH_MIX_BLOCK_SIZE] = {0};
    fch_store_le64(buffer + 0u, domain);
    fch_store_le64(buffer + 8u, (uint64_t)length);
    fch_store_le64(buffer + 16u, (uint64_t)normalized_depth);
    fch_store_le64(buffer + 24u, (uint64_t)S);
    fch_store_le64(buffer + 32u, FCH_MIN_BLOCK_SIZE);
    fch_store_le64(buffer + 40u, FCH_MIX_ROUNDS);

    if (!fch_mix_compress(
            state,
            S,
            buffer,
            48u,
            0,
            domain,
            FCH_MIX_FLAG_PARAMETER
        ))
        return 0;

    size_t processed = 0;

    if (length == 0) {
        for (size_t i = 0; i < sizeof(buffer); i++)
            buffer[i] = 0;
        if (!fch_mix_compress(
                state,
                S,
                buffer,
                0,
                0,
                domain,
                FCH_MIX_FLAG_LEAF_DATA | FCH_MIX_FLAG_FINAL
            ))
            return 0;
    }

    while (processed < length) {
        size_t chunk = length - processed;
        if (chunk > sizeof(buffer))
            chunk = sizeof(buffer);

        for (size_t i = 0; i < sizeof(buffer); i++)
            buffer[i] = 0;
        if (!reader->read(
                reader->context,
                offset + processed,
                buffer,
                chunk
            ))
            return 0;

        uint64_t flags = FCH_MIX_FLAG_LEAF_DATA;
        if (chunk == length - processed)
            flags |= FCH_MIX_FLAG_FINAL;

        processed += chunk;
        if (!fch_mix_compress(
                state,
                S,
                buffer,
                chunk,
                (uint64_t)processed,
                domain,
                flags
            ))
            return 0;
    }

    out->words = S;
    return 1;
}

void fch_leaf_compress(
    const uint8_t *data,
    size_t length,
    fch_state_t *out,
    int depth
) {
    if (!data && length > 0)
        return;

    fch_memory_reader_t memory = { data, length };
    fch_reader_t reader = { fch_memory_read, &memory };
    (void)fch_leaf_compress_reader(&reader, 0, length, out, depth);
}
