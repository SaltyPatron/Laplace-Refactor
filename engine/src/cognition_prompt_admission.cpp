#include "laplace/cognition_prompt_admission.h"

#include "laplace/identity.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr char ExactBytesDomain[] = "laplace-cognition-prompt-exact-bytes-v1";
constexpr char OccurrenceDomain[] = "laplace-cognition-prompt-occurrence-v1";
constexpr char AdmissionDomain[] = "laplace-cognition-prompt-admission-v1";

bool BytesZero(const std::uint8_t* const bytes, const std::size_t count) {
    std::uint8_t aggregate = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        aggregate = static_cast<std::uint8_t>(aggregate | bytes[index]);
    }
    return aggregate == 0U;
}

bool DigestZero(const laplace_digest256& value) {
    return BytesZero(value.bytes, sizeof(value.bytes));
}

bool IdZero(const laplace_id128& value) {
    return BytesZero(value.bytes, sizeof(value.bytes));
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

laplace_digest256 ExactBytesFingerprint(
    const std::uint8_t* const bytes,
    const std::size_t byte_count) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, ExactBytesDomain, sizeof(ExactBytesDomain) - 1U);
    HashU64(&hasher, static_cast<std::uint64_t>(byte_count));
    blake3_hasher_update(&hasher, bytes, byte_count);
    Finish(&hasher, &output);
    return output;
}

bool AtomProviderValid(const laplace_cognition_prompt_atom_provider_v1& provider) {
    return provider.abi_major == LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <= LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MINOR &&
        provider.resolve != nullptr && !DigestZero(provider.provider_fingerprint) &&
        provider.flags == 0U && provider.reserved == 0U;
}

bool ScopeValid(const laplace_cognition_prompt_occurrence_scope& scope) {
    return !DigestZero(scope.principal_fingerprint) &&
        !DigestZero(scope.session_fingerprint) &&
        !DigestZero(scope.discourse_id) &&
        !DigestZero(scope.world_id) &&
        !DigestZero(scope.time_fingerprint) &&
        !DigestZero(scope.context_fingerprint) &&
        (scope.turn_flags & ~LAPLACE_COGNITION_TURN_KNOWN_FLAGS) == 0U &&
        scope.reserved == 0U &&
        (((scope.turn_flags & LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE) != 0U)
             ? scope.turn_ordinal > 0U
             : scope.turn_ordinal == 0U);
}

bool InputValid(const laplace_cognition_prompt_admission_input& input) {
    return input.version == LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION &&
        input.reserved == 0U && input.framework_context != nullptr &&
        input.source_ordinal_base > 0U && input.preferred_batch_bytes > 0U &&
        !DigestZero(input.source_fingerprint) &&
        !DigestZero(input.content_recipe_fingerprint) &&
        !DigestZero(input.calculation_recipe_fingerprint) &&
        !DigestZero(input.geometry_epoch) &&
        !DigestZero(input.occurrence_context_fingerprint) &&
        ScopeValid(input.occurrence);
}

bool KnownAtomValid(
    const std::uint32_t position,
    const laplace_composition_known_entity& known) {
    laplace_id128 expected_id{};
    laplace_digest256 expected_witness{};
    return known.atom == position && known.has_atom == 1U &&
        known.tier_floor == 0U && known.reserved == 0U &&
        !DigestZero(known.physicality_id) &&
        laplace_identity_codepoint_witness(
            position, &expected_id, &expected_witness) == LAPLACE_IDENTITY_OK &&
        SameId(known.entity_id, expected_id) &&
        SameDigest(known.identity_witness, expected_witness);
}

