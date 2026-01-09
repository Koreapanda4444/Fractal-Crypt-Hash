#ifndef FCH_TEST_UTILS_H
#define FCH_TEST_UTILS_H

#include <stdint.h>
#include <stddef.h>

static inline int bit_diff(
    const uint8_t *a,
    const uint8_t *b,
    size_t len
) {
    int diff = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t v = a[i] ^ b[i];
        for (int j = 0; j < 8; j++) {
            diff += (v >> j) & 1;
        }
    }
    return diff;
}

#endif
