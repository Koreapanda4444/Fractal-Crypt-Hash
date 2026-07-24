#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../include/fch.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif



static uint64_t now_ns(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    static int init = 0;
    LARGE_INTEGER t;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    QueryPerformanceCounter(&t);
    return (uint64_t)((t.QuadPart * 1000000000ULL) / (uint64_t)freq.QuadPart);
#else
    
    return (uint64_t)clock() * (1000000000ULL / (uint64_t)CLOCKS_PER_SEC);
#endif
}

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void fill_random(uint8_t *buf, size_t len, uint32_t seed) {
    uint32_t s = seed ? seed : 0xC001D00Du;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(xorshift32(&s) & 0xFFu);
    }
}



typedef void (*hash256_fn)(const uint8_t *input, size_t len, uint8_t out[32]);


static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static const uint32_t K256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static void sha256_compress(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = load_be32(block + (size_t)i * 4);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void sha256_ref(const uint8_t *input, size_t len, uint8_t out[32]) {
    uint32_t st[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    size_t full = len & ~(size_t)63;
    for (size_t i = 0; i < full; i += 64) {
        sha256_compress(st, input + i);
    }

    uint8_t block[128];
    size_t rem = len - full;
    memcpy(block, input + full, rem);
    block[rem++] = 0x80;

    size_t pad_len = (rem <= 56) ? (56 - rem) : (120 - rem);
    memset(block + rem, 0, pad_len);
    rem += pad_len;

    uint64_t bitlen = (uint64_t)len * 8ULL;
    block[rem + 0] = (uint8_t)(bitlen >> 56);
    block[rem + 1] = (uint8_t)(bitlen >> 48);
    block[rem + 2] = (uint8_t)(bitlen >> 40);
    block[rem + 3] = (uint8_t)(bitlen >> 32);
    block[rem + 4] = (uint8_t)(bitlen >> 24);
    block[rem + 5] = (uint8_t)(bitlen >> 16);
    block[rem + 6] = (uint8_t)(bitlen >> 8);
    block[rem + 7] = (uint8_t)(bitlen);
    rem += 8;

    sha256_compress(st, block);
    if (rem == 128) {
        sha256_compress(st, block + 64);
    }

    for (int i = 0; i < 8; i++) {
        store_be32(out + (size_t)i * 4, st[i]);
    }
}




#define FCH_PARAMS_ALLOW_REINCLUDE 1

#include "../src/sbox.c"
#include "../src/bitops.c"


#undef FCH_MAX_DEPTH_CAP
#define FCH_MAX_DEPTH_CAP 4
#define fch_hash_256 fch_hash_256_D4
#define fch_hash_512 fch_hash_512_D4
#define fch_hash_256_checked fch_hash_256_checked_D4
#define fch_hash_512_checked fch_hash_512_checked_D4
#define fch_pad fch_pad_D4
#define fch_process fch_process_D4
#define fch_process_reader fch_process_reader_D4
#define fch_fractal_split fch_fractal_split_D4
#define fch_fractal_split_reader fch_fractal_split_reader_D4
#define determine_n determine_n_D4
#define scaled_length scaled_length_D4
#define fch_leaf_compress fch_leaf_compress_D4
#define fch_leaf_compress_reader fch_leaf_compress_reader_D4
#define fch_combine fch_combine_D4
#define fch_debug_emit_root_if fch_debug_emit_root_if_D4

#define rotl64 rotl64_leaf_D4
#include "../src/leaf.c"
#undef rotl64

#define rotl64 rotl64_combine_D4
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
#undef fch_process_reader
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef determine_n
#undef scaled_length
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_debug_emit_root_if


#undef FCH_MAX_DEPTH_CAP
#define FCH_MAX_DEPTH_CAP 8
#define fch_hash_256 fch_hash_256_D8
#define fch_hash_512 fch_hash_512_D8
#define fch_hash_256_checked fch_hash_256_checked_D8
#define fch_hash_512_checked fch_hash_512_checked_D8
#define fch_pad fch_pad_D8
#define fch_process fch_process_D8
#define fch_process_reader fch_process_reader_D8
#define fch_fractal_split fch_fractal_split_D8
#define fch_fractal_split_reader fch_fractal_split_reader_D8
#define determine_n determine_n_D8
#define scaled_length scaled_length_D8
#define fch_leaf_compress fch_leaf_compress_D8
#define fch_leaf_compress_reader fch_leaf_compress_reader_D8
#define fch_combine fch_combine_D8
#define fch_debug_emit_root_if fch_debug_emit_root_if_D8

#define rotl64 rotl64_leaf_D8
#include "../src/leaf.c"
#undef rotl64

#define rotl64 rotl64_combine_D8
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
#undef fch_process_reader
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef determine_n
#undef scaled_length
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_debug_emit_root_if


#undef FCH_MAX_DEPTH_CAP
#define FCH_MAX_DEPTH_CAP 16
#define fch_hash_256 fch_hash_256_D16
#define fch_hash_512 fch_hash_512_D16
#define fch_hash_256_checked fch_hash_256_checked_D16
#define fch_hash_512_checked fch_hash_512_checked_D16
#define fch_pad fch_pad_D16
#define fch_process fch_process_D16
#define fch_process_reader fch_process_reader_D16
#define fch_fractal_split fch_fractal_split_D16
#define fch_fractal_split_reader fch_fractal_split_reader_D16
#define determine_n determine_n_D16
#define scaled_length scaled_length_D16
#define fch_leaf_compress fch_leaf_compress_D16
#define fch_leaf_compress_reader fch_leaf_compress_reader_D16
#define fch_combine fch_combine_D16
#define fch_debug_emit_root_if fch_debug_emit_root_if_D16

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
#undef fch_process_reader
#undef fch_fractal_split
#undef fch_fractal_split_reader
#undef determine_n
#undef scaled_length
#undef fch_leaf_compress
#undef fch_leaf_compress_reader
#undef fch_combine
#undef fch_debug_emit_root_if

#undef FCH_PARAMS_ALLOW_REINCLUDE



typedef struct {
    const char *name;
    hash256_fn fn;
} bench_target_t;

static double bench_ms_per_mb(hash256_fn fn, const uint8_t *buf, size_t len, int iters) {
    uint8_t out[32];

    uint64_t t0 = now_ns();
    for (int i = 0; i < iters; i++) {
        fn(buf, len, out);
    }
    uint64_t t1 = now_ns();

    double seconds = (double)(t1 - t0) / 1e9;
    double mb = ((double)len * (double)iters) / 1000000.0;
    if (mb <= 0.0 || seconds <= 0.0)
        return 0.0;
    return (seconds * 1000.0) / mb;
}

static int iters_for_len(size_t len) {
    if (len >= 1024 * 1024) return 64;
    if (len >= 256 * 1024)  return 128;
    if (len >= 64 * 1024)   return 256;
    if (len >= 16 * 1024)   return 512;
    return 1024;
}

int main(void) {
    size_t sizes[] = {
        32, 64, 128, 256, 512, 1024,
        4096, 16384, 65536, 262144, 1048576
    };

    bench_target_t targets[] = {
        {"fch256_D4",  fch_hash_256_D4},
        {"fch256_D8",  fch_hash_256_D8},
        {"fch256_D16", fch_hash_256_D16},
    };

    
    hash256_fn sha_fn = sha256_ref;

    uint8_t *buf = (uint8_t *)malloc(1024 * 1024);
    if (!buf) {
        fprintf(stderr, "ERR: alloc failed\n");
        return 1;
    }
    fill_random(buf, 1024 * 1024, 0x12345678u);

    printf("algo,len,iter,ms_per_mb,mb_per_s,rel_vs_sha\n");

    for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        size_t len = sizes[si];
        int iters = iters_for_len(len);

        double sha_msmb = bench_ms_per_mb(sha_fn, buf, len, iters);

        for (size_t ti = 0; ti < sizeof(targets) / sizeof(targets[0]); ti++) {
            double msmb = bench_ms_per_mb(targets[ti].fn, buf, len, iters);
            double mbs = (msmb > 0.0) ? (1000.0 / msmb) : 0.0;

            double rel = 0.0;
            if (sha_msmb > 0.0 && msmb > 0.0) {
                
                rel = msmb / sha_msmb;
            }

            printf(
                "%s,%u,%d,%.3f,%.2f,%.3f\n",
                targets[ti].name,
                (unsigned)len,
                iters,
                msmb,
                mbs,
                rel
            );
        }

        if (sha_msmb > 0.0) {
            double sha_mbs = 1000.0 / sha_msmb;
            printf(
                "%s,%u,%d,%.3f,%.2f,%.3f\n",
                "sha256_ref",
                (unsigned)len,
                iters,
                sha_msmb,
                sha_mbs,
                1.0
            );
        }
    }

    free(buf);
    return 0;
}