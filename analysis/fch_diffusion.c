#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fch.h"
#include "fractal.h"
#include "mix.h"
#include "params.h"
#include "bitops.h"
#include "debug_hooks.h"
#include "../tests/test_utils.h"

#define FCH_DIFFUSION_MAX_INPUT 4096
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
    int valid;
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
    { 63,   FLIP_SWEEP, 50.02, 14.45, 50.12, 12.50 },
    { 64,   FLIP_SWEEP, 49.45, 19.14, 49.97, 12.30 },
    { 65,   FLIP_SWEEP, 49.96, 17.19, 50.18, 10.94 },
    { 127,  FLIP_SWEEP, 49.84, 18.75, 49.94, 11.91 },
    { 128,  FLIP_SWEEP, 49.66, 14.84, 50.40, 10.55 },
    { 129,  FLIP_SWEEP, 50.02, 18.75, 50.30, 13.48 },
    { 255,  FLIP_SWEEP, 50.34, 14.84, 50.03, 9.96 },
    { 257,  FLIP_SWEEP, 50.09, 16.02, 49.85, 10.35 },
    { 512,  FLIP_SWEEP, 50.25, 19.14, 50.06, 9.57 },
    { 1024, FLIP_SWEEP, 50.01, 15.23, 50.17, 10.94 },
    { 4096, FLIP_SWEEP, 50.08, 16.80, 49.91, 9.96 },
};

static const regression_baseline_t *find_baseline(size_t len, flip_mode_t mode) {
    for (size_t i = 0; i < sizeof(BASELINES) / sizeof(BASELINES[0]); i++) {
        if (BASELINES[i].len == len && BASELINES[i].mode == mode)
            return &BASELINES[i];
    }
    return NULL;
}

static double hash_diff_ratio(
    const uint8_t *a, size_t alen,
    const uint8_t *b, size_t blen,
    int hash_bits
) {
    uint8_t ha[64], hb[64];
    size_t bytes = hash_bits == 256 ? 32u : 64u;
    int ok = hash_bits == 256
        ? fch_hash_256_checked(a, alen, ha) &&
            fch_hash_256_checked(b, blen, hb)
        : fch_hash_512_checked(a, alen, ha) &&
            fch_hash_512_checked(b, blen, hb);
    if (!ok) {
        fprintf(stderr, "FAIL: diffusion input hashing failed\n");
        return -1.0;
    }
    return bit_diff(ha, hb, bytes) / (double)hash_bits;
}

typedef struct {
    double sum;
    double min;
    double max;
    int count;
} stats_t;

static void stats_init(stats_t *s) {
    s->sum = 0.0;
    s->min = 1.0;
    s->max = 0.0;
    s->count = 0;
}

