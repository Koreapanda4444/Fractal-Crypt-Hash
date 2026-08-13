#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "bitops.h"
#include "combine.h"
#include "debug_hooks.h"
#include "fch_stream.h"
#include "leaf.h"
#include "mix.h"
#include "params.h"

enum {
    FCH_STREAM_WORKSPACE_SLOTS = sizeof(size_t) * CHAR_BIT,
    FCH_STREAM_TAIL_BYTES = FCH_TREE_LEAF_BYTES + 9u
};

typedef struct {
    uint64_t words[FCH_INTERNAL_STATE_WORDS];
    fch_tree_position_t tree;
    int occupied;
} fch_stream_node_t;

typedef struct {
    fch_stream_node_t workspace[FCH_STREAM_WORKSPACE_SLOTS];
    uint8_t pending[FCH_TREE_LEAF_BYTES];
    size_t pending_length;
    size_t processed_length;
} fch_stream_state_t;

typedef struct {
    const uint8_t *data;
    size_t offset;
    size_t length;
} fch_stream_window_t;

static int stream_length_supported(size_t current, size_t added, size_t *result) {
    if (!result || added > SIZE_MAX - current)
        return 0;

    size_t length = current + added;
    if (length > SIZE_MAX - 9u)
        return 0;
    if ((uintmax_t)length > UINT64_MAX / UINT64_C(8))
        return 0;

    *result = length;
    return 1;
}

static int stream_window_read(
    void *opaque,
    size_t offset,
    uint8_t *output,
    size_t length
) {
    fch_stream_window_t *window = (fch_stream_window_t *)opaque;

    if (!window || (!output && length > 0u) || offset < window->offset)
        return 0;

    size_t relative = offset - window->offset;
    if (relative > window->length || length > window->length - relative)
        return 0;
    if (!window->data && length > 0u)
        return 0;

    if (length > 0u)
        memcpy(output, window->data + relative, length);
    return 1;
}

static fch_state_t stream_node_view(fch_stream_node_t *node) {
    fch_state_t view = {
        node ? node->words : NULL,
        FCH_INTERNAL_STATE_WORDS,
        node ? node->tree : (fch_tree_position_t){0, 0, 0, 0, 0}
    };
    return view;
}

static int same_position(
    const fch_tree_position_t *left,
    const fch_tree_position_t *right
) {
    return
        left && right &&
        left->level == right->level &&
        left->first_leaf == right->first_leaf &&
        left->leaf_count == right->leaf_count &&
        left->byte_offset == right->byte_offset &&
        left->byte_length == right->byte_length;
}

static int stream_combine_nodes(
    fch_stream_node_t *left,
    fch_stream_node_t *right,
    fch_stream_node_t *output
) {
    if (!left || !right || !output ||
        !left->occupied || !right->occupied ||
        left->tree.byte_length > SIZE_MAX - right->tree.byte_length)
        return 0;

    fch_state_t children[FCH_TREE_ARITY] = {
        stream_node_view(left),
        stream_node_view(right)
    };
    fch_block_t blocks[FCH_TREE_ARITY] = {
        {0u, left->tree.byte_length},
        {left->tree.byte_length, right->tree.byte_length}
    };
    fch_state_t combined = stream_node_view(output);
    size_t node_length =
        left->tree.byte_length + right->tree.byte_length;

    if (!fch_combine_into(
            children,
            blocks,
            FCH_TREE_ARITY,
            node_length,
            FCH_INTERNAL_STATE_WORDS,
            0,
            &combined
        ))
        return 0;

    output->tree = combined.tree;
    output->occupied = 1;
    FCH_DEBUG_EMIT(
        FCH_HOOK_AFTER_NODE,
        (int)output->tree.level,
        output->words,
        FCH_INTERNAL_STATE_WORDS
    );
    return 1;
}

static int stream_state_valid(
    const fch_stream_state_t *state,
    size_t input_length
) {
    if (!state || state->pending_length >= FCH_TREE_LEAF_BYTES)
        return 0;
    if (state->processed_length % FCH_TREE_LEAF_BYTES != 0u)
        return 0;
    if (state->processed_length > SIZE_MAX - state->pending_length)
        return 0;
    return
        state->processed_length + state->pending_length == input_length;
}

static fch_stream_state_t *stream_state_open(
    void **storage,
    size_t input_length
) {
    if (!storage)
        return NULL;

    fch_stream_state_t *state = (fch_stream_state_t *)*storage;
    if (state)
        return stream_state_valid(state, input_length) ? state : NULL;
    if (input_length != 0u)
        return NULL;

    state = (fch_stream_state_t *)calloc(1u, sizeof(*state));
    if (!state)
        return NULL;
    *storage = state;
    return state;
}

