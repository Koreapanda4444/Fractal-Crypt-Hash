#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#undef malloc
#undef calloc
#undef free

#include <stdlib.h>

#include "fch.h"
#include "fch_stream.h"
#include "params.h"

typedef union {
    max_align_t alignment;
    size_t size;
} fch_bench_allocation_header_t;

static size_t allocation_current;
static size_t allocation_peak;
static size_t allocation_calls;
static int allocation_error;

static void allocation_reset(void) {
    allocation_current = 0u;
    allocation_peak = 0u;
    allocation_calls = 0u;
    allocation_error = 0;
}

void *fch_bench_malloc(size_t size) {
    allocation_calls++;
    if (size > SIZE_MAX - sizeof(fch_bench_allocation_header_t)) {
        allocation_error = 1;
        return NULL;
    }

    fch_bench_allocation_header_t *header =
        (fch_bench_allocation_header_t *)malloc(
            sizeof(*header) + size
        );
    if (!header)
        return NULL;

    if (allocation_current > SIZE_MAX - size) {
        allocation_error = 1;
        free(header);
        return NULL;
    }

    header->size = size;
    allocation_current += size;
    if (allocation_current > allocation_peak)
        allocation_peak = allocation_current;
    return header + 1;
}

void *fch_bench_calloc(size_t count, size_t size) {
    if (count != 0u && size > SIZE_MAX / count) {
        allocation_calls++;
        allocation_error = 1;
        return NULL;
    }

    size_t total = count * size;
    void *pointer = fch_bench_malloc(total);
    if (pointer)
        memset(pointer, 0, total);
    return pointer;
}

void fch_bench_free(void *pointer) {
    if (!pointer)
        return;

    fch_bench_allocation_header_t *header =
        (fch_bench_allocation_header_t *)pointer - 1;
    if (header->size > allocation_current) {
        allocation_error = 1;
        allocation_current = 0u;
    } else {
        allocation_current -= header->size;
    }
    free(header);
}

typedef int (*bench_fn)(
    const uint8_t *input,
    size_t length,
    size_t chunk_size,
    uint8_t output[64]
);

typedef enum {
    BENCH_ONE_SHOT,
    BENCH_STREAM
} bench_kind_t;

typedef struct {
    const char *name;
    bench_fn hash;
    size_t chunk_size;
    bench_kind_t kind;
} bench_target_t;

typedef struct {
    double seconds;
    double throughput;
    size_t peak_heap;
    size_t allocations_per_hash;
} bench_result_t;

enum {
    TIMING_PATTERN_COUNT = 4,
    TIMING_TRIALS = 7,
    TIMING_ITERATIONS = 16,
    TIMING_LENGTH = 65536
};

static const double TIMING_RATIO_LIMIT = 1.50;

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
    for (size_t i = 0u; i < length; i++)
        buffer[i] = (uint8_t)xorshift32(&state);
}

static int hash_256_once(
    const uint8_t *input,
    size_t length,
    size_t chunk_size,
    uint8_t output[64]
) {
    (void)chunk_size;
    return fch_hash_256_checked(input, length, output);
}

static int hash_512_once(
    const uint8_t *input,
    size_t length,
    size_t chunk_size,
    uint8_t output[64]
) {
    (void)chunk_size;
    return fch_hash_512_checked(input, length, output);
}

static int hash_256_stream(
    const uint8_t *input,
    size_t length,
    size_t chunk_size,
    uint8_t output[64]
) {
    if (chunk_size == 0u)
        return 0;

    fch256_ctx context;
    fch256_init(&context);

    size_t offset = 0u;
    int ok = 1;
    while (ok && offset < length) {
        size_t count = chunk_size;
        if (count > length - offset)
            count = length - offset;
        ok = fch256_update(&context, input + offset, count);
        offset += count;
    }
    if (ok)
        ok = fch256_final_checked(&context, output);
    fch256_free(&context);
    return ok;
}