static void stats_add(stats_t *s, double v) {
    if (s->count == 0) {
        s->sum = v;
        s->min = v;
        s->max = v;
        s->count = 1;
        return;
    }
    s->sum += v;
    if (v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->count++;
}

static avalanche_stats_t compute_stats(size_t len, flip_mode_t mode, int hash_bits) {
    uint8_t base[FCH_DIFFUSION_MAX_INPUT];
    uint8_t mod[FCH_DIFFUSION_MAX_INPUT];
    memset(base, 0xA5, len);

    avalanche_stats_t s;
    s.avg = 0.0;
    s.min = 1.0;
    s.max = 0.0;
    s.spread = 0.0;
    s.valid = 1;

    for (int r = 0; r < ROUNDS; r++) {
        memcpy(mod, base, len);
        if (len > 0) {
            if (mode == FLIP_SINGLE) {
                mod[0] ^= 1;
            } else {
                mod[r % len] ^= (uint8_t)(1u << (unsigned)(r % 8));
            }
        }

        double d = hash_diff_ratio(base, len, mod, len, hash_bits);
        if (d < 0.0) {
            s.valid = 0;
            return s;
        }

        s.avg += d;
        if (d < s.min) s.min = d;
        if (d > s.max) s.max = d;
    }

    s.avg /= ROUNDS;
    s.spread = s.max - s.min;
    return s;
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
    if (!s->valid)
        return 0;
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
    const char *baseline_id = "canonical-tree-v2-baseline";
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

typedef enum {
    PAT_ALL_ZERO = 0,
    PAT_ALL_FF = 1,
    PAT_ABAB = 2,
    PAT_ABCABC = 3,
    PAT_INC = 4,
    PAT_RANDOM = 5,
} pattern_t;

static const char *pattern_name(pattern_t p) {
    switch (p) {
        case PAT_ALL_ZERO: return "all_zero";
        case PAT_ALL_FF: return "all_ff";
        case PAT_ABAB: return "ABAB";
        case PAT_ABCABC: return "ABCABC";
        case PAT_INC: return "inc";
        case PAT_RANDOM: return "random";
        default: return "?";
    }
}

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void fill_pattern(uint8_t *buf, size_t len, pattern_t pat, uint32_t seed) {
    if (!buf) return;

    if (pat == PAT_ALL_ZERO) {
        memset(buf, 0x00, len);
        return;
    }
    if (pat == PAT_ALL_FF) {
        memset(buf, 0xFF, len);
        return;
    }
    if (pat == PAT_ABAB) {
        for (size_t i = 0; i < len; i++)
            buf[i] = (uint8_t)((i & 1) ? 0xBA : 0xAB);
        return;
    }
    if (pat == PAT_ABCABC) {
        static const uint8_t p3[3] = { 0xAB, 0xBC, 0xCA };
        for (size_t i = 0; i < len; i++)
            buf[i] = p3[i % 3];
        return;
    }
    if (pat == PAT_INC) {
        for (size_t i = 0; i < len; i++)
            buf[i] = (uint8_t)i;
        return;
    }

    uint32_t s = seed ? seed : 0xC001D00Du;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(xorshift32(&s) & 0xFFu);
    }
}

static avalanche_stats_t compute_stats_on_base(
    const uint8_t *base,
    size_t len,
    uint32_t flip_seed,
    int hash_bits
) {
    uint8_t mod[FCH_DIFFUSION_MAX_INPUT];

    avalanche_stats_t s;
    s.avg = 0.0;
    s.min = 1.0;
    s.max = 0.0;
    s.spread = 0.0;
    s.valid = 1;

    uint32_t fs = flip_seed ? flip_seed : 0xBADC0DEu;
    for (int r = 0; r < ROUNDS; r++) {
        memcpy(mod, base, len);
        if (len > 0) {
            size_t pos = (size_t)(xorshift32(&fs) % (uint32_t)len);
            unsigned bit = (unsigned)(xorshift32(&fs) % 8u);
            mod[pos] ^= (uint8_t)(1u << bit);
        }

        double d = hash_diff_ratio(base, len, mod, len, hash_bits);
        if (d < 0.0) {
            s.valid = 0;
            return s;
        }

        s.avg += d;
        if (d < s.min) s.min = d;
        if (d > s.max) s.max = d;
    }

    s.avg /= ROUNDS;
    s.spread = s.max - s.min;
    return s;
}

static int pattern_row(
    pattern_t pat,
    size_t len,
    flip_mode_t mode,
    int hash_bits
) {
    uint8_t base[FCH_DIFFUSION_MAX_INPUT];
    uint8_t ref[FCH_DIFFUSION_MAX_INPUT];

    fill_pattern(base, len, pat, 0);
    fill_pattern(ref,  len, PAT_RANDOM, (uint32_t)(0x12345678u ^ (uint32_t)len ^ (uint32_t)mode ^ (uint32_t)hash_bits));

    (void)mode;
    const uint32_t flip_seed = (uint32_t)(0x13579BDFu ^ (uint32_t)len ^ (uint32_t)hash_bits);

    avalanche_stats_t sp = compute_stats_on_base(base, len, flip_seed, hash_bits);
    avalanche_stats_t sr = compute_stats_on_base(ref,  len, flip_seed, hash_bits);

    const regression_baseline_t *b = find_baseline(len, mode);

    double ref_avg_pct = sr.avg * 100.0;
    double ref_spread_pct = sr.spread * 100.0;
    double ref_avg_limit = ref_avg_pct * (1.0 - (AVG_DROP_PCT / 100.0));

    const double PATTERN_SPREAD_INCR_PCT = 150.0;
    const double SPREAD_ABS_SLACK_PCT = 5.0;

    double base_avg_limit = 0.0, base_spread_limit = 0.0;
    (void)regression_check_row(b, hash_bits, &sp, &base_avg_limit, &base_spread_limit);

    double sp_avg_pct = sp.avg * 100.0;
    double sp_spread_pct = sp.spread * 100.0;

    double used_avg_limit = ref_avg_limit;
    if (b && base_avg_limit < used_avg_limit)
        used_avg_limit = base_avg_limit;

    double used_spread_limit = ref_spread_pct * (1.0 + (PATTERN_SPREAD_INCR_PCT / 100.0)) + SPREAD_ABS_SLACK_PCT;
    if (b && base_spread_limit > used_spread_limit)
        used_spread_limit = base_spread_limit;

    int pass = sp.valid && sr.valid;
    if (sp_avg_pct + 1e-9 < used_avg_limit)
        pass = 0;
    if (sp_spread_pct - 1e-9 > used_spread_limit)
        pass = 0;

    printf(
        "pattern,%s,%u,randflip,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s\n",
        pattern_name(pat),
        (unsigned)len,
        hash_bits,
        sp.avg * 100.0,
        sp.min * 100.0,
        sp.max * 100.0,
        sp.spread * 100.0,
        sr.avg * 100.0,
        sr.spread * 100.0,
        used_avg_limit,
        used_spread_limit,
        b ? base_avg_limit : 0.0,
        b ? base_spread_limit : 0.0,
        pass ? "PASS" : "FAIL"
    );

    return pass;
}

static int run_pattern_stress_tests(void) {

    size_t lengths[] = { 64, 128, 255, 257, 512, 1024, 4096 };
    pattern_t pats[] = { PAT_ALL_ZERO, PAT_ALL_FF, PAT_ABAB, PAT_ABCABC, PAT_INC };

    printf("kind,pat,len,mode,hash,avg,min,max,spread,ref_avg,ref_spread,ref_avg_limit,ref_spread_limit,base_avg_limit,base_spread_limit,pass\n");

    int failures = 0;

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        size_t len = lengths[i];
        for (size_t p = 0; p < sizeof(pats) / sizeof(pats[0]); p++) {
            if (!pattern_row(pats[p], len, FLIP_SWEEP, 256)) failures++;
            if (!pattern_row(pats[p], len, FLIP_SWEEP, 512)) failures++;
        }
    }

    if (failures == 0) {
        printf("PATTERNS: PASS\n");
        return 1;
    }

    printf("PATTERNS: FAIL rows=%d\n", failures);
    return 0;
}

