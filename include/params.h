#if !defined(FCH_PARAMS_H) || defined(FCH_PARAMS_ALLOW_REINCLUDE)
#ifndef FCH_PARAMS_H
#define FCH_PARAMS_H
#endif

#include <stdint.h>
#include <stddef.h>

#ifndef FCH_256_STATE_WORDS
#define FCH_256_STATE_WORDS 4
#endif
#ifndef FCH_512_STATE_WORDS
#define FCH_512_STATE_WORDS 8
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
