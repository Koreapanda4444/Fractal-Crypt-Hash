#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "fractal.h"
#include "params.h"
#include "bitops.h"

typedef fch_state_t (*fch_process_fn)(
    const uint8_t *data,
    size_t length,
    int depth,
    size_t state_words
);

typedef enum {
    PAT_CONST = 0,
    PAT_RAMP = 1,
    PAT_XORSHIFT = 2
} pattern_t;

static const char *pattern_name(pattern_t p) {
    switch (p) {
        case PAT_CONST: return "const";
        case PAT_RAMP: return "ramp";
        case PAT_XORSHIFT: return "xorshift";
        default: return "?";
    }
}

typedef struct {
    double avg;
    double min;
    double max;
    double spread;
} stats_t;

static int popcount64(uint64_t v) {
#if defined(__GNUC__)
    return __builtin_popcountll((unsigned long long)v);
#else
    int c = 0;
    while (v) {
        c += (int)(v & 1u);
        v >>= 1;
    }
    return c;
#endif
}

static double diff_ratio_words(const uint64_t *a, const uint64_t *b, size_t words) {
    int bits = 0;
    for (size_t i = 0; i < words; i++) {
        bits += popcount64(a[i] ^ b[i]);
    }
    return bits / (double)(words * 64.0);
}

static uint8_t *pad_input(const uint8_t *input, size_t length, size_t min_block, size_t *out_len) {
    if (!out_len) return NULL;

    size_t min_len = length + 1 + 8;
    size_t padded_len = min_len;
    if (padded_len < min_block)
        padded_len = min_block;

    uint8_t *buf = (uint8_t *)calloc(padded_len, 1);
    if (!buf) {
        *out_len = 0;
        return NULL;
    }

    if (length > 0) {
        memcpy(buf, input, length);
    }

    buf[length] = 0x80;

    
    uint64_t bit_len = (uint64_t)length * 8u;
    fch_store_le64(buf + padded_len - 8, bit_len);

    *out_len = padded_len;
    return buf;
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

    if (pat == PAT_CONST) {
        memset(buf, 0xA5, len);
        return;
    }

    if (pat == PAT_RAMP) {
        for (size_t i = 0; i < len; i++)
            buf[i] = (uint8_t)i;
        return;
    }

    uint32_t s = seed ? seed : 0xC001D00Du;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(xorshift32(&s) & 0xFFu);
    }
}

static stats_t measure_diffusion(
    fch_process_fn fn,
    size_t min_block,
    pattern_t pat,
    size_t input_len,
    int rounds
) {
    uint8_t *base = (uint8_t *)malloc(input_len);
    uint8_t *mod  = (uint8_t *)malloc(input_len);

    stats_t s;
    s.avg = 0.0;
    s.min = 1.0;
    s.max = 0.0;
    s.spread = 0.0;

    if (!base || !mod) {
        free(base);
        free(mod);
        return s;
    }

    fill_pattern(base, input_len, pat, 0x12345678u);
    uint32_t flip_s = 0xBADC0DEu ^ (uint32_t)input_len ^ (uint32_t)pat;

    for (int r = 0; r < rounds; r++) {
        memcpy(mod, base, input_len);

        size_t pos = (size_t)(xorshift32(&flip_s) % (uint32_t)input_len);
        unsigned bit = (unsigned)(xorshift32(&flip_s) % 8u);
        mod[pos] ^= (uint8_t)(1u << bit);

        size_t bl = 0, ml = 0;
        uint8_t *bp = pad_input(base, input_len, min_block, &bl);
        uint8_t *mp = pad_input(mod,  input_len, min_block, &ml);
        if (!bp || !mp || bl != ml) {
            free(bp);
            free(mp);
            continue;
        }

        fch_state_t a = fn(bp, bl, 0, FCH_256_STATE_WORDS);
        fch_state_t b = fn(mp, ml, 0, FCH_256_STATE_WORDS);

        if (a.state && b.state) {
            double d = diff_ratio_words(a.state, b.state, FCH_256_STATE_WORDS);
            s.avg += d;
            if (d < s.min) s.min = d;
            if (d > s.max) s.max = d;
        }

        free(a.state);
        free(b.state);
        free(bp);
        free(mp);
    }

    free(base);
    free(mod);

    s.avg /= (double)rounds;
    s.spread = s.max - s.min;
    return s;
}

static double stability_score(const stats_t *s) {
    
    double avg_penalty = fabs(s->avg - 0.5);
    double spread_penalty = s->spread;
    return 1.0 - (avg_penalty * 2.0) - (spread_penalty * 1.0);
}

