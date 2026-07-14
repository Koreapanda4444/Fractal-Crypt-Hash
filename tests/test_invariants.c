#include <stdio.h>
#include <stdint.h>

#include "fractal.h"
#include "params.h"

#define MAX_TEST_LENGTH 4096u

static void fill_pattern(uint8_t *data, size_t length, unsigned int pattern) {
    uint32_t state = 0x9E3779B9u ^ pattern;

    for (size_t i = 0; i < length; i++) {
        switch (pattern) {
            case 0:
                data[i] = 0;
                break;
            case 1:
                data[i] = 0xFF;
                break;
            case 2:
                data[i] = (uint8_t)i;
                break;
            default:
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                data[i] = (uint8_t)state;
                break;
        }
    }
}

static int check_split(const uint8_t *data, size_t length, int depth) {
    fch_block_t blocks[FCH_N_MAX];
    size_t count = fch_fractal_split(
        data,
        length,
        depth,
        blocks,
        FCH_N_MAX
    );

    if (count == 0 || count > FCH_N_MAX || count > length)
        return 0;

    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        if (blocks[i].offset != offset)
            return 0;
        if (blocks[i].length == 0)
            return 0;
        if (blocks[i].length > length - offset)
            return 0;
        offset += blocks[i].length;
    }

    return offset == length;
}

int main(void) {
    uint8_t data[MAX_TEST_LENGTH];

    for (unsigned int pattern = 0; pattern < 4; pattern++) {
        fill_pattern(data, sizeof(data), pattern);

        for (size_t length = 1; length <= sizeof(data); length++) {
            for (int depth = 0; depth < FCH_MAX_DEPTH_CAP; depth++) {
                if (!check_split(data, length, depth)) {
                    printf(
                        "FAIL: split invariant (pattern=%u length=%u depth=%d)\n",
                        pattern,
                        (unsigned int)length,
                        depth
                    );
                    return 1;
                }
            }
        }
    }

    printf("PASS: split bounds and coverage\n");
    return 0;
}
