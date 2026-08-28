#include "laplace/evidence_lineage.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "blake3.h"

typedef struct lineage_edge {
    size_t child;
    size_t parent;
} lineage_edge;

static int digest_compare(const laplace_digest256* left, const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes));
}

static int digest_equal(const laplace_digest256* left, const laplace_digest256* right) {
    return digest_compare(left, right) == 0;
}

static int bytes_zero(const void* value, size_t count) {
    const uint8_t* bytes = (const uint8_t*)value;
    uint8_t aggregate = 0u;
    size_t index;
    for (index = 0u; index < count; ++index) {
        aggregate = (uint8_t)(aggregate | bytes[index]);
    }
    return aggregate == 0u;
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void finish_digest(blake3_hasher* hasher, laplace_digest256* digest) {
    blake3_hasher_finalize(hasher, digest->bytes, sizeof(digest->bytes));
}

static int epistemic_kind_valid(uint32_t kind) {
    return kind >= LAPLACE_EVIDENCE_KIND_OBSERVED &&
        kind <= LAPLACE_EVIDENCE_KIND_CONTRADICTION;
}

laplace_evidence_lineage_status laplace_evidence_node_identify(
    const laplace_evidence_lineage_record* node,
    laplace_digest256* node_id) {
    blake3_hasher hasher;
    if (node == NULL || node_id == NULL ||
        node->record_kind != LAPLACE_EVIDENCE_RECORD_NODE ||
        !epistemic_kind_valid(node->epistemic_kind) ||
        node->source_ordinal == 0u || node->flags != LAPLACE_EVIDENCE_FLAGS_NONE ||
        node->reserved != 0u ||
        !bytes_zero(&node->parent_node_id, sizeof(node->parent_node_id))) {
        return LAPLACE_EVIDENCE_LINEAGE_RECORD_INVALID;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_EVIDENCE_NODE_DOMAIN,
        sizeof(LAPLACE_EVIDENCE_NODE_DOMAIN) - 1u);
    blake3_hasher_update(
        &hasher, node->proposition_id.bytes, sizeof(node->proposition_id.bytes));
    blake3_hasher_update(
        &hasher, node->occurrence_id.bytes, sizeof(node->occurrence_id.bytes));
    blake3_hasher_update(&hasher, node->source_id.bytes, sizeof(node->source_id.bytes));
    blake3_hasher_update(
        &hasher, node->context_id.bytes, sizeof(node->context_id.bytes));
    hash_u64(&hasher, node->source_ordinal);
    hash_u32(&hasher, node->epistemic_kind);
    hash_u32(&hasher, node->flags);
    finish_digest(&hasher, node_id);
    return LAPLACE_EVIDENCE_LINEAGE_OK;
}

static size_t find_node(
    const laplace_evidence_lineage_record* records,
    size_t node_count,
    const laplace_digest256* node_id) {
    size_t low = 0u;
    size_t high = node_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const int order = digest_compare(&records[middle].node_id, node_id);
        if (order < 0) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low < node_count && digest_equal(&records[low].node_id, node_id)
        ? low : SIZE_MAX;
}

static int root_record_compare(const void* left_value, const void* right_value) {
    const laplace_evidence_root_record* left =
        (const laplace_evidence_root_record*)left_value;
    const laplace_evidence_root_record* right =
        (const laplace_evidence_root_record*)right_value;
    const int node_order = digest_compare(&left->node_id, &right->node_id);
    return node_order != 0 ? node_order :
        digest_compare(&left->root_node_id, &right->root_node_id);
}

static void hash_inputs(
    const laplace_evidence_lineage_record* records,
    size_t record_count,
    laplace_digest256* digest) {
    static const uint8_t domain[] = "laplace-evidence-lineage-input-v1";
    blake3_hasher hasher;
    size_t index;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    hash_u64(&hasher, (uint64_t)record_count);
    for (index = 0u; index < record_count; ++index) {
        const laplace_evidence_lineage_record* record = &records[index];
        blake3_hasher_update(&hasher, record->node_id.bytes, 32u);
        blake3_hasher_update(&hasher, record->proposition_id.bytes, 16u);
        blake3_hasher_update(&hasher, record->occurrence_id.bytes, 32u);
        blake3_hasher_update(&hasher, record->source_id.bytes, 32u);
        blake3_hasher_update(&hasher, record->context_id.bytes, 32u);
        blake3_hasher_update(&hasher, record->parent_node_id.bytes, 32u);
        hash_u64(&hasher, record->source_ordinal);
        hash_u32(&hasher, record->record_kind);
        hash_u32(&hasher, record->epistemic_kind);
        hash_u32(&hasher, record->flags);
        hash_u32(&hasher, record->reserved);
    }
    finish_digest(&hasher, digest);
}

static void hash_outputs(
    const laplace_evidence_root_record* roots,
    size_t root_count,
    laplace_digest256* digest) {
    static const uint8_t domain[] = "laplace-evidence-lineage-output-v1";
    blake3_hasher hasher;
    size_t index;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    hash_u64(&hasher, (uint64_t)root_count);
    for (index = 0u; index < root_count; ++index) {
        blake3_hasher_update(&hasher, roots[index].node_id.bytes, 32u);
        blake3_hasher_update(&hasher, roots[index].root_node_id.bytes, 32u);
        blake3_hasher_update(&hasher, roots[index].proposition_id.bytes, 16u);
        hash_u64(&hasher, roots[index].path_depth);
        hash_u32(&hasher, roots[index].root_epistemic_kind);
        hash_u32(&hasher, roots[index].flags);
    }
    finish_digest(&hasher, digest);
}

static void finish_receipt(laplace_evidence_lineage_receipt* receipt) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_EVIDENCE_RECEIPT_DOMAIN,
        sizeof(LAPLACE_EVIDENCE_RECEIPT_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, receipt->input_fingerprint.bytes, 32u);
    blake3_hasher_update(&hasher, receipt->output_fingerprint.bytes, 32u);
    hash_u64(&hasher, receipt->input_record_count);
    hash_u64(&hasher, receipt->node_count);
    hash_u64(&hasher, receipt->edge_count);
    hash_u64(&hasher, receipt->root_relation_count);
    hash_u32(&hasher, receipt->version);
    hash_u32(&hasher, receipt->status);
    finish_digest(&hasher, &receipt->receipt_id);
}