static int rounds_for_len(size_t len) {
    if (len >= 262144) return 16;
    if (len >= 65536)  return 32;
    if (len >= 8192)   return 64;
    return 128;
}



#include "../src/sbox.c"
#include "../src/bitops.c"


#define FCH_PARAMS_ALLOW_REINCLUDE 1
#undef FCH_N_MIN
#undef FCH_N_MAX
#undef FCH_MAX_DEPTH_CAP
#define FCH_N_MIN 2
#define FCH_N_MAX 2
#define FCH_MAX_DEPTH_CAP 16
#define fch_leaf_compress fch_leaf_compress_N22
#define fch_leaf_compress_reader fch_leaf_compress_reader_N22
#define fch_combine fch_combine_N22
#define fch_fractal_split fch_fractal_split_N22
#define fch_fractal_split_reader fch_fractal_split_reader_N22
#define fch_process fch_process_N22
#define fch_process_reader fch_process_reader_N22
#define rotl64 rotl64_leaf_N22
#include "../src/leaf.c"
#undef rotl64
#define rotl64 rotl64_combine_N22
#include "../src/combine.c"
#undef rotl64
#define determine_n determine_n_N22
#define scaled_length scaled_length_N22
#include "../src/fractal_split.c"
#undef determine_n
#undef scaled_length
#define fch_debug_emit_root_if fch_debug_emit_root_if_N22
#include "../src/fractal_process.c"
#undef fch_debug_emit_root_if
enum { MINBLOCK_N22 = FCH_MIN_BLOCK_SIZE };
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef fch_process
#undef fch_process_reader

#undef FCH_N_MIN
#undef FCH_N_MAX
#undef FCH_MAX_DEPTH_CAP
#define FCH_N_MIN 2
#define FCH_N_MAX 6
#define FCH_MAX_DEPTH_CAP 16
#define fch_leaf_compress fch_leaf_compress_N26
#define fch_leaf_compress_reader fch_leaf_compress_reader_N26
#define fch_combine fch_combine_N26
#define fch_fractal_split fch_fractal_split_N26
#define fch_fractal_split_reader fch_fractal_split_reader_N26
#define fch_process fch_process_N26
#define fch_process_reader fch_process_reader_N26
#define rotl64 rotl64_leaf_N26
#include "../src/leaf.c"
#undef rotl64
#define rotl64 rotl64_combine_N26
#include "../src/combine.c"
#undef rotl64
#define determine_n determine_n_N26
#define scaled_length scaled_length_N26
#include "../src/fractal_split.c"
#undef determine_n
#undef scaled_length
#define fch_debug_emit_root_if fch_debug_emit_root_if_N26
#include "../src/fractal_process.c"
#undef fch_debug_emit_root_if
enum { MINBLOCK_N26 = FCH_MIN_BLOCK_SIZE };
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef fch_process
#undef fch_process_reader

#undef FCH_N_MIN
#undef FCH_N_MAX
#undef FCH_MAX_DEPTH_CAP
#define FCH_N_MIN 4
#define FCH_N_MAX 6
#define FCH_MAX_DEPTH_CAP 16
#define fch_leaf_compress fch_leaf_compress_N46
#define fch_leaf_compress_reader fch_leaf_compress_reader_N46
#define fch_combine fch_combine_N46
#define fch_fractal_split fch_fractal_split_N46
#define fch_fractal_split_reader fch_fractal_split_reader_N46
#define fch_process fch_process_N46
#define fch_process_reader fch_process_reader_N46
#define rotl64 rotl64_leaf_N46
#include "../src/leaf.c"
#undef rotl64
#define rotl64 rotl64_combine_N46
#include "../src/combine.c"
#undef rotl64
#define determine_n determine_n_N46
#define scaled_length scaled_length_N46
#include "../src/fractal_split.c"
#undef determine_n
#undef scaled_length
#define fch_debug_emit_root_if fch_debug_emit_root_if_N46
#include "../src/fractal_process.c"
#undef fch_debug_emit_root_if
enum { MINBLOCK_N46 = FCH_MIN_BLOCK_SIZE };
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef fch_process
#undef fch_process_reader

