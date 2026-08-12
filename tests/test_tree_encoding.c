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
        FCH_TREE_TAG_OUTPUT
    };
    static const char *const tag_text[] = {
        "FCHLEAF2",
        "FCHLDAT2",
        "FCHNODE2",
        "FCHCHLD2",
        "FCHOUT02"
    };
    const uint64_t domains[] = {
        FCH_DOMAIN_LEAF,
        FCH_DOMAIN_NODE,
        FCH_DOMAIN_OUTPUT_256,
        FCH_DOMAIN_OUTPUT_512
    };
    const uint64_t flags[] = {
        FCH_MIX_FLAG_LEAF_HEADER,
        FCH_MIX_FLAG_LEAF_DATA,
        FCH_MIX_FLAG_NODE_HEADER,
        FCH_MIX_FLAG_NODE_CHILD,
        FCH_MIX_FLAG_OUTPUT
    };

    REQUIRE(
        values_are_distinct(tags, sizeof(tags) / sizeof(tags[0])),
        "record tags are not distinct"
    );
    REQUIRE(
        values_are_distinct(domains, sizeof(domains) / sizeof(domains[0])),
        "domains are not distinct"
    );
    REQUIRE(
        values_are_distinct(flags, sizeof(flags) / sizeof(flags[0])),
        "record flags are not distinct"
    );

    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++)
        REQUIRE(tag_matches(tags[i], tag_text[i]), "record tag bytes changed");

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
        FCH_TREE_ENCODING_VERSION == UINT64_C(2),
        "tree encoding version changed"
    );
    REQUIRE(
        FCH_PADDING_FORMAT_VERSION == UINT64_C(1),
        "padding version changed"
    );
    REQUIRE(FCH_TREE_LEAF_BYTES == 1024u, "leaf span changed");
    REQUIRE(FCH_TREE_ARITY == 2u, "tree arity changed");
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

static int check_leaf_records(void) {
    uint8_t data[130];
    uint64_t state_words[FCH_INTERNAL_STATE_WORDS] = {0};
    fch_state_t state = {
        state_words,
        FCH_INTERNAL_STATE_WORDS,
        {0, 0, 0, 0, 0}
    };

    for (size_t i = 0; i < sizeof(data); i++)
        data[i] = (uint8_t)(i * 17u + 3u);

    reset_capture();
    fch_leaf_compress(data, sizeof(data), &state, 99);

    REQUIRE(g_init_count == 1u, "leaf did not initialize exactly once");
    REQUIRE(g_init_state_words == FCH_INTERNAL_STATE_WORDS,
            "leaf initialized the wrong state width");
    REQUIRE(g_init_domain == FCH_DOMAIN_LEAF,
            "leaf initialized the wrong domain");
    REQUIRE(g_record_count == 3u, "leaf emitted the wrong record count");

    const captured_record_t *header = &g_records[0];
    REQUIRE(check_record_tweaks(
                header,
                FCH_MIX_BLOCK_SIZE,
                0,
                FCH_DOMAIN_LEAF,
                FCH_MIX_FLAG_LEAF_HEADER,
                "leaf header tweak mismatch"),
            "leaf header tweak check failed");

    const uint64_t expected_header[] = {
        FCH_TREE_TAG_LEAF_HEADER,
        FCH_TREE_ENCODING_VERSION,
        FCH_DOMAIN_LEAF,
        0,
        0,
        sizeof(data),
        FCH_TREE_LEAF_BYTES,
        FCH_INTERNAL_STATE_WORDS,
        FCH_MIX_BLOCK_SIZE,
        FCH_MIX_ROUNDS,
        FCH_PADDING_FORMAT_VERSION,
        FCH_TREE_ARITY
    };
    for (size_t i = 0;
         i < sizeof(expected_header) / sizeof(expected_header[0]);
         i++) {
        REQUIRE(record_word(header, i) == expected_header[i],
                "leaf header field mismatch");
    }
    REQUIRE(bytes_are_zero(header->block, 96u, sizeof(header->block)),
            "leaf header reserved bytes are not zero");

    const captured_record_t *first = &g_records[1];
    REQUIRE(check_record_tweaks(
                first,
                FCH_MIX_BLOCK_SIZE,
                120,
                FCH_DOMAIN_LEAF,
                FCH_MIX_FLAG_LEAF_DATA,
                "first leaf data tweak mismatch"),
            "first leaf data tweak check failed");
    REQUIRE(record_word(first, 0) == FCH_TREE_TAG_LEAF_DATA,
            "first leaf data tag mismatch");
    REQUIRE(memcmp(first->block + 8u, data, 120u) == 0,
            "first leaf payload mismatch");

    const captured_record_t *last = &g_records[2];
    REQUIRE(check_record_tweaks(
                last,
                18u,
                sizeof(data),
                FCH_DOMAIN_LEAF,
                FCH_MIX_FLAG_LEAF_DATA | FCH_MIX_FLAG_FINAL,
                "final leaf data tweak mismatch"),
            "final leaf data tweak check failed");
    REQUIRE(record_word(last, 0) == FCH_TREE_TAG_LEAF_DATA,
            "final leaf data tag mismatch");
    REQUIRE(memcmp(last->block + 8u, data + 120u, 10u) == 0,
            "final leaf payload mismatch");
    REQUIRE(bytes_are_zero(last->block, 18u, sizeof(last->block)),
            "final leaf record padding is not zero");

    REQUIRE(state.tree.level == 0u && state.tree.first_leaf == 0u &&
            state.tree.leaf_count == 1u && state.tree.byte_offset == 0u &&
            state.tree.byte_length == sizeof(data),
            "leaf position metadata mismatch");
    return 1;
}

