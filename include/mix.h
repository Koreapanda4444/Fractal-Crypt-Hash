#ifndef FCH_MIX_H
#define FCH_MIX_H

#include <stddef.h>
#include <stdint.h>

#define FCH_MIX_BLOCK_SIZE 128u
#define FCH_MIX_ROUNDS 12u

#define FCH_MIX_FLAG_PARAMETER UINT64_C(0x0000000000000001)
#define FCH_MIX_FLAG_LEAF_DATA UINT64_C(0x0000000000000002)
#define FCH_MIX_FLAG_NODE_CHILD UINT64_C(0x0000000000000004)
#define FCH_MIX_FLAG_OUTPUT UINT64_C(0x0000000000000008)
#define FCH_MIX_FLAG_FINAL UINT64_C(0x8000000000000000)

int fch_mix_init(
    uint64_t *state,
    size_t state_words,
    uint64_t domain
);

int fch_mix_compress(
    uint64_t *state,
    size_t state_words,
    const uint8_t block[FCH_MIX_BLOCK_SIZE],
    size_t block_length,
    uint64_t counter,
    uint64_t domain,
    uint64_t flags
);

int fch_mix_finalize_output(
    uint64_t *state,
    size_t state_words,
    size_t output_words
);

#endif
