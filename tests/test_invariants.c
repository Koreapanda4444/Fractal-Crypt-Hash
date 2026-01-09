#include <stdio.h>

#include "fractal.h"
#include "params.h"

int main(void) {
    uint8_t data[512];
    for (int i = 0; i < 512; i++)
        data[i] = (uint8_t)i;

    fch_block_t blocks[FCH_N_MAX];
    size_t n = fch_fractal_split(data, 512, 0, blocks, FCH_N_MAX);

    size_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += blocks[i].length;
    }

    if (sum != 512) {
        printf("FAIL: split does not cover full input\n");
        return 1;
    }

    printf("PASS: split covers full input\n");
    return 0;
}
