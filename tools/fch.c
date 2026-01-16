#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static int read_all(FILE *fp, uint8_t **out, size_t *out_len) {
	*out = NULL;
	*out_len = 0;
	size_t cap = 0;

	for (;;) {
		if (*out_len == cap) {
			size_t new_cap = (cap == 0) ? 4096 : cap * 2;
			uint8_t *new_buf = (uint8_t *)realloc(*out, new_cap);
			if (!new_buf) {
				free(*out);
				*out = NULL;
				*out_len = 0;
				return 0;
			}
			*out = new_buf;
			cap = new_cap;
		}

		size_t space = cap - *out_len;
		size_t n = fread(*out + *out_len, 1, space, fp);
		*out_len += n;

		if (n == 0) {
			if (feof(fp)) return 1;
			free(*out);
			*out = NULL;
			*out_len = 0;
			return 0;
		}
	}
}

static int hash_stream(FILE *fp, int variant, const char *label) {
	uint8_t *data = NULL;
	size_t len = 0;
	if (!read_all(fp, &data, &len)) {
		fprintf(stderr, "fch: failed to read %s\n", label);
		return 2;
	}

	int rc = 0;
	if (variant == 256) {
		uint8_t out[32];
		fch_hash_256(data, len, out);
		print_hex(out, sizeof(out));
	} else {
		uint8_t out[64];
		fch_hash_512(data, len, out);
		print_hex(out, sizeof(out));
	}

	if (label) {
		printf("  %s", label);
	}
	putchar('\n');

	free(data);
	return rc;
}

static void usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s [-256|-512] [FILE...]\n"
		"  If no FILE is given, reads from stdin.\n",
		argv0);
}

int main(int argc, char **argv) {
	int variant = 256;
	int argi = 1;

	if (argi < argc && (strcmp(argv[argi], "-256") == 0 || strcmp(argv[argi], "--256") == 0)) {
		variant = 256;
		argi++;
	} else if (argi < argc && (strcmp(argv[argi], "-512") == 0 || strcmp(argv[argi], "--512") == 0)) {
		variant = 512;
		argi++;
	} else if (argi < argc && (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0)) {
		usage(argv[0]);
		return 0;
	}

	if (argi >= argc) {
		return hash_stream(stdin, variant, "-");
	}

	int exit_code = 0;
	for (int i = argi; i < argc; i++) {
		const char *path = argv[i];
		FILE *fp = fopen(path, "rb");
		if (!fp) {
			fprintf(stderr, "fch: cannot open %s\n", path);
			exit_code = 2;
			continue;
		}
		int rc = hash_stream(fp, variant, path);
		fclose(fp);
		if (rc != 0) exit_code = rc;
	}

	return exit_code;
}
