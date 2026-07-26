#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fch.h"
#include "mix.h"
#include "test_utils.h"

enum {
    DIFFERENTIAL_SAMPLES = 2048,
    LINEAR_SAMPLES = 8192,
    LINEAR_MASKS = 32,
    FIXED_POINT_SAMPLES = 4096,
    REDUCED_BASES = 4,
    NEAR_COLLISION_SAMPLES = 2048,
    NEAR_COLLISION_MESSAGE_SIZE = 64
};

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
    static const unsigned int round_counts[] = {
        1u,
        2u,
        4u,
        FCH_MIX_REDUCED_ROUND_REFERENCE,
        FCH_MIX_ROUNDS
    };
    int full_ok = 0;
    int reference_ok = 0;

    printf("reduced_search,rounds,avg,min,zero_differences,status\n");
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        unsigned int rounds = round_counts[ri];
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
        int strong_enough =
            zero_differences == 0u &&
            average >= 47.0 && average <= 53.0 &&
            minimum_weight >= 160;

        if (rounds == FCH_MIX_REDUCED_ROUND_REFERENCE)
            reference_ok = strong_enough;
        if (rounds == FCH_MIX_ROUNDS)
            full_ok = strong_enough;

        printf(
            "reduced_search,%u,%.2f,%.2f,%u,%s\n",
            rounds,
            average,
            minimum,
            zero_differences,
            strong_enough ? "PASS" : "WEAK"
        );
    }

    return reference_ok && full_ok;
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