static int hash_512_stream(
    const uint8_t *input,
    size_t length,
    size_t chunk_size,
    uint8_t output[64]
) {
    if (chunk_size == 0u)
        return 0;

    fch512_ctx context;
    fch512_init(&context);

    size_t offset = 0u;
    int ok = 1;
    while (ok && offset < length) {
        size_t count = chunk_size;
        if (count > length - offset)
            count = length - offset;
        ok = fch512_update(&context, input + offset, count);
        offset += count;
    }
    if (ok)
        ok = fch512_final_checked(&context, output);
    fch512_free(&context);
    return ok;
}

static unsigned int iterations_for_length(size_t length, int quick) {
    if (quick)
        return length < 1024u ? 16u : 1u;
    if (length >= 8u * 1024u * 1024u)
        return 2u;
    if (length >= 1024u * 1024u)
        return 4u;
    if (length >= 256u * 1024u)
        return 8u;
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
    volatile uint32_t *sink,
    bench_result_t *result
) {
    if (!target || !target->hash || !buffer || iterations == 0u ||
        !sink || !result)
        return 0;

    uint8_t output[64];
    allocation_reset();
    clock_t start = clock();
    if (start == (clock_t)-1)
        return 0;

    for (unsigned int i = 0u; i < iterations; i++) {
        if (!target->hash(
                buffer,
                length,
                target->chunk_size,
                output
            ))
            return 0;
        if (allocation_error || allocation_current != 0u)
            return 0;
        *sink ^= output[i % 32u];
    }

    clock_t end = clock();
    if (end == (clock_t)-1 || end < start)
        return 0;
    if (allocation_calls % iterations != 0u)
        return 0;

    result->seconds =
        (double)(end - start) / (double)CLOCKS_PER_SEC;
    double megabytes =
        ((double)length * (double)iterations) / 1000000.0;
    result->throughput = result->seconds > 0.0
        ? megabytes / result->seconds
        : 0.0;
    result->peak_heap = allocation_peak;
    result->allocations_per_hash = allocation_calls / iterations;
    return 1;
}

