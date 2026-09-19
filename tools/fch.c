#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "fch.h"
#include "fch_stream.h"
#include "fch_cli.h"

static int prepare_stdin(FILE *input, FILE *error) {
#ifdef _WIN32
	if (_setmode(_fileno(input), _O_BINARY) == -1) {
		fprintf(error, "fch: cannot set stdin to binary mode\n");
		return 0;
	}
#else
	(void)input;
	(void)error;
#endif

	return 1;
}

static int print_hex(FILE *output, const uint8_t *buf, size_t len) {
	static const char hexdigits[] = "0123456789abcdef";
	for (size_t i = 0; i < len; i++) {
		uint8_t b = buf[i];
		if (fputc(hexdigits[b >> 4], output) == EOF ||
			fputc(hexdigits[b & 0x0F], output) == EOF)
			return 0;
	}

	return 1;
}

static int write_digest(
	FILE *output,
	FILE *error,
	const uint8_t *buf,
	size_t len,
	const char *label
) {
	if (!print_hex(output, buf, len))
		goto fail;
	if (label && fprintf(output, "  %s", label) < 0)
		goto fail;
	if (fputc('\n', output) == EOF || fflush(output) == EOF)
		goto fail;

	return 1;

fail:
	fprintf(error, "fch: failed to write output\n");
	return 0;
}

static int hash_stream(
	FILE *fp,
	FILE *output,
	FILE *error,
	int variant,
	const char *label
) {
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
				fprintf(error, "fch: failed to buffer %s\n", label);
				if (variant == 256)
					fch256_free(&ctx256);
				else
					fch512_free(&ctx512);
				return 2;
			}
		}

		if (count < sizeof(buffer)) {
			if (ferror(fp)) {
				fprintf(error, "fch: failed to read %s\n", label);
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
			fprintf(error, "fch: failed to hash %s\n", label);
			fch256_free(&ctx256);
			return 2;
		}
		fch256_free(&ctx256);
		if (!write_digest(output, error, out, sizeof(out), label))
			return 2;
	} else {
		uint8_t out[64];
		if (!fch512_final_checked(&ctx512, out)) {
			fprintf(error, "fch: failed to hash %s\n", label);
			fch512_free(&ctx512);
			return 2;
		}
		fch512_free(&ctx512);
		if (!write_digest(output, error, out, sizeof(out), label))
			return 2;
	}

	return 0;
}

static void usage(FILE *error, const char *argv0) {
	fprintf(error,
		"Usage: %s [-256|-512] [FILE...]\n"
		"  If no FILE is given, reads from stdin.\n",
		argv0);
}

int fch_cli_run(
	int argc,
	char **argv,
	FILE *input,
	FILE *output,
	FILE *error
) {
	if (argc < 1 || !argv || !argv[0] || !input || !output || !error)
		return 2;

	int variant = 256;
	int argi = 1;

	if (argi < argc && (strcmp(argv[argi], "-256") == 0 || strcmp(argv[argi], "--256") == 0)) {
		variant = 256;
		argi++;
	} else if (argi < argc && (strcmp(argv[argi], "-512") == 0 || strcmp(argv[argi], "--512") == 0)) {
		variant = 512;
		argi++;
	} else if (argi < argc && (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0)) {
		usage(error, argv[0]);
		return 0;
	}

	if (argi >= argc) {
		if (!prepare_stdin(input, error))
			return 2;
		return hash_stream(input, output, error, variant, "-");
	}

	int exit_code = 0;
	for (int i = argi; i < argc; i++) {
		const char *path = argv[i];
		FILE *fp = fopen(path, "rb");
		if (!fp) {
			fprintf(error, "fch: cannot open %s\n", path);
			exit_code = 2;
			continue;
		}
		int rc = hash_stream(fp, output, error, variant, path);
		fclose(fp);
		if (rc != 0) exit_code = rc;
	}

	return exit_code;
}

#ifndef FCH_CLI_NO_MAIN
int main(int argc, char **argv) {
	return fch_cli_run(argc, argv, stdin, stdout, stderr);
}
#endif