static int stream_push_leaf(
    fch_stream_state_t *state,
    const uint8_t *data,
    size_t length
) {
    if (!state || !data || length == 0u ||
        length > FCH_TREE_LEAF_BYTES ||
        state->processed_length % FCH_TREE_LEAF_BYTES != 0u ||
        state->processed_length > SIZE_MAX - length)
        return 0;

    fch_stream_window_t window = {
        data,
        state->processed_length,
        length
    };
    fch_reader_t reader = {stream_window_read, &window};
    fch_stream_node_t carry = {
        {0},
        {0, 0, 0, 0, 0},
        1
    };
    fch_state_t leaf = stream_node_view(&carry);

    if (!fch_leaf_compress_reader(
            &reader,
            state->processed_length,
            length,
            &leaf,
            0
        ))
        return 0;
    carry.tree = leaf.tree;

    FCH_DEBUG_EMIT(
        FCH_HOOK_AFTER_LEAF,
        (int)carry.tree.level,
        carry.words,
        FCH_INTERNAL_STATE_WORDS
    );

    size_t level = 0u;
    while (level < FCH_STREAM_WORKSPACE_SLOTS &&
           state->workspace[level].occupied) {
        fch_stream_node_t combined = {
            {0},
            {0, 0, 0, 0, 0},
            0
        };
        if (!stream_combine_nodes(
                &state->workspace[level],
                &carry,
                &combined
            ))
            return 0;
        state->workspace[level].occupied = 0;
        carry = combined;
        level++;
    }
    if (level >= FCH_STREAM_WORKSPACE_SLOTS)
        return 0;

    state->workspace[level] = carry;
    state->processed_length += length;
    return 1;
}

static int stream_fold_root(
    fch_stream_state_t *state,
    const fch_tree_position_t *expected,
    fch_state_t *output
) {
    if (!state || !expected || !output || !output->state ||
        output->words != FCH_INTERNAL_STATE_WORDS)
        return 0;

    fch_stream_node_t root = {
        {0},
        {0, 0, 0, 0, 0},
        0
    };
    for (size_t level = 0u;
         level < FCH_STREAM_WORKSPACE_SLOTS;
         level++) {
        if (!state->workspace[level].occupied)
            continue;
        if (!root.occupied) {
            root = state->workspace[level];
            continue;
        }

        fch_stream_node_t combined = {
            {0},
            {0, 0, 0, 0, 0},
            0
        };
        if (!stream_combine_nodes(
                &state->workspace[level],
                &root,
                &combined
            ))
            return 0;
        root = combined;
    }

    if (!root.occupied || !same_position(&root.tree, expected))
        return 0;

    memcpy(
        output->state,
        root.words,
        output->words * sizeof(*output->state)
    );
    output->tree = root.tree;

    FCH_DEBUG_EMIT(
        FCH_HOOK_AFTER_ROOT,
        (int)output->tree.level,
        output->state,
        output->words
    );
    return 1;
}

static int stream_append(
    void **storage,
    size_t *length,
    const uint8_t *data,
    size_t data_length
) {
    if (!storage || !length)
        return 0;
    if (data_length == 0u) {
        if (!*storage)
            return *length == 0u;
        return stream_state_valid(
            (const fch_stream_state_t *)*storage,
            *length
        );
    }
    if (!data)
        return 0;

    size_t new_length = 0u;
    if (!stream_length_supported(*length, data_length, &new_length))
        return 0;

    fch_stream_state_t *state = stream_state_open(storage, *length);
    if (!state)
        return 0;

    size_t consumed = 0u;
    while (consumed < data_length) {
        size_t count = FCH_TREE_LEAF_BYTES - state->pending_length;
        if (count > data_length - consumed)
            count = data_length - consumed;

        memcpy(
            state->pending + state->pending_length,
            data + consumed,
            count
        );
        state->pending_length += count;
        consumed += count;

        if (state->pending_length == FCH_TREE_LEAF_BYTES) {
            if (!stream_push_leaf(
                    state,
                    state->pending,
                    FCH_TREE_LEAF_BYTES
                ))
                return 0;
            state->pending_length = 0u;
        }
    }

    *length = new_length;
    return stream_state_valid(state, *length);
}

static void stream_close(void **storage) {
    if (!storage)
        return;
    free(*storage);
    *storage = NULL;
}