laplace_digest256 OccurrenceId(
    const laplace_cognition_prompt_admission_input& input,
    const laplace_cognition_prompt_admission_view& view) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, OccurrenceDomain, sizeof(OccurrenceDomain) - 1U);
    HashId(&hasher, view.trunk_entity_id);
    HashDigest(&hasher, view.trunk_identity_witness);
    HashDigest(&hasher, input.source_fingerprint);
    HashDigest(&hasher, input.occurrence.principal_fingerprint);
    HashDigest(&hasher, input.occurrence.session_fingerprint);
    HashDigest(&hasher, input.occurrence.discourse_id);
    HashDigest(&hasher, input.occurrence.world_id);
    HashDigest(&hasher, input.occurrence.time_fingerprint);
    HashDigest(&hasher, input.occurrence.context_fingerprint);
    HashDigest(&hasher, input.occurrence_context_fingerprint);
    HashU64(&hasher, input.occurrence.turn_ordinal);
    HashU32(&hasher, input.occurrence.turn_flags);
    Finish(&hasher, &output);
    return output;
}

laplace_digest256 AdmissionReceipt(
    const laplace_cognition_prompt_admission_input& input,
    const laplace_cognition_prompt_admission_view& view) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, AdmissionDomain, sizeof(AdmissionDomain) - 1U);
    HashId(&hasher, view.trunk_entity_id);
    HashDigest(&hasher, view.trunk_identity_witness);
    HashDigest(&hasher, view.trunk_physicality_id);
    HashDigest(&hasher, view.occurrence_id);
    HashDigest(&hasher, view.exact_bytes_fingerprint);
    HashDigest(&hasher, view.decomposition_trace_fingerprint);
    HashDigest(&hasher, view.atom_provider_fingerprint);
    HashDigest(&hasher, view.atom_provider_receipt_id);
    HashDigest(&hasher, view.presence_receipt_id);
    HashDigest(&hasher, view.composition_summary.receipt_id);
    HashDigest(&hasher, input.source_fingerprint);
    HashDigest(&hasher, input.content_recipe_fingerprint);
    HashDigest(&hasher, input.calculation_recipe_fingerprint);
    HashDigest(&hasher, input.geometry_epoch);
    HashU64(&hasher, view.decomposition_summary.span_count);
    HashU64(&hasher, view.atom_count);
    HashU64(&hasher, view.request_count);
    HashU64(&hasher, view.semantic_attestation_count);
    HashU32(&hasher, view.version);
    Finish(&hasher, &output);
    return output;
}

}  // namespace

struct laplace_cognition_prompt_admission {
    laplace_decomposition_result* decomposition{};
    laplace_decomposition_composition_plan* composition_plan{};
    laplace_composition_working_set* working_set{};
    std::vector<laplace_composition_known_entity> known_entities;
    laplace_cognition_prompt_admission_view view{};
};

