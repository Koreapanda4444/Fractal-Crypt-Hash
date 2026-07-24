#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fractal.h"
#include "leaf.h"
#include "combine.h"
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

    return changed_splits * 10u >= sizeof(base) * 9u;
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
    fch_state_t children[2] = {
        { child_a_words, FCH_256_STATE_WORDS },
        { child_b_words, FCH_256_STATE_WORDS }
    };
    fch_state_t reversed[2] = {
        { child_b_words, FCH_256_STATE_WORDS },
        { child_a_words, FCH_256_STATE_WORDS }
    };
    const fch_block_t balanced[2] = { { 0, 32 }, { 32, 32 } };
    const fch_block_t uneven[2] = { { 0, 31 }, { 31, 33 } };

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

    int ok = a.state && b.state && c.state && root.state;
    if (ok && memcmp(a.state, b.state, FCH_256_STATE_WORDS * sizeof(uint64_t)) == 0)
        ok = 0;
    if (ok && memcmp(a.state, c.state, FCH_256_STATE_WORDS * sizeof(uint64_t)) == 0)
        ok = 0;
    if (ok && memcmp(a.state, root.state, FCH_256_STATE_WORDS * sizeof(uint64_t)) == 0)
        ok = 0;

    free(a.state);
    free(b.state);
    free(c.state);
    free(root.state);
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
    if (!check_leaf_domain_separation()) {
        printf("FAIL: leaf domain separation\n");
        return 1;
    }
    if (!check_node_domain_separation()) {
        printf("FAIL: node domain separation\n");
        return 1;
    }

    printf("PASS: split invariants and domain separation\n");
    return 0;
}
