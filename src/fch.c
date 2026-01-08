#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fractal.h"
#include "params.h"

static uint8_t *fch_pad(
    const uint8_t *input,
    size_t length,
    size_t *out_len
) {
    size_t min_len = length + 1 + 8;
    size_t padded_len = min_len;

    if (padded_len < FCH_MIN_BLOCK_SIZE)
        padded_len = FCH_MIN_BLOCK_SIZE;

    uint8_t *buf = (uint8_t *)calloc(padded_len, 1);

    memcpy(buf, input, length);

    buf[length] = 0x80;

    uint64_t bit_len = (uint64_t)length * 8;
    memcpy(buf + padded_len - 8, &bit_len, 8);

    *out_len = padded_len;
    return buf;
}

void fch_hash_256(
    const uint8_t *input,
    size_t length,
    uint8_t output[32]
) {
    size_t padded_len = 0;
    uint8_t *padded =
        fch_pad(input, length, &padded_len);

    fch_state_t root =
        fch_process(padded, padded_len, 0, FCH_256_STATE_WORDS);

    for (size_t i = 0; i < FCH_256_STATE_WORDS; i++) {
        memcpy(output + i * 8, &root.state[i], 8);
    }

    free(root.state);
    free(padded);
}

void fch_hash_512(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
) {
    size_t padded_len = 0;
    uint8_t *padded =
        fch_pad(input, length, &padded_len);

    fch_state_t root =
        fch_process(padded, padded_len, 0, FCH_512_STATE_WORDS);

    for (size_t i = 0; i < FCH_512_STATE_WORDS; i++) {
        memcpy(output + i * 8, &root.state[i], 8);
    }

    free(root.state);
    free(padded);
}