static int stream_final_checked(
    void **storage,
    size_t length,
    int *failed,
    int *finalized,
    uint8_t *output,
    size_t output_length,
    size_t state_words,
    size_t output_words
) {
    if (!storage || !failed || !finalized || !output)
        return 0;

    if (*finalized) {
        memset(output, 0, output_length);
        return 0;
    }

    *finalized = 1;
    if (*failed) {
        stream_close(storage);
        memset(output, 0, output_length);
        return 0;
    }

    size_t checked_length = 0u;
    fch_stream_state_t *state = NULL;
    if (!stream_length_supported(length, 0u, &checked_length) ||
        !(state = stream_state_open(storage, length))) {
        *failed = 1;
        stream_close(storage);
        memset(output, 0, output_length);
        return 0;
    }

    size_t padded_length = checked_length + 9u;
    if (padded_length < FCH_PADDING_MIN_BYTES)
        padded_length = FCH_PADDING_MIN_BYTES;

    if (state->processed_length > padded_length) {
        *failed = 1;
        stream_close(storage);
        memset(output, 0, output_length);
        return 0;
    }

    size_t tail_length = padded_length - state->processed_length;
    if (tail_length > FCH_STREAM_TAIL_BYTES ||
        state->pending_length > tail_length ||
        tail_length - state->pending_length < 9u) {
        *failed = 1;
        stream_close(storage);
        memset(output, 0, output_length);
        return 0;
    }

    uint8_t tail[FCH_STREAM_TAIL_BYTES] = {0};
    if (state->pending_length > 0u) {
        memcpy(
            tail,
            state->pending,
            state->pending_length
        );
    }
    tail[state->pending_length] = 0x80u;
    fch_store_le64(
        tail + tail_length - 8u,
        (uint64_t)checked_length * UINT64_C(8)
    );

    size_t consumed = 0u;
    int ok = 1;
    while (ok && consumed < tail_length) {
        size_t count = tail_length - consumed;
        if (count > FCH_TREE_LEAF_BYTES)
            count = FCH_TREE_LEAF_BYTES;
        ok = stream_push_leaf(state, tail + consumed, count);
        consumed += count;
    }

    fch_tree_position_t expected;
    uint64_t root_words[FCH_INTERNAL_STATE_WORDS];
    fch_state_t root = {
        root_words,
        state_words,
        {0, 0, 0, 0, 0}
    };
    if (ok)
        ok = state->processed_length == padded_length &&
            fch_tree_position_for_range(0u, padded_length, &expected) &&
            stream_fold_root(state, &expected, &root);
    if (ok)
        ok = fch_mix_finalize_output(
            root.state,
            root.words,
            output_words,
            checked_length,
            padded_length,
            root.tree.level,
            root.tree.first_leaf,
            root.tree.leaf_count,
            root.tree.byte_offset,
            root.tree.byte_length
        );
    if (ok) {
        for (size_t i = 0u; i < output_words; i++)
            fch_store_le64(output + i * 8u, root.state[i]);
    } else {
        *failed = 1;
        memset(output, 0, output_length);
    }

    stream_close(storage);
    return ok;
}

void fch256_init(fch256_ctx *ctx) {
    if (!ctx)
        return;
    ctx->storage = NULL;
    ctx->length = 0u;
    ctx->failed = 0;
    ctx->finalized = 0;
}

int fch256_update(fch256_ctx *ctx, const uint8_t *data, size_t len) {
    if (!ctx || ctx->failed || ctx->finalized)
        return 0;

    if (!stream_append(&ctx->storage, &ctx->length, data, len)) {
        ctx->failed = 1;
        return 0;
    }
    return 1;
}

int fch256_final_checked(fch256_ctx *ctx, uint8_t out[32]) {
    if (!out)
        return 0;
    if (!ctx) {
        memset(out, 0, 32u);
        return 0;
    }

    return stream_final_checked(
        &ctx->storage,
        ctx->length,
        &ctx->failed,
        &ctx->finalized,
        out,
        32u,
        FCH_256_STATE_WORDS,
        FCH_256_OUTPUT_WORDS
    );
}

void fch256_final(fch256_ctx *ctx, uint8_t out[32]) {
    (void)fch256_final_checked(ctx, out);
}

void fch256_free(fch256_ctx *ctx) {
    if (!ctx)
        return;
    stream_close(&ctx->storage);
    ctx->length = 0u;
    ctx->failed = 0;
    ctx->finalized = 0;
}

void fch512_init(fch512_ctx *ctx) {
    if (!ctx)
        return;
    ctx->storage = NULL;
    ctx->length = 0u;
    ctx->failed = 0;
    ctx->finalized = 0;
}

int fch512_update(fch512_ctx *ctx, const uint8_t *data, size_t len) {
    if (!ctx || ctx->failed || ctx->finalized)
        return 0;

    if (!stream_append(&ctx->storage, &ctx->length, data, len)) {
        ctx->failed = 1;
        return 0;
    }
    return 1;
}

int fch512_final_checked(fch512_ctx *ctx, uint8_t out[64]) {
    if (!out)
        return 0;
    if (!ctx) {
        memset(out, 0, 64u);
        return 0;
    }

    return stream_final_checked(
        &ctx->storage,
        ctx->length,
        &ctx->failed,
        &ctx->finalized,
        out,
        64u,
        FCH_512_STATE_WORDS,
        FCH_512_OUTPUT_WORDS
    );
}

void fch512_final(fch512_ctx *ctx, uint8_t out[64]) {
    (void)fch512_final_checked(ctx, out);
}

void fch512_free(fch512_ctx *ctx) {
    if (!ctx)
        return;
    stream_close(&ctx->storage);
    ctx->length = 0u;
    ctx->failed = 0;
    ctx->finalized = 0;
}