static void fill_timing_pattern(
    uint8_t *buffer,
    size_t length,
    unsigned int pattern
) {
    if (pattern == 0u) {
        memset(buffer, 0, length);
        return;
    }
    if (pattern == 1u) {
        memset(buffer, 0xFF, length);
        return;
    }
    if (pattern == 2u) {
        for (size_t i = 0u; i < length; i++)
            buffer[i] = (uint8_t)(i * 131u + i / 17u);
        return;
    }
    fill_random(buffer, length);
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static double median_time(const double values[TIMING_TRIALS]) {
    double ordered[TIMING_TRIALS];
    memcpy(ordered, values, sizeof(ordered));
    qsort(
        ordered,
        TIMING_TRIALS,
        sizeof(ordered[0]),
        compare_double
    );
    return ordered[TIMING_TRIALS / 2u];
}

static int run_timing_check(void) {
    static const bench_target_t targets[] = {
        {"fch256-one-shot", hash_256_once, 0u, BENCH_ONE_SHOT},
        {"fch512-one-shot", hash_512_once, 0u, BENCH_ONE_SHOT},
        {"fch256-stream", hash_256_stream, 1024u, BENCH_STREAM},
        {"fch512-stream", hash_512_stream, 1024u, BENCH_STREAM}
    };
    uint8_t *patterns = (uint8_t *)malloc(
        (size_t)TIMING_PATTERN_COUNT * TIMING_LENGTH
    );
    if (!patterns)
        return 0;

    for (unsigned int pattern = 0u;
         pattern < TIMING_PATTERN_COUNT;
         pattern++) {
        fill_timing_pattern(
            patterns + (size_t)pattern * TIMING_LENGTH,
            TIMING_LENGTH,
            pattern
        );
    }

    volatile uint32_t sink = 0u;
    int ok = 1;
    for (size_t target_index = 0u;
         target_index < sizeof(targets) / sizeof(targets[0]);
         target_index++) {
        double samples[TIMING_PATTERN_COUNT][TIMING_TRIALS];
        size_t expected_peak = 0u;
        size_t expected_allocations = 0u;
        int profiles_equal = 1;

        for (unsigned int pattern = 0u;
             pattern < TIMING_PATTERN_COUNT;
             pattern++) {
            bench_result_t warmup;
            if (!measure(
                    &targets[target_index],
                    patterns + (size_t)pattern * TIMING_LENGTH,
                    TIMING_LENGTH,
                    2u,
                    &sink,
                    &warmup
                )) {
                free(patterns);
                return 0;
            }
        }

        for (unsigned int trial = 0u;
             trial < TIMING_TRIALS;
             trial++) {
            for (unsigned int slot = 0u;
                 slot < TIMING_PATTERN_COUNT;
                 slot++) {
                unsigned int pattern =
                    (slot + trial) % TIMING_PATTERN_COUNT;
                bench_result_t result;
                if (!measure(
                        &targets[target_index],
                        patterns + (size_t)pattern * TIMING_LENGTH,
                        TIMING_LENGTH,
                        TIMING_ITERATIONS,
                        &sink,
                        &result
                    )) {
                    free(patterns);
                    return 0;
                }
                samples[pattern][trial] =
                    result.seconds / (double)TIMING_ITERATIONS;
                if (trial == 0u && slot == 0u) {
                    expected_peak = result.peak_heap;
                    expected_allocations =
                        result.allocations_per_hash;
                } else if (result.peak_heap != expected_peak ||
                           result.allocations_per_hash !=
                               expected_allocations) {
                    profiles_equal = 0;
                }
            }
        }

        double minimum = 0.0;
        double maximum = 0.0;
        for (unsigned int pattern = 0u;
             pattern < TIMING_PATTERN_COUNT;
             pattern++) {
            double median = median_time(samples[pattern]);
            if (pattern == 0u || median < minimum)
                minimum = median;
            if (pattern == 0u || median > maximum)
                maximum = median;
        }

        double ratio = minimum > 0.0 ? maximum / minimum : 0.0;
        int target_ok =
            profiles_equal &&
            minimum > 0.0 &&
            ratio <= TIMING_RATIO_LIMIT;
        printf(
            "timing_content,algorithm=%s,bytes=%u,patterns=%u,"
            "trials=%u,iterations=%u,min_median_us=%.3f,"
            "max_median_us=%.3f,ratio=%.3f,limit=%.2f,"
            "allocations=%zu,peak_heap=%zu,%s\n",
            targets[target_index].name,
            TIMING_LENGTH,
            TIMING_PATTERN_COUNT,
            TIMING_TRIALS,
            TIMING_ITERATIONS,
            minimum * 1000000.0,
            maximum * 1000000.0,
            ratio,
            TIMING_RATIO_LIMIT,
            expected_allocations,
            expected_peak,
            target_ok ? "PASS" : "FAIL"
        );
        if (!target_ok)
            ok = 0;
    }

    fprintf(stderr, "timing sink=%u\n", (unsigned int)sink);
    free(patterns);
    return ok;
}

static int validate_scaling(
    const bench_target_t *target,
    size_t length,
    const bench_result_t *result,
    size_t *stream_peak
) {
    if (!target || !result || !stream_peak)
        return 0;

    if (target->kind == BENCH_ONE_SHOT) {
        size_t padded_length = length + 9u;
        if (padded_length < FCH_PADDING_MIN_BYTES)
            padded_length = FCH_PADDING_MIN_BYTES;
        size_t expected_peak = padded_length +
            FCH_INTERNAL_STATE_WORDS * sizeof(uint64_t);
        return result->allocations_per_hash == 2u &&
            result->peak_heap == expected_peak;
    }

    if (result->allocations_per_hash != 1u || result->peak_heap == 0u)
        return 0;
    if (*stream_peak == 0u) {
        *stream_peak = result->peak_heap;
        return 1;
    }
    return result->peak_heap == *stream_peak;
}

static void print_result(
    const bench_target_t *target,
    size_t length,
    unsigned int iterations,
    const bench_result_t *result
) {
    printf(
        "%s,%zu,%zu,%u,%.6f,%.3f,%zu,%zu\n",
        target->name,
        length,
        target->chunk_size,
        iterations,
        result->seconds,
        result->throughput,
        result->peak_heap,
        result->allocations_per_hash
    );
}

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s [--quick|--timing-check]\n", program);
}

