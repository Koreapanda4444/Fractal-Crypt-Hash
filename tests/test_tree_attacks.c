#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitops.h"
#include "combine.h"
#include "debug_hooks.h"
#include "fch.h"
#include "fractal.h"
#include "leaf.h"
#include "params.h"
#include "test_utils.h"

enum {
    MULTICOLLISION_SAMPLES = 4096,
    MULTICOLLISION_PREFIX_BITS = 20,
    SECOND_PREIMAGE_LENGTH = 16384,
    SECOND_PREIMAGE_CANDIDATES = 512,
    SECOND_PREIMAGE_MODES = 8,
    ATTACK_CHUNK_SIZE = 64,
    TREE_SHAPE_MIN_LEAVES = 3,
    TREE_SHAPE_MAX_LEAVES = 16,
    SUBTREE_REPLACEMENT_LEAVES = 8,
    LONG_MESSAGE_LENGTH = 262144,
    LONG_CHUNK_SIZE = 4096,
    LONG_VARIANTS = 15,
    EXPANDABLE_PAIRS = 96,
    EXPANDABLE_SUFFIX_LENGTH = 2048,
    EXPANDABLE_MAX_EXTRA_LEAVES = 4,
    HERDING_PREFIX_BITS = 8,
    HERDING_PREFIXES = 1 << HERDING_PREFIX_BITS,
    HERDING_MESSAGE_LENGTH = 4096,
    MULTI_TARGET_TARGETS = 64,
    MULTI_TARGET_CANDIDATES = 256,
    MULTI_TARGET_LENGTH = 4096,
    VARIANT_REUSE_SAMPLES = 256,
    VARIANT_REUSE_MAX_LENGTH = 8192
};

typedef struct {
    uint64_t words[FCH_INTERNAL_STATE_WORDS];
    fch_tree_position_t tree;
} state_record_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t base_offset;
} positioned_input_t;

typedef struct {
    uint64_t truncated_pairs;
    size_t maximum_bucket;
    size_t exact_collisions;
} collision_stats_t;

static int g_current_max_depth = 0;
static uint64_t g_last_root[FCH_INTERNAL_STATE_WORDS];
static int g_last_root_valid = 0;

void fch_debug_hook(
    fch_hook_point_t point,
    int depth,
    const uint64_t *state,
    size_t state_words
) {
    if (depth > g_current_max_depth)
        g_current_max_depth = depth;
    if (point == FCH_HOOK_AFTER_ROOT && state &&
        state_words == FCH_INTERNAL_STATE_WORDS) {
        memcpy(g_last_root, state, sizeof(g_last_root));
        g_last_root_valid = 1;
    }
}

static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t value;

    *state += UINT64_C(0x9E3779B97F4A7C15);
    value = *state;
    value = (value ^ (value >> 30u)) * UINT64_C(0xBF58476D1CE4E5B9);
    value = (value ^ (value >> 27u)) * UINT64_C(0x94D049BB133111EB);
    return value ^ (value >> 31u);
}

static void fill_bytes(uint8_t *output, size_t length, uint64_t *state) {
    size_t offset = 0;

    while (offset < length) {
        uint64_t value = splitmix64_next(state);
        for (size_t i = 0; i < 8u && offset < length; i++) {
            output[offset++] = (uint8_t)value;
            value >>= 8u;
        }
    }
}

static int hash_both_capture(
    const uint8_t *message,
    size_t length,
    uint8_t output256[32],
    uint8_t output512[64],
    uint64_t root256[FCH_INTERNAL_STATE_WORDS],
    uint64_t root512[FCH_INTERNAL_STATE_WORDS],
    int *maximum_depth
) {
    g_current_max_depth = 0;
    g_last_root_valid = 0;
    if (!fch_hash_256_checked(message, length, output256) ||
        !g_last_root_valid)
        return 0;
    if (root256)
        memcpy(root256, g_last_root, sizeof(g_last_root));

    g_last_root_valid = 0;
    if (!fch_hash_512_checked(message, length, output512) ||
        !g_last_root_valid)
        return 0;
    if (root512)
        memcpy(root512, g_last_root, sizeof(g_last_root));

    if (maximum_depth)
        *maximum_depth = g_current_max_depth;
    return 1;
}

static int hash_both(
    const uint8_t *message,
    size_t length,
    uint8_t output256[32],
    uint8_t output512[64],
    int *maximum_depth
) {
    return hash_both_capture(
        message,
        length,
        output256,
        output512,
        NULL,
        NULL,
        maximum_depth
    );
}

static int positioned_read(
    void *context,
    size_t offset,
    uint8_t *output,
    size_t length
) {
    positioned_input_t *input = (positioned_input_t *)context;

    if (!input || (!output && length > 0u) ||
        offset < input->base_offset)
        return 0;
    size_t local_offset = offset - input->base_offset;
    if (local_offset > input->length ||
        length > input->length - local_offset ||
        (!input->data && length > 0u))
        return 0;

    if (length > 0u)
        memcpy(output, input->data + local_offset, length);
    return 1;
}

static int make_leaf(
    const uint8_t *message,
    size_t length,
    size_t offset,
    int depth,
    state_record_t *output
) {
    if (!output)
        return 0;

    positioned_input_t input = { message, length, offset };
    fch_reader_t reader = { positioned_read, &input };
    fch_state_t state = {
        output->words,
        FCH_INTERNAL_STATE_WORDS,
        {0, 0, 0, 0, 0}
    };
    int ok = fch_leaf_compress_reader(
        &reader,
        offset,
        length,
        &state,
        depth
    );
    if (ok)
        output->tree = state.tree;
    return ok;
}

static int make_subtree(
    const uint8_t *message,
    size_t length,
    size_t offset,
    state_record_t *output
) {
    if (!output)
        return 0;

    positioned_input_t input = { message, length, offset };
    fch_reader_t reader = { positioned_read, &input };
    fch_state_t state = fch_process_reader(
        &reader,
        offset,
        length,
        0,
        FCH_INTERNAL_STATE_WORDS
    );
    if (!state.state)
        return 0;

    memcpy(output->words, state.state, sizeof(output->words));
    output->tree = state.tree;
    free(state.state);
    return 1;
}

static fch_state_t state_view(state_record_t *record) {
    fch_state_t view = {
        record ? record->words : NULL,
        FCH_INTERNAL_STATE_WORDS,
        record ? record->tree : (fch_tree_position_t){0, 0, 0, 0, 0}
    };
    return view;
}

static int same_tree_position(
    const fch_tree_position_t *left,
    const fch_tree_position_t *right
) {
    return
        left && right &&
        left->level == right->level &&
        left->first_leaf == right->first_leaf &&
        left->leaf_count == right->leaf_count &&
        left->byte_offset == right->byte_offset &&
        left->byte_length == right->byte_length;
}

static int same_state(
    const state_record_t *left,
    const state_record_t *right
) {
    return
        left && right &&
        same_tree_position(&left->tree, &right->tree) &&
        memcmp(left->words, right->words, sizeof(left->words)) == 0;
}

static int same_state_words(
    const state_record_t *left,
    const state_record_t *right
) {
    return
        left && right &&
        memcmp(left->words, right->words, sizeof(left->words)) == 0;
}

static int combine_state(
    fch_state_t *children,
    const fch_block_t *blocks,
    size_t count,
    size_t node_length,
    int depth,
    state_record_t *output
) {
    if (!output)
        return 0;

    fch_state_t combined = fch_combine(
        children,
        blocks,
        count,
        node_length,
        FCH_INTERNAL_STATE_WORDS,
        depth
    );
    if (!combined.state)
        return 0;

    memcpy(output->words, combined.state, sizeof(output->words));
    output->tree = combined.tree;
    free(combined.state);
    return 1;
}