extern "C" laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_create(
    const laplace_cognition_prompt_admission_input* const input,
    const laplace_cognition_prompt_atom_provider_v1* const atom_provider,
    const laplace_composition_presence_provider_v1* const presence_provider,
    laplace_cognition_prompt_admission** const output) {
    if (output != nullptr) *output = nullptr;
    if (input == nullptr || atom_provider == nullptr || output == nullptr) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
    }
    if (input->version != LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_VERSION;
    }
    if (!InputValid(*input) || !AtomProviderValid(*atom_provider)) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
    }

    auto* admission = new (std::nothrow) laplace_cognition_prompt_admission{};
    if (admission == nullptr) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_MEMORY_FAILURE;
    }

    const auto decomposition_status = input->media_resolver == nullptr
        ? laplace_decomposition_run(&input->decomposition, &admission->decomposition)
        : laplace_decomposition_run_with_media_resolver(
              &input->decomposition, input->media_resolver,
              input->media_resolver_state, &admission->decomposition);
    if (decomposition_status != LAPLACE_DECOMPOSITION_OK ||
        admission->decomposition == nullptr ||
        laplace_decomposition_summary_get(
            admission->decomposition, &admission->view.decomposition_summary) !=
            LAPLACE_DECOMPOSITION_OK) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_DECOMPOSITION_FAILURE;
    }

    laplace_decomposition_composition_input composition_input{};
    composition_input.content = &input->decomposition.content;
    composition_input.decomposition = admission->decomposition;
    composition_input.recipe_fingerprint = input->content_recipe_fingerprint;
    composition_input.geometry_epoch = input->geometry_epoch;
    composition_input.occurrence_context_fingerprint =
        input->occurrence_context_fingerprint;
    composition_input.source_ordinal_base = input->source_ordinal_base;
    if (laplace_decomposition_composition_plan_create(
            &composition_input, &admission->composition_plan) !=
            LAPLACE_DECOMPOSITION_COMPOSITION_OK ||
        admission->composition_plan == nullptr) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_PLAN_FAILURE;
    }

    laplace_decomposition_composition_plan_view plan_view{};
    if (laplace_decomposition_composition_plan_view_get(
            admission->composition_plan, &plan_view) !=
        LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_PLAN_FAILURE;
    }
    admission->view.decomposition_trace_fingerprint = plan_view.trace_fingerprint;
    admission->view.atom_count = plan_view.atom_count;
    admission->view.request_count = plan_view.request_count;

    try {
        admission->known_entities.resize(
            static_cast<std::size_t>(plan_view.atom_count));
    } catch (const std::bad_alloc&) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_MEMORY_FAILURE;
    }

    laplace_digest256 atom_receipt{};
    if (atom_provider->resolve(
            atom_provider->state, plan_view.atom_positions,
            static_cast<std::size_t>(plan_view.atom_count),
            admission->known_entities.data(), &atom_receipt) != 0 ||
        DigestZero(atom_receipt)) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_ATOM_PROVIDER_FAILURE;
    }
    for (std::size_t index = 0U; index < admission->known_entities.size(); ++index) {
        if (!KnownAtomValid(
                plan_view.atom_positions[index], admission->known_entities[index])) {
            laplace_cognition_prompt_admission_destroy(&admission);
            return LAPLACE_COGNITION_PROMPT_ADMISSION_ATOM_PROVIDER_INVALID;
        }
    }
    admission->view.atom_provider_fingerprint = atom_provider->provider_fingerprint;
    admission->view.atom_provider_receipt_id = atom_receipt;

    if (plan_view.request_count != 0U) {
        if (presence_provider == nullptr) {
            laplace_cognition_prompt_admission_destroy(&admission);
            return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
        }
        for (std::uint64_t index = 0U; index < plan_view.request_count; ++index) {
            if (plan_view.requests[index].flags != 0U) {
                laplace_cognition_prompt_admission_destroy(&admission);
                return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_PLAN_FAILURE;
            }
        }
        const laplace_composition_working_set_input working_input{
            input->framework_context,
            &input->source_fingerprint,
            &input->calculation_recipe_fingerprint,
            admission->known_entities.data(),
            plan_view.atom_count,
            plan_view.operands,
            plan_view.operand_count,
            plan_view.requests,
            plan_view.request_count,
            input->preferred_batch_bytes,
            0U};
        if (laplace_composition_working_set_create(
                &working_input, &admission->working_set) != LAPLACE_COMPOSITION_OK ||
            admission->working_set == nullptr) {
            laplace_cognition_prompt_admission_destroy(&admission);
            return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_FAILURE;
        }
        laplace_composition_presence_receipt presence_receipt{};
        if (laplace_composition_working_set_resolve_presence(
                admission->working_set, presence_provider,
                &presence_receipt) != LAPLACE_COMPOSITION_OK) {
            laplace_cognition_prompt_admission_destroy(&admission);
            return LAPLACE_COGNITION_PROMPT_ADMISSION_PRESENCE_FAILURE;
        }
        admission->view.presence_receipt_id = presence_receipt.semantic_receipt_id;
        if (laplace_composition_working_set_summary_get(
                admission->working_set,
                &admission->view.composition_summary) != LAPLACE_COMPOSITION_OK ||
            admission->view.composition_summary.occurrence_count != 0U) {
            laplace_cognition_prompt_admission_destroy(&admission);
            return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_FAILURE;
        }
    }

    if (plan_view.root_reference.reference_kind ==
        LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
        if (plan_view.root_reference.reference_index >=
            admission->known_entities.size()) {
            laplace_cognition_prompt_admission_destroy(&admission);
            return LAPLACE_COGNITION_PROMPT_ADMISSION_ROOT_INVALID;
        }
        const auto& root = admission->known_entities[
            static_cast<std::size_t>(plan_view.root_reference.reference_index)];
        admission->view.trunk_entity_id = root.entity_id;
        admission->view.trunk_identity_witness = root.identity_witness;
        admission->view.trunk_physicality_id = root.physicality_id;
    } else if (plan_view.root_reference.reference_kind ==
        LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
        std::size_t result_count = 0U;
        const auto* results = laplace_composition_working_set_results(
            admission->working_set, &result_count);
        if (results == nullptr ||
            plan_view.root_reference.reference_index >= result_count) {
            laplace_cognition_prompt_admission_destroy(&admission);
            return LAPLACE_COGNITION_PROMPT_ADMISSION_ROOT_INVALID;
        }
        const auto& root = results[
            static_cast<std::size_t>(plan_view.root_reference.reference_index)];
        admission->view.trunk_entity_id = root.entity_id;
        admission->view.trunk_identity_witness = root.identity_witness;
        admission->view.trunk_physicality_id = root.physicality_id;
    } else {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_ROOT_INVALID;
    }
    if (IdZero(admission->view.trunk_entity_id) ||
        DigestZero(admission->view.trunk_identity_witness) ||
        DigestZero(admission->view.trunk_physicality_id)) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_ROOT_INVALID;
    }

    admission->view.exact_bytes_fingerprint = ExactBytesFingerprint(
        input->decomposition.content.bytes,
        static_cast<std::size_t>(input->decomposition.content.byte_count));
    admission->view.semantic_attestation_count = 0U;
    admission->view.version = LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION;
    admission->view.status = LAPLACE_COGNITION_PROMPT_ADMISSION_OK;
    admission->view.occurrence_id = OccurrenceId(*input, admission->view);

    admission->view.turn.discourse_id = input->occurrence.discourse_id;
    admission->view.turn.observation_entity_id = admission->view.trunk_entity_id;
    admission->view.turn.observation_occurrence_id = admission->view.occurrence_id;
    admission->view.turn.world_id = input->occurrence.world_id;
    admission->view.turn.time_fingerprint = input->occurrence.time_fingerprint;
    admission->view.turn.context_fingerprint = input->occurrence.context_fingerprint;
    admission->view.turn.turn_ordinal = input->occurrence.turn_ordinal;
    admission->view.turn.flags = input->occurrence.turn_flags;
    admission->view.turn.version = LAPLACE_COGNITION_TURN_VERSION;
    admission->view.admission_receipt_id = AdmissionReceipt(*input, admission->view);

    *output = admission;
    return LAPLACE_COGNITION_PROMPT_ADMISSION_OK;
}