#undef FCH_N_MIN
#undef FCH_N_MAX
#undef FCH_MAX_DEPTH_CAP
#define FCH_N_MIN 6
#define FCH_N_MAX 6
#define FCH_MAX_DEPTH_CAP 16
#define fch_leaf_compress fch_leaf_compress_N66
#define fch_leaf_compress_reader fch_leaf_compress_reader_N66
#define fch_combine fch_combine_N66
#define fch_fractal_split fch_fractal_split_N66
#define fch_fractal_split_reader fch_fractal_split_reader_N66
#define fch_process fch_process_N66
#define fch_process_reader fch_process_reader_N66
#define rotl64 rotl64_leaf_N66
#include "../src/leaf.c"
#undef rotl64
#define rotl64 rotl64_combine_N66
#include "../src/combine.c"
#undef rotl64
#define determine_n determine_n_N66
#define scaled_length scaled_length_N66
#include "../src/fractal_split.c"
#undef determine_n
#undef scaled_length
#define fch_debug_emit_root_if fch_debug_emit_root_if_N66
#include "../src/fractal_process.c"
#undef fch_debug_emit_root_if
enum { MINBLOCK_N66 = FCH_MIN_BLOCK_SIZE };
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef fch_process
#undef fch_process_reader


#undef FCH_N_MIN
#undef FCH_N_MAX
#undef FCH_MAX_DEPTH_CAP
#define FCH_N_MIN 2
#define FCH_N_MAX 6
#define FCH_MAX_DEPTH_CAP 4
#define fch_leaf_compress fch_leaf_compress_D4
#define fch_leaf_compress_reader fch_leaf_compress_reader_D4
#define fch_combine fch_combine_D4
#define fch_fractal_split fch_fractal_split_D4
#define fch_fractal_split_reader fch_fractal_split_reader_D4
#define fch_process fch_process_D4
#define fch_process_reader fch_process_reader_D4
#define rotl64 rotl64_leaf_D4
#include "../src/leaf.c"
#undef rotl64
#define rotl64 rotl64_combine_D4
#include "../src/combine.c"
#undef rotl64
#define determine_n determine_n_D4
#define scaled_length scaled_length_D4
#include "../src/fractal_split.c"
#undef determine_n
#undef scaled_length
#define fch_debug_emit_root_if fch_debug_emit_root_if_D4
#include "../src/fractal_process.c"
#undef fch_debug_emit_root_if
enum { MINBLOCK_D4 = FCH_MIN_BLOCK_SIZE };
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef fch_process
#undef fch_process_reader

#undef FCH_MAX_DEPTH_CAP
#define FCH_MAX_DEPTH_CAP 8
#define fch_leaf_compress fch_leaf_compress_D8
#define fch_leaf_compress_reader fch_leaf_compress_reader_D8
#define fch_combine fch_combine_D8
#define fch_fractal_split fch_fractal_split_D8
#define fch_fractal_split_reader fch_fractal_split_reader_D8
#define fch_process fch_process_D8
#define fch_process_reader fch_process_reader_D8
#define rotl64 rotl64_leaf_D8
#include "../src/leaf.c"
#undef rotl64
#define rotl64 rotl64_combine_D8
#include "../src/combine.c"
#undef rotl64
#define determine_n determine_n_D8
#define scaled_length scaled_length_D8
#include "../src/fractal_split.c"
#undef determine_n
#undef scaled_length
#define fch_debug_emit_root_if fch_debug_emit_root_if_D8
#include "../src/fractal_process.c"
#undef fch_debug_emit_root_if
enum { MINBLOCK_D8 = FCH_MIN_BLOCK_SIZE };
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef fch_process
#undef fch_process_reader

#undef FCH_MAX_DEPTH_CAP
#define FCH_MAX_DEPTH_CAP 16
#define fch_leaf_compress fch_leaf_compress_D16
#define fch_leaf_compress_reader fch_leaf_compress_reader_D16
#define fch_combine fch_combine_D16
#define fch_fractal_split fch_fractal_split_D16
#define fch_fractal_split_reader fch_fractal_split_reader_D16
#define fch_process fch_process_D16
#define fch_process_reader fch_process_reader_D16
#define rotl64 rotl64_leaf_D16
#include "../src/leaf.c"
#undef rotl64
#define rotl64 rotl64_combine_D16
#include "../src/combine.c"
#undef rotl64
#define determine_n determine_n_D16
#define scaled_length scaled_length_D16
#include "../src/fractal_split.c"
#undef determine_n
#undef scaled_length
#define fch_debug_emit_root_if fch_debug_emit_root_if_D16
#include "../src/fractal_process.c"
#undef fch_debug_emit_root_if
enum { MINBLOCK_D16 = FCH_MIN_BLOCK_SIZE };
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef fch_process
#undef fch_process_reader

#undef FCH_PARAMS_ALLOW_REINCLUDE

typedef struct {
    const char *group;
    const char *id;
    int nmin;
    int nmax;
    int depthcap;
    size_t min_block;
    fch_process_fn fn;
} cfg_t;

