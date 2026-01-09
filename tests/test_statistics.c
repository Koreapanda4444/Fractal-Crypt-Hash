#include <stdio.h>
#include <string.h>

#include "fch.h"
#include "test_utils.h"

#define TEST_ROUNDS 100
#define INPUT_SIZE 64

int main(void) {
    uint8_t input[INPUT_SIZE];
    uint8_t mod[INPUT_SIZE];

    memset(input, 0x41, INPUT_SIZE);

    double avg256 = 0.0;
    double avg512 = 0.0;

    for (int r = 0; r < TEST_ROUNDS; r++) {
        memcpy(mod, input, INPUT_SIZE);

        mod[r % INPUT_SIZE] ^= (1 << (r % 8));

        uint8_t out1_256[32], out2_256[32];
        uint8_t out1_512[64], out2_512[64];

        fch_hash_256(input, INPUT_SIZE, out1_256);
        fch_hash_256(mod,   INPUT_SIZE, out2_256);

        fch_hash_512(input, INPUT_SIZE, out1_512);
        fch_hash_512(mod,   INPUT_SIZE, out2_512);

        avg256 += bit_diff(out1_256, out2_256, 32) / 256.0;
        avg512 += bit_diff(out1_512, out2_512, 64) / 512.0;
    }

    avg256 /= TEST_ROUNDS;
    avg512 /= TEST_ROUNDS;

    printf("FCH-256 average avalanche: %.2f%%\n", avg256 * 100);
    printf("FCH-512 average avalanche: %.2f%%\n", avg512 * 100);

    return 0;
}
