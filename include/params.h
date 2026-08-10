#if !defined(FCH_PARAMS_H) || defined(FCH_PARAMS_ALLOW_REINCLUDE)
#ifndef FCH_PARAMS_H
#define FCH_PARAMS_H
#endif

#include <stdint.h>
#include <stddef.h>

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

#ifndef FCH_MIN_BLOCK_SIZE
#define FCH_MIN_BLOCK_SIZE 64
#endif
#ifndef FCH_MAX_DEPTH_CAP
#define FCH_MAX_DEPTH_CAP 16
#endif

#ifndef FCH_N_MIN
#define FCH_N_MIN 2
#endif
#ifndef FCH_N_MAX
#define FCH_N_MAX 6
#endif

#endif
