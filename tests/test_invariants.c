#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "combine.h"
#include "fch.h"
#include "fractal.h"
#include "leaf.h"
#include "mix.h"
#include "params.h"

#define REQUIRE(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", (message)); \
            return 0; \
        } \
    } while (0)

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

static int check_position_recursive(const fch_tree_position_t *position) {
    REQUIRE(fch_tree_position_valid(position), "invalid tree position");

    if (position->leaf_count == 1u) {
        REQUIRE(position->level == 0u, "leaf level is not zero");
        REQUIRE(
            position->byte_length <= FCH_TREE_LEAF_BYTES,
            "leaf exceeds fixed span"
        );
        return 1;
    }

    fch_tree_position_t children[FCH_TREE_ARITY];
    REQUIRE(
        fch_tree_split_position(position, children),
        "internal position did not split"
    );
    REQUIRE(
        children[0].first_leaf + children[0].leaf_count ==
            children[1].first_leaf,
        "child leaf ranges are not adjacent"
    );
    REQUIRE(
        children[0].byte_offset + children[0].byte_length ==
            children[1].byte_offset,
        "child byte ranges are not adjacent"
    );
    REQUIRE(
        children[0].byte_length + children[1].byte_length ==
            position->byte_length,
        "children do not cover parent"
    );
    REQUIRE(
        check_position_recursive(&children[0]) &&
            check_position_recursive(&children[1]),
        "descendant position is invalid"
    );
    return 1;
}

static int check_canonical_schedule(void) {
    static const size_t lengths[] = {
        1u, 63u, 64u, 1023u, 1024u, 1025u,
        2048u, 2049u, 3072u, 3073u, 4096u,
        4097u, 8193u, 16385u
    };

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        fch_tree_position_t root;
        REQUIRE(
            fch_tree_position_for_range(0, lengths[i], &root),
            "root position creation failed"
        );
        REQUIRE(
            root.leaf_count ==
                1u + (lengths[i] - 1u) / FCH_TREE_LEAF_BYTES,
            "leaf count mismatch"
        );
        REQUIRE(
            root.level == fch_tree_level_for_leaves(root.leaf_count),
            "tree level mismatch"
        );
        REQUIRE(check_position_recursive(&root), "tree schedule failed");
    }

    fch_tree_position_t invalid;
    REQUIRE(
        !fch_tree_position_for_range(1u, 64u, &invalid),
        "unaligned range accepted"
    );
    REQUIRE(
        !fch_tree_position_for_range(0u, 0u, &invalid),
        "empty tree range accepted"
    );
    return 1;
}

typedef struct {
    unsigned int calls;
} rejecting_reader_t;

static int reject_read(
    void *opaque,
    size_t offset,
    uint8_t *output,
    size_t length
) {
    rejecting_reader_t *reader = (rejecting_reader_t *)opaque;
    (void)offset;
    (void)output;
    (void)length;
    reader->calls++;
    return 0;
}

static int check_content_independence(void) {
    enum { LENGTH = 8193 };
    uint8_t zeros[LENGTH] = {0};
    uint8_t pattern[LENGTH];
    for (size_t i = 0; i < sizeof(pattern); i++)
        pattern[i] = (uint8_t)(i * 73u + (i >> 3u));

    fch_block_t a[FCH_TREE_ARITY];
    fch_block_t b[FCH_TREE_ARITY];
    fch_block_t c[FCH_TREE_ARITY];
    size_t count_a = fch_fractal_split(
        zeros, sizeof(zeros), 0, a, FCH_TREE_ARITY
    );
    size_t count_b = fch_fractal_split(
        pattern, sizeof(pattern), 0, b, FCH_TREE_ARITY
    );
    size_t count_c = fch_fractal_split(
        pattern, sizeof(pattern), 11, c, FCH_TREE_ARITY
    );

    REQUIRE(count_a == FCH_TREE_ARITY, "binary split count mismatch");
    REQUIRE(count_b == count_a && count_c == count_a, "split count changed");
    REQUIRE(same_blocks(a, b, count_a), "message changed tree shape");
    REQUIRE(same_blocks(a, c, count_a), "depth changed tree shape");

    rejecting_reader_t context = {0};
    fch_reader_t reader = { reject_read, &context };
    fch_block_t reader_blocks[FCH_TREE_ARITY];
    REQUIRE(
        fch_fractal_split_reader(
            &reader,
            0,
            sizeof(pattern),
            0,
            reader_blocks,
            FCH_TREE_ARITY
        ) == FCH_TREE_ARITY,
        "reader split failed"
    );
    REQUIRE(context.calls == 0u, "tree schedule read message bytes");
    REQUIRE(
        same_blocks(a, reader_blocks, count_a),
        "reader split differs from memory split"
    );
    return 1;
}

