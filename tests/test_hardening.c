#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fch_stream.h"
#include "fractal.h"
#include "params.h"

enum {
    FCH_FUZZ_MAX_INPUT = 1024 * 1024,
    FCH_FUZZ_SMOKE_CASES = 1024,
    FCH_FUZZ_SMOKE_MAX_LENGTH = 65536,
    FCH_FUZZ_STRUCTURED_PATTERNS = 4,
    FCH_LARGE_STREAM_BYTES = 8 * 1024 * 1024,
    FCH_LARGE_STREAM_BUFFER = 64 * 1024
};

static int all_zero(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

static void require_or_abort(int condition) {
    if (!condition)
        abort();
}

static void check_split_invariants(const uint8_t *data, size_t size) {
    fch_block_t blocks[FCH_TREE_ARITY];
    int depth = size == 0 ? 0 : (int)(data[0] & 31u);
    size_t count = fch_fractal_split(
        data,
        size,
        depth,
        blocks,
        FCH_TREE_ARITY
    );

    if (size == 0u) {
        require_or_abort(count == 0u);
        return;
    }

    require_or_abort(count > 0u && count <= FCH_TREE_ARITY);

    size_t covered = 0;
    for (size_t i = 0; i < count; i++) {
        require_or_abort(blocks[i].offset == covered);
        require_or_abort(blocks[i].length > 0u);
        require_or_abort(blocks[i].length <= size - covered);
        covered += blocks[i].length;
    }
    require_or_abort(covered == size);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if ((!data && size > 0) || size > FCH_FUZZ_MAX_INPUT)
        return 0;

    uint8_t direct256[32];
    uint8_t direct512[64];
    if (!fch_hash_256_checked(data, size, direct256) ||
        !fch_hash_512_checked(data, size, direct512))
        return 0;

    check_split_invariants(data, size);

    fch256_ctx ctx256;
    fch512_ctx ctx512;
    fch256_init(&ctx256);
    fch512_init(&ctx512);

    if (!fch256_update(&ctx256, NULL, 0) ||
        !fch512_update(&ctx512, NULL, 0)) {
        fch256_free(&ctx256);
        fch512_free(&ctx512);
        return 0;
    }

    size_t offset = 0;
    size_t chunk_index = 0;
    while (offset < size) {
        size_t selector = (offset + chunk_index) % size;
        size_t count = (size_t)data[selector] + 1u;
        if (count > size - offset)
            count = size - offset;

        if (!fch256_update(&ctx256, data + offset, count) ||
            !fch512_update(&ctx512, data + offset, count)) {
            fch256_free(&ctx256);
            fch512_free(&ctx512);
            return 0;
        }
        offset += count;
        chunk_index++;
    }

    uint8_t streamed256[32];
    uint8_t streamed512[64];
    if (!fch256_final_checked(&ctx256, streamed256) ||
        !fch512_final_checked(&ctx512, streamed512)) {
        fch256_free(&ctx256);
        fch512_free(&ctx512);
        return 0;
    }

    require_or_abort(
        memcmp(direct256, streamed256, sizeof(direct256)) == 0
    );
    require_or_abort(
        memcmp(direct512, streamed512, sizeof(direct512)) == 0
    );
    require_or_abort(ctx256.storage == NULL && ctx512.storage == NULL);
    require_or_abort(!fch256_update(&ctx256, data, size > 0 ? 1u : 0u));
    require_or_abort(!fch512_update(&ctx512, data, size > 0 ? 1u : 0u));

    memset(streamed256, 0xA5, sizeof(streamed256));
    memset(streamed512, 0xA5, sizeof(streamed512));
    require_or_abort(!fch256_final_checked(&ctx256, streamed256));
    require_or_abort(!fch512_final_checked(&ctx512, streamed512));
    require_or_abort(all_zero(streamed256, sizeof(streamed256)));
    require_or_abort(all_zero(streamed512, sizeof(streamed512)));

    fch256_free(&ctx256);
    fch512_free(&ctx512);
    fch256_free(&ctx256);
    fch512_free(&ctx512);
    return 0;
}

#ifdef FCH_FUZZ_STANDALONE

static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t value;

    *state += UINT64_C(0x9E3779B97F4A7C15);
    value = *state;
    value = (value ^ (value >> 30u)) *
        UINT64_C(0xBF58476D1CE4E5B9);
    value = (value ^ (value >> 27u)) *
        UINT64_C(0x94D049BB133111EB);
    return value ^ (value >> 31u);
}

static void fill_random(uint8_t *data, size_t length, uint64_t *state) {
    size_t offset = 0;

    while (offset < length) {
        uint64_t value = splitmix64_next(state);
        for (size_t i = 0; i < 8u && offset < length; i++) {
            data[offset++] = (uint8_t)value;
            value >>= 8u;
        }
    }
}

