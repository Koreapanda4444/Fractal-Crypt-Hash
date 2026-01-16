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

	fch_hash_256(msg, len, out);
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

	fch_hash_512(msg, len, out);
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
		"59dceb4ed296023ce88996c0583dc2a03510cd2dc7c4b480f0e07574c8585510");
	ok &= check_512((const unsigned char *)"", 0,
		"5076953a329427594a6c76fb10d023e5e0cacf0d3200fb4f7a3af8d2b213c66b"
		"4326a4636ec1896c0332d594ba5acc6c8ec5594f0006e4478d5cd21c93eb9804");

	/* "abc" */
	ok &= check_256((const unsigned char *)"abc", 3,
		"f2704d7f2fd863b1dce5b5ff702606ade345a7666b3116655f41d28c217f11f8");
	ok &= check_512((const unsigned char *)"abc", 3,
		"573d8fd1480613da1f70d481741f7fb57077a14e1c002f75d4fc2c2a3005fa00"
		"95da0d6d3d63bc086cdb3c3899e83c121af92f8ae63f566150b0bec075713a2b");

	/* quick brown fox */
	ok &= check_256((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"248cb3432d515f534ada1d41ad726aae913703627f8b0b584cf7a753aa0010f7");
	ok &= check_512((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"2d64397cbc732063e816fdc827ea064efdefb4bf0968585ad065054bef5eca91"
		"b5868c5298f26f15aec5ff491f3695d7e0a41bd9ce3ebafadbcbbd72b3110ade");

	if (ok) {
		printf("PASS: fixed test vectors\n");
		return 0;
	}

	return 1;
}
