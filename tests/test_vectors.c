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

	ok &= check_256((const unsigned char *)"", 0,
		"591a3e8b905a36eb6c89c5db9a65e521d3128fe1c60ec330f917ea80b1182b6c");
	ok &= check_512((const unsigned char *)"", 0,
		"bb67776c28aff2b306e55ae975b036584c05fa1bc39916c740d7c46f29d82679"
		"e493de1d3be6755a6e854e2889a09db55202d9213e915b7fc6db0b2a0c01b953");

	ok &= check_256((const unsigned char *)"abc", 3,
		"2bf673ce22b55e5e1c38fbc76c56b3952a4a2e02be924ea4b3fb6bf98900ce52");
	ok &= check_512((const unsigned char *)"abc", 3,
		"be0a25570e7b7083ec3bbf80b7c09da633d7c6a66f68b2574f9b305af590ed58"
		"cec99b3c348a4c3d0ed8c47ee0063fb46b7aa21a126fa93dcab9cab4df1b71d2");

	ok &= check_256((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"6d7b894f7d9bf047a1d9ceaffe6d53e52349fd666c56664a753ee415d3c4ebfc");
	ok &= check_512((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"eabf40e7ae323e76433d24a1d3d02eccb1af0dd68a3700c147c2d2a94f859d96"
		"790f2f87ae566ddcac50e522ba08b0dcd7af4d0699d77e88d03fb90cbdfb7f21");

	if (ok) {
		printf("PASS: fixed test vectors\n");
		return 0;
	}

	return 1;
}
