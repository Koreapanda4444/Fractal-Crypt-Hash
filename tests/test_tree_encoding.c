#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitops.h"
#include "combine.h"
#include "leaf.h"
#include "mix.h"
#include "params.h"

enum {
    MAX_CAPTURE_RECORDS = 4
};

#define FCH_DOMAIN_ROOT_LEAF UINT64_C(0x524F4F544C454146)
#define FCH_DOMAIN_INNER_LEAF UINT64_C(0x494E544C45414631)
#define FCH_DOMAIN_ROOT_NODE UINT64_C(0x524F4F544E4F4445)
#define FCH_DOMAIN_INNER_NODE UINT64_C(0x494E544E4F444531)

typedef struct {
    uint8_t block[FCH_MIX_BLOCK_SIZE];
    size_t block_length;
    uint64_t counter;
    uint64_t domain;
    uint64_t flags;
} captured_record_t;

static captured_record_t g_records[MAX_CAPTURE_RECORDS];
static size_t g_record_count;
static size_t g_init_count;
static size_t g_init_state_words;
static uint64_t g_init_domain;

#define REQUIRE(_condition, _message) \
    do { \
        if (!(_condition)) { \
            fprintf(stderr, "FAIL: %s\n", (_message)); \
            return 0; \
        } \
    } while (0)

static void reset_capture(void) {
    memset(g_records, 0, sizeof(g_records));
    g_record_count = 0;
    g_init_count = 0;
    g_init_state_words = 0;
    g_init_domain = 0;
}

/*
 * Test doubles capture the exact records emitted by leaf.c and combine.c.
 * This checks the encoder before the permutation can hide field mistakes.
 */
int fch_mix_init(
    uint64_t *state,
    size_t state_words,
    uint64_t domain
) {
    if (!state || state_words != FCH_INTERNAL_STATE_WORDS)
        return 0;

    memset(state, 0, state_words * sizeof(uint64_t));
    g_init_count++;
    g_init_state_words = state_words;
    g_init_domain = domain;
    return 1;
}

int fch_mix_compress(
    uint64_t *state,
    size_t state_words,
    const uint8_t block[FCH_MIX_BLOCK_SIZE],
    size_t block_length,
    uint64_t counter,
    uint64_t domain,
    uint64_t flags
) {
    if (!state || state_words != FCH_INTERNAL_STATE_WORDS || !block ||
        block_length > FCH_MIX_BLOCK_SIZE ||
        g_record_count >= MAX_CAPTURE_RECORDS)
        return 0;

    captured_record_t *record = &g_records[g_record_count++];
    memcpy(record->block, block, sizeof(record->block));
    record->block_length = block_length;
    record->counter = counter;
    record->domain = domain;
    record->flags = flags;
    return 1;
}

static uint64_t record_word(
    const captured_record_t *record,
    size_t index
) {
    return fch_load_le64(record->block + index * 8u);
}

