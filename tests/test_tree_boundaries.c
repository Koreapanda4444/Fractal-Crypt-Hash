#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug_hooks.h"
#include "fch.h"
#include "fractal.h"
#include "params.h"

typedef struct {
    size_t message_length;
    size_t padded_length;
    size_t leaf_count;
    size_t tree_level;
} boundary_case_t;

typedef struct {
    size_t leaves;
    size_t nodes;
    size_t roots;
    size_t root_level;
    int valid;
} trace_t;

static trace_t trace;

static void reset_trace(void) {
    memset(&trace, 0, sizeof(trace));
    trace.root_level = SIZE_MAX;
    trace.valid = 1;
}

void fch_debug_hook(
    fch_hook_point_t point,
    int level,
    const uint64_t *state,
    size_t state_words
) {
    if (!state || state_words != FCH_INTERNAL_STATE_WORDS || level < 0) {
        trace.valid = 0;
        return;
    }

    if (point == FCH_HOOK_AFTER_LEAF) {
        if (level != 0)
            trace.valid = 0;
        trace.leaves++;
    } else if (point == FCH_HOOK_AFTER_NODE) {
        if (level == 0)
            trace.valid = 0;
        trace.nodes++;
    } else if (point == FCH_HOOK_AFTER_ROOT) {
        trace.roots++;
        trace.root_level = (size_t)level;
    } else {
        trace.valid = 0;
    }
}

static void fill_pattern(uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++)
        data[i] = (uint8_t)(i * 73u + (i >> 2u) + 0x5bu);
}

static int trace_matches(const boundary_case_t *test) {
    return
        trace.valid &&
        trace.leaves == test->leaf_count &&
        trace.nodes == test->leaf_count - 1u &&
        trace.roots == 1u &&
        trace.root_level == test->tree_level;
}

static int check_trace(
    const boundary_case_t *test,
    unsigned int bits
) {
    fprintf(
        stderr,
        "FAIL: FCH-%u trace at %zu bytes "
        "(leaves=%zu nodes=%zu roots=%zu level=%zu)\n",
        bits,
        test->message_length,
        trace.leaves,
        trace.nodes,
        trace.roots,
        trace.root_level
    );
    return 0;
}

static int check_case(
    const boundary_case_t *test,
    const uint8_t *input,
    uint8_t output256[32],
    uint8_t output512[64]
) {
    fch_tree_position_t position;
    if (!fch_tree_position_for_range(
            0u,
            test->padded_length,
            &position
        ) ||
        position.level != test->tree_level ||
        position.first_leaf != 0u ||
        position.leaf_count != test->leaf_count ||
        position.byte_offset != 0u ||
        position.byte_length != test->padded_length) {
        fprintf(
            stderr,
            "FAIL: tree position at %zu padded bytes\n",
            test->padded_length
        );
        return 0;
    }

    reset_trace();
    if (!fch_hash_256_checked(input, test->message_length, output256)) {
        fprintf(
            stderr,
            "FAIL: FCH-256 rejected %zu bytes\n",
            test->message_length
        );
        return 0;
    }
    if (!trace_matches(test))
        return check_trace(test, 256u);

    reset_trace();
    if (!fch_hash_512_checked(input, test->message_length, output512)) {
        fprintf(
            stderr,
            "FAIL: FCH-512 rejected %zu bytes\n",
            test->message_length
        );
        return 0;
    }
    if (!trace_matches(test))
        return check_trace(test, 512u);

    return 1;
}

int main(void) {
    static const boundary_case_t cases[] = {
        {1014u, 1023u, 1u, 0u},
        {1015u, 1024u, 1u, 0u},
        {1016u, 1025u, 2u, 1u},
        {2038u, 2047u, 2u, 1u},
        {2039u, 2048u, 2u, 1u},
        {2040u, 2049u, 3u, 2u},
        {3062u, 3071u, 3u, 2u},
        {3063u, 3072u, 3u, 2u},
        {3064u, 3073u, 4u, 2u},
        {4086u, 4095u, 4u, 2u},
        {4087u, 4096u, 4u, 2u},
        {4088u, 4097u, 5u, 3u}
    };
    uint8_t input[4088];
    uint8_t outputs256[sizeof(cases) / sizeof(cases[0])][32];
    uint8_t outputs512[sizeof(cases) / sizeof(cases[0])][64];

    fill_pattern(input, sizeof(input));

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (!check_case(
                &cases[i],
                input,
                outputs256[i],
                outputs512[i]
            ))
            return 1;
    }

    for (size_t i = 1; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (memcmp(outputs256[i - 1u], outputs256[i], 32u) == 0 ||
            memcmp(outputs512[i - 1u], outputs512[i], 64u) == 0) {
            fprintf(stderr, "FAIL: adjacent tree-boundary digests match\n");
            return 1;
        }
    }

    printf("PASS: padded tree leaf counts and heights\n");
    return 0;
}
