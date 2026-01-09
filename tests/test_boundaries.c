#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fch.h"

int main(void) {
    uint8_t out[32];

    fch_hash_256(NULL, 0, out);

    uint8_t buf[64];
    memset(buf, 0x11, 64);
    fch_hash_256(buf, 64, out);

    size_t big = 1 << 20;
    uint8_t *large = (uint8_t *)malloc(big);
    if (!large) {
        printf("FAIL: malloc failed\n");
        return 1;
    }
    memset(large, 0x22, big);
    fch_hash_256(large, big, out);
    free(large);

    printf("PASS: boundary inputs handled\n");
    return 0;
}
