#ifndef LAPLACE_EXECUTION_H
#define LAPLACE_EXECUTION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/execution.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAPLACE_EXECUTION_UNKNOWN_ID UINT32_MAX

typedef struct laplace_execution_processor {
    uint32_t logical_id;
    uint32_t package_id;
    uint32_t core_id;
    uint32_t memory_domain_id;
    uint64_t maximum_frequency_hz;
    uint32_t core_kind;
    uint32_t flags;
} laplace_execution_processor;

typedef struct laplace_execution_cache {
    uint64_t size_bytes;
    uint32_t level;
    uint32_t kind;
    uint32_t line_bytes;
    uint32_t processor_id_offset;
    uint32_t processor_id_count;
    uint32_t reserved;
} laplace_execution_cache;

typedef struct laplace_execution_memory_domain {
    uint32_t domain_id;
    uint32_t processor_count;
    uint64_t total_bytes;
    uint64_t usable_bytes;
} laplace_execution_memory_domain;

typedef struct laplace_execution_topology {
    laplace_execution_processor* processors;
    laplace_execution_cache* caches;
    uint32_t* cache_processor_ids;
    laplace_execution_memory_domain* memory_domains;
    uint32_t processor_count;
    uint32_t processor_capacity;
    uint32_t cache_count;
    uint32_t cache_capacity;
    uint32_t cache_processor_id_count;
    uint32_t cache_processor_id_capacity;
    uint32_t memory_domain_count;
    uint32_t memory_domain_capacity;
    uint64_t total_memory_bytes;
    uint64_t usable_memory_bytes;
    uint64_t page_bytes;
    uint64_t isa_flags;
    uint32_t flags;
    uint32_t reserved;
} laplace_execution_topology;

typedef struct laplace_execution_topology_size {
    uint32_t processor_count;
    uint32_t cache_count;
    uint32_t cache_processor_id_count;
    uint32_t memory_domain_count;
} laplace_execution_topology_size;

typedef struct laplace_execution_external_ownership {
    uint64_t externally_owned_memory_bytes;
    uint32_t externally_owned_cpu_slots;
    uint32_t available_io_slots;
} laplace_execution_external_ownership;

typedef struct laplace_execution_grant {
    uint64_t memory_bytes;
    uint32_t cpu_slots;
    uint32_t io_slots;
} laplace_execution_grant;

typedef struct laplace_execution_partition_request {
    uint64_t minimum_memory_bytes;
    uint16_t memory_weight;
    uint16_t cpu_weight;
    uint16_t io_weight;
    uint16_t minimum_cpu_slots;
    uint16_t minimum_io_slots;
    uint16_t reserved;
} laplace_execution_partition_request;

typedef struct laplace_execution_work_request {
    uint64_t item_count;
    uint64_t resident_memory_bytes;
    uint64_t memory_bytes_per_item;
    uint64_t minimum_chunk_items;
    uint32_t outer_worker_limit;
    uint32_t inner_threads_per_worker;
    uint32_t required_io_slots;
    uint32_t reserved;
} laplace_execution_work_request;

typedef struct laplace_execution_work_plan {
    uint64_t chunk_items;
    uint64_t chunk_count;
    uint64_t peak_memory_bytes;
    uint32_t outer_workers;
    uint32_t inner_threads_per_worker;
    uint32_t io_slots;
    uint32_t reserved;
} laplace_execution_work_plan;

typedef enum laplace_execution_status {
    LAPLACE_EXECUTION_OK = 0,
    LAPLACE_EXECUTION_INVALID_ARGUMENT = 1,
    LAPLACE_EXECUTION_CAPACITY_INSUFFICIENT = 2,
    LAPLACE_EXECUTION_TOPOLOGY_INVALID = 3,
    LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT = 4,
    LAPLACE_EXECUTION_OVERFLOW = 5,
    LAPLACE_EXECUTION_OBSERVATION_FAILED = 6,
    LAPLACE_EXECUTION_PLATFORM_UNSUPPORTED = 7
} laplace_execution_status;

LAPLACE_API laplace_execution_status laplace_execution_topology_measure_host(
    laplace_execution_topology_size* required);

LAPLACE_API laplace_execution_status laplace_execution_topology_observe_host(
    laplace_execution_topology* topology);

LAPLACE_API laplace_execution_status laplace_execution_topology_validate(
    const laplace_execution_topology* topology);

LAPLACE_API laplace_execution_status laplace_execution_root_grant(
    const laplace_execution_topology* topology,
    const laplace_execution_external_ownership* external_ownership,
    laplace_execution_grant* grant);

LAPLACE_API laplace_execution_status laplace_execution_partition_grant(
    const laplace_execution_grant* parent,
    const laplace_execution_partition_request* requests,
    size_t request_count,
    laplace_execution_grant* children);

LAPLACE_API laplace_execution_status laplace_execution_plan_work(
    const laplace_execution_grant* grant,
    const laplace_execution_work_request* request,
    laplace_execution_work_plan* plan);

#ifdef __cplusplus
}
#endif

#endif