extern "C" laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_view_get(
    const laplace_cognition_prompt_admission* const admission,
    laplace_cognition_prompt_admission_view* const view) {
    if (admission == nullptr || view == nullptr) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
    }
    *view = admission->view;
    return LAPLACE_COGNITION_PROMPT_ADMISSION_OK;
}

extern "C" laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_producer(
    laplace_cognition_prompt_admission* const admission,
    laplace_framework_producer_v1* const producer) {
    if (admission == nullptr || producer == nullptr) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
    }
    *producer = laplace_framework_producer_v1{};
    if (admission->working_set == nullptr) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_NO_PUBLICATION_REQUIRED;
    }
    if (laplace_composition_working_set_producer(
            admission->working_set, producer) != LAPLACE_COMPOSITION_OK) {
        *producer = laplace_framework_producer_v1{};
        return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_FAILURE;
    }
    return LAPLACE_COGNITION_PROMPT_ADMISSION_OK;
}

extern "C" void laplace_cognition_prompt_admission_destroy(
    laplace_cognition_prompt_admission** const admission) {
    if (admission == nullptr || *admission == nullptr) return;
    laplace_composition_working_set_destroy(&(*admission)->working_set);
    laplace_decomposition_composition_plan_destroy(&(*admission)->composition_plan);
    laplace_decomposition_result_destroy(&(*admission)->decomposition);
    delete *admission;
    *admission = nullptr;
}
