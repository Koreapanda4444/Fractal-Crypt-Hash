#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fch_stream.h"
#include "params.h"

enum {
    EXHAUSTIVE_MAX_LENGTH = 4097,
    MAX_INPUT_LENGTH = 65537
};

typedef enum {
    CHUNKS_WHOLE,
    CHUNKS_FIXED,
    CHUNKS_ALTERNATING,
    CHUNKS_RANDOM
} chunk_kind_t;

typedef struct {
    const char *name;
    chunk_kind_t kind;
    size_t width;
    uint64_t seed;
} chunk_plan_t;

typedef struct {
    uint8_t digest256[32];
    uint8_t digest512[64];
} digest_pair_t;

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

static void fill_input(uint8_t *data, size_t length) {
    uint64_t state = UINT64_C(0x53545245414D4551);
    size_t offset = 0u;

    while (offset < length) {
        uint64_t value = splitmix64_next(&state);
        for (size_t i = 0u; i < 8u && offset < length; i++) {
            data[offset++] = (uint8_t)value;
            value >>= 8u;
        }
    }
}

static size_t next_chunk(
    const chunk_plan_t *plan,
    size_t index,
    size_t remaining,
    uint64_t *state
) {
    static const size_t alternating[] = {
        1u, 1023u, 2u, 1022u, 55u, 9u,
        64u, 65u, 511u, 513u, 2049u, 7u
    };
    size_t count;

    if (plan->kind == CHUNKS_WHOLE) {
        count = remaining;
    } else if (plan->kind == CHUNKS_FIXED) {
        count = plan->width;
    } else if (plan->kind == CHUNKS_ALTERNATING) {
        count = alternating[
            index % (sizeof(alternating) / sizeof(alternating[0]))
        ];
    } else {
        count = 1u + (size_t)(splitmix64_next(state) % plan->width);
    }

    return count < remaining ? count : remaining;
}

static int direct_hash(
    const uint8_t *data,
    size_t length,
    digest_pair_t *digests
) {
    return
        fch_hash_256_checked(data, length, digests->digest256) &&
        fch_hash_512_checked(data, length, digests->digest512);
}

static int check_plan(
    const uint8_t *data,
    size_t length,
    const chunk_plan_t *plan,
    const digest_pair_t *expected
) {
    fch256_ctx ctx256;
    fch512_ctx ctx512;
    digest_pair_t actual;
    uint64_t state = plan->seed ^
        ((uint64_t)length * UINT64_C(0xD6E8FEB86659FD93));
    size_t offset = 0u;
    size_t chunk_index = 0u;
    int ok;

    fch256_init(&ctx256);
    fch512_init(&ctx512);
    ok = fch256_update(&ctx256, NULL, 0u) &&
        fch512_update(&ctx512, NULL, 0u);

    while (ok && offset < length) {
        const uint8_t *zero_data =
            (chunk_index & 1u) != 0u ? data + offset : NULL;
        ok = fch256_update(&ctx256, zero_data, 0u) &&
            fch512_update(&ctx512, zero_data, 0u);
        if (!ok)
            break;

        size_t count = next_chunk(
            plan,
            chunk_index,
            length - offset,
            &state
        );
        if (count == 0u) {
            ok = 0;
            break;
        }

        ok = fch256_update(&ctx256, data + offset, count) &&
            fch512_update(&ctx512, data + offset, count);
        if (ok) {
            offset += count;
            ok = ctx256.length == offset && ctx512.length == offset;
        }
        chunk_index++;
    }

    if (ok)
        ok = fch256_update(&ctx256, data + offset, 0u) &&
            fch512_update(&ctx512, data + offset, 0u);
    if (ok)
        ok = fch256_final_checked(&ctx256, actual.digest256) &&
            fch512_final_checked(&ctx512, actual.digest512);
    if (ok)
        ok = ctx256.storage == NULL && ctx512.storage == NULL &&
            ctx256.length == length && ctx512.length == length &&
            !ctx256.failed && !ctx512.failed &&
            ctx256.finalized && ctx512.finalized;
    if (ok)
        ok = memcmp(
                expected->digest256,
                actual.digest256,
                sizeof(actual.digest256)
            ) == 0 &&
            memcmp(
                expected->digest512,
                actual.digest512,
                sizeof(actual.digest512)
            ) == 0;

    fch256_free(&ctx256);
    fch512_free(&ctx512);

    if (!ok) {
        fprintf(
            stderr,
            "FAIL: %s partition at %zu bytes\n",
            plan->name,
            length
        );
    }
    return ok;
}

