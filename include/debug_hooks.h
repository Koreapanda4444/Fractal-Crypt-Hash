#ifndef FCH_DEBUG_HOOKS_H
#define FCH_DEBUG_HOOKS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FCH_HOOK_AFTER_LEAF = 1,
    FCH_HOOK_AFTER_NODE = 2,
    FCH_HOOK_AFTER_ROOT = 3
} fch_hook_point_t;

#ifdef FCH_DEBUG_HOOKS










void fch_debug_hook(
    fch_hook_point_t point,
    int depth,
    const uint64_t *state,
    size_t state_words
);

#define FCH_DEBUG_EMIT(_point, _depth, _state, _words) \
    fch_debug_hook((_point), (_depth), (_state), (_words))

#else

#define FCH_DEBUG_EMIT(_point, _depth, _state, _words) ((void)0)

#endif

#ifdef __cplusplus
}
#endif

#endif
