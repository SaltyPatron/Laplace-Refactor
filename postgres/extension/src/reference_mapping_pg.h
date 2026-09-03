#ifndef LAPLACE_REFERENCE_MAPPING_PG_H
#define LAPLACE_REFERENCE_MAPPING_PG_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/isa.h"
#include "laplace/reference_mapping.h"

typedef struct laplace_pg_mapping_persistence_measurement {
    uint64_t batch_count;
    uint64_t maximum_batch_records;
    uint64_t maximum_encoded_batch_bytes;
    uint64_t minimum_encoded_record_bytes;
} laplace_pg_mapping_persistence_measurement;

typedef struct laplace_pg_reference_mapping_execution {
    laplace_reference_mapping_receipt semantic_receipt;
    laplace_isa_receipt isa_receipt;
    laplace_pg_mapping_persistence_measurement persistence;
} laplace_pg_reference_mapping_execution;

void laplace_pg_reference_mapping_execute_and_persist(
    const laplace_framework_context* context,
    const laplace_reference_mapping_candidate* candidates,
    size_t candidate_count,
    uint64_t preferred_batch_bytes,
    laplace_reference_mapping_record* records,
    laplace_pg_reference_mapping_execution* execution);

#endif