static int check_length(
    const uint8_t *data,
    size_t length,
    const chunk_plan_t *plans,
    size_t plan_count
) {
    digest_pair_t expected;

    if (!direct_hash(data, length, &expected)) {
        fprintf(stderr, "FAIL: one-shot hash at %zu bytes\n", length);
        return 0;
    }

    for (size_t i = 0u; i < plan_count; i++) {
        if (!check_plan(data, length, &plans[i], &expected))
            return 0;
    }
    return 1;
}

int main(void) {
    static const chunk_plan_t exhaustive_plans[] = {
        {"whole", CHUNKS_WHOLE, 0u, 0u},
        {"random-small", CHUNKS_RANDOM, 97u,
            UINT64_C(0x94A53EED00000001)},
        {"random-wide", CHUNKS_RANDOM, 2053u,
            UINT64_C(0x94A53EED00000002)}
    };
    static const chunk_plan_t boundary_plans[] = {
        {"whole", CHUNKS_WHOLE, 0u, 0u},
        {"byte", CHUNKS_FIXED, 1u, 0u},
        {"block-63", CHUNKS_FIXED, 63u, 0u},
        {"block-64", CHUNKS_FIXED, 64u, 0u},
        {"block-65", CHUNKS_FIXED, 65u, 0u},
        {"leaf-minus-one", CHUNKS_FIXED, FCH_TREE_LEAF_BYTES - 1u, 0u},
        {"leaf", CHUNKS_FIXED, FCH_TREE_LEAF_BYTES, 0u},
        {"leaf-plus-one", CHUNKS_FIXED, FCH_TREE_LEAF_BYTES + 1u, 0u},
        {"alternating", CHUNKS_ALTERNATING, 0u, 0u},
        {"random", CHUNKS_RANDOM, 4099u,
            UINT64_C(0x94A53EED00000003)}
    };
    static const size_t boundary_lengths[] = {
        0u, 1u, 54u, 55u, 56u, 63u, 64u, 65u,
        1014u, 1015u, 1016u, 1023u, 1024u, 1025u,
        2038u, 2039u, 2040u, 2047u, 2048u, 2049u,
        3062u, 3063u, 3064u, 3071u, 3072u, 3073u,
        4086u, 4087u, 4088u, 4095u, 4096u, 4097u,
        8182u, 8183u, 8184u, 8191u, 8192u, 8193u,
        16374u, 16375u, 16376u, 16383u, 16384u, 16385u,
        32758u, 32759u, 32760u, 32767u, 32768u, 32769u,
        65526u, 65527u, 65528u, 65535u, 65536u, 65537u
    };
    uint8_t *data = (uint8_t *)malloc(MAX_INPUT_LENGTH);
    if (!data) {
        fprintf(stderr, "FAIL: input allocation\n");
        return 1;
    }
    fill_input(data, MAX_INPUT_LENGTH);

    for (size_t length = 0u; length <= EXHAUSTIVE_MAX_LENGTH; length++) {
        if (!check_length(
                data,
                length,
                exhaustive_plans,
                sizeof(exhaustive_plans) / sizeof(exhaustive_plans[0])
            )) {
            free(data);
            return 1;
        }
    }

    for (size_t i = 0u;
         i < sizeof(boundary_lengths) / sizeof(boundary_lengths[0]);
         i++) {
        if (!check_length(
                data,
                boundary_lengths[i],
                boundary_plans,
                sizeof(boundary_plans) / sizeof(boundary_plans[0])
            )) {
            free(data);
            return 1;
        }
    }

    free(data);
    printf("PASS: one-shot and streaming partition equivalence\n");
    return 0;
}