int main(int argc, char **argv) {
    static const size_t full_lengths[] = {
        64u,
        1024u,
        16384u,
        262144u,
        1048576u,
        8388608u
    };
    static const size_t quick_lengths[] = {
        64u,
        1024u,
        65536u,
        1048576u
    };
    static const bench_target_t targets[] = {
        {"fch256-one-shot", hash_256_once, 0u, BENCH_ONE_SHOT},
        {"fch512-one-shot", hash_512_once, 0u, BENCH_ONE_SHOT},
        {"fch256-stream", hash_256_stream, 1u, BENCH_STREAM},
        {"fch256-stream", hash_256_stream, 64u, BENCH_STREAM},
        {"fch256-stream", hash_256_stream, 1024u, BENCH_STREAM},
        {"fch256-stream", hash_256_stream, 65536u, BENCH_STREAM},
        {"fch512-stream", hash_512_stream, 1024u, BENCH_STREAM},
        {"fch512-stream", hash_512_stream, 65536u, BENCH_STREAM}
    };

    int quick = 0;
    if (argc == 2 && strcmp(argv[1], "--quick") == 0) {
        quick = 1;
    } else if (argc == 2 &&
               strcmp(argv[1], "--timing-check") == 0) {
        return run_timing_check() ? 0 : 1;
    } else if (argc != 1) {
        usage(argv[0]);
        return 2;
    }

    const size_t *lengths = quick ? quick_lengths : full_lengths;
    size_t length_count = quick
        ? sizeof(quick_lengths) / sizeof(quick_lengths[0])
        : sizeof(full_lengths) / sizeof(full_lengths[0]);
    size_t maximum_length = lengths[length_count - 1u];

    uint8_t *buffer = (uint8_t *)malloc(maximum_length);
    if (!buffer) {
        fprintf(stderr, "benchmark input allocation failed\n");
        return 1;
    }
    fill_random(buffer, maximum_length);

    volatile uint32_t sink = 0u;
    size_t stream_peak = 0u;
    puts(
        "algorithm,bytes,chunk_bytes,iterations,seconds,"
        "mb_per_second,peak_heap_bytes,allocations_per_hash"
    );

    for (size_t target_index = 0u;
         target_index < sizeof(targets) / sizeof(targets[0]);
         target_index++) {
        for (size_t length_index = 0u;
             length_index < length_count;
             length_index++) {
            unsigned int iterations =
                iterations_for_length(lengths[length_index], quick);
            bench_result_t result;
            if (!measure(
                    &targets[target_index],
                    buffer,
                    lengths[length_index],
                    iterations,
                    &sink,
                    &result
                ) ||
                !validate_scaling(
                    &targets[target_index],
                    lengths[length_index],
                    &result,
                    &stream_peak
                )) {
                fprintf(
                    stderr,
                    "benchmark validation failed: %s bytes=%zu chunk=%zu\n",
                    targets[target_index].name,
                    lengths[length_index],
                    targets[target_index].chunk_size
                );
                free(buffer);
                return 1;
            }
            print_result(
                &targets[target_index],
                lengths[length_index],
                iterations,
                &result
            );
        }
    }

    fprintf(
        stderr,
        "benchmark sink=%u stream_peak_heap_bytes=%zu\n",
        (unsigned int)sink,
        stream_peak
    );
    free(buffer);
    return 0;
}
