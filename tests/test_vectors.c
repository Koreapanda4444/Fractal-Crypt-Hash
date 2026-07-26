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
		"7817c10e1806fe73cf318d6f00717197182bf1803bfc49b408d2518cb7ddbef7");
	ok &= check_512((const unsigned char *)"", 0,
		"1c42353ed38e3a18b92843f133ebe4b83771eb084f63ab63f558a1cc1e05ddd1"
		"7fad7d12c75e2d1871ed20ba9f0aca0ebee639763e743632533756c76e410e4e");

	/* "abc" */
	ok &= check_256((const unsigned char *)"abc", 3,
		"4b8d5548c2a9e141db4fcd2c80e98da86f85ceb0905838fe408f8054c320ef14");
	ok &= check_512((const unsigned char *)"abc", 3,
		"b820a5d49517bd0e21b9d416af7caa0e484c14a4245eb84be278f1be755a04db"
		"1a5d08d7a6beb288fc4cf1158c72e80bca5780679642ed1db599c5896e24ffa3");

	/* quick brown fox */
	ok &= check_256((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"844f9290d513f4b2940a54ad39276c5f9e122571d7dd2b564841bc0257becb00");
	ok &= check_512((const unsigned char *)"The quick brown fox jumps over the lazy dog", 43,
		"1d632475203320274e70238cab693284424ff7155efdd591a30c4e3d2dc2d2493"
		"bb79bff94dce2ca9cd3365c533b4fdff7f424e7c793c8651bcef190147823ac");

	if (ok) {
		printf("PASS: fixed test vectors\n");
		return 0;
	}

	return 1;
}