static int bytes_are_zero(
    const uint8_t *data,
    size_t offset,
    size_t length
) {
    for (size_t i = offset; i < length; i++) {
        if (data[i] != 0u)
            return 0;
    }
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

static int tag_matches(uint64_t tag, const char expected[9]) {
    uint8_t encoded[8];
    fch_store_le64(encoded, tag);
    return memcmp(encoded, expected, sizeof(encoded)) == 0;
}

static int check_namespaces(void) {
    const uint64_t tags[] = {
        FCH_TREE_TAG_LEAF_HEADER,
        FCH_TREE_TAG_LEAF_DATA,
        FCH_TREE_TAG_NODE_HEADER,
        FCH_TREE_TAG_NODE_CHILD,
        FCH_TREE_TAG_OUTPUT,
        FCH_SPLIT_TAG_HEADER,
        FCH_SPLIT_TAG_DATA,
        FCH_SPLIT_TAG_OUTPUT
    };
    const uint64_t domains[] = {
        FCH_DOMAIN_ROOT_LEAF,
        FCH_DOMAIN_INNER_LEAF,
        FCH_DOMAIN_ROOT_NODE,
        FCH_DOMAIN_INNER_NODE,
        FCH_SPLIT_DOMAIN
    };
    const uint64_t flags[] = {
        FCH_MIX_FLAG_LEAF_HEADER,
        FCH_MIX_FLAG_LEAF_DATA,
        FCH_MIX_FLAG_NODE_HEADER,
        FCH_MIX_FLAG_NODE_CHILD,
        FCH_MIX_FLAG_OUTPUT,
        FCH_MIX_FLAG_SPLIT_HEADER,
        FCH_MIX_FLAG_SPLIT_DATA,
        FCH_MIX_FLAG_SPLIT_OUTPUT
    };
    static const char *const tag_text[] = {
        "FCHLEAF1",
        "FCHLDAT1",
        "FCHNODE1",
        "FCHCHLD1",
        "FCHOUT01",
        "FCHSPH01",
        "FCHSPD01",
        "FCHSPO01"
    };

    REQUIRE(
        values_are_distinct(tags, sizeof(tags) / sizeof(tags[0])),
        "record tags are not distinct"
    );
    REQUIRE(
        values_are_distinct(domains, sizeof(domains) / sizeof(domains[0])),
        "tree domains are not distinct"
    );
    REQUIRE(
        values_are_distinct(flags, sizeof(flags) / sizeof(flags[0])),
        "record flags are not distinct"
    );

    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        REQUIRE(tag_matches(tags[i], tag_text[i]), "record tag bytes changed");
    }

    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        REQUIRE(flags[i] != 0u, "record flag is zero");
        REQUIRE(
            (flags[i] & (flags[i] - 1u)) == 0u,
            "record flag is not a single role bit"
        );
        REQUIRE(
            (flags[i] & FCH_MIX_FLAG_FINAL) == 0u,
            "record role flag overlaps the final bit"
        );
    }

    REQUIRE(
        FCH_MIX_FLAG_FINAL == UINT64_C(0x8000000000000000),
        "final flag bit changed"
    );
    REQUIRE(
        FCH_TREE_ENCODING_VERSION == UINT64_C(1),
        "tree encoding version changed"
    );
    return 1;
}

static int check_record_tweaks(
    const captured_record_t *record,
    size_t block_length,
    uint64_t counter,
    uint64_t domain,
    uint64_t flags,
    const char *message
) {
    if (record->block_length != block_length ||
        record->counter != counter ||
        record->domain != domain ||
        record->flags != flags) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static int check_leaf_header(
    const captured_record_t *record,
    uint64_t domain,
    size_t length,
    unsigned int depth
) {
    const uint64_t expected[] = {
        FCH_TREE_TAG_LEAF_HEADER,
        FCH_TREE_ENCODING_VERSION,
        domain,
        (uint64_t)length,
        (uint64_t)depth,
        FCH_INTERNAL_STATE_WORDS,
        FCH_MIN_BLOCK_SIZE,
        FCH_MAX_DEPTH_CAP,
        FCH_N_MIN,
        FCH_N_MAX,
        FCH_MIX_BLOCK_SIZE,
        FCH_MIX_ROUNDS,
        FCH_SPLIT_WEIGHT_MIN,
        FCH_SPLIT_WEIGHT_MAX,
        FCH_SPLIT_DERIVATION_VERSION,
        0u
    };

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        if (record_word(record, i) != expected[i]) {
            fprintf(
                stderr,
                "FAIL: leaf header word %u\n",
                (unsigned int)i
            );
            return 0;
        }
    }

    return check_record_tweaks(
        record,
        FCH_MIX_BLOCK_SIZE,
        0u,
        domain,
        FCH_MIX_FLAG_LEAF_HEADER,
        "leaf header tweaks"
    );
}

static int capture_leaf(
    const uint8_t *data,
    size_t length,
    int depth
) {
    uint64_t state_words[FCH_INTERNAL_STATE_WORDS] = {0};
    fch_state_t state = { state_words, FCH_INTERNAL_STATE_WORDS };
    fch_memory_reader_t memory = { data, length };
    fch_reader_t reader = { fch_memory_read, &memory };

    reset_capture();
    return fch_leaf_compress_reader(
        &reader,
        0u,
        length,
        &state,
        depth
    );
}

