#ifndef FCH_BITOPS_H
#define FCH_BITOPS_H

#include <stdint.h>

uint64_t fch_rotl64(uint64_t value, unsigned int count);
uint64_t fch_load_le64(const uint8_t input[8]);
void fch_store_le64(uint8_t output[8], uint64_t value);

#endif
