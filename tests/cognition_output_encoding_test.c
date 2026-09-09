#include "../engine/src/cognition_output_encoding.h"

#include <inttypes.h>
#include <stdio.h>

int main(void) {
    for (uint32_t position = 0; position <= UINT32_C(0x10ffff); ++position) {
        uint8_t output[4] = {0xa5, 0xa5, 0xa5, 0xa5};
        size_t count = 99;
        const int ok = laplace_cognition_encode_octet_position(
            position, output, &count);
        if (position <= 255) {
            if (ok != 1 || count != 1 || output[0] != position ||
                output[1] != 0xa5 || output[2] != 0xa5 || output[3] != 0xa5) {
                fprintf(stderr, "octet preservation failure position=%" PRIu32 "\n", position);
                return 1;
            }
        } else if (ok != 0 || count != 0 || output[0] != 0xa5 ||
                   output[1] != 0xa5 || output[2] != 0xa5 || output[3] != 0xa5) {
            fprintf(stderr, "octet boundary rejection failure position=%" PRIu32 "\n", position);
            return 1;
        }
    }
    uint8_t output[4] = {0xa5, 0xa5, 0xa5, 0xa5};
    size_t count = 99;
    if (laplace_cognition_encode_octet_position(UINT32_MAX, output, &count) != 0 ||
        count != 0 || output[0] != 0xa5 ||
        laplace_cognition_encode_octet_position(0, NULL, &count) != 0 ||
        count != 0 ||
        laplace_cognition_encode_octet_position(0, output, NULL) != 0 ||
        output[0] != 0xa5) {
        fputs("octet argument rejection failure\n", stderr);
        return 1;
    }
    puts("PASS: 1114112 Unicode positions; 256 exact octets; no publication on rejection");
    return 0;
}