static int checked_add_size(size_t* total, size_t count, size_t element_size) {
    if (count != 0u && element_size > SIZE_MAX / count) {
        return 0;
    }
    if (*total > SIZE_MAX - count * element_size) {
        return 0;
    }
    *total += count * element_size;
    return 1;
}

static void set_error(laplace_evidence_lineage_error* error, size_t record_index) {
    if (error != NULL) {
        error->record_index = (uint64_t)record_index;
    }
}

static laplace_evidence_lineage_status copy_cycle_path(
    const laplace_evidence_lineage_record* records,
    const size_t* stack,
    size_t stack_count,
    size_t repeated,
    laplace_evidence_lineage_error* error) {
    size_t begin = 0u;
    size_t path_count;
    size_t index;
    while (begin < stack_count && stack[begin] != repeated) {
        ++begin;
    }
    path_count = stack_count - begin + 1u;
    if (error != NULL) {
        error->cycle_path_count = (uint64_t)path_count;
        if (error->cycle_path != NULL && error->cycle_path_capacity >= path_count) {
            for (index = 0u; index + 1u < path_count; ++index) {
                error->cycle_path[index] = records[stack[begin + index]].node_id;
            }
            error->cycle_path[path_count - 1u] = records[repeated].node_id;
        }
    }
    return LAPLACE_EVIDENCE_LINEAGE_CYCLE;
}