#define BOUNDARY_REQUIRE(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", (message)); \
            return 0; \
        } \
    } while (0)

static int boundary_diffusion(void) {
    static const size_t lengths[] = {
        FCH_TREE_LEAF_BYTES - 10u,
        FCH_TREE_LEAF_BYTES - 9u,
        FCH_TREE_LEAF_BYTES - 8u,
        FCH_TREE_LEAF_BYTES * 2u - 10u,
        FCH_TREE_LEAF_BYTES * 2u - 9u,
        FCH_TREE_LEAF_BYTES * 2u - 8u
    };
    uint8_t message[FCH_TREE_LEAF_BYTES * 2u];
    uint8_t changed[FCH_TREE_LEAF_BYTES * 2u];
    uint32_t state = UINT32_C(0xA5A55A5A);
    for (size_t i = 0; i < sizeof(message); i++)
        message[i] = (uint8_t)(xorshift32(&state) + (uint32_t)i * 29u);

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        size_t length = lengths[i];
        uint8_t base256[32];
        uint8_t changed256[32];
        uint8_t base512[64];
        uint8_t changed512[64];

        memcpy(changed, message, length);
        changed[length / 2u] ^= (uint8_t)(1u << (i % 8u));
        BOUNDARY_REQUIRE(fch_hash_256_checked(message, length, base256) &&
                fch_hash_256_checked(changed, length, changed256) &&
                fch_hash_512_checked(message, length, base512) &&
                fch_hash_512_checked(changed, length, changed512),
                "boundary hash failed");
        BOUNDARY_REQUIRE(bit_diff(base256, changed256, sizeof(base256)) >= 64,
                "weak 256-bit diffusion at a leaf boundary");
        BOUNDARY_REQUIRE(bit_diff(base512, changed512, sizeof(base512)) >= 160,
                "weak 512-bit diffusion at a leaf boundary");
    }
    return 1;
}

