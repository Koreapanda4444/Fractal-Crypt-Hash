#ifndef FCH_SBOX_H
#define FCH_SBOX_H

#include <stdint.h>

extern const uint8_t FCH_SBOX[256];
uint64_t fch_sbox64(uint64_t value);

#endif
