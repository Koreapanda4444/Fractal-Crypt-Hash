#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bitops.h"
#include "fch.h"
#include "fch_stream.h"
#include "fractal.h"
#include "mix.h"
#include "params.h"

enum {
    FCH_FUZZ_PADDING_MAX_INPUT = 65536,
    FCH_FUZZ_PADDING_MAX_MESSAGE = 16385
};

static void require_or_abort(int condition) {
    if (!condition)
        abort();
}

static size_t select_length(const uint8_t *data, size_t size) {
    static const size_t boundaries[] = {
        0u, 1u, 54u, 55u, 56u, 63u, 64u, 65u,
        1014u, 1015u, 1016u, 1023u, 1024u, 1025u,
        2038u, 2039u, 2040u, 2047u, 2048u, 2049u,
        4095u, 4096u, 4097u, 8191u, 8192u, 8193u,
        16383u, 16384u, 16385u
    };

    if (size == 0u)
        return 0u;
    if (size == 1u)
        return boundaries[data[0] %
            (sizeof(boundaries) / sizeof(boundaries[0]))];

    size_t selector = (size_t)data[0] | ((size_t)data[1] << 8u);
    if ((data[0] & 1u) != 0u) {
        return boundaries[selector %
            (sizeof(boundaries) / sizeof(boundaries[0]))];
    }
    return selector % (FCH_FUZZ_PADDING_MAX_MESSAGE + 1u);
}

static void fill_message(
    uint8_t *message,
    size_t length,
    const uint8_t *data,
    size_t size
) {
    size_t payload = size > 2u ? size - 2u : 0u;

    for (size_t i = 0u; i < length; i++) {
        if (payload > 0u)
            message[i] = data[2u + i % payload];
        else
            message[i] = (uint8_t)(i * 131u + length);
    }
}

static int manual_padding_hashes(
    const uint8_t *message,
    size_t length,
    uint8_t output256[32],
    uint8_t output512[64]
) {
    size_t padded_length = length + 9u;
    if (padded_length < FCH_PADDING_MIN_BYTES)
        padded_length = FCH_PADDING_MIN_BYTES;

    uint8_t *padded = (uint8_t *)calloc(padded_length, 1u);
    if (!padded)
        return 0;
    if (length > 0u)
        memcpy(padded, message, length);
    padded[length] = 0x80u;
    fch_store_le64(
        padded + padded_length - 8u,
        (uint64_t)length * UINT64_C(8)
    );

    if (padded[length] != 0x80u ||
        fch_load_le64(padded + padded_length - 8u) !=
            (uint64_t)length * UINT64_C(8)) {
        free(padded);
        return 0;
    }
    for (size_t i = length + 1u; i < padded_length - 8u; i++) {
        if (padded[i] != 0u) {
            free(padded);
            return 0;
        }
    }

    fch_state_t root = fch_process(
        padded,
        padded_length,
        0,
        FCH_INTERNAL_STATE_WORDS
    );
    free(padded);
    if (!root.state)
        return 0;

    uint64_t state256[FCH_INTERNAL_STATE_WORDS];
    uint64_t state512[FCH_INTERNAL_STATE_WORDS];
    memcpy(state256, root.state, sizeof(state256));
    memcpy(state512, root.state, sizeof(state512));

    int ok = fch_mix_finalize_output(
        state256,
        FCH_INTERNAL_STATE_WORDS,
        FCH_256_OUTPUT_WORDS,
        length,
        padded_length,
        root.tree.level,
        root.tree.first_leaf,
        root.tree.leaf_count,
        root.tree.byte_offset,
        root.tree.byte_length
    ) && fch_mix_finalize_output(
        state512,
        FCH_INTERNAL_STATE_WORDS,
        FCH_512_OUTPUT_WORDS,
        length,
        padded_length,
        root.tree.level,
        root.tree.first_leaf,
        root.tree.leaf_count,
        root.tree.byte_offset,
        root.tree.byte_length
    );
    free(root.state);
    if (!ok)
        return 0;

    for (size_t i = 0u; i < FCH_256_OUTPUT_WORDS; i++)
        fch_store_le64(output256 + i * 8u, state256[i]);
    for (size_t i = 0u; i < FCH_512_OUTPUT_WORDS; i++)
        fch_store_le64(output512 + i * 8u, state512[i]);
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if ((!data && size > 0u) || size > FCH_FUZZ_PADDING_MAX_INPUT)
        return 0;

    size_t length = select_length(data, size);
    uint8_t *message = (uint8_t *)malloc(length > 0u ? length : 1u);
    if (!message)
        return 0;
    fill_message(message, length, data, size);

    uint8_t direct256[32];
    uint8_t direct512[64];
    uint8_t manual256[32];
    uint8_t manual512[64];
    require_or_abort(fch_hash_256_checked(message, length, direct256));
    require_or_abort(fch_hash_512_checked(message, length, direct512));
    require_or_abort(manual_padding_hashes(
        message,
        length,
        manual256,
        manual512
    ));
    require_or_abort(memcmp(direct256, manual256, sizeof(direct256)) == 0);
    require_or_abort(memcmp(direct512, manual512, sizeof(direct512)) == 0);

    fch256_ctx ctx256;
    fch512_ctx ctx512;
    fch256_init(&ctx256);
    fch512_init(&ctx512);
    require_or_abort(fch256_update(&ctx256, message, length));
    require_or_abort(fch512_update(&ctx512, message, length));

    uint8_t streamed256[32];
    uint8_t streamed512[64];
    require_or_abort(fch256_final_checked(&ctx256, streamed256));
    require_or_abort(fch512_final_checked(&ctx512, streamed512));
    require_or_abort(memcmp(direct256, streamed256, sizeof(direct256)) == 0);
    require_or_abort(memcmp(direct512, streamed512, sizeof(direct512)) == 0);
    fch256_free(&ctx256);
    fch512_free(&ctx512);
    free(message);
    return 0;
}
