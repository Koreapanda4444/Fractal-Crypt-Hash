#include "combine.h"

fch_state_t fch_combine(
    fch_state_t *children,
    size_t count,
    size_t state_words
) {
    fch_state_t out;
    out.state = NULL;
    out.words = state_words;

    (void)children;
    (void)count;

    return out;
}
