#ifndef FCH_FRACTAL_H
#define FCH_FRACTAL_H

#include <stdint.h>
#include <stddef.h>

#include "params.h"

typedef struct {
    size_t level;
    size_t first_leaf;
    size_t leaf_count;
    size_t byte_offset;
    size_t byte_length;
} fch_tree_position_t;

typedef struct {
    uint64_t *state;
    size_t words;
    fch_tree_position_t tree;
} fch_state_t;

typedef struct {
    size_t offset;
    size_t length;
} fch_block_t;

typedef int (*fch_read_callback_t)(
    void *context,
    size_t offset,
    uint8_t *output,
    size_t length
);

typedef struct {
    fch_read_callback_t read;
    void *context;
} fch_reader_t;

typedef struct {
    const uint8_t *data;
    size_t length;
} fch_memory_reader_t;

size_t fch_tree_leaf_count_for_length(size_t length);
size_t fch_tree_level_for_leaves(size_t leaf_count);

int fch_tree_position_for_range(
    size_t offset,
    size_t length,
    fch_tree_position_t *position
);

int fch_tree_position_valid(const fch_tree_position_t *position);

int fch_tree_split_position(
    const fch_tree_position_t *parent,
    fch_tree_position_t children[FCH_TREE_ARITY]
);

static inline int fch_memory_read(
    void *context,
    size_t offset,
    uint8_t *output,
    size_t length
) {
    fch_memory_reader_t *memory = (fch_memory_reader_t *)context;

    if (!memory || (!output && length > 0))
        return 0;
    if (offset > memory->length || length > memory->length - offset)
        return 0;
    if (!memory->data && length > 0)
        return 0;

    for (size_t i = 0; i < length; i++)
        output[i] = memory->data[offset + i];

    return 1;
}

fch_state_t fch_process(
    const uint8_t *data,
    size_t length,
    int depth,
    size_t state_words
);

fch_state_t fch_process_reader(
    const fch_reader_t *reader,
    size_t offset,
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

size_t fch_fractal_split_reader(
    const fch_reader_t *reader,
    size_t offset,
    size_t length,
    int depth,
    fch_block_t *blocks,
    size_t max_blocks
);

#endif
