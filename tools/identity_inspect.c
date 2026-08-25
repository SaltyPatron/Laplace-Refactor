#include "laplace/identity.h"

#include <inttypes.h>
#include <stdio.h>

static void print_id(const char* label, const laplace_id128* id) {
    size_t index;
    printf("%s=", label);
    for (index = 0; index < LAPLACE_IDENTITY_BYTES; ++index) {
        printf("%02x", (unsigned int)id->bytes[index]);
    }
    putchar('\n');
}

int main(void) {
    laplace_id128 two;
    laplace_id128 five;
    laplace_id128 surrogate;
    laplace_id128 numeric_255;
    laplace_id128 children[3];

    if (laplace_identity_codepoint((uint32_t)'2', &two) != LAPLACE_IDENTITY_OK ||
        laplace_identity_codepoint((uint32_t)'5', &five) != LAPLACE_IDENTITY_OK ||
        laplace_identity_codepoint(UINT32_C(0xd800), &surrogate) != LAPLACE_IDENTITY_OK) {
        return 1;
    }
    children[0] = two;
    children[1] = five;
    children[2] = five;
    if (laplace_identity_composite(children, 3u, &numeric_255) != LAPLACE_IDENTITY_OK) {
        return 1;
    }

    print_id("U+0032", &two);
    print_id("U+0035", &five);
    print_id("U+D800", &surrogate);
    print_id("sequence-2-5-5", &numeric_255);
    return 0;
}