static int run_avalanche(void) {
    size_t lengths[] = {
        0, 1, 8, 32,
        63, 64, 65,
        127, 128, 129,
        255, 257,
        512, 1024, 4096
    };

    if (!reduced_round_margin_check()) {
        printf("DIFFUSION: reduced-round margin FAIL\n");
        return 0;
    }

    printf("baseline,len,mode,hash,avg,min,max,spread,avg_limit,spread_limit,pass\n");

    int failures = 0;

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        if (!test_length_csv(lengths[i], FLIP_SINGLE))
            failures++;
        if (!test_length_csv(lengths[i], FLIP_SWEEP))
            failures++;
    }

    if (!boundary_diffusion())
        failures++;

    if (!run_pattern_stress_tests())
        failures++;

    if (failures == 0) {
        printf("DIFFUSION: PASS (avg_drop<=%.1f%% spread_incr<=%.1f%%)\n", AVG_DROP_PCT, SPREAD_INCR_PCT);
        return 1;
    }

    printf("DIFFUSION: FAIL rows=%d (avg_drop<=%.1f%% spread_incr<=%.1f%%)\n", failures, AVG_DROP_PCT, SPREAD_INCR_PCT);
    return 0;
}

static void fill_deterministic(uint8_t *buf, size_t len, uint32_t seed) {
    uint32_t s = seed ? seed : 0xC001D00Du;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(xorshift32(&s) & 0xFFu);
    }
}

static size_t build_padding_tail(size_t msg_len, uint8_t *out, size_t out_cap) {

    size_t min_len = msg_len + 1 + 8;
    size_t padded_len = (min_len < 64) ? 64 : min_len;

    size_t tail_len = padded_len - msg_len;
    if (!out || out_cap < tail_len)
        return 0;

    memset(out, 0, tail_len);
    out[0] = 0x80;

    uint64_t bit_len = (uint64_t)msg_len * 8u;
    fch_store_le64(out + tail_len - 8, bit_len);

    return tail_len;
}

static int check_length_case(
    const char *kind,
    const char *case_id,
    const uint8_t *a,
    size_t alen,
    const uint8_t *b,
    size_t blen,
    stats_t *s256,
    stats_t *s512
) {
    double d256 = hash_diff_ratio(a, alen, b, blen, 256);
    double d512 = hash_diff_ratio(a, alen, b, blen, 512);

    if (d256 < 0.0 || d512 < 0.0)
        return 0;

    stats_add(s256, d256);
    stats_add(s512, d512);

    printf(
        "%s,%s,%u,%u,256,%.2f\n",
        kind,
        case_id,
        (unsigned)alen,
        (unsigned)blen,
        d256 * 100.0
    );
    printf(
        "%s,%s,%u,%u,512,%.2f\n",
        kind,
        case_id,
        (unsigned)alen,
        (unsigned)blen,
        d512 * 100.0
    );

    return 1;
}

static int require_threshold(const char *label, const stats_t *s, double min_required) {
    double avg = (s->count > 0) ? (s->sum / (double)s->count) : 0.0;
    fprintf(
        stderr,
        "SUMMARY,%s,avg=%.2f%%,min=%.2f%%,max=%.2f%%,n=%d\n",
        label,
        avg * 100.0,
        s->min * 100.0,
        s->max * 100.0,
        s->count
    );

    if (s->count == 0)
        return 0;
    if (s->min + 1e-12 < min_required)
        return 0;
    return 1;
}

