#include "laplace/cognition_prompt_admission.h"
#include "laplace/content_admission.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
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
    if (byte_count != 0U) {
        blake3_hasher_update(&hasher, bytes, byte_count);
    }
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

laplace_cognition_prompt_admission_status MapContentStatus(
    const laplace_content_admission_status status) {
    switch (status) {
        case LAPLACE_CONTENT_ADMISSION_OK:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_OK;
        case LAPLACE_CONTENT_ADMISSION_INVALID_VERSION:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_VERSION;
        case LAPLACE_CONTENT_ADMISSION_DECOMPOSITION_FAILURE:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_DECOMPOSITION_FAILURE;
        case LAPLACE_CONTENT_ADMISSION_COMPOSITION_PLAN_FAILURE:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_PLAN_FAILURE;
        case LAPLACE_CONTENT_ADMISSION_ATOM_PROVIDER_FAILURE:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_ATOM_PROVIDER_FAILURE;
        case LAPLACE_CONTENT_ADMISSION_ATOM_PROVIDER_INVALID:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_ATOM_PROVIDER_INVALID;
        case LAPLACE_CONTENT_ADMISSION_COMPOSITION_FAILURE:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_FAILURE;
        case LAPLACE_CONTENT_ADMISSION_PRESENCE_FAILURE:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_PRESENCE_FAILURE;
        case LAPLACE_CONTENT_ADMISSION_ROOT_INVALID:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_ROOT_INVALID;
        case LAPLACE_CONTENT_ADMISSION_MEMORY_FAILURE:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_MEMORY_FAILURE;
        case LAPLACE_CONTENT_ADMISSION_NO_PUBLICATION_REQUIRED:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_NO_PUBLICATION_REQUIRED;
        default:
            return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
    }
}

}  // namespace

namespace laplace_cognition_prompt_structural_detail {
struct RetainedIndex;
void DestroyIndex(RetainedIndex* index);
}

struct laplace_cognition_prompt_admission {
    laplace_content_admission* content{};
    const laplace_decomposition_result* decomposition{};
    const laplace_decomposition_composition_plan* composition_plan{};
    const laplace_composition_working_set* working_set{};
    std::vector<laplace_composition_known_entity> known_entities;
    laplace_cognition_prompt_admission_view view{};
    std::mutex structural_index_mutex;
    laplace_cognition_prompt_structural_detail::RetainedIndex* structural_index{};
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

    laplace_content_admission_input content_input{};
    content_input.decomposition = input->decomposition;
    content_input.media_resolver = input->media_resolver;
    content_input.media_resolver_state = input->media_resolver_state;
    content_input.framework_context = input->framework_context;
    content_input.source_fingerprint = input->source_fingerprint;
    content_input.content_recipe_fingerprint = input->content_recipe_fingerprint;
    content_input.calculation_recipe_fingerprint = input->calculation_recipe_fingerprint;
    content_input.geometry_epoch = input->geometry_epoch;
    content_input.occurrence_context_fingerprint = input->occurrence_context_fingerprint;
    content_input.source_ordinal_base = input->source_ordinal_base;
    content_input.preferred_batch_bytes = input->preferred_batch_bytes;
    content_input.version = LAPLACE_CONTENT_ADMISSION_VERSION;

    laplace_content_atom_provider_v1 content_atom_provider{};
    content_atom_provider.state = atom_provider->state;
    content_atom_provider.provider_fingerprint = atom_provider->provider_fingerprint;
    content_atom_provider.resolve = atom_provider->resolve;
    content_atom_provider.abi_major = LAPLACE_CONTENT_ATOM_PROVIDER_ABI_MAJOR;
    content_atom_provider.abi_minor = LAPLACE_CONTENT_ATOM_PROVIDER_ABI_MINOR;

    const auto content_status = laplace_content_admission_create(
        &content_input, &content_atom_provider, presence_provider, &admission->content);
    if (content_status != LAPLACE_CONTENT_ADMISSION_OK || admission->content == nullptr) {
        const auto mapped = MapContentStatus(content_status);
        laplace_cognition_prompt_admission_destroy(&admission);
        return mapped;
    }

    laplace_content_admission_view content_view{};
    if (laplace_content_admission_view_get(admission->content, &content_view) !=
        LAPLACE_CONTENT_ADMISSION_OK) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_FAILURE;
    }

    admission->decomposition = laplace_content_admission_decomposition(admission->content);
    admission->composition_plan = laplace_content_admission_composition_plan(admission->content);
    admission->working_set = laplace_content_admission_working_set(admission->content);
    std::size_t known_count = 0U;
    const auto* known = laplace_content_admission_known_entities(
        admission->content, &known_count);
    if (known_count != 0U && known == nullptr) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_FAILURE;
    }
    try {
        admission->known_entities.assign(known, known + known_count);
    } catch (const std::bad_alloc&) {
        laplace_cognition_prompt_admission_destroy(&admission);
        return LAPLACE_COGNITION_PROMPT_ADMISSION_MEMORY_FAILURE;
    }

    admission->view.decomposition_summary = content_view.decomposition_summary;
    admission->view.composition_summary = content_view.composition_summary;
    admission->view.decomposition_trace_fingerprint =
        content_view.decomposition_trace_fingerprint;
    admission->view.atom_provider_fingerprint = content_view.atom_provider_fingerprint;
    admission->view.atom_provider_receipt_id = content_view.atom_provider_receipt_id;
    admission->view.presence_receipt_id = content_view.presence_receipt_id;
    admission->view.trunk_entity_id = content_view.root_entity_id;
    admission->view.trunk_identity_witness = content_view.root_identity_witness;
    admission->view.trunk_physicality_id = content_view.root_physicality_id;
    admission->view.atom_count = content_view.atom_count;
    admission->view.request_count = content_view.request_count;
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
    if (admission == nullptr || producer == nullptr || admission->content == nullptr) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
    }
    return MapContentStatus(
        laplace_content_admission_producer(admission->content, producer));
}

extern "C" void laplace_cognition_prompt_admission_destroy(
    laplace_cognition_prompt_admission** const admission) {
    if (admission == nullptr || *admission == nullptr) return;
    laplace_cognition_prompt_structural_detail::DestroyIndex((*admission)->structural_index);
    laplace_content_admission_destroy(&(*admission)->content);
    delete *admission;
    *admission = nullptr;
}