static const cfg_t CFGS[] = {
    {"n-range", "N=2..2", 2, 2, 16, MINBLOCK_N22, fch_process_N22},
    {"n-range", "N=2..6(default)", 2, 6, 16, MINBLOCK_N26, fch_process_N26},
    {"n-range", "N=4..6", 4, 6, 16, MINBLOCK_N46, fch_process_N46},
    {"n-range", "N=6..6", 6, 6, 16, MINBLOCK_N66, fch_process_N66},

    {"depthcap", "D=4", 2, 6, 4, MINBLOCK_D4, fch_process_D4},
    {"depthcap", "D=8", 2, 6, 8, MINBLOCK_D8, fch_process_D8},
    {"depthcap", "D=16(default)", 2, 6, 16, MINBLOCK_D16, fch_process_D16},
};

static int is_default_cfg(const cfg_t *c) {
    if (!c) return 0;
    return (c->nmin == 2 && c->nmax == 6 && c->depthcap == 16);
}

int main(void) {
    const pattern_t patterns[] = { PAT_CONST, PAT_RAMP, PAT_XORSHIFT };
    const size_t lengths[] = { 1024, 8192, 65536, 262144 };

    printf("group,cfg,nmin,nmax,depthcap,len,pattern,avg,min,max,spread,score\n");

    
    double best_score_n = -1e9;
    double best_score_d = -1e9;
    double default_score_n = -1e9;
    double default_score_d = -1e9;
    const cfg_t *best_n = NULL;
    const cfg_t *best_d = NULL;

    for (size_t ci = 0; ci < sizeof(CFGS) / sizeof(CFGS[0]); ci++) {
        const cfg_t *cfg = &CFGS[ci];

        double score_sum = 0.0;
        int score_cnt = 0;

        for (size_t li = 0; li < sizeof(lengths) / sizeof(lengths[0]); li++) {
            size_t input_len = lengths[li];
            int rounds = rounds_for_len(input_len);

            for (size_t pi = 0; pi < sizeof(patterns) / sizeof(patterns[0]); pi++) {
                pattern_t pat = patterns[pi];
                stats_t st = measure_diffusion(cfg->fn, cfg->min_block, pat, input_len, rounds);
                double score = stability_score(&st);

                score_sum += score;
                score_cnt++;

                printf(
                    "%s,%s,%d,%d,%d,%u,%s,%.2f,%.2f,%.2f,%.2f,%.4f\n",
                    cfg->group,
                    cfg->id,
                    cfg->nmin,
                    cfg->nmax,
                    cfg->depthcap,
                    (unsigned)input_len,
                    pattern_name(pat),
                    st.avg * 100.0,
                    st.min * 100.0,
                    st.max * 100.0,
                    st.spread * 100.0,
                    score
                );
            }
        }

        double avg_score = score_sum / (double)score_cnt;

        fprintf(
            stderr,
            "SCORE,%s,%s,avg_score=%.4f%s\n",
            cfg->group,
            cfg->id,
            avg_score,
            is_default_cfg(cfg) ? ",default" : ""
        );

        if (strcmp(cfg->group, "n-range") == 0) {
            if (avg_score > best_score_n) {
                best_score_n = avg_score;
                best_n = cfg;
            }
        } else if (strcmp(cfg->group, "depthcap") == 0) {
            if (avg_score > best_score_d) {
                best_score_d = avg_score;
                best_d = cfg;
            }
        }

        if (is_default_cfg(cfg)) {
            if (strcmp(cfg->group, "n-range") == 0)
                default_score_n = avg_score;
            else if (strcmp(cfg->group, "depthcap") == 0)
                default_score_d = avg_score;
            fprintf(
                stderr,
                "INFO: default cfg avg_score=%.4f (group=%s)\n",
                avg_score,
                cfg->group
            );
        }
    }

    if (best_n) {
        fprintf(stderr, "BEST[n-range]: %s (avg_score=%.4f)\n", best_n->id, best_score_n);
    }
    if (best_d) {
        fprintf(stderr, "BEST[depthcap]: %s (avg_score=%.4f)\n", best_d->id, best_score_d);
    }

    if (default_score_n < 0.80 || default_score_d < 0.80) {
        fprintf(
            stderr,
            "SPLIT_SENSITIVITY: FAIL (default scores %.4f, %.4f)\n",
            default_score_n,
            default_score_d
        );
        return 1;
    }

    fprintf(
        stderr,
        "SPLIT_SENSITIVITY: PASS (default scores %.4f, %.4f)\n",
        default_score_n,
        default_score_d
    );

    return 0;
}
