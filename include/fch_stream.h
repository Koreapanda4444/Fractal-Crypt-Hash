#ifndef FCH_STREAM_H
#define FCH_STREAM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Streaming input is written to an anonymous temporary file and processed
 * through a random-access reader during finalization. RAM usage stays bounded
 * by the recursive state and fixed-size I/O buffers. Finalization can be
 * slower than the one-shot API and requires temporary-file support.
 */

typedef struct {
	void *storage;
	size_t length;
	int failed;
	int finalized;
} fch256_ctx;

typedef struct {
	void *storage;
	size_t length;
	int failed;
	int finalized;
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
