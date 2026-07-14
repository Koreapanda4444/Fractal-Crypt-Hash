#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fractal.h"
#include "params.h"
#include "bitops.h"

static uint8_t *fch_pad(
    const uint8_t *input,
    size_t length,
    size_t *out_len
) {
    if (!out_len) return NULL;
    *out_len = 0;

    if (length > SIZE_MAX - 9u)
        return NULL;
    if (length > UINT64_MAX / 8u)
        return NULL;
    if (length > 0 && !input)
        return NULL;

    size_t min_len = length + 1 + 8;
    size_t padded_len = min_len;

    if (padded_len < FCH_MIN_BLOCK_SIZE)
        padded_len = FCH_MIN_BLOCK_SIZE;

    uint8_t *buf = (uint8_t *)calloc(padded_len, 1);

    if (!buf) {
        *out_len = 0;
        return NULL;
    }

    if (length > 0) {
        memcpy(buf, input, length);
    }

    buf[length] = 0x80;

    uint64_t bit_len = (uint64_t)length * 8;
    fch_store_le64(buf + padded_len - 8, bit_len);

    *out_len = padded_len;
    return buf;
}

int fch_hash_256_checked(
    const uint8_t *input,
    size_t length,
    uint8_t output[32]
) {
    if (!output) return 0;

    size_t padded_len = 0;
    uint8_t *padded =
        fch_pad(input, length, &padded_len);

    if (!padded) {
        memset(output, 0, 32);
        return 0;
    }

    fch_state_t root =
        fch_process(padded, padded_len, 0, FCH_256_STATE_WORDS);

    if (!root.state) {
        memset(output, 0, 32);
        free(padded);
        return 0;
    }

    for (size_t i = 0; i < FCH_256_STATE_WORDS; i++) {
        fch_store_le64(output + i * 8, root.state[i]);
    }

    free(root.state);
    free(padded);
    return 1;
}

int fch_hash_512_checked(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
) {
    if (!output) return 0;

    size_t padded_len = 0;
    uint8_t *padded =
        fch_pad(input, length, &padded_len);

    if (!padded) {
        memset(output, 0, 64);
        return 0;
    }

    fch_state_t root =
        fch_process(padded, padded_len, 0, FCH_512_STATE_WORDS);

    if (!root.state) {
        memset(output, 0, 64);
        free(padded);
        return 0;
    }

    for (size_t i = 0; i < FCH_512_STATE_WORDS; i++) {
        fch_store_le64(output + i * 8, root.state[i]);
    }

    free(root.state);
    free(padded);
    return 1;
}

void fch_hash_256(
    const uint8_t *input,
    size_t length,
    uint8_t output[32]
) {
    (void)fch_hash_256_checked(input, length, output);
}

void fch_hash_512(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
) {
    (void)fch_hash_512_checked(input, length, output);
}
