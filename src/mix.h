#ifndef FCH_MIX_H
#define FCH_MIX_H

#include <stddef.h>
#include <stdint.h>

#define FCH_MIX_BLOCK_SIZE 128u
#define FCH_MIX_ROUNDS 16u
#define FCH_MIX_REDUCED_ROUND_REFERENCE 8u
#define FCH_MIX_ROUND_MARGIN \
    (FCH_MIX_ROUNDS - FCH_MIX_REDUCED_ROUND_REFERENCE)

#define FCH_TREE_ENCODING_VERSION UINT64_C(2)
#define FCH_PADDING_FORMAT_VERSION UINT64_C(1)

#define FCH_TREE_TAG_LEAF_HEADER UINT64_C(0x324641454C484346)
#define FCH_TREE_TAG_LEAF_DATA UINT64_C(0x325441444C484346)
#define FCH_TREE_TAG_NODE_HEADER UINT64_C(0x3245444F4E484346)
#define FCH_TREE_TAG_NODE_CHILD UINT64_C(0x32444C4843484346)
#define FCH_TREE_TAG_OUTPUT UINT64_C(0x323054554F484346)

#define FCH_DOMAIN_LEAF UINT64_C(0x32304D444C484346)
#define FCH_DOMAIN_NODE UINT64_C(0x32304D444E484346)
#define FCH_DOMAIN_OUTPUT_256 UINT64_C(0x323635324F484346)
#define FCH_DOMAIN_OUTPUT_512 UINT64_C(0x323231354F484346)

#define FCH_MIX_FLAG_LEAF_HEADER UINT64_C(0x0000000000000001)
#define FCH_MIX_FLAG_LEAF_DATA UINT64_C(0x0000000000000002)
#define FCH_MIX_FLAG_NODE_HEADER UINT64_C(0x0000000000000004)
#define FCH_MIX_FLAG_NODE_CHILD UINT64_C(0x0000000000000008)
#define FCH_MIX_FLAG_OUTPUT UINT64_C(0x0000000000000010)
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

#ifdef FCH_ENABLE_REDUCED_ROUND_TESTS
int fch_mix_compress_rounds(
    uint64_t *state,
    size_t state_words,
    const uint8_t block[FCH_MIX_BLOCK_SIZE],
    size_t block_length,
    uint64_t counter,
    uint64_t domain,
    uint64_t flags,
    unsigned int rounds
);

int fch_mix_test_prepare(
    uint64_t work[16],
    uint64_t message[16],
    const uint64_t state[8],
    const uint8_t block[FCH_MIX_BLOCK_SIZE],
    size_t block_length,
    uint64_t counter,
    uint64_t domain,
    uint64_t flags
);

int fch_mix_test_forward(
    uint64_t work[16],
    const uint64_t message[16],
    unsigned int start_round,
    unsigned int round_count
);

int fch_mix_test_inverse(
    uint64_t work[16],
    const uint64_t message[16],
    unsigned int start_round,
    unsigned int round_count
);
#endif

int fch_mix_finalize_output(
    uint64_t *state,
    size_t state_words,
    size_t output_words,
    size_t original_length,
    size_t padded_length,
    size_t root_level,
    size_t root_first_leaf,
    size_t root_leaf_count,
    size_t root_byte_offset,
    size_t root_byte_length
);

#endif
