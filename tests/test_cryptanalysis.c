#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitops.h"
#include "fch.h"
#include "mix.h"
#include "test_utils.h"

enum {
    DIFFERENTIAL_SAMPLES = 2048,
    LINEAR_SAMPLES = 8192,
    LINEAR_MASKS = 32,
    FIXED_POINT_SAMPLES = 4096,
    REDUCED_BASES = 4,
    TRAIL_BASES = 4,
    TRAIL_PATTERN_COUNT = 6,
    TRAIL_ROUND_COUNT = 16,
    ARX_PAIR_SAMPLES = 512,
    ARX_ROTATION_COUNT = 6,
    ARX_ADDITIVE_CASES = 4,
    DIFFERENTIAL_PROBABILITY_SAMPLES = 4096,
    DIFFERENTIAL_PROBABILITY_CASES = 8,
    DIFFERENTIAL_PROJECTIONS = 4,
    RELATED_CONTEXT_SAMPLES = 512,
    RELATED_CONTEXT_CASES = 8,
    REBOUND_CANDIDATES = 4096,
    REBOUND_CASES = 3,
    MITM_CANDIDATES = 4096,
    MITM_PREFIX_BITS = 24,
    NEAR_COLLISION_SAMPLES = 2048,
    NEAR_COLLISION_MESSAGE_SIZE = 64
};

typedef struct {
    uint64_t total_weight;
    int minimum_weight;
    int maximum_weight;
    unsigned int minimum_active_words;
    unsigned int zero_differences;
    unsigned int best_base;
    unsigned int best_pattern;
    size_t best_bit_a;
    size_t best_bit_b;
} trail_stats_t;

typedef struct {
    uint32_t key;
    unsigned int candidate;
    uint64_t state[16];
} mitm_entry_t;

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

static unsigned int parity64(uint64_t value) {
    unsigned int parity = 0;

    while (value != 0u) {
        parity ^= 1u;
        value &= value - 1u;
    }
    return parity;
}

static int core_output(
    const uint8_t block[FCH_MIX_BLOCK_SIZE],
    uint64_t counter,
    uint64_t domain,
    uint64_t flags,
    unsigned int rounds,
    uint64_t output[8]
) {
    if (!fch_mix_init(output, 8u, domain))
        return 0;

    return fch_mix_compress_rounds(
        output,
        8u,
        block,
        FCH_MIX_BLOCK_SIZE,
        counter,
        domain,
        flags,
        rounds
    );
}

static int differential_bias_check(void) {
    static const size_t input_differences[] = { 0u, 63u, 511u, 1023u };
    static const unsigned int round_counts[] = {
        FCH_MIX_REDUCED_ROUND_REFERENCE,
        FCH_MIX_ROUNDS
    };
    int all_ok = 1;

    printf("differential,rounds,avg,min,max,max_bit_bias,status\n");
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        unsigned int rounds = round_counts[ri];
        double worst_bias = 0.0;
        uint64_t total_weight = 0;
        int minimum_weight = 512;
        int maximum_weight = 0;

        for (size_t di = 0;
             di < sizeof(input_differences) / sizeof(input_differences[0]);
             di++) {
            uint32_t flip_counts[512] = {0};
            uint64_t stream =
                UINT64_C(0xD1FF3E7E5A17B1A5) ^
                (uint64_t)input_differences[di];

            for (unsigned int sample = 0;
                 sample < DIFFERENTIAL_SAMPLES;
                 sample++) {
                uint8_t base[FCH_MIX_BLOCK_SIZE];
                uint8_t changed[FCH_MIX_BLOCK_SIZE];
                uint64_t output_a[8];
                uint64_t output_b[8];
                size_t difference = input_differences[di];

                fill_bytes(base, sizeof(base), &stream);
                memcpy(changed, base, sizeof(changed));
                changed[difference / 8u] ^=
                    (uint8_t)(1u << (unsigned int)(difference % 8u));

                if (!core_output(
                        base,
                        sample,
                        UINT64_C(0x4449464645523031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        output_a
                    ) ||
                    !core_output(
                        changed,
                        sample,
                        UINT64_C(0x4449464645523031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        output_b
                    ))
                    return 0;

                int weight = bit_diff(
                    (const uint8_t *)output_a,
                    (const uint8_t *)output_b,
                    sizeof(output_a)
                );
                if (weight == 0)
                    return 0;
                total_weight += (uint64_t)weight;
                if (weight < minimum_weight)
                    minimum_weight = weight;
                if (weight > maximum_weight)
                    maximum_weight = weight;

                for (size_t word = 0; word < 8u; word++) {
                    uint64_t difference_word =
                        output_a[word] ^ output_b[word];
                    for (size_t bit = 0; bit < 64u; bit++) {
                        flip_counts[word * 64u + bit] +=
                            (uint32_t)((difference_word >> bit) & 1u);
                    }
                }
            }

            for (size_t bit = 0; bit < 512u; bit++) {
                double probability =
                    (double)flip_counts[bit] /
                    (double)DIFFERENTIAL_SAMPLES * 100.0;
                double bias = probability >= 50.0
                    ? probability - 50.0
                    : 50.0 - probability;
                if (bias > worst_bias)
                    worst_bias = bias;
            }
        }

        const size_t difference_count =
            sizeof(input_differences) / sizeof(input_differences[0]);
        double average = (double)total_weight /
            ((double)DIFFERENTIAL_SAMPLES *
             (double)difference_count * 512.0) * 100.0;
        double minimum =
            (double)minimum_weight / 512.0 * 100.0;
        double maximum =
            (double)maximum_weight / 512.0 * 100.0;
        int ok =
            average >= 47.0 && average <= 53.0 &&
            minimum_weight >= 160 &&
            maximum_weight <= 352 &&
            worst_bias <= 7.0;

        printf(
            "differential,%u,%.2f,%.2f,%.2f,%.2f,%s\n",
            rounds,
            average,
            minimum,
            maximum,
            worst_bias,
            ok ? "PASS" : "FAIL"
        );
        if (!ok)
            all_ok = 0;
    }

    return all_ok;
}

static int linear_correlation_check(void) {
    static const unsigned int round_counts[] = {
        FCH_MIX_REDUCED_ROUND_REFERENCE,
        FCH_MIX_ROUNDS
    };
    uint64_t output_masks_a[LINEAR_MASKS];
    uint64_t output_masks_b[LINEAR_MASKS];
    uint64_t mask_stream = UINT64_C(0x11E4A7C0DE5EED01);
    int all_ok = 1;

    for (size_t mask = 0; mask < LINEAR_MASKS; mask++) {
        output_masks_a[mask] = splitmix64_next(&mask_stream) | 1u;
        output_masks_b[mask] = splitmix64_next(&mask_stream) | 1u;
    }

    printf("linear,rounds,max_abs_correlation,status\n");
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        int correlations[LINEAR_MASKS] = {0};
        uint64_t stream = UINT64_C(0x1A2B3C4D5E6F7081);
        unsigned int rounds = round_counts[ri];

        for (unsigned int sample = 0; sample < LINEAR_SAMPLES; sample++) {
            uint8_t block[FCH_MIX_BLOCK_SIZE];
            uint64_t output[8];

            fill_bytes(block, sizeof(block), &stream);
            if (!core_output(
                    block,
                    sample,
                    UINT64_C(0x4C494E4541523031),
                    FCH_MIX_FLAG_LEAF_DATA,
                    rounds,
                    output
                ))
                return 0;

            for (size_t mask = 0; mask < LINEAR_MASKS; mask++) {
                size_t input_bit_a = (mask * 97u + 3u) % 1024u;
                size_t input_bit_b = (mask * 193u + 29u) % 1024u;
                unsigned int input_parity =
                    ((block[input_bit_a / 8u] >>
                      (input_bit_a % 8u)) & 1u) ^
                    ((block[input_bit_b / 8u] >>
                      (input_bit_b % 8u)) & 1u);
                unsigned int output_parity =
                    parity64(
                        output[mask % 8u] & output_masks_a[mask]
                    ) ^
                    parity64(
                        output[(mask * 5u + 1u) % 8u] &
                        output_masks_b[mask]
                    );
                correlations[mask] +=
                    input_parity == output_parity ? 1 : -1;
            }
        }

        double maximum_correlation = 0.0;
        for (size_t mask = 0; mask < LINEAR_MASKS; mask++) {
            int absolute = correlations[mask] < 0
                ? -correlations[mask]
                : correlations[mask];
            double correlation =
                (double)absolute / (double)LINEAR_SAMPLES * 100.0;
            if (correlation > maximum_correlation)
                maximum_correlation = correlation;
        }

        int ok = maximum_correlation <= 8.0;
        printf(
            "linear,%u,%.2f,%s\n",
            rounds,
            maximum_correlation,
            ok ? "PASS" : "FAIL"
        );
        if (!ok)
            all_ok = 0;
    }

    return all_ok;
}

