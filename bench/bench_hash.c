#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "fch.h"
#include "fch_stream.h"

typedef int (*bench_fn)(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
);

typedef struct {
    const char *name;
    bench_fn hash;
} bench_target_t;

static uint64_t now_ns(void) {
    return (uint64_t)clock() *
        (UINT64_C(1000000000) / (uint64_t)CLOCKS_PER_SEC);
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *state = value;
    return value;
}

static void fill_random(uint8_t *buffer, size_t length) {
    uint32_t state = UINT32_C(0xC001D00D);
    for (size_t i = 0; i < length; i++)
        buffer[i] = (uint8_t)xorshift32(&state);
}

static int hash_256_once(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
) {
    return fch_hash_256_checked(input, length, output);
}

static int hash_512_once(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
) {
    return fch_hash_512_checked(input, length, output);
}

static int hash_256_stream(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
) {
    fch256_ctx context;
    fch256_init(&context);

    size_t offset = 0u;
    int ok = 1;
    while (ok && offset < length) {
        size_t chunk = 64u * 1024u;
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

static unsigned int iterations_for_length(size_t length) {
    if (length >= 1024u * 1024u)
        return 4u;
    if (length >= 256u * 1024u)
        return 8u;
    if (length >= 64u * 1024u)
        return 16u;
    if (length >= 16u * 1024u)
        return 32u;
    if (length >= 1024u)
        return 64u;
    return 256u;
}

static int measure(
    const bench_target_t *target,
    const uint8_t *buffer,
    size_t length,
    unsigned int iterations,
    volatile uint32_t *sink
) {
    uint8_t output[64];
    uint64_t start = now_ns();

    for (unsigned int i = 0u; i < iterations; i++) {
        if (!target->hash(buffer, length, output))
            return 0;
        *sink ^= output[i % sizeof(output)];
    }

    uint64_t end = now_ns();
    double seconds = (double)(end - start) / 1000000000.0;
    double megabytes =
        ((double)length * (double)iterations) / 1000000.0;
    double throughput = seconds > 0.0 ? megabytes / seconds : 0.0;

    printf(
        "%s,%u,%u,%.3f\n",
        target->name,
        (unsigned int)length,
        iterations,
        throughput
    );
    return 1;
}

int main(void) {
    static const size_t lengths[] = {
        32u,
        64u,
        128u,
        512u,
        1024u,
        4096u,
        16384u,
        65536u,
        262144u,
        1048576u
    };
    static const bench_target_t targets[] = {
        {"fch256-one-shot", hash_256_once},
        {"fch512-one-shot", hash_512_once},
        {"fch256-stream-64k", hash_256_stream}
    };

    uint8_t *buffer = (uint8_t *)malloc(lengths[
        sizeof(lengths) / sizeof(lengths[0]) - 1u
    ]);
    if (!buffer) {
        fprintf(stderr, "benchmark allocation failed\n");
        return 1;
    }
    fill_random(
        buffer,
        lengths[sizeof(lengths) / sizeof(lengths[0]) - 1u]
    );

    volatile uint32_t sink = 0u;
    puts("algorithm,bytes,iterations,mb_per_second");
    for (size_t target_index = 0;
         target_index < sizeof(targets) / sizeof(targets[0]);
         target_index++) {
        for (size_t length_index = 0;
             length_index < sizeof(lengths) / sizeof(lengths[0]);
             length_index++) {
            if (!measure(
                    &targets[target_index],
                    buffer,
                    lengths[length_index],
                    iterations_for_length(lengths[length_index]),
                    &sink
                )) {
                free(buffer);
                return 1;
            }
        }
    }

    fprintf(stderr, "benchmark sink=%u\n", (unsigned int)sink);
    free(buffer);
    return 0;
}