static int run_length_variation(void) {

    const double MIN_DIFF = 0.35;

    uint8_t stream[FCH_DIFFUSION_MAX_INPUT];
    fill_deterministic(stream, sizeof(stream), 0x12345678u);

    struct { size_t a, b; const char *id; } pairs[] = {
        { 0,    1,    "L0_vs_L1" },
        { 1,    2,    "L1_vs_L2" },
        { 63,   64,   "L63_vs_L64" },
        { 64,   65,   "L64_vs_L65" },
        { 127,  128,  "L127_vs_L128" },
        { 128,  129,  "L128_vs_L129" },
        { 255,  256,  "L255_vs_L256" },
        { 256,  257,  "L256_vs_L257" },
        { 511,  512,  "L511_vs_L512" },
        { 512,  513,  "L512_vs_L513" },
        { 1023, 1024, "L1023_vs_L1024" },
    };

    stats_t adj256, adj512;
    stats_init(&adj256);
    stats_init(&adj512);

    stats_t pad256, pad512;
    stats_init(&pad256);
    stats_init(&pad512);

    int ok = 1;
    printf("kind,case,len_a,len_b,hash,diff_pct\n");

    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        size_t la = pairs[i].a;
        size_t lb = pairs[i].b;
        if (lb > sizeof(stream))
            continue;
        ok &= check_length_case("adj_len", pairs[i].id, stream, la, stream, lb, &adj256, &adj512);
    }

    {
        uint8_t prefix[37];
        uint8_t suffix[29];
        fill_deterministic(prefix, sizeof(prefix), 0xA11CE5E1u);
        fill_deterministic(suffix, sizeof(suffix), 0x51FF1D00u);

        uint8_t pad_tail[128];
        size_t tail_len = build_padding_tail(sizeof(prefix), pad_tail, sizeof(pad_tail));
        if (tail_len == 0) {
            fprintf(stderr, "ERR: padding tail build failed\n");
            return 0;
        }

        uint8_t a[FCH_DIFFUSION_MAX_INPUT];
        uint8_t b[FCH_DIFFUSION_MAX_INPUT];

        size_t alen = 0;
        memcpy(a + alen, prefix, sizeof(prefix)); alen += sizeof(prefix);
        memcpy(a + alen, suffix, sizeof(suffix)); alen += sizeof(suffix);

        size_t blen = 0;
        memcpy(b + blen, prefix, sizeof(prefix)); blen += sizeof(prefix);
        memcpy(b + blen, pad_tail, tail_len);     blen += tail_len;
        memcpy(b + blen, suffix, sizeof(suffix)); blen += sizeof(suffix);

        ok &= check_length_case("pad_inject", "prefix_suffix_vs_prefix_pad_suffix", a, alen, b, blen, &pad256, &pad512);

        uint8_t c[FCH_DIFFUSION_MAX_INPUT];
        size_t clen = 0;
        memcpy(c + clen, prefix, sizeof(prefix)); clen += sizeof(prefix);
        memcpy(c + clen, pad_tail, tail_len);     clen += tail_len;

        ok &= check_length_case("pad_inject", "prefix_vs_prefix_pad", prefix, sizeof(prefix), c, clen, &pad256, &pad512);
    }

    ok &= require_threshold("adj_len_256", &adj256, MIN_DIFF);
    ok &= require_threshold("adj_len_512", &adj512, MIN_DIFF);
    ok &= require_threshold("pad_inject_256", &pad256, MIN_DIFF);
    ok &= require_threshold("pad_inject_512", &pad512, MIN_DIFF);

    if (ok) {
        printf("LENGTH_VARIATION: PASS (min_diff>=%.0f%%)\n", MIN_DIFF * 100.0);
        return 1;
    }

    printf("LENGTH_VARIATION: FAIL (min_diff>=%.0f%%)\n", MIN_DIFF * 100.0);
    return 0;
}

#define MAX_LEVEL 64
#define MAX_WORDS FCH_512_STATE_WORDS
#define TREE_INPUT_LEN 8192

typedef struct {
    uint64_t x[MAX_WORDS];
    int seen;
} agg_t;

static agg_t g_base_node[MAX_LEVEL];
static agg_t g_mod_node[MAX_LEVEL];
static agg_t g_base_leaf[MAX_LEVEL];
static agg_t g_mod_leaf[MAX_LEVEL];

static uint64_t g_base_root[MAX_WORDS];
static uint64_t g_mod_root[MAX_WORDS];
static int g_base_root_seen = 0;
static int g_mod_root_seen = 0;

static int g_collecting_base = 1;
static int g_max_level_seen = -1;
static size_t g_state_words = 0;

