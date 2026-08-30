#include "laplace/decomposition_composition.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blake3.h"

struct laplace_decomposition_composition_plan {
    laplace_decomposition_composition_plan_view view{};
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
};

namespace {

constexpr std::uint32_t RecipeVersion = 1u;
constexpr std::uint64_t NoResultIndex = std::numeric_limits<std::uint64_t>::max();

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

bool AddU64(std::uint64_t left, std::uint64_t right, std::uint64_t& output) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) return false;
    output = left + right;
    return true;
}

void HashU32(blake3_hasher& hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashBytes(blake3_hasher& hasher, const void* bytes, const std::size_t count) {
    HashU64(hasher, static_cast<std::uint64_t>(count));
    if (count != 0u) blake3_hasher_update(&hasher, bytes, count);
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool DecodeUtf8(
    const std::uint8_t* bytes,
    const std::size_t byte_count,
    std::vector<std::uint32_t>& output) {
    std::size_t offset = 0u;
    while (offset < byte_count) {
        const std::uint8_t first = bytes[offset];
        std::uint32_t value = 0u;
        std::size_t width = 0u;
        std::uint32_t minimum = 0u;
        if (first <= 0x7fu) {
            value = first;
            width = 1u;
        } else if ((first & 0xe0u) == 0xc0u) {
            value = static_cast<std::uint32_t>(first & 0x1fu);
            width = 2u;
            minimum = 0x80u;
        } else if ((first & 0xf0u) == 0xe0u) {
            value = static_cast<std::uint32_t>(first & 0x0fu);
            width = 3u;
            minimum = 0x800u;
        } else if ((first & 0xf8u) == 0xf0u) {
            value = static_cast<std::uint32_t>(first & 0x07u);
            width = 4u;
            minimum = 0x10000u;
        } else {
            return false;
        }
        if (width > byte_count - offset) return false;
        for (std::size_t index = 1u; index < width; ++index) {
            const std::uint8_t continuation = bytes[offset + index];
            if ((continuation & 0xc0u) != 0x80u) return false;
            value = (value << 6u) |
                static_cast<std::uint32_t>(continuation & 0x3fu);
        }
        if (value < minimum || value > 0x10ffffu ||
            (value >= 0xd800u && value <= 0xdfffu)) {
            return false;
        }
        output.push_back(value);
        offset += width;
    }
    return true;
}

laplace_composition_operand KnownEntityReference(const std::uint64_t index) {
    return laplace_composition_operand{
        index,
        1u,
        0u,
        LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY,
        0u};
}

laplace_composition_operand PriorResultReference(const std::uint64_t index) {
    return laplace_composition_operand{
        index,
        1u,
        0u,
        LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT,
        0u};
}

class Builder final {
public:
    Builder(
        laplace_decomposition_composition_plan& plan,
        const laplace_decomposition_composition_input& input)
        : plan_(plan), input_(input) {}

    laplace_decomposition_composition_status Build() {
        std::size_t span_count = 0u;
        const laplace_decomposition_span* spans =
            laplace_decomposition_spans(input_.decomposition, &span_count);
        if (spans == nullptr || span_count == 0u ||
            input_.content == nullptr || input_.content->bytes == nullptr ||
            input_.content->byte_count == 0u ||
            input_.content->byte_count > static_cast<std::uint64_t>(SIZE_MAX)) {
            return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
        }
        laplace_decomposition_summary summary{};
        if (laplace_decomposition_summary_get(input_.decomposition, &summary) !=
                LAPLACE_DECOMPOSITION_OK ||
            summary.span_count != static_cast<std::uint64_t>(span_count)) {
            return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
        }

        blake3_hasher trace_hasher;
        blake3_hasher_init(&trace_hasher);
        static constexpr std::string_view TraceDomain{
            "laplace.decomposition.composition.trace/v1"};
        HashBytes(trace_hasher, TraceDomain.data(), TraceDomain.size());
        HashBytes(
            trace_hasher,
            input_.content->bytes,
            static_cast<std::size_t>(input_.content->byte_count));
        HashU64(trace_hasher, static_cast<std::uint64_t>(span_count));

        for (std::size_t span_index = 0u; span_index < span_count; ++span_index) {
            const laplace_decomposition_span& span = spans[span_index];
            if (span.byte_start >= span.byte_end ||
                span.byte_end > input_.content->byte_count ||
                (span_index == 0u &&
                 span.parent_span_index !=
                     std::numeric_limits<std::uint64_t>::max()) ||
                (span_index != 0u &&
                 span.parent_span_index >= static_cast<std::uint64_t>(span_index))) {
                return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
            }
            std::size_t media_type_bytes = 0u;
            const char* media_type = laplace_decomposition_span_media_type(
                input_.decomposition, span_index, &media_type_bytes);
            if ((media_type == nullptr) != (media_type_bytes == 0u)) {
                return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
            }

            HashU64(trace_hasher, static_cast<std::uint64_t>(span_index));
            HashU64(trace_hasher, span.byte_start);
            HashU64(trace_hasher, span.byte_end);
            HashU64(trace_hasher, span.parent_span_index);
            HashBytes(
                trace_hasher,
                span.provider_fingerprint.bytes,
                sizeof(span.provider_fingerprint.bytes));
            HashU64(trace_hasher, span.kind);
            HashU32(trace_hasher, span.depth);
            HashU32(trace_hasher, span.flags);
            HashBytes(trace_hasher, media_type, media_type_bytes);
        }

        auto root = Text(
            input_.content->bytes,
            static_cast<std::size_t>(input_.content->byte_count));
        if (!root.has_value()) return status_;

#if defined(LAPLACE_TEST_DECOMPOSITION_COMPOSITION_COUPLE_WITNESS)
        const auto witness = Text(std::to_string(spans[span_count - 1u].kind));
        if (!witness.has_value()) return status_;
        root = Couple(*root, *witness);
        if (!root.has_value()) return status_;
#endif

        plan_.view.trace_fingerprint = Finish(trace_hasher);
        plan_.view.root_reference = *root;
        plan_.view.root_result_index =
            root->reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT
                ? root->reference_index
                : NoResultIndex;
        plan_.view.span_count = static_cast<std::uint64_t>(span_count);
        Bind();
        return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
    }

private:
    std::uint64_t AtomIndex(const std::uint32_t position) {
        const auto found = atom_indexes_.find(position);
        if (found != atom_indexes_.end()) return found->second;
        const std::uint64_t index =
            static_cast<std::uint64_t>(plan_.atom_positions.size());
        plan_.atom_positions.push_back(position);
        atom_indexes_.emplace(position, index);
        return index;
    }

    std::optional<laplace_composition_operand> Text(
        const std::uint8_t* bytes,
        const std::size_t byte_count) {
        if (bytes == nullptr || byte_count == 0u) {
            status_ = LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID;
            return std::nullopt;
        }
        const std::string key(
            reinterpret_cast<const char*>(bytes), byte_count);
        const auto found = text_references_.find(key);
        if (found != text_references_.end()) return found->second;

        std::vector<std::uint32_t> positions;
        if (!DecodeUtf8(bytes, byte_count, positions) || positions.empty()) {
            status_ = LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID;
            return std::nullopt;
        }

#if !defined(LAPLACE_TEST_DECOMPOSITION_COMPOSITION_REMATERIALIZE_ATOM)
        if (positions.size() == 1u) {
            const auto reference = KnownEntityReference(AtomIndex(positions.front()));
            text_references_.emplace(key, reference);
            return reference;
        }
#endif

        const std::uint64_t first =
            static_cast<std::uint64_t>(plan_.operands.size());
        for (const std::uint32_t position : positions) {
            plan_.operands.push_back(KnownEntityReference(AtomIndex(position)));
        }
        const auto result = AddRequest(first, positions.size());
        if (!result.has_value()) return std::nullopt;
        const auto reference = PriorResultReference(*result);
        text_references_.emplace(key, reference);
        return reference;
    }

#if defined(LAPLACE_TEST_DECOMPOSITION_COMPOSITION_COUPLE_WITNESS)
    std::optional<laplace_composition_operand> Couple(
        const laplace_composition_operand& content,
        const laplace_composition_operand& witness) {
        const std::uint64_t first =
            static_cast<std::uint64_t>(plan_.operands.size());
        plan_.operands.push_back(content);
        plan_.operands.push_back(witness);
        const auto result = AddRequest(first, 2u);
        if (!result.has_value()) return std::nullopt;
        return PriorResultReference(*result);
    }
#endif

    std::optional<std::uint64_t> AddRequest(
        const std::uint64_t first,
        const std::size_t count) {
        if (count == 0u ||
            plan_.requests.size() >=
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint64_t>::max())) {
            status_ = LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW;
            return std::nullopt;
        }
        const std::uint64_t index =
            static_cast<std::uint64_t>(plan_.requests.size());
        std::uint64_t source_ordinal = 0u;
        if (!AddU64(input_.source_ordinal_base, index + 1u, source_ordinal)) {
            status_ = LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW;
            return std::nullopt;
        }
        plan_.requests.push_back(laplace_composition_request{
            first,
            static_cast<std::uint64_t>(count),
            source_ordinal,
            RecipeVersion,
            0u,
            input_.recipe_fingerprint,
            input_.geometry_epoch,
            input_.occurrence_context_fingerprint});
        return index;
    }

    void Bind() {
        plan_.view.atom_positions =
            plan_.atom_positions.empty() ? nullptr : plan_.atom_positions.data();
        plan_.view.operands =
            plan_.operands.empty() ? nullptr : plan_.operands.data();
        plan_.view.requests =
            plan_.requests.empty() ? nullptr : plan_.requests.data();
        plan_.view.atom_count =
            static_cast<std::uint64_t>(plan_.atom_positions.size());
        plan_.view.operand_count =
            static_cast<std::uint64_t>(plan_.operands.size());
        plan_.view.request_count =
            static_cast<std::uint64_t>(plan_.requests.size());
        plan_.view.recipe_version = RecipeVersion;
    }

    laplace_decomposition_composition_plan& plan_;
    const laplace_decomposition_composition_input& input_;
    std::map<std::uint32_t, std::uint64_t> atom_indexes_;
    std::map<std::string, laplace_composition_operand> text_references_;
    laplace_decomposition_composition_status status_{
        LAPLACE_DECOMPOSITION_COMPOSITION_OK};
};

bool InputValid(const laplace_decomposition_composition_input& input) {
    return input.content != nullptr && input.decomposition != nullptr &&
        input.content->bytes != nullptr && input.content->byte_count != 0u &&
        input.content->byte_count <= static_cast<std::uint64_t>(SIZE_MAX) &&
        !DigestZero(input.recipe_fingerprint) &&
        !DigestZero(input.geometry_epoch) &&
        !DigestZero(input.occurrence_context_fingerprint) &&
        input.flags == 0u && input.reserved == 0u;
}

bool RootReferenceValid(const laplace_decomposition_composition_plan_view& view) {
    const laplace_composition_operand& root = view.root_reference;
    if (root.multiplicity != 1u || root.relationship_metadata != 0u ||
        root.flags != 0u) {
        return false;
    }
    if (root.reference_kind == LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
        return root.reference_index < view.atom_count &&
            view.root_result_index == NoResultIndex;
    }
    if (root.reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
        return root.reference_index < view.request_count &&
            root.reference_index == view.root_result_index &&
            root.reference_index + 1u == view.request_count;
    }
    return false;
}

}  // namespace

