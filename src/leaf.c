#include "leaf.h"
#include "params.h"
#include "sbox.h"
#include "bitops.h"

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
    uint64_t length_tag = (uint64_t)length;

    for (size_t i = 0; i < S; i++) {
        state[i] = UINT64_C(0x9E3779B97F4A7C15);
        state[i] ^= domain;
        state[i] ^= length_tag * UINT64_C(0xD6E8FEB86659FD93);
        state[i] ^= (uint64_t)normalized_depth * UINT64_C(0xA24BAED4963EE407);
        state[i] ^= (uint64_t)S * UINT64_C(0x9FB21C651E98DF25);
        state[i] ^= (uint64_t)i * UINT64_C(0xC2B2AE3D27D4EB4F);
        state[i] = fch_sbox64(state[i]);
    }

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
            size_t idx = i % S;
            size_t next = (idx + 1u) % S;
            size_t far = (idx + 2u) % S;
            uint64_t message = (uint64_t)buffer[j];
            message ^= (uint64_t)i * UINT64_C(0x9E3779B97F4A7C15);
            message ^= length_tag * UINT64_C(0xA24BAED4963EE407);

            state[idx] ^= message + UINT64_C(0xD6E8FEB86659FD93);
            state[idx] = fch_rotl64(
                state[idx],
                (unsigned int)(((i + idx * 13u) % 63u) + 1u)
            );
            state[next] += state[idx] ^ fch_rotl64(
                message,
                (unsigned int)(((i * 7u + idx) % 63u) + 1u)
            );
            state[far] ^= fch_rotl64(
                state[idx] + state[next],
                (unsigned int)(((i * 11u + idx * 3u) % 63u) + 1u)
            );
        }

        processed += chunk;
    }

    for (unsigned int round = 0; round < 4u; round++) {
        for (size_t i = 0; i < S; i++) {
            uint64_t left = state[(i + S - 1u) % S];
            uint64_t current = state[i];
            uint64_t right = state[(i + 1u) % S];
            uint64_t mixed = current ^ domain;

            mixed ^= length_tag * UINT64_C(0x9E3779B97F4A7C15);
            mixed ^= (uint64_t)normalized_depth << 32u;
            mixed ^= (uint64_t)round * UINT64_C(0xD6E8FEB86659FD93);
            mixed += fch_rotl64(
                left,
                (unsigned int)(((i * 7u + round * 11u) % 63u) + 1u)
            );
            mixed ^= fch_rotl64(
                right,
                (unsigned int)(((i * 13u + round * 5u) % 63u) + 1u)
            );
            mixed = fch_sbox64(mixed);
            state[i] = fch_rotl64(
                mixed,
                (unsigned int)(((i * 17u + round * 19u) % 63u) + 1u)
            );
        }
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
