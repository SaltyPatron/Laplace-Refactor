struct laplace_cognition_observation_candidate_provider_set {
    std::vector<laplace_cognition_observation_candidate_provider_v1> providers;
    laplace_digest256 provider_fingerprint{};
    std::uint64_t maximum_candidate_records_per_expansion{};
};

namespace laplace_cognition_observation_provider_set_detail {

constexpr char ProviderSetDomain[] =
    "laplace-cognition-observation-candidate-provider-set-v1";

using laplace_observation_query_detail::DigestZero;
using laplace_observation_query_detail::Finish;
using laplace_observation_query_detail::HashDigest;
using laplace_observation_query_detail::HashU32;
using laplace_observation_query_detail::HashU64;
using laplace_observation_query_detail::StartDomain;

bool SameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) noexcept {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool LessDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) noexcept {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

bool AddChecked(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t* const output) noexcept {
    if (output == nullptr ||
        right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    *output = left + right;
    return true;
}

bool ProviderDescriptorValid(
    const laplace_cognition_observation_candidate_provider_v1& provider) noexcept {
    return provider.enumerate_candidates != nullptr &&
        !DigestZero(provider.provider_fingerprint) &&
        provider.maximum_candidate_records_per_expansion != 0U &&
        provider.abi_major ==
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <=
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR &&
        provider.flags == 0U && provider.reserved == 0U;
}

bool CandidateSourceLayerValid(const std::uint32_t source_layer) noexcept {
    return source_layer != 0U &&
        (source_layer & ~static_cast<std::uint32_t>(
            LAPLACE_OBSERVATION_QUERY_SOURCE_LAYER_MASK)) == 0U &&
        (source_layer & (source_layer - 1U)) == 0U;
}

bool LimitingDispositionValid(const std::uint32_t disposition) noexcept {
    return disposition == 0U ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_UNSUPPORTED ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_DENIED ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_CONTRADICTED ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
}

bool AddUsage(
    laplace_cognition_observation_candidate_usage* const total,
    const laplace_cognition_observation_candidate_usage& addend) noexcept {
    return total != nullptr &&
        AddChecked(total->rows_examined, addend.rows_examined,
                   &total->rows_examined) &&
        AddChecked(total->index_plan_count, addend.index_plan_count,
                   &total->index_plan_count) &&
        AddChecked(total->crossing_count, addend.crossing_count,
                   &total->crossing_count) &&
        AddChecked(total->io_operations, addend.io_operations,
                   &total->io_operations) &&
        AddChecked(total->database_operations, addend.database_operations,
                   &total->database_operations);
}

int EnumerateCompositeCandidates(
    void* const opaque,
    const laplace_observation_query_binding* const binding,
    const laplace_id128* const source_entity_ids,
    const laplace_query_search_state* const frontier_states,
    const std::uint64_t* const accumulated_costs,
    const std::size_t frontier_state_count,
    laplace_cognition_observation_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_observation_candidate_usage* const usage) {
    if (opaque == nullptr || binding == nullptr || source_entity_ids == nullptr ||
        frontier_states == nullptr || accumulated_costs == nullptr ||
        frontier_state_count == 0U || candidates == nullptr ||
        candidate_capacity == 0U || candidate_count == nullptr || usage == nullptr) {
        return 1;
    }

    auto& provider_set =
        *static_cast<laplace_cognition_observation_candidate_provider_set*>(opaque);
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    std::uint32_t common_limiting_disposition = 0U;
    bool saw_limiting_disposition = false;
    bool limiting_dispositions_differ = false;

    for (const auto& provider : provider_set.providers) {
        if (*candidate_count >= candidate_capacity) return 2;
        const std::size_t first_child_candidate = *candidate_count;
        const std::size_t remaining = candidate_capacity - first_child_candidate;
        std::size_t child_count = 0U;
        laplace_cognition_observation_candidate_usage child_usage{};
        const int child_status = provider.enumerate_candidates(
            provider.state, binding, source_entity_ids, frontier_states,
            accumulated_costs, frontier_state_count,
            candidates + first_child_candidate, remaining, &child_count, &child_usage);
        if (child_status != 0) return 3;
        if (child_count > remaining || child_usage.flags != 0U ||
            !LimitingDispositionValid(child_usage.limiting_disposition) ||
            (child_usage.limiting_disposition != 0U && child_count != 0U)) {
            return 4;
        }
        for (std::size_t offset = 0U; offset < child_count; ++offset) {
            if (!CandidateSourceLayerValid(
                    candidates[first_child_candidate + offset].source_layer)) {
                return 5;
            }
        }

        if (provider.maximum_candidate_records_per_expansion >
            std::numeric_limits<std::uint64_t>::max() /
                static_cast<std::uint64_t>(frontier_state_count)) {
            return 6;
        }
        const std::uint64_t child_maximum =
            provider.maximum_candidate_records_per_expansion *
            static_cast<std::uint64_t>(frontier_state_count);
        if (child_usage.rows_examined > child_maximum ||
            static_cast<std::uint64_t>(child_count) > child_maximum) {
            return 7;
        }
        if (!AddUsage(usage, child_usage)) return 8;
        *candidate_count += child_count;

        /* Capacity exhaustion means this provider could not enumerate its exact
         * finite contribution. Preserve atomicity for the composite instead of
         * letting candidates from another child disguise an incomplete frontier. */
        if (child_usage.limiting_disposition ==
            LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED) {
            *candidate_count = 0U;
            usage->limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED;
            return 0;
        }

        if (child_usage.limiting_disposition != 0U) {
            if (!saw_limiting_disposition) {
                common_limiting_disposition = child_usage.limiting_disposition;
                saw_limiting_disposition = true;
            } else if (common_limiting_disposition !=
                       child_usage.limiting_disposition) {
                limiting_dispositions_differ = true;
            }
        }
    }

    /*
     * A provider that produced candidates must not make another provider's
     * abstention look like a terminal denial. If every provider abstained with
     * one common reason, preserve it; heterogeneous abstentions remain UNKNOWN.
     */
    if (*candidate_count != 0U) {
        usage->limiting_disposition = 0U;
    } else if (limiting_dispositions_differ) {
        usage->limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
    } else if (saw_limiting_disposition) {
        usage->limiting_disposition = common_limiting_disposition;
    }
    return 0;
}

}  // namespace laplace_cognition_observation_provider_set_detail

extern "C" laplace_cognition_observation_provider_set_status
laplace_cognition_observation_candidate_provider_set_create(
    const laplace_cognition_observation_candidate_provider_v1* const providers,
    const std::size_t provider_count,
    laplace_cognition_observation_candidate_provider_set** const provider_set,
    laplace_cognition_observation_candidate_provider_v1* const composite_provider) {
    using namespace laplace_cognition_observation_provider_set_detail;
    if (provider_set != nullptr) *provider_set = nullptr;
    if (composite_provider != nullptr) {
        *composite_provider = laplace_cognition_observation_candidate_provider_v1{};
    }
    if (providers == nullptr || provider_count == 0U || provider_set == nullptr ||
        composite_provider == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_INVALID_ARGUMENT;
    }

    laplace_cognition_observation_candidate_provider_set* state = nullptr;
    try {
        state = new laplace_cognition_observation_candidate_provider_set{};
        state->providers.assign(providers, providers + provider_count);
        for (const auto& provider : state->providers) {
            if (!ProviderDescriptorValid(provider)) {
                delete state;
                return LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_INVALID_PROVIDER;
            }
        }
        std::sort(
            state->providers.begin(), state->providers.end(),
            [](const auto& left, const auto& right) {
                return LessDigest(
                    left.provider_fingerprint, right.provider_fingerprint);
            });
        for (std::size_t index = 1U; index < state->providers.size(); ++index) {
            if (SameDigest(
                    state->providers[index - 1U].provider_fingerprint,
                    state->providers[index].provider_fingerprint)) {
                delete state;
                return LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_DUPLICATE_PROVIDER;
            }
        }

        std::uint64_t maximum = 0U;
        for (const auto& provider : state->providers) {
            if (!AddChecked(
                    maximum,
                    provider.maximum_candidate_records_per_expansion,
                    &maximum)) {
                delete state;
                return LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OVERFLOW;
            }
        }
        state->maximum_candidate_records_per_expansion = maximum;

        blake3_hasher hasher{};
        StartDomain(&hasher, ProviderSetDomain);
        HashU64(&hasher, static_cast<std::uint64_t>(state->providers.size()));
        for (const auto& provider : state->providers) {
            HashDigest(&hasher, provider.provider_fingerprint);
            HashU64(
                &hasher,
                provider.maximum_candidate_records_per_expansion);
            HashU32(&hasher, provider.abi_major);
            HashU32(&hasher, provider.abi_minor);
        }
        Finish(&hasher, &state->provider_fingerprint);
        if (DigestZero(state->provider_fingerprint)) {
            delete state;
            return LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_INVALID_PROVIDER;
        }

        composite_provider->state = state;
        composite_provider->provider_fingerprint = state->provider_fingerprint;
        composite_provider->maximum_candidate_records_per_expansion = maximum;
        composite_provider->enumerate_candidates = EnumerateCompositeCandidates;
        composite_provider->abi_major =
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
        composite_provider->abi_minor =
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
        *provider_set = state;
        return LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK;
    } catch (const std::bad_alloc&) {
        delete state;
        return LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_MEMORY_FAILURE;
    }
}

extern "C" void
laplace_cognition_observation_candidate_provider_set_destroy(
    laplace_cognition_observation_candidate_provider_set** const provider_set) {
    if (provider_set == nullptr || *provider_set == nullptr) return;
    delete *provider_set;
    *provider_set = nullptr;
}
