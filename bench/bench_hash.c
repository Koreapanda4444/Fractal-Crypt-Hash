#include <inttypes.h>
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

#ifdef FCH_BENCH_OPENSSL
#include <openssl/crypto.h>
#include <openssl/evp.h>
#endif

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
    BENCH_STREAM,
    BENCH_PEER
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
    BASELINE_LENGTH_COUNT = 6,
    BASELINE_TARGET_COUNT = 8,
    PROFILE_TARGET_LIMIT = BASELINE_TARGET_COUNT + 3,
    BASELINE_WARMUPS = 1,
    BASELINE_TRIALS = 5,
    TIMING_PATTERN_COUNT = 4,
    TIMING_TRIALS = 7,
    TIMING_ITERATIONS = 16,
    TIMING_LENGTH = 65536
};

static const uint32_t BENCH_INPUT_SEED = UINT32_C(0xC001D00D);
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
    uint32_t state = BENCH_INPUT_SEED;
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

#ifdef FCH_BENCH_OPENSSL
static int hash_peer(
    const uint8_t *input, size_t length, uint8_t output[64],
    const EVP_MD *algorithm, unsigned int expected_length
) {
    unsigned int output_length = 0u;
    return EVP_Digest(input, length, output, &output_length, algorithm, NULL) == 1 &&
        output_length == expected_length;
}

static int hash_sha256_once(
    const uint8_t *input, size_t length, size_t chunk_size, uint8_t output[64]
) {
    (void)chunk_size;
    return hash_peer(input, length, output, EVP_sha256(), 32u);
}

static int hash_sha512_once(
    const uint8_t *input, size_t length, size_t chunk_size, uint8_t output[64]
) {
    (void)chunk_size;
    return hash_peer(input, length, output, EVP_sha512(), 64u);
}

static int hash_blake2b_once(
    const uint8_t *input, size_t length, size_t chunk_size, uint8_t output[64]
) {
    (void)chunk_size;
    return hash_peer(input, length, output, EVP_blake2b512(), 64u);
}

