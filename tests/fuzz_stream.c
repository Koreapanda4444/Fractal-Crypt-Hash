#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fch_stream.h"
#include "params.h"

enum {
    FCH_FUZZ_STREAM_MAX_INPUT = 1024 * 1024
};

static void require_or_abort(int condition) {
    if (!condition)
        abort();
}

static int all_zero(const uint8_t *data, size_t length) {
    for (size_t i = 0u; i < length; i++) {
        if (data[i] != 0u)
            return 0;
    }
    return 1;
}

static size_t next_chunk(
    const uint8_t *data,
    size_t size,
    size_t offset,
    size_t index
) {
    static const size_t widths[] = {
        1u, 7u, 63u, 64u, 65u,
        FCH_TREE_LEAF_BYTES - 1u,
        FCH_TREE_LEAF_BYTES,
        FCH_TREE_LEAF_BYTES + 1u,
        4093u, 8191u
    };
    uint8_t selector = data[(offset + index) % size];
    size_t count;

    if ((selector & 0x80u) != 0u)
        count = (size_t)(selector & 0x7Fu) + 1u;
    else
        count = widths[
            selector % (sizeof(widths) / sizeof(widths[0]))
        ];

    if (count > size - offset)
        count = size - offset;
    return count;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if ((!data && size > 0u) || size > FCH_FUZZ_STREAM_MAX_INPUT)
        return 0;

    uint8_t expected256[32];
    uint8_t expected512[64];
    require_or_abort(fch_hash_256_checked(data, size, expected256));
    require_or_abort(fch_hash_512_checked(data, size, expected512));

    fch256_ctx ctx256;
    fch512_ctx ctx512;
    fch256_init(&ctx256);
    fch512_init(&ctx512);
    require_or_abort(fch256_update(&ctx256, NULL, 0u));
    require_or_abort(fch512_update(&ctx512, NULL, 0u));

    size_t offset = 0u;
    size_t index = 0u;
    while (offset < size) {
        const uint8_t *zero_input =
            (index & 1u) != 0u ? data + offset : NULL;
        require_or_abort(fch256_update(&ctx256, zero_input, 0u));
        require_or_abort(fch512_update(&ctx512, zero_input, 0u));

        size_t count = next_chunk(data, size, offset, index);
        require_or_abort(count > 0u);
        require_or_abort(fch256_update(&ctx256, data + offset, count));
        require_or_abort(fch512_update(&ctx512, data + offset, count));
        offset += count;
        index++;
        require_or_abort(ctx256.length == offset);
        require_or_abort(ctx512.length == offset);
    }

    require_or_abort(fch256_update(&ctx256, data, 0u));
    require_or_abort(fch512_update(&ctx512, data, 0u));

    uint8_t actual256[32];
    uint8_t actual512[64];
    require_or_abort(fch256_final_checked(&ctx256, actual256));
    require_or_abort(fch512_final_checked(&ctx512, actual512));
    require_or_abort(memcmp(expected256, actual256, sizeof(actual256)) == 0);
    require_or_abort(memcmp(expected512, actual512, sizeof(actual512)) == 0);
    require_or_abort(ctx256.storage == NULL && ctx512.storage == NULL);
    require_or_abort(ctx256.finalized && ctx512.finalized);

    require_or_abort(!fch256_update(&ctx256, data, size));
    require_or_abort(!fch512_update(&ctx512, data, size));
    memset(actual256, 0xA5, sizeof(actual256));
    memset(actual512, 0xA5, sizeof(actual512));
    require_or_abort(!fch256_final_checked(&ctx256, actual256));
    require_or_abort(!fch512_final_checked(&ctx512, actual512));
    require_or_abort(all_zero(actual256, sizeof(actual256)));
    require_or_abort(all_zero(actual512, sizeof(actual512)));

    fch256_free(&ctx256);
    fch512_free(&ctx512);
    fch256_free(&ctx256);
    fch512_free(&ctx512);
    require_or_abort(ctx256.storage == NULL && ctx512.storage == NULL);
    require_or_abort(ctx256.length == 0u && ctx512.length == 0u);
    require_or_abort(!ctx256.failed && !ctx512.failed);
    require_or_abort(!ctx256.finalized && !ctx512.finalized);
    return 0;
}
