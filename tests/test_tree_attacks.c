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
    ATTACK_CHUNK_SIZE = 64,
    LONG_MESSAGE_LENGTH = 262144,
    LONG_CHUNK_SIZE = 4096,
    LONG_VARIANTS = 15
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

void fch_debug_hook(
    fch_hook_point_t point,
    int depth,
    const uint64_t *state,
    size_t state_words
) {
    (void)point;
    (void)state;
    (void)state_words;

    if (depth > g_current_max_depth)
        g_current_max_depth = depth;
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

static int hash_both(
    const uint8_t *message,
    size_t length,
    uint8_t output256[32],
    uint8_t output512[64],
    int *maximum_depth
) {
    g_current_max_depth = 0;
    if (!fch_hash_256_checked(message, length, output256) ||
        !fch_hash_512_checked(message, length, output512))
        return 0;

    if (maximum_depth)
        *maximum_depth = g_current_max_depth;
    return 1;
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

static fch_state_t state_view(state_record_t *record) {
    fch_state_t view = {
        record ? record->words : NULL,
        FCH_INTERNAL_STATE_WORDS,
        record ? record->tree : (fch_tree_position_t){0, 0, 0, 0, 0}
    };
    return view;
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
    if (!leaves || !nodes) {
        free(leaves);
        free(nodes);
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
        return 0;
    }

    const fch_block_t blocks[2] = {
        { 0u, FCH_TREE_LEAF_BYTES },
        { FCH_TREE_LEAF_BYTES, FCH_TREE_LEAF_BYTES }
    };
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
                blocks,
                2,
                FCH_TREE_LEAF_BYTES * 2u,
                1,
                &nodes[sample]
            )) {
            generated = 0;
            break;
        }
    }

    collision_stats_t leaf_stats;
    collision_stats_t node_stats;
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
        );

    if (ok) {
        ok =
            leaf_stats.exact_collisions == 0u &&
            node_stats.exact_collisions == 0u &&
            leaf_stats.truncated_pairs <= 64u &&
            node_stats.truncated_pairs <= 64u &&
            leaf_stats.maximum_bucket <= 4u &&
            node_stats.maximum_bucket <= 4u;

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
    }

    free(leaves);
    free(nodes);
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

static void mutate_second_preimage(
    uint8_t *candidate,
    const uint8_t *target,
    size_t length,
    unsigned int sample
) {
    memcpy(candidate, target, length);
    const size_t chunk_count = length / ATTACK_CHUNK_SIZE;

    switch (sample % 4u) {
        case 0: {
            size_t position =
                ((size_t)sample * 2654435761u + 17u) % length;
            candidate[position] ^=
                (uint8_t)(1u << ((sample / 4u) % 8u));
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
        default: {
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
        "second_preimage,candidates=%u,avg256=%.2f,"
        "min256=%d,matches256=%u,avg512=%.2f,"
        "min512=%d,matches512=%u,%s\n",
        SECOND_PREIMAGE_CANDIDATES,
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

int main(void) {
    int ok = 1;

    if (!multicollision_screen())
        ok = 0;
    if (!tree_shape_screen())
        ok = 0;
    if (!second_preimage_screen())
        ok = 0;
    if (!long_message_screen())
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