static int check_peer_vectors(void) {
    static const struct {
        bench_fn hash;
        const char *hex;
    } vectors[] = {
        {hash_sha256_once,
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {hash_sha512_once,
         "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2"
         "192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
        {hash_blake2b_once,
         "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
         "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"}
    };
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0u; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        uint8_t output[64];
        char hex[129];
        if (!vectors[i].hash((const uint8_t *)"abc", 3u, 0u, output))
            return 0;
        size_t length = strlen(vectors[i].hex) / 2u;
        for (size_t j = 0u; j < length; j++) {
            hex[j * 2u] = hex_digits[output[j] >> 4u];
            hex[j * 2u + 1u] = hex_digits[output[j] & 15u];
        }
        hex[length * 2u] = '\0';
        if (strcmp(hex, vectors[i].hex) != 0)
            return 0;
    }
    return 1;
}
#endif

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

static unsigned int baseline_iterations_for_length(size_t length) {
    if (length >= 8u * 1024u * 1024u)
        return 2u;
    if (length >= 1024u * 1024u)
        return 8u;
    if (length >= 256u * 1024u)
        return 32u;
    if (length >= 16u * 1024u)
        return 512u;
    if (length >= 1024u)
        return 8192u;
    return 65536u;
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

    if (target->kind == BENCH_PEER)
        return 1;

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

static void print_baseline_result(
    const bench_target_t *target,
    size_t length,
    unsigned int iterations,
    const bench_result_t *result,
    const char *profile
) {
    printf(
        "%s,%08" PRIx32 ",process_cpu,%u,%u,"
        "%s,%zu,%zu,%u,%.6f,%.3f,",
        profile,
        BENCH_INPUT_SEED,
        BASELINE_WARMUPS,
        BASELINE_TRIALS,
        target->name,
        length,
        target->chunk_size,
        iterations,
        result->seconds,
        result->throughput
    );
    if (target->kind == BENCH_PEER)
        puts("unmeasured,unmeasured");
    else
        printf("%zu,%zu\n", result->peak_heap, result->allocations_per_hash);
}

static int run_baseline_profile(
    const bench_target_t *targets,
    size_t target_count,
    const size_t lengths[BASELINE_LENGTH_COUNT],
    const uint8_t *buffer,
    volatile uint32_t *sink,
    size_t *stream_peak,
    const char *profile
) {
    double samples
        [PROFILE_TARGET_LIMIT]
        [BASELINE_LENGTH_COUNT]
        [BASELINE_TRIALS];
    size_t expected_peak
        [PROFILE_TARGET_LIMIT]
        [BASELINE_LENGTH_COUNT];
    size_t expected_allocations
        [PROFILE_TARGET_LIMIT]
        [BASELINE_LENGTH_COUNT];

    if (target_count == 0u || target_count > PROFILE_TARGET_LIMIT)
        return 0;
    size_t case_count = target_count * BASELINE_LENGTH_COUNT;

    for (unsigned int warmup = 0u;
         warmup < BASELINE_WARMUPS;
         warmup++) {
        for (size_t case_index = 0u;
             case_index < case_count;
             case_index++) {
            size_t target_index = case_index / BASELINE_LENGTH_COUNT;
            size_t length_index = case_index % BASELINE_LENGTH_COUNT;
            bench_result_t current;
            unsigned int iterations =
                baseline_iterations_for_length(lengths[length_index]);
            if (!measure(
                    &targets[target_index],
                    buffer,
                    lengths[length_index],
                    iterations,
                    sink,
                    &current
                )) {
                fprintf(
                    stderr,
                    "baseline warmup failed: %s bytes=%zu chunk=%zu\n",
                    targets[target_index].name,
                    lengths[length_index],
                    targets[target_index].chunk_size
                );
                return 0;
            }
            if (warmup == 0u) {
                expected_peak[target_index][length_index] =
                    current.peak_heap;
                expected_allocations[target_index][length_index] =
                    current.allocations_per_hash;
            } else if (
                current.peak_heap !=
                    expected_peak[target_index][length_index] ||
                current.allocations_per_hash !=
                    expected_allocations[target_index][length_index]
            ) {
                return 0;
            }
        }
    }

    for (unsigned int trial = 0u; trial < BASELINE_TRIALS; trial++) {
        for (size_t position = 0u;
             position < case_count;
             position++) {
            size_t case_index =
                (position + (size_t)trial * 13u) % case_count;
            size_t target_index = case_index / BASELINE_LENGTH_COUNT;
            size_t length_index = case_index % BASELINE_LENGTH_COUNT;
            bench_result_t current;
            unsigned int iterations =
                baseline_iterations_for_length(lengths[length_index]);
            if (!measure(
                    &targets[target_index],
                    buffer,
                    lengths[length_index],
                    iterations,
                    sink,
                    &current
                ) ||
                current.peak_heap !=
                    expected_peak[target_index][length_index] ||
                current.allocations_per_hash !=
                    expected_allocations[target_index][length_index]) {
                fprintf(
                    stderr,
                    "baseline trial failed: %s bytes=%zu chunk=%zu\n",
                    targets[target_index].name,
                    lengths[length_index],
                    targets[target_index].chunk_size
                );
                return 0;
            }
            samples[target_index][length_index][trial] = current.seconds;
        }
    }

    for (size_t target_index = 0u;
         target_index < target_count;
         target_index++) {
        for (size_t length_index = 0u;
             length_index < BASELINE_LENGTH_COUNT;
             length_index++) {
            qsort(
                samples[target_index][length_index],
                BASELINE_TRIALS,
                sizeof(samples[target_index][length_index][0]),
                compare_double
            );
            unsigned int iterations =
                baseline_iterations_for_length(lengths[length_index]);
            bench_result_t result;
            result.seconds =
                samples[target_index][length_index]
                    [BASELINE_TRIALS / 2u];
            double megabytes =
                ((double)lengths[length_index] * (double)iterations) /
                1000000.0;
            result.throughput = result.seconds > 0.0
                ? megabytes / result.seconds
                : 0.0;
            result.peak_heap =
                expected_peak[target_index][length_index];
            result.allocations_per_hash =
                expected_allocations[target_index][length_index];
            if (result.seconds <= 0.0 ||
                !validate_scaling(
                    &targets[target_index],
                    lengths[length_index],
                    &result,
                    stream_peak
                )) {
                fprintf(
                    stderr,
                    "baseline validation failed: %s bytes=%zu chunk=%zu\n",
                    targets[target_index].name,
                    lengths[length_index],
                    targets[target_index].chunk_size
                );
                return 0;
            }
            print_baseline_result(
                &targets[target_index],
                lengths[length_index],
                iterations,
                &result,
                profile
            );
        }
    }
    return 1;
}

static void usage(const char *program) {
    fprintf(
        stderr,
        "Usage: %s [--quick|--baseline|--timing-check|--peers|--peer-version]\n",
        program
    );
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
        {"fch512-stream", hash_512_stream, 65536u, BENCH_STREAM},
#ifdef FCH_BENCH_OPENSSL
        {"openssl-sha256", hash_sha256_once, 0u, BENCH_PEER},
        {"openssl-sha512", hash_sha512_once, 0u, BENCH_PEER},
        {"openssl-blake2b512", hash_blake2b_once, 0u, BENCH_PEER},
#endif
    };

    int quick = 0;
    int baseline = 0;
    size_t target_count = BASELINE_TARGET_COUNT;
    const char *profile = "baseline-v1";
    if (argc == 2 && strcmp(argv[1], "--quick") == 0) {
        quick = 1;
    } else if (argc == 2 && strcmp(argv[1], "--baseline") == 0) {
        baseline = 1;
    } else if (argc == 2 &&
               strcmp(argv[1], "--timing-check") == 0) {
        return run_timing_check() ? 0 : 1;
#ifdef FCH_BENCH_OPENSSL
    } else if (argc == 2 && strcmp(argv[1], "--peer-version") == 0) {
        puts(OpenSSL_version(OPENSSL_VERSION));
        return 0;
    } else if (argc == 2 && strcmp(argv[1], "--peers") == 0) {
        if (!check_peer_vectors()) {
            fprintf(stderr, "peer digest verification failed\n");
            return 1;
        }
        baseline = 1;
        target_count = sizeof(targets) / sizeof(targets[0]);
        profile = "peers-v1";
#endif
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
    if (baseline) {
        puts(
            "profile,input_seed,timer,warmups,trials,algorithm,bytes,"
            "chunk_bytes,iterations,median_seconds,mb_per_second,"
            "peak_heap_bytes,allocations_per_hash"
        );
    } else {
        puts(
            "algorithm,bytes,chunk_bytes,iterations,seconds,"
            "mb_per_second,peak_heap_bytes,allocations_per_hash"
        );
    }

    if (baseline) {
        int ok = run_baseline_profile(
            targets,
            target_count,
            full_lengths,
            buffer,
            &sink,
            &stream_peak,
            profile
        );
        fprintf(
            stderr,
            "benchmark sink=%u stream_peak_heap_bytes=%zu\n",
            (unsigned int)sink,
            stream_peak
        );
        free(buffer);
        return ok ? 0 : 1;
    }

    for (size_t target_index = 0u;
         target_index < target_count;
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
