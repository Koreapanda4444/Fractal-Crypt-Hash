#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void fill_random(uint8_t *buf, size_t len, uint32_t seed) {
    uint32_t s = seed ? seed : 0xA5A5A5A5u;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(xorshift32(&s) & 0xFFu);
    }
}

static void mutate(uint8_t *buf, size_t len, uint32_t iter) {
    if (!buf || len == 0)
        return;

    size_t idx1 = (size_t)(iter * 1315423911u) % len;
    size_t idx2 = (size_t)(iter * 2654435761u) % len;
    buf[idx1] ^= (uint8_t)(iter & 0xFFu);
    buf[idx2] ^= (uint8_t)((iter >> 8) & 0xFFu);
}

typedef struct {
    size_t big_mb;
    size_t small_kb;
    uint32_t iters_big;
    uint32_t iters_small;
    uint32_t seed;
    int variant;
    int quiet;
} opts_t;

static void opts_default(opts_t *o) {
    memset(o, 0, sizeof(*o));
    o->big_mb = 32;
    o->small_kb = 256;
    o->iters_big = 16;
    o->iters_small = 20000;
    o->seed = 0x12345678u;
    o->variant = 24;
    o->quiet = 0;
}

static int parse_u32(const char *s, uint32_t *out) {
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!s || !*s || (end && *end != '\0'))
        return 0;
    *out = (uint32_t)v;
    return 1;
}

static int parse_size(const char *s, size_t *out) {
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (!s || !*s || (end && *end != '\0'))
        return 0;
    *out = (size_t)v;
    return 1;
}

static void usage(void) {
    fprintf(stderr,
        "Usage: test_stress.exe [--variant 16|24] [--big-mb N] [--small-kb N] [--iters-big N] [--iters-small N] [--seed N] [--quiet]\n"
        "Defaults: --variant 24 --big-mb 32 --iters-big 16 --small-kb 256 --iters-small 20000\n"
    );
}

static volatile int g_max_depth_16 = 0;
static volatile int g_max_depth_24 = 0;

#define FCH_PARAMS_ALLOW_REINCLUDE 1

#define FCH_DEBUG_HOOKS_H 1
#ifndef FCH_HOOK_AFTER_LEAF
#define FCH_HOOK_AFTER_LEAF 1
#endif
#ifndef FCH_HOOK_AFTER_NODE
#define FCH_HOOK_AFTER_NODE 2
#endif
#ifndef FCH_HOOK_AFTER_ROOT
#define FCH_HOOK_AFTER_ROOT 3
#endif

#include "../src/sbox.c"
#include "../src/bitops.c"

#undef FCH_MAX_DEPTH_CAP
#define FCH_MAX_DEPTH_CAP 16

#define fch_hash_256 fch_hash_256_D16
#define fch_hash_512 fch_hash_512_D16
#define fch_hash_256_checked fch_hash_256_checked_D16
#define fch_hash_512_checked fch_hash_512_checked_D16
#define fch_pad fch_pad_D16
#define fch_process fch_process_D16
#define fch_fractal_split fch_fractal_split_D16
#define determine_n determine_n_D16
#define scaled_length scaled_length_D16
#define fch_leaf_compress fch_leaf_compress_D16
#define fch_combine fch_combine_D16
#define fch_debug_emit_root_if fch_debug_emit_root_if_D16

#undef FCH_DEBUG_EMIT
#define FCH_DEBUG_EMIT(_point, _depth, _state, _words) \
    do { \
        (void)(_point); (void)(_state); (void)(_words); \
        if ((_depth) > g_max_depth_16) g_max_depth_16 = (_depth); \
    } while (0)

#define rotl64 rotl64_leaf_D16
#include "../src/leaf.c"
#undef rotl64

#define rotl64 rotl64_combine_D16
#include "../src/combine.c"
#undef rotl64

#include "../src/fractal_split.c"
#include "../src/fractal_process.c"
#include "../src/fch.c"

#undef fch_hash_256
#undef fch_hash_512
#undef fch_hash_256_checked
#undef fch_hash_512_checked
#undef fch_pad
#undef fch_process
#undef fch_fractal_split
#undef determine_n
#undef scaled_length
#undef fch_leaf_compress
#undef fch_combine
#undef fch_debug_emit_root_if

#undef FCH_MAX_DEPTH_CAP
#define FCH_MAX_DEPTH_CAP 24

#define fch_hash_256 fch_hash_256_D24
#define fch_hash_512 fch_hash_512_D24
#define fch_hash_256_checked fch_hash_256_checked_D24
#define fch_hash_512_checked fch_hash_512_checked_D24
#define fch_pad fch_pad_D24
#define fch_process fch_process_D24
#define fch_fractal_split fch_fractal_split_D24
#define determine_n determine_n_D24
#define scaled_length scaled_length_D24
#define fch_leaf_compress fch_leaf_compress_D24
#define fch_combine fch_combine_D24
#define fch_debug_emit_root_if fch_debug_emit_root_if_D24

