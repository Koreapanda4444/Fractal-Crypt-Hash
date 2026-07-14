#ifndef FCH_STREAM_H
#define FCH_STREAM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NOTE: This is a buffered streaming API: data is accumulated in memory and
 * hashed on finalization using the one-shot FCH functions.
 * Memory usage is O(n) in total input size.
 */

typedef struct {
	uint8_t *buffer;
	size_t length;
	size_t capacity;
	int failed;
} fch256_ctx;

typedef struct {
	uint8_t *buffer;
	size_t length;
	size_t capacity;
	int failed;
} fch512_ctx;

void fch256_init(fch256_ctx *ctx);
int  fch256_update(fch256_ctx *ctx, const uint8_t *data, size_t len);
int  fch256_final_checked(fch256_ctx *ctx, uint8_t out[32]);
void fch256_final(fch256_ctx *ctx, uint8_t out[32]);
void fch256_free(fch256_ctx *ctx);

void fch512_init(fch512_ctx *ctx);
int  fch512_update(fch512_ctx *ctx, const uint8_t *data, size_t len);
int  fch512_final_checked(fch512_ctx *ctx, uint8_t out[64]);
void fch512_final(fch512_ctx *ctx, uint8_t out[64]);
void fch512_free(fch512_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
