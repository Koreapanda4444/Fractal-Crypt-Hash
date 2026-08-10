#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bitops.h"
#include "fch.h"
#include "fch_stream.h"
#include "mix.h"

static int test_rotate(void) {
    const uint64_t value = UINT64_C(0x0123456789ABCDEF);

    if (fch_rotl64(value, 0) != value)
        return 0;
    if (fch_rotl64(value, 64) != value)
        return 0;
    if (fch_rotl64(value, 4) != UINT64_C(0x123456789ABCDEF0))
        return 0;
    if (fch_rotl64(value, 68) != UINT64_C(0x123456789ABCDEF0))
        return 0;

    return 1;
}

static int test_little_endian(void) {
    const uint64_t value = UINT64_C(0x0123456789ABCDEF);
    const uint8_t expected[8] = {
        0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01
    };
    uint8_t encoded[8];

    fch_store_le64(encoded, value);
    if (memcmp(encoded, expected, sizeof(expected)) != 0)
        return 0;
    if (fch_load_le64(encoded) != value)
        return 0;

    return 1;
}

static int test_tree_encoding_tags(void) {
    static const uint64_t tags[] = {
        FCH_TREE_TAG_LEAF_HEADER,
        FCH_TREE_TAG_LEAF_DATA,
        FCH_TREE_TAG_NODE_HEADER,
        FCH_TREE_TAG_NODE_CHILD,
        FCH_TREE_TAG_OUTPUT
    };
    static const uint8_t expected[][8] = {
        { 'F', 'C', 'H', 'L', 'E', 'A', 'F', '1' },
        { 'F', 'C', 'H', 'L', 'D', 'A', 'T', '1' },
        { 'F', 'C', 'H', 'N', 'O', 'D', 'E', '1' },
        { 'F', 'C', 'H', 'C', 'H', 'L', 'D', '1' },
        { 'F', 'C', 'H', 'O', 'U', 'T', '0', '1' }
    };

    if (FCH_TREE_ENCODING_VERSION != UINT64_C(1))
        return 0;

    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        uint8_t encoded[8];
        fch_store_le64(encoded, tags[i]);
        if (memcmp(encoded, expected[i], sizeof(encoded)) != 0)
            return 0;
    }

    return 1;
}

