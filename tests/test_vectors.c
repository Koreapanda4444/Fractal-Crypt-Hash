#include <stdio.h>
#include <string.h>

#include "fch.h"

static void to_hex(const unsigned char *in, size_t in_len, char *out) {
	static const char hexdigits[] = "0123456789abcdef";
	for (size_t i = 0; i < in_len; i++) {
		unsigned char b = in[i];
		out[i * 2 + 0] = hexdigits[b >> 4];
		out[i * 2 + 1] = hexdigits[b & 0x0F];
	}
	out[in_len * 2] = '\0';
}

static int check_256(const unsigned char *msg, size_t len, const char *expected_hex) {
	unsigned char out[32];
	char hex[65];

	if (!fch_hash_256_checked(msg, len, out)) {
		printf("FAIL: FCH-256 hashing failed\n");
		return 0;
	}
	to_hex(out, sizeof(out), hex);

	if (strcmp(hex, expected_hex) != 0) {
		printf("FAIL: FCH-256 vector mismatch\n");
		printf("  expected: %s\n", expected_hex);
		printf("  got     : %s\n", hex);
		return 0;
	}
	return 1;
}

static int check_512(const unsigned char *msg, size_t len, const char *expected_hex) {
	unsigned char out[64];
	char hex[129];

	if (!fch_hash_512_checked(msg, len, out)) {
		printf("FAIL: FCH-512 hashing failed\n");
		return 0;
	}
	to_hex(out, sizeof(out), hex);

	if (strcmp(hex, expected_hex) != 0) {
		printf("FAIL: FCH-512 vector mismatch\n");
		printf("  expected: %s\n", expected_hex);
		printf("  got     : %s\n", hex);
		return 0;
	}
	return 1;
}

int main(void) {
	int ok = 1;

	/* Empty string */
	ok &= check_256((const unsigned char *)"", 0,
		"5b9f539558c8a96bb7b8a4933d22ef812d89357974605fdb8a52b5f47c50dc29");
	ok &= check_512((const unsigned char *)"", 0,
		"84aa6df797db46779b11a7313d3e194db8ea12c1b6cfa981bd081a5d9c33993f"
		"4941040ef86cdf9e6977e13bc23dc9442140ec90c9f7f59eaf1386a200730c99");

	/* "abc" */
	ok &= check_256((const unsigned char *)"abc", 3,
		"206e3526930e745eaf1c09287a42646d73f2cc9b5f2a4fa3a1fff4a10c1ffd38");
	ok &= check_512((const unsigned char *)"abc", 3,
		"2241aa81773b7c4eee33957bbcc44f2e1ba2bdcb4ba075dfc1f026558e2c349ad"
		"c9999df940a372a76de1c1e71fd758118f054d7741ee34e910aa2571c8dc127");

	/* quick brown fox */
	ok &= check_256((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"2ac12d6772408b515278c5fa21ff89e091962a25b51f686c84737b5b8f2bef06");
	ok &= check_512((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"fa692c18d2a86ce2c9900b0dd13868bf393dd2d5dd1f018d97e674e216069251"
		"a09369f39e6b2ce50404cc99222e54e160f470292a445154ad2fa289c62b2bc5");

	if (ok) {
		printf("PASS: fixed test vectors\n");
		return 0;
	}

	return 1;
}
