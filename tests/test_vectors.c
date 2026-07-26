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
		"38f65f245b346088ae04f935e3fb998b6e5aebc464e4585af4b22c4e82ffbb4f");
	ok &= check_512((const unsigned char *)"", 0,
		"7e933eaefdf363697ff0555ce54ca3b764d8db6ba41a91f8a6465aae95be5784"
		"5746750dc416c38490f2c11a1c8adda0bfd2e3c17909f85dc86bac31cc471ec5");

	/* "abc" */
	ok &= check_256((const unsigned char *)"abc", 3,
		"5fca62fb37616b24e0cf8406f6a73aeefeb45957ded4249b0b02fe103653d999");
	ok &= check_512((const unsigned char *)"abc", 3,
		"2fd6cc30afcb9b54a257cef3427f05e828786752b84261fd7a4a7036025a7fa7"
		"4b41fee088dee5502a2c31b9cfb1908bda8088c0f82d291f6f3c4e230c71a505");

	/* quick brown fox */
	ok &= check_256((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"1dda94f7ae6c24670a49f1fa04ba702ce71b2341453f8e0160eda55666ba80ea");
	ok &= check_512((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"2a9b6fd1db836248d0f0cf598054b2b411d255cf425f2eacabdb834c3f19c801"
		"0a55235a5e774c2942bb9128cd681b1f777e87ef856cd00f379edab6945fc60d");

	if (ok) {
		printf("PASS: fixed test vectors\n");
		return 0;
	}

	return 1;
}
