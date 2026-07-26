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
		"2122420c1f9892042b1035c7353e2e3feed2884326031e4e14d82853ea152444");
	ok &= check_512((const unsigned char *)"", 0,
		"29a65bd8be899adebda5d04e0c2b1a7dab4e517daf9a542b59c905ba35884b16"
		"a736299163cf116763ea8b52f9ca849ce05c18deab176a59387a014d359f7add");

	/* "abc" */
	ok &= check_256((const unsigned char *)"abc", 3,
		"6eb735442b3d1616b99e8b5ec229f9349421ce84bfb6447b046ac0691fe9f84d");
	ok &= check_512((const unsigned char *)"abc", 3,
		"a99ee96c4f4d2cf84f408e967f96e56084e51f2bd3871f3aa33f6354e9b8e27e"
		"138a2f66a7b307155c46a162f27646746552df4e33fc4b1ce0ed9494339c9be7");

	/* quick brown fox */
	ok &= check_256((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"aaac23a5abf4de365266f4dd3e7db7b1c68d548361aa8740636deddd45cd412a");
	ok &= check_512((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"c3b608df3b38fb6330428338b96adba0136ffea5a74ebd70d97810779e06df19"
		"c3b16cbfe8e93664ddd0d302f6620bf594b19e40b8625e4c4321998f9ef29964");

	if (ok) {
		printf("PASS: fixed test vectors\n");
		return 0;
	}

	return 1;
}
