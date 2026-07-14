#include "bitops.h"

uint64_t fch_rotl64(uint64_t value, unsigned int count) {
    count &= 63u;
    if (count == 0u)
        return value;
    return (value << count) | (value >> (64u - count));
}

uint64_t fch_load_le64(const uint8_t input[8]) {
    uint64_t value = 0;
    for (unsigned int i = 0; i < 8u; i++) {
        value |= (uint64_t)input[i] << (i * 8u);
    }
    return value;
}

void fch_store_le64(uint8_t output[8], uint64_t value) {
    for (unsigned int i = 0; i < 8u; i++) {
        output[i] = (uint8_t)(value >> (i * 8u));
    }
}
