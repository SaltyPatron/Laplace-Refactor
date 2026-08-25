#ifndef LAPLACE_TEST_UNICODE_SOURCE_FIXTURE_MANIFEST_H
#define LAPLACE_TEST_UNICODE_SOURCE_FIXTURE_MANIFEST_H

#include <stdint.h>

typedef struct laplace_unicode_generated_source_file {
    const char* relative_path;
    const char* version_marker;
    uint64_t expected_bytes;
    uint8_t expected_sha256[32];
} laplace_unicode_generated_source_file;

#define LAPLACE_UNICODE_GENERATED_SOURCE_COUNT 1u
#define LAPLACE_UNICODE_GENERATED_VERSION "fixture-v1"

static const laplace_unicode_generated_source_file
laplace_unicode_generated_sources[LAPLACE_UNICODE_GENERATED_SOURCE_COUNT] = {
    {"fixture.txt", "", UINT64_C(3),
     {0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
      0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
      0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
      0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu}}
};

typedef struct laplace_unicode_generated_contract_file {
    const char* name;
    uint8_t sha256[32];
} laplace_unicode_generated_contract_file;

#define LAPLACE_UNICODE_GENERATED_CONTRACT_COUNT 1u
static const laplace_unicode_generated_contract_file
laplace_unicode_generated_contracts[LAPLACE_UNICODE_GENERATED_CONTRACT_COUNT] = {
    {"fixture-contract", {0u}}
};

#endif
