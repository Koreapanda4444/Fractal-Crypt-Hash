#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "fractal.h"
#include "params.h"
#include "debug_hooks.h"

#define MAX_DEPTH 64
#define MAX_WORDS FCH_512_STATE_WORDS
#define ROUNDS 128
#define INPUT_LEN 8192

typedef struct {
    uint64_t x[MAX_WORDS];
    int seen;
} agg_t;

typedef struct {
    double sum;
    double min;
    double max;
    int count;
} stats_t;

static agg_t g_base_node[MAX_DEPTH];
static agg_t g_mod_node[MAX_DEPTH];
static agg_t g_base_leaf[MAX_DEPTH];
static agg_t g_mod_leaf[MAX_DEPTH];

static uint64_t g_base_root[MAX_WORDS];
static uint64_t g_mod_root[MAX_WORDS];
static int g_base_root_seen = 0;
static int g_mod_root_seen = 0;

static int g_collecting_base = 1;
static int g_max_depth_seen = -1;
static size_t g_state_words = 0;

static void reset_aggs(void) {
    memset(g_base_node, 0, sizeof(g_base_node));
    memset(g_mod_node, 0, sizeof(g_mod_node));
    memset(g_base_leaf, 0, sizeof(g_base_leaf));
    memset(g_mod_leaf, 0, sizeof(g_mod_leaf));
    memset(g_base_root, 0, sizeof(g_base_root));
    memset(g_mod_root, 0, sizeof(g_mod_root));
    g_base_root_seen = 0;
    g_mod_root_seen = 0;
    g_max_depth_seen = -1;
}

static void agg_xor(agg_t *a, const uint64_t *state, size_t words) {
    if (!a || !state)
        return;
    if (words > MAX_WORDS)
        words = MAX_WORDS;
    for (size_t i = 0; i < words; i++) {
        a->x[i] ^= state[i];
    }
    a->seen = 1;
}

#if defined(__GNUC__)
static inline int popcount64(uint64_t v) {
    return __builtin_popcountll((unsigned long long)v);
}
#else
static inline int popcount64(uint64_t v) {
    int c = 0;
    while (v) {
        c += (int)(v & 1u);
        v >>= 1;
    }
    return c;
}
#endif

static double diff_ratio_words(const uint64_t *a, const uint64_t *b, size_t words) {
    if (!a || !b || words == 0)
        return 0.0;
    if (words > MAX_WORDS)
        words = MAX_WORDS;

    int bits = 0;
    for (size_t i = 0; i < words; i++) {
        bits += popcount64(a[i] ^ b[i]);
    }
    return bits / (double)(words * 64.0);
}