static int check_leaf_position_binding(void) {
    uint8_t storage[FCH_TREE_LEAF_BYTES * 2u];
    for (size_t i = 0; i < FCH_TREE_LEAF_BYTES; i++) {
        uint8_t value = (uint8_t)(i * 29u + 7u);
        storage[i] = value;
        storage[FCH_TREE_LEAF_BYTES + i] = value;
    }

    fch_memory_reader_t memory = { storage, sizeof(storage) };
    fch_reader_t reader = { fch_memory_read, &memory };
    uint64_t words_a[FCH_INTERNAL_STATE_WORDS] = {0};
    uint64_t words_b[FCH_INTERNAL_STATE_WORDS] = {0};
    uint64_t words_c[FCH_INTERNAL_STATE_WORDS] = {0};
    fch_state_t a = {
        words_a, FCH_INTERNAL_STATE_WORDS, { 0, 0, 0, 0, 0 }
    };
    fch_state_t b = {
        words_b, FCH_INTERNAL_STATE_WORDS, { 0, 0, 0, 0, 0 }
    };
    fch_state_t c = {
        words_c, FCH_INTERNAL_STATE_WORDS, { 0, 0, 0, 0, 0 }
    };

    REQUIRE(
        fch_leaf_compress_reader(
            &reader, 0, FCH_TREE_LEAF_BYTES, &a, 0
        ),
        "first leaf compression failed"
    );
    REQUIRE(
        fch_leaf_compress_reader(
            &reader, 0, FCH_TREE_LEAF_BYTES, &b, 9
        ),
        "depth-independent leaf compression failed"
    );
    REQUIRE(
        fch_leaf_compress_reader(
            &reader,
            FCH_TREE_LEAF_BYTES,
            FCH_TREE_LEAF_BYTES,
            &c,
            0
        ),
        "relocated leaf compression failed"
    );

    REQUIRE(
        memcmp(words_a, words_b, sizeof(words_a)) == 0,
        "obsolete root depth changed leaf state"
    );
    REQUIRE(
        memcmp(words_a, words_c, sizeof(words_a)) != 0,
        "leaf position was not bound"
    );
    REQUIRE(a.tree.first_leaf == 0u && c.tree.first_leaf == 1u,
        "leaf descriptor index mismatch");
    return 1;
}

