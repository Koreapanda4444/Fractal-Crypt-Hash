#include <stdio.h>
#include <string.h>

#include "fch.h"
#include "test_utils.h"

#define MAX_INPUT 4096
#define ROUNDS 128

static void test_length(size_t len) {
    uint8_t base[MAX_INPUT];
    uint8_t mod[MAX_INPUT];
    memset(base, 0xA5, len);

    double avg256 = 0.0, min256 = 1.0, max256 = 0.0;
    double avg512 = 0.0, min512 = 1.0, max512 = 0.0;

    for (int r = 0; r < ROUNDS; r++) {
        memcpy(mod, base, len);
        if (len > 0)
            mod[r % len] ^= (1 << (r % 8));

        uint8_t a256[32], b256[32];
        uint8_t a512[64], b512[64];

        fch_hash_256(base, len, a256);
        fch_hash_256(mod,  len, b256);
        fch_hash_512(base, len, a512);
        fch_hash_512(mod,  len, b512);

        double d256 = bit_diff(a256, b256, 32) / 256.0;
        double d512 = bit_diff(a512, b512, 64) / 512.0;

        avg256 += d256;
        avg512 += d512;

        if (d256 < min256) min256 = d256;
        if (d256 > max256) max256 = d256;
        if (d512 < min512) min512 = d512;
        if (d512 > max512) max512 = d512;
    }

    avg256 /= ROUNDS;
    avg512 /= ROUNDS;

    printf(
        "[len=%4I64u] FCH-256 avg %.2f%% (min %.2f / max %.2f) | "
        "FCH-512 avg %.2f%% (min %.2f / max %.2f)\n",
        (unsigned long long)len,
        avg256 * 100, min256 * 100, max256 * 100,
        avg512 * 100, min512 * 100, max512 * 100
    );
}

int main(void) {
    size_t lengths[] = {
        0, 1, 8, 32,
        64, 128, 512,
        1024, 4096
    };

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        test_length(lengths[i]);
    }

    return 0;
}
