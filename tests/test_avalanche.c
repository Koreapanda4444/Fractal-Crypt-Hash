#include <stdio.h>
#include <string.h>

#include "fch.h"
#include "fractal.h"
#include "mix.h"
#include "params.h"
#include "test_utils.h"

#define MAX_INPUT 4096
#define ROUNDS 128

typedef enum {
    FLIP_SINGLE = 0,
    FLIP_SWEEP = 1
} flip_mode_t;

static const char *flip_mode_name(flip_mode_t mode) {
    return mode == FLIP_SINGLE ? "single" : "sweep";
}

typedef struct {
    double avg;
    double min;
    double max;
    double spread;
} avalanche_stats_t;

typedef struct {
    size_t len;
    flip_mode_t mode;
    double base_avg256;
    double base_spread256;
    double base_avg512;
    double base_spread512;
} regression_baseline_t;

static const double AVG_DROP_PCT = 5.0;
static const double SPREAD_INCR_PCT = 25.0;

static const regression_baseline_t BASELINES[] = {
    { 63,   FLIP_SWEEP, 49.88, 12.11, 49.82, 11.33 },
    { 64,   FLIP_SWEEP, 49.93, 16.02, 50.42, 10.16 },
    { 65,   FLIP_SWEEP, 50.42, 20.31, 50.33, 8.79 },
    { 127,  FLIP_SWEEP, 49.62, 16.80, 49.98, 11.91 },
    { 128,  FLIP_SWEEP, 50.42, 12.89, 50.27, 11.52 },
    { 129,  FLIP_SWEEP, 49.77, 15.62, 50.55, 13.28 },
    { 255,  FLIP_SWEEP, 50.01, 16.41, 49.91, 10.94 },
    { 257,  FLIP_SWEEP, 49.71, 19.14, 50.01, 12.70 },
    { 512,  FLIP_SWEEP, 49.51, 14.06, 50.28, 9.77 },
    { 1024, FLIP_SWEEP, 50.43, 15.62, 50.07, 10.16 },
    { 4096, FLIP_SWEEP, 50.47, 19.53, 49.89, 13.87 },
};

static const regression_baseline_t *find_baseline(size_t len, flip_mode_t mode) {
    for (size_t i = 0; i < sizeof(BASELINES) / sizeof(BASELINES[0]); i++) {
        if (BASELINES[i].len == len && BASELINES[i].mode == mode)
            return &BASELINES[i];
    }
    return NULL;
}

static avalanche_stats_t compute_stats(size_t len, flip_mode_t mode, int hash_bits) {
    uint8_t base[MAX_INPUT];
    uint8_t mod[MAX_INPUT];
    memset(base, 0xA5, len);

    avalanche_stats_t s;
    s.avg = 0.0;
    s.min = 1.0;
    s.max = 0.0;
    s.spread = 0.0;

    for (int r = 0; r < ROUNDS; r++) {
        memcpy(mod, base, len);
        if (len > 0) {
            if (mode == FLIP_SINGLE) {
                mod[0] ^= 1;
            } else {
                mod[r % len] ^= (uint8_t)(1u << (unsigned)(r % 8));
            }
        }

        double d = 0.0;
        if (hash_bits == 256) {
            uint8_t a[32], b[32];
            fch_hash_256(base, len, a);
            fch_hash_256(mod,  len, b);
            d = bit_diff(a, b, 32) / 256.0;
        } else {
            uint8_t a[64], b[64];
            fch_hash_512(base, len, a);
            fch_hash_512(mod,  len, b);
            d = bit_diff(a, b, 64) / 512.0;
        }

        s.avg += d;
        if (d < s.min) s.min = d;
        if (d > s.max) s.max = d;
    }

    s.avg /= ROUNDS;
    s.spread = s.max - s.min;
    return s;
}

static int determinism_check(void) {
    uint8_t in[MAX_INPUT];
    memset(in, 0x3C, sizeof(in));

    size_t lens[] = { 0, 1, 64, 128, 1024 };
    for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        size_t len = lens[i];
        uint8_t a256[32], b256[32];
        uint8_t a512[64], b512[64];

        fch_hash_256(in, len, a256);
        fch_hash_256(in, len, b256);
        fch_hash_512(in, len, a512);
        fch_hash_512(in, len, b512);

        if (memcmp(a256, b256, 32) != 0)
            return 0;
        if (memcmp(a512, b512, 64) != 0)
            return 0;
    }
    return 1;
}

static int invariant_split_covers_input_check(void) {
    uint8_t data[512];
    for (int i = 0; i < 512; i++)
        data[i] = (uint8_t)i;

    fch_block_t blocks[FCH_N_MAX];
    size_t n = fch_fractal_split(data, 512, 0, blocks, FCH_N_MAX);

    size_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += blocks[i].length;
    }

    return sum == 512;
}

static int reduced_round_margin_check(void) {
    static const unsigned int round_counts[] = {
        4u,
        FCH_MIX_REDUCED_ROUND_REFERENCE,
        12u,
        FCH_MIX_ROUNDS
    };
    enum { CORE_SAMPLES = 256 };
    int reference_ok = 0;
    int full_ok = 0;

    if (FCH_MIX_ROUNDS != 16u || FCH_MIX_ROUND_MARGIN < 8u)
        return 0;

    printf("round_margin,rounds,avg,min,max,gap,diffusion_status\n");
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        unsigned int rounds = round_counts[ri];
        uint64_t total_diff = 0;
        int min_diff = 512;
        int max_diff = 0;

        for (unsigned int sample = 0; sample < CORE_SAMPLES; sample++) {
            uint8_t base[FCH_MIX_BLOCK_SIZE];
            uint8_t changed[FCH_MIX_BLOCK_SIZE];
            uint32_t stream = UINT32_C(0x9E3779B9) ^
                (uint32_t)(sample * UINT32_C(0x85EBCA6B));

            for (size_t i = 0; i < sizeof(base); i++) {
                stream ^= stream << 13u;
                stream ^= stream >> 17u;
                stream ^= stream << 5u;
                base[i] = (uint8_t)(stream + (uint32_t)i * 29u);
            }
            memcpy(changed, base, sizeof(base));
            size_t bit_index =
                ((size_t)sample * 313u + (size_t)sample / 7u) %
                (sizeof(base) * 8u);
            changed[bit_index / 8u] ^=
                (uint8_t)(1u << (unsigned int)(bit_index % 8u));

            uint64_t state_a[8];
            uint64_t state_b[8];
            const uint64_t domain = UINT64_C(0x4D415247494E3031);
            if (!fch_mix_init(state_a, 8u, domain) ||
                !fch_mix_init(state_b, 8u, domain))
                return 0;
            if (!fch_mix_compress_rounds(
                    state_a,
                    8u,
                    base,
                    sizeof(base),
                    sample,
                    domain,
                    FCH_MIX_FLAG_LEAF_DATA,
                    rounds
                ))
                return 0;
            if (!fch_mix_compress_rounds(
                    state_b,
                    8u,
                    changed,
                    sizeof(changed),
                    sample,
                    domain,
                    FCH_MIX_FLAG_LEAF_DATA,
                    rounds
                ))
                return 0;

            int diff = bit_diff(
                (const uint8_t *)state_a,
                (const uint8_t *)state_b,
                sizeof(state_a)
            );
            total_diff += (uint64_t)diff;
            if (diff < min_diff)
                min_diff = diff;
            if (diff > max_diff)
                max_diff = diff;
        }

        double avg = (double)total_diff /
            ((double)CORE_SAMPLES * 512.0) * 100.0;
        double min_pct = (double)min_diff / 512.0 * 100.0;
        double max_pct = (double)max_diff / 512.0 * 100.0;
        int stable = avg >= 47.0 && avg <= 53.0 &&
            min_pct >= 35.0 && max_pct <= 65.0;
        if (rounds == FCH_MIX_REDUCED_ROUND_REFERENCE)
            reference_ok = stable;
        if (rounds == FCH_MIX_ROUNDS)
            full_ok = stable;

        printf(
            "round_margin,%u,%.2f,%.2f,%.2f,%u,%s\n",
            rounds,
            avg,
            min_pct,
            max_pct,
            FCH_MIX_ROUNDS - rounds,
            stable ? "STABLE" : "UNSTABLE"
        );
    }

    uint8_t zero_block[FCH_MIX_BLOCK_SIZE] = {0};
    uint64_t reduced_outputs[
        sizeof(round_counts) / sizeof(round_counts[0])
    ][8];
    for (size_t ri = 0;
         ri < sizeof(round_counts) / sizeof(round_counts[0]);
         ri++) {
        if (!fch_mix_init(reduced_outputs[ri], 8u, UINT64_C(1)))
            return 0;
        if (!fch_mix_compress_rounds(
                reduced_outputs[ri],
                8u,
                zero_block,
                sizeof(zero_block),
                0,
                UINT64_C(1),
                0,
                round_counts[ri]
            ))
            return 0;
        if (ri > 0 && memcmp(
                reduced_outputs[ri - 1u],
                reduced_outputs[ri],
                sizeof(reduced_outputs[ri])
            ) == 0)
            return 0;
    }

    uint64_t wrapper_output[8];
    if (!fch_mix_init(wrapper_output, 8u, UINT64_C(1)))
        return 0;
    if (!fch_mix_compress(
            wrapper_output,
            8u,
            zero_block,
            sizeof(zero_block),
            0,
            UINT64_C(1),
            0
        ))
        return 0;
    if (memcmp(
            wrapper_output,
            reduced_outputs[
                sizeof(round_counts) / sizeof(round_counts[0]) - 1u
            ],
            sizeof(wrapper_output)
        ) != 0)
        return 0;

    uint64_t state[8];
    if (!fch_mix_init(state, 8u, UINT64_C(1)))
        return 0;
    if (fch_mix_compress_rounds(
            state,
            8u,
            zero_block,
            sizeof(zero_block),
            0,
            UINT64_C(1),
            0,
            0u
        ))
        return 0;
    if (fch_mix_compress_rounds(
            state,
            8u,
            zero_block,
            sizeof(zero_block),
            0,
            UINT64_C(1),
            0,
            FCH_MIX_ROUNDS + 1u
        ))
        return 0;

    return reference_ok && full_ok;
}

