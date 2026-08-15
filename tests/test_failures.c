#include <stdint.h>
#include <stdio.h>
#include <string.h>

#undef malloc
#undef calloc

#include <stdlib.h>

#include "combine.h"
#include "fch.h"
#include "fch_stream.h"
#include "fractal.h"
#include "leaf.h"
#include "params.h"

static size_t allocation_calls;
static size_t allocation_failure;

void *fch_test_malloc(size_t size) {
    allocation_calls++;
    if (allocation_failure != 0u &&
        allocation_calls == allocation_failure)
        return NULL;
    return malloc(size);
}

void *fch_test_calloc(size_t count, size_t size) {
    allocation_calls++;
    if (allocation_failure != 0u &&
        allocation_calls == allocation_failure)
        return NULL;
    return calloc(count, size);
}

static void allow_allocations(void) {
    allocation_calls = 0u;
    allocation_failure = 0u;
}

static void fail_allocation(size_t index) {
    allocation_calls = 0u;
    allocation_failure = index;
}

static int all_zero(const uint8_t *data, size_t length) {
    for (size_t i = 0u; i < length; i++) {
        if (data[i] != 0u)
            return 0;
    }
    return 1;
}

static void fill_input(uint8_t *data, size_t length) {
    for (size_t i = 0u; i < length; i++)
        data[i] = (uint8_t)(i * 131u + i / 17u);
}

static int test_one_shot_allocation_failures(void) {
    uint8_t input[2049];
    uint8_t expected256[32];
    uint8_t expected512[64];
    uint8_t output256[32];
    uint8_t output512[64];
    fill_input(input, sizeof(input));

    allow_allocations();
    if (!fch_hash_256_checked(input, sizeof(input), expected256) ||
        !fch_hash_512_checked(input, sizeof(input), expected512))
        return 0;

    for (size_t failure = 1u; failure <= 2u; failure++) {
        memset(output256, 0xA5, sizeof(output256));
        fail_allocation(failure);
        if (fch_hash_256_checked(input, sizeof(input), output256) ||
            allocation_calls != failure ||
            !all_zero(output256, sizeof(output256)))
            return 0;

        memset(output512, 0xA5, sizeof(output512));
        fail_allocation(failure);
        if (fch_hash_512_checked(input, sizeof(input), output512) ||
            allocation_calls != failure ||
            !all_zero(output512, sizeof(output512)))
            return 0;
    }

    allow_allocations();
    if (!fch_hash_256_checked(input, sizeof(input), output256) ||
        !fch_hash_512_checked(input, sizeof(input), output512))
        return 0;
    return memcmp(expected256, output256, sizeof(expected256)) == 0 &&
        memcmp(expected512, output512, sizeof(expected512)) == 0;
}

static int test_stream_allocation_failures(void) {
    uint8_t input[2049];
    uint8_t expected[32];
    uint8_t output[32];
    fill_input(input, sizeof(input));

    allow_allocations();
    if (!fch_hash_256_checked(input, sizeof(input), expected))
        return 0;

    fch256_ctx update_failure;
    fch256_init(&update_failure);
    fail_allocation(1u);
    if (fch256_update(&update_failure, input, sizeof(input)) ||
        !update_failure.failed || update_failure.storage != NULL ||
        update_failure.length != 0u || allocation_calls != 1u)
        return 0;
    memset(output, 0xA5, sizeof(output));
    if (fch256_final_checked(&update_failure, output) ||
        !all_zero(output, sizeof(output)) ||
        update_failure.storage != NULL)
        return 0;
    fch256_free(&update_failure);

    fch512_ctx final_failure;
    uint8_t output512[64];
    fch512_init(&final_failure);
    fail_allocation(1u);
    memset(output512, 0xA5, sizeof(output512));
    if (fch512_final_checked(&final_failure, output512) ||
        !final_failure.failed || !final_failure.finalized ||
        final_failure.storage != NULL || allocation_calls != 1u ||
        !all_zero(output512, sizeof(output512)))
        return 0;
    fch512_free(&final_failure);

    fch256_ctx no_final_allocation;
    fch256_init(&no_final_allocation);
    allow_allocations();
    if (!fch256_update(
            &no_final_allocation,
            input,
            sizeof(input)
        ) || allocation_calls != 1u)
        return 0;
    fail_allocation(1u);
    if (!fch256_final_checked(&no_final_allocation, output) ||
        allocation_calls != 0u ||
        memcmp(expected, output, sizeof(expected)) != 0 ||
        no_final_allocation.storage != NULL)
        return 0;
    fch256_free(&no_final_allocation);
    allow_allocations();
    return 1;
}

