#ifndef FCH_H
#define FCH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void fch_hash_256(
	const uint8_t *input,
	size_t length,
	uint8_t output[32]
);

void fch_hash_512(
	const uint8_t *input,
	size_t length,
	uint8_t output[64]
);

#ifdef __cplusplus
}
#endif

#endif
