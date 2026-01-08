#ifndef FCH_FRACTAL_H
#define FCH_FRACTAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t *state;
    size_t words;
} fch_state_t;

fch_state_t fch_process(
    const uint8_t *data,
    size_t length,
    int depth,
    size_t state_words
);

#endif
