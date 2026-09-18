#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fractal.h"
#include "leaf.h"
#include "params.h"

static void fill_pattern(uint8_t *buffer, size_t length) {
    for (size_t i = 0; i < length; i++)
        buffer[i] = (uint8_t)(i * 131u + (i >> 3u) + 17u);
}

static int check_leaf_boundaries(void) {
    const size_t lengths[] = {
        FCH_TREE_LEAF_BYTES - 1u,
        FCH_TREE_LEAF_BYTES,
        FCH_TREE_LEAF_BYTES + 1u
    };
    const size_t expected_leaf_counts[] = {1u, 1u, 2u};
    uint8_t *input = (uint8_t *)malloc(FCH_TREE_LEAF_BYTES + 1u);
    uint8_t digest256[3][32];
    uint8_t digest512[3][64];

    if (!input) {
        printf("FAIL: leaf-boundary allocation failed\n");
        return 0;
    }
    fill_pattern(input, FCH_TREE_LEAF_BYTES + 1u);

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        if (fch_tree_leaf_count_for_length(lengths[i]) !=
            expected_leaf_counts[i]) {
            printf("FAIL: leaf count mismatch at %zu bytes\n", lengths[i]);
            free(input);
            return 0;
        }
        if (!fch_hash_256_checked(input, lengths[i], digest256[i]) ||
            !fch_hash_512_checked(input, lengths[i], digest512[i])) {
            printf("FAIL: hashing failed at %zu bytes\n", lengths[i]);
            free(input);
            return 0;
        }
    }

    for (size_t i = 1; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        if (memcmp(digest256[i - 1u], digest256[i], 32u) == 0 ||
            memcmp(digest512[i - 1u], digest512[i], 64u) == 0) {
            printf("FAIL: adjacent leaf-boundary digests match\n");
            free(input);
            return 0;
        }
    }

    fch_memory_reader_t memory = {
        input,
        FCH_TREE_LEAF_BYTES + 1u
    };
    fch_reader_t reader = {fch_memory_read, &memory};
    uint64_t state_words[FCH_INTERNAL_STATE_WORDS];

    for (size_t i = 0; i < 2u; i++) {
        memset(state_words, 0, sizeof(state_words));
        fch_state_t state = {
            state_words,
            FCH_INTERNAL_STATE_WORDS,
            {0, 0, 0, 0, 0}
        };
        if (!fch_leaf_compress_reader(
                &reader,
                0u,
                lengths[i],
                &state,
                0
            ) ||
            state.tree.level != 0u ||
            state.tree.first_leaf != 0u ||
            state.tree.leaf_count != 1u ||
            state.tree.byte_offset != 0u ||
            state.tree.byte_length != lengths[i]) {
            printf("FAIL: leaf compressor rejected %zu bytes\n", lengths[i]);
            free(input);
            return 0;
        }
    }

    memset(state_words, 0, sizeof(state_words));
    fch_state_t oversized = {
        state_words,
        FCH_INTERNAL_STATE_WORDS,
        {0, 0, 0, 0, 0}
    };
    if (fch_leaf_compress_reader(
            &reader,
            0u,
            FCH_TREE_LEAF_BYTES + 1u,
            &oversized,
            0
        )) {
        printf("FAIL: leaf compressor accepted an oversized leaf\n");
        free(input);
        return 0;
    }

    free(input);
    return 1;
}

int main(void) {
    uint8_t out[32];

    if (!fch_hash_256_checked(NULL, 0, out)) {
        printf("FAIL: empty input rejected\n");
        return 1;
    }

    memset(out, 0xA5, sizeof(out));
    if (fch_hash_256_checked(NULL, 1, out)) {
        printf("FAIL: invalid input accepted\n");
        return 1;
    }
    for (size_t i = 0; i < sizeof(out); i++) {
        if (out[i] != 0) {
            printf("FAIL: failure output not cleared\n");
            return 1;
        }
    }

    uint8_t buf[64];
    memset(buf, 0x11, 64);
    if (!fch_hash_256_checked(buf, 64, out)) {
        printf("FAIL: 64-byte input rejected\n");
        return 1;
    }

    size_t big = 1 << 20;
    uint8_t *large = (uint8_t *)malloc(big);
    if (!large) {
        printf("FAIL: malloc failed\n");
        return 1;
    }
    memset(large, 0x22, big);
    if (!fch_hash_256_checked(large, big, out)) {
        printf("FAIL: large input rejected\n");
        free(large);
        return 1;
    }
    free(large);

    if (!check_leaf_boundaries())
        return 1;

    printf("PASS: boundary inputs handled\n");
    return 0;
}
