#ifndef FCH_COMBINE_H
#define FCH_COMBINE_H

#include "fractal.h"

fch_state_t fch_combine(
	fch_state_t *children,
	const fch_block_t *blocks,
	size_t count,
	size_t node_length,
	size_t state_words,
	int depth
);

#endif
