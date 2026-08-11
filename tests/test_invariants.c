#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fractal.h"
#include "leaf.h"
#include "combine.h"
#include "mix.h"
#include "params.h"

#define MAX_TEST_LENGTH 4096u

static void fill_pattern(uint8_t *data, size_t length, unsigned int pattern) {
    uint32_t state = 0x9E3779B9u ^ pattern;

    for (size_t i = 0; i < length; i++) {
        switch (pattern) {
            case 0:
                data[i] = 0;
                break;
            case 1:
                data[i] = 0xFF;
                break;
            case 2:
                data[i] = (uint8_t)i;
                break;
            default:
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                data[i] = (uint8_t)state;
                break;
        }
    }
}

static int check_split(const uint8_t *data, size_t length, int depth) {
    fch_block_t blocks[FCH_N_MAX];
    size_t count = fch_fractal_split(
        data,
        length,
        depth,
        blocks,
        FCH_N_MAX
    );

    if (count == 0 || count > FCH_N_MAX || count > length)
        return 0;

    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        if (blocks[i].offset != offset)
            return 0;
        if (blocks[i].length == 0)
            return 0;
        if (blocks[i].length > length - offset)
            return 0;
        offset += blocks[i].length;
    }

    return offset == length;
}

static int same_split(
    const fch_block_t *a,
    size_t a_count,
    const fch_block_t *b,
    size_t b_count
) {
    if (a_count != b_count)
        return 0;
    for (size_t i = 0; i < a_count; i++) {
        if (a[i].offset != b[i].offset || a[i].length != b[i].length)
            return 0;
    }
    return 1;
}

static int check_full_input_split_influence(void) {
    uint8_t base[512];
    uint8_t changed[512];
    fch_block_t base_blocks[FCH_N_MAX];

    fill_pattern(base, sizeof(base), 3);
    size_t base_count = fch_fractal_split(
        base,
        sizeof(base),
        3,
        base_blocks,
        FCH_N_MAX
    );
    if (base_count == 0)
        return 0;

    size_t changed_splits = 0;
    for (size_t position = 0; position < sizeof(base); position++) {
        fch_block_t blocks[FCH_N_MAX];
        memcpy(changed, base, sizeof(base));
        changed[position] ^= (uint8_t)(1u << (unsigned int)(position % 8u));

        size_t count = fch_fractal_split(
            changed,
            sizeof(changed),
            3,
            blocks,
            FCH_N_MAX
        );
        if (count == 0)
            return 0;
        if (!same_split(base_blocks, base_count, blocks, count))
            changed_splits++;
    }

    return changed_splits * 100u >= sizeof(base) * 99u;
}

static int check_split_derivation_properties(void) {
    enum {
        PREFIX_LENGTH = 37,
        SAMPLE_LENGTH = 1024,
        SAMPLE_COUNT = 256
    };
    uint8_t data[SAMPLE_LENGTH];
    uint8_t prefixed[PREFIX_LENGTH + SAMPLE_LENGTH];
    fch_block_t direct[FCH_N_MAX];
    fch_block_t repeated[FCH_N_MAX];
    fch_block_t relocated[FCH_N_MAX];

    fill_pattern(data, sizeof(data), 3);
    size_t direct_count = fch_fractal_split(
        data,
        sizeof(data),
        3,
        direct,
        FCH_N_MAX
    );
    size_t repeated_count = fch_fractal_split(
        data,
        sizeof(data),
        3,
        repeated,
        FCH_N_MAX
    );
    if (direct_count == 0 ||
        !same_split(direct, direct_count, repeated, repeated_count))
        return 0;

    memset(prefixed, 0xA5, PREFIX_LENGTH);
    memcpy(prefixed + PREFIX_LENGTH, data, sizeof(data));
    fch_memory_reader_t memory = { prefixed, sizeof(prefixed) };
    fch_reader_t reader = { fch_memory_read, &memory };
    size_t relocated_count = fch_fractal_split_reader(
        &reader,
        PREFIX_LENGTH,
        sizeof(data),
        3,
        relocated,
        FCH_N_MAX
    );
    if (!same_split(direct, direct_count, relocated, relocated_count))
        return 0;

    unsigned int seen_counts = 0;
    size_t depth_changes = 0;
    for (size_t sample = 0; sample < SAMPLE_COUNT; sample++) {
        fch_block_t blocks[FCH_N_MAX];
        fch_block_t deeper[FCH_N_MAX];

        for (size_t i = 0; i < sizeof(data); i++) {
            data[i] = (uint8_t)(
                i * 131u + sample * 17u + (i >> 3u) * (sample + 1u)
            );
        }
        data[(sample * 257u + 13u) % sizeof(data)] ^=
            (uint8_t)(sample | 1u);

        size_t count = fch_fractal_split(
            data,
            sizeof(data),
            3,
            blocks,
            FCH_N_MAX
        );
        size_t deeper_count = fch_fractal_split(
            data,
            sizeof(data),
            4,
            deeper,
            FCH_N_MAX
        );
        if (count < FCH_N_MIN || count > FCH_N_MAX ||
            deeper_count == 0)
            return 0;

        seen_counts |= 1u << (unsigned int)(count - FCH_N_MIN);
        if (!same_split(blocks, count, deeper, deeper_count))
            depth_changes++;

        size_t min_length = blocks[0].length;
        size_t max_length = blocks[0].length;
        for (size_t i = 1; i < count; i++) {
            if (blocks[i].length < min_length)
                min_length = blocks[i].length;
            if (blocks[i].length > max_length)
                max_length = blocks[i].length;
        }
        if (max_length > min_length * 2u + count)
            return 0;
    }

    unsigned int expected_counts =
        (1u << (unsigned int)(FCH_N_MAX - FCH_N_MIN + 1)) - 1u;
    return seen_counts == expected_counts &&
        depth_changes * 10u >= SAMPLE_COUNT * 9u;
}

