#include "laplace/decomposition_composition.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <unordered_map>
#include <vector>

namespace {

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

bool DecodeUtf8(
    const std::uint8_t* bytes,
    const std::size_t count,
    std::vector<std::uint32_t>& output) {
    output.clear();
    output.reserve(count);
    std::size_t offset = 0u;
    while (offset < count) {
        const std::uint8_t first = bytes[offset];
        std::uint32_t cp = 0u;
        std::size_t width = 0u;
        std::uint32_t minimum = 0u;
        if (first <= 0x7fu) {
            cp = first;
            width = 1u;
        } else if ((first & 0xe0u) == 0xc0u) {
            cp = static_cast<std::uint32_t>(first & 0x1fu);
            width = 2u;
            minimum = 0x80u;
        } else if ((first & 0xf0u) == 0xe0u) {
            cp = static_cast<std::uint32_t>(first & 0x0fu);
            width = 3u;
            minimum = 0x800u;
        } else if ((first & 0xf8u) == 0xf0u) {
            cp = static_cast<std::uint32_t>(first & 0x07u);
            width = 4u;
            minimum = 0x10000u;
        } else {
            return false;
        }
        if (width > count - offset) return false;
        for (std::size_t index = 1u; index < width; ++index) {
            const std::uint8_t continuation = bytes[offset + index];
            if ((continuation & 0xc0u) != 0x80u) return false;
            cp = (cp << 6u) | static_cast<std::uint32_t>(continuation & 0x3fu);
        }
        if (cp < minimum || cp > 0x10ffffu ||
            (cp >= 0xd800u && cp <= 0xdfffu)) {
            return false;
        }
        output.push_back(cp);
        offset += width;
    }
    return true;
}

}  // namespace

struct laplace_decomposition_composition_plan {
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    std::vector<laplace_decomposition_composition_occurrence> occurrences;
};

extern "C" laplace_decomposition_composition_status
laplace_decomposition_composition_plan_create(
    const laplace_decomposition_composition_input* input,
    laplace_decomposition_composition_plan** output) {
    if (input == nullptr || output == nullptr) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    *output = nullptr;
    if (input->decomposition == nullptr || input->bytes == nullptr ||
        input->byte_count == 0u ||
        input->byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        input->first_source_ordinal == 0u || input->recipe_version == 0u ||
        input->flags != 0u || DigestZero(input->recipe_fingerprint) ||
        DigestZero(input->geometry_epoch) ||
        DigestZero(input->occurrence_context_fingerprint)) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }

    std::size_t span_count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(input->decomposition, &span_count);
    if (spans == nullptr || span_count == 0u) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }

    auto* plan = new (std::nothrow) laplace_decomposition_composition_plan{};
    if (plan == nullptr) return LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE;

    try {
        std::unordered_map<std::uint32_t, std::uint64_t> atom_indexes;
        atom_indexes.reserve(4096u);
        plan->requests.reserve(span_count);
        plan->occurrences.reserve(span_count);
        std::vector<std::uint32_t> decoded;
        std::uint64_t source_ordinal = input->first_source_ordinal;
        const std::uint32_t text_flag =
            static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT);

        for (std::size_t span_index = 0u; span_index < span_count; ++span_index) {
            const laplace_decomposition_span& span = spans[span_index];
            if ((span.flags & text_flag) == 0u) continue;
            if (span.byte_start >= span.byte_end || span.byte_end > input->byte_count ||
                span.byte_start > static_cast<std::uint64_t>(SIZE_MAX) ||
                span.byte_end > static_cast<std::uint64_t>(SIZE_MAX)) {
                delete plan;
                return LAPLACE_DECOMPOSITION_COMPOSITION_RANGE_INVALID;
            }
            const std::size_t byte_start = static_cast<std::size_t>(span.byte_start);
            const std::size_t byte_end = static_cast<std::size_t>(span.byte_end);
            if (!DecodeUtf8(
                    input->bytes + byte_start,
                    byte_end - byte_start,
                    decoded) || decoded.empty()) {
                delete plan;
                return LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID;
            }
            const std::uint64_t first_operand =
                static_cast<std::uint64_t>(plan->operands.size());
            for (const std::uint32_t cp : decoded) {
                const auto found = atom_indexes.find(cp);
                std::uint64_t atom_index = 0u;
                if (found == atom_indexes.end()) {
                    atom_index = static_cast<std::uint64_t>(plan->atom_positions.size());
                    plan->atom_positions.push_back(cp);
                    atom_indexes.emplace(cp, atom_index);
                } else {
                    atom_index = found->second;
                }
                plan->operands.push_back(laplace_composition_operand{
                    atom_index,
                    1u,
                    0u,
                    LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY,
                    0u});
            }
            const std::uint64_t result_index =
                static_cast<std::uint64_t>(plan->requests.size());
            plan->requests.push_back(laplace_composition_request{
                first_operand,
                static_cast<std::uint64_t>(decoded.size()),
                source_ordinal,
                input->recipe_version,
                0u,
                input->recipe_fingerprint,
                input->geometry_epoch,
                input->occurrence_context_fingerprint});
            plan->occurrences.push_back(laplace_decomposition_composition_occurrence{
                static_cast<std::uint64_t>(span_index),
                result_index,
                span.parent_span_index,
                span.byte_start,
                span.byte_end,
                span.provider_fingerprint,
                span.kind,
                span.depth,
                span.flags});
            if (source_ordinal == std::numeric_limits<std::uint64_t>::max()) {
                delete plan;
                return LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW;
            }
            ++source_ordinal;
        }
    } catch (...) {
        delete plan;
        return LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE;
    }

    if (plan->requests.empty()) {
        delete plan;
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    *output = plan;
    return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
}

extern "C" laplace_decomposition_composition_status
laplace_decomposition_composition_plan_view_get(
    const laplace_decomposition_composition_plan* plan,
    laplace_decomposition_composition_plan_view* view) {
    if (plan == nullptr || view == nullptr) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    *view = laplace_decomposition_composition_plan_view{};
    view->atom_positions = plan->atom_positions.empty() ? nullptr : plan->atom_positions.data();
    view->operands = plan->operands.empty() ? nullptr : plan->operands.data();
    view->requests = plan->requests.empty() ? nullptr : plan->requests.data();
    view->occurrences = plan->occurrences.empty() ? nullptr : plan->occurrences.data();
    view->atom_count = static_cast<std::uint64_t>(plan->atom_positions.size());
    view->operand_count = static_cast<std::uint64_t>(plan->operands.size());
    view->request_count = static_cast<std::uint64_t>(plan->requests.size());
    view->occurrence_count = static_cast<std::uint64_t>(plan->occurrences.size());
    return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
}

extern "C" void laplace_decomposition_composition_plan_destroy(
    laplace_decomposition_composition_plan** plan) {
    if (plan == nullptr) return;
    delete *plan;
    *plan = nullptr;
}
