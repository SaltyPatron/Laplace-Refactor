#include "laplace/text_decomposition.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct SpanCollector {
    std::vector<laplace_uax29_span>* spans;
};

int CollectSpan(void* opaque, const laplace_uax29_span* span) {
    if (opaque == nullptr || span == nullptr) return 1;
    auto* collector = static_cast<SpanCollector*>(opaque);
    try {
        collector->spans->push_back(*span);
    } catch (...) {
        return 1;
    }
    return 0;
}

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

bool DecodeCodepoints(
    const std::uint8_t* bytes,
    std::size_t count,
    std::vector<std::uint32_t>& codepoints) {
    codepoints.clear();
    codepoints.reserve(count);
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
            (cp >= 0xd800u && cp <= 0xdfffu)) return false;
        codepoints.push_back(cp);
        offset += width;
    }
    return true;
}

}  // namespace

struct laplace_text_decomposition_plan {
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    std::vector<laplace_text_decomposition_span_result> spans;
    std::uint64_t grapheme_count{};
    std::uint64_t word_count{};
    std::uint64_t sentence_count{};
};

extern "C" laplace_text_decomposition_status laplace_text_decomposition_plan_create(
    const laplace_text_decomposition_input* input,
    laplace_text_decomposition_plan** output) {
    if (input == nullptr || output == nullptr) {
        return LAPLACE_TEXT_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *output = nullptr;
    if (input->uax29 == nullptr || input->utf8 == nullptr ||
        input->byte_count == 0u ||
        input->byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        input->first_source_ordinal == 0u || input->recipe_version == 0u ||
        input->flags != 0u || DigestZero(input->recipe_fingerprint) ||
        DigestZero(input->geometry_epoch) ||
        DigestZero(input->occurrence_context_fingerprint)) {
        return LAPLACE_TEXT_DECOMPOSITION_INVALID_ARGUMENT;
    }

    const std::size_t byte_count = static_cast<std::size_t>(input->byte_count);
    std::vector<std::uint32_t> codepoints;
    try {
        if (!DecodeCodepoints(input->utf8, byte_count, codepoints) || codepoints.empty()) {
            return LAPLACE_TEXT_DECOMPOSITION_UTF8_INVALID;
        }
    } catch (...) {
        return LAPLACE_TEXT_DECOMPOSITION_MEMORY_FAILURE;
    }

    auto* plan = new (std::nothrow) laplace_text_decomposition_plan{};
    if (plan == nullptr) return LAPLACE_TEXT_DECOMPOSITION_MEMORY_FAILURE;

    try {
        std::vector<laplace_uax29_span> authority_spans;
        authority_spans.reserve(codepoints.size() * 3u);
        SpanCollector collector{&authority_spans};
        laplace_uax29_summary summary{};

        auto segment = [&](const laplace_uax29_boundary_kind kind) {
            const std::size_t before = authority_spans.size();
            const laplace_uax29_status status = laplace_uax29_segment(
                input->uax29, input->utf8, byte_count, kind,
                CollectSpan, &collector, &summary);
            if (status != LAPLACE_UAX29_OK) return false;
            const std::uint64_t count = static_cast<std::uint64_t>(authority_spans.size() - before);
            if (kind == LAPLACE_UAX29_GRAPHEME) plan->grapheme_count = count;
            else if (kind == LAPLACE_UAX29_WORD) plan->word_count = count;
            else if (kind == LAPLACE_UAX29_SENTENCE) plan->sentence_count = count;
            return true;
        };

        if (!segment(LAPLACE_UAX29_GRAPHEME) ||
            !segment(LAPLACE_UAX29_WORD) ||
            !segment(LAPLACE_UAX29_SENTENCE)) {
            delete plan;
            return LAPLACE_TEXT_DECOMPOSITION_UAX29_FAILURE;
        }

        std::unordered_map<std::uint32_t, std::uint64_t> atom_indexes;
        atom_indexes.reserve(codepoints.size());
        plan->atom_positions.reserve(codepoints.size());
        plan->requests.reserve(authority_spans.size());
        plan->spans.reserve(authority_spans.size());

        std::uint64_t source_ordinal = input->first_source_ordinal;
        for (const laplace_uax29_span& span : authority_spans) {
            if (span.codepoint_start >= span.codepoint_end ||
                span.codepoint_end > static_cast<std::uint64_t>(codepoints.size()) ||
                span.byte_start >= span.byte_end || span.byte_end > input->byte_count) {
                delete plan;
                return LAPLACE_TEXT_DECOMPOSITION_UAX29_FAILURE;
            }
            const std::uint64_t first_operand =
                static_cast<std::uint64_t>(plan->operands.size());
            for (std::uint64_t cp_index = span.codepoint_start;
                 cp_index < span.codepoint_end; ++cp_index) {
                const std::uint32_t cp = codepoints[static_cast<std::size_t>(cp_index)];
                auto found = atom_indexes.find(cp);
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
            const std::uint64_t operand_count = span.codepoint_end - span.codepoint_start;
            plan->requests.push_back(laplace_composition_request{
                first_operand,
                operand_count,
                source_ordinal,
                input->recipe_version,
                0u,
                input->recipe_fingerprint,
                input->geometry_epoch,
                input->occurrence_context_fingerprint});
            const std::uint64_t request_index =
                static_cast<std::uint64_t>(plan->requests.size() - 1u);
            plan->spans.push_back(laplace_text_decomposition_span_result{
                span, request_index});
            if (source_ordinal == std::numeric_limits<std::uint64_t>::max()) {
                delete plan;
                return LAPLACE_TEXT_DECOMPOSITION_OVERFLOW;
            }
            ++source_ordinal;
        }
    } catch (...) {
        delete plan;
        return LAPLACE_TEXT_DECOMPOSITION_MEMORY_FAILURE;
    }

    *output = plan;
    return LAPLACE_TEXT_DECOMPOSITION_OK;
}

extern "C" laplace_text_decomposition_status laplace_text_decomposition_plan_view_get(
    const laplace_text_decomposition_plan* plan,
    laplace_text_decomposition_plan_view* view) {
    if (plan == nullptr || view == nullptr) {
        return LAPLACE_TEXT_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *view = laplace_text_decomposition_plan_view{};
    view->atom_positions = plan->atom_positions.empty() ? nullptr : plan->atom_positions.data();
    view->operands = plan->operands.empty() ? nullptr : plan->operands.data();
    view->requests = plan->requests.empty() ? nullptr : plan->requests.data();
    view->spans = plan->spans.empty() ? nullptr : plan->spans.data();
    view->atom_count = static_cast<std::uint64_t>(plan->atom_positions.size());
    view->operand_count = static_cast<std::uint64_t>(plan->operands.size());
    view->request_count = static_cast<std::uint64_t>(plan->requests.size());
    view->span_count = static_cast<std::uint64_t>(plan->spans.size());
    view->grapheme_count = plan->grapheme_count;
    view->word_count = plan->word_count;
    view->sentence_count = plan->sentence_count;
    return LAPLACE_TEXT_DECOMPOSITION_OK;
}

extern "C" void laplace_text_decomposition_plan_destroy(
    laplace_text_decomposition_plan** plan) {
    if (plan == nullptr) return;
    delete *plan;
    *plan = nullptr;
}
