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
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

struct laplace_decomposition_result {
    std::vector<laplace_decomposition_span> spans;
    std::vector<std::string> media_types;
    laplace_decomposition_summary summary{};
};

namespace {

using Fingerprint = std::array<std::uint8_t, 32>;

constexpr std::uint32_t RedispatchFlag =
    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_REDISPATCH);
constexpr std::uint32_t TextFlag =
    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT);
constexpr std::uint32_t GrammarInputFlag =
    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT);
constexpr std::uint32_t KnownSpanFlags = RedispatchFlag | TextFlag | GrammarInputFlag;
constexpr std::uint32_t ApplicabilityFlags = TextFlag | GrammarInputFlag;

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

std::string MediaType(const laplace_decomposition_content& content) {
    if (content.media_type == nullptr || content.media_type_byte_count == 0u) {
        return {};
    }
    return std::string(
        content.media_type,
        static_cast<std::size_t>(content.media_type_byte_count));
}

struct Task {
    std::uint64_t span_index;
    std::uint64_t skip_provider;
    std::string media_type;
};

using EmittedKey = std::tuple<
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t>;

using ExecutionKey = std::tuple<
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::uint32_t,
    std::string>;

struct ApplyContext {
    laplace_decomposition_result* result;
    const laplace_decomposition_input* input;
    const laplace_decomposition_content* content;
    laplace_decomposition_media_type_resolver_fn media_resolver;
    void* media_resolver_state;
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

    std::string child_media_type;
    try {
        child_media_type = MediaType(*context.content);
    } catch (...) {
        context.status = LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
        return 1;
    }
    if (context.media_resolver != nullptr) {
        const char* resolved_media_type = nullptr;
        std::uint64_t resolved_media_type_byte_count = 0u;
        const auto resolver_status = context.media_resolver(
            context.media_resolver_state,
            context.content,
            &parent,
            &span.provider_fingerprint,
            byte_start,
            byte_end,
            kind,
            flags,
            &resolved_media_type,
            &resolved_media_type_byte_count);
        if (resolver_status != LAPLACE_DECOMPOSITION_OK) {
            context.status = resolver_status;
            return 1;
        }
        if ((resolved_media_type == nullptr) !=
                (resolved_media_type_byte_count == 0u) ||
            resolved_media_type_byte_count >
                static_cast<std::uint64_t>(SIZE_MAX)) {
            context.status = LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
            return 1;
        }
        if (resolved_media_type != nullptr) {
            try {
                child_media_type.assign(
                    resolved_media_type,
                    static_cast<std::size_t>(resolved_media_type_byte_count));
            } catch (...) {
                context.status = LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
                return 1;
            }
        }
    }

    const std::uint64_t span_index =
        static_cast<std::uint64_t>(context.result->spans.size());
    try {
        context.result->spans.push_back(span);
        context.result->media_types.push_back(child_media_type);
        if ((flags & RedispatchFlag) != 0u) {
            context.queue->push_back(Task{
                span_index,
                context.provider_index,
                std::move(child_media_type)});
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

laplace_decomposition_status Run(
    const laplace_decomposition_input* input,
    laplace_decomposition_media_type_resolver_fn media_resolver,
    void* media_resolver_state,
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
        const std::size_t reserve_count = std::min<std::size_t>(
            static_cast<std::size_t>(input->maximum_spans), 4096u);
        result->spans.reserve(reserve_count);
        result->media_types.reserve(reserve_count);
        laplace_decomposition_span root{};
        root.byte_start = 0u;
        root.byte_end = input->content.byte_count;
        root.parent_span_index = std::numeric_limits<std::uint64_t>::max();
        root.kind = 0u;
        root.depth = 0u;
        root.flags = RedispatchFlag |
            (RootIsText(input->content) ? TextFlag : 0u) |
            (input->content.media_type_byte_count != 0u ? GrammarInputFlag : 0u);
        result->spans.push_back(root);
        result->media_types.push_back(MediaType(input->content));

        std::deque<Task> queue;
        queue.push_back(Task{
            0u,
            std::numeric_limits<std::uint64_t>::max(),
            result->media_types.front()});
        std::set<ExecutionKey> executions;
        std::set<EmittedKey> emitted;

        while (!queue.empty()) {
            Task task = std::move(queue.front());
            queue.pop_front();
            if (task.span_index >= result->spans.size()) {
                delete result;
                return LAPLACE_DECOMPOSITION_RANGE_INVALID;
            }
            const laplace_decomposition_span span =
                result->spans[static_cast<std::size_t>(task.span_index)];
            laplace_decomposition_content task_content = input->content;
            task_content.media_type =
                task.media_type.empty() ? nullptr : task.media_type.data();
            task_content.media_type_byte_count =
                static_cast<std::uint64_t>(task.media_type.size());
            const std::uint32_t applicability_class = span.flags & ApplicabilityFlags;
            for (std::uint64_t provider_index = 0u;
                 provider_index < input->provider_count; ++provider_index) {
                if (provider_index == task.skip_provider) continue;
                const ExecutionKey execution_key{
                    provider_index,
                    span.byte_start,
                    span.byte_end,
                    span.kind,
                    applicability_class,
                    task.media_type};
                if (!executions.insert(execution_key).second) continue;
                ++result->summary.provider_execution_count;
                int applicable = 0;
                const auto applicable_status = input->providers[provider_index].applicable(
                    input->providers[provider_index].state,
                    &task_content,
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
                    &task_content,
                    media_resolver,
                    media_resolver_state,
                    task.span_index,
                    provider_index,
                    &queue,
                    &emitted,
                    LAPLACE_DECOMPOSITION_OK};
                const auto apply_status = input->providers[provider_index].apply(
                    input->providers[provider_index].state,
                    &task_content,
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
    if (result->spans.size() != result->media_types.size()) {
        delete result;
        return LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
    }
    result->summary.span_count = static_cast<std::uint64_t>(result->spans.size());
    result->summary.status = static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_OK);
    *output = result;
    return LAPLACE_DECOMPOSITION_OK;
}

}  // namespace

extern "C" laplace_decomposition_status laplace_decomposition_run(
    const laplace_decomposition_input* input,
    laplace_decomposition_result** output) {
    return Run(input, nullptr, nullptr, output);
}

extern "C" laplace_decomposition_status laplace_decomposition_run_with_media_resolver(
    const laplace_decomposition_input* input,
    laplace_decomposition_media_type_resolver_fn resolver,
    void* resolver_state,
    laplace_decomposition_result** output) {
    return Run(input, resolver, resolver_state, output);
}

extern "C" const laplace_decomposition_span* laplace_decomposition_spans(
    const laplace_decomposition_result* result,
    std::size_t* span_count) {
    if (span_count == nullptr) return nullptr;
    *span_count = result == nullptr ? 0u : result->spans.size();
    if (result == nullptr || result->spans.empty()) return nullptr;
    return result->spans.data();
}

extern "C" const char* laplace_decomposition_span_media_type(
    const laplace_decomposition_result* result,
    const std::size_t span_index,
    std::size_t* byte_count) {
    if (byte_count == nullptr) return nullptr;
    *byte_count = 0u;
    if (result == nullptr || span_index >= result->media_types.size()) return nullptr;
    const std::string& media_type = result->media_types[span_index];
    *byte_count = media_type.size();
    return media_type.empty() ? nullptr : media_type.data();
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