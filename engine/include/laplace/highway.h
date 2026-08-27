#ifndef LAPLACE_HIGHWAY_H
#define LAPLACE_HIGHWAY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/highway.h"
#include "laplace/composition.h"
#include "laplace/export.h"
#include "laplace/framework.h"
#include "laplace/identity.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_highway_key {
    uint32_t kind;
    uint32_t reserved;
    laplace_id128 authority;
    laplace_id128 release;
    laplace_id128 name_space;
    laplace_id128 local_identifier;
    uint64_t version;
} laplace_highway_key;

typedef struct laplace_highway_coordinate {
    laplace_id128 coordinate;
    laplace_digest256 collision_fingerprint;
    uint32_t kind;
    uint32_t reserved;
    uint64_t version;
} laplace_highway_coordinate;

typedef struct laplace_highway_kind_contract_row {
    uint32_t id;
    const char* name;
    uint64_t introduced;
    uint64_t retired;
} laplace_highway_kind_contract_row;

typedef struct laplace_highway_alias_contract_row {
    uint32_t kind_id;
    const char* name;
    uint64_t introduced;
    uint64_t retired;
} laplace_highway_alias_contract_row;

typedef struct laplace_highway_disposition_contract_row {
    uint32_t id;
    const char* name;
} laplace_highway_disposition_contract_row;

typedef struct laplace_highway_registry_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 registry_fingerprint;
    laplace_id128 activation_epoch_id;
    laplace_digest256 activation_epoch_fingerprint;
    uint64_t registry_version;
    uint64_t kind_count;
    uint64_t alias_count;
    uint64_t disposition_count;
    uint32_t status;
    uint32_t reserved;
} laplace_highway_registry_receipt;

typedef enum laplace_highway_status {
    LAPLACE_HIGHWAY_OK = 0,
    LAPLACE_HIGHWAY_INVALID_ARGUMENT = 1,
    LAPLACE_HIGHWAY_UNKNOWN_KIND = 2,
    LAPLACE_HIGHWAY_ZERO_SCOPE = 3,
    LAPLACE_HIGHWAY_ZERO_VERSION = 4,
    LAPLACE_HIGHWAY_COUNT_OVERFLOW = 5,
    LAPLACE_HIGHWAY_ZERO_COORDINATE = 6,
    LAPLACE_HIGHWAY_REGISTRY_INVALID = 7,
    LAPLACE_HIGHWAY_CONTEXT_INVALID = 8,
    LAPLACE_HIGHWAY_MEMORY_FAILURE = 9,
    LAPLACE_HIGHWAY_UTF8_INVALID = 10
} laplace_highway_status;

typedef struct laplace_highway_registry_ast_plan
    laplace_highway_registry_ast_plan;

typedef struct laplace_highway_registry_ast_view {
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    const uint32_t* atom_positions;
    const laplace_composition_operand* operands;
    const laplace_composition_request* requests;
    const uint64_t* kind_name_result_indexes;
    const uint64_t* alias_name_result_indexes;
    const uint64_t* disposition_name_result_indexes;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t root_result_index;
    uint64_t kind_count;
    uint64_t alias_count;
    uint64_t disposition_count;
    uint32_t recipe_version;
    uint32_t reserved;
} laplace_highway_registry_ast_view;

LAPLACE_API laplace_highway_status laplace_highway_coordinate_calculate(
    const laplace_highway_key* key,
    laplace_highway_coordinate* output);

LAPLACE_API laplace_highway_status laplace_highway_coordinate_calculate_batch(
    const laplace_highway_key* keys,
    size_t key_count,
    laplace_highway_coordinate* outputs);

LAPLACE_API int laplace_highway_kind_valid(uint32_t kind);

LAPLACE_API const laplace_highway_kind_contract_row*
laplace_highway_registry_kinds(size_t* count);

LAPLACE_API const laplace_highway_alias_contract_row*
laplace_highway_registry_aliases(size_t* count);

LAPLACE_API const laplace_highway_disposition_contract_row*
laplace_highway_registry_dispositions(size_t* count);

LAPLACE_API laplace_highway_status laplace_highway_registry_materialize(
    const laplace_framework_context* context,
    laplace_highway_registry_receipt* receipt);

LAPLACE_API laplace_highway_status laplace_highway_registry_ast_plan_create(
    const laplace_digest256* geometry_epoch,
    const laplace_digest256* occurrence_context_fingerprint,
    laplace_highway_registry_ast_plan** plan);

LAPLACE_API laplace_highway_status laplace_highway_registry_ast_plan_view(
    const laplace_highway_registry_ast_plan* plan,
    laplace_highway_registry_ast_view* view);

LAPLACE_API void laplace_highway_registry_ast_plan_destroy(
    laplace_highway_registry_ast_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