static int compare_states(const void *left, const void *right) {
    const state_record_t *a = (const state_record_t *)left;
    const state_record_t *b = (const state_record_t *)right;

    for (size_t i = 0; i < FCH_INTERNAL_STATE_WORDS; i++) {
        if (a->words[i] < b->words[i])
            return -1;
        if (a->words[i] > b->words[i])
            return 1;
    }
    return 0;
}

static int compare_u32(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static int collision_stats(
    const state_record_t *states,
    size_t count,
    collision_stats_t *stats
) {
    if (!states || count == 0 || !stats)
        return 0;

    state_record_t *ordered =
        (state_record_t *)malloc(count * sizeof(*ordered));
    uint32_t *prefixes =
        (uint32_t *)malloc(count * sizeof(*prefixes));
    if (!ordered || !prefixes) {
        free(ordered);
        free(prefixes);
        return 0;
    }

    memcpy(ordered, states, count * sizeof(*ordered));
    const uint64_t prefix_mask =
        (UINT64_C(1) << MULTICOLLISION_PREFIX_BITS) - 1u;
    for (size_t i = 0; i < count; i++)
        prefixes[i] = (uint32_t)(states[i].words[0] & prefix_mask);

    qsort(ordered, count, sizeof(*ordered), compare_states);
    qsort(prefixes, count, sizeof(*prefixes), compare_u32);

    stats->truncated_pairs = 0;
    stats->maximum_bucket = 1;
    stats->exact_collisions = 0;

    for (size_t i = 1; i < count; i++) {
        if (compare_states(&ordered[i - 1u], &ordered[i]) == 0)
            stats->exact_collisions++;
    }

    size_t run_start = 0;
    while (run_start < count) {
        size_t run_end = run_start + 1u;
        while (run_end < count &&
               prefixes[run_end] == prefixes[run_start])
            run_end++;

        size_t bucket = run_end - run_start;
        stats->truncated_pairs +=
            (uint64_t)bucket * (uint64_t)(bucket - 1u) / 2u;
        if (bucket > stats->maximum_bucket)
            stats->maximum_bucket = bucket;
        run_start = run_end;
    }

    free(ordered);
    free(prefixes);
    return 1;
}

static int multicollision_screen(void) {
    state_record_t *leaves = (state_record_t *)calloc(
        MULTICOLLISION_SAMPLES,
        sizeof(*leaves)
    );
    state_record_t *nodes = (state_record_t *)calloc(
        MULTICOLLISION_SAMPLES,
        sizeof(*nodes)
    );
    state_record_t *roots = (state_record_t *)calloc(
        MULTICOLLISION_SAMPLES,
        sizeof(*roots)
    );
    if (!leaves || !nodes || !roots) {
        free(leaves);
        free(nodes);
        free(roots);
        return 0;
    }

    uint8_t sibling_message[FCH_TREE_LEAF_BYTES];
    uint64_t sibling_stream = UINT64_C(0x51B11A65EED00001);
    state_record_t sibling;
    fill_bytes(
        sibling_message,
        sizeof(sibling_message),
        &sibling_stream
    );
    if (!make_leaf(
            sibling_message,
            sizeof(sibling_message),
            FCH_TREE_LEAF_BYTES,
            2,
            &sibling
        )) {
        free(leaves);
        free(nodes);
        free(roots);
        return 0;
    }

    state_record_t fixed_leaves[2];
    uint8_t fixed_messages[2][FCH_TREE_LEAF_BYTES];
    uint64_t fixed_stream = UINT64_C(0x51B11A65EED00002);
    for (size_t i = 0; i < 2u; i++) {
        fill_bytes(
            fixed_messages[i],
            sizeof(fixed_messages[i]),
            &fixed_stream
        );
        if (!make_leaf(
                fixed_messages[i],
                sizeof(fixed_messages[i]),
                (i + 2u) * FCH_TREE_LEAF_BYTES,
                2,
                &fixed_leaves[i]
            )) {
            free(leaves);
            free(nodes);
            free(roots);
            return 0;
        }
    }

    const fch_block_t leaf_blocks[2] = {
        { 0u, FCH_TREE_LEAF_BYTES },
        { FCH_TREE_LEAF_BYTES, FCH_TREE_LEAF_BYTES }
    };
    const fch_block_t subtree_blocks[2] = {
        { 0u, FCH_TREE_LEAF_BYTES * 2u },
        { FCH_TREE_LEAF_BYTES * 2u, FCH_TREE_LEAF_BYTES * 2u }
    };
    state_record_t fixed_subtree;
    fch_state_t fixed_children[2] = {
        state_view(&fixed_leaves[0]),
        state_view(&fixed_leaves[1])
    };
    if (!combine_state(
            fixed_children,
            leaf_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 2u,
            1,
            &fixed_subtree
        )) {
        free(leaves);
        free(nodes);
        free(roots);
        return 0;
    }

    uint64_t stream = UINT64_C(0xC0111510A5EED801);
    int generated = 1;
    for (size_t sample = 0;
         sample < MULTICOLLISION_SAMPLES;
         sample++) {
        uint8_t message[FCH_TREE_LEAF_BYTES];
        fill_bytes(message, sizeof(message), &stream);
        fch_store_le64(message, (uint64_t)sample);

        if (!make_leaf(
                message,
                sizeof(message),
                0u,
                2,
                &leaves[sample]
            )) {
            generated = 0;
            break;
        }

        fch_state_t children[2] = {
            state_view(&leaves[sample]),
            state_view(&sibling)
        };
        if (!combine_state(
                children,
                leaf_blocks,
                2,
                FCH_TREE_LEAF_BYTES * 2u,
                1,
                &nodes[sample]
            )) {
            generated = 0;
            break;
        }

        fch_state_t root_children[2] = {
            state_view(&nodes[sample]),
            state_view(&fixed_subtree)
        };
        if (!combine_state(
                root_children,
                subtree_blocks,
                2u,
                FCH_TREE_LEAF_BYTES * 4u,
                0,
                &roots[sample]
            )) {
            generated = 0;
            break;
        }
    }

    collision_stats_t leaf_stats;
    collision_stats_t node_stats;
    collision_stats_t root_stats;
    int ok = generated &&
        collision_stats(
            leaves,
            MULTICOLLISION_SAMPLES,
            &leaf_stats
        ) &&
        collision_stats(
            nodes,
            MULTICOLLISION_SAMPLES,
            &node_stats
        ) &&
        collision_stats(
            roots,
            MULTICOLLISION_SAMPLES,
            &root_stats
        );

    if (ok) {
        ok =
            leaf_stats.exact_collisions == 0u &&
            node_stats.exact_collisions == 0u &&
            root_stats.exact_collisions == 0u &&
            leaf_stats.truncated_pairs <= 64u &&
            node_stats.truncated_pairs <= 64u &&
            root_stats.truncated_pairs <= 64u &&
            leaf_stats.maximum_bucket <= 4u &&
            node_stats.maximum_bucket <= 4u &&
            root_stats.maximum_bucket <= 4u;

        printf(
            "multicollision,leaf,samples=%u,prefix_bits=%u,"
            "pairs=%llu,max_bucket=%u,exact=%u,%s\n",
            MULTICOLLISION_SAMPLES,
            MULTICOLLISION_PREFIX_BITS,
            (unsigned long long)leaf_stats.truncated_pairs,
            (unsigned int)leaf_stats.maximum_bucket,
            (unsigned int)leaf_stats.exact_collisions,
            ok ? "PASS" : "FAIL"
        );
        printf(
            "multicollision,node,samples=%u,prefix_bits=%u,"
            "pairs=%llu,max_bucket=%u,exact=%u,%s\n",
            MULTICOLLISION_SAMPLES,
            MULTICOLLISION_PREFIX_BITS,
            (unsigned long long)node_stats.truncated_pairs,
            (unsigned int)node_stats.maximum_bucket,
            (unsigned int)node_stats.exact_collisions,
            ok ? "PASS" : "FAIL"
        );
        printf(
            "multicollision,root,samples=%u,prefix_bits=%u,"
            "pairs=%llu,max_bucket=%u,exact=%u,%s\n",
            MULTICOLLISION_SAMPLES,
            MULTICOLLISION_PREFIX_BITS,
            (unsigned long long)root_stats.truncated_pairs,
            (unsigned int)root_stats.maximum_bucket,
            (unsigned int)root_stats.exact_collisions,
            ok ? "PASS" : "FAIL"
        );
    }

    free(leaves);
    free(nodes);
    free(roots);
    return ok;
}

static int tree_shape_screen(void) {
    state_record_t leaves[4];
    uint64_t stream = UINT64_C(0x7EED5A9E5EED0001);

    for (size_t i = 0; i < 4u; i++) {
        uint8_t message[FCH_TREE_LEAF_BYTES];
        fill_bytes(message, sizeof(message), &stream);
        fch_store_le64(message, i);
        if (!make_leaf(
                message,
                sizeof(message),
                i * FCH_TREE_LEAF_BYTES,
                2,
                &leaves[i]
            ))
            return 0;
    }

    const fch_block_t pair_blocks[2] = {
        {0u, FCH_TREE_LEAF_BYTES},
        {FCH_TREE_LEAF_BYTES, FCH_TREE_LEAF_BYTES}
    };
    const fch_block_t bcd_blocks[2] = {
        {0u, FCH_TREE_LEAF_BYTES * 2u},
        {FCH_TREE_LEAF_BYTES * 2u, FCH_TREE_LEAF_BYTES}
    };
    const fch_block_t root_blocks[2] = {
        {0u, FCH_TREE_LEAF_BYTES * 2u},
        {FCH_TREE_LEAF_BYTES * 2u, FCH_TREE_LEAF_BYTES * 2u}
    };

    state_record_t pair_ab;
    state_record_t pair_cd;
    state_record_t pair_bc;
    fch_state_t ab_children[2] = {
        state_view(&leaves[0]),
        state_view(&leaves[1])
    };
    fch_state_t cd_children[2] = {
        state_view(&leaves[2]),
        state_view(&leaves[3])
    };
    fch_state_t bc_children[2] = {
        state_view(&leaves[1]),
        state_view(&leaves[2])
    };
    if (!combine_state(
            ab_children,
            pair_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 2u,
            1,
            &pair_ab
        ) ||
        !combine_state(
            cd_children,
            pair_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 2u,
            1,
            &pair_cd
        ) ||
        !combine_state(
            bc_children,
            pair_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 2u,
            1,
            &pair_bc
        ))
        return 0;

    state_record_t subtree_bcd;
    fch_state_t bcd_children[2] = {
        state_view(&pair_bc),
        state_view(&leaves[3])
    };
    if (!combine_state(
            bcd_children,
            bcd_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 3u,
            1,
            &subtree_bcd
        ))
        return 0;

    state_record_t root_depth0;
    state_record_t root_depth99;
    fch_state_t canonical_children[2] = {
        state_view(&pair_ab),
        state_view(&pair_cd)
    };
    if (!combine_state(
            canonical_children,
            root_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 4u,
            0,
            &root_depth0
        ) ||
        !combine_state(
            canonical_children,
            root_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 4u,
            99,
            &root_depth99
        ))
        return 0;

    if (memcmp(
            root_depth0.words,
            root_depth99.words,
            sizeof(root_depth0.words)
        ) != 0)
        return 0;

    state_record_t altered_ab = pair_ab;
    altered_ab.words[0] ^= UINT64_C(1);
    state_record_t altered_root;
    fch_state_t altered_children[2] = {
        state_view(&altered_ab),
        state_view(&pair_cd)
    };
    if (!combine_state(
            altered_children,
            root_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 4u,
            0,
            &altered_root
        ))
        return 0;

    int altered_distance = bit_diff(
        (const uint8_t *)root_depth0.words,
        (const uint8_t *)altered_root.words,
        sizeof(root_depth0.words)
    );

    unsigned int rejected = 0;
    state_record_t rejected_output;

    fch_state_t reversed_children[2] = {
        state_view(&pair_cd),
        state_view(&pair_ab)
    };
    if (!combine_state(
            reversed_children,
            root_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 4u,
            0,
            &rejected_output
        ))
        rejected++;

    fch_state_t skew_children[2] = {
        state_view(&leaves[0]),
        state_view(&subtree_bcd)
    };
    const fch_block_t skew_blocks[2] = {
        {0u, FCH_TREE_LEAF_BYTES},
        {FCH_TREE_LEAF_BYTES, FCH_TREE_LEAF_BYTES * 3u}
    };
    if (!combine_state(
            skew_children,
            skew_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 4u,
            0,
            &rejected_output
        ))
        rejected++;

    fch_state_t flat_children[4] = {
        state_view(&leaves[0]),
        state_view(&leaves[1]),
        state_view(&leaves[2]),
        state_view(&leaves[3])
    };
    const fch_block_t flat_blocks[4] = {
        {0u, FCH_TREE_LEAF_BYTES},
        {FCH_TREE_LEAF_BYTES, FCH_TREE_LEAF_BYTES},
        {FCH_TREE_LEAF_BYTES * 2u, FCH_TREE_LEAF_BYTES},
        {FCH_TREE_LEAF_BYTES * 3u, FCH_TREE_LEAF_BYTES}
    };
    if (!combine_state(
            flat_children,
            flat_blocks,
            4u,
            FCH_TREE_LEAF_BYTES * 4u,
            0,
            &rejected_output
        ))
        rejected++;

    const fch_block_t shifted_blocks[2] = {
        {0u, FCH_TREE_LEAF_BYTES * 2u - 1u},
        {FCH_TREE_LEAF_BYTES * 2u - 1u,
         FCH_TREE_LEAF_BYTES * 2u + 1u}
    };
    if (!combine_state(
            canonical_children,
            shifted_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 4u,
            0,
            &rejected_output
        ))
        rejected++;

    state_record_t forged_cd = pair_cd;
    forged_cd.tree.first_leaf++;
    fch_state_t forged_children[2] = {
        state_view(&pair_ab),
        state_view(&forged_cd)
    };
    if (!combine_state(
            forged_children,
            root_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 4u,
            0,
            &rejected_output
        ))
        rejected++;

    if (!combine_state(
            canonical_children,
            root_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 4u,
            -1,
            &rejected_output
        ))
        rejected++;

    int ok = rejected == 6u && altered_distance >= 160;
    printf(
        "tree_shape,canonical=accepted,invalid_rejected=%u,"
        "depth_independent=yes,child_bit_distance=%d,%s\n",
        rejected,
        altered_distance,
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

static int canonical_partition_screen(void) {
    uint8_t message[TREE_SHAPE_MAX_LEAVES * FCH_TREE_LEAF_BYTES];
    uint64_t stream = UINT64_C(0xCA110CA17AEE0001);
    fill_bytes(message, sizeof(message), &stream);

    unsigned int accepted = 0;
    unsigned int rejected = 0;
    unsigned int expected_rejected = 0;

    for (size_t leaf_count = TREE_SHAPE_MIN_LEAVES;
         leaf_count <= TREE_SHAPE_MAX_LEAVES;
         leaf_count++) {
        size_t length = leaf_count * FCH_TREE_LEAF_BYTES;
        fch_tree_position_t parent;
        fch_tree_position_t expected_children[2];
        if (!fch_tree_position_for_range(0u, length, &parent) ||
            !fch_tree_split_position(&parent, expected_children))
            return 0;

        state_record_t direct;
        state_record_t left;
        state_record_t right;
        state_record_t combined;
        state_record_t rejected_output;
        if (!make_subtree(message, length, 0u, &direct) ||
            !make_subtree(
                message,
                expected_children[0].byte_length,
                expected_children[0].byte_offset,
                &left
            ) ||
            !make_subtree(
                message + expected_children[1].byte_offset,
                expected_children[1].byte_length,
                expected_children[1].byte_offset,
                &right
            ))
            return 0;

        fch_block_t canonical_blocks[2] = {
            {0u, expected_children[0].byte_length},
            {
                expected_children[0].byte_length,
                expected_children[1].byte_length
            }
        };
        fch_state_t canonical_children[2] = {
            state_view(&left),
            state_view(&right)
        };
        if (!combine_state(
                canonical_children,
                canonical_blocks,
                2u,
                length,
                0,
                &combined
            ) ||
            !same_state(&direct, &combined))
            return 0;
        accepted++;

        fch_state_t reversed_children[2] = {
            state_view(&right),
            state_view(&left)
        };
        expected_rejected++;
        if (combine_state(
                reversed_children,
                canonical_blocks,
                2u,
                length,
                0,
                &rejected_output
            ))
            return 0;
        rejected++;

        for (size_t split = 1u; split < leaf_count; split++) {
            if (split == expected_children[0].leaf_count)
                continue;

            size_t left_length = split * FCH_TREE_LEAF_BYTES;
            size_t right_length = length - left_length;
            state_record_t alternative_left;
            state_record_t alternative_right;
            if (!make_subtree(
                    message,
                    left_length,
                    0u,
                    &alternative_left
                ) ||
                !make_subtree(
                    message + left_length,
                    right_length,
                    left_length,
                    &alternative_right
                ))
                return 0;

            fch_state_t alternative_children[2] = {
                state_view(&alternative_left),
                state_view(&alternative_right)
            };
            fch_block_t alternative_blocks[2] = {
                {0u, left_length},
                {left_length, right_length}
            };
            expected_rejected++;
            if (combine_state(
                    alternative_children,
                    alternative_blocks,
                    2u,
                    length,
                    0,
                    &rejected_output
                ))
                return 0;
            rejected++;
        }
    }

    unsigned int expected_accepted =
        TREE_SHAPE_MAX_LEAVES - TREE_SHAPE_MIN_LEAVES + 1u;
    int ok =
        accepted == expected_accepted &&
        rejected == expected_rejected;
    printf(
        "canonical_partition,leaf_counts=%u-%u,accepted=%u,"
        "alternatives_rejected=%u,%s\n",
        TREE_SHAPE_MIN_LEAVES,
        TREE_SHAPE_MAX_LEAVES,
        accepted,
        rejected,
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

static int subtree_replacement_screen(void) {
    uint8_t message[
        SUBTREE_REPLACEMENT_LEAVES * FCH_TREE_LEAF_BYTES
    ];
    size_t half_length = sizeof(message) / 2u;
    uint64_t stream = UINT64_C(0x5AB7AEE5EED00001);
    fill_bytes(message, half_length, &stream);
    memcpy(message + half_length, message, half_length);

    state_record_t leaf_left;
    state_record_t leaf_next;
    state_record_t leaf_relocated;
    state_record_t pair_left;
    state_record_t pair_middle;
    state_record_t pair_relocated;
    state_record_t pair_tail;
    state_record_t quad_left;
    state_record_t quad_right;
    state_record_t root;
    if (!make_leaf(
            message,
            FCH_TREE_LEAF_BYTES,
            0u,
            0,
            &leaf_left
        ) ||
        !make_leaf(
            message + FCH_TREE_LEAF_BYTES,
            FCH_TREE_LEAF_BYTES,
            FCH_TREE_LEAF_BYTES,
            0,
            &leaf_next
        ) ||
        !make_leaf(
            message + FCH_TREE_LEAF_BYTES * 4u,
            FCH_TREE_LEAF_BYTES,
            FCH_TREE_LEAF_BYTES * 4u,
            0,
            &leaf_relocated
        ) ||
        !make_subtree(
            message,
            FCH_TREE_LEAF_BYTES * 2u,
            0u,
            &pair_left
        ) ||
        !make_subtree(
            message + FCH_TREE_LEAF_BYTES * 2u,
            FCH_TREE_LEAF_BYTES * 2u,
            FCH_TREE_LEAF_BYTES * 2u,
            &pair_middle
        ) ||
        !make_subtree(
            message + FCH_TREE_LEAF_BYTES * 4u,
            FCH_TREE_LEAF_BYTES * 2u,
            FCH_TREE_LEAF_BYTES * 4u,
            &pair_relocated
        ) ||
        !make_subtree(
            message + FCH_TREE_LEAF_BYTES * 6u,
            FCH_TREE_LEAF_BYTES * 2u,
            FCH_TREE_LEAF_BYTES * 6u,
            &pair_tail
        ) ||
        !make_subtree(message, half_length, 0u, &quad_left) ||
        !make_subtree(
            message + half_length,
            half_length,
            half_length,
            &quad_right
        ) ||
        !make_subtree(message, sizeof(message), 0u, &root))
        return 0;

    if (same_state_words(&leaf_left, &leaf_relocated) ||
        same_state_words(&pair_left, &pair_relocated) ||
        same_state_words(&quad_left, &quad_right))
        return 0;

    const fch_block_t leaf_blocks[2] = {
        {0u, FCH_TREE_LEAF_BYTES},
        {FCH_TREE_LEAF_BYTES, FCH_TREE_LEAF_BYTES}
    };
    const fch_block_t pair_blocks[2] = {
        {0u, FCH_TREE_LEAF_BYTES * 2u},
        {FCH_TREE_LEAF_BYTES * 2u, FCH_TREE_LEAF_BYTES * 2u}
    };
    const fch_block_t quad_blocks[2] = {
        {0u, FCH_TREE_LEAF_BYTES * 4u},
        {FCH_TREE_LEAF_BYTES * 4u, FCH_TREE_LEAF_BYTES * 4u}
    };

    unsigned int rejected = 0;
    state_record_t output;
    fch_state_t wrong_left_pair[2] = {
        state_view(&pair_relocated),
        state_view(&pair_middle)
    };
    if (!combine_state(
            wrong_left_pair,
            pair_blocks,
            2u,
            half_length,
            0,
            &output
        ))
        rejected++;

    fch_state_t wrong_right_pair[2] = {
        state_view(&pair_left),
        state_view(&pair_tail)
    };
    if (!combine_state(
            wrong_right_pair,
            pair_blocks,
            2u,
            half_length,
            0,
            &output
        ))
        rejected++;

    fch_state_t swapped_quads[2] = {
        state_view(&quad_right),
        state_view(&quad_left)
    };
    if (!combine_state(
            swapped_quads,
            quad_blocks,
            2u,
            sizeof(message),
            0,
            &output
        ))
        rejected++;

    for (unsigned int field = 0; field < 5u; field++) {
        state_record_t forged = pair_middle;
        if (field == 0u)
            forged.tree.level++;
        else if (field == 1u)
            forged.tree.first_leaf++;
        else if (field == 2u)
            forged.tree.leaf_count++;
        else if (field == 3u)
            forged.tree.byte_offset += FCH_TREE_LEAF_BYTES;
        else
            forged.tree.byte_length--;

        fch_state_t forged_children[2] = {
            state_view(&pair_left),
            state_view(&forged)
        };
        if (!combine_state(
                forged_children,
                pair_blocks,
                2u,
                half_length,
                0,
                &output
            ))
            rejected++;
    }

    unsigned int grafts_detected = 0;
    state_record_t grafted_leaf = leaf_relocated;
    grafted_leaf.tree = leaf_left.tree;
    fch_state_t leaf_graft_children[2] = {
        state_view(&grafted_leaf),
        state_view(&leaf_next)
    };
    if (!combine_state(
            leaf_graft_children,
            leaf_blocks,
            2u,
            FCH_TREE_LEAF_BYTES * 2u,
            0,
            &output
        ) ||
        same_state(&output, &pair_left))
        return 0;
    grafts_detected++;

    state_record_t grafted_pair = pair_relocated;
    grafted_pair.tree = pair_left.tree;
    fch_state_t pair_graft_children[2] = {
        state_view(&grafted_pair),
        state_view(&pair_middle)
    };
    if (!combine_state(
            pair_graft_children,
            pair_blocks,
            2u,
            half_length,
            0,
            &output
        ) ||
        same_state(&output, &quad_left))
        return 0;
    grafts_detected++;

    state_record_t grafted_quad = quad_right;
    grafted_quad.tree = quad_left.tree;
    fch_state_t quad_graft_children[2] = {
        state_view(&grafted_quad),
        state_view(&quad_right)
    };
    if (!combine_state(
            quad_graft_children,
            quad_blocks,
            2u,
            sizeof(message),
            0,
            &output
        ) ||
        same_state(&output, &root))
        return 0;
    grafts_detected++;

    int ok = rejected == 8u && grafts_detected == 3u;
    printf(
        "subtree_replacement,position_bound_levels=3,"
        "invalid_rejected=%u,grafts_detected=%u,%s\n",
        rejected,
        grafts_detected,
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

static void mutate_second_preimage(
    uint8_t *candidate,
    const uint8_t *target,
    size_t length,
    unsigned int sample
) {
    memcpy(candidate, target, length);
    const size_t chunk_count = length / ATTACK_CHUNK_SIZE;
    const size_t leaf_count = length / FCH_TREE_LEAF_BYTES;
    const size_t mode_sample = sample / SECOND_PREIMAGE_MODES;

    switch (sample % SECOND_PREIMAGE_MODES) {
        case 0: {
            size_t position =
                ((size_t)sample * 2654435761u + 17u) % length;
            candidate[position] ^=
                (uint8_t)(1u << (mode_sample % 8u));
            break;
        }
        case 1: {
            size_t chunk = ((size_t)sample * 17u) % chunk_count;
            uint64_t stream =
                UINT64_C(0x5EC0AD1A7E000001) ^ sample;
            fill_bytes(
                candidate + chunk * ATTACK_CHUNK_SIZE,
                ATTACK_CHUNK_SIZE,
                &stream
            );
            break;
        }
        case 2: {
            size_t first = ((size_t)sample * 29u) % chunk_count;
            size_t second =
                ((size_t)sample * 73u + 1u) % chunk_count;
            if (first == second)
                second = (second + 1u) % chunk_count;
            uint8_t temporary[ATTACK_CHUNK_SIZE];
            memcpy(
                temporary,
                candidate + first * ATTACK_CHUNK_SIZE,
                sizeof(temporary)
            );
            memcpy(
                candidate + first * ATTACK_CHUNK_SIZE,
                candidate + second * ATTACK_CHUNK_SIZE,
                ATTACK_CHUNK_SIZE
            );
            memcpy(
                candidate + second * ATTACK_CHUNK_SIZE,
                temporary,
                sizeof(temporary)
            );
            break;
        }
        case 3: {
            size_t source = ((size_t)sample * 43u) % chunk_count;
            size_t destination =
                ((size_t)sample * 101u + 3u) % chunk_count;
            if (source == destination)
                destination = (destination + 1u) % chunk_count;
            memcpy(
                candidate + destination * ATTACK_CHUNK_SIZE,
                target + source * ATTACK_CHUNK_SIZE,
                ATTACK_CHUNK_SIZE
            );
            break;
        }
        case 4: {
            size_t leaf = (mode_sample * 11u + 3u) % leaf_count;
            uint64_t stream =
                UINT64_C(0x1EAF5EED00000001) ^ sample;
            fill_bytes(
                candidate + leaf * FCH_TREE_LEAF_BYTES,
                FCH_TREE_LEAF_BYTES,
                &stream
            );
            break;
        }
        case 5: {
            size_t first = (mode_sample * 5u) % leaf_count;
            size_t second =
                (mode_sample * 13u + 1u) % leaf_count;
            if (first == second)
                second = (second + 1u) % leaf_count;
            uint8_t temporary[FCH_TREE_LEAF_BYTES];
            memcpy(
                temporary,
                candidate + first * FCH_TREE_LEAF_BYTES,
                sizeof(temporary)
            );
            memcpy(
                candidate + first * FCH_TREE_LEAF_BYTES,
                candidate + second * FCH_TREE_LEAF_BYTES,
                FCH_TREE_LEAF_BYTES
            );
            memcpy(
                candidate + second * FCH_TREE_LEAF_BYTES,
                temporary,
                sizeof(temporary)
            );
            break;
        }
        case 6: {
            size_t subtree_count = leaf_count / 2u;
            size_t source = (mode_sample * 3u) % subtree_count;
            size_t destination =
                (source + 1u +
                 mode_sample * 5u % (subtree_count - 1u)) %
                subtree_count;
            memcpy(
                candidate + destination * FCH_TREE_LEAF_BYTES * 2u,
                target + source * FCH_TREE_LEAF_BYTES * 2u,
                FCH_TREE_LEAF_BYTES * 2u
            );
            break;
        }
        default: {
            uint8_t temporary[SECOND_PREIMAGE_LENGTH];
            size_t multiplier = 1u + 2u * (mode_sample % 8u);
            size_t shift = 1u + mode_sample / 8u % 8u;
            memcpy(temporary, candidate, length);
            for (size_t i = 0; i < leaf_count; i++) {
                size_t source =
                    (multiplier * i + shift) % leaf_count;
                memcpy(
                    candidate + i * FCH_TREE_LEAF_BYTES,
                    temporary + source * FCH_TREE_LEAF_BYTES,
                    FCH_TREE_LEAF_BYTES
                );
            }
            break;
        }
    }

    if (memcmp(candidate, target, length) == 0)
        candidate[(sample * 13u + 1u) % length] ^= 1u;
}

static int second_preimage_screen(void) {
    uint8_t *target = (uint8_t *)malloc(SECOND_PREIMAGE_LENGTH);
    uint8_t *candidate = (uint8_t *)malloc(SECOND_PREIMAGE_LENGTH);
    if (!target || !candidate) {
        free(target);
        free(candidate);
        return 0;
    }

    uint64_t stream = UINT64_C(0x5EC0AD1A6E5EED01);
    fill_bytes(target, SECOND_PREIMAGE_LENGTH, &stream);

    uint8_t target256[32];
    uint8_t target512[64];
    if (!hash_both(
            target,
            SECOND_PREIMAGE_LENGTH,
            target256,
            target512,
            NULL
        )) {
        free(target);
        free(candidate);
        return 0;
    }

    int minimum256 = 256;
    int minimum512 = 512;
    uint64_t total256 = 0;
    uint64_t total512 = 0;
    unsigned int matches256 = 0;
    unsigned int matches512 = 0;

    for (unsigned int sample = 0;
         sample < SECOND_PREIMAGE_CANDIDATES;
         sample++) {
        uint8_t digest256[32];
        uint8_t digest512[64];

        mutate_second_preimage(
            candidate,
            target,
            SECOND_PREIMAGE_LENGTH,
            sample
        );
        if (!hash_both(
                candidate,
                SECOND_PREIMAGE_LENGTH,
                digest256,
                digest512,
                NULL
            )) {
            free(target);
            free(candidate);
            return 0;
        }

        int distance256 = bit_diff(
            target256,
            digest256,
            sizeof(target256)
        );
        int distance512 = bit_diff(
            target512,
            digest512,
            sizeof(target512)
        );
        if (distance256 == 0)
            matches256++;
        if (distance512 == 0)
            matches512++;
        if (distance256 < minimum256)
            minimum256 = distance256;
        if (distance512 < minimum512)
            minimum512 = distance512;
        total256 += (uint64_t)distance256;
        total512 += (uint64_t)distance512;
    }

    double average256 =
        (double)total256 /
        ((double)SECOND_PREIMAGE_CANDIDATES * 256.0) * 100.0;
    double average512 =
        (double)total512 /
        ((double)SECOND_PREIMAGE_CANDIDATES * 512.0) * 100.0;
    int ok =
        matches256 == 0u &&
        matches512 == 0u &&
        minimum256 >= 64 &&
        minimum512 >= 160 &&
        average256 >= 47.0 && average256 <= 53.0 &&
        average512 >= 47.0 && average512 <= 53.0;

    printf(
        "second_preimage,candidates=%u,modes=%u,avg256=%.2f,"
        "min256=%d,matches256=%u,avg512=%.2f,"
        "min512=%d,matches512=%u,%s\n",
        SECOND_PREIMAGE_CANDIDATES,
        SECOND_PREIMAGE_MODES,
        average256,
        minimum256,
        matches256,
        average512,
        minimum512,
        matches512,
        ok ? "PASS" : "FAIL"
    );

    free(target);
    free(candidate);
    return ok;
}

static int record_long_variant(
    const uint8_t *message,
    size_t length,
    uint8_t output256[32],
    uint8_t output512[64],
    int *maximum_depth
) {
    int depth = 0;
    if (!hash_both(
            message,
            length,
            output256,
            output512,
            &depth
        ))
        return 0;
    if (depth > *maximum_depth)
        *maximum_depth = depth;
    return 1;
}

static int long_message_screen(void) {
    uint8_t *base = (uint8_t *)malloc(LONG_MESSAGE_LENGTH);
    uint8_t *work = (uint8_t *)malloc(LONG_MESSAGE_LENGTH + 128u);
    if (!base || !work) {
        free(base);
        free(work);
        return 0;
    }

    uint64_t stream = UINT64_C(0x10A65EED7E570001);
    fill_bytes(base, LONG_MESSAGE_LENGTH, &stream);

    uint8_t digests256[LONG_VARIANTS][32];
    uint8_t digests512[LONG_VARIANTS][64];
    int maximum_depth = 0;
    size_t variant = 0;

#define RECORD_LONG(_data, _length) \
    do { \
        if (!record_long_variant( \
                (_data), \
                (_length), \
                digests256[variant], \
                digests512[variant], \
                &maximum_depth \
            )) { \
            free(base); \
            free(work); \
            return 0; \
        } \
        variant++; \
    } while (0)

    RECORD_LONG(base, LONG_MESSAGE_LENGTH);

    memcpy(work, base, LONG_MESSAGE_LENGTH);
    work[LONG_MESSAGE_LENGTH] = 0x01u;
    RECORD_LONG(work, LONG_MESSAGE_LENGTH + 1u);

    memcpy(work, base, LONG_MESSAGE_LENGTH);
    uint64_t suffix_stream = UINT64_C(0x5AFF1CE5EED00001);
    fill_bytes(
        work + LONG_MESSAGE_LENGTH,
        ATTACK_CHUNK_SIZE,
        &suffix_stream
    );
    RECORD_LONG(work, LONG_MESSAGE_LENGTH + ATTACK_CHUNK_SIZE);

    RECORD_LONG(base, LONG_MESSAGE_LENGTH - 1u);
    RECORD_LONG(base, LONG_MESSAGE_LENGTH - ATTACK_CHUNK_SIZE);

    uint64_t prefix_stream = UINT64_C(0x9AEF1A5EED000001);
    fill_bytes(work, ATTACK_CHUNK_SIZE, &prefix_stream);
    memcpy(
        work + ATTACK_CHUNK_SIZE,
        base,
        LONG_MESSAGE_LENGTH
    );
    RECORD_LONG(work, LONG_MESSAGE_LENGTH + ATTACK_CHUNK_SIZE);

    uint8_t extension[32];
    uint64_t extension_stream = UINT64_C(0xE17E6510A5EED001);
    fill_bytes(extension, sizeof(extension), &extension_stream);
    memcpy(work, base, LONG_MESSAGE_LENGTH);
    work[LONG_MESSAGE_LENGTH] = 0x80u;
    fch_store_le64(
        work + LONG_MESSAGE_LENGTH + 1u,
        (uint64_t)LONG_MESSAGE_LENGTH * 8u
    );
    memcpy(
        work + LONG_MESSAGE_LENGTH + 9u,
        extension,
        sizeof(extension)
    );
    RECORD_LONG(
        work,
        LONG_MESSAGE_LENGTH + 9u + sizeof(extension)
    );

    memcpy(work, base, LONG_MESSAGE_LENGTH);
    memcpy(
        work + LONG_MESSAGE_LENGTH,
        extension,
        sizeof(extension)
    );
    RECORD_LONG(work, LONG_MESSAGE_LENGTH + sizeof(extension));

    memcpy(work, base, LONG_MESSAGE_LENGTH);
    uint8_t temporary[LONG_CHUNK_SIZE];
    memcpy(temporary, work + LONG_CHUNK_SIZE, sizeof(temporary));
    memcpy(
        work + LONG_CHUNK_SIZE,
        work + LONG_MESSAGE_LENGTH - LONG_CHUNK_SIZE * 2u,
        LONG_CHUNK_SIZE
    );
    memcpy(
        work + LONG_MESSAGE_LENGTH - LONG_CHUNK_SIZE * 2u,
        temporary,
        sizeof(temporary)
    );
    RECORD_LONG(work, LONG_MESSAGE_LENGTH);

    memcpy(work, base, LONG_MESSAGE_LENGTH);
    memcpy(
        work + LONG_CHUNK_SIZE * 3u,
        base + LONG_CHUNK_SIZE * 7u,
        LONG_CHUNK_SIZE
    );
    RECORD_LONG(work, LONG_MESSAGE_LENGTH);

    memcpy(work, base, LONG_MESSAGE_LENGTH);
    memcpy(temporary, work, sizeof(temporary));
    memmove(
        work,
        work + LONG_CHUNK_SIZE,
        LONG_MESSAGE_LENGTH - LONG_CHUNK_SIZE
    );
    memcpy(
        work + LONG_MESSAGE_LENGTH - LONG_CHUNK_SIZE,
        temporary,
        sizeof(temporary)
    );
    RECORD_LONG(work, LONG_MESSAGE_LENGTH);

    memcpy(work, base, LONG_MESSAGE_LENGTH);
    static const size_t boundary_positions[] = {
        63u, 64u, 127u, 128u,
        LONG_MESSAGE_LENGTH - 65u,
        LONG_MESSAGE_LENGTH - 64u
    };
    for (size_t i = 0;
         i < sizeof(boundary_positions) /
             sizeof(boundary_positions[0]);
         i++)
        work[boundary_positions[i]] ^= (uint8_t)(1u << (i % 8u));
    RECORD_LONG(work, LONG_MESSAGE_LENGTH);

    memset(work, 0, LONG_MESSAGE_LENGTH);
    RECORD_LONG(work, LONG_MESSAGE_LENGTH);

    memset(work, 0xFF, LONG_MESSAGE_LENGTH);
    RECORD_LONG(work, LONG_MESSAGE_LENGTH);

    for (size_t i = 0; i < LONG_MESSAGE_LENGTH; i++)
        work[i] = (uint8_t)(i * 131u + i / 17u);
    RECORD_LONG(work, LONG_MESSAGE_LENGTH);

#undef RECORD_LONG

    if (variant != LONG_VARIANTS) {
        free(base);
        free(work);
        return 0;
    }

    int minimum256 = 256;
    int minimum512 = 512;
    unsigned int collisions256 = 0;
    unsigned int collisions512 = 0;
    for (size_t a = 0; a < LONG_VARIANTS; a++) {
        for (size_t b = a + 1u; b < LONG_VARIANTS; b++) {
            int distance256 = bit_diff(
                digests256[a],
                digests256[b],
                sizeof(digests256[a])
            );
            int distance512 = bit_diff(
                digests512[a],
                digests512[b],
                sizeof(digests512[a])
            );
            if (distance256 == 0)
                collisions256++;
            if (distance512 == 0)
                collisions512++;
            if (distance256 < minimum256)
                minimum256 = distance256;
            if (distance512 < minimum512)
                minimum512 = distance512;
        }
    }

    int ok =
        collisions256 == 0u &&
        collisions512 == 0u &&
        minimum256 >= 64 &&
        minimum512 >= 160 &&
        maximum_depth == 9;
    printf(
        "long_message,bytes=%u,variants=%u,max_depth=%d,"
        "min256=%d,min512=%d,collisions256=%u,"
        "collisions512=%u,%s\n",
        LONG_MESSAGE_LENGTH,
        LONG_VARIANTS,
        maximum_depth,
        minimum256,
        minimum512,
        collisions256,
        collisions512,
        ok ? "PASS" : "FAIL"
    );

    free(base);
    free(work);
    return ok;
}

static int expandable_message_screen(void) {
    const size_t maximum_length =
        FCH_TREE_LEAF_BYTES * (1u + EXPANDABLE_MAX_EXTRA_LEAVES) +
        EXPANDABLE_SUFFIX_LENGTH;
    uint8_t *short_message = (uint8_t *)malloc(maximum_length);
    uint8_t *long_message = (uint8_t *)malloc(maximum_length);
    uint8_t suffix[EXPANDABLE_SUFFIX_LENGTH];
    if (!short_message || !long_message) {
        free(short_message);
        free(long_message);
        return 0;
    }

    uint64_t suffix_stream = UINT64_C(0xE7AADA81E5EED001);
    uint64_t message_stream = UINT64_C(0xE7AADA81E5EED002);
    fill_bytes(suffix, sizeof(suffix), &suffix_stream);

    int minimum_root = 512;
    int minimum256 = 256;
    int minimum512 = 512;
    unsigned int root_matches = 0;
    unsigned int matches256 = 0;
    unsigned int matches512 = 0;

    for (unsigned int sample = 0; sample < EXPANDABLE_PAIRS; sample++) {
        size_t prefix_length = FCH_TREE_LEAF_BYTES;
        size_t extra_length = FCH_TREE_LEAF_BYTES *
            (1u + sample % EXPANDABLE_MAX_EXTRA_LEAVES);
        size_t short_length = prefix_length + sizeof(suffix);
        size_t long_length = short_length + extra_length;
        uint8_t short256[32];
        uint8_t long256[32];
        uint8_t short512[64];
        uint8_t long512[64];
        uint64_t short_root256[FCH_INTERNAL_STATE_WORDS];
        uint64_t short_root512[FCH_INTERNAL_STATE_WORDS];
        uint64_t long_root256[FCH_INTERNAL_STATE_WORDS];
        uint64_t long_root512[FCH_INTERNAL_STATE_WORDS];

        fill_bytes(short_message, prefix_length, &message_stream);
        fch_store_le64(short_message, sample);
        memcpy(
            short_message + prefix_length,
            suffix,
            sizeof(suffix)
        );
        memcpy(long_message, short_message, prefix_length);
        fill_bytes(
            long_message + prefix_length,
            extra_length,
            &message_stream
        );
        memcpy(
            long_message + prefix_length + extra_length,
            suffix,
            sizeof(suffix)
        );

        if (!hash_both_capture(
                short_message,
                short_length,
                short256,
                short512,
                short_root256,
                short_root512,
                NULL
            ) ||
            !hash_both_capture(
                long_message,
                long_length,
                long256,
                long512,
                long_root256,
                long_root512,
                NULL
            ) ||
            memcmp(
                short_root256,
                short_root512,
                sizeof(short_root256)
            ) != 0 ||
            memcmp(
                long_root256,
                long_root512,
                sizeof(long_root256)
            ) != 0) {
            free(short_message);
            free(long_message);
            return 0;
        }

        int root_distance = bit_diff(
            (const uint8_t *)short_root256,
            (const uint8_t *)long_root256,
            sizeof(short_root256)
        );
        int distance256 = bit_diff(
            short256,
            long256,
            sizeof(short256)
        );
        int distance512 = bit_diff(
            short512,
            long512,
            sizeof(short512)
        );
        if (root_distance == 0)
            root_matches++;
        if (distance256 == 0)
            matches256++;
        if (distance512 == 0)
            matches512++;
        if (root_distance < minimum_root)
            minimum_root = root_distance;
        if (distance256 < minimum256)
            minimum256 = distance256;
        if (distance512 < minimum512)
            minimum512 = distance512;
    }

    int ok =
        root_matches == 0u &&
        matches256 == 0u &&
        matches512 == 0u &&
        minimum_root >= 160 &&
        minimum256 >= 64 &&
        minimum512 >= 160;
    printf(
        "expandable_message,pairs=%u,shared_suffix=%u,"
        "extra_leaves=1-%u,min_root=%d,root_matches=%u,"
        "min256=%d,matches256=%u,min512=%d,matches512=%u,%s\n",
        EXPANDABLE_PAIRS,
        EXPANDABLE_SUFFIX_LENGTH,
        EXPANDABLE_MAX_EXTRA_LEAVES,
        minimum_root,
        root_matches,
        minimum256,
        matches256,
        minimum512,
        matches512,
        ok ? "PASS" : "FAIL"
    );

    free(short_message);
    free(long_message);
    return ok;
}

static int herding_screen(void) {
    uint8_t message[HERDING_MESSAGE_LENGTH];
    uint8_t suffix[HERDING_MESSAGE_LENGTH - FCH_TREE_LEAF_BYTES];
    state_record_t roots[HERDING_PREFIXES];
    uint8_t digests256[HERDING_PREFIXES][32];
    uint8_t digests512[HERDING_PREFIXES][64];
    uint64_t suffix_stream = UINT64_C(0x4E2D1A65EED00001);
    fill_bytes(suffix, sizeof(suffix), &suffix_stream);

    for (unsigned int prefix = 0; prefix < HERDING_PREFIXES; prefix++) {
        uint64_t prefix_stream =
            UINT64_C(0x4E2D1A65EED10000) ^ prefix;
        uint64_t root512[FCH_INTERNAL_STATE_WORDS];
        fill_bytes(message, FCH_TREE_LEAF_BYTES, &prefix_stream);
        fch_store_le64(message, prefix);
        memcpy(
            message + FCH_TREE_LEAF_BYTES,
            suffix,
            sizeof(suffix)
        );
        if (!hash_both_capture(
                message,
                sizeof(message),
                digests256[prefix],
                digests512[prefix],
                roots[prefix].words,
                root512,
                NULL
            ) ||
            memcmp(
                roots[prefix].words,
                root512,
                sizeof(root512)
            ) != 0)
            return 0;
        roots[prefix].tree = (fch_tree_position_t){0, 0, 0, 0, 0};
    }

    collision_stats_t root_stats;
    if (!collision_stats(roots, HERDING_PREFIXES, &root_stats))
        return 0;

    int minimum256 = 256;
    int minimum512 = 512;
    unsigned int collisions256 = 0;
    unsigned int collisions512 = 0;
    for (size_t a = 0; a < HERDING_PREFIXES; a++) {
        for (size_t b = a + 1u; b < HERDING_PREFIXES; b++) {
            int distance256 = bit_diff(
                digests256[a],
                digests256[b],
                sizeof(digests256[a])
            );
            int distance512 = bit_diff(
                digests512[a],
                digests512[b],
                sizeof(digests512[a])
            );
            if (distance256 == 0)
                collisions256++;
            if (distance512 == 0)
                collisions512++;
            if (distance256 < minimum256)
                minimum256 = distance256;
            if (distance512 < minimum512)
                minimum512 = distance512;
        }
    }

    int ok =
        root_stats.exact_collisions == 0u &&
        root_stats.maximum_bucket <= 3u &&
        collisions256 == 0u &&
        collisions512 == 0u &&
        minimum256 >= 64 &&
        minimum512 >= 160;
    printf(
        "herding,prefix_bits=%u,prefixes=%u,fixed_suffix=%u,"
        "root_prefix_pairs=%llu,root_max_bucket=%u,root_exact=%u,"
        "min256=%d,collisions256=%u,min512=%d,collisions512=%u,%s\n",
        HERDING_PREFIX_BITS,
        HERDING_PREFIXES,
        (unsigned int)sizeof(suffix),
        (unsigned long long)root_stats.truncated_pairs,
        (unsigned int)root_stats.maximum_bucket,
        (unsigned int)root_stats.exact_collisions,
        minimum256,
        collisions256,
        minimum512,
        collisions512,
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

static int multi_target_screen(void) {
    uint8_t *targets = (uint8_t *)malloc(
        (size_t)MULTI_TARGET_TARGETS * MULTI_TARGET_LENGTH
    );
    uint8_t *candidate = (uint8_t *)malloc(MULTI_TARGET_LENGTH);
    uint8_t target256[MULTI_TARGET_TARGETS][32];
    uint8_t target512[MULTI_TARGET_TARGETS][64];
    if (!targets || !candidate) {
        free(targets);
        free(candidate);
        return 0;
    }

    for (unsigned int target = 0;
         target < MULTI_TARGET_TARGETS;
         target++) {
        uint8_t *message =
            targets + (size_t)target * MULTI_TARGET_LENGTH;
        uint64_t stream =
            UINT64_C(0xAD1717A26E5EED00) ^ target;
        fill_bytes(message, MULTI_TARGET_LENGTH, &stream);
        fch_store_le64(message, target);
        if (!hash_both(
                message,
                MULTI_TARGET_LENGTH,
                target256[target],
                target512[target],
                NULL
            )) {
            free(targets);
            free(candidate);
            return 0;
        }
    }

    int minimum256 = 256;
    int minimum512 = 512;
    unsigned int matches256 = 0;
    unsigned int matches512 = 0;
    for (unsigned int sample = 0;
         sample < MULTI_TARGET_CANDIDATES;
         sample++) {
        unsigned int base = sample % MULTI_TARGET_TARGETS;
        const uint8_t *base_message =
            targets + (size_t)base * MULTI_TARGET_LENGTH;
        uint8_t digest256[32];
        uint8_t digest512[64];
        mutate_second_preimage(
            candidate,
            base_message,
            MULTI_TARGET_LENGTH,
            sample
        );

        for (unsigned int target = 0;
             target < MULTI_TARGET_TARGETS;
             target++) {
            if (memcmp(
                    candidate,
                    targets + (size_t)target * MULTI_TARGET_LENGTH,
                    MULTI_TARGET_LENGTH
                ) == 0) {
                free(targets);
                free(candidate);
                return 0;
            }
        }

        if (!hash_both(
                candidate,
                MULTI_TARGET_LENGTH,
                digest256,
                digest512,
                NULL
            )) {
            free(targets);
            free(candidate);
            return 0;
        }

        for (unsigned int target = 0;
             target < MULTI_TARGET_TARGETS;
             target++) {
            int distance256 = bit_diff(
                digest256,
                target256[target],
                sizeof(digest256)
            );
            int distance512 = bit_diff(
                digest512,
                target512[target],
                sizeof(digest512)
            );
            if (distance256 == 0)
                matches256++;
            if (distance512 == 0)
                matches512++;
            if (distance256 < minimum256)
                minimum256 = distance256;
            if (distance512 < minimum512)
                minimum512 = distance512;
        }
    }

    unsigned int comparisons =
        MULTI_TARGET_TARGETS * MULTI_TARGET_CANDIDATES;
    int ok =
        matches256 == 0u &&
        matches512 == 0u &&
        minimum256 >= 64 &&
        minimum512 >= 150;
    printf(
        "multi_target,targets=%u,candidates=%u,comparisons=%u,"
        "min256=%d,matches256=%u,min512=%d,matches512=%u,%s\n",
        MULTI_TARGET_TARGETS,
        MULTI_TARGET_CANDIDATES,
        comparisons,
        minimum256,
        matches256,
        minimum512,
        matches512,
        ok ? "PASS" : "FAIL"
    );

    free(targets);
    free(candidate);
    return ok;
}

static int variant_reuse_screen(void) {
    uint8_t *message = (uint8_t *)malloc(VARIANT_REUSE_MAX_LENGTH);
    if (!message)
        return 0;

    unsigned int shared_roots = 0;
    unsigned int prefix_matches = 0;
    unsigned int suffix_matches = 0;
    int minimum_prefix = 256;
    int minimum_suffix = 256;
    uint64_t total_prefix = 0;
    uint64_t total_suffix = 0;

    for (unsigned int sample = 0;
         sample < VARIANT_REUSE_SAMPLES;
         sample++) {
        size_t length =
            ((size_t)sample * 4051u) %
            (VARIANT_REUSE_MAX_LENGTH + 1u);
        uint64_t stream =
            UINT64_C(0xA261AA7A5EED0001) ^ sample;
        uint8_t digest256[32];
        uint8_t digest512[64];
        uint64_t root256[FCH_INTERNAL_STATE_WORDS];
        uint64_t root512[FCH_INTERNAL_STATE_WORDS];
        fill_bytes(message, length, &stream);
        if (length >= 8u)
            fch_store_le64(message, sample);

        if (!hash_both_capture(
                message,
                length,
                digest256,
                digest512,
                root256,
                root512,
                NULL
            )) {
            free(message);
            return 0;
        }

        if (memcmp(root256, root512, sizeof(root256)) == 0)
            shared_roots++;
        int prefix_distance = bit_diff(
            digest256,
            digest512,
            sizeof(digest256)
        );
        int suffix_distance = bit_diff(
            digest256,
            digest512 + sizeof(digest256),
            sizeof(digest256)
        );
        if (prefix_distance == 0)
            prefix_matches++;
        if (suffix_distance == 0)
            suffix_matches++;
        if (prefix_distance < minimum_prefix)
            minimum_prefix = prefix_distance;
        if (suffix_distance < minimum_suffix)
            minimum_suffix = suffix_distance;
        total_prefix += (uint64_t)prefix_distance;
        total_suffix += (uint64_t)suffix_distance;
    }

    double average_prefix =
        (double)total_prefix /
        ((double)VARIANT_REUSE_SAMPLES * 256.0) * 100.0;
    double average_suffix =
        (double)total_suffix /
        ((double)VARIANT_REUSE_SAMPLES * 256.0) * 100.0;
    int ok =
        shared_roots == VARIANT_REUSE_SAMPLES &&
        prefix_matches == 0u &&
        suffix_matches == 0u &&
        minimum_prefix >= 64 &&
        minimum_suffix >= 64 &&
        average_prefix >= 47.0 && average_prefix <= 53.0 &&
        average_suffix >= 47.0 && average_suffix <= 53.0;
    printf(
        "variant_reuse,samples=%u,shared_preoutput_roots=%u,"
        "prefix_avg=%.2f,prefix_min=%d,prefix_matches=%u,"
        "suffix_avg=%.2f,suffix_min=%d,suffix_matches=%u,%s\n",
        VARIANT_REUSE_SAMPLES,
        shared_roots,
        average_prefix,
        minimum_prefix,
        prefix_matches,
        average_suffix,
        minimum_suffix,
        suffix_matches,
        ok ? "PASS" : "FAIL"
    );

    free(message);
    return ok;
}

int main(void) {
    int ok = 1;

    if (!multicollision_screen())
        ok = 0;
    if (!tree_shape_screen())
        ok = 0;
    if (!canonical_partition_screen())
        ok = 0;
    if (!subtree_replacement_screen())
        ok = 0;
    if (!second_preimage_screen())
        ok = 0;
    if (!long_message_screen())
        ok = 0;
    if (!expandable_message_screen())
        ok = 0;
    if (!herding_screen())
        ok = 0;
    if (!multi_target_screen())
        ok = 0;
    if (!variant_reuse_screen())
        ok = 0;

    if (!ok) {
        fprintf(stderr, "TREE_ATTACKS: FAIL\n");
        return 1;
    }

    printf(
        "TREE_ATTACKS: PASS "
        "(bounded regression checks; not a security proof)\n"
    );
    return 0;
}