static void fill_structured(
    uint8_t *data,
    size_t length,
    unsigned int pattern
) {
    for (size_t i = 0; i < length; i++) {
        if (pattern == 0u)
            data[i] = 0u;
        else if (pattern == 1u)
            data[i] = 0xFFu;
        else if (pattern == 2u)
            data[i] = (uint8_t)(i * 131u + i / 17u);
        else
            data[i] = (uint8_t)((i & 1u) ? 0xAAu : 0x55u);
    }
}

static int test_fuzz_smoke(void) {
    static const size_t boundary_lengths[] = {
        0u, 1u, 7u, 8u, 31u, 32u, 63u, 64u, 65u,
        127u, 128u, 129u, 255u, 256u, 257u,
        511u, 512u, 513u, 1023u, 1024u, 1025u,
        4095u, 4096u, 4097u, 8191u, 8192u, 8193u,
        16383u, 16384u, 16385u, 32767u, 32768u,
        32769u, 65535u, 65536u
    };
    uint8_t *data = (uint8_t *)malloc(FCH_FUZZ_SMOKE_MAX_LENGTH);
    if (!data)
        return 0;

    uint64_t state = UINT64_C(0x48415244454E494E);
    unsigned int structured_cases = 0u;
    for (size_t i = 0;
         i < sizeof(boundary_lengths) / sizeof(boundary_lengths[0]);
         i++) {
        for (unsigned int pattern = 0;
             pattern < FCH_FUZZ_STRUCTURED_PATTERNS;
             pattern++) {
            fill_structured(data, boundary_lengths[i], pattern);
            (void)LLVMFuzzerTestOneInput(data, boundary_lengths[i]);
            structured_cases++;
        }
    }

    for (unsigned int sample = 0;
         sample < FCH_FUZZ_SMOKE_CASES;
         sample++) {
        size_t length = (size_t)(
            splitmix64_next(&state) %
            (FCH_FUZZ_SMOKE_MAX_LENGTH + 1u)
        );
        fill_random(data, length, &state);
        if (length > 0) {
            data[0] = (uint8_t)sample;
            data[length - 1u] ^= (uint8_t)(sample * 17u);
        }
        (void)LLVMFuzzerTestOneInput(data, length);
    }

    free(data);
    printf(
        "hardening,fuzz_smoke_cases=%u,structured_cases=%u,"
        "boundaries=%u,max_length=%u,PASS\n",
        (unsigned int)FCH_FUZZ_SMOKE_CASES,
        structured_cases,
        (unsigned int)(
            sizeof(boundary_lengths) / sizeof(boundary_lengths[0])
        ),
        (unsigned int)FCH_FUZZ_SMOKE_MAX_LENGTH
    );
    return 1;
}

static int test_api_failures(void) {
    uint8_t byte = 0xA5u;
    uint8_t output256[32];
    uint8_t output512[64];

    memset(output256, 0xA5, sizeof(output256));
    if (fch_hash_256_checked(NULL, 1u, output256) ||
        !all_zero(output256, sizeof(output256)))
        return 0;

    memset(output512, 0xA5, sizeof(output512));
    if (fch_hash_512_checked(NULL, 1u, output512) ||
        !all_zero(output512, sizeof(output512)))
        return 0;

    memset(output256, 0xA5, sizeof(output256));
    if (fch_hash_256_checked(&byte, SIZE_MAX, output256) ||
        !all_zero(output256, sizeof(output256)))
        return 0;

    if (fch_hash_256_checked(&byte, 1u, NULL) ||
        fch_hash_512_checked(&byte, 1u, NULL))
        return 0;
    fch_hash_256(&byte, 1u, NULL);
    fch_hash_512(&byte, 1u, NULL);

    memset(output256, 0xA5, sizeof(output256));
    if (fch256_final_checked(NULL, output256) ||
        !all_zero(output256, sizeof(output256)))
        return 0;
    memset(output512, 0xA5, sizeof(output512));
    if (fch512_final_checked(NULL, output512) ||
        !all_zero(output512, sizeof(output512)))
        return 0;

    fch256_ctx overflow;
    fch256_init(&overflow);
    overflow.length = SIZE_MAX - 8u;
    if (fch256_update(&overflow, &byte, 1u) || !overflow.failed)
        return 0;
    memset(output256, 0xA5, sizeof(output256));
    if (fch256_final_checked(&overflow, output256) ||
        !all_zero(output256, sizeof(output256)) ||
        overflow.storage != NULL)
        return 0;
    fch256_free(&overflow);
    fch256_free(&overflow);

    fch512_ctx lifecycle;
    fch512_init(&lifecycle);
    if (fch512_final_checked(&lifecycle, NULL))
        return 0;
    if (!fch512_final_checked(&lifecycle, output512))
        return 0;
    if (fch512_update(&lifecycle, NULL, 0))
        return 0;
    memset(output512, 0xA5, sizeof(output512));
    if (fch512_final_checked(&lifecycle, output512) ||
        !all_zero(output512, sizeof(output512)))
        return 0;
    fch512_free(&lifecycle);
    fch512_free(&lifecycle);
    fch256_free(NULL);
    fch512_free(NULL);

    printf("hardening,api_failure_paths=PASS\n");
    return 1;
}

