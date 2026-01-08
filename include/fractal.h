#ifndef FCH_FRACTAL_H
#define FCH_FRACTAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t *state;
    size_t words;
} fch_state_t;

typedef struct {
    size_t offset;
    size_t length;
} fch_block_t;

fch_state_t fch_process(
    const uint8_t *data,
    size_t length,
    int depth,
    size_t state_words
);

size_t fch_fractal_split(
    const uint8_t *data,
    size_t length,
    int depth,
    fch_block_t *blocks,
    size_t max_blocks
);

#endif