static int test_stream_state_mismatch(void) {
    static const uint8_t input[] = {0x11u, 0x22u, 0x33u};
    uint8_t output[32];
    fch256_ctx context;

    fch256_init(&context);
    allow_allocations();
    if (!fch256_update(&context, input, sizeof(input)))
        return 0;
    context.length++;
    if (fch256_update(&context, NULL, 0u) || !context.failed)
        return 0;
    memset(output, 0xA5, sizeof(output));
    if (fch256_final_checked(&context, output) ||
        !all_zero(output, sizeof(output)) || context.storage != NULL)
        return 0;
    fch256_free(&context);

    fch256_init(&context);
    if (!fch256_update(&context, input, sizeof(input)))
        return 0;
    context.length++;
    memset(output, 0xA5, sizeof(output));
    if (fch256_final_checked(&context, output) ||
        !context.failed || !context.finalized ||
        !all_zero(output, sizeof(output)) || context.storage != NULL)
        return 0;
    fch256_free(&context);
    return 1;
}

static int test_tree_allocation_failures(void) {
    uint8_t input[FCH_TREE_LEAF_BYTES * 2u];
    uint64_t left_words[FCH_INTERNAL_STATE_WORDS];
    uint64_t right_words[FCH_INTERNAL_STATE_WORDS];
    uint64_t parent_words[FCH_INTERNAL_STATE_WORDS];
    fill_input(input, sizeof(input));

    fch_memory_reader_t memory = {input, sizeof(input)};
    fch_reader_t reader = {fch_memory_read, &memory};
    fch_state_t children[FCH_TREE_ARITY] = {
        {left_words, FCH_INTERNAL_STATE_WORDS, {0, 0, 0, 0, 0}},
        {right_words, FCH_INTERNAL_STATE_WORDS, {0, 0, 0, 0, 0}}
    };
    if (!fch_leaf_compress_reader(
            &reader,
            0u,
            FCH_TREE_LEAF_BYTES,
            &children[0],
            0
        ) ||
        !fch_leaf_compress_reader(
            &reader,
            FCH_TREE_LEAF_BYTES,
            FCH_TREE_LEAF_BYTES,
            &children[1],
            0
        ))
        return 0;

    fch_block_t blocks[FCH_TREE_ARITY] = {
        {0u, FCH_TREE_LEAF_BYTES},
        {FCH_TREE_LEAF_BYTES, FCH_TREE_LEAF_BYTES}
    };

    fail_allocation(1u);
    fch_state_t allocated = fch_combine(
        children,
        blocks,
        FCH_TREE_ARITY,
        sizeof(input),
        FCH_INTERNAL_STATE_WORDS,
        0
    );
    if (allocated.state != NULL || allocation_calls != 1u)
        return 0;

    fch_state_t parent = {
        parent_words,
        FCH_INTERNAL_STATE_WORDS,
        {0, 0, 0, 0, 0}
    };
    fail_allocation(1u);
    if (!fch_combine_into(
            children,
            blocks,
            FCH_TREE_ARITY,
            sizeof(input),
            FCH_INTERNAL_STATE_WORDS,
            0,
            &parent
        ) || allocation_calls != 0u)
        return 0;

    fail_allocation(1u);
    fch_state_t processed = fch_process_reader(
        &reader,
        0u,
        sizeof(input),
        0,
        FCH_INTERNAL_STATE_WORDS
    );
    if (processed.state != NULL || allocation_calls != 1u)
        return 0;

    allow_allocations();
    allocated = fch_combine(
        children,
        blocks,
        FCH_TREE_ARITY,
        sizeof(input),
        FCH_INTERNAL_STATE_WORDS,
        0
    );
    if (!allocated.state ||
        memcmp(
            allocated.state,
            parent.state,
            FCH_INTERNAL_STATE_WORDS * sizeof(*parent.state)
        ) != 0) {
        free(allocated.state);
        return 0;
    }
    free(allocated.state);
    return 1;
}

int main(void) {
    if (!test_one_shot_allocation_failures()) {
        printf("FAIL: one-shot allocation failures\n");
        return 1;
    }
    if (!test_stream_allocation_failures()) {
        printf("FAIL: streaming allocation failures\n");
        return 1;
    }
    if (!test_stream_state_mismatch()) {
        printf("FAIL: streaming state mismatch\n");
        return 1;
    }
    if (!test_tree_allocation_failures()) {
        printf("FAIL: tree allocation failures\n");
        return 1;
    }

    printf("PASS: allocation and streaming failure paths\n");
    return 0;
}