static void stats_add(stats_t *s, double v) {
    if (!s)
        return;
    if (s->count == 0) {
        s->min = v;
        s->max = v;
        s->sum = v;
        s->count = 1;
        return;
    }
    s->sum += v;
    if (v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->count++;
}

void fch_debug_hook(
    fch_hook_point_t point,
    int depth,
    const uint64_t *state,
    size_t state_words
) {
    if (!state)
        return;
    if (depth < 0 || depth >= MAX_DEPTH)
        return;

    g_state_words = state_words;
    if (depth > g_max_depth_seen)
        g_max_depth_seen = depth;

    if (g_collecting_base) {
        if (point == FCH_HOOK_AFTER_LEAF) {
            agg_xor(&g_base_leaf[depth], state, state_words);
        } else if (point == FCH_HOOK_AFTER_NODE) {
            agg_xor(&g_base_node[depth], state, state_words);
        } else if (point == FCH_HOOK_AFTER_ROOT) {
            if (state_words > MAX_WORDS)
                state_words = MAX_WORDS;
            memcpy(g_base_root, state, state_words * sizeof(uint64_t));
            g_base_root_seen = 1;
        }
    } else {
        if (point == FCH_HOOK_AFTER_LEAF) {
            agg_xor(&g_mod_leaf[depth], state, state_words);
        } else if (point == FCH_HOOK_AFTER_NODE) {
            agg_xor(&g_mod_node[depth], state, state_words);
        } else if (point == FCH_HOOK_AFTER_ROOT) {
            if (state_words > MAX_WORDS)
                state_words = MAX_WORDS;
            memcpy(g_mod_root, state, state_words * sizeof(uint64_t));
            g_mod_root_seen = 1;
        }
    }
}

static void run_process(const uint8_t *data, size_t len, int collecting_base) {
    g_collecting_base = collecting_base;
    fch_state_t out = fch_process(data, len, 0, FCH_256_STATE_WORDS);
    if (out.state)
        free(out.state);
}

static void print_stage_stats(const char *stage, const stats_t *stats, int max_depth) {
    for (int d = 0; d <= max_depth; d++) {
        if (stats[d].count == 0)
            continue;
        double avg = stats[d].sum / (double)stats[d].count;
        printf(
            "%s,%d,%.2f,%.2f,%.2f,%d\n",
            stage,
            d,
            avg * 100.0,
            stats[d].min * 100.0,
            stats[d].max * 100.0,
            stats[d].count
        );
    }
}

int main(void) {
    uint8_t base[INPUT_LEN];
    uint8_t mod[INPUT_LEN];

    for (size_t i = 0; i < INPUT_LEN; i++) {
        base[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }

    stats_t node_stats[MAX_DEPTH];
    stats_t leaf_stats[MAX_DEPTH];
    stats_t root_stats[MAX_DEPTH];
    memset(node_stats, 0, sizeof(node_stats));
    memset(leaf_stats, 0, sizeof(leaf_stats));
    memset(root_stats, 0, sizeof(root_stats));

    int global_max_depth = -1;

    for (int r = 0; r < ROUNDS; r++) {
        memcpy(mod, base, INPUT_LEN);
        mod[r % INPUT_LEN] ^= (uint8_t)(1u << (unsigned)(r % 8));

        reset_aggs();
        run_process(base, INPUT_LEN, 1);
        int base_max = g_max_depth_seen;
        run_process(mod, INPUT_LEN, 0);
        int mod_max = g_max_depth_seen;

        int max_depth = base_max > mod_max ? base_max : mod_max;
        if (max_depth > global_max_depth)
            global_max_depth = max_depth;

        size_t words = (g_state_words == 0) ? FCH_256_STATE_WORDS : g_state_words;

        for (int d = 0; d <= max_depth; d++) {
            if (!(g_base_node[d].seen || g_mod_node[d].seen))
                continue;
            double dv = diff_ratio_words(g_base_node[d].x, g_mod_node[d].x, words);
            stats_add(&node_stats[d], dv);
        }

        for (int d = 0; d <= max_depth; d++) {
            if (!(g_base_leaf[d].seen || g_mod_leaf[d].seen))
                continue;
            double dv = diff_ratio_words(g_base_leaf[d].x, g_mod_leaf[d].x, words);
            stats_add(&leaf_stats[d], dv);
        }

        if (g_base_root_seen || g_mod_root_seen) {
            double dv = diff_ratio_words(g_base_root, g_mod_root, words);
            stats_add(&root_stats[0], dv);
        } else {
            if (g_base_node[0].seen || g_mod_node[0].seen) {
                double dv = diff_ratio_words(g_base_node[0].x, g_mod_node[0].x, words);
                stats_add(&root_stats[0], dv);
            }
        }
    }

    printf("stage,level,avg,min,max,rounds\n");
    print_stage_stats("node", node_stats, global_max_depth);
    print_stage_stats("leaf", leaf_stats, global_max_depth);
    print_stage_stats("root", root_stats, 0);

    if (global_max_depth >= 2) {
        int mid = global_max_depth / 2;
        if (node_stats[mid].count > 0) {
            double avg = node_stats[mid].sum / (double)node_stats[mid].count;
            fprintf(
                stderr,
                "INFO: mid depth=%d node diffusion avg=%.2f%% (min=%.2f%% max=%.2f%%)\n",
                mid,
                avg * 100.0,
                node_stats[mid].min * 100.0,
                node_stats[mid].max * 100.0
            );
        }
        if (leaf_stats[global_max_depth].count > 0) {
            double avg = leaf_stats[global_max_depth].sum / (double)leaf_stats[global_max_depth].count;
            fprintf(
                stderr,
                "INFO: leaf depth=%d leaf diffusion avg=%.2f%% (min=%.2f%% max=%.2f%%)\n",
                global_max_depth,
                avg * 100.0,
                leaf_stats[global_max_depth].min * 100.0,
                leaf_stats[global_max_depth].max * 100.0
            );
        }
        if (root_stats[0].count > 0) {
            double avg = root_stats[0].sum / (double)root_stats[0].count;
            fprintf(
                stderr,
                "INFO: root diffusion avg=%.2f%% (min=%.2f%% max=%.2f%%)\n",
                avg * 100.0,
                root_stats[0].min * 100.0,
                root_stats[0].max * 100.0
            );
        }
    }

    return 0;
}