static int check_leaf_domain_separation(void) {
    uint8_t data[64];
    uint64_t root_words[FCH_256_STATE_WORDS];
    uint64_t inner_words[FCH_256_STATE_WORDS];
    uint64_t shorter_words[FCH_256_STATE_WORDS];
    fch_state_t root = { root_words, FCH_256_STATE_WORDS };
    fch_state_t inner = { inner_words, FCH_256_STATE_WORDS };
    fch_state_t shorter = { shorter_words, FCH_256_STATE_WORDS };

    fill_pattern(data, sizeof(data), 2);
    fch_leaf_compress(data, sizeof(data), &root, 0);
    fch_leaf_compress(data, sizeof(data), &inner, 1);
    fch_leaf_compress(data, sizeof(data) - 1u, &shorter, 1);

    if (root.words != FCH_256_STATE_WORDS ||
        inner.words != FCH_256_STATE_WORDS ||
        shorter.words != FCH_256_STATE_WORDS)
        return 0;
    if (memcmp(root.state, inner.state, sizeof(root_words)) == 0)
        return 0;
    if (memcmp(inner.state, shorter.state, sizeof(inner_words)) == 0)
        return 0;

    return 1;
}

static int check_node_domain_separation(void) {
    uint64_t child_a_words[FCH_256_STATE_WORDS] = {
        UINT64_C(1), UINT64_C(2), UINT64_C(3), UINT64_C(4)
    };
    uint64_t child_b_words[FCH_256_STATE_WORDS] = {
        UINT64_C(5), UINT64_C(6), UINT64_C(7), UINT64_C(8)
    };
    uint64_t child_c_words[FCH_256_STATE_WORDS] = {
        UINT64_C(9), UINT64_C(10), UINT64_C(11), UINT64_C(12)
    };
    fch_state_t children[2] = {
        { child_a_words, FCH_256_STATE_WORDS },
        { child_b_words, FCH_256_STATE_WORDS }
    };
    fch_state_t reversed[2] = {
        { child_b_words, FCH_256_STATE_WORDS },
        { child_a_words, FCH_256_STATE_WORDS }
    };
    fch_state_t three_children[3] = {
        { child_a_words, FCH_256_STATE_WORDS },
        { child_b_words, FCH_256_STATE_WORDS },
        { child_c_words, FCH_256_STATE_WORDS }
    };
    const fch_block_t balanced[2] = { { 0, 32 }, { 32, 32 } };
    const fch_block_t uneven[2] = { { 0, 31 }, { 31, 33 } };
    const fch_block_t three_blocks[3] = {
        { 0, 16 }, { 16, 16 }, { 32, 32 }
    };

    fch_state_t a = fch_combine(
        children, balanced, 2, 64, FCH_256_STATE_WORDS, 1
    );
    fch_state_t b = fch_combine(
        children, uneven, 2, 64, FCH_256_STATE_WORDS, 1
    );
    fch_state_t c = fch_combine(
        reversed, balanced, 2, 64, FCH_256_STATE_WORDS, 1
    );
    fch_state_t root = fch_combine(
        children, balanced, 2, 64, FCH_256_STATE_WORDS, 0
    );
    fch_state_t deeper = fch_combine(
        children, balanced, 2, 64, FCH_256_STATE_WORDS, 2
    );
    fch_state_t wider = fch_combine(
        three_children, three_blocks, 3, 64, FCH_256_STATE_WORDS, 1
    );

    int ok = a.state && b.state && c.state && root.state &&
        deeper.state && wider.state;
    if (ok && memcmp(a.state, b.state, FCH_256_STATE_WORDS * sizeof(uint64_t)) == 0)
        ok = 0;
    if (ok && memcmp(a.state, c.state, FCH_256_STATE_WORDS * sizeof(uint64_t)) == 0)
        ok = 0;
    if (ok && memcmp(a.state, root.state, FCH_256_STATE_WORDS * sizeof(uint64_t)) == 0)
        ok = 0;
    if (ok && memcmp(a.state, deeper.state, FCH_256_STATE_WORDS * sizeof(uint64_t)) == 0)
        ok = 0;
    if (ok && memcmp(a.state, wider.state, FCH_256_STATE_WORDS * sizeof(uint64_t)) == 0)
        ok = 0;

    free(a.state);
    free(b.state);
    free(c.state);
    free(root.state);
    free(deeper.state);
    free(wider.state);
    return ok;
}

