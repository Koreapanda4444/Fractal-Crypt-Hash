#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"
#include "../tools/fch_cli.h"

enum {
    FCH_FUZZ_CLI_MAX_INPUT = 65536,
    FCH_FUZZ_CLI_OUTPUT_CAPACITY = 256,
    FCH_FUZZ_CLI_ERROR_CAPACITY = 512
};

static void require_or_abort(int condition) {
    if (!condition)
        abort();
}

static size_t read_stream(FILE *stream, char *output, size_t capacity) {
    require_or_abort(fflush(stream) == 0);
    require_or_abort(fseek(stream, 0, SEEK_END) == 0);
    long end = ftell(stream);
    require_or_abort(end >= 0);
    require_or_abort((size_t)end < capacity);
    rewind(stream);

    size_t length = fread(output, 1u, (size_t)end, stream);
    require_or_abort(length == (size_t)end);
    output[length] = '\0';
    return length;
}

static size_t format_digest(
    const uint8_t *digest,
    size_t digest_length,
    char *output
) {
    static const char digits[] = "0123456789abcdef";
    size_t offset = 0u;

    for (size_t i = 0u; i < digest_length; i++) {
        output[offset++] = digits[digest[i] >> 4u];
        output[offset++] = digits[digest[i] & 0x0Fu];
    }
    output[offset++] = ' ';
    output[offset++] = ' ';
    output[offset++] = '-';
    output[offset++] = '\n';
    return offset;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if ((!data && size > 0u) || size > FCH_FUZZ_CLI_MAX_INPUT)
        return 0;

    const uint8_t *payload = size > 0u ? data + 1u : NULL;
    size_t payload_length = size > 0u ? size - 1u : 0u;
    unsigned int mode = size > 0u ? data[0] % 9u : 0u;

    FILE *input = tmpfile();
    FILE *output = tmpfile();
    FILE *error = tmpfile();
    if (!input || !output || !error) {
        if (input)
            fclose(input);
        if (output)
            fclose(output);
        if (error)
            fclose(error);
        return 0;
    }

    if (payload_length > 0u) {
        require_or_abort(fwrite(
            payload,
            1u,
            payload_length,
            input
        ) == payload_length);
    }
    rewind(input);

    char *argv[3] = {"fch", NULL, NULL};
    int argc = 1;
    int variant = 256;
    int help_mode = 0;
    int missing_mode = 0;
    int invalid_mode = 0;

    switch (mode) {
    case 1u:
        argv[1] = "-256";
        argc = 2;
        break;
    case 2u:
        argv[1] = "--256";
        argc = 2;
        break;
    case 3u:
        argv[1] = "-512";
        argc = 2;
        variant = 512;
        break;
    case 4u:
        argv[1] = "--512";
        argc = 2;
        variant = 512;
        break;
    case 5u:
        argv[1] = "-h";
        argc = 2;
        help_mode = 1;
        break;
    case 6u:
        argv[1] = "--help";
        argc = 2;
        help_mode = 1;
        break;
    case 7u:
        argv[1] = "fch-fuzz-input-does-not-exist";
        argc = 2;
        missing_mode = 1;
        break;
    case 8u:
        argc = 0;
        invalid_mode = 1;
        break;
    default:
        break;
    }

    int status = fch_cli_run(argc, argv, input, output, error);
    char actual_output[FCH_FUZZ_CLI_OUTPUT_CAPACITY];
    char actual_error[FCH_FUZZ_CLI_ERROR_CAPACITY];
    size_t output_length = read_stream(
        output,
        actual_output,
        sizeof(actual_output)
    );
    size_t error_length = read_stream(
        error,
        actual_error,
        sizeof(actual_error)
    );

    if (help_mode) {
        require_or_abort(status == 0);
        require_or_abort(output_length == 0u);
        require_or_abort(error_length >= 6u);
        require_or_abort(memcmp(actual_error, "Usage:", 6u) == 0);
    } else if (missing_mode) {
        static const char prefix[] = "fch: cannot open ";
        require_or_abort(status == 2);
        require_or_abort(output_length == 0u);
        require_or_abort(error_length >= sizeof(prefix) - 1u);
        require_or_abort(memcmp(
            actual_error,
            prefix,
            sizeof(prefix) - 1u
        ) == 0);
    } else if (invalid_mode) {
        require_or_abort(status == 2);
        require_or_abort(output_length == 0u);
        require_or_abort(error_length == 0u);
    } else {
        uint8_t digest[64];
        size_t digest_length = variant == 256 ? 32u : 64u;
        int ok = variant == 256
            ? fch_hash_256_checked(payload, payload_length, digest)
            : fch_hash_512_checked(payload, payload_length, digest);
        require_or_abort(ok);

        char expected[FCH_FUZZ_CLI_OUTPUT_CAPACITY];
        size_t expected_length = format_digest(
            digest,
            digest_length,
            expected
        );
        require_or_abort(status == 0);
        require_or_abort(error_length == 0u);
        require_or_abort(output_length == expected_length);
        require_or_abort(memcmp(
            actual_output,
            expected,
            expected_length
        ) == 0);
    }

    fclose(input);
    fclose(output);
    fclose(error);
    return 0;
}
