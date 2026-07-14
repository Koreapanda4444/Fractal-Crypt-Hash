#ifndef FCH_LEAF_H
#define FCH_LEAF_H

#include "fractal.h"

void fch_leaf_compress(
    const uint8_t *data,
    size_t length,
    fch_state_t *out,
    int depth
);

int fch_leaf_compress_reader(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    fch_state_t *out,
    int depth
);

#endif