static int check_node_records(void) {
    enum {
        NODE_LENGTH = 1500,
        RIGHT_LENGTH = NODE_LENGTH - FCH_TREE_LEAF_BYTES
    };
    uint64_t left_words[FCH_INTERNAL_STATE_WORDS];
    uint64_t right_words[FCH_INTERNAL_STATE_WORDS];
    fch_state_t children[FCH_TREE_ARITY] = {
        {left_words, FCH_INTERNAL_STATE_WORDS, {0, 0, 0, 0, 0}},
        {right_words, FCH_INTERNAL_STATE_WORDS, {0, 0, 0, 0, 0}}
    };
    const fch_block_t blocks[FCH_TREE_ARITY] = {
        {0u, FCH_TREE_LEAF_BYTES},
        {FCH_TREE_LEAF_BYTES, RIGHT_LENGTH}
    };

    for (size_t i = 0; i < FCH_INTERNAL_STATE_WORDS; i++) {
        left_words[i] = UINT64_C(0x1111111111111111) + i;
        right_words[i] = UINT64_C(0xA0A0A0A0A0A0A0A0) + i;
    }
    REQUIRE(fch_tree_position_for_range(
                0u,
                FCH_TREE_LEAF_BYTES,
                &children[0].tree),
            "left child position setup failed");
    REQUIRE(fch_tree_position_for_range(
                FCH_TREE_LEAF_BYTES,
                RIGHT_LENGTH,
                &children[1].tree),
            "right child position setup failed");

    reset_capture();
    fch_state_t parent = fch_combine(
        children,
        blocks,
        FCH_TREE_ARITY,
        NODE_LENGTH,
        FCH_INTERNAL_STATE_WORDS,
        77
    );

    REQUIRE(parent.state != NULL, "canonical node combine failed");
    REQUIRE(g_init_count == 1u, "node did not initialize exactly once");
    REQUIRE(g_init_domain == FCH_DOMAIN_NODE,
            "node initialized the wrong domain");
    REQUIRE(g_record_count == 3u, "node emitted the wrong record count");

    const captured_record_t *header = &g_records[0];
    REQUIRE(check_record_tweaks(
                header,
                FCH_MIX_BLOCK_SIZE,
                0,
                FCH_DOMAIN_NODE,
                FCH_MIX_FLAG_NODE_HEADER,
                "node header tweak mismatch"),
            "node header tweak check failed");
    const uint64_t expected_header[] = {
        FCH_TREE_TAG_NODE_HEADER,
        FCH_TREE_ENCODING_VERSION,
        FCH_DOMAIN_NODE,
        1,
        0,
        2,
        0,
        NODE_LENGTH,
        FCH_TREE_ARITY,
        FCH_TREE_LEAF_BYTES,
        FCH_INTERNAL_STATE_WORDS,
        FCH_MIX_BLOCK_SIZE,
        FCH_MIX_ROUNDS
    };
    for (size_t i = 0;
         i < sizeof(expected_header) / sizeof(expected_header[0]);
         i++) {
        REQUIRE(record_word(header, i) == expected_header[i],
                "node header field mismatch");
    }
    REQUIRE(bytes_are_zero(header->block, 104u, sizeof(header->block)),
            "node header reserved bytes are not zero");

    for (size_t child_index = 0;
         child_index < FCH_TREE_ARITY;
         child_index++) {
        const captured_record_t *record = &g_records[child_index + 1u];
        const fch_tree_position_t *position = &children[child_index].tree;
        const uint64_t *words = children[child_index].state;
        uint64_t flags = FCH_MIX_FLAG_NODE_CHILD;
        if (child_index + 1u == FCH_TREE_ARITY)
            flags |= FCH_MIX_FLAG_FINAL;

        REQUIRE(check_record_tweaks(
                    record,
                    FCH_MIX_BLOCK_SIZE,
                    child_index + 1u,
                    FCH_DOMAIN_NODE,
                    flags,
                    "node child tweak mismatch"),
                "node child tweak check failed");
        REQUIRE(record_word(record, 0) == FCH_TREE_TAG_NODE_CHILD,
                "node child tag mismatch");
        REQUIRE(record_word(record, 1) == FCH_TREE_ENCODING_VERSION,
                "node child version mismatch");
        REQUIRE(record_word(record, 2) == child_index,
                "node child index mismatch");
        REQUIRE(record_word(record, 3) == position->level,
                "node child level mismatch");
        REQUIRE(record_word(record, 4) == position->first_leaf,
                "node child first-leaf mismatch");
        REQUIRE(record_word(record, 5) == position->leaf_count,
                "node child leaf-count mismatch");
        REQUIRE(record_word(record, 6) == position->byte_offset,
                "node child offset mismatch");
        REQUIRE(record_word(record, 7) == position->byte_length,
                "node child length mismatch");
        for (size_t i = 0; i < FCH_INTERNAL_STATE_WORDS; i++) {
            REQUIRE(record_word(record, 8u + i) == words[i],
                    "node child state word mismatch");
        }
    }

    REQUIRE(parent.tree.level == 1u && parent.tree.first_leaf == 0u &&
            parent.tree.leaf_count == 2u && parent.tree.byte_offset == 0u &&
            parent.tree.byte_length == NODE_LENGTH,
            "parent position metadata mismatch");
    free(parent.state);
    return 1;
}

int main(void) {
    if (!check_namespaces() ||
        !check_leaf_records() ||
        !check_node_records())
        return 1;

    puts("PASS: canonical tree records use the v2 encoding");
    return 0;
}
