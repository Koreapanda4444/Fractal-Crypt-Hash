#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fractal.h"
#include "params.h"
#include "test_utils.h"

#define REQUIRE(_condition, _message) \
    do { \
        if (!(_condition)) { \
            fprintf(stderr, "FAIL: %s\n", (_message)); \
            return 0; \
        } \
    } while (0)

static void fill_pattern(uint8_t *data, size_t length, uint32_t seed) {
    uint32_t state = seed;
    for (size_t i = 0; i < length; i++) {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        data[i] = (uint8_t)(state + (uint32_t)i * 29u);
    }
}

static size_t left_leaf_count(size_t leaf_count) {
    if (leaf_count < 2u)
        return 0u;

    size_t power = 1u;
    while (power <= (leaf_count - 1u) / 2u)
        power *= 2u;
    return power;
}

static int same_blocks(
    const fch_block_t *left,
    const fch_block_t *right,
    size_t count
) {
    for (size_t i = 0; i < count; i++) {
        if (left[i].offset != right[i].offset ||
            left[i].length != right[i].length)
            return 0;
    }
    return 1;
}

static int check_schedule_case(size_t length) {
    uint8_t *first = (uint8_t *)malloc(length);
    uint8_t *second = (uint8_t *)malloc(length);
    if (!first || !second) {
        free(first);
        free(second);
        fprintf(stderr, "FAIL: schedule test allocation failed\n");
        return 0;
    }

    fill_pattern(first, length, UINT32_C(0x12345678));
    fill_pattern(second, length, UINT32_C(0xD00DFEED));

    fch_block_t base[FCH_TREE_ARITY];
    fch_block_t changed[FCH_TREE_ARITY];
    fch_block_t other_depth[FCH_TREE_ARITY];
    size_t base_count = fch_fractal_split(
        first,
        length,
        0,
        base,
        FCH_TREE_ARITY
    );
    size_t changed_count = fch_fractal_split(
        second,
        length,
        0,
        changed,
        FCH_TREE_ARITY
    );
    size_t depth_count = fch_fractal_split(
        first,
        length,
        63,
        other_depth,
        FCH_TREE_ARITY
    );

    size_t leaves = fch_tree_leaf_count_for_length(length);
    size_t expected_count = leaves == 1u ? 1u : FCH_TREE_ARITY;
    int ok = base_count == expected_count &&
        changed_count == base_count && depth_count == base_count &&
        same_blocks(base, changed, base_count) &&
        same_blocks(base, other_depth, base_count);

    if (ok && leaves == 1u) {
        ok = base[0].offset == 0u && base[0].length == length;
    } else if (ok) {
        size_t left = left_leaf_count(leaves) * FCH_TREE_LEAF_BYTES;
        ok = base[0].offset == 0u && base[0].length == left &&
            base[1].offset == left &&
            base[1].length == length - left;
    }

    free(first);
    free(second);
    REQUIRE(ok, "schedule depends on content, depth, or wrong boundary");
    return 1;
}

static int schedule_cases(void) {
    static const size_t lengths[] = {
        1u,
        FCH_TREE_LEAF_BYTES - 1u,
        FCH_TREE_LEAF_BYTES,
        FCH_TREE_LEAF_BYTES + 1u,
        FCH_TREE_LEAF_BYTES * 2u - 1u,
        FCH_TREE_LEAF_BYTES * 2u,
        FCH_TREE_LEAF_BYTES * 2u + 1u,
        FCH_TREE_LEAF_BYTES * 3u,
        FCH_TREE_LEAF_BYTES * 4u,
        FCH_TREE_LEAF_BYTES * 5u,
        FCH_TREE_LEAF_BYTES * 8u,
        FCH_TREE_LEAF_BYTES * 9u + 17u
    };

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        if (!check_schedule_case(lengths[i]))
            return 0;
    }

    for (size_t leaves = 1u; leaves <= 17u; leaves++) {
        if (!check_schedule_case(leaves * FCH_TREE_LEAF_BYTES))
            return 0;
    }
    return 1;
}

