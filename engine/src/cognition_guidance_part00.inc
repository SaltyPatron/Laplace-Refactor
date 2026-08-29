#include "laplace/cognition_guidance.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <vector>

namespace {

using DigestKey = std::array<std::uint8_t, 32>;

bool Zero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

DigestKey Key(const laplace_digest256& value) {
    DigestKey key{};
    std::copy(std::begin(value.bytes), std::end(value.bytes), key.begin());
    return key;
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashI32(blake3_hasher* const hasher, const std::int32_t value) {
    HashU32(hasher, static_cast<std::uint32_t>(value));
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool HeaderValid(const laplace_cognition_guidance_header& header) {
    return !Zero(header.program_id) && !Zero(header.goal_id) &&
        !Zero(header.bindings_fingerprint) && !Zero(header.scope_fingerprint) &&
        !Zero(header.world_id) && !Zero(header.time_fingerprint) &&
        !Zero(header.context_fingerprint) && !Zero(header.evidence_epoch) &&
        !Zero(header.authority_id) && !Zero(header.result_contract_fingerprint) &&
        header.version == LAPLACE_COGNITION_GUIDANCE_VERSION &&
        header.flags == 0U;
}

bool DispositionValid(const std::uint32_t disposition) {
    return disposition >= LAPLACE_COGNITION_OBLIGATION_OPEN &&
        disposition <= LAPLACE_COGNITION_OBLIGATION_UNKNOWN;
}

bool TypedUnresolved(const std::uint32_t disposition) {
    return disposition >= LAPLACE_COGNITION_OBLIGATION_UNSUPPORTED &&
        disposition <= LAPLACE_COGNITION_OBLIGATION_UNKNOWN;
}

bool ObligationValid(const laplace_cognition_obligation& obligation) {
    if (Zero(obligation.obligation_id) || Zero(obligation.binding_fingerprint) ||
        Zero(obligation.world_id) || Zero(obligation.time_fingerprint) ||
        Zero(obligation.context_fingerprint) || Zero(obligation.evidence_boundary) ||
        Zero(obligation.authority_id) ||
        Zero(obligation.result_contract_fingerprint) || obligation.kind == 0U ||
        !DispositionValid(obligation.disposition) ||
        (obligation.flags & ~LAPLACE_COGNITION_OBLIGATION_KNOWN_FLAGS) != 0U ||
        obligation.reserved != 0U) {
        return false;
    }
    if (obligation.disposition == LAPLACE_COGNITION_OBLIGATION_OPEN) {
        return Zero(obligation.value_id) && Zero(obligation.resolution_receipt_id);
    }
    if (Zero(obligation.resolution_receipt_id)) {
        return false;
    }
    if (obligation.disposition == LAPLACE_COGNITION_OBLIGATION_SATISFIED) {
        return !Zero(obligation.value_id);
    }
    return TypedUnresolved(obligation.disposition);
}

void HashHeader(
    blake3_hasher* const hasher,
    const laplace_cognition_guidance_header& header) {
    HashDigest(hasher, header.program_id);
    HashDigest(hasher, header.goal_id);
    HashDigest(hasher, header.bindings_fingerprint);
    HashDigest(hasher, header.scope_fingerprint);
    HashDigest(hasher, header.world_id);
    HashDigest(hasher, header.time_fingerprint);
    HashDigest(hasher, header.context_fingerprint);
    HashDigest(hasher, header.evidence_epoch);
    HashDigest(hasher, header.authority_id);
    HashDigest(hasher, header.result_contract_fingerprint);
    HashU32(hasher, header.version);
    HashU32(hasher, header.flags);
}

void HashObligation(
    blake3_hasher* const hasher,
    const laplace_cognition_obligation& obligation) {
    HashDigest(hasher, obligation.obligation_id);
    HashDigest(hasher, obligation.value_id);
    HashDigest(hasher, obligation.binding_fingerprint);
    HashDigest(hasher, obligation.world_id);
    HashDigest(hasher, obligation.time_fingerprint);
    HashDigest(hasher, obligation.context_fingerprint);
    HashDigest(hasher, obligation.evidence_boundary);
    HashDigest(hasher, obligation.authority_id);
    HashDigest(hasher, obligation.result_contract_fingerprint);
    HashDigest(hasher, obligation.resolution_receipt_id);
    HashU32(hasher, obligation.kind);
    HashU32(hasher, obligation.disposition);
    HashU32(hasher, obligation.flags);
}

bool DigestLess(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

bool ResolutionLess(
    const laplace_cognition_resolution& left,
    const laplace_cognition_resolution& right) {
    return DigestLess(left.obligation_id, right.obligation_id);
}

}  // namespace

struct laplace_cognition_guidance_state {
    laplace_cognition_guidance_header header{};
    laplace_digest256 state_id{};
    std::vector<laplace_cognition_obligation> obligations;
};

namespace {

void IdentifyState(laplace_cognition_guidance_state* const state) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_GUIDANCE_STATE_DOMAIN,
        sizeof(LAPLACE_COGNITION_GUIDANCE_STATE_DOMAIN) - 1U);
    HashHeader(&hasher, state->header);
    HashU64(&hasher, static_cast<std::uint64_t>(state->obligations.size()));
    for (const auto& obligation : state->obligations) {
#if defined(LAPLACE_TEST_COGNITION_GUIDANCE_COLLAPSE_KIND)
        auto collapsed = obligation;
        collapsed.kind = 1U;
        HashObligation(&hasher, collapsed);
#else
        HashObligation(&hasher, obligation);
#endif
    }
    Finish(&hasher, &state->state_id);
}

laplace_cognition_guidance_status BuildState(
    const laplace_cognition_guidance_header& header,
    const laplace_cognition_obligation* const obligations,
    const std::size_t obligation_count,
    laplace_cognition_guidance_state** const output) {
    if (!HeaderValid(header) || obligations == nullptr || obligation_count == 0U ||
        output == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_STATE_INVALID;
    }
    try {
        std::unique_ptr<laplace_cognition_guidance_state> state(
            new laplace_cognition_guidance_state{});
        state->header = header;
        state->obligations.assign(obligations, obligations + obligation_count);
        for (const auto& obligation : state->obligations) {
            if (!ObligationValid(obligation)) {
                return LAPLACE_COGNITION_GUIDANCE_STATE_INVALID;
            }
            if (!Same(obligation.world_id, header.world_id) ||
                !Same(obligation.time_fingerprint, header.time_fingerprint) ||
                !Same(obligation.context_fingerprint, header.context_fingerprint) ||
                !Same(obligation.authority_id, header.authority_id) ||
                !Same(obligation.result_contract_fingerprint,
                      header.result_contract_fingerprint)) {
                return LAPLACE_COGNITION_GUIDANCE_STATE_INVALID;
            }
        }
        std::sort(
            state->obligations.begin(), state->obligations.end(),
            [](const auto& left, const auto& right) {
                return DigestLess(left.obligation_id, right.obligation_id);
            });
        for (std::size_t index = 1U; index < state->obligations.size(); ++index) {
            if (Same(
                    state->obligations[index - 1U].obligation_id,
                    state->obligations[index].obligation_id)) {
                return LAPLACE_COGNITION_GUIDANCE_DUPLICATE;
            }
        }
        IdentifyState(state.get());
        *output = state.release();
        return LAPLACE_COGNITION_GUIDANCE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_GUIDANCE_MEMORY_FAILURE;
    }
}

const laplace_cognition_obligation* FindObligation(
    const laplace_cognition_guidance_state& state,
    const laplace_digest256& obligation_id) {
    const auto found = std::lower_bound(
        state.obligations.begin(), state.obligations.end(), obligation_id,
        [](const auto& obligation, const auto& id) {
            return DigestLess(obligation.obligation_id, id);
        });
    return found != state.obligations.end() && Same(found->obligation_id, obligation_id)
        ? &*found
        : nullptr;
}

}  // namespace