extern "C" laplace_decomposition_composition_status
laplace_decomposition_composition_plan_create(
    const laplace_decomposition_composition_input* input,
    laplace_decomposition_composition_plan** plan) {
    if (input == nullptr || plan == nullptr || *plan != nullptr ||
        !InputValid(*input)) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    try {
        auto* created = new laplace_decomposition_composition_plan{};
        Builder builder(*created, *input);
        const auto status = builder.Build();
        if (status != LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
            delete created;
            return status;
        }
        *plan = created;
        return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE;
    }
}

extern "C" laplace_decomposition_composition_status
laplace_decomposition_composition_plan_view_get(
    const laplace_decomposition_composition_plan* plan,
    laplace_decomposition_composition_plan_view* view) {
    if (plan == nullptr || view == nullptr ||
        plan->view.atom_positions == nullptr ||
        plan->view.atom_count == 0u ||
        (plan->view.operand_count != 0u && plan->view.operands == nullptr) ||
        (plan->view.request_count != 0u && plan->view.requests == nullptr) ||
        plan->view.span_count == 0u ||
        !RootReferenceValid(plan->view)) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    *view = plan->view;
    return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
}

extern "C" void laplace_decomposition_composition_plan_destroy(
    laplace_decomposition_composition_plan** plan) {
    if (plan == nullptr) return;
    delete *plan;
    *plan = nullptr;
}