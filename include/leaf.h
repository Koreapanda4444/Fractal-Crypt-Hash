#ifndef FCH_LEAF_H
#define FCH_LEAF_H

#include "fractal.h"

void fch_leaf_compress(
	const uint8_t *data,
	size_t length,
	fch_state_t *out,
	int depth
);

#endif
