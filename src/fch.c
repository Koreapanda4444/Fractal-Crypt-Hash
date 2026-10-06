#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fractal.h"
#include "params.h"
#include "bitops.h"
#include "mix.h"

typedef struct {
    const uint8_t *input;
    size_t length;
    size_t padded_length;
    uint8_t bit_length[8];
} fch_padded_reader_t;

static int fch_padded_read(
    void *context,
    size_t offset,
    uint8_t *output,
    size_t length
) {
    const fch_padded_reader_t *padding = (const fch_padded_reader_t *)context;
    if (!padding || (!output && length > 0u) ||
        offset > padding->padded_length ||
        length > padding->padded_length - offset)
        return 0;
    if (length == 0u)
        return 1;

    size_t copied = 0u;
    if (offset < padding->length) {
        copied = padding->length - offset;
        if (copied > length)
            copied = length;
        memcpy(output, padding->input + offset, copied);
        if (copied == length)
            return 1;
    }
    memset(output + copied, 0, length - copied);
    if (offset <= padding->length && padding->length - offset < length)
        output[padding->length - offset] = 0x80u;

    size_t footer = padding->padded_length - 8u;
    for (size_t i = 0u; i < 8u; i++) {
        size_t position = footer + i;
        if (position >= offset && position - offset < length)
            output[position - offset] = padding->bit_length[i];
    }
    return 1;
}

static int fch_hash_checked(
    const uint8_t *input,
    size_t length,
    uint8_t *output,
    size_t state_words,
    size_t output_words
) {
    if (!output)
        return 0;
    size_t output_length = output_words * sizeof(uint64_t);
    if ((!input && length > 0u) || length > SIZE_MAX - 9u) {
        memset(output, 0, output_length);
        return 0;
    }
#if SIZE_MAX > UINT64_MAX / 8u
    if (length > UINT64_MAX / 8u) {
        memset(output, 0, output_length);
        return 0;
    }
#endif

    size_t padded_length = length + 9u;
    if (padded_length < FCH_PADDING_MIN_BYTES)
        padded_length = FCH_PADDING_MIN_BYTES;
    fch_padded_reader_t padding = {input, length, padded_length, {0}};
    fch_store_le64(padding.bit_length, (uint64_t)length * UINT64_C(8));
    fch_reader_t reader = {fch_padded_read, &padding};
    fch_state_t root = fch_process_reader(&reader, 0u, padded_length, state_words);

    int ok = root.state && root.words == state_words &&
        fch_mix_finalize_output(
            root.state, root.words, output_words, length, padded_length,
            root.tree.level, root.tree.first_leaf, root.tree.leaf_count,
            root.tree.byte_offset, root.tree.byte_length
        );
    if (ok) {
        for (size_t i = 0u; i < output_words; i++)
            fch_store_le64(output + i * 8u, root.state[i]);
    } else {
        memset(output, 0, output_length);
    }
    free(root.state);
    return ok;
}

int fch_hash_256_checked(
    const uint8_t *input,
    size_t length,
    uint8_t output[32]
) {
    return fch_hash_checked(input, length, output, FCH_256_STATE_WORDS,
                            FCH_256_OUTPUT_WORDS);
}

int fch_hash_512_checked(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
) {
    return fch_hash_checked(input, length, output, FCH_512_STATE_WORDS,
                            FCH_512_OUTPUT_WORDS);
}

void fch_hash_256(
    const uint8_t *input,
    size_t length,
    uint8_t output[32]
) {
    (void)fch_hash_256_checked(input, length, output);
}

void fch_hash_512(
    const uint8_t *input,
    size_t length,
    uint8_t output[64]
) {
    (void)fch_hash_512_checked(input, length, output);
}
