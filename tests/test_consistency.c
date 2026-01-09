#include <stdio.h>
#include <string.h>

#include "fch.h"

int main(void) {
    const char *msgs[] = {
        "",
        "a",
        "fractal",
        "fractal-crypt-hash",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    };

    for (size_t i = 0; i < sizeof(msgs) / sizeof(msgs[0]); i++) {
        uint8_t a[32], b[32];

        fch_hash_256(
            (const uint8_t *)msgs[i],
            strlen(msgs[i]),
            a
        );
        fch_hash_256(
            (const uint8_t *)msgs[i],
            strlen(msgs[i]),
            b
        );

        if (memcmp(a, b, 32) != 0) {
            printf("FAIL: non-deterministic output\n");
            return 1;
        }
    }

    printf("PASS: deterministic behavior\n");
    return 0;
}