static void reset_aggs(void) {
    memset(g_base_node, 0, sizeof(g_base_node));
    memset(g_mod_node, 0, sizeof(g_mod_node));
    memset(g_base_leaf, 0, sizeof(g_base_leaf));
    memset(g_mod_leaf, 0, sizeof(g_mod_leaf));
    memset(g_base_root, 0, sizeof(g_base_root));
    memset(g_mod_root, 0, sizeof(g_mod_root));
    g_base_root_seen = 0;
    g_mod_root_seen = 0;
    g_max_level_seen = -1;
}

static void agg_xor(agg_t *a, const uint64_t *state, size_t words) {
    if (!a || !state)
        return;
    if (words > MAX_WORDS)
        words = MAX_WORDS;
    for (size_t i = 0; i < words; i++) {
        a->x[i] ^= state[i];
    }
    a->seen = 1;
}

#if defined(__GNUC__)
static inline int popcount64(uint64_t v) {
    return __builtin_popcountll((unsigned long long)v);
}
#else
static inline int popcount64(uint64_t v) {
    int c = 0;
    while (v) {
        c += (int)(v & 1u);
        v >>= 1;
    }
    return c;
}
#endif

static double diff_ratio_words(const uint64_t *a, const uint64_t *b, size_t words) {
    if (!a || !b || words == 0)
        return 0.0;
    if (words > MAX_WORDS)
        words = MAX_WORDS;

    int bits = 0;
    for (size_t i = 0; i < words; i++) {
        bits += popcount64(a[i] ^ b[i]);
    }
    return bits / (double)(words * 64.0);
}

void fch_debug_hook(
    fch_hook_point_t point,
    int level,
    const uint64_t *state,
    size_t state_words
) {
    if (!state)
        return;
    if (level < 0 || level >= MAX_LEVEL)
        return;

    g_state_words = state_words;
    if (level > g_max_level_seen)
        g_max_level_seen = level;

    if (g_collecting_base) {
        if (point == FCH_HOOK_AFTER_LEAF) {
            agg_xor(&g_base_leaf[level], state, state_words);
        } else if (point == FCH_HOOK_AFTER_NODE) {
            agg_xor(&g_base_node[level], state, state_words);
        } else if (point == FCH_HOOK_AFTER_ROOT) {
            if (state_words > MAX_WORDS)
                state_words = MAX_WORDS;
            memcpy(g_base_root, state, state_words * sizeof(uint64_t));
            g_base_root_seen = 1;
        }
    } else {
        if (point == FCH_HOOK_AFTER_LEAF) {
            agg_xor(&g_mod_leaf[level], state, state_words);
        } else if (point == FCH_HOOK_AFTER_NODE) {
            agg_xor(&g_mod_node[level], state, state_words);
        } else if (point == FCH_HOOK_AFTER_ROOT) {
            if (state_words > MAX_WORDS)
                state_words = MAX_WORDS;
            memcpy(g_mod_root, state, state_words * sizeof(uint64_t));
            g_mod_root_seen = 1;
        }
    }
}

static int run_process(const uint8_t *data, size_t len, int collecting_base) {
    g_collecting_base = collecting_base;
    fch_state_t out = fch_process(data, len, FCH_256_STATE_WORDS);
    if (!out.state)
        return 0;
    free(out.state);
    return 1;
}

static void print_stage_stats(const char *stage, const stats_t *stats, int max_level) {
    for (int d = 0; d <= max_level; d++) {
        if (stats[d].count == 0)
            continue;
        double avg = stats[d].sum / (double)stats[d].count;
        printf(
            "%s,%d,%.2f,%.2f,%.2f,%d\n",
            stage,
            d,
            avg * 100.0,
            stats[d].min * 100.0,
            stats[d].max * 100.0,
            stats[d].count
        );
    }
}

