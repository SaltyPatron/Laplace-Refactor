#include "laplace/decomposition.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

struct laplace_decomposition_result {
    std::vector<laplace_decomposition_span> spans;
    laplace_decomposition_summary summary{};
};

namespace {

using Fingerprint = std::array<std::uint8_t, 32>;

Fingerprint Key(const laplace_digest256& digest) {
    Fingerprint result{};
    std::memcpy(result.data(), digest.bytes, result.size());
    return result;
}

bool DigestZero(const laplace_digest256& digest) {
    for (const std::uint8_t byte : digest.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

bool ContentValid(const laplace_decomposition_content& content) {
    if (content.bytes == nullptr || content.byte_count == 0u ||
        content.byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        (content.media_type == nullptr) != (content.media_type_byte_count == 0u) ||
        (content.name == nullptr) != (content.name_byte_count == 0u) ||
        content.media_type_byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        content.name_byte_count > static_cast<std::uint64_t>(SIZE_MAX)) {
        return false;
    }
    return true;
}

bool ProviderValid(const laplace_decomposition_provider_v1& provider) {
    return provider.applicable != nullptr && provider.apply != nullptr &&
        !DigestZero(provider.provider_fingerprint) &&
        provider.abi_major == LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <= LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR &&
        provider.flags == 0u && provider.reserved == 0u;
}

bool RootIsText(const laplace_decomposition_content& content) {
    if (content.media_type == nullptr || content.media_type_byte_count == 0u) return false;
    const std::string_view media_type(
        content.media_type, static_cast<std::size_t>(content.media_type_byte_count));
    return media_type.starts_with("text/") ||
        media_type == "application/json" ||
        media_type == "application/xml" ||
        media_type == "application/javascript" ||
        media_type == "application/sql";
}

struct Task {
    std::uint64_t span_index;
    std::uint64_t skip_provider;
};

using EmittedKey = std::tuple<
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t>;

struct ApplyContext {
    laplace_decomposition_result* result;
    const laplace_decomposition_input* input;
    std::uint64_t parent_span_index;
    std::uint64_t provider_index;
    std::deque<Task>* queue;
    std::set<EmittedKey>* emitted;
    laplace_decomposition_status status;
};

int Emit(
    void* opaque,
    const std::uint64_t byte_start,
    const std::uint64_t byte_end,
    const std::uint64_t kind,
    const std::uint32_t flags) {
    if (opaque == nullptr) return 1;
    auto& context = *static_cast<ApplyContext*>(opaque);
    if (context.status != LAPLACE_DECOMPOSITION_OK) return 1;
    constexpr std::uint32_t KnownSpanFlags =
        LAPLACE_DECOMPOSITION_SPAN_REDISPATCH |
        LAPLACE_DECOMPOSITION_SPAN_TEXT |
        LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT;
    if ((flags & ~KnownSpanFlags) != 0u || kind == 0u) {
        context.status = LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        return 1;
    }
    const auto& parent = context.result->spans[
        static_cast<std::size_t>(context.parent_span_index)];
    if (byte_start < parent.byte_start || byte_start >= byte_end ||
        byte_end > parent.byte_end) {
        context.status = LAPLACE_DECOMPOSITION_RANGE_INVALID;
        return 1;
    }
    if (parent.depth >= context.input->maximum_depth) {
        context.status = LAPLACE_DECOMPOSITION_LIMIT_EXCEEDED;
        return 1;
    }
    const EmittedKey duplicate_key{
        context.parent_span_index,
        context.provider_index,
        byte_start,
        byte_end,
        kind};
    if (!context.emitted->insert(duplicate_key).second) return 0;
    if (context.result->spans.size() >=
        static_cast<std::size_t>(context.input->maximum_spans)) {
        context.status = LAPLACE_DECOMPOSITION_LIMIT_EXCEEDED;
        return 1;
    }
    laplace_decomposition_span span{};
    span.byte_start = byte_start;
    span.byte_end = byte_end;
    span.parent_span_index = context.parent_span_index;
    span.provider_fingerprint = context.input->providers[context.provider_index].provider_fingerprint;
    span.kind = kind;
    span.depth = parent.depth + 1u;
    span.flags = flags;
    const std::uint64_t span_index =
        static_cast<std::uint64_t>(context.result->spans.size());
    try {
        context.result->spans.push_back(span);
        if ((flags & LAPLACE_DECOMPOSITION_SPAN_REDISPATCH) != 0u) {
            context.queue->push_back(Task{span_index, context.provider_index});
            ++context.result->summary.redispatch_count;
        }
    } catch (...) {
        context.status = LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
        return 1;
    }
    if (span.depth > context.result->summary.maximum_depth_reached) {
        context.result->summary.maximum_depth_reached = span.depth;
    }
    return 0;
}

}  // namespace

extern "C" laplace_decomposition_status laplace_decomposition_run(
    const laplace_decomposition_input* input,
    laplace_decomposition_result** output) {
    if (input == nullptr || output == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *output = nullptr;
    if (!ContentValid(input->content) || input->providers == nullptr ||
        input->provider_count == 0u ||
        input->provider_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        input->maximum_spans == 0u ||
        input->maximum_spans > static_cast<std::uint64_t>(SIZE_MAX) ||
        input->maximum_depth == 0u || input->flags != 0u) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }

    std::set<Fingerprint> provider_fingerprints;
    for (std::uint64_t index = 0u; index < input->provider_count; ++index) {
        if (!ProviderValid(input->providers[index]) ||
            !provider_fingerprints.insert(Key(input->providers[index].provider_fingerprint)).second) {
            return LAPLACE_DECOMPOSITION_PROVIDER_INVALID;
        }
    }

    auto* result = new (std::nothrow) laplace_decomposition_result{};
    if (result == nullptr) return LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
    try {
        result->spans.reserve(std::min<std::size_t>(
            static_cast<std::size_t>(input->maximum_spans), 4096u));
        laplace_decomposition_span root{};
        root.byte_start = 0u;
        root.byte_end = input->content.byte_count;
        root.parent_span_index = std::numeric_limits<std::uint64_t>::max();
        root.kind = 0u;
        root.depth = 0u;
        root.flags = LAPLACE_DECOMPOSITION_SPAN_REDISPATCH |
            (RootIsText(input->content) ? LAPLACE_DECOMPOSITION_SPAN_TEXT : 0u) |
            (input->content.media_type_byte_count != 0u
                 ? LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT : 0u);
        result->spans.push_back(root);

        std::deque<Task> queue;
        queue.push_back(Task{0u, std::numeric_limits<std::uint64_t>::max()});
        std::set<std::tuple<
            std::uint64_t,
            std::uint64_t,
            std::uint64_t,
            std::uint32_t>> executions;
        std::set<EmittedKey> emitted;

        while (!queue.empty()) {
            const Task task = queue.front();
            queue.pop_front();
            if (task.span_index >= result->spans.size()) {
                delete result;
                return LAPLACE_DECOMPOSITION_RANGE_INVALID;
            }
            const laplace_decomposition_span span =
                result->spans[static_cast<std::size_t>(task.span_index)];
            const std::uint32_t applicability_class = span.flags &
                (LAPLACE_DECOMPOSITION_SPAN_TEXT |
                 LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT);
            for (std::uint64_t provider_index = 0u;
                 provider_index < input->provider_count; ++provider_index) {
                if (provider_index == task.skip_provider) continue;
                const auto execution_key = std::tuple{
                    provider_index, span.byte_start, span.byte_end,
                    applicability_class};
                if (!executions.insert(execution_key).second) continue;
                ++result->summary.provider_execution_count;
                int applicable = 0;
                const auto applicable_status = input->providers[provider_index].applicable(
                    input->providers[provider_index].state,
                    &input->content,
                    &span,
                    &applicable);
                if (applicable_status != LAPLACE_DECOMPOSITION_OK ||
                    (applicable != 0 && applicable != 1)) {
                    delete result;
                    return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                }
                if (applicable == 0) continue;
                ++result->summary.applicable_execution_count;
                ApplyContext context{
                    result,
                    input,
                    task.span_index,
                    provider_index,
                    &queue,
                    &emitted,
                    LAPLACE_DECOMPOSITION_OK};
                const auto apply_status = input->providers[provider_index].apply(
                    input->providers[provider_index].state,
                    &input->content,
                    &span,
                    Emit,
                    &context);
                if (apply_status != LAPLACE_DECOMPOSITION_OK ||
                    context.status != LAPLACE_DECOMPOSITION_OK) {
                    const auto status = context.status != LAPLACE_DECOMPOSITION_OK
                        ? context.status : LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                    delete result;
                    return status;
                }
            }
        }
    } catch (...) {
        delete result;
        return LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
    }
    result->summary.span_count = static_cast<std::uint64_t>(result->spans.size());
    result->summary.status = LAPLACE_DECOMPOSITION_OK;
    *output = result;
    return LAPLACE_DECOMPOSITION_OK;
}

extern "C" const laplace_decomposition_span* laplace_decomposition_spans(
    const laplace_decomposition_result* result,
    std::size_t* span_count) {
    if (span_count == nullptr) return nullptr;
    *span_count = result == nullptr ? 0u : result->spans.size();
    if (result == nullptr || result->spans.empty()) return nullptr;
    return result->spans.data();
}

extern "C" laplace_decomposition_status laplace_decomposition_summary_get(
    const laplace_decomposition_result* result,
    laplace_decomposition_summary* summary) {
    if (result == nullptr || summary == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *summary = result->summary;
    return LAPLACE_DECOMPOSITION_OK;
}

extern "C" void laplace_decomposition_result_destroy(
    laplace_decomposition_result** result) {
    if (result == nullptr) return;
    delete *result;
    *result = nullptr;
}