static int check_leaf_records(void) {
    uint8_t short_data[13];
    for (size_t i = 0; i < sizeof(short_data); i++)
        short_data[i] = (uint8_t)(i * 17u + 3u);

    REQUIRE(
        capture_leaf(short_data, sizeof(short_data), 0),
        "root leaf capture failed"
    );
    REQUIRE(g_init_count == 1u, "root leaf initialization count");
    REQUIRE(
        g_init_state_words == FCH_INTERNAL_STATE_WORDS,
        "root leaf state width"
    );
    REQUIRE(g_init_domain == FCH_DOMAIN_ROOT_LEAF, "root leaf domain");
    REQUIRE(g_record_count == 2u, "root leaf record count");
    REQUIRE(
        check_leaf_header(
            &g_records[0],
            FCH_DOMAIN_ROOT_LEAF,
            sizeof(short_data),
            0u
        ),
        "root leaf header"
    );
    REQUIRE(
        record_word(&g_records[1], 0u) == FCH_TREE_TAG_LEAF_DATA,
        "root leaf data tag"
    );
    REQUIRE(
        memcmp(g_records[1].block + 8u, short_data, sizeof(short_data)) == 0,
        "root leaf data bytes"
    );
    REQUIRE(
        bytes_are_zero(
            g_records[1].block,
            8u + sizeof(short_data),
            FCH_MIX_BLOCK_SIZE
        ),
        "root leaf data padding"
    );
    REQUIRE(
        check_record_tweaks(
            &g_records[1],
            8u + sizeof(short_data),
            sizeof(short_data),
            FCH_DOMAIN_ROOT_LEAF,
            FCH_MIX_FLAG_LEAF_DATA | FCH_MIX_FLAG_FINAL,
            "root leaf data tweaks"
        ),
        "root leaf data record"
    );

    uint8_t long_data[121];
    for (size_t i = 0; i < sizeof(long_data); i++)
        long_data[i] = (uint8_t)(i * 29u + 11u);

    REQUIRE(
        capture_leaf(long_data, sizeof(long_data), 3),
        "internal leaf capture failed"
    );
    REQUIRE(g_init_count == 1u, "internal leaf initialization count");
    REQUIRE(g_init_domain == FCH_DOMAIN_INNER_LEAF, "internal leaf domain");
    REQUIRE(g_record_count == 3u, "internal leaf record count");
    REQUIRE(
        check_leaf_header(
            &g_records[0],
            FCH_DOMAIN_INNER_LEAF,
            sizeof(long_data),
            3u
        ),
        "internal leaf header"
    );

    REQUIRE(
        record_word(&g_records[1], 0u) == FCH_TREE_TAG_LEAF_DATA,
        "first leaf chunk tag"
    );
    REQUIRE(
        memcmp(g_records[1].block + 8u, long_data, 120u) == 0,
        "first leaf chunk bytes"
    );
    REQUIRE(
        check_record_tweaks(
            &g_records[1],
            FCH_MIX_BLOCK_SIZE,
            120u,
            FCH_DOMAIN_INNER_LEAF,
            FCH_MIX_FLAG_LEAF_DATA,
            "first leaf chunk position"
        ),
        "first leaf chunk"
    );

    REQUIRE(
        record_word(&g_records[2], 0u) == FCH_TREE_TAG_LEAF_DATA,
        "final leaf chunk tag"
    );
    REQUIRE(g_records[2].block[8] == long_data[120], "final leaf byte");
    REQUIRE(
        bytes_are_zero(g_records[2].block, 9u, FCH_MIX_BLOCK_SIZE),
        "final leaf chunk padding"
    );
    REQUIRE(
        check_record_tweaks(
            &g_records[2],
            9u,
            121u,
            FCH_DOMAIN_INNER_LEAF,
            FCH_MIX_FLAG_LEAF_DATA | FCH_MIX_FLAG_FINAL,
            "final leaf chunk position"
        ),
        "final leaf chunk"
    );

    return 1;
}

static int check_node_header(
    const captured_record_t *record,
    uint64_t domain,
    size_t node_length,
    unsigned int depth,
    size_t child_count
) {
    const uint64_t expected[] = {
        FCH_TREE_TAG_NODE_HEADER,
        FCH_TREE_ENCODING_VERSION,
        domain,
        (uint64_t)node_length,
        (uint64_t)depth,
        (uint64_t)child_count,
        FCH_INTERNAL_STATE_WORDS,
        FCH_MIN_BLOCK_SIZE,
        FCH_MAX_DEPTH_CAP,
        FCH_N_MIN,
        FCH_N_MAX,
        FCH_MIX_BLOCK_SIZE,
        FCH_MIX_ROUNDS,
        FCH_SPLIT_WEIGHT_MIN,
        FCH_SPLIT_WEIGHT_MAX,
        FCH_SPLIT_DERIVATION_VERSION
    };

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        if (record_word(record, i) != expected[i]) {
            fprintf(
                stderr,
                "FAIL: node header word %u\n",
                (unsigned int)i
            );
            return 0;
        }
    }

    return check_record_tweaks(
        record,
        FCH_MIX_BLOCK_SIZE,
        0u,
        domain,
        FCH_MIX_FLAG_NODE_HEADER,
        "node header tweaks"
    );
}