static int all_zero(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

static int test_bounded_streaming(void) {
    const size_t length = 262273u;
    const size_t chunks[] = { 1u, 7u, 4093u, 65537u, 31u };
    uint8_t *data = (uint8_t *)malloc(length);
    if (!data)
        return 0;

    for (size_t i = 0; i < length; i++)
        data[i] = (uint8_t)((i * 131u + i / 17u) & 0xFFu);

    uint8_t direct256[32];
    uint8_t direct512[64];
    uint8_t streamed256[32];
    uint8_t streamed512[64];
    if (!fch_hash_256_checked(data, length, direct256) ||
        !fch_hash_512_checked(data, length, direct512)) {
        free(data);
        return 0;
    }

    fch256_ctx ctx256;
    fch512_ctx ctx512;
    fch256_init(&ctx256);
    fch512_init(&ctx512);

    int ok = fch256_update(&ctx256, NULL, 0) &&
        fch512_update(&ctx512, NULL, 0);
    size_t offset = 0;
    size_t chunk_index = 0;
    while (ok && offset < length) {
        size_t count = chunks[chunk_index %
            (sizeof(chunks) / sizeof(chunks[0]))];
        if (count > length - offset)
            count = length - offset;
        ok = fch256_update(&ctx256, data + offset, count) &&
            fch512_update(&ctx512, data + offset, count);
        offset += count;
        chunk_index++;
    }

    if (ok)
        ok = fch256_final_checked(&ctx256, streamed256) &&
            fch512_final_checked(&ctx512, streamed512);
    if (ok)
        ok = ctx256.storage == NULL && ctx512.storage == NULL;
    if (ok)
        ok = memcmp(direct256, streamed256, sizeof(direct256)) == 0 &&
            memcmp(direct512, streamed512, sizeof(direct512)) == 0;
    if (ok)
        ok = !fch256_update(&ctx256, data, 1) &&
            !fch512_update(&ctx512, data, 1);

    uint8_t repeated[32];
    memset(repeated, 0xA5, sizeof(repeated));
    if (ok)
        ok = !fch256_final_checked(&ctx256, repeated) &&
            all_zero(repeated, sizeof(repeated));

    fch256_free(&ctx256);
    fch512_free(&ctx512);
    free(data);
    return ok;
}

static int test_stream_failure_state(void) {
    fch256_ctx ctx;
    uint8_t output[32];
    fch256_init(&ctx);

    if (fch256_update(&ctx, NULL, 1)) {
        fch256_free(&ctx);
        return 0;
    }

    memset(output, 0xA5, sizeof(output));
    int ok = !fch256_final_checked(&ctx, output) &&
        all_zero(output, sizeof(output));
    fch256_free(&ctx);
    return ok;
}

static int test_empty_stream(void) {
    uint8_t direct256[32];
    uint8_t direct512[64];
    uint8_t streamed256[32];
    uint8_t streamed512[64];
    if (!fch_hash_256_checked(NULL, 0, direct256) ||
        !fch_hash_512_checked(NULL, 0, direct512))
        return 0;

    fch256_ctx ctx256;
    fch512_ctx ctx512;
    fch256_init(&ctx256);
    fch512_init(&ctx512);
    int ok = fch256_final_checked(&ctx256, streamed256) &&
        fch512_final_checked(&ctx512, streamed512) &&
        memcmp(direct256, streamed256, sizeof(direct256)) == 0 &&
        memcmp(direct512, streamed512, sizeof(direct512)) == 0;
    fch256_free(&ctx256);
    fch512_free(&ctx512);
    return ok;
}

static int test_variant_domain_separation(void) {
    static const uint8_t input[] = "variant-domain-check";
    uint8_t output256[32];
    uint8_t output512[64];

    if (!fch_hash_256_checked(
            input,
            sizeof(input) - 1u,
            output256
        ))
        return 0;
    if (!fch_hash_512_checked(
            input,
            sizeof(input) - 1u,
            output512
        ))
        return 0;

    return memcmp(output256, output512, sizeof(output256)) != 0;
}

int main(void) {
    uint8_t output[32];
    uint8_t streamed[32];

    if (!test_rotate()) {
        printf("FAIL: portable rotation\n");
        return 1;
    }
    if (!test_little_endian()) {
        printf("FAIL: little-endian conversion\n");
        return 1;
    }
    if (!test_tree_encoding_tags()) {
        printf("FAIL: canonical tree encoding tags\n");
        return 1;
    }
    if (!fch_hash_256_checked((const uint8_t *)"abc", 3, output)) {
        printf("FAIL: checked API\n");
        return 1;
    }

    fch256_ctx ctx;
    fch256_init(&ctx);
    if (!fch256_update(&ctx, (const uint8_t *)"a", 1) ||
        !fch256_update(&ctx, (const uint8_t *)"bc", 2) ||
        !fch256_final_checked(&ctx, streamed)) {
        fch256_free(&ctx);
        printf("FAIL: checked streaming API\n");
        return 1;
    }
    fch256_free(&ctx);

    if (memcmp(output, streamed, sizeof(output)) != 0) {
        printf("FAIL: streaming mismatch\n");
        return 1;
    }
    if (!test_bounded_streaming()) {
        printf("FAIL: bounded streaming\n");
        return 1;
    }
    if (!test_stream_failure_state()) {
        printf("FAIL: streaming failure state\n");
        return 1;
    }
    if (!test_empty_stream()) {
        printf("FAIL: empty streaming input\n");
        return 1;
    }
    if (!test_variant_domain_separation()) {
        printf("FAIL: output variant domain separation\n");
        return 1;
    }

    printf("PASS: portable primitives and bounded streaming\n");
    return 0;
}
