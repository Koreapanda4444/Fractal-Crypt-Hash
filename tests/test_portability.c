#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "bitops.h"
#include "fch.h"
#include "fch_stream.h"

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

    printf("PASS: portable primitives\n");
    return 0;
}