static int regression_check_row(
    const regression_baseline_t *b,
    int hash_bits,
    const avalanche_stats_t *s,
    double *out_avg_limit,
    double *out_spread_limit
) {
    if (!b)
        return 1;

    double base_avg = (hash_bits == 256) ? b->base_avg256 : b->base_avg512;
    double base_spread = (hash_bits == 256) ? b->base_spread256 : b->base_spread512;

    double avg_limit = base_avg * (1.0 - (AVG_DROP_PCT / 100.0));
    double spread_limit = base_spread * (1.0 + (SPREAD_INCR_PCT / 100.0));

    if (out_avg_limit) *out_avg_limit = avg_limit;
    if (out_spread_limit) *out_spread_limit = spread_limit;

    double avg_pct = s->avg * 100.0;
    double spread_pct = s->spread * 100.0;

    if (avg_pct + 1e-9 < avg_limit)
        return 0;
    if (spread_pct - 1e-9 > spread_limit)
        return 0;
    return 1;
}

static int test_length_csv(size_t len, flip_mode_t mode) {
    const char *baseline_id = "domain-split-baseline";
    const regression_baseline_t *b = find_baseline(len, mode);

    avalanche_stats_t s256 = compute_stats(len, mode, 256);
    avalanche_stats_t s512 = compute_stats(len, mode, 512);

    double avg_limit256 = 0.0, spread_limit256 = 0.0;
    double avg_limit512 = 0.0, spread_limit512 = 0.0;

    int pass256 = regression_check_row(b, 256, &s256, &avg_limit256, &spread_limit256);
    int pass512 = regression_check_row(b, 512, &s512, &avg_limit512, &spread_limit512);

    int pass = pass256 && pass512;

    printf(
        "%s,%u,%s,256,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s\n",
        baseline_id,
        (unsigned)len,
        flip_mode_name(mode),
        s256.avg * 100.0,
        s256.min * 100.0,
        s256.max * 100.0,
        s256.spread * 100.0,
        b ? avg_limit256 : 0.0,
        b ? spread_limit256 : 0.0,
        pass256 ? "PASS" : "FAIL"
    );
    printf(
        "%s,%u,%s,512,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s\n",
        baseline_id,
        (unsigned)len,
        flip_mode_name(mode),
        s512.avg * 100.0,
        s512.min * 100.0,
        s512.max * 100.0,
        s512.spread * 100.0,
        b ? avg_limit512 : 0.0,
        b ? spread_limit512 : 0.0,
        pass512 ? "PASS" : "FAIL"
    );

    return pass;
}


#include "test_patterns.c"

int main(void) {
    size_t lengths[] = {
        0, 1, 8, 32,
        63, 64, 65,
        127, 128, 129,
        255, 257,
        512, 1024, 4096
    };

    if (!determinism_check()) {
        printf("REGRESSION: determinism FAIL\n");
        return 1;
    }

    if (!invariant_split_covers_input_check()) {
        printf("REGRESSION: invariant FAIL (split coverage)\n");
        return 1;
    }
    if (!reduced_round_margin_check()) {
        printf("REGRESSION: reduced-round margin FAIL\n");
        return 1;
    }

    printf("baseline,len,mode,hash,avg,min,max,spread,avg_limit,spread_limit,pass\n");

    int failures = 0;

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        if (!test_length_csv(lengths[i], FLIP_SINGLE))
            failures++;
        if (!test_length_csv(lengths[i], FLIP_SWEEP))
            failures++;
    }

    if (!run_pattern_stress_tests())
        failures++;

    if (failures == 0) {
        printf("REGRESSION: PASS (avg_drop<=%.1f%% spread_incr<=%.1f%%)\n", AVG_DROP_PCT, SPREAD_INCR_PCT);
        return 0;
    }

    printf("REGRESSION: FAIL rows=%d (avg_drop<=%.1f%% spread_incr<=%.1f%%)\n", failures, AVG_DROP_PCT, SPREAD_INCR_PCT);
    return 1;
}
