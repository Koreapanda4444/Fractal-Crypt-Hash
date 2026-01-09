#ifndef FCH_TEST_UTILS_H
#define FCH_TEST_UTILS_H

#include <stdint.h>
#include <stddef.h>

static inline int popcount8(uint8_t v) {
    int c = 0;
    for (int i = 0; i < 8; i++)
        c += (v >> i) & 1;
    return c;
}

static inline int bit_diff(
    const uint8_t *a,
    const uint8_t *b,
    size_t len
) {
    int diff = 0;
    for (size_t i = 0; i < len; i++)
        diff += popcount8(a[i] ^ b[i]);
    return diff;
}

#endif
