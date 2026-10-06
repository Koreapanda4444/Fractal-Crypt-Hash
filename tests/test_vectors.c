#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fch_stream.h"

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

static int deterministic_input(const uint8_t *message, size_t length) {
	uint8_t first256[32], repeated256[32];
	uint8_t first512[64], repeated512[64];
	return fch_hash_256_checked(message, length, first256) &&
		fch_hash_256_checked(message, length, repeated256) &&
		fch_hash_512_checked(message, length, first512) &&
		fch_hash_512_checked(message, length, repeated512) &&
		memcmp(first256, repeated256, sizeof(first256)) == 0 &&
		memcmp(first512, repeated512, sizeof(first512)) == 0;
}

static int check_determinism(void) {
	static const char *const messages[] = {
		"", "a", "fractal", "fractal-crypt-hash",
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
	};
	static const size_t lengths[] = {0u, 1u, 64u, 128u, 1024u};
	uint8_t input[1024];
	memset(input, 0x3C, sizeof(input));

	for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
		if (!deterministic_input((const uint8_t *)messages[i], strlen(messages[i]))) {
			printf("FAIL: deterministic hashing for message %zu\n", i);
			return 0;
		}
	}
	for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		if (!deterministic_input(input, lengths[i])) {
			printf("FAIL: deterministic hashing at %zu bytes\n", lengths[i]);
			return 0;
		}
	}
	return 1;
}

static int hex_digit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static int decode_hex(const char *hex, uint8_t *output, size_t length) {
	if (strlen(hex) != length * 2u) return 0;
	for (size_t i = 0; i < length; i++) {
		int a = hex_digit(hex[i * 2u]), b = hex_digit(hex[i * 2u + 1u]);
		if (a < 0 || b < 0) return 0;
		output[i] = (uint8_t)((a << 4) | b);
	}
	return 1;
}

static int corpus_input(const char *recipe, uint8_t *input, size_t length) {
	if (strcmp(recipe, "counter") == 0) {
		for (size_t i = 0; i < length; i++) input[i] = (uint8_t)i;
		return 1;
	}
	if (strncmp(recipe, "hex:", 4u) == 0) {
		if (length == 0u) return strcmp(recipe, "hex:-") == 0;
		return decode_hex(recipe + 4u, input, length);
	}
	if (strncmp(recipe, "repeat:", 7u) == 0) {
		uint8_t value;
		if (!decode_hex(recipe + 7u, &value, 1u)) return 0;
		memset(input, value, length);
		return 1;
	}
	return 0;
}

static int corpus_streams(
	const uint8_t *input, size_t length,
	const uint8_t expected256[32], const uint8_t expected512[64],
	const size_t *chunks, size_t chunk_count
) {
	for (size_t plan = 0; plan <= chunk_count; plan++) {
		fch256_ctx a;
		fch512_ctx b;
		fch256_init(&a);
		fch512_init(&b);
		int ok = fch256_update(&a, NULL, 0u) && fch512_update(&b, NULL, 0u);
		size_t offset = 0u, index = 0u;
		while (ok && offset < length) {
			size_t count = chunks[plan < chunk_count ? plan : index++ % chunk_count];
			if (count > length - offset) count = length - offset;
			ok = fch256_update(&a, input + offset, count) &&
				fch512_update(&b, input + offset, count);
			offset += count;
		}
		uint8_t out256[32], out512[64];
		ok = ok && fch256_update(&a, NULL, 0u) && fch512_update(&b, NULL, 0u) &&
			fch256_final_checked(&a, out256) && fch512_final_checked(&b, out512) &&
			memcmp(out256, expected256, sizeof(out256)) == 0 &&
			memcmp(out512, expected512, sizeof(out512)) == 0;
		fch256_free(&a);
		fch512_free(&b);
		if (!ok) return 0;
	}
	return 1;
}

static int corpus_aliases(
	const uint8_t *input, size_t length,
	const char *expected256, const char *expected512
) {
	uint8_t *overlap = (uint8_t *)malloc(length + 71u);
	if (!overlap) return 0;
	int ok = 1;
	for (size_t offset = 0u; ok && offset <= 7u; offset += 7u) {
		char hex[129];
		memcpy(overlap, input, length);
		ok = fch_hash_256_checked(overlap, length, overlap + offset);
		if (ok) {
			to_hex(overlap + offset, 32u, hex);
			ok = strcmp(hex, expected256) == 0;
		}
		memcpy(overlap, input, length);
		ok = ok && fch_hash_512_checked(overlap, length, overlap + offset);
		if (ok) {
			to_hex(overlap + offset, 64u, hex);
			ok = strcmp(hex, expected512) == 0;
		}
	}
	free(overlap);
	return ok;
}

static int check_corpus(const char *path) {
	FILE *file = fopen(path, "r");
	if (!file) {
		fprintf(stderr, "FAIL: cannot open KAT corpus %s\n", path);
		return 0;
	}
	char line[512];
	size_t chunks[16], chunk_count = 0u, cases = 0u;
	int ok = 1, version = 0;
	while (ok && fgets(line, sizeof(line), file)) {
		if (!strchr(line, '\n') && !feof(file)) { ok = 0; break; }
		line[strcspn(line, "\r\n")] = '\0';
		if (strcmp(line, "# FCH interoperability corpus v1; tree=2; padding=1; rounds=16; state_bits=512") == 0) {
			version = 1;
			continue;
		}
		const char *prefix = "# streaming-chunks: ";
		if (strncmp(line, prefix, strlen(prefix)) == 0) {
			char *token = strtok(line + strlen(prefix), ",\r\n");
			while (token) {
				char *end;
				unsigned long value = strtoul(token, &end, 10);
				if (!value || value > 65536u || *end || chunk_count == 16u) { ok = 0; break; }
				chunks[chunk_count++] = (size_t)value;
				token = strtok(NULL, ",\r\n");
			}
			continue;
		}
		if (line[0] == '#') continue;
		char name[64], recipe[128], hex256[65], hex512[129], extra;
		size_t length;
		uint8_t input[32768], expected256[32], expected512[64];
		if (!version || !chunk_count ||
			sscanf(line, "%63s %zu %127s %64s %128s %c", name, &length, recipe, hex256, hex512, &extra) != 5 ||
			length > sizeof(input) || !corpus_input(recipe, input, length) ||
			!decode_hex(hex256, expected256, sizeof(expected256)) ||
			!decode_hex(hex512, expected512, sizeof(expected512))) { ok = 0; break; }
		ok = check_256(input, length, hex256) && check_512(input, length, hex512) &&
			corpus_streams(input, length, expected256, expected512, chunks, chunk_count) &&
			corpus_aliases(input, length, hex256, hex512);
		if (!ok) fprintf(stderr, "FAIL: KAT case %s\n", name);
		cases++;
	}
	if (ferror(file) || cases != 54u || chunk_count != 10u) ok = 0;
	fclose(file);
	if (ok) printf("PASS: canonical KAT corpus (%zu cases, %zu C digests, 11 streaming plans, 2 overlap offsets)\n",
		cases, cases * (chunk_count + 4u) * 2u);
	else fprintf(stderr, "FAIL: invalid or mismatching KAT corpus\n");
	return ok;
}

int main(int argc, char **argv) {
	if (argc > 2) return 2;
	int ok = check_determinism();
	ok &= check_corpus(argc == 2 ? argv[1] : "../analysis/interoperability-v1.tsv");

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
		printf("PASS: fixed test vectors and deterministic hashing\n");
		return 0;
	}

	return 1;
}
