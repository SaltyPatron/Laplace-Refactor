#include "laplace/ucdxml_projection.h"

#include <cstddef>
#include <cstdint>
#include <limits>

/*
 * Keep exact source-span access outside the private projection implementation so
 * callers never manufacture property names from aliases or Unicode-specific
 * tables.  The public property view already carries source offsets; this helper
 * validates those offsets against the caller-owned exact content that created
 * the projection and publishes only that immutable slice.
 *
 * The projection remains opaque here, so this entry point is implemented in the
 * main projection translation unit through an internal accessor below.  This
 * translation unit deliberately contains no second source/parser implementation.
 */
extern "C" laplace_ucdxml_status
laplace_ucdxml_property_name_source_internal(
    const laplace_ucdxml_projection* projection,
    const laplace_ucdxml_property_view* property,
    const uint8_t** bytes,
    size_t* byte_count);

extern "C" laplace_ucdxml_status laplace_ucdxml_property_name_source(
    const laplace_ucdxml_projection* const projection,
    const laplace_ucdxml_property_view* const property,
    const uint8_t** const bytes,
    size_t* const byte_count) {
    if (bytes != nullptr) *bytes = nullptr;
    if (byte_count != nullptr) *byte_count = 0u;
    if (projection == nullptr || property == nullptr || bytes == nullptr ||
        byte_count == nullptr ||
        property->property_name_byte_start >= property->property_name_byte_end ||
        property->property_name_byte_end >
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return LAPLACE_UCDXML_INVALID_ARGUMENT;
    }
    return laplace_ucdxml_property_name_source_internal(
        projection, property, bytes, byte_count);
}
