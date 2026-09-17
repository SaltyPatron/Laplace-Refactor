#include "laplace/cognition_coupling.h"
#include "laplace/cognition_interpretation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace laplace_cognition_prompt_orientation_detail {

constexpr char SpanOccurrenceDomain[] =
    "laplace-cognition-prompt-orientation-span-occurrence-v1";
constexpr char ContextDomain[] =
    "laplace-cognition-prompt-orientation-context-v2";
constexpr char GoalSlotDomain[] =
    "laplace-cognition-prompt-orientation-goal-slot-v1";
constexpr char FactorDomain[] =
    "laplace-cognition-prompt-orientation-factor-v1";
constexpr char FactorLawDomain[] =
    "laplace-cognition-prompt-orientation-factor-law-v1";

using OrientationDigestKey = std::array<std::uint8_t, 32>;

enum class OrientationStatus {
    Ok,
    Invalid,
    Exhausted,
    Incomplete,
    NoResponse,
    Ambiguous
};

struct OrientationOutput final {
    laplace_cognition_turn_input turn{};
    laplace_cognition_turn_policy policy{};
    laplace_cognition_coupling_receipt receipt{};
    laplace_cognition_interpretation_receipt interpretation_receipt{};
    laplace_cognition_coupling_response selected{};
};

OrientationDigestKey OrientationKey(const laplace_digest256& value) {
    OrientationDigestKey key{};
    std::copy(std::begin(value.bytes), std::end(value.bytes), key.begin());
    return key;
}

bool OrientationSameId(
    const laplace_id128& left,
    const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

void OrientationHashU32(
    blake3_hasher* const hasher,
    const std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void OrientationHashU64(
    blake3_hasher* const hasher,
    const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void OrientationHashId(
    blake3_hasher* const hasher,
    const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void OrientationHashDigest(
    blake3_hasher* const hasher,
    const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void OrientationFinish(
    blake3_hasher* const hasher,
    laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

laplace_digest256 OrientationDerivedDigest(
    const char* const domain,
    const std::size_t domain_bytes,
    const laplace_digest256& occurrence_id,
    const std::uint64_t ordinal) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, domain_bytes);
    OrientationHashDigest(&hasher, occurrence_id);
    OrientationHashU64(&hasher, ordinal);
    OrientationFinish(&hasher, &output);
    return output;
}

template <std::size_t Count>
laplace_digest256 OrientationDerivedDigest(
    const char (&domain)[Count],
    const laplace_digest256& occurrence_id,
    const std::uint64_t ordinal) {
    return OrientationDerivedDigest(
        domain,
        Count - 1U,
        occurrence_id,
        ordinal);
}

laplace_digest256 SpanOccurrence(
    const laplace_cognition_prompt_admission& admission,
    const laplace_decomposition_span& span,
    const laplace_id128& entity_id) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher,
        SpanOccurrenceDomain,
        sizeof(SpanOccurrenceDomain) - 1U);
    OrientationHashDigest(&hasher, admission.view.occurrence_id);
    OrientationHashId(&hasher, entity_id);
    OrientationHashU64(&hasher, span.byte_start);
    OrientationHashU64(&hasher, span.byte_end);
    OrientationFinish(&hasher, &output);
    return output;
}
