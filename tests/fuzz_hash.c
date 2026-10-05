#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fch_stream.h"
#include "fractal.h"
#include "params.h"

enum {
    FCH_FUZZ_MAX_INPUT = 1024 * 1024
};

static int all_zero(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

static void require_or_abort(int condition) {
    if (!condition)
        abort();
}

static void check_split_invariants(const uint8_t *data, size_t size) {
    fch_block_t blocks[FCH_TREE_ARITY];
    size_t count = fch_fractal_split(
        data,
        size,
        blocks,
        FCH_TREE_ARITY
    );

    if (size == 0u) {
        require_or_abort(count == 0u);
        return;
    }

    require_or_abort(count > 0u && count <= FCH_TREE_ARITY);

    size_t covered = 0;
    for (size_t i = 0; i < count; i++) {
        require_or_abort(blocks[i].offset == covered);
        require_or_abort(blocks[i].length > 0u);
        require_or_abort(blocks[i].length <= size - covered);
        covered += blocks[i].length;
    }
    require_or_abort(covered == size);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if ((!data && size > 0) || size > FCH_FUZZ_MAX_INPUT)
        return 0;

    uint8_t direct256[32];
    uint8_t direct512[64];
    if (!fch_hash_256_checked(data, size, direct256) ||
        !fch_hash_512_checked(data, size, direct512))
        return 0;

    check_split_invariants(data, size);

    fch256_ctx ctx256;
    fch512_ctx ctx512;
    fch256_init(&ctx256);
    fch512_init(&ctx512);

    if (!fch256_update(&ctx256, NULL, 0) ||
        !fch512_update(&ctx512, NULL, 0)) {
        fch256_free(&ctx256);
        fch512_free(&ctx512);
        return 0;
    }

    size_t offset = 0;
    size_t chunk_index = 0;
    while (offset < size) {
        size_t selector = (offset + chunk_index) % size;
        size_t count = (size_t)data[selector] + 1u;
        if (count > size - offset)
            count = size - offset;

        if (!fch256_update(&ctx256, data + offset, count) ||
            !fch512_update(&ctx512, data + offset, count)) {
            fch256_free(&ctx256);
            fch512_free(&ctx512);
            return 0;
        }
        offset += count;
        chunk_index++;
    }

    uint8_t streamed256[32];
    uint8_t streamed512[64];
    if (!fch256_final_checked(&ctx256, streamed256) ||
        !fch512_final_checked(&ctx512, streamed512)) {
        fch256_free(&ctx256);
        fch512_free(&ctx512);
        return 0;
    }

    require_or_abort(
        memcmp(direct256, streamed256, sizeof(direct256)) == 0
    );
    require_or_abort(
        memcmp(direct512, streamed512, sizeof(direct512)) == 0
    );
    require_or_abort(ctx256.storage == NULL && ctx512.storage == NULL);
    require_or_abort(!fch256_update(&ctx256, data, size > 0 ? 1u : 0u));
    require_or_abort(!fch512_update(&ctx512, data, size > 0 ? 1u : 0u));

    memset(streamed256, 0xA5, sizeof(streamed256));
    memset(streamed512, 0xA5, sizeof(streamed512));
    require_or_abort(!fch256_final_checked(&ctx256, streamed256));
    require_or_abort(!fch512_final_checked(&ctx512, streamed512));
    require_or_abort(all_zero(streamed256, sizeof(streamed256)));
    require_or_abort(all_zero(streamed512, sizeof(streamed512)));

    fch256_free(&ctx256);
    fch512_free(&ctx512);
    fch256_free(&ctx256);
    fch512_free(&ctx512);
    return 0;
}
