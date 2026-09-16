        for (auto& entry : aggregates) {
            FinalizeAggregate(*request, &entry.second);
            owned->responses.push_back(entry.second.response);
        }
        std::sort(
            owned->responses.begin(),
            owned->responses.end(),
            [](const auto& left, const auto& right) {
                return Key(left.entity_id) < Key(right.entity_id);
            });
        std::sort(
            owned->witnesses.begin(),
            owned->witnesses.end(),
            [](const auto& left, const auto& right) {
                return Key(left.witness_id) < Key(right.witness_id);
            });
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_COUPLING_MEMORY_FAILURE;
    }

    receipt->witness_count =
        static_cast<std::uint64_t>(owned->witnesses.size());
    receipt->response_count =
        static_cast<std::uint64_t>(owned->responses.size());
    if (provider_limited) {
        receipt->disposition = LAPLACE_COGNITION_COUPLING_UNKNOWN;
    } else if (resource_limited) {
        receipt->disposition = LAPLACE_COGNITION_COUPLING_PARTIAL;
    } else if (owned->responses.empty()) {
        receipt->disposition = LAPLACE_COGNITION_COUPLING_NO_RESPONSE;
    } else {
        receipt->disposition = LAPLACE_COGNITION_COUPLING_COMPLETE;
    }
