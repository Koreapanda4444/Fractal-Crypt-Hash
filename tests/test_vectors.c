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
		"7d3cedda6c66f17a4b02009eacf71e810d1805819d94012df8258581f0e86d3d");
	ok &= check_512((const unsigned char *)"", 0,
		"dd21a2acebf4efe4dc1a8f43325447d66db3223e0a70f60ef890405f8a86fc23"
		"7cec9bc2b18897eb90e8815514527500ab0adb8fb391e12dbec943b85703e6a9");

	/* "abc" */
	ok &= check_256((const unsigned char *)"abc", 3,
		"301a2ae4799ccd901d6301528b35bc2a813e41e0c2f29bc5e3000e6db8de1c46");
	ok &= check_512((const unsigned char *)"abc", 3,
		"8eae7162296ad270291e20b319791ecc25667b0dd6995f26be3f03eec7b787b0"
		"4062823b805760f3fe5546d5ec34194f4b50a9a3307d6c49425dc9979fafb31e");

	/* quick brown fox */
	ok &= check_256((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"6e6713a8b51157af963db58d54ec004f7f14dde3a8925af3febce2bf0a06076a");
	ok &= check_512((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"8da21ec4c9477d0e3e3966cf1f1db2051a9af7329d1b20df488669bad2eaa1a6"
		"dacbff89af287e7f43ca28be2fc01bacc65e2f098202a95ebde050b7cea2cdc1");

	if (ok) {
		printf("PASS: fixed test vectors\n");
		return 0;
	}

	return 1;
}