static int check_tree_encoding_validation(void) {
    uint8_t data[64] = {0};
    uint64_t child_a_words[FCH_256_STATE_WORDS] = {0};
    uint64_t child_b_words[FCH_256_STATE_WORDS] = {0};
    fch_state_t children[2] = {
        { child_a_words, FCH_256_STATE_WORDS },
        { child_b_words, FCH_256_STATE_WORDS }
    };
    const fch_block_t valid[2] = { { 0, 32 }, { 32, 32 } };
    const fch_block_t gap[2] = { { 0, 32 }, { 33, 31 } };

    fch_state_t one_child = fch_combine(
        children, valid, 1, 64, FCH_256_STATE_WORDS, 1
    );
    fch_state_t negative_depth = fch_combine(
        children, valid, 2, 64, FCH_256_STATE_WORDS, -1
    );
    fch_state_t noncanonical = fch_combine(
        children, gap, 2, 64, FCH_256_STATE_WORDS, 1
    );

    int ok = !one_child.state && !negative_depth.state &&
        !noncanonical.state;

    uint64_t leaf_words[FCH_256_STATE_WORDS] = {0};
    fch_state_t leaf = { leaf_words, FCH_256_STATE_WORDS };
    uint64_t narrow_words[4] = {0};
    fch_state_t narrow = { narrow_words, 4 };
    fch_memory_reader_t memory = { data, sizeof(data) };
    fch_reader_t reader = { fch_memory_read, &memory };
    if (fch_leaf_compress_reader(&reader, 0, sizeof(data), &leaf, -1))
        ok = 0;
    if (fch_leaf_compress_reader(&reader, 0, sizeof(data), &narrow, 0))
        ok = 0;

    fch_state_t wrong_width = fch_process(data, sizeof(data), 0, 4);
    if (wrong_width.state)
        ok = 0;

    fch_block_t blocks[FCH_N_MAX];
    if (fch_fractal_split(data, sizeof(data), -1, blocks, FCH_N_MAX) != 0)
        ok = 0;

    if (FCH_TREE_ENCODING_VERSION == 0 ||
        FCH_SPLIT_DERIVATION_VERSION == 0 ||
        FCH_TREE_TAG_LEAF_HEADER == FCH_TREE_TAG_NODE_HEADER ||
        FCH_TREE_TAG_LEAF_DATA == FCH_TREE_TAG_NODE_CHILD ||
        FCH_SPLIT_TAG_HEADER == FCH_SPLIT_TAG_DATA ||
        FCH_SPLIT_TAG_DATA == FCH_SPLIT_TAG_OUTPUT ||
        FCH_MIX_FLAG_SPLIT_HEADER == FCH_MIX_FLAG_SPLIT_DATA ||
        FCH_MIX_FLAG_SPLIT_DATA == FCH_MIX_FLAG_SPLIT_OUTPUT)
        ok = 0;

    free(one_child.state);
    free(negative_depth.state);
    free(noncanonical.state);
    free(wrong_width.state);
    return ok;
}

int main(void) {
    uint8_t data[MAX_TEST_LENGTH];

    for (unsigned int pattern = 0; pattern < 4; pattern++) {
        fill_pattern(data, sizeof(data), pattern);

        for (size_t length = 1; length <= sizeof(data); length++) {
            for (int depth = 0; depth < FCH_MAX_DEPTH_CAP; depth++) {
                if (!check_split(data, length, depth)) {
                    printf(
                        "FAIL: split invariant (pattern=%u length=%u depth=%d)\n",
                        pattern,
                        (unsigned int)length,
                        depth
                    );
                    return 1;
                }
            }
        }
    }

    if (!check_full_input_split_influence()) {
        printf("FAIL: split ignores too much of the input\n");
        return 1;
    }
    if (!check_split_derivation_properties()) {
        printf("FAIL: hardened split derivation properties\n");
        return 1;
    }
    if (!check_leaf_domain_separation()) {
        printf("FAIL: leaf domain separation\n");
        return 1;
    }
    if (!check_node_domain_separation()) {
        printf("FAIL: node domain separation\n");
        return 1;
    }
    if (!check_tree_encoding_validation()) {
        printf("FAIL: canonical tree encoding validation\n");
        return 1;
    }

    printf(
        "PASS: split derivation, invariants, domains, and canonical tree encoding\n"
    );
    return 0;
}
