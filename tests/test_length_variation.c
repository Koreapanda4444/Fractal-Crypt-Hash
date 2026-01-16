#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "test_utils.h"

#define MAX_INPUT 4096

typedef struct {
    double sum;
    double min;
    double max;
    int count;
} stats_t;

static void stats_init(stats_t *s) {
    s->sum = 0.0;
    s->min = 1.0;
    s->max = 0.0;
    s->count = 0;
}

static void stats_add(stats_t *s, double v) {
    if (s->count == 0) {
        s->sum = v;
        s->min = v;
        s->max = v;
        s->count = 1;
        return;
    }
    s->sum += v;
    if (v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->count++;
}

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void fill_deterministic(uint8_t *buf, size_t len, uint32_t seed) {
    uint32_t s = seed ? seed : 0xC001D00Du;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(xorshift32(&s) & 0xFFu);
    }
}

static double hash_diff_ratio(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen, int hash_bits) {
    if (hash_bits == 256) {
        uint8_t ha[32], hb[32];
        fch_hash_256(a, alen, ha);
        fch_hash_256(b, blen, hb);
        return bit_diff(ha, hb, 32) / 256.0;
    }

    uint8_t ha[64], hb[64];
    fch_hash_512(a, alen, ha);
    fch_hash_512(b, blen, hb);
    return bit_diff(ha, hb, 64) / 512.0;
}

static size_t build_padding_tail(size_t msg_len, uint8_t *out, size_t out_cap) {
    /* Mirrors fch_pad() behavior: padded_len = max(len+1+8, 64) */
    size_t min_len = msg_len + 1 + 8;
    size_t padded_len = (min_len < 64) ? 64 : min_len;

    size_t tail_len = padded_len - msg_len;
    if (!out || out_cap < tail_len)
        return 0;

    memset(out, 0, tail_len);
    out[0] = 0x80;

    /* Same as fch_pad(): writes native-endian bit length */
    uint64_t bit_len = (uint64_t)msg_len * 8u;
    memcpy(out + tail_len - 8, &bit_len, 8);

    return tail_len;
}

static int check_case(
    const char *kind,
    const char *case_id,
    const uint8_t *a,
    size_t alen,
    const uint8_t *b,
    size_t blen,
    stats_t *s256,
    stats_t *s512
) {
    double d256 = hash_diff_ratio(a, alen, b, blen, 256);
    double d512 = hash_diff_ratio(a, alen, b, blen, 512);

    stats_add(s256, d256);
    stats_add(s512, d512);

    printf(
        "%s,%s,%u,%u,256,%.2f\n",
        kind,
        case_id,
        (unsigned)alen,
        (unsigned)blen,
        d256 * 100.0
    );
    printf(
        "%s,%s,%u,%u,512,%.2f\n",
        kind,
        case_id,
        (unsigned)alen,
        (unsigned)blen,
        d512 * 100.0
    );

    return 1;
}

static int require_threshold(const char *label, const stats_t *s, double min_required) {
    double avg = (s->count > 0) ? (s->sum / (double)s->count) : 0.0;
    fprintf(
        stderr,
        "SUMMARY,%s,avg=%.2f%%,min=%.2f%%,max=%.2f%%,n=%d\n",
        label,
        avg * 100.0,
        s->min * 100.0,
        s->max * 100.0,
        s->count
    );

    if (s->count == 0)
        return 0;
    if (s->min + 1e-12 < min_required)
        return 0;
    return 1;
}

int main(void) {
    /* Pass criteria: no similarity under length-only variation */
    const double MIN_DIFF = 0.35; /* 35% */

    /* Build a deterministic stream; messages are prefixes of this stream. */
    uint8_t stream[MAX_INPUT];
    fill_deterministic(stream, sizeof(stream), 0x12345678u);

    /* Select boundary pairs where padding/fractal decisions may shift. */
    struct { size_t a, b; const char *id; } pairs[] = {
        { 0,    1,    "L0_vs_L1" },
        { 1,    2,    "L1_vs_L2" },
        { 63,   64,   "L63_vs_L64" },
        { 64,   65,   "L64_vs_L65" },
        { 127,  128,  "L127_vs_L128" },
        { 128,  129,  "L128_vs_L129" },
        { 255,  256,  "L255_vs_L256" },
        { 256,  257,  "L256_vs_L257" },
        { 511,  512,  "L511_vs_L512" },
        { 512,  513,  "L512_vs_L513" },
        { 1023, 1024, "L1023_vs_L1024" },
    };

    stats_t adj256, adj512;
    stats_init(&adj256);
    stats_init(&adj512);

    stats_t pad256, pad512;
    stats_init(&pad256);
    stats_init(&pad512);

    printf("kind,case,len_a,len_b,hash,diff_pct\n");

    /* 1) Same prefix, length-only variation */
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        size_t la = pairs[i].a;
        size_t lb = pairs[i].b;
        if (lb > sizeof(stream))
            continue;
        check_case("adj_len", pairs[i].id, stream, la, stream, lb, &adj256, &adj512);
    }

    /* 2) prefix || padding || suffix vs prefix || suffix */
    {
        uint8_t prefix[37];
        uint8_t suffix[29];
        fill_deterministic(prefix, sizeof(prefix), 0xA11CE5E1u);
        fill_deterministic(suffix, sizeof(suffix), 0x51FF1D00u);

        uint8_t pad_tail[128];
        size_t tail_len = build_padding_tail(sizeof(prefix), pad_tail, sizeof(pad_tail));
        if (tail_len == 0) {
            fprintf(stderr, "ERR: padding tail build failed\n");
            return 1;
        }

        uint8_t a[MAX_INPUT];
        uint8_t b[MAX_INPUT];

        size_t alen = 0;
        memcpy(a + alen, prefix, sizeof(prefix)); alen += sizeof(prefix);
        memcpy(a + alen, suffix, sizeof(suffix)); alen += sizeof(suffix);

        size_t blen = 0;
        memcpy(b + blen, prefix, sizeof(prefix)); blen += sizeof(prefix);
        memcpy(b + blen, pad_tail, tail_len);     blen += tail_len;
        memcpy(b + blen, suffix, sizeof(suffix)); blen += sizeof(suffix);

        check_case("pad_inject", "prefix_suffix_vs_prefix_pad_suffix", a, alen, b, blen, &pad256, &pad512);

        /* Also: prefix vs prefix||pad(prefix) */
        uint8_t c[MAX_INPUT];
        size_t clen = 0;
        memcpy(c + clen, prefix, sizeof(prefix)); clen += sizeof(prefix);
        memcpy(c + clen, pad_tail, tail_len);     clen += tail_len;

        check_case("pad_inject", "prefix_vs_prefix_pad", prefix, sizeof(prefix), c, clen, &pad256, &pad512);
    }

    int ok = 1;
    ok &= require_threshold("adj_len_256", &adj256, MIN_DIFF);
    ok &= require_threshold("adj_len_512", &adj512, MIN_DIFF);
    ok &= require_threshold("pad_inject_256", &pad256, MIN_DIFF);
    ok &= require_threshold("pad_inject_512", &pad512, MIN_DIFF);

    if (ok) {
        printf("LENGTH_VARIATION: PASS (min_diff>=%.0f%%)\n", MIN_DIFF * 100.0);
        return 0;
    }

    printf("LENGTH_VARIATION: FAIL (min_diff>=%.0f%%)\n", MIN_DIFF * 100.0);
    return 1;
}