static int run_tree_diffusion(void) {
    uint8_t base[TREE_INPUT_LEN];
    uint8_t mod[TREE_INPUT_LEN];

    for (size_t i = 0; i < TREE_INPUT_LEN; i++) {
        base[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }

    stats_t node_stats[MAX_LEVEL];
    stats_t leaf_stats[MAX_LEVEL];
    stats_t root_stats[MAX_LEVEL];
    memset(node_stats, 0, sizeof(node_stats));
    memset(leaf_stats, 0, sizeof(leaf_stats));
    memset(root_stats, 0, sizeof(root_stats));

    int global_max_level = -1;

    for (int r = 0; r < ROUNDS; r++) {
        memcpy(mod, base, TREE_INPUT_LEN);
        mod[r % TREE_INPUT_LEN] ^= (uint8_t)(1u << (unsigned)(r % 8));

        reset_aggs();
        if (!run_process(base, TREE_INPUT_LEN, 1))
            return 0;
        int base_max = g_max_level_seen;
        if (!run_process(mod, TREE_INPUT_LEN, 0))
            return 0;
        int mod_max = g_max_level_seen;

        int max_level = base_max > mod_max ? base_max : mod_max;
        if (max_level > global_max_level)
            global_max_level = max_level;

        if (!g_base_root_seen || !g_mod_root_seen ||
            g_state_words != FCH_INTERNAL_STATE_WORDS || max_level < 0)
            return 0;
        size_t words = g_state_words;

        for (int d = 0; d <= max_level; d++) {
            if (!(g_base_node[d].seen || g_mod_node[d].seen))
                continue;
            double dv = diff_ratio_words(g_base_node[d].x, g_mod_node[d].x, words);
            stats_add(&node_stats[d], dv);
        }

        for (int d = 0; d <= max_level; d++) {
            if (!(g_base_leaf[d].seen || g_mod_leaf[d].seen))
                continue;
            double dv = diff_ratio_words(g_base_leaf[d].x, g_mod_leaf[d].x, words);
            stats_add(&leaf_stats[d], dv);
        }

        double root_diff = diff_ratio_words(g_base_root, g_mod_root, words);
        stats_add(&root_stats[max_level], root_diff);
    }

    printf("stage,level,avg,min,max,rounds\n");
    print_stage_stats("node", node_stats, global_max_level);
    print_stage_stats("leaf", leaf_stats, global_max_level);
    print_stage_stats("root", root_stats, global_max_level);

    if (global_max_level >= 2) {
        int mid = global_max_level / 2;
        if (node_stats[mid].count > 0) {
            double avg = node_stats[mid].sum / (double)node_stats[mid].count;
            fprintf(
                stderr,
                "INFO: mid level=%d node diffusion avg=%.2f%% (min=%.2f%% max=%.2f%%)\n",
                mid,
                avg * 100.0,
                node_stats[mid].min * 100.0,
                node_stats[mid].max * 100.0
            );
        }
        if (leaf_stats[0].count > 0) {
            double avg = leaf_stats[0].sum / (double)leaf_stats[0].count;
            fprintf(
                stderr,
                "INFO: leaf level=%d leaf diffusion avg=%.2f%% (min=%.2f%% max=%.2f%%)\n",
                0,
                avg * 100.0,
                leaf_stats[0].min * 100.0,
                leaf_stats[0].max * 100.0
            );
        }
        if (root_stats[global_max_level].count > 0) {
            double avg = root_stats[global_max_level].sum / (double)root_stats[global_max_level].count;
            fprintf(
                stderr,
                "INFO: root diffusion avg=%.2f%% (min=%.2f%% max=%.2f%%)\n",
                avg * 100.0,
                root_stats[global_max_level].min * 100.0,
                root_stats[global_max_level].max * 100.0
            );
        }
    }

    return 1;
}

int main(int argc, char **argv) {
    const char *section = argc == 1 ? "all" : argv[1];
    if (argc > 2 ||
        (strcmp(section, "all") != 0 &&
         strcmp(section, "avalanche") != 0 &&
         strcmp(section, "length") != 0 &&
         strcmp(section, "tree") != 0)) {
        fprintf(stderr, "Usage: fch_diffusion [all|avalanche|length|tree]\n");
        return 2;
    }

    int ok = 1;
    if (strcmp(section, "all") == 0 || strcmp(section, "avalanche") == 0)
        ok &= run_avalanche();
    if (strcmp(section, "all") == 0 || strcmp(section, "length") == 0)
        ok &= run_length_variation();
    if (strcmp(section, "all") == 0 || strcmp(section, "tree") == 0)
        ok &= run_tree_diffusion();

    puts("Diffusion screens report empirical observations, not security proofs.");
    printf("DIFFUSION_SCREENS: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
