#include <stdio.h>
#include <string.h>
#include <stdint.h>














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
    uint8_t mod[MAX_INPUT];

    avalanche_stats_t s;
    s.avg = 0.0;
    s.min = 1.0;
    s.max = 0.0;
    s.spread = 0.0;

    uint32_t fs = flip_seed ? flip_seed : 0xBADC0DEu;
    for (int r = 0; r < ROUNDS; r++) {
        memcpy(mod, base, len);
        if (len > 0) {
            size_t pos = (size_t)(xorshift32(&fs) % (uint32_t)len);
            unsigned bit = (unsigned)(xorshift32(&fs) % 8u);
            mod[pos] ^= (uint8_t)(1u << bit);
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

static int pattern_row(
    pattern_t pat,
    size_t len,
    flip_mode_t mode,
    int hash_bits
) {
    uint8_t base[MAX_INPUT];
    uint8_t ref[MAX_INPUT];

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

    int pass = 1;
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
