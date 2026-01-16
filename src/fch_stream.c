#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fch_stream.h"

static int ensure_capacity(uint8_t **buffer, size_t *capacity, size_t needed) {
	if (needed <= *capacity) return 1;

	size_t new_cap = (*capacity == 0) ? 64 : *capacity;
	while (new_cap < needed) {
		if (new_cap > (SIZE_MAX / 2)) {
			new_cap = needed;
			break;
		}
		new_cap *= 2;
	}

	uint8_t *new_buf = (uint8_t *)realloc(*buffer, new_cap);
	if (!new_buf) return 0;

	*buffer = new_buf;
	*capacity = new_cap;
	return 1;
}

static int append_bytes(uint8_t **buffer, size_t *length, size_t *capacity, const uint8_t *data, size_t len) {
	if (len == 0) return 1;
	if (!data) return 0;

	size_t new_len = *length + len;
	if (new_len < *length) return 0;

	if (!ensure_capacity(buffer, capacity, new_len)) return 0;

	memcpy((*buffer) + (*length), data, len);
	*length = new_len;
	return 1;
}

void fch256_init(fch256_ctx *ctx) {
	if (!ctx) return;
	ctx->buffer = NULL;
	ctx->length = 0;
	ctx->capacity = 0;
	ctx->failed = 0;
}

int fch256_update(fch256_ctx *ctx, const uint8_t *data, size_t len) {
	if (!ctx) return 0;
	if (ctx->failed) return 0;

	if (!append_bytes(&ctx->buffer, &ctx->length, &ctx->capacity, data, len)) {
		ctx->failed = 1;
		return 0;
	}
	return 1;
}

void fch256_final(fch256_ctx *ctx, uint8_t out[32]) {
	if (!out) return;
	if (!ctx || ctx->failed) {
		memset(out, 0, 32);
		return;
	}

	fch_hash_256(ctx->buffer, ctx->length, out);
}

void fch256_free(fch256_ctx *ctx) {
	if (!ctx) return;
	free(ctx->buffer);
	ctx->buffer = NULL;
	ctx->length = 0;
	ctx->capacity = 0;
	ctx->failed = 0;
}

void fch512_init(fch512_ctx *ctx) {
	if (!ctx) return;
	ctx->buffer = NULL;
	ctx->length = 0;
	ctx->capacity = 0;
	ctx->failed = 0;
}

int fch512_update(fch512_ctx *ctx, const uint8_t *data, size_t len) {
	if (!ctx) return 0;
	if (ctx->failed) return 0;

	if (!append_bytes(&ctx->buffer, &ctx->length, &ctx->capacity, data, len)) {
		ctx->failed = 1;
		return 0;
	}
	return 1;
}

void fch512_final(fch512_ctx *ctx, uint8_t out[64]) {
	if (!out) return;
	if (!ctx || ctx->failed) {
		memset(out, 0, 64);
		return;
	}

	fch_hash_512(ctx->buffer, ctx->length, out);
}

void fch512_free(fch512_ctx *ctx) {
	if (!ctx) return;
	free(ctx->buffer);
	ctx->buffer = NULL;
	ctx->length = 0;
	ctx->capacity = 0;
	ctx->failed = 0;
}
