#include <stdio.h>
#include <string.h>

#include "fch.h"

int main(void) {
    const char *msg = "fractal-crypt-hash";
    uint8_t out1[32], out2[32];

    fch_hash_256(
        (const uint8_t *)msg,
        strlen(msg),
        out1
    );

    fch_hash_256(
        (const uint8_t *)msg,
        strlen(msg),
        out2
    );

    if (memcmp(out1, out2, 32) != 0) {
        printf("FAIL: inconsistent hash output\n");
        return 1;
    }

    printf("PASS: consistent hash output\n");
    return 0;
}
