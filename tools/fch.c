#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "fch_stream.h"

static void print_hex(const uint8_t *buf, size_t len) {
	static const char hexdigits[] = "0123456789abcdef";
	for (size_t i = 0; i < len; i++) {
		uint8_t b = buf[i];
		putchar(hexdigits[b >> 4]);
		putchar(hexdigits[b & 0x0F]);
	}
}

static int hash_stream(FILE *fp, int variant, const char *label) {
	uint8_t buffer[65536];
	fch256_ctx ctx256;
	fch512_ctx ctx512;

	if (variant == 256)
		fch256_init(&ctx256);
	else
		fch512_init(&ctx512);

	for (;;) {
		size_t count = fread(buffer, 1, sizeof(buffer), fp);
		if (count > 0) {
			int ok = variant == 256
				? fch256_update(&ctx256, buffer, count)
				: fch512_update(&ctx512, buffer, count);
			if (!ok) {
				fprintf(stderr, "fch: failed to buffer %s\n", label);
				if (variant == 256)
					fch256_free(&ctx256);
				else
					fch512_free(&ctx512);
				return 2;
			}
		}

		if (count < sizeof(buffer)) {
			if (ferror(fp)) {
				fprintf(stderr, "fch: failed to read %s\n", label);
				if (variant == 256)
					fch256_free(&ctx256);
				else
					fch512_free(&ctx512);
				return 2;
			}
			break;
		}
	}

	if (variant == 256) {
		uint8_t out[32];
		if (!fch256_final_checked(&ctx256, out)) {
			fprintf(stderr, "fch: failed to hash %s\n", label);
			fch256_free(&ctx256);
			return 2;
		}
		fch256_free(&ctx256);
		print_hex(out, sizeof(out));
	} else {
		uint8_t out[64];
		if (!fch512_final_checked(&ctx512, out)) {
			fprintf(stderr, "fch: failed to hash %s\n", label);
			fch512_free(&ctx512);
			return 2;
		}
		fch512_free(&ctx512);
		print_hex(out, sizeof(out));
	}

	if (label) {
		printf("  %s", label);
	}
	putchar('\n');

	return 0;
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