static int check_combine_validation(void) {
    uint8_t data[FCH_TREE_LEAF_BYTES * 2u];
    for (size_t i = 0; i < sizeof(data); i++)
        data[i] = (uint8_t)(i * 17u + 3u);

    fch_memory_reader_t memory = { data, sizeof(data) };
    fch_reader_t reader = { fch_memory_read, &memory };
    uint64_t left_words[FCH_INTERNAL_STATE_WORDS] = {0};
    uint64_t right_words[FCH_INTERNAL_STATE_WORDS] = {0};
    fch_state_t children[FCH_TREE_ARITY] = {
        { left_words, FCH_INTERNAL_STATE_WORDS, { 0, 0, 0, 0, 0 } },
        { right_words, FCH_INTERNAL_STATE_WORDS, { 0, 0, 0, 0, 0 } }
    };

    REQUIRE(
        fch_leaf_compress_reader(
            &reader, 0, FCH_TREE_LEAF_BYTES, &children[0], 0
        ),
        "left child compression failed"
    );
    REQUIRE(
        fch_leaf_compress_reader(
            &reader,
            FCH_TREE_LEAF_BYTES,
            FCH_TREE_LEAF_BYTES,
            &children[1],
            0
        ),
        "right child compression failed"
    );

    fch_block_t blocks[FCH_TREE_ARITY] = {
        { 0, FCH_TREE_LEAF_BYTES },
        { FCH_TREE_LEAF_BYTES, FCH_TREE_LEAF_BYTES }
    };
    fch_state_t parent = fch_combine(
        children,
        blocks,
        FCH_TREE_ARITY,
        sizeof(data),
        FCH_INTERNAL_STATE_WORDS,
        0
    );
    REQUIRE(parent.state != NULL, "canonical children were rejected");
    REQUIRE(parent.tree.level == 1u && parent.tree.leaf_count == 2u,
        "parent descriptor mismatch");

    fch_state_t swapped[FCH_TREE_ARITY] = { children[1], children[0] };
    fch_state_t rejected_swap = fch_combine(
        swapped,
        blocks,
        FCH_TREE_ARITY,
        sizeof(data),
        FCH_INTERNAL_STATE_WORDS,
        0
    );
    REQUIRE(!rejected_swap.state, "reordered children were accepted");

    fch_block_t wrong_blocks[FCH_TREE_ARITY] = {
        { 0, FCH_TREE_LEAF_BYTES - 1u },
        { FCH_TREE_LEAF_BYTES - 1u, FCH_TREE_LEAF_BYTES + 1u }
    };
    fch_state_t rejected_layout = fch_combine(
        children,
        wrong_blocks,
        FCH_TREE_ARITY,
        sizeof(data),
        FCH_INTERNAL_STATE_WORDS,
        0
    );
    REQUIRE(!rejected_layout.state, "non-canonical blocks were accepted");

    free(parent.state);
    free(rejected_swap.state);
    free(rejected_layout.state);
    return 1;
}

static int values_are_distinct(const uint64_t *values, size_t count) {
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1u; j < count; j++) {
            if (values[i] == values[j])
                return 0;
        }
    }
    return 1;
}

static int check_encoding_constants(void) {
    const uint64_t tags[] = {
        FCH_TREE_TAG_LEAF_HEADER,
        FCH_TREE_TAG_LEAF_DATA,
        FCH_TREE_TAG_NODE_HEADER,
        FCH_TREE_TAG_NODE_CHILD,
        FCH_TREE_TAG_OUTPUT
    };
    const uint64_t domains[] = {
        FCH_DOMAIN_LEAF,
        FCH_DOMAIN_NODE,
        FCH_DOMAIN_OUTPUT_256,
        FCH_DOMAIN_OUTPUT_512
    };

    REQUIRE(FCH_TREE_ENCODING_VERSION == 2u, "wrong tree version");
    REQUIRE(FCH_PADDING_FORMAT_VERSION == 1u, "wrong padding version");
    REQUIRE(FCH_TREE_ARITY == 2u, "tree is not binary");
    REQUIRE(values_are_distinct(tags, sizeof(tags) / sizeof(tags[0])),
        "record tags overlap");
    REQUIRE(values_are_distinct(domains, sizeof(domains) / sizeof(domains[0])),
        "domains overlap");
    return 1;
}

int main(void) {
    if (!check_canonical_schedule() ||
        !check_content_independence() ||
        !check_leaf_position_binding() ||
        !check_combine_validation() ||
        !check_encoding_constants())
        return 1;

    printf(
        "PASS: canonical binary schedule, positions, domains, and validation\n"
    );
    return 0;
}