typedef struct {
    size_t reads;
} counting_reader_t;

static int count_read(
    void *context,
    size_t offset,
    uint8_t *output,
    size_t length
) {
    counting_reader_t *reader = (counting_reader_t *)context;
    (void)offset;
    (void)output;
    (void)length;
    reader->reads++;
    return 0;
}

static int schedule_does_not_read_input(void) {
    counting_reader_t context = {0u};
    fch_reader_t reader = {count_read, &context};
    fch_block_t blocks[FCH_TREE_ARITY];
    size_t count = fch_fractal_split_reader(
        &reader,
        0u,
        FCH_TREE_LEAF_BYTES * 5u + 7u,
        0,
        blocks,
        FCH_TREE_ARITY
    );

    REQUIRE(count == FCH_TREE_ARITY, "reader schedule generation failed");
    REQUIRE(context.reads == 0u, "schedule read message contents");
    return 1;
}

static int prefix_subtrees_remain_stable(void) {
    static const size_t completed_prefixes[] = {1u, 2u, 4u, 8u};

    for (size_t i = 0;
         i < sizeof(completed_prefixes) / sizeof(completed_prefixes[0]);
         i++) {
        size_t prefix_leaves = completed_prefixes[i];
        size_t extended_leaves = prefix_leaves + 1u;
        fch_tree_position_t root;
        fch_tree_position_t children[FCH_TREE_ARITY];

        REQUIRE(fch_tree_position_for_range(
                    0u,
                    extended_leaves * FCH_TREE_LEAF_BYTES,
                    &root),
                "extended prefix position failed");
        REQUIRE(fch_tree_split_position(&root, children),
                "extended prefix split failed");
        REQUIRE(children[0].first_leaf == 0u &&
                children[0].leaf_count == prefix_leaves &&
                children[0].byte_offset == 0u &&
                children[0].byte_length ==
                    prefix_leaves * FCH_TREE_LEAF_BYTES,
                "completed prefix subtree moved after extension");
    }
    return 1;
}

static int boundary_diffusion(void) {
    static const size_t lengths[] = {
        FCH_TREE_LEAF_BYTES - 10u,
        FCH_TREE_LEAF_BYTES - 9u,
        FCH_TREE_LEAF_BYTES - 8u,
        FCH_TREE_LEAF_BYTES * 2u - 10u,
        FCH_TREE_LEAF_BYTES * 2u - 9u,
        FCH_TREE_LEAF_BYTES * 2u - 8u
    };
    uint8_t message[FCH_TREE_LEAF_BYTES * 2u];
    uint8_t changed[FCH_TREE_LEAF_BYTES * 2u];
    fill_pattern(message, sizeof(message), UINT32_C(0xA5A55A5A));

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        size_t length = lengths[i];
        uint8_t base256[32];
        uint8_t changed256[32];
        uint8_t base512[64];
        uint8_t changed512[64];

        memcpy(changed, message, length);
        changed[length / 2u] ^= (uint8_t)(1u << (i % 8u));
        REQUIRE(fch_hash_256_checked(message, length, base256) &&
                fch_hash_256_checked(changed, length, changed256) &&
                fch_hash_512_checked(message, length, base512) &&
                fch_hash_512_checked(changed, length, changed512),
                "boundary hash failed");
        REQUIRE(bit_diff(base256, changed256, sizeof(base256)) >= 64,
                "weak 256-bit diffusion at a leaf boundary");
        REQUIRE(bit_diff(base512, changed512, sizeof(base512)) >= 160,
                "weak 512-bit diffusion at a leaf boundary");
    }
    return 1;
}

int main(void) {
    if (!schedule_cases() ||
        !schedule_does_not_read_input() ||
        !prefix_subtrees_remain_stable() ||
        !boundary_diffusion())
        return 1;

    puts("PASS: canonical schedule is content-independent and prefix-stable");
    return 0;
}