typedef struct {
    size_t calls;
} rejecting_reader_t;

static int reject_read(
    void *context,
    size_t offset,
    uint8_t *output,
    size_t length
) {
    rejecting_reader_t *reader = (rejecting_reader_t *)context;
    (void)offset;
    (void)output;
    (void)length;

    if (reader)
        reader->calls++;
    return 0;
}

static int test_reader_failures(void) {
    rejecting_reader_t context = {0};
    fch_reader_t reader = { reject_read, &context };

    fch_state_t leaf = fch_process_reader(
        &reader,
        0,
        FCH_TREE_LEAF_BYTES,
        0,
        FCH_INTERNAL_STATE_WORDS
    );
    if (leaf.state || context.calls == 0u) {
        free(leaf.state);
        return 0;
    }

    context.calls = 0;
    fch_state_t node = fch_process_reader(
        &reader,
        0,
        FCH_TREE_LEAF_BYTES * 2u,
        0,
        FCH_INTERNAL_STATE_WORDS
    );
    if (node.state || context.calls == 0u) {
        free(node.state);
        return 0;
    }

    uint8_t bytes[8] = {0};
    uint8_t output = 0;
    fch_memory_reader_t memory = { bytes, sizeof(bytes) };
    if (fch_memory_read(&memory, SIZE_MAX, &output, 1u) ||
        fch_memory_read(&memory, sizeof(bytes), &output, 1u) ||
        !fch_memory_read(&memory, sizeof(bytes), NULL, 0u))
        return 0;

    printf("hardening,reader_failure_paths=PASS\n");
    return 1;
}

static void fill_large_pattern(
    uint8_t *buffer,
    size_t length,
    size_t absolute_offset
) {
    for (size_t i = 0; i < length; i++) {
        size_t position = absolute_offset + i;
        buffer[i] = (uint8_t)(
            position * 131u + position / 17u
        );
    }
}

static int test_large_stream(void) {
    static const size_t chunks[] = {
        1u, 7u, 63u, 4093u, 8191u, 16384u
    };
    uint8_t *buffer = (uint8_t *)malloc(FCH_LARGE_STREAM_BUFFER);
    if (!buffer)
        return 0;

    fch256_ctx whole_chunks;
    fch256_ctx mixed_chunks;
    fch256_init(&whole_chunks);
    fch256_init(&mixed_chunks);

    size_t offset = 0;
    size_t chunk_index = 0;
    int ok = 1;
    while (ok && offset < FCH_LARGE_STREAM_BYTES) {
        size_t count = FCH_LARGE_STREAM_BYTES - offset;
        if (count > FCH_LARGE_STREAM_BUFFER)
            count = FCH_LARGE_STREAM_BUFFER;
        fill_large_pattern(buffer, count, offset);

        ok = fch256_update(&whole_chunks, buffer, count);
        size_t local_offset = 0;
        while (ok && local_offset < count) {
            size_t part = chunks[
                chunk_index % (sizeof(chunks) / sizeof(chunks[0]))
            ];
            if (part > count - local_offset)
                part = count - local_offset;
            ok = fch256_update(
                &mixed_chunks,
                buffer + local_offset,
                part
            );
            local_offset += part;
            chunk_index++;
        }
        offset += count;
    }

    uint8_t whole_digest[32];
    uint8_t mixed_digest[32];
    if (ok)
        ok = whole_chunks.length == FCH_LARGE_STREAM_BYTES &&
            mixed_chunks.length == FCH_LARGE_STREAM_BYTES;
    if (ok)
        ok = fch256_final_checked(&whole_chunks, whole_digest) &&
            fch256_final_checked(&mixed_chunks, mixed_digest);
    if (ok)
        ok = whole_chunks.storage == NULL &&
            mixed_chunks.storage == NULL &&
            memcmp(
                whole_digest,
                mixed_digest,
                sizeof(whole_digest)
            ) == 0;

    fch256_free(&whole_chunks);
    fch256_free(&mixed_chunks);
    free(buffer);

    if (ok) {
        printf(
            "hardening,large_stream_bytes=%u,chunk_patterns=2,PASS\n",
            (unsigned int)FCH_LARGE_STREAM_BYTES
        );
    }
    return ok;
}

int main(void) {
    if (!test_fuzz_smoke()) {
        fprintf(stderr, "HARDENING: FAIL (fuzz smoke)\n");
        return 1;
    }
    if (!test_api_failures()) {
        fprintf(stderr, "HARDENING: FAIL (API failures)\n");
        return 1;
    }
    if (!test_reader_failures()) {
        fprintf(stderr, "HARDENING: FAIL (reader failures)\n");
        return 1;
    }
    if (!test_large_stream()) {
        fprintf(stderr, "HARDENING: FAIL (large stream)\n");
        return 1;
    }

    printf("HARDENING: PASS\n");
    return 0;
}

#endif
