#ifndef LAPLACE_UCDXML_PROJECTION_H
#define LAPLACE_UCDXML_PROJECTION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/decomposition.h"
#include "laplace/decomposition_xml.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_ucdxml_projection laplace_ucdxml_projection;

enum {
    LAPLACE_UCDXML_PROPERTY_INHERITED_FROM_GROUP = UINT32_C(1),
    LAPLACE_UCDXML_PROPERTY_RANGE_DECLARATION = UINT32_C(2),
    LAPLACE_UCDXML_PROPERTY_HASH_SUBSTITUTION = UINT32_C(4),
    LAPLACE_UCDXML_PROPERTY_KNOWN_FLAGS =
        LAPLACE_UCDXML_PROPERTY_INHERITED_FROM_GROUP |
        LAPLACE_UCDXML_PROPERTY_RANGE_DECLARATION |
        LAPLACE_UCDXML_PROPERTY_HASH_SUBSTITUTION
};

typedef struct laplace_ucdxml_projection_input {
    const laplace_decomposition_content* content;
    const laplace_decomposition_result* decomposition;
    const laplace_decomposition_xml_provider* xml_provider;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    uint32_t flags;
    uint32_t reserved;
} laplace_ucdxml_projection_input;

typedef struct laplace_ucdxml_property_view {
    laplace_digest256 observation_fingerprint;
    uint64_t element_span_index;
    uint64_t declaration_element_span_index;
    uint64_t property_name_byte_start;
    uint64_t property_name_byte_end;
    uint64_t property_value_byte_start;
    uint64_t property_value_byte_end;
    uint32_t codepoint_position;
    uint32_t range_first;
    uint32_t range_last;
    uint32_t flags;
} laplace_ucdxml_property_view;

typedef struct laplace_ucdxml_projection_summary {
    laplace_digest256 projection_fingerprint;
    uint64_t codepoint_declaration_count;
    uint64_t group_count;
    uint64_t source_attribute_count;
    uint32_t covered_position_count;
    uint32_t status;
} laplace_ucdxml_projection_summary;

typedef enum laplace_ucdxml_status {
    LAPLACE_UCDXML_OK = 0,
    LAPLACE_UCDXML_INVALID_ARGUMENT = 1,
    LAPLACE_UCDXML_DECOMPOSITION_INVALID = 2,
    LAPLACE_UCDXML_STRUCTURE_INVALID = 3,
    LAPLACE_UCDXML_RANGE_INVALID = 4,
    LAPLACE_UCDXML_PROPERTY_NOT_FOUND = 5,
    LAPLACE_UCDXML_PROPERTY_RANGE = 6,
    LAPLACE_UCDXML_CAPACITY_INSUFFICIENT = 7,
    LAPLACE_UCDXML_VALUE_INVALID = 8,
    LAPLACE_UCDXML_MEMORY_FAILURE = 9
} laplace_ucdxml_status;

LAPLACE_API laplace_ucdxml_status laplace_ucdxml_projection_create(
    const laplace_ucdxml_projection_input* input,
    laplace_ucdxml_projection** projection,
    laplace_ucdxml_projection_summary* summary);

LAPLACE_API void laplace_ucdxml_projection_destroy(
    laplace_ucdxml_projection** projection);

LAPLACE_API laplace_ucdxml_status laplace_ucdxml_property_count(
    const laplace_ucdxml_projection* projection,
    uint32_t codepoint_position,
    size_t* property_count);

LAPLACE_API laplace_ucdxml_status laplace_ucdxml_property(
    const laplace_ucdxml_projection* projection,
    uint32_t codepoint_position,
    size_t property_index,
    laplace_ucdxml_property_view* property);

LAPLACE_API laplace_ucdxml_status laplace_ucdxml_property_find(
    const laplace_ucdxml_projection* projection,
    uint32_t codepoint_position,
    const uint8_t* property_name,
    size_t property_name_bytes,
    laplace_ucdxml_property_view* property);

LAPLACE_API laplace_ucdxml_status laplace_ucdxml_property_name_source(
    const laplace_ucdxml_projection* projection,
    const laplace_ucdxml_property_view* property,
    const uint8_t** bytes,
    size_t* byte_count);

LAPLACE_API laplace_ucdxml_status laplace_ucdxml_property_value(
    const laplace_ucdxml_projection* projection,
    const laplace_ucdxml_property_view* property,
    uint8_t* output,
    size_t output_capacity,
    size_t* required_bytes);

#ifdef __cplusplus
}
#endif

#endif