#undef FCH_DEBUG_EMIT
#define FCH_DEBUG_EMIT(_point, _depth, _state, _words) \
    do { \
        (void)(_point); (void)(_state); (void)(_words); \
        if ((_depth) > g_max_depth_24) g_max_depth_24 = (_depth); \
    } while (0)

#define rotl64 rotl64_leaf_D24
#include "../src/leaf.c"
#undef rotl64

#define rotl64 rotl64_combine_D24
#include "../src/combine.c"
#undef rotl64

#include "../src/fractal_split.c"
#include "../src/fractal_process.c"
#include "../src/fch.c"

#undef fch_hash_256
#undef fch_hash_512
#undef fch_hash_256_checked
#undef fch_hash_512_checked
#undef fch_pad
#undef fch_process
#undef fch_fractal_split
#undef determine_n
#undef scaled_length
#undef fch_leaf_compress
#undef fch_combine
#undef fch_debug_emit_root_if
#undef FCH_PARAMS_ALLOW_REINCLUDE

typedef void (*hash256_fn)(const uint8_t *input, size_t len, uint8_t out[32]);

static void run_one_variant(
    const char *name,
    int cap,
    hash256_fn fn,
    volatile int *max_depth_ptr,
    const opts_t *o
) {
    size_t big_len = o->big_mb * 1000000u;
    size_t small_len = o->small_kb * 1000u;

    uint8_t *big = (uint8_t *)malloc(big_len);
    uint8_t *small = (uint8_t *)malloc(small_len);
    if (!big || !small) {
        fprintf(stderr, "ERR: alloc failed (big=%u bytes, small=%u bytes)\n", (unsigned)big_len, (unsigned)small_len);
        free(big);
        free(small);
        exit(1);
    }

    fill_random(big, big_len, o->seed);
    fill_random(small, small_len, o->seed ^ 0x9E3779B9u);

    uint8_t out[32];
    volatile uint32_t sink = 0;

    *max_depth_ptr = 0;

    if (!o->quiet) {
        fprintf(stderr, "[STRESS] variant=%s cap=%d big=%uMB it_big=%u small=%uKB it_small=%u\n",
            name, cap, (unsigned)o->big_mb, (unsigned)o->iters_big, (unsigned)o->small_kb, (unsigned)o->iters_small);
    }

    for (uint32_t i = 0; i < o->iters_big; i++) {
        mutate(big, big_len, i);
        fn(big, big_len, out);
        sink ^= out[i & 31u];

        if (!o->quiet && ((i + 1) % 4u == 0u)) {
            fprintf(stderr, "  big %u/%u (max_depth=%d)\n", (unsigned)(i + 1), (unsigned)o->iters_big, (int)(*max_depth_ptr));
        }
    }

    for (uint32_t i = 0; i < o->iters_small; i++) {
        mutate(small, small_len, 0xABC00000u ^ i);
        fn(small, small_len, out);
        sink ^= out[(i * 7u) & 31u];

        if (!o->quiet && ((i + 1) % 2000u == 0u)) {
            fprintf(stderr, "  small %u/%u (max_depth=%d)\n", (unsigned)(i + 1), (unsigned)o->iters_small, (int)(*max_depth_ptr));
        }
    }

    printf(
        "SUMMARY,%s,cap=%d,max_depth=%d,cap_margin=%d,big_mb=%u,it_big=%u,small_kb=%u,it_small=%u,sink=%u\n",
        name,
        cap,
        (int)(*max_depth_ptr),
        cap - (int)(*max_depth_ptr),
        (unsigned)o->big_mb,
        (unsigned)o->iters_big,
        (unsigned)o->small_kb,
        (unsigned)o->iters_small,
        (unsigned)sink
    );

    free(big);
    free(small);
}

int main(int argc, char **argv) {
    opts_t o;
    opts_default(&o);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            o.quiet = 1;
        } else if (strcmp(argv[i], "--variant") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32(argv[++i], &v) || (v != 16u && v != 24u)) {
                fprintf(stderr, "ERR: --variant must be 16 or 24\n");
                return 2;
            }
            o.variant = (int)v;
        } else if (strcmp(argv[i], "--big-mb") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &o.big_mb)) return 2;
        } else if (strcmp(argv[i], "--small-kb") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &o.small_kb)) return 2;
        } else if (strcmp(argv[i], "--iters-big") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &o.iters_big)) return 2;
        } else if (strcmp(argv[i], "--iters-small") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &o.iters_small)) return 2;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &o.seed)) return 2;
        } else {
            fprintf(stderr, "ERR: unknown arg: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    printf("case,fields...\n");

    if (o.variant == 16) {
        run_one_variant("fch256_D16", 16, fch_hash_256_D16, &g_max_depth_16, &o);
    } else {
        run_one_variant("fch256_D24", 24, fch_hash_256_D24, &g_max_depth_24, &o);
    }

    fprintf(stderr, "STRESS: PASS (no crash observed)\n");
    return 0;
}