laplace_evidence_lineage_status laplace_evidence_record_lineage_batch(
    const laplace_evidence_lineage_record* records,
    size_t record_count,
    uint64_t memory_limit_bytes,
    laplace_evidence_root_record* roots,
    size_t root_capacity,
    size_t* root_count,
    laplace_evidence_lineage_receipt* receipt,
    laplace_evidence_lineage_error* error) {
    size_t node_count = 0u;
    size_t edge_count;
    size_t allocation_bytes = 0u;
    size_t index;
    size_t produced = 0u;
    size_t processed_count = 0u;
    lineage_edge* edges = NULL;
    size_t* dependency_count = NULL;
    size_t* child_offsets = NULL;
    size_t* parent_counts = NULL;
    size_t* parent_offsets = NULL;
    size_t* parent_cursor = NULL;
    size_t* children = NULL;
    size_t* queue = NULL;
    uint8_t* processed = NULL;
    size_t* node_heads = NULL;
    size_t* root_next = NULL;
    laplace_evidence_root_record* temporary_roots = NULL;
    uint8_t* colors = NULL;
    size_t* stack = NULL;
    size_t* stack_edge_cursor = NULL;
    laplace_evidence_lineage_status status = LAPLACE_EVIDENCE_LINEAGE_OK;

    if (root_count != NULL) {
        *root_count = 0u;
    }
    if (receipt != NULL) {
        memset(receipt, 0, sizeof(*receipt));
        receipt->version = LAPLACE_EVIDENCE_LINEAGE_VERSION;
    }
    if (error != NULL) {
        error->cycle_path_count = 0u;
        error->record_index = UINT64_MAX;
    }
    if (records == NULL || record_count == 0u || roots == NULL ||
        root_capacity == 0u || root_count == NULL || receipt == NULL) {
        return LAPLACE_EVIDENCE_LINEAGE_INVALID_ARGUMENT;
    }
    while (node_count < record_count &&
           records[node_count].record_kind == LAPLACE_EVIDENCE_RECORD_NODE) {
        laplace_digest256 expected;
        if (laplace_evidence_node_identify(&records[node_count], &expected) !=
            LAPLACE_EVIDENCE_LINEAGE_OK) {
            set_error(error, node_count);
            return LAPLACE_EVIDENCE_LINEAGE_RECORD_INVALID;
        }
        if (!digest_equal(&expected, &records[node_count].node_id)) {
            set_error(error, node_count);
            return LAPLACE_EVIDENCE_LINEAGE_IDENTITY_MISMATCH;
        }
        if (node_count != 0u && digest_compare(
                &records[node_count - 1u].node_id,
                &records[node_count].node_id) >= 0) {
            set_error(error, node_count);
            return LAPLACE_EVIDENCE_LINEAGE_ORDER_INVALID;
        }
        ++node_count;
    }
    if (node_count == 0u) {
        return LAPLACE_EVIDENCE_LINEAGE_RECORD_INVALID;
    }
    edge_count = record_count - node_count;
    if (!checked_add_size(&allocation_bytes, edge_count, sizeof(*edges)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*dependency_count)) ||
        !checked_add_size(&allocation_bytes, node_count + 1u, sizeof(*child_offsets)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*parent_counts)) ||
        !checked_add_size(&allocation_bytes, node_count + 1u, sizeof(*parent_offsets)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*parent_cursor)) ||
        !checked_add_size(&allocation_bytes, edge_count, sizeof(*children)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*queue)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*processed)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*node_heads)) ||
        !checked_add_size(&allocation_bytes, root_capacity, sizeof(*root_next)) ||
        !checked_add_size(&allocation_bytes, root_capacity, sizeof(*temporary_roots)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*colors)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*stack)) ||
        !checked_add_size(&allocation_bytes, node_count, sizeof(*stack_edge_cursor))) {
        return LAPLACE_EVIDENCE_LINEAGE_OVERFLOW;
    }
    if ((uint64_t)allocation_bytes > memory_limit_bytes) {
        return LAPLACE_EVIDENCE_LINEAGE_RESOURCE_INSUFFICIENT;
    }
    edges = (lineage_edge*)calloc(edge_count == 0u ? 1u : edge_count, sizeof(*edges));
    dependency_count = (size_t*)calloc(node_count, sizeof(*dependency_count));
    child_offsets = (size_t*)calloc(node_count + 1u, sizeof(*child_offsets));
    parent_counts = (size_t*)calloc(node_count, sizeof(*parent_counts));
    parent_offsets = (size_t*)calloc(node_count + 1u, sizeof(*parent_offsets));
    parent_cursor = (size_t*)calloc(node_count, sizeof(*parent_cursor));
    children = (size_t*)calloc(edge_count == 0u ? 1u : edge_count, sizeof(*children));
    queue = (size_t*)calloc(node_count, sizeof(*queue));
    processed = (uint8_t*)calloc(node_count, sizeof(*processed));
    node_heads = (size_t*)malloc(node_count * sizeof(*node_heads));
    root_next = (size_t*)malloc(root_capacity * sizeof(*root_next));
    temporary_roots = (laplace_evidence_root_record*)calloc(
        root_capacity, sizeof(*temporary_roots));
    colors = (uint8_t*)calloc(node_count, sizeof(*colors));
    stack = (size_t*)calloc(node_count, sizeof(*stack));
    stack_edge_cursor = (size_t*)calloc(node_count, sizeof(*stack_edge_cursor));
    if (edges == NULL || dependency_count == NULL || child_offsets == NULL ||
        parent_counts == NULL ||
        parent_offsets == NULL || parent_cursor == NULL || children == NULL ||
        queue == NULL || processed == NULL || node_heads == NULL ||
        root_next == NULL || temporary_roots == NULL || colors == NULL ||
        stack == NULL || stack_edge_cursor == NULL) {
        status = LAPLACE_EVIDENCE_LINEAGE_RESOURCE_INSUFFICIENT;
        goto cleanup;
    }
    for (index = 0u; index < node_count; ++index) {
        node_heads[index] = SIZE_MAX;
    }
    for (index = 0u; index < edge_count; ++index) {
        const size_t record_index = node_count + index;
        const laplace_evidence_lineage_record* edge = &records[record_index];
        const size_t child = find_node(records, node_count, &edge->node_id);
        const size_t parent = find_node(records, node_count, &edge->parent_node_id);
        if (edge->record_kind != LAPLACE_EVIDENCE_RECORD_DEPENDENCE_EDGE ||
            edge->epistemic_kind != 0u || edge->source_ordinal != 0u ||
            edge->flags != 0u || edge->reserved != 0u ||
            !bytes_zero(&edge->proposition_id, sizeof(edge->proposition_id)) ||
            !bytes_zero(&edge->occurrence_id, sizeof(edge->occurrence_id)) ||
            !bytes_zero(&edge->source_id, sizeof(edge->source_id)) ||
            !bytes_zero(&edge->context_id, sizeof(edge->context_id))) {
            set_error(error, record_index);
            status = LAPLACE_EVIDENCE_LINEAGE_RECORD_INVALID;
            goto cleanup;
        }
        if (child == SIZE_MAX || parent == SIZE_MAX) {
            set_error(error, record_index);
            status = LAPLACE_EVIDENCE_LINEAGE_REFERENCE_MISSING;
            goto cleanup;
        }
        if (child == parent || (index != 0u &&
            (digest_compare(&records[record_index - 1u].node_id, &edge->node_id) > 0 ||
             (digest_equal(&records[record_index - 1u].node_id, &edge->node_id) &&
              digest_compare(&records[record_index - 1u].parent_node_id,
                             &edge->parent_node_id) >= 0)))) {
            set_error(error, record_index);
            status = child == parent ? LAPLACE_EVIDENCE_LINEAGE_CYCLE :
                LAPLACE_EVIDENCE_LINEAGE_ORDER_INVALID;
            if (child == parent && error != NULL) {
                error->cycle_path_count = 2u;
                if (error->cycle_path != NULL && error->cycle_path_capacity >= 2u) {
                    error->cycle_path[0] = edge->node_id;
                    error->cycle_path[1] = edge->node_id;
                }
            }
            goto cleanup;
        }
        edges[index].child = child;
        edges[index].parent = parent;
        ++dependency_count[child];
        ++parent_counts[parent];
    }
    for (index = 0u; index < node_count; ++index) {
        child_offsets[index + 1u] = child_offsets[index] + dependency_count[index];
        parent_offsets[index + 1u] = parent_offsets[index] + parent_counts[index];
        parent_cursor[index] = parent_offsets[index];
    }
    for (index = 0u; index < edge_count; ++index) {
        children[parent_cursor[edges[index].parent]++] = edges[index].child;
    }
    {
        size_t start;
        for (start = 0u; start < node_count; ++start) {
            size_t depth = 0u;
            if (colors[start] != 0u) {
                continue;
            }
            stack[depth] = start;
            stack_edge_cursor[depth] = 0u;
            colors[start] = 1u;
            ++depth;
            while (depth != 0u) {
                const size_t node = stack[depth - 1u];
                const size_t edge_position = stack_edge_cursor[depth - 1u];
                const size_t edge_begin = child_offsets[node];
                const size_t edge_end = child_offsets[node + 1u];
                if (edge_begin + edge_position >= edge_end) {
                    colors[node] = 2u;
                    --depth;
                    continue;
                }
                {
                    const size_t parent = edges[edge_begin + edge_position].parent;
                    stack_edge_cursor[depth - 1u] = edge_position + 1u;
                    if (colors[parent] == 1u) {
                        status = copy_cycle_path(
                            records, stack, depth, parent, error);
                        goto cleanup;
                    }
                    if (colors[parent] == 0u) {
                        stack[depth] = parent;
                        stack_edge_cursor[depth] = 0u;
                        colors[parent] = 1u;
                        ++depth;
                    }
                }
            }
        }
    }
    {
        size_t queue_begin = 0u;
        size_t queue_end = 0u;
        for (index = 0u; index < node_count; ++index) {
            if (dependency_count[index] == 0u) {
                if (produced >= root_capacity) {
                    status = LAPLACE_EVIDENCE_LINEAGE_CAPACITY_INSUFFICIENT;
                    goto cleanup;
                }
                temporary_roots[produced].node_id = records[index].node_id;
                temporary_roots[produced].root_node_id = records[index].node_id;
                temporary_roots[produced].proposition_id = records[index].proposition_id;
                temporary_roots[produced].root_epistemic_kind =
                    records[index].epistemic_kind;
                temporary_roots[produced].flags = LAPLACE_EVIDENCE_FLAGS_NONE;
                root_next[produced] = node_heads[index];
                node_heads[index] = produced++;
                queue[queue_end++] = index;
            }
        }
        while (queue_begin < queue_end) {
            const size_t node = queue[queue_begin++];
            size_t child_position;
            processed[node] = 1u;
            ++processed_count;
#if defined(LAPLACE_TEST_LINEAGE_COUNT_DESCENDANTS_AS_ROOTS)
            if (child_offsets[node + 1u] != child_offsets[node]) {
                if (produced >= root_capacity) {
                    status = LAPLACE_EVIDENCE_LINEAGE_CAPACITY_INSUFFICIENT;
                    goto cleanup;
                }
                temporary_roots[produced].node_id = records[node].node_id;
                temporary_roots[produced].root_node_id = records[node].node_id;
                temporary_roots[produced].proposition_id = records[node].proposition_id;
                temporary_roots[produced].root_epistemic_kind = records[node].epistemic_kind;
                temporary_roots[produced].flags = LAPLACE_EVIDENCE_FLAGS_NONE;
                root_next[produced] = node_heads[node];
                node_heads[node] = produced++;
            }
#endif
            for (child_position = parent_offsets[node];
                 child_position < parent_offsets[node + 1u]; ++child_position) {
                const size_t child = children[child_position];
                size_t relation = node_heads[node];
                while (relation != SIZE_MAX) {
                    size_t existing = node_heads[child];
                    while (existing != SIZE_MAX && !digest_equal(
                            &temporary_roots[existing].root_node_id,
                            &temporary_roots[relation].root_node_id)) {
                        existing = root_next[existing];
                    }
                    if (existing == SIZE_MAX) {
                        if (produced >= root_capacity) {
                            status = LAPLACE_EVIDENCE_LINEAGE_CAPACITY_INSUFFICIENT;
                            goto cleanup;
                        }
                        temporary_roots[produced] = temporary_roots[relation];
                        temporary_roots[produced].node_id = records[child].node_id;
                        temporary_roots[produced].proposition_id =
                            records[child].proposition_id;
                        temporary_roots[produced].path_depth =
                            temporary_roots[relation].path_depth + 1u;
                        root_next[produced] = node_heads[child];
                        node_heads[child] = produced++;
                    } else if (temporary_roots[existing].path_depth >
                               temporary_roots[relation].path_depth + 1u) {
                        temporary_roots[existing].path_depth =
                            temporary_roots[relation].path_depth + 1u;
                    }
                    relation = root_next[relation];
                }
                --dependency_count[child];
                if (dependency_count[child] == 0u) {
                    queue[queue_end++] = child;
                }
            }
        }
    }
    if (processed_count != node_count) {
        status = LAPLACE_EVIDENCE_LINEAGE_RECORD_INVALID;
        goto cleanup;
    }
    qsort(temporary_roots, produced, sizeof(*temporary_roots), root_record_compare);
    memcpy(roots, temporary_roots, produced * sizeof(*roots));
    *root_count = produced;
    receipt->input_record_count = (uint64_t)record_count;
    receipt->node_count = (uint64_t)node_count;
    receipt->edge_count = (uint64_t)edge_count;
    receipt->root_relation_count = (uint64_t)produced;
    receipt->status = LAPLACE_EVIDENCE_LINEAGE_OK;
    hash_inputs(records, record_count, &receipt->input_fingerprint);
    hash_outputs(roots, produced, &receipt->output_fingerprint);
    finish_receipt(receipt);

cleanup:
    free(stack_edge_cursor);
    free(stack);
    free(colors);
    free(temporary_roots);
    free(root_next);
    free(node_heads);
    free(processed);
    free(queue);
    free(children);
    free(parent_cursor);
    free(parent_offsets);
    free(parent_counts);
    free(child_offsets);
    free(dependency_count);
    free(edges);
    if (status != LAPLACE_EVIDENCE_LINEAGE_OK) {
        receipt->status = (uint32_t)status;
    }
    return status;
}
