#ifndef LAPLACE_DECOMPOSITION_XML_H
#define LAPLACE_DECOMPOSITION_XML_H

#include <stdint.h>

#include "laplace/decomposition.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic XML 1.x structural decomposition provider.
 *
 * This provider owns no Unicode, UCD, corpus, or domain semantics. It preserves
 * exact XML byte ranges and structural parentage so source recipes can map those
 * observations into the universal AST. Element/attribute names and values remain
 * exact source content; text and attribute values are redispatched to other
 * decomposition providers when the enclosing recipe authorizes them.
 */
typedef struct laplace_decomposition_xml_provider {
    uint64_t kind_base;
    uint64_t document_kind;
    uint64_t element_kind;
    uint64_t start_tag_kind;
    uint64_t end_tag_kind;
    uint64_t attribute_kind;
    uint64_t attribute_name_kind;
    uint64_t attribute_value_kind;
    uint64_t text_kind;
    uint64_t comment_kind;
    uint64_t cdata_kind;
    uint64_t processing_instruction_kind;
    uint64_t doctype_kind;
    uint64_t element_name_kind;
    uint64_t cdata_text_kind;
    uint64_t bom_kind;
    laplace_decomposition_provider_v1 provider;
} laplace_decomposition_xml_provider;

enum {
    LAPLACE_DECOMPOSITION_XML_KIND_COUNT = 16,
    LAPLACE_DECOMPOSITION_XML_FIELD_ELEMENT_NAME = 1,
    LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE = 2,
    LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE_NAME = 3,
    LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE_VALUE = 4,
    LAPLACE_DECOMPOSITION_XML_FIELD_CONTENT = 5,
    LAPLACE_DECOMPOSITION_XML_FIELD_END_NAME = 6
};

LAPLACE_API laplace_decomposition_status laplace_decomposition_xml_provider_init(
    laplace_decomposition_xml_provider* storage,
    uint64_t kind_base,
    const laplace_digest256* provider_fingerprint);

#ifdef __cplusplus
}
#endif

#endif
