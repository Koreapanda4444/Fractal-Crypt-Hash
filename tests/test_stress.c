#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fch_stream.h"

typedef struct {
    size_t big_mb;
    size_t small_kb;
    uint32_t iters_big;
    uint32_t iters_small;
    uint32_t seed;
    int quiet;
} opts_t;

static uint32_t xorshift32(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *state = value;
    return value;
}

static void fill_random(uint8_t *buffer, size_t length, uint32_t seed) {
    uint32_t state = seed ? seed : UINT32_C(0xA5A5A5A5);
    for (size_t i = 0; i < length; i++)
        buffer[i] = (uint8_t)xorshift32(&state);
}

static void mutate(uint8_t *buffer, size_t length, uint32_t iteration) {
    if (!buffer || length == 0u)
        return;

    size_t first =
        ((size_t)iteration * UINT32_C(1315423911) + 17u) % length;
    size_t second =
        ((size_t)iteration * UINT32_C(2654435761) + 43u) % length;
    buffer[first] ^= (uint8_t)(1u << (iteration % 8u));
    buffer[second] ^= (uint8_t)(iteration * 29u + 1u);
}

static void opts_default(opts_t *options) {
    memset(options, 0, sizeof(*options));
    options->big_mb = 32u;
    options->small_kb = 256u;
    options->iters_big = 4u;
    options->iters_small = 512u;
    options->seed = UINT32_C(0x12345678);
}

static int parse_u32(const char *text, uint32_t *value) {
    char *end = NULL;
    unsigned long parsed;

    if (!text || !*text || !value)
        return 0;
    parsed = strtoul(text, &end, 10);
    if (!end || *end != '\0' || parsed > UINT32_MAX)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_size(const char *text, size_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    if (!text || !*text || !value)
        return 0;
    parsed = strtoull(text, &end, 10);
    if (!end || *end != '\0' || parsed > SIZE_MAX)
        return 0;
    *value = (size_t)parsed;
    return 1;
}

static void usage(void) {
    fprintf(
        stderr,
        "Usage: test_stress [--big-mb N] [--small-kb N] "
        "[--iters-big N] [--iters-small N] [--seed N] [--quiet]\n"
    );
}

static int stream_256(
    const uint8_t *input,
    size_t length,
    uint32_t seed,
    uint8_t output[32]
) {
    fch256_ctx context;
    fch256_init(&context);

    size_t offset = 0u;
    uint32_t state = seed;
    int ok = 1;
    while (ok && offset < length) {
        size_t chunk = 1u + (size_t)(xorshift32(&state) % 65536u);
        if (chunk > length - offset)
            chunk = length - offset;
        ok = fch256_update(&context, input + offset, chunk);
        offset += chunk;
    }
    if (ok)
        ok = fch256_final_checked(&context, output);
    fch256_free(&context);
    return ok;
}

static int exercise(
    const char *name,
    uint8_t *buffer,
    size_t length,
    uint32_t iterations,
    uint32_t seed,
    int quiet,
    uint32_t *sink
) {
    for (uint32_t iteration = 0u; iteration < iterations; iteration++) {
        uint8_t digest256[32];
        uint8_t repeated256[32];
        uint8_t streamed256[32];
        uint8_t digest512[64];

        mutate(buffer, length, iteration ^ seed);
        if (!fch_hash_256_checked(buffer, length, digest256) ||
            !fch_hash_256_checked(buffer, length, repeated256) ||
            !fch_hash_512_checked(buffer, length, digest512) ||
            !stream_256(
                buffer,
                length,
                seed ^ iteration ^ UINT32_C(0x9E3779B9),
                streamed256
            ))
            return 0;

        if (memcmp(digest256, repeated256, sizeof(digest256)) != 0 ||
            memcmp(digest256, streamed256, sizeof(digest256)) != 0 ||
            memcmp(digest256, digest512, sizeof(digest256)) == 0)
            return 0;

        *sink ^= digest256[iteration % sizeof(digest256)];
        *sink ^= digest512[(iteration * 7u) % sizeof(digest512)];

        if (!quiet && ((iteration + 1u) % 64u == 0u ||
                       iteration + 1u == iterations)) {
            fprintf(
                stderr,
                "[STRESS] %s %u/%u\n",
                name,
                (unsigned int)(iteration + 1u),
                (unsigned int)iterations
            );
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    opts_t options;
    opts_default(&options);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[i], "--quiet") == 0) {
            options.quiet = 1;
        } else if (strcmp(argv[i], "--big-mb") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &options.big_mb))
                return 2;
        } else if (strcmp(argv[i], "--small-kb") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &options.small_kb))
                return 2;
        } else if (strcmp(argv[i], "--iters-big") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &options.iters_big))
                return 2;
        } else if (strcmp(argv[i], "--iters-small") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &options.iters_small))
                return 2;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &options.seed))
                return 2;
        } else {
            fprintf(stderr, "ERR: unknown argument: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    if (options.big_mb == 0u || options.small_kb == 0u ||
        options.big_mb > SIZE_MAX / 1000000u ||
        options.small_kb > SIZE_MAX / 1000u)
        return 2;

    size_t big_length = options.big_mb * 1000000u;
    size_t small_length = options.small_kb * 1000u;
    uint8_t *big = (uint8_t *)malloc(big_length);
    uint8_t *small = (uint8_t *)malloc(small_length);
    if (!big || !small) {
        free(big);
        free(small);
        fprintf(stderr, "ERR: allocation failed\n");
        return 1;
    }

    fill_random(big, big_length, options.seed);
    fill_random(small, small_length, options.seed ^ UINT32_C(0x9E3779B9));

    uint32_t sink = 0u;
    int ok = exercise(
            "large",
            big,
            big_length,
            options.iters_big,
            options.seed,
            options.quiet,
            &sink
        ) &&
        exercise(
            "small",
            small,
            small_length,
            options.iters_small,
            options.seed ^ UINT32_C(0x85EBCA6B),
            options.quiet,
            &sink
        );

    printf(
        "stress,tree=v2,big_mb=%u,iters_big=%u,small_kb=%u,"
        "iters_small=%u,sink=%u,%s\n",
        (unsigned int)options.big_mb,
        (unsigned int)options.iters_big,
        (unsigned int)options.small_kb,
        (unsigned int)options.iters_small,
        (unsigned int)sink,
        ok ? "PASS" : "FAIL"
    );

    free(big);
    free(small);
    return ok ? 0 : 1;
}
