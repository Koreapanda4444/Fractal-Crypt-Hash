#if !defined(FCH_PARAMS_H) || defined(FCH_PARAMS_ALLOW_REINCLUDE)
#ifndef FCH_PARAMS_H
#define FCH_PARAMS_H
#endif

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#if CHAR_BIT != 8
#error "FCH requires eight-bit bytes"
#endif

#ifndef FCH_INTERNAL_STATE_WORDS
#define FCH_INTERNAL_STATE_WORDS 8
#endif
#ifndef FCH_256_STATE_WORDS
#define FCH_256_STATE_WORDS FCH_INTERNAL_STATE_WORDS
#endif
#ifndef FCH_512_STATE_WORDS
#define FCH_512_STATE_WORDS FCH_INTERNAL_STATE_WORDS
#endif

#ifndef FCH_256_OUTPUT_WORDS
#define FCH_256_OUTPUT_WORDS 4
#endif
#ifndef FCH_512_OUTPUT_WORDS
#define FCH_512_OUTPUT_WORDS 8
#endif

#if FCH_INTERNAL_STATE_WORDS != 8
#error "FCH ARX core requires an eight-word internal state"
#endif
#if FCH_256_STATE_WORDS != FCH_INTERNAL_STATE_WORDS || \
    FCH_512_STATE_WORDS != FCH_INTERNAL_STATE_WORDS
#error "FCH variants must use the common internal state width"
#endif
#if FCH_256_OUTPUT_WORDS != 4 || FCH_512_OUTPUT_WORDS != 8
#error "FCH output widths are fixed at 256 and 512 bits"
#endif

#define FCH_PADDING_MIN_BYTES 64u
#define FCH_TREE_LEAF_BYTES 1024u
#define FCH_TREE_ARITY 2u

#if FCH_PADDING_MIN_BYTES < 9u
#error "FCH padding requires room for the marker and length field"
#endif
#if FCH_TREE_LEAF_BYTES < FCH_PADDING_MIN_BYTES
#error "FCH tree leaves must hold the minimum padded message"
#endif
#if FCH_TREE_ARITY != 2u
#error "FCH tree encoding version 2 requires binary nodes"
#endif

#endif
