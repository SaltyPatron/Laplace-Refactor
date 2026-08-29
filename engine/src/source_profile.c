#include "laplace/source_profile.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blake3.h"

_Static_assert(sizeof(laplace_source_profile_manifest) == 744u,
               "source-profile manifest ABI must be exactly 744 bytes");
_Static_assert(sizeof(laplace_source_profile_receipt) == 192u,
               "source-profile receipt ABI must be exactly 192 bytes");
_Static_assert(sizeof(laplace_source_profile_error) == 16u,
               "source-profile error ABI must be exactly 16 bytes");

enum source_profile_field {
    SOURCE_PROFILE_FIELD_NONE = 0u,
    SOURCE_PROFILE_FIELD_COORDINATE = 1u,
    SOURCE_PROFILE_FIELD_FINGERPRINT = 2u,
    SOURCE_PROFILE_FIELD_DENOMINATOR = 3u,
    SOURCE_PROFILE_FIELD_DISPOSITION = 4u,
    SOURCE_PROFILE_FIELD_RECONSTRUCTION = 5u,
    SOURCE_PROFILE_FIELD_FLAGS = 6u,
    SOURCE_PROFILE_FIELD_IDENTITY = 7u,
    SOURCE_PROFILE_FIELD_ORDER = 8u,
    SOURCE_PROFILE_FIELD_SELECTED_BOUNDARY = 9u,
    SOURCE_PROFILE_FIELD_AGGREGATE = 10u
};

static int bytes_zero(const void* value, size_t count) {
    const uint8_t* bytes = (const uint8_t*)value;
    uint8_t aggregate = 0u;
    size_t index;
    for (index = 0u; index < count; ++index) {
        aggregate = (uint8_t)(aggregate | bytes[index]);
    }
    return aggregate == 0u;
}

