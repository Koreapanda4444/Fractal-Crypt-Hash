#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"

int main(void) {
    uint8_t out[32];

    if (!fch_hash_256_checked(NULL, 0, out)) {
        printf("FAIL: empty input rejected\n");
        return 1;
    }

    memset(out, 0xA5, sizeof(out));
    if (fch_hash_256_checked(NULL, 1, out)) {
        printf("FAIL: invalid input accepted\n");
        return 1;
    }
    for (size_t i = 0; i < sizeof(out); i++) {
        if (out[i] != 0) {
            printf("FAIL: failure output not cleared\n");
            return 1;
        }
    }

    uint8_t buf[64];
    memset(buf, 0x11, 64);
    if (!fch_hash_256_checked(buf, 64, out)) {
        printf("FAIL: 64-byte input rejected\n");
        return 1;
    }

    size_t big = 1 << 20;
    uint8_t *large = (uint8_t *)malloc(big);
    if (!large) {
        printf("FAIL: malloc failed\n");
        return 1;
    }
    memset(large, 0x22, big);
    if (!fch_hash_256_checked(large, big, out)) {
        printf("FAIL: large input rejected\n");
        free(large);
        return 1;
    }
    free(large);

    printf("PASS: boundary inputs handled\n");
    return 0;
}
