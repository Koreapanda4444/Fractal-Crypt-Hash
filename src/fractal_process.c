#include "fractal.h"
#include "leaf.h"
#include "combine.h"
#include "params.h"

fch_state_t fch_process(
    const uint8_t *data,
    size_t length,
    int depth,
    size_t state_words
) {
    fch_state_t result;
    result.state = NULL;
    result.words = state_words;

    (void)data;
    (void)length;
    (void)depth;

    return result;
}
