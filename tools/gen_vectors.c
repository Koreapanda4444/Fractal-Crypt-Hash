#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fch.h"

static void print_hex(const uint8_t *buf, size_t len) {
	static const char hexdigits[] = "0123456789abcdef";
	for (size_t i = 0; i < len; i++) {
		uint8_t b = buf[i];
		putchar(hexdigits[b >> 4]);
		putchar(hexdigits[b & 0x0F]);
	}
}

static void vec(const char *label, const uint8_t *msg, size_t len) {
	uint8_t out256[32];
	uint8_t out512[64];

	fch_hash_256(msg, len, out256);
	fch_hash_512(msg, len, out512);

	printf("%s\n", label);
	printf("  256: ");
	print_hex(out256, sizeof(out256));
	putchar('\n');
	printf("  512: ");
	print_hex(out512, sizeof(out512));
	putchar('\n');
}

int main(void) {
	vec("empty", (const uint8_t *)"", 0);
	vec("abc", (const uint8_t *)"abc", 3);
	vec("quick_brown_fox", (const uint8_t *)"The quick brown fox jumps over the lazy dog", 43);
	return 0;
}