static int check_child_record(
    const captured_record_t *record,
    const uint64_t child_state[FCH_INTERNAL_STATE_WORDS],
    uint64_t domain,
    size_t node_length,
    unsigned int depth,
    size_t child_count,
    size_t child_index,
    size_t child_offset,
    size_t child_length
) {
    const uint64_t expected_prefix[] = {
        FCH_TREE_TAG_NODE_CHILD,
        FCH_TREE_ENCODING_VERSION,
        (uint64_t)node_length,
        (uint64_t)depth,
        (uint64_t)child_count,
        (uint64_t)child_index,
        (uint64_t)child_offset,
        (uint64_t)child_length
    };

    for (size_t i = 0;
         i < sizeof(expected_prefix) / sizeof(expected_prefix[0]);
         i++) {
        if (record_word(record, i) != expected_prefix[i]) {
            fprintf(
                stderr,
                "FAIL: child %u metadata word %u\n",
                (unsigned int)child_index,
                (unsigned int)i
            );
            return 0;
        }
    }
    for (size_t i = 0; i < FCH_INTERNAL_STATE_WORDS; i++) {
        if (record_word(record, 8u + i) != child_state[i]) {
            fprintf(
                stderr,
                "FAIL: child %u state word %u\n",
                (unsigned int)child_index,
                (unsigned int)i
            );
            return 0;
        }
    }

    uint64_t flags = FCH_MIX_FLAG_NODE_CHILD;
    if (child_index + 1u == child_count)
        flags |= FCH_MIX_FLAG_FINAL;

    return check_record_tweaks(
        record,
        FCH_MIX_BLOCK_SIZE,
        (uint64_t)child_index + 1u,
        domain,
        flags,
        "child record tweaks"
    );
}

static int check_node_case(int depth, uint64_t expected_domain) {
    uint64_t child_words[FCH_INTERNAL_STATE_WORDS];
    for (size_t i = 0; i < FCH_INTERNAL_STATE_WORDS; i++)
        child_words[i] = UINT64_C(0xA5A5000000000000) + i;

    fch_state_t children[2] = {
        { child_words, FCH_INTERNAL_STATE_WORDS },
        { child_words, FCH_INTERNAL_STATE_WORDS }
    };
    const fch_block_t blocks[2] = {
        { 0u, 19u },
        { 19u, 45u }
    };

    reset_capture();
    fch_state_t node = fch_combine(
        children,
        blocks,
        2u,
        64u,
        FCH_INTERNAL_STATE_WORDS,
        depth
    );
    REQUIRE(node.state != NULL, "node capture failed");
    free(node.state);

    REQUIRE(g_init_count == 1u, "node initialization count");
    REQUIRE(g_init_domain == expected_domain, "node domain");
    REQUIRE(g_record_count == 3u, "node record count");
    REQUIRE(
        check_node_header(
            &g_records[0],
            expected_domain,
            64u,
            (unsigned int)depth,
            2u
        ),
        "node header"
    );
    REQUIRE(
        check_child_record(
            &g_records[1],
            child_words,
            expected_domain,
            64u,
            (unsigned int)depth,
            2u,
            0u,
            0u,
            19u
        ),
        "first child record"
    );
    REQUIRE(
        check_child_record(
            &g_records[2],
            child_words,
            expected_domain,
            64u,
            (unsigned int)depth,
            2u,
            1u,
            19u,
            45u
        ),
        "second child record"
    );
    REQUIRE(
        memcmp(
            g_records[1].block,
            g_records[2].block,
            FCH_MIX_BLOCK_SIZE
        ) != 0,
        "child position and bounds do not change the record"
    );

    return 1;
}

static int check_node_records(void) {
    REQUIRE(
        check_node_case(0, FCH_DOMAIN_ROOT_NODE),
        "root node encoding"
    );
    REQUIRE(
        check_node_case(5, FCH_DOMAIN_INNER_NODE),
        "internal node encoding"
    );
    return 1;
}

int main(void) {
    if (!check_namespaces())
        return 1;
    if (!check_leaf_records())
        return 1;
    if (!check_node_records())
        return 1;

    printf(
        "PASS: tree domains, headers, positions, lengths, and records verified\n"
    );
    return 0;
}
