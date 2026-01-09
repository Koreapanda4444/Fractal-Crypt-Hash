#include <stdio.h>
#include <string.h>

#include "fch.h"
#include "fractal.h"
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
    { 63,   FLIP_SWEEP, 49.33, 25.78, 49.00, 28.52 },
    { 64,   FLIP_SWEEP, 49.26, 30.47, 49.17, 28.71 },
    { 65,   FLIP_SWEEP, 49.32, 21.88, 49.26, 33.59 },
    { 127,  FLIP_SWEEP, 49.72, 14.84, 49.99, 10.74 },
    { 128,  FLIP_SWEEP, 49.74, 17.58, 49.99, 10.16 },
    { 129,  FLIP_SWEEP, 49.78, 17.97, 50.26, 10.16 },
    { 255,  FLIP_SWEEP, 50.61, 18.75, 48.81, 17.97 },
    { 257,  FLIP_SWEEP, 49.86, 12.50, 48.86, 17.58 },
    { 512,  FLIP_SWEEP, 49.63, 16.41, 50.04, 12.30 },
    { 1024, FLIP_SWEEP, 49.93, 17.97, 50.04, 12.30 },
    { 4096, FLIP_SWEEP, 50.00, 15.23, 49.55, 11.91 },
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
    const char *baseline_id = "regress-commit12";
    const regression_baseline_t *b = find_baseline(len, mode);

    avalanche_stats_t s256 = compute_stats(len, mode, 256);
    avalanche_stats_t s512 = compute_stats(len, mode, 512);

    double avg_limit256 = 0.0, spread_limit256 = 0.0;
    double avg_limit512 = 0.0, spread_limit512 = 0.0;

    int pass256 = regression_check_row(b, 256, &s256, &avg_limit256, &spread_limit256);
    int pass512 = regression_check_row(b, 512, &s512, &avg_limit512, &spread_limit512);

    int pass = pass256 && pass512;

    printf(
        "%s,%zu,%s,256,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s\n",
        baseline_id,
        len,
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
        "%s,%zu,%s,512,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s\n",
        baseline_id,
        len,
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

    printf("baseline,len,mode,hash,avg,min,max,spread,avg_limit,spread_limit,pass\n");

    int failures = 0;

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        if (!test_length_csv(lengths[i], FLIP_SINGLE))
            failures++;
        if (!test_length_csv(lengths[i], FLIP_SWEEP))
            failures++;
    }

    if (failures == 0) {
        printf("REGRESSION: PASS (avg_drop<=%.1f%% spread_incr<=%.1f%%)\n", AVG_DROP_PCT, SPREAD_INCR_PCT);
        return 0;
    }

    printf("REGRESSION: FAIL rows=%d (avg_drop<=%.1f%% spread_incr<=%.1f%%)\n", failures, AVG_DROP_PCT, SPREAD_INCR_PCT);
    return 1;
}