static int digest_equal(const laplace_digest256* left, const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

uint32_t laplace_source_profile_epistemic_class(
    const laplace_source_profile_manifest* profile) {
    if (profile == NULL) {
        return LAPLACE_SOURCE_PROFILE_EPISTEMIC_UNSPECIFIED;
    }
    return LAPLACE_SOURCE_PROFILE_GET_EPISTEMIC_CLASS(profile->flags);
}

uint32_t laplace_source_profile_evidence_source_type(
    const laplace_source_profile_manifest* profile) {
    if (profile == NULL) {
        return LAPLACE_SOURCE_PROFILE_EVIDENCE_UNSPECIFIED;
    }
    return LAPLACE_SOURCE_PROFILE_GET_EVIDENCE_SOURCE_TYPE(profile->flags);
}

static int flags_valid(uint32_t flags) {
    const uint32_t epistemic_class =
        LAPLACE_SOURCE_PROFILE_GET_EPISTEMIC_CLASS(flags);
    const uint32_t evidence_source_type =
        LAPLACE_SOURCE_PROFILE_GET_EVIDENCE_SOURCE_TYPE(flags);
    return
        (flags & ~LAPLACE_SOURCE_PROFILE_FLAGS_KNOWN_MASK) == 0u &&
        epistemic_class <= LAPLACE_SOURCE_PROFILE_EPISTEMIC_MAX &&
        evidence_source_type <= LAPLACE_SOURCE_PROFILE_EVIDENCE_MAX;
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

static void hash_key(blake3_hasher* hasher, const laplace_highway_key* key) {
    hash_u32(hasher, key->kind);
    blake3_hasher_update(hasher, key->authority.bytes, sizeof(key->authority.bytes));
    blake3_hasher_update(hasher, key->release.bytes, sizeof(key->release.bytes));
    blake3_hasher_update(hasher, key->name_space.bytes, sizeof(key->name_space.bytes));
    blake3_hasher_update(
        hasher, key->local_identifier.bytes, sizeof(key->local_identifier.bytes));
    hash_u64(hasher, key->version);
}

static void hash_fingerprints(
    blake3_hasher* hasher,
    const laplace_source_profile_manifest* profile) {
    blake3_hasher_update(hasher, profile->authority_release_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->license_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->artifact_graph_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->syntax_authority_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->recipe_program_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->universal_ast_mapping_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->highway_references_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->epistemic_witnessing_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->denominator_declaration_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->conformance_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->completion_law_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, profile->selected_boundary_fingerprint.bytes, 32u);
}

static void hash_denominators(
    blake3_hasher* hasher,
    const laplace_source_profile_manifest* profile) {
    hash_u64(hasher, profile->byte_count);
    hash_u64(hasher, profile->container_count);
    hash_u64(hasher, profile->member_count);
    hash_u64(hasher, profile->file_count);
    hash_u64(hasher, profile->record_count);
    hash_u64(hasher, profile->field_count);
    hash_u64(hasher, profile->syntax_node_count);
    hash_u64(hasher, profile->span_count);
    hash_u64(hasher, profile->edge_count);
    hash_u64(hasher, profile->reference_count);
    hash_u64(hasher, profile->occurrence_count);
    hash_u64(hasher, profile->claim_count);
    hash_u64(hasher, profile->mapping_count);
    hash_u64(hasher, profile->error_count);
    hash_u64(hasher, profile->unknown_count);
    hash_u64(hasher, profile->transformation_count);
    hash_u64(hasher, profile->output_count);
}

static void hash_dispositions(
    blake3_hasher* hasher,
    const laplace_source_profile_manifest* profile) {
    hash_u64(hasher, profile->closure_subject_count);
    hash_u64(hasher, profile->accepted_count);
    hash_u64(hasher, profile->rejected_count);
    hash_u64(hasher, profile->duplicate_count);
    hash_u64(hasher, profile->reused_count);
    hash_u64(hasher, profile->transformed_count);
    hash_u64(hasher, profile->lossy_count);
    hash_u64(hasher, profile->unsupported_count);
    hash_u64(hasher, profile->malformed_count);
    hash_u64(hasher, profile->unresolved_count);
    hash_u64(hasher, profile->persisted_count);
    hash_u64(hasher, profile->derived_count);
    hash_u64(hasher, profile->not_applicable_mask);
    hash_u32(hasher, profile->reconstruction_class);
    hash_u32(hasher, profile->flags);
}

static void hash_profile(
    blake3_hasher* hasher,
    const laplace_source_profile_manifest* profile,
    int include_identity) {
    if (include_identity) {
        blake3_hasher_update(hasher, profile->profile_id.bytes, 32u);
    }
    hash_key(hasher, &profile->coordinate);
    hash_fingerprints(hasher, profile);
    hash_denominators(hasher, profile);
    hash_dispositions(hasher, profile);
}

static int add_u64(uint64_t* total, uint64_t value) {
    if (UINT64_MAX - *total < value) {
        return 0;
    }
    *total += value;
    return 1;
}

static int fingerprints_valid(const laplace_source_profile_manifest* profile) {
    return
        !bytes_zero(&profile->authority_release_fingerprint, 32u) &&
        !bytes_zero(&profile->license_fingerprint, 32u) &&
        !bytes_zero(&profile->artifact_graph_fingerprint, 32u) &&
        !bytes_zero(&profile->syntax_authority_fingerprint, 32u) &&
        !bytes_zero(&profile->recipe_program_fingerprint, 32u) &&
        !bytes_zero(&profile->universal_ast_mapping_fingerprint, 32u) &&
        !bytes_zero(&profile->highway_references_fingerprint, 32u) &&
        !bytes_zero(&profile->epistemic_witnessing_fingerprint, 32u) &&
        !bytes_zero(&profile->denominator_declaration_fingerprint, 32u) &&
        !bytes_zero(&profile->conformance_fingerprint, 32u) &&
        !bytes_zero(&profile->completion_law_fingerprint, 32u) &&
        !bytes_zero(&profile->selected_boundary_fingerprint, 32u);
}

static int denominators_valid(const laplace_source_profile_manifest* profile) {
    const uint64_t values[LAPLACE_SOURCE_PROFILE_DENOMINATOR_COUNT] = {
        profile->byte_count, profile->container_count, profile->member_count,
        profile->file_count, profile->record_count, profile->field_count,
        profile->syntax_node_count, profile->span_count, profile->edge_count,
        profile->reference_count, profile->occurrence_count, profile->claim_count,
        profile->mapping_count, profile->error_count, profile->unknown_count,
        profile->transformation_count, profile->output_count};
    const uint64_t required_mask =
        (UINT64_C(1) << 0u) | (UINT64_C(1) << 3u) |
        (UINT64_C(1) << 4u) | (UINT64_C(1) << 5u) |
        (UINT64_C(1) << 6u) | (UINT64_C(1) << 7u) |
        (UINT64_C(1) << 10u) | (UINT64_C(1) << 16u);
    size_t index;
    if ((profile->not_applicable_mask >> LAPLACE_SOURCE_PROFILE_DENOMINATOR_COUNT) != 0u ||
        (profile->not_applicable_mask & required_mask) != 0u) {
        return 0;
    }
    for (index = 0u; index < LAPLACE_SOURCE_PROFILE_DENOMINATOR_COUNT; ++index) {
        const uint64_t mask = UINT64_C(1) << index;
        if ((profile->not_applicable_mask & mask) != 0u && values[index] != 0u) {
            return 0;
        }
    }
    return profile->byte_count != 0u && profile->file_count != 0u &&
        profile->record_count != 0u && profile->field_count != 0u &&
        profile->syntax_node_count != 0u && profile->span_count != 0u &&
        profile->occurrence_count != 0u && profile->output_count != 0u;
}

static laplace_source_profile_status dispositions_validate(
    const laplace_source_profile_manifest* profile) {
    const uint64_t values[LAPLACE_SOURCE_PROFILE_DISPOSITION_COUNT] = {
        profile->accepted_count, profile->rejected_count, profile->duplicate_count,
        profile->reused_count, profile->transformed_count, profile->lossy_count,
        profile->unsupported_count, profile->malformed_count,
        profile->unresolved_count, profile->persisted_count, profile->derived_count};
    uint64_t total = 0u;
    size_t index;
    if (profile->closure_subject_count == 0u) {
        return LAPLACE_SOURCE_PROFILE_DISPOSITION_INVALID;
    }
    for (index = 0u; index < LAPLACE_SOURCE_PROFILE_DISPOSITION_COUNT; ++index) {
        if (!add_u64(&total, values[index])) {
            return LAPLACE_SOURCE_PROFILE_OVERFLOW;
        }
    }
    if (total != profile->closure_subject_count ||
        (profile->reconstruction_class == LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT &&
         profile->lossy_count != 0u)) {
        return LAPLACE_SOURCE_PROFILE_DISPOSITION_INVALID;
    }
    return LAPLACE_SOURCE_PROFILE_OK;
}

laplace_source_profile_status laplace_source_profile_identify(
    const laplace_source_profile_manifest* profile,
    laplace_digest256* profile_id) {
    laplace_highway_coordinate coordinate;
    laplace_source_profile_status disposition_status;
    blake3_hasher hasher;
    if (profile == NULL || profile_id == NULL) {
        return LAPLACE_SOURCE_PROFILE_INVALID_ARGUMENT;
    }
    if (profile->coordinate.kind != LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE ||
        laplace_highway_coordinate_calculate(&profile->coordinate, &coordinate) !=
            LAPLACE_HIGHWAY_OK) {
        return LAPLACE_SOURCE_PROFILE_COORDINATE_INVALID;
    }
    if (!fingerprints_valid(profile)) {
        return LAPLACE_SOURCE_PROFILE_FINGERPRINT_MISSING;
    }
    if (!denominators_valid(profile)) {
        return LAPLACE_SOURCE_PROFILE_DENOMINATOR_INVALID;
    }
    if (profile->reconstruction_class < LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT ||
        profile->reconstruction_class > LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_NONE) {
        return LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_INVALID;
    }
    if (!flags_valid(profile->flags)) {
        return LAPLACE_SOURCE_PROFILE_INVALID_ARGUMENT;
    }
    disposition_status = dispositions_validate(profile);
    if (disposition_status != LAPLACE_SOURCE_PROFILE_OK) {
        return disposition_status;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_SOURCE_PROFILE_PROFILE_DOMAIN,
        sizeof(LAPLACE_SOURCE_PROFILE_PROFILE_DOMAIN) - 1u);
    hash_profile(&hasher, profile, 0);
    finish_digest(&hasher, profile_id);
    return LAPLACE_SOURCE_PROFILE_OK;
}

laplace_source_profile_status laplace_source_profile_validate_batch(
    const laplace_source_profile_manifest* profiles,
    size_t profile_count,
    laplace_source_profile_receipt* receipt,
    laplace_source_profile_error* error) {
    blake3_hasher input_hasher;
    blake3_hasher output_hasher;
    blake3_hasher receipt_hasher;
    size_t index;
    if (receipt != NULL) {
        memset(receipt, 0, sizeof(*receipt));
        receipt->version = LAPLACE_SOURCE_PROFILE_VERSION;
    }
    if (error != NULL) {
        error->profile_index = UINT64_MAX;
        error->field = SOURCE_PROFILE_FIELD_NONE;
        error->reserved = 0u;
    }
    if (profiles == NULL || profile_count == 0u || receipt == NULL) {
        return LAPLACE_SOURCE_PROFILE_INVALID_ARGUMENT;
    }
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(
        &input_hasher, LAPLACE_SOURCE_PROFILE_INPUT_DOMAIN,
        sizeof(LAPLACE_SOURCE_PROFILE_INPUT_DOMAIN) - 1u);
    hash_u64(&input_hasher, (uint64_t)profile_count);
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(
        &output_hasher, LAPLACE_SOURCE_PROFILE_OUTPUT_DOMAIN,
        sizeof(LAPLACE_SOURCE_PROFILE_OUTPUT_DOMAIN) - 1u);
    hash_u64(&output_hasher, (uint64_t)profile_count);
    for (index = 0u; index < profile_count; ++index) {
        laplace_digest256 expected;
        const laplace_source_profile_status identify_status =
            laplace_source_profile_identify(&profiles[index], &expected);
        if (identify_status != LAPLACE_SOURCE_PROFILE_OK) {
            if (error != NULL) {
                error->profile_index = (uint64_t)index;
                error->field = identify_status == LAPLACE_SOURCE_PROFILE_INVALID_ARGUMENT
                    ? SOURCE_PROFILE_FIELD_FLAGS
                    : SOURCE_PROFILE_FIELD_FINGERPRINT;
            }
            return identify_status;
        }
        if (!digest_equal(&expected, &profiles[index].profile_id)) {
            if (error != NULL) {
                error->profile_index = (uint64_t)index;
                error->field = SOURCE_PROFILE_FIELD_IDENTITY;
            }
            return LAPLACE_SOURCE_PROFILE_IDENTITY_MISMATCH;
        }
        if (index != 0u && memcmp(
                profiles[index - 1u].profile_id.bytes,
                profiles[index].profile_id.bytes, 32u) >= 0) {
            if (error != NULL) {
                error->profile_index = (uint64_t)index;
                error->field = SOURCE_PROFILE_FIELD_ORDER;
            }
            return LAPLACE_SOURCE_PROFILE_ORDER_INVALID;
        }
#ifndef LAPLACE_TEST_SOURCE_PROFILE_BOUNDARY_BYPASS
        if (index != 0u && !digest_equal(
                &profiles[0].selected_boundary_fingerprint,
                &profiles[index].selected_boundary_fingerprint)) {
            if (error != NULL) {
                error->profile_index = (uint64_t)index;
                error->field = SOURCE_PROFILE_FIELD_SELECTED_BOUNDARY;
            }
            return LAPLACE_SOURCE_PROFILE_BOUNDARY_MISMATCH;
        }
#endif
        if (!add_u64(&receipt->closure_subject_count, profiles[index].closure_subject_count) ||
            !add_u64(&receipt->persisted_count, profiles[index].persisted_count) ||
            !add_u64(&receipt->negative_count, profiles[index].rejected_count) ||
            !add_u64(&receipt->negative_count, profiles[index].lossy_count) ||
            !add_u64(&receipt->negative_count, profiles[index].unsupported_count) ||
            !add_u64(&receipt->negative_count, profiles[index].malformed_count) ||
            !add_u64(&receipt->negative_count, profiles[index].unresolved_count)) {
            if (error != NULL) {
                error->profile_index = (uint64_t)index;
                error->field = SOURCE_PROFILE_FIELD_AGGREGATE;
            }
            return LAPLACE_SOURCE_PROFILE_OVERFLOW;
        }
        switch (profiles[index].reconstruction_class) {
            case LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT:
                receipt->exact_reconstruction_count += 1u;
                break;
            case LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC:
                receipt->semantic_reconstruction_count += 1u;
                break;
            case LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_NONE:
                receipt->no_reconstruction_count += 1u;
                break;
            default:
                return LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_INVALID;
        }
        hash_profile(&input_hasher, &profiles[index], 1);
        blake3_hasher_update(&output_hasher, profiles[index].profile_id.bytes, 32u);
    }
    receipt->selected_boundary_fingerprint = profiles[0].selected_boundary_fingerprint;
    receipt->profile_count = (uint64_t)profile_count;
    finish_digest(&input_hasher, &receipt->input_fingerprint);
    finish_digest(&output_hasher, &receipt->output_fingerprint);
    receipt->status = LAPLACE_SOURCE_PROFILE_OK;
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, LAPLACE_SOURCE_PROFILE_RECEIPT_DOMAIN,
        sizeof(LAPLACE_SOURCE_PROFILE_RECEIPT_DOMAIN) - 1u);
    blake3_hasher_update(
        &receipt_hasher, receipt->selected_boundary_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->input_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->output_fingerprint.bytes, 32u);
    hash_u64(&receipt_hasher, receipt->profile_count);
    hash_u64(&receipt_hasher, receipt->closure_subject_count);
    hash_u64(&receipt_hasher, receipt->persisted_count);
    hash_u64(&receipt_hasher, receipt->negative_count);
    hash_u64(&receipt_hasher, receipt->exact_reconstruction_count);
    hash_u64(&receipt_hasher, receipt->semantic_reconstruction_count);
    hash_u64(&receipt_hasher, receipt->no_reconstruction_count);
    hash_u32(&receipt_hasher, receipt->version);
    hash_u32(&receipt_hasher, receipt->status);
    finish_digest(&receipt_hasher, &receipt->receipt_id);
    return LAPLACE_SOURCE_PROFILE_OK;
}
