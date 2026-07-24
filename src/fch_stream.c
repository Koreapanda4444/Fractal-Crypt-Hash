#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitops.h"
#include "fch_stream.h"
#include "fractal.h"
#include "mix.h"
#include "params.h"

typedef struct {
    FILE *file;
    size_t input_length;
    size_t padded_length;
    size_t file_position;
} fch_stream_reader_context_t;

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

static int stream_append(
    void **storage,
    size_t *length,
    const uint8_t *data,
    size_t data_length
) {
    if (!storage || !length)
        return 0;
    if (data_length == 0)
        return 1;
    if (!data)
        return 0;

    size_t new_length = 0;
    if (!stream_length_supported(*length, data_length, &new_length))
        return 0;

    FILE *file = (FILE *)*storage;
    if (!file) {
        file = tmpfile();
        if (!file)
            return 0;
        *storage = file;
    }

    size_t written = 0;
    while (written < data_length) {
        size_t count = fwrite(data + written, 1, data_length - written, file);
        if (count == 0)
            return 0;
        written += count;
    }

    *length = new_length;
    return 1;
}

static int stream_seek(
    fch_stream_reader_context_t *context,
    size_t position
) {
    if (!context || position > context->input_length)
        return 0;
    if (!context->file)
        return position == 0 && context->input_length == 0;

    if (position < context->file_position) {
        if (fseek(context->file, 0L, SEEK_SET) != 0)
            return 0;
        context->file_position = 0;
    }

    while (context->file_position < position) {
        size_t remaining = position - context->file_position;
        long step = remaining > (size_t)LONG_MAX
            ? LONG_MAX
            : (long)remaining;
        if (fseek(context->file, step, SEEK_CUR) != 0)
            return 0;
        context->file_position += (size_t)step;
    }

    return 1;
}

static int stream_reader_read(
    void *opaque,
    size_t offset,
    uint8_t *output,
    size_t length
) {
    fch_stream_reader_context_t *context =
        (fch_stream_reader_context_t *)opaque;

    if (!context || (!output && length > 0))
        return 0;
    if (offset > context->padded_length ||
        length > context->padded_length - offset)
        return 0;

    uint64_t bit_length = (uint64_t)context->input_length * UINT64_C(8);
    size_t produced = 0;

    while (produced < length) {
        size_t position = offset + produced;

        if (position < context->input_length) {
            size_t count = context->input_length - position;
            if (count > length - produced)
                count = length - produced;
            if (!stream_seek(context, position))
                return 0;

            size_t read_count = fread(output + produced, 1, count, context->file);
            context->file_position += read_count;
            if (read_count != count)
                return 0;
            produced += count;
            continue;
        }

        if (position == context->input_length) {
            output[produced++] = 0x80u;
            continue;
        }

        size_t length_field = context->padded_length - 8u;
        if (position < length_field) {
            size_t count = length_field - position;
            if (count > length - produced)
                count = length - produced;
            memset(output + produced, 0, count);
            produced += count;
            continue;
        }

        size_t index = position - length_field;
        output[produced++] = (uint8_t)(bit_length >> (index * 8u));
    }

    return 1;
}

static void stream_close(void **storage) {
    if (!storage || !*storage)
        return;
    (void)fclose((FILE *)*storage);
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

    if (*failed) {
        *finalized = 1;
        stream_close(storage);
        memset(output, 0, output_length);
        return 0;
    }

    size_t checked_length = 0;
    if (!stream_length_supported(length, 0, &checked_length)) {
        *failed = 1;
        *finalized = 1;
        stream_close(storage);
        memset(output, 0, output_length);
        return 0;
    }

    *finalized = 1;
    FILE *file = (FILE *)*storage;

    if (file) {
        if (fflush(file) != 0 || fseek(file, 0L, SEEK_SET) != 0) {
            *failed = 1;
            stream_close(storage);
            memset(output, 0, output_length);
            return 0;
        }
        clearerr(file);
    } else if (length != 0) {
        *failed = 1;
        memset(output, 0, output_length);
        return 0;
    }

    size_t padded_length = checked_length + 9u;
    if (padded_length < FCH_MIN_BLOCK_SIZE)
        padded_length = FCH_MIN_BLOCK_SIZE;

    fch_stream_reader_context_t context = {
        file,
        length,
        padded_length,
        0
    };
    fch_reader_t reader = { stream_reader_read, &context };
    fch_state_t root = fch_process_reader(
        &reader,
        0,
        padded_length,
        0,
        state_words
    );

    int ok = root.state != NULL &&
        root.words == state_words &&
        fch_mix_finalize_output(root.state, root.words, output_words);
    if (ok) {
        for (size_t i = 0; i < output_words; i++)
            fch_store_le64(output + i * 8u, root.state[i]);
    } else {
        *failed = 1;
        memset(output, 0, output_length);
    }

    free(root.state);
    stream_close(storage);
    return ok;
}

void fch256_init(fch256_ctx *ctx) {
    if (!ctx)
        return;
    ctx->storage = NULL;
    ctx->length = 0;
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
        memset(out, 0, 32);
        return 0;
    }

    return stream_final_checked(
        &ctx->storage,
        ctx->length,
        &ctx->failed,
        &ctx->finalized,
        out,
        32,
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
    ctx->length = 0;
    ctx->failed = 0;
    ctx->finalized = 0;
}

void fch512_init(fch512_ctx *ctx) {
    if (!ctx)
        return;
    ctx->storage = NULL;
    ctx->length = 0;
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
        memset(out, 0, 64);
        return 0;
    }

    return stream_final_checked(
        &ctx->storage,
        ctx->length,
        &ctx->failed,
        &ctx->finalized,
        out,
        64,
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
    ctx->length = 0;
    ctx->failed = 0;
    ctx->finalized = 0;
}