static int reduced_round_search(void) {
    int all_ok = 1;

    printf("round_comparison,rounds,avg,min,zero_differences,status\n");
    for (unsigned int rounds = 1u;
         rounds <= FCH_MIX_ROUNDS;
         rounds++) {
        uint64_t stream = UINT64_C(0xA77AC5EED5EED001);
        uint64_t total_weight = 0;
        int minimum_weight = 512;
        unsigned int zero_differences = 0;

        for (unsigned int base_index = 0;
             base_index < REDUCED_BASES;
             base_index++) {
            uint8_t base[FCH_MIX_BLOCK_SIZE];
            uint64_t base_output[8];

            fill_bytes(base, sizeof(base), &stream);
            if (!core_output(
                    base,
                    base_index,
                    UINT64_C(0x5245445543453031),
                    FCH_MIX_FLAG_LEAF_DATA,
                    rounds,
                    base_output
                ))
                return 0;

            for (size_t bit = 0;
                 bit < FCH_MIX_BLOCK_SIZE * 8u;
                 bit++) {
                uint8_t changed[FCH_MIX_BLOCK_SIZE];
                uint64_t changed_output[8];

                memcpy(changed, base, sizeof(changed));
                changed[bit / 8u] ^=
                    (uint8_t)(1u << (unsigned int)(bit % 8u));
                if (!core_output(
                        changed,
                        base_index,
                        UINT64_C(0x5245445543453031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        changed_output
                    ))
                    return 0;

                int weight = bit_diff(
                    (const uint8_t *)base_output,
                    (const uint8_t *)changed_output,
                    sizeof(base_output)
                );
                if (weight == 0)
                    zero_differences++;
                if (weight < minimum_weight)
                    minimum_weight = weight;
                total_weight += (uint64_t)weight;
            }
        }

        const double comparisons =
            (double)REDUCED_BASES *
            (double)FCH_MIX_BLOCK_SIZE * 8.0;
        double average =
            (double)total_weight / (comparisons * 512.0) * 100.0;
        double minimum =
            (double)minimum_weight / 512.0 * 100.0;
        int acceptable = zero_differences == 0u;
        if (rounds >= 2u) {
            acceptable = acceptable &&
                average >= 47.0 && average <= 53.0 &&
                minimum_weight >= 160;
        }

        printf(
            "round_comparison,%u,%.2f,%.2f,%u,%s\n",
            rounds,
            average,
            minimum,
            zero_differences,
            acceptable ? (rounds == 1u ? "WEAK" : "PASS") : "FAIL"
        );
        if (!acceptable)
            all_ok = 0;
    }

    return all_ok;
}

static unsigned int active_difference_words(
    const uint64_t a[8],
    const uint64_t b[8]
) {
    unsigned int active = 0;

    for (size_t word = 0; word < 8u; word++) {
        if ((a[word] ^ b[word]) != 0u)
            active++;
    }
    return active;
}

static const char *trail_pattern_name(unsigned int pattern) {
    static const char *names[TRAIL_PATTERN_COUNT] = {
        "single",
        "xor-r32",
        "xor-r24",
        "xor-r16",
        "xor-r63",
        "xor-next-word"
    };

    return pattern < TRAIL_PATTERN_COUNT ? names[pattern] : "invalid";
}

static size_t paired_trail_bit(size_t bit, unsigned int pattern) {
    static const unsigned int rotations[4] = {32u, 24u, 16u, 63u};
    size_t word = bit / 64u;
    size_t word_bit = bit % 64u;

    if (pattern >= 1u && pattern <= 4u) {
        size_t rotated =
            (word_bit + rotations[pattern - 1u]) & 63u;
        return word * 64u + rotated;
    }

    return ((word + 1u) % 16u) * 64u + word_bit;
}

static void apply_trail_difference(
    uint8_t changed[FCH_MIX_BLOCK_SIZE],
    const uint8_t base[FCH_MIX_BLOCK_SIZE],
    unsigned int pattern,
    size_t bit_a,
    size_t bit_b
) {
    memcpy(changed, base, FCH_MIX_BLOCK_SIZE);
    changed[bit_a / 8u] ^=
        (uint8_t)(1u << (unsigned int)(bit_a % 8u));
    if (pattern != 0u) {
        changed[bit_b / 8u] ^=
            (uint8_t)(1u << (unsigned int)(bit_b % 8u));
    }
}

static void reset_trail_stats(trail_stats_t *stats) {
    stats->total_weight = 0u;
    stats->minimum_weight = 512;
    stats->maximum_weight = 0;
    stats->minimum_active_words = 8u;
    stats->zero_differences = 0u;
    stats->best_base = 0u;
    stats->best_pattern = 0u;
    stats->best_bit_a = 0u;
    stats->best_bit_b = 0u;
}

static void record_trail_stats(
    trail_stats_t *stats,
    int weight,
    unsigned int active,
    unsigned int base,
    unsigned int pattern,
    size_t bit_a,
    size_t bit_b
) {
    stats->total_weight += (uint64_t)weight;
    if (weight == 0)
        stats->zero_differences++;
    if (weight < stats->minimum_weight) {
        stats->minimum_weight = weight;
        stats->best_base = base;
        stats->best_pattern = pattern;
        stats->best_bit_a = bit_a;
        stats->best_bit_b = bit_b;
    }
    if (weight > stats->maximum_weight)
        stats->maximum_weight = weight;
    if (active < stats->minimum_active_words)
        stats->minimum_active_words = active;
}

static void rotate_message_words(
    uint8_t output[FCH_MIX_BLOCK_SIZE],
    const uint8_t input[FCH_MIX_BLOCK_SIZE],
    unsigned int rotation
) {
    for (size_t word = 0; word < 16u; word++) {
        uint64_t value = fch_load_le64(input + word * 8u);
        fch_store_le64(output + word * 8u, fch_rotl64(value, rotation));
    }
}

static int rotational_pair_check(void) {
    static const unsigned int rotations[ARX_ROTATION_COUNT] = {
        1u, 8u, 16u, 24u, 32u, 63u
    };
    static const unsigned int round_counts[] = {
        1u, 2u, 4u, FCH_MIX_REDUCED_ROUND_REFERENCE, FCH_MIX_ROUNDS
    };
    int all_ok = 1;

    printf(
        "rotational_pair,rounds,pairs,avg,min,max,min_active_words,"
        "max_rotation_bias,exact_relations,status\n"
    );
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        unsigned int rounds = round_counts[ri];
        trail_stats_t stats;
        double maximum_rotation_bias = 0.0;

        reset_trail_stats(&stats);
        for (size_t rotation_index = 0;
             rotation_index < ARX_ROTATION_COUNT;
             rotation_index++) {
            unsigned int rotation = rotations[rotation_index];
            uint64_t stream =
                UINT64_C(0x524F745041495201) ^
                ((uint64_t)rounds << 32u) ^ rotation;
            uint64_t rotation_weight = 0u;

            for (unsigned int sample = 0;
                 sample < ARX_PAIR_SAMPLES;
                 sample++) {
                uint8_t base[FCH_MIX_BLOCK_SIZE];
                uint8_t rotated[FCH_MIX_BLOCK_SIZE];
                uint64_t output_a[8];
                uint64_t output_b[8];
                uint64_t rotated_output[8];

                fill_bytes(base, sizeof(base), &stream);
                rotate_message_words(rotated, base, rotation);
                if (!core_output(
                        base,
                        sample,
                        UINT64_C(0x524F544154453031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        output_a
                    ) ||
                    !core_output(
                        rotated,
                        sample,
                        UINT64_C(0x524F544154453031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        output_b
                    ))
                    return 0;

                for (size_t word = 0; word < 8u; word++) {
                    rotated_output[word] =
                        fch_rotl64(output_a[word], rotation);
                }
                int weight = bit_diff(
                    (const uint8_t *)rotated_output,
                    (const uint8_t *)output_b,
                    sizeof(output_b)
                );
                unsigned int active = active_difference_words(
                    rotated_output,
                    output_b
                );
                record_trail_stats(
                    &stats,
                    weight,
                    active,
                    (unsigned int)rotation_index,
                    0u,
                    sample,
                    rotation
                );
                rotation_weight += (uint64_t)weight;
            }

            double rotation_average =
                (double)rotation_weight /
                ((double)ARX_PAIR_SAMPLES * 512.0) * 100.0;
            double rotation_bias = rotation_average >= 50.0
                ? rotation_average - 50.0
                : 50.0 - rotation_average;
            if (rotation_bias > maximum_rotation_bias)
                maximum_rotation_bias = rotation_bias;
        }

        const uint64_t pairs =
            (uint64_t)ARX_PAIR_SAMPLES * ARX_ROTATION_COUNT;
        double average =
            (double)stats.total_weight /
            ((double)pairs * 512.0) * 100.0;
        int strong =
            average >= 47.0 && average <= 53.0 &&
            stats.minimum_weight >= 160 &&
            stats.minimum_active_words == 8u &&
            maximum_rotation_bias <= 4.0;
        int ok = stats.zero_differences == 0u &&
            (rounds == 1u || strong);

        printf(
            "rotational_pair,%u,%llu,%.2f,%d,%d,%u,%.2f,%u,%s\n",
            rounds,
            (unsigned long long)pairs,
            average,
            stats.minimum_weight,
            stats.maximum_weight,
            stats.minimum_active_words,
            maximum_rotation_bias,
            stats.zero_differences,
            strong ? "PASS" : (ok ? "WEAK" : "FAIL")
        );
        if (!ok)
            all_ok = 0;
    }

    return all_ok;
}

static int additive_differential_check(void) {
    static const size_t message_words[ARX_ADDITIVE_CASES] = {
        0u, 5u, 10u, 15u
    };
    static const uint64_t deltas[ARX_ADDITIVE_CASES] = {
        UINT64_C(0x0000000000000001),
        UINT64_C(0x0000000080000000),
        UINT64_C(0x8000000000000000),
        UINT64_C(0x0101010101010101)
    };
    static const unsigned int round_counts[] = {
        1u, 2u, 4u, FCH_MIX_REDUCED_ROUND_REFERENCE, FCH_MIX_ROUNDS
    };
    int all_ok = 1;

    printf(
        "additive_differential,rounds,pairs,avg,min,max,min_active_words,"
        "max_bit_bias,zero_differences,status\n"
    );
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        unsigned int rounds = round_counts[ri];
        trail_stats_t stats;
        uint32_t flip_counts[512] = {0};

        reset_trail_stats(&stats);
        for (size_t case_index = 0;
             case_index < ARX_ADDITIVE_CASES;
             case_index++) {
            uint64_t stream =
                UINT64_C(0xADD1D1FF5EED0001) ^
                ((uint64_t)rounds << 32u) ^ case_index;

            for (unsigned int sample = 0;
                 sample < ARX_PAIR_SAMPLES;
                 sample++) {
                uint8_t base[FCH_MIX_BLOCK_SIZE];
                uint8_t changed[FCH_MIX_BLOCK_SIZE];
                uint64_t output_a[8];
                uint64_t output_b[8];
                size_t message_word = message_words[case_index];
                uint64_t value;

                fill_bytes(base, sizeof(base), &stream);
                memcpy(changed, base, sizeof(changed));
                value = fch_load_le64(changed + message_word * 8u);
                fch_store_le64(
                    changed + message_word * 8u,
                    value + deltas[case_index]
                );
                if (!core_output(
                        base,
                        sample + case_index * ARX_PAIR_SAMPLES,
                        UINT64_C(0x4144444449463031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        output_a
                    ) ||
                    !core_output(
                        changed,
                        sample + case_index * ARX_PAIR_SAMPLES,
                        UINT64_C(0x4144444449463031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        output_b
                    ))
                    return 0;

                int weight = bit_diff(
                    (const uint8_t *)output_a,
                    (const uint8_t *)output_b,
                    sizeof(output_a)
                );
                unsigned int active = active_difference_words(
                    output_a,
                    output_b
                );
                record_trail_stats(
                    &stats,
                    weight,
                    active,
                    (unsigned int)case_index,
                    0u,
                    sample,
                    message_word
                );
                for (size_t word = 0; word < 8u; word++) {
                    uint64_t difference = output_a[word] ^ output_b[word];
                    for (size_t bit = 0; bit < 64u; bit++) {
                        flip_counts[word * 64u + bit] +=
                            (uint32_t)((difference >> bit) & 1u);
                    }
                }
            }
        }

        const uint64_t pairs =
            (uint64_t)ARX_PAIR_SAMPLES * ARX_ADDITIVE_CASES;
        double maximum_bit_bias = 0.0;
        for (size_t bit = 0; bit < 512u; bit++) {
            double probability =
                (double)flip_counts[bit] / (double)pairs * 100.0;
            double bias = probability >= 50.0
                ? probability - 50.0
                : 50.0 - probability;
            if (bias > maximum_bit_bias)
                maximum_bit_bias = bias;
        }

        double average =
            (double)stats.total_weight /
            ((double)pairs * 512.0) * 100.0;
        int strong =
            average >= 47.0 && average <= 53.0 &&
            stats.minimum_weight >= 160 &&
            stats.minimum_active_words == 8u &&
            maximum_bit_bias <= 7.0;
        int ok = stats.zero_differences == 0u &&
            (rounds == 1u || strong);

        printf(
            "additive_differential,%u,%llu,%.2f,%d,%d,%u,%.2f,%u,%s\n",
            rounds,
            (unsigned long long)pairs,
            average,
            stats.minimum_weight,
            stats.maximum_weight,
            stats.minimum_active_words,
            maximum_bit_bias,
            stats.zero_differences,
            strong ? "PASS" : (ok ? "WEAK" : "FAIL")
        );
        if (!ok)
            all_ok = 0;
    }

    return all_ok;
}

static uint16_t differential_projection(
    const uint64_t difference[8],
    unsigned int projection
) {
    uint64_t value = UINT64_C(0x9E3779B97F4A7C15) *
        (uint64_t)(projection + 1u);

    for (size_t word = 0; word < 8u; word++) {
        unsigned int rotation =
            (unsigned int)((word * 11u + projection * 17u) & 63u);
        value ^= fch_rotl64(difference[word], rotation);
    }
    value ^= value >> 32u;
    value ^= value >> 16u;
    return (uint16_t)value;
}

static int differential_probability_check(void) {
    static const size_t bit_a[DIFFERENTIAL_PROBABILITY_CASES] = {
        0u, 63u, 64u, 511u, 0u, 0u, 63u, 511u
    };
    static const size_t bit_b[DIFFERENTIAL_PROBABILITY_CASES] = {
        SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX,
        32u, 64u, 127u, 1023u
    };
    static const unsigned int round_counts[] = {
        1u, 2u, 4u, FCH_MIX_REDUCED_ROUND_REFERENCE, FCH_MIX_ROUNDS
    };
    static uint16_t buckets[DIFFERENTIAL_PROJECTIONS][UINT16_MAX + 1u];
    int all_ok = 1;

    printf(
        "differential_probability,rounds,characteristics,samples_each,pairs,"
        "max_projection_count,max_projection_probability,zero_differences,"
        "status\n"
    );
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        unsigned int rounds = round_counts[ri];
        unsigned int maximum_count = 0u;
        unsigned int zero_differences = 0u;

        for (size_t characteristic = 0;
             characteristic < DIFFERENTIAL_PROBABILITY_CASES;
             characteristic++) {
            uint64_t stream =
                UINT64_C(0xD1FF50524F423031) ^
                ((uint64_t)rounds << 32u) ^ characteristic;

            memset(buckets, 0, sizeof(buckets));
            for (unsigned int sample = 0;
                 sample < DIFFERENTIAL_PROBABILITY_SAMPLES;
                 sample++) {
                uint8_t base[FCH_MIX_BLOCK_SIZE];
                uint8_t changed[FCH_MIX_BLOCK_SIZE];
                uint64_t output_a[8];
                uint64_t output_b[8];
                uint64_t difference[8];

                fill_bytes(base, sizeof(base), &stream);
                memcpy(changed, base, sizeof(changed));
                changed[bit_a[characteristic] / 8u] ^=
                    (uint8_t)(1u <<
                        (unsigned int)(bit_a[characteristic] % 8u));
                if (bit_b[characteristic] != SIZE_MAX) {
                    changed[bit_b[characteristic] / 8u] ^=
                        (uint8_t)(1u <<
                            (unsigned int)(bit_b[characteristic] % 8u));
                }

                uint64_t counter =
                    sample +
                    characteristic * DIFFERENTIAL_PROBABILITY_SAMPLES;
                if (!core_output(
                        base,
                        counter,
                        UINT64_C(0x445046524F423031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        output_a
                    ) ||
                    !core_output(
                        changed,
                        counter,
                        UINT64_C(0x445046524F423031),
                        FCH_MIX_FLAG_LEAF_DATA,
                        rounds,
                        output_b
                    ))
                    return 0;

                uint64_t combined = 0u;
                for (size_t word = 0; word < 8u; word++) {
                    difference[word] = output_a[word] ^ output_b[word];
                    combined |= difference[word];
                }
                if (combined == 0u)
                    zero_differences++;

                for (unsigned int projection = 0;
                     projection < DIFFERENTIAL_PROJECTIONS;
                     projection++) {
                    uint16_t value = differential_projection(
                        difference,
                        projection
                    );
                    buckets[projection][value]++;
                }
            }

            for (unsigned int projection = 0;
                 projection < DIFFERENTIAL_PROJECTIONS;
                 projection++) {
                for (size_t value = 0; value <= UINT16_MAX; value++) {
                    if (buckets[projection][value] > maximum_count)
                        maximum_count = buckets[projection][value];
                }
            }
        }

        const uint64_t pairs =
            (uint64_t)DIFFERENTIAL_PROBABILITY_CASES *
            DIFFERENTIAL_PROBABILITY_SAMPLES;
        double maximum_probability =
            (double)maximum_count /
            (double)DIFFERENTIAL_PROBABILITY_SAMPLES * 100.0;
        int strong =
            zero_differences == 0u &&
            maximum_probability <= 1.0;
        int ok = zero_differences == 0u &&
            (rounds == 1u || strong);

        printf(
            "differential_probability,%u,%u,%u,%llu,%u,%.4f,%u,%s\n",
            rounds,
            DIFFERENTIAL_PROBABILITY_CASES,
            DIFFERENTIAL_PROBABILITY_SAMPLES,
            (unsigned long long)pairs,
            maximum_count,
            maximum_probability,
            zero_differences,
            strong ? "PASS" : (ok ? "WEAK" : "FAIL")
        );
        if (!ok)
            all_ok = 0;
    }

    return all_ok;
}

static int related_context_check(void) {
    static const unsigned int round_counts[] = {
        1u, 2u, 4u, FCH_MIX_REDUCED_ROUND_REFERENCE, FCH_MIX_ROUNDS
    };
    int all_ok = 1;

    printf(
        "related_context,rounds,pairs,avg,min,max,min_active_words,"
        "max_bit_bias,zero_differences,status\n"
    );
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        unsigned int rounds = round_counts[ri];
        trail_stats_t stats;
        uint32_t flip_counts[512] = {0};

        reset_trail_stats(&stats);
        for (unsigned int context = 0;
             context < RELATED_CONTEXT_CASES;
             context++) {
            uint64_t stream =
                UINT64_C(0x52454C4354583031) ^
                ((uint64_t)rounds << 32u) ^ context;

            for (unsigned int sample = 0;
                 sample < RELATED_CONTEXT_SAMPLES;
                 sample++) {
                uint8_t block[FCH_MIX_BLOCK_SIZE];
                uint64_t output_a[8];
                uint64_t output_b[8];
                uint64_t counter_a =
                    sample + context * RELATED_CONTEXT_SAMPLES;
                uint64_t counter_b = counter_a;
                uint64_t domain_a = FCH_DOMAIN_LEAF;
                uint64_t domain_b = domain_a;
                uint64_t flags_a = FCH_MIX_FLAG_LEAF_DATA;
                uint64_t flags_b = flags_a;

                if (context == 0u)
                    counter_b++;
                else if (context == 1u)
                    counter_b ^= UINT64_C(0x8000000000000000);
                else if (context == 2u) {
                    domain_b = FCH_DOMAIN_NODE;
                    flags_b = FCH_MIX_FLAG_NODE_CHILD;
                } else if (context == 3u) {
                    domain_b = FCH_DOMAIN_OUTPUT_256;
                    flags_b = FCH_MIX_FLAG_OUTPUT | FCH_MIX_FLAG_FINAL;
                } else if (context == 4u) {
                    domain_a = FCH_DOMAIN_OUTPUT_256;
                    domain_b = FCH_DOMAIN_OUTPUT_512;
                    flags_a = FCH_MIX_FLAG_OUTPUT | FCH_MIX_FLAG_FINAL;
                    flags_b = flags_a;
                } else if (context == 5u) {
                    flags_b = FCH_MIX_FLAG_LEAF_HEADER;
                } else if (context == 6u) {
                    domain_b ^= UINT64_C(1);
                } else {
                    counter_b++;
                    domain_b ^= UINT64_C(1);
                    flags_b = FCH_MIX_FLAG_NODE_CHILD;
                }

                fill_bytes(block, sizeof(block), &stream);
                if (!core_output(
                        block,
                        counter_a,
                        domain_a,
                        flags_a,
                        rounds,
                        output_a
                    ) ||
                    !core_output(
                        block,
                        counter_b,
                        domain_b,
                        flags_b,
                        rounds,
                        output_b
                    ))
                    return 0;

                int weight = bit_diff(
                    (const uint8_t *)output_a,
                    (const uint8_t *)output_b,
                    sizeof(output_a)
                );
                unsigned int active = active_difference_words(
                    output_a,
                    output_b
                );
                record_trail_stats(
                    &stats,
                    weight,
                    active,
                    context,
                    0u,
                    sample,
                    0u
                );
                for (size_t word = 0; word < 8u; word++) {
                    uint64_t difference = output_a[word] ^ output_b[word];
                    for (size_t bit = 0; bit < 64u; bit++) {
                        flip_counts[word * 64u + bit] +=
                            (uint32_t)((difference >> bit) & 1u);
                    }
                }
            }
        }

        const uint64_t pairs =
            (uint64_t)RELATED_CONTEXT_CASES * RELATED_CONTEXT_SAMPLES;
        double maximum_bit_bias = 0.0;
        for (size_t bit = 0; bit < 512u; bit++) {
            double probability =
                (double)flip_counts[bit] / (double)pairs * 100.0;
            double bias = probability >= 50.0
                ? probability - 50.0
                : 50.0 - probability;
            if (bias > maximum_bit_bias)
                maximum_bit_bias = bias;
        }

        double average =
            (double)stats.total_weight /
            ((double)pairs * 512.0) * 100.0;
        int strong =
            average >= 47.0 && average <= 53.0 &&
            stats.minimum_weight >= 160 &&
            stats.minimum_active_words == 8u &&
            maximum_bit_bias <= 7.0;
        int ok = stats.zero_differences == 0u &&
            (rounds == 1u || strong);

        printf(
            "related_context,%u,%llu,%.2f,%d,%d,%u,%.2f,%u,%s\n",
            rounds,
            (unsigned long long)pairs,
            average,
            stats.minimum_weight,
            stats.maximum_weight,
            stats.minimum_active_words,
            maximum_bit_bias,
            stats.zero_differences,
            strong ? "PASS" : (ok ? "WEAK" : "FAIL")
        );
        if (!ok)
            all_ok = 0;
    }

    return all_ok;
}

static int prepare_test_work(
    const uint8_t block[FCH_MIX_BLOCK_SIZE],
    uint64_t counter,
    uint64_t domain,
    uint64_t flags,
    uint64_t work[16],
    uint64_t message[16]
) {
    uint64_t state[8];

    if (!fch_mix_init(state, 8u, domain))
        return 0;
    return fch_mix_test_prepare(
        work,
        message,
        state,
        block,
        FCH_MIX_BLOCK_SIZE,
        counter,
        domain,
        flags
    );
}

static unsigned int active_work_words(
    const uint64_t a[16],
    const uint64_t b[16]
) {
    unsigned int active = 0u;

    for (size_t word = 0; word < 16u; word++) {
        if ((a[word] ^ b[word]) != 0u)
            active++;
    }
    return active;
}

static void candidate_block(
    uint8_t output[FCH_MIX_BLOCK_SIZE],
    const uint8_t base[FCH_MIX_BLOCK_SIZE],
    unsigned int candidate
) {
    memcpy(output, base, FCH_MIX_BLOCK_SIZE);
    output[0] ^= (uint8_t)candidate;
    output[FCH_MIX_BLOCK_SIZE - 1u] ^=
        (uint8_t)((candidate >> 8u) & 0x0fu);
}

static uint32_t work_projection(const uint64_t state[16]) {
    uint64_t value = UINT64_C(0x4D49544D50524F4A);

    for (size_t word = 0; word < 16u; word++) {
        value ^= fch_rotl64(
            state[word],
            (unsigned int)((word * 13u + 7u) & 63u)
        );
        value *= UINT64_C(0x9E3779B185EBCA87);
    }
    value ^= value >> 32u;
    value ^= value >> 16u;
    return (uint32_t)(value & ((UINT32_C(1) << MITM_PREFIX_BITS) - 1u));
}

static int mitm_entry_compare(const void *left, const void *right) {
    const mitm_entry_t *a = left;
    const mitm_entry_t *b = right;

    if (a->key < b->key)
        return -1;
    if (a->key > b->key)
        return 1;
    if (a->candidate < b->candidate)
        return -1;
    if (a->candidate > b->candidate)
        return 1;
    return 0;
}

static size_t mitm_lower_bound(
    const mitm_entry_t entries[MITM_CANDIDATES],
    uint32_t key
) {
    size_t left = 0u;
    size_t right = MITM_CANDIDATES;

    while (left < right) {
        size_t middle = left + (right - left) / 2u;
        if (entries[middle].key < key)
            left = middle + 1u;
        else
            right = middle;
    }
    return left;
}

static int mix_roundtrip_check(void) {
    uint8_t block[FCH_MIX_BLOCK_SIZE];
    uint64_t stream = UINT64_C(0x494E564552534531);
    uint64_t initial[16];
    uint64_t work[16];
    uint64_t message[16];
    uint64_t roundtrip[16];
    uint64_t checkpoint[16];
    uint64_t expected[8];
    uint64_t derived[8];
    const uint64_t counter = UINT64_C(0x1020304050607080);
    const uint64_t domain = UINT64_C(0x494E565445535431);
    const uint64_t flags = FCH_MIX_FLAG_LEAF_DATA;

    fill_bytes(block, sizeof(block), &stream);
    if (!prepare_test_work(
            block,
            counter,
            domain,
            flags,
            work,
            message
        ))
        return 0;
    memcpy(initial, work, sizeof(initial));
    if (!fch_mix_test_forward(work, message, 0u, 8u))
        return 0;

    if (!fch_mix_init(derived, 8u, domain) ||
        !core_output(block, counter, domain, flags, 8u, expected))
        return 0;
    for (size_t word = 0; word < 8u; word++)
        derived[word] ^= work[word] ^ work[word + 8u];
    if (memcmp(derived, expected, sizeof(derived)) != 0)
        return 0;

    memcpy(roundtrip, work, sizeof(roundtrip));
    if (!fch_mix_test_inverse(roundtrip, message, 0u, 8u) ||
        memcmp(roundtrip, initial, sizeof(roundtrip)) != 0)
        return 0;

    memcpy(roundtrip, initial, sizeof(roundtrip));
    if (!fch_mix_test_forward(roundtrip, message, 0u, 2u))
        return 0;
    memcpy(checkpoint, roundtrip, sizeof(checkpoint));
    if (!fch_mix_test_forward(roundtrip, message, 2u, 4u) ||
        !fch_mix_test_inverse(roundtrip, message, 2u, 4u) ||
        memcmp(roundtrip, checkpoint, sizeof(roundtrip)) != 0)
        return 0;

    printf("inverse_roundtrip,full=8,window=2+4,status=PASS\n");
    return 1;
}

static int rebound_screen(void) {
    static const unsigned int total_rounds[REBOUND_CASES] = {
        4u, 8u, 16u
    };
    static const unsigned int split_rounds[REBOUND_CASES] = {
        2u, 4u, 8u
    };
    uint8_t base[FCH_MIX_BLOCK_SIZE];
    uint64_t stream = UINT64_C(0x5245424F554E4431);
    const uint64_t counter = UINT64_C(0x5245424F554E4401);
    const uint64_t domain = UINT64_C(0x5245424F554E4432);
    const uint64_t flags = FCH_MIX_FLAG_LEAF_DATA;
    int all_ok = 1;

    fill_bytes(base, sizeof(base), &stream);
    printf(
        "rebound_screen,total_rounds,split_round,candidates,min_middle,"
        "min_middle_active,low_middle,min_end,min_end_active,zero_middle,"
        "zero_end,status\n"
    );
    for (unsigned int case_index = 0;
         case_index < REBOUND_CASES;
         case_index++) {
        unsigned int total = total_rounds[case_index];
        unsigned int split = split_rounds[case_index];
        uint64_t base_work[16];
        uint64_t base_message[16];
        uint64_t base_middle[16];
        uint64_t base_end[16];
        int minimum_middle = 1024;
        int minimum_end = 1024;
        unsigned int minimum_middle_active = 16u;
        unsigned int minimum_end_active = 16u;
        unsigned int low_middle = 0u;
        unsigned int zero_middle = 0u;
        unsigned int zero_end = 0u;

        if (!prepare_test_work(
                base,
                counter,
                domain,
                flags,
                base_work,
                base_message
            ) ||
            !fch_mix_test_forward(base_work, base_message, 0u, split))
            return 0;
        memcpy(base_middle, base_work, sizeof(base_middle));
        if (!fch_mix_test_forward(
                base_work,
                base_message,
                split,
                total - split
            ))
            return 0;
        memcpy(base_end, base_work, sizeof(base_end));

        for (unsigned int candidate = 1u;
             candidate < REBOUND_CANDIDATES;
             candidate++) {
            uint8_t changed[FCH_MIX_BLOCK_SIZE];
            uint64_t work[16];
            uint64_t message[16];

            candidate_block(changed, base, candidate);
            if (!prepare_test_work(
                    changed,
                    counter,
                    domain,
                    flags,
                    work,
                    message
                ) ||
                !fch_mix_test_forward(work, message, 0u, split))
                return 0;

            int middle_weight = bit_diff(
                (const uint8_t *)base_middle,
                (const uint8_t *)work,
                sizeof(work)
            );
            unsigned int middle_active = active_work_words(base_middle, work);
            if (middle_weight < minimum_middle)
                minimum_middle = middle_weight;
            if (middle_active < minimum_middle_active)
                minimum_middle_active = middle_active;
            if (middle_weight <= 256)
                low_middle++;
            if (middle_weight == 0)
                zero_middle++;

            if (!fch_mix_test_forward(
                    work,
                    message,
                    split,
                    total - split
                ))
                return 0;
            int end_weight = bit_diff(
                (const uint8_t *)base_end,
                (const uint8_t *)work,
                sizeof(work)
            );
            unsigned int end_active = active_work_words(base_end, work);
            if (end_weight < minimum_end)
                minimum_end = end_weight;
            if (end_active < minimum_end_active)
                minimum_end_active = end_active;
            if (end_weight == 0)
                zero_end++;
        }

        int ok =
            minimum_middle >= 256 &&
            minimum_end >= 320 &&
            minimum_middle_active == 16u &&
            minimum_end_active == 16u &&
            low_middle == 0u &&
            zero_middle == 0u &&
            zero_end == 0u;
        printf(
            "rebound_screen,%u,%u,%u,%d,%u,%u,%d,%u,%u,%u,%s\n",
            total,
            split,
            REBOUND_CANDIDATES - 1u,
            minimum_middle,
            minimum_middle_active,
            low_middle,
            minimum_end,
            minimum_end_active,
            zero_middle,
            zero_end,
            ok ? "PASS" : "FAIL"
        );
        if (!ok)
            all_ok = 0;
    }

    return all_ok;
}

static int mitm_screen(void) {
    static mitm_entry_t entries[MITM_CANDIDATES];
    uint8_t base[FCH_MIX_BLOCK_SIZE];
    uint64_t stream = UINT64_C(0x4D49544D53435231);
    const uint64_t counter = UINT64_C(0x4D49544D00000001);
    const uint64_t domain = UINT64_C(0x4D49544D00000002);
    const uint64_t flags = FCH_MIX_FLAG_LEAF_DATA;
    const unsigned int target_candidate = 0xA5Bu;
    uint64_t target_work[16];
    uint64_t target_message[16];
    uint64_t prefix_pairs = 0u;
    unsigned int exact_matches = 0u;
    unsigned int recovered = 0u;

    fill_bytes(base, sizeof(base), &stream);
    uint8_t target_block[FCH_MIX_BLOCK_SIZE];
    candidate_block(target_block, base, target_candidate);
    if (!prepare_test_work(
            target_block,
            counter,
            domain,
            flags,
            target_work,
            target_message
        ) ||
        !fch_mix_test_forward(target_work, target_message, 0u, 8u))
        return 0;

    for (unsigned int candidate = 0u;
         candidate < MITM_CANDIDATES;
         candidate++) {
        uint8_t block[FCH_MIX_BLOCK_SIZE];
        uint64_t message[16];

        candidate_block(block, base, candidate);
        if (!prepare_test_work(
                block,
                counter,
                domain,
                flags,
                entries[candidate].state,
                message
            ) ||
            !fch_mix_test_forward(
                entries[candidate].state,
                message,
                0u,
                4u
            ))
            return 0;
        entries[candidate].key = work_projection(entries[candidate].state);
        entries[candidate].candidate = candidate;
    }
    qsort(entries, MITM_CANDIDATES, sizeof(entries[0]), mitm_entry_compare);

    for (unsigned int candidate = 0u;
         candidate < MITM_CANDIDATES;
         candidate++) {
        uint8_t block[FCH_MIX_BLOCK_SIZE];
        uint64_t unused_work[16];
        uint64_t message[16];
        uint64_t backward[16];

        candidate_block(block, base, candidate);
        if (!prepare_test_work(
                block,
                counter,
                domain,
                flags,
                unused_work,
                message
            ))
            return 0;
        memcpy(backward, target_work, sizeof(backward));
        if (!fch_mix_test_inverse(backward, message, 4u, 4u))
            return 0;

        uint32_t key = work_projection(backward);
        size_t position = mitm_lower_bound(entries, key);
        while (position < MITM_CANDIDATES &&
               entries[position].key == key) {
            prefix_pairs++;
            if (memcmp(
                    entries[position].state,
                    backward,
                    sizeof(backward)
                ) == 0) {
                exact_matches++;
                if (candidate == target_candidate &&
                    entries[position].candidate == target_candidate)
                    recovered = 1u;
            }
            position++;
        }
    }

    int ok =
        prefix_pairs >= 1u &&
        prefix_pairs <= 64u &&
        exact_matches == 1u &&
        recovered == 1u;
    printf(
        "mitm_screen,rounds=8,split=4,candidates=%u,prefix_bits=%u,"
        "forward=%u,backward=%u,prefix_pairs=%llu,exact_matches=%u,"
        "target=0x%03x,recovered=%s,status=%s\n",
        MITM_CANDIDATES,
        MITM_PREFIX_BITS,
        MITM_CANDIDATES,
        MITM_CANDIDATES,
        (unsigned long long)prefix_pairs,
        exact_matches,
        target_candidate,
        recovered ? "yes" : "no",
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

static int print_best_trail(
    uint8_t bases[TRAIL_BASES][FCH_MIX_BLOCK_SIZE],
    const trail_stats_t *best,
    unsigned int target_round
) {
    uint8_t changed[FCH_MIX_BLOCK_SIZE];

    apply_trail_difference(
        changed,
        bases[best->best_base],
        best->best_pattern,
        best->best_bit_a,
        best->best_bit_b
    );

    printf(
        "arx_trail_best,target=%u,base=%u,pattern=%s,"
        "bit_a=%u",
        target_round,
        best->best_base,
        trail_pattern_name(best->best_pattern),
        (unsigned int)best->best_bit_a
    );
    if (best->best_pattern != 0u)
        printf(",bit_b=%u", (unsigned int)best->best_bit_b);

    for (unsigned int rounds = 1u;
         rounds <= TRAIL_ROUND_COUNT;
         rounds++) {
        uint64_t output_a[8];
        uint64_t output_b[8];

        if (!core_output(
                bases[best->best_base],
                best->best_base,
                UINT64_C(0x545241494C303031),
                FCH_MIX_FLAG_LEAF_DATA,
                rounds,
                output_a
            ) ||
            !core_output(
                changed,
                best->best_base,
                UINT64_C(0x545241494C303031),
                FCH_MIX_FLAG_LEAF_DATA,
                rounds,
                output_b
            ))
            return 0;

        int weight = bit_diff(
            (const uint8_t *)output_a,
            (const uint8_t *)output_b,
            sizeof(output_a)
        );
        printf(",r%u=%d", rounds, weight);
    }
    printf("\n");
    return 1;
}

static int reduced_round_trail_search(void) {
    uint8_t bases[TRAIL_BASES][FCH_MIX_BLOCK_SIZE];
    uint64_t base_outputs[TRAIL_BASES][TRAIL_ROUND_COUNT][8];
    trail_stats_t stats[TRAIL_ROUND_COUNT];
    trail_stats_t pattern_stats[TRAIL_PATTERN_COUNT][TRAIL_ROUND_COUNT];
    uint64_t stream = UINT64_C(0x7A4115EED5EED001);
    const size_t bit_count = FCH_MIX_BLOCK_SIZE * 8u;
    const uint64_t comparisons =
        (uint64_t)TRAIL_BASES * TRAIL_PATTERN_COUNT * bit_count;
    const uint64_t pattern_comparisons =
        (uint64_t)TRAIL_BASES * bit_count;
    int all_ok = 1;

    if (FCH_MIX_ROUNDS != TRAIL_ROUND_COUNT ||
        FCH_MIX_REDUCED_ROUND_REFERENCE >= TRAIL_ROUND_COUNT)
        return 0;

    for (size_t round = 0; round < TRAIL_ROUND_COUNT; round++)
        reset_trail_stats(&stats[round]);
    for (size_t pattern = 0; pattern < TRAIL_PATTERN_COUNT; pattern++) {
        for (size_t round = 0; round < TRAIL_ROUND_COUNT; round++)
            reset_trail_stats(&pattern_stats[pattern][round]);
    }

    for (unsigned int base_index = 0;
         base_index < TRAIL_BASES;
         base_index++) {
        fill_bytes(bases[base_index], FCH_MIX_BLOCK_SIZE, &stream);
        for (unsigned int rounds = 1u;
             rounds <= TRAIL_ROUND_COUNT;
             rounds++) {
            if (!core_output(
                    bases[base_index],
                    base_index,
                    UINT64_C(0x545241494C303031),
                    FCH_MIX_FLAG_LEAF_DATA,
                    rounds,
                    base_outputs[base_index][rounds - 1u]
                ))
                return 0;
        }
    }

    for (unsigned int base_index = 0;
         base_index < TRAIL_BASES;
         base_index++) {
        for (unsigned int pattern = 0;
             pattern < TRAIL_PATTERN_COUNT;
             pattern++) {
            for (size_t bit_a = 0; bit_a < bit_count; bit_a++) {
                size_t bit_b = paired_trail_bit(bit_a, pattern);
                uint8_t changed[FCH_MIX_BLOCK_SIZE];

                apply_trail_difference(
                    changed,
                    bases[base_index],
                    pattern,
                    bit_a,
                    bit_b
                );

                for (unsigned int rounds = 1u;
                     rounds <= TRAIL_ROUND_COUNT;
                     rounds++) {
                    uint64_t changed_output[8];
                    uint64_t *base_output =
                        base_outputs[base_index][rounds - 1u];

                    if (!core_output(
                            changed,
                            base_index,
                            UINT64_C(0x545241494C303031),
                            FCH_MIX_FLAG_LEAF_DATA,
                            rounds,
                            changed_output
                        ))
                        return 0;

                    int weight = bit_diff(
                        (const uint8_t *)base_output,
                        (const uint8_t *)changed_output,
                        sizeof(changed_output)
                    );
                    unsigned int active = active_difference_words(
                        base_output,
                        changed_output
                    );
                    trail_stats_t *round_stats = &stats[rounds - 1u];
                    trail_stats_t *pattern_round_stats =
                        &pattern_stats[pattern][rounds - 1u];

                    record_trail_stats(
                        round_stats,
                        weight,
                        active,
                        base_index,
                        pattern,
                        bit_a,
                        bit_b
                    );
                    record_trail_stats(
                        pattern_round_stats,
                        weight,
                        active,
                        base_index,
                        pattern,
                        bit_a,
                        bit_b
                    );
                }
            }
        }
    }

    printf(
        "low_weight_search,rounds,candidates,avg,min,max,"
        "min_active_words,zero_differences,status\n"
    );
    for (unsigned int rounds = 1u;
         rounds <= TRAIL_ROUND_COUNT;
         rounds++) {
        const trail_stats_t *round_stats = &stats[rounds - 1u];
        double average =
            (double)round_stats->total_weight /
            ((double)comparisons * 512.0) * 100.0;
        int ok = round_stats->zero_differences == 0u;

        if (rounds >= 2u) {
            ok = ok &&
                average >= 47.0 && average <= 53.0 &&
                round_stats->minimum_weight >= 160 &&
                round_stats->minimum_active_words == 8u;
        }

        printf(
            "low_weight_search,%u,%llu,%.2f,%d,%d,%u,%u,%s\n",
            rounds,
            (unsigned long long)comparisons,
            average,
            round_stats->minimum_weight,
            round_stats->maximum_weight,
            round_stats->minimum_active_words,
            round_stats->zero_differences,
            ok ? (rounds == 1u ? "WEAK" : "PASS") : "FAIL"
        );
        if (!ok)
            all_ok = 0;
    }

    static const unsigned int report_rounds[] = {
        1u, 2u, 4u, 8u, 12u, 16u
    };
    printf(
        "arx_rotation,pattern,rounds,candidates,avg,min,max,"
        "min_active_words,zero_differences,status\n"
    );
    for (unsigned int pattern = 0;
         pattern < TRAIL_PATTERN_COUNT;
         pattern++) {
        for (size_t report = 0;
             report < sizeof(report_rounds) / sizeof(report_rounds[0]);
             report++) {
            unsigned int rounds = report_rounds[report];
            const trail_stats_t *round_stats =
                &pattern_stats[pattern][rounds - 1u];
            double average =
                (double)round_stats->total_weight /
                ((double)pattern_comparisons * 512.0) * 100.0;
            int ok = round_stats->zero_differences == 0u;
            if (rounds >= 2u) {
                ok = ok &&
                    average >= 47.0 && average <= 53.0 &&
                    round_stats->minimum_weight >= 160 &&
                    round_stats->minimum_active_words == 8u;
            }

            printf(
                "arx_rotation,%s,%u,%llu,%.2f,%d,%d,%u,%u,%s\n",
                trail_pattern_name(pattern),
                rounds,
                (unsigned long long)pattern_comparisons,
                average,
                round_stats->minimum_weight,
                round_stats->maximum_weight,
                round_stats->minimum_active_words,
                round_stats->zero_differences,
                ok ? (rounds == 1u ? "WEAK" : "PASS") : "FAIL"
            );
            if (!ok)
                all_ok = 0;
        }
    }

    if (stats[TRAIL_ROUND_COUNT - 1u].minimum_weight <
        stats[0].minimum_weight + 128)
        all_ok = 0;

    if (!print_best_trail(bases, &stats[0], 1u) ||
        !print_best_trail(bases, &stats[1], 2u) ||
        !print_best_trail(bases, &stats[3], 4u) ||
        !print_best_trail(bases, &stats[7], 8u) ||
        !print_best_trail(bases, &stats[11], 12u) ||
        !print_best_trail(
            bases,
            &stats[TRAIL_ROUND_COUNT - 1u],
            TRAIL_ROUND_COUNT
        ))
        return 0;

    return all_ok;
}

static int fixed_point_search(void) {
    static const unsigned int round_counts[] = {
        4u,
        FCH_MIX_REDUCED_ROUND_REFERENCE,
        FCH_MIX_ROUNDS
    };
    unsigned int core_fixed_points = 0;
    unsigned int core_two_cycles = 0;
    unsigned int hash_fixed_points = 0;
    unsigned int hash_two_cycles = 0;

    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        uint64_t stream =
            UINT64_C(0xF17ED00175EED001) ^ round_counts[ri];

        for (unsigned int sample = 0;
             sample < FIXED_POINT_SAMPLES;
             sample++) {
            uint8_t block[FCH_MIX_BLOCK_SIZE];
            uint64_t initial[8];
            uint64_t first[8];
            uint64_t second[8];

            fill_bytes(block, sizeof(block), &stream);
            for (size_t word = 0; word < 8u; word++)
                initial[word] = splitmix64_next(&stream);
            memcpy(first, initial, sizeof(first));
            if (!fch_mix_compress_rounds(
                    first,
                    8u,
                    block,
                    sizeof(block),
                    sample,
                    UINT64_C(0x4649584544503031),
                    FCH_MIX_FLAG_LEAF_DATA,
                    round_counts[ri]
                ))
                return 0;
            if (memcmp(first, initial, sizeof(first)) == 0)
                core_fixed_points++;

            memcpy(second, first, sizeof(second));
            if (!fch_mix_compress_rounds(
                    second,
                    8u,
                    block,
                    sizeof(block),
                    sample,
                    UINT64_C(0x4649584544503031),
                    FCH_MIX_FLAG_LEAF_DATA,
                    round_counts[ri]
                ))
                return 0;
            if (memcmp(second, initial, sizeof(second)) == 0)
                core_two_cycles++;
            if (memcmp(second, first, sizeof(second)) == 0)
                core_fixed_points++;
        }
    }

    uint64_t stream = UINT64_C(0x4841534846495831);
    for (unsigned int sample = 0;
         sample < FIXED_POINT_SAMPLES;
         sample++) {
        uint8_t message256[32];
        uint8_t digest256[32];
        uint8_t second256[32];
        uint8_t message512[64];
        uint8_t digest512[64];
        uint8_t second512[64];

        fill_bytes(message256, sizeof(message256), &stream);
        fill_bytes(message512, sizeof(message512), &stream);
        fch_hash_256(message256, sizeof(message256), digest256);
        fch_hash_256(digest256, sizeof(digest256), second256);
        fch_hash_512(message512, sizeof(message512), digest512);
        fch_hash_512(digest512, sizeof(digest512), second512);

        if (memcmp(message256, digest256, sizeof(message256)) == 0)
            hash_fixed_points++;
        if (memcmp(message256, second256, sizeof(message256)) == 0)
            hash_two_cycles++;
        if (memcmp(message512, digest512, sizeof(message512)) == 0)
            hash_fixed_points++;
        if (memcmp(message512, second512, sizeof(message512)) == 0)
            hash_two_cycles++;
    }

    int ok =
        core_fixed_points == 0u &&
        core_two_cycles == 0u &&
        hash_fixed_points == 0u &&
        hash_two_cycles == 0u;
    printf(
        "fixed_point,core_fixed=%u,core_two_cycle=%u,"
        "hash_fixed=%u,hash_two_cycle=%u,%s\n",
        core_fixed_points,
        core_two_cycles,
        hash_fixed_points,
        hash_two_cycles,
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

static int near_collision_search(void) {
    static uint8_t digests256[NEAR_COLLISION_SAMPLES][32];
    static uint8_t digests512[NEAR_COLLISION_SAMPLES][64];
    uint64_t stream = UINT64_C(0xC0111510A5EED001);

    for (unsigned int sample = 0;
         sample < NEAR_COLLISION_SAMPLES;
         sample++) {
        uint8_t message[NEAR_COLLISION_MESSAGE_SIZE];

        fill_bytes(message, sizeof(message), &stream);
        for (size_t i = 0; i < 8u; i++)
            message[i] = (uint8_t)((uint64_t)sample >> (i * 8u));
        fch_hash_256(message, sizeof(message), digests256[sample]);
        fch_hash_512(message, sizeof(message), digests512[sample]);
    }

    int minimum256 = 256;
    int minimum512 = 512;
    unsigned int collisions256 = 0;
    unsigned int collisions512 = 0;
    for (unsigned int a = 0; a < NEAR_COLLISION_SAMPLES; a++) {
        for (unsigned int b = a + 1u;
             b < NEAR_COLLISION_SAMPLES;
             b++) {
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
        minimum512 >= 160;
    printf(
        "near_collision,samples=%u,min256=%d,min512=%d,"
        "collisions256=%u,collisions512=%u,%s\n",
        NEAR_COLLISION_SAMPLES,
        minimum256,
        minimum512,
        collisions256,
        collisions512,
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

int main(void) {
    int ok = 1;

    if (!differential_bias_check())
        ok = 0;
    if (!linear_correlation_check())
        ok = 0;
    if (!reduced_round_search())
        ok = 0;
    if (!reduced_round_trail_search())
        ok = 0;
    if (!rotational_pair_check())
        ok = 0;
    if (!additive_differential_check())
        ok = 0;
    if (!differential_probability_check())
        ok = 0;
    if (!related_context_check())
        ok = 0;
    if (!mix_roundtrip_check())
        ok = 0;
    if (!rebound_screen())
        ok = 0;
    if (!mitm_screen())
        ok = 0;
    if (!fixed_point_search())
        ok = 0;
    if (!near_collision_search())
        ok = 0;

    if (!ok) {
        fprintf(stderr, "CRYPTANALYSIS: FAIL\n");
        return 1;
    }

    printf(
        "CRYPTANALYSIS: PASS "
        "(bounded deterministic checks; not a security proof)\n"
    );
    return 0;
}
