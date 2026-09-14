#include "laplace/cognition_coupling.h"
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
#include <utility>
#include <vector>

namespace laplace_cognition_coupling_detail {

using IdKey = std::array<std::uint8_t, 16>;
using DigestKey = std::array<std::uint8_t, 32>;

constexpr char SeedSetDomain[] = "laplace-cognition-coupling-seed-set-v1";
constexpr char SeedContextDomain[] = "laplace-cognition-coupling-seed-context-v1";
constexpr char ObligationDomain[] = "laplace-cognition-coupling-obligation-v1";
constexpr char StateDomain[] = "laplace-cognition-coupling-state-v1";
constexpr char DominanceDomain[] = "laplace-cognition-coupling-dominance-v1";
constexpr char PathContextDomain[] = "laplace-cognition-coupling-path-context-v1";
constexpr char WitnessDomain[] = "laplace-cognition-coupling-witness-v1";
constexpr char RootSetDomain[] = "laplace-cognition-coupling-root-set-v1";
constexpr char EvidenceSetDomain[] = "laplace-cognition-coupling-evidence-set-v1";
constexpr char ResponseDomain[] = "laplace-cognition-coupling-response-v1";
constexpr char ResponseSetDomain[] = "laplace-cognition-coupling-response-set-v1";
constexpr char ReceiptDomain[] = "laplace-cognition-coupling-receipt-v1";

bool ZeroDigest(const laplace_digest256& value) noexcept {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool ZeroId(const laplace_id128& value) noexcept {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) noexcept {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

IdKey Key(const laplace_id128& value) noexcept {
    IdKey key{};
    std::copy(std::begin(value.bytes), std::end(value.bytes), key.begin());
    return key;
}

DigestKey Key(const laplace_digest256& value) noexcept {
    DigestKey key{};
    std::copy(std::begin(value.bytes), std::end(value.bytes), key.begin());
    return key;
}

void HashU32(blake3_hasher* hasher, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* hasher, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashId(blake3_hasher* hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashDigest(blake3_hasher* hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

template <std::size_t Count>
void Start(blake3_hasher* hasher, const char (&domain)[Count]) {
    blake3_hasher_init(hasher);
    blake3_hasher_update(hasher, domain, Count - 1U);
}

void Finish(blake3_hasher* hasher, laplace_digest256* output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool Add(std::uint64_t left, std::uint64_t right, std::uint64_t* output) noexcept {
    if (output == nullptr || right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    *output = left + right;
    return true;
}

bool Multiply(std::uint64_t left, std::uint64_t right, std::uint64_t* output) noexcept {
    if (output == nullptr ||
        (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)) {
        return false;
    }
    *output = left * right;
    return true;
}

bool ProviderValid(const laplace_cognition_observation_candidate_provider_v1& provider) noexcept {
    return provider.enumerate_candidates != nullptr &&
        !ZeroDigest(provider.provider_fingerprint) &&
        provider.maximum_candidate_records_per_expansion != 0U &&
        provider.abi_major == LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <= LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR &&
        provider.flags == 0U && provider.reserved == 0U;
}

bool SeedValid(const laplace_cognition_coupling_seed& seed) noexcept {
    return !ZeroDigest(seed.occurrence_id) &&
        (seed.flags & ~LAPLACE_COGNITION_COUPLING_SEED_KNOWN_FLAGS) == 0U &&
        seed.reserved == 0U;
}

bool RequestValid(const laplace_cognition_coupling_request& request) noexcept {
    if (request.version != LAPLACE_COGNITION_COUPLING_VERSION ||
        request.seeds == nullptr || request.seed_count == 0U ||
        ZeroDigest(request.world_id) || ZeroDigest(request.time_fingerprint) ||
        ZeroDigest(request.context_fingerprint) || ZeroDigest(request.evidence_boundary) ||
        ZeroDigest(request.evidence_epoch) || ZeroDigest(request.authority_id) ||
        request.maximum_witnesses == 0U || request.maximum_frontier_states == 0U ||
        request.maximum_provider_calls == 0U || request.maximum_memory_bytes == 0U ||
        request.maximum_depth == 0U || request.frontier_batch_width == 0U ||
        request.relation_mask == 0U ||
        (request.relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) != 0U) {
        return false;
    }
    for (std::size_t index = 0U; index < request.seed_count; ++index) {
        if (!SeedValid(request.seeds[index])) return false;
    }
    return true;
}
