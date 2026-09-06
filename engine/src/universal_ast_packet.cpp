#include "laplace/universal_ast.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

#include "blake3.h"

namespace {

constexpr std::uint32_t PacketMagic = UINT32_C(0x5453414c); /* LAST */
constexpr std::size_t DigestWords = 8u;
constexpr std::size_t BindingWords = 68u;
constexpr std::size_t HeaderWords = 59u;
constexpr std::uint64_t NoBinding = UINT64_MAX;

bool DigestZero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0u;
    for (const std::uint8_t byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0u;
}

bool DigestEqual(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool IdZero(const laplace_id128& value) {
    std::uint8_t aggregate = 0u;
    for (const std::uint8_t byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0u;
}

bool CoordinateZero(const laplace_highway_coordinate& value) {
    return IdZero(value.coordinate) && DigestZero(value.collision_fingerprint) &&
        value.kind == 0u && value.reserved == 0u && value.version == 0u;
}

bool CoordinateValid(
    const laplace_highway_coordinate& value,
    const std::uint32_t required_kind) {
    return value.kind == required_kind && value.reserved == 0u &&
        value.version != 0u && !IdZero(value.coordinate) &&
        !DigestZero(value.collision_fingerprint);
}

bool ReferenceZero(const laplace_composition_operand& value) {
    const laplace_composition_operand zero{};
    return std::memcmp(&value, &zero, sizeof(value)) == 0;
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

void HashDigest(blake3_hasher& hasher, const laplace_digest256& value) {
    blake3_hasher_update(&hasher, value.bytes, sizeof(value.bytes));
}

void HashId(blake3_hasher& hasher, const laplace_id128& value) {
    blake3_hasher_update(&hasher, value.bytes, sizeof(value.bytes));
}

void HashCoordinate(blake3_hasher& hasher, const laplace_highway_coordinate& value) {
    HashId(hasher, value.coordinate);
    HashDigest(hasher, value.collision_fingerprint);
    HashU32(hasher, value.kind);
    HashU64(hasher, value.version);
}

void HashReference(blake3_hasher& hasher, const laplace_composition_operand& value) {
    HashU64(hasher, value.reference_index);
    HashU64(hasher, value.multiplicity);
    HashU64(hasher, value.relationship_metadata);
    HashU32(hasher, value.reference_kind);
    HashU32(hasher, value.flags);
}

void BeginHash(blake3_hasher& hasher, const std::string_view domain) {
    blake3_hasher_init(&hasher);
    HashU64(hasher, static_cast<std::uint64_t>(domain.size()));
    blake3_hasher_update(&hasher, domain.data(), domain.size());
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 value{};
    blake3_hasher_finalize(&hasher, value.bytes, sizeof(value.bytes));
    return value;
}

bool AddWords(const std::uint64_t binding_count, std::size_t& required) {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (binding_count >
        static_cast<std::uint64_t>((maximum - HeaderWords) / BindingWords)) {
        return false;
    }
    required = HeaderWords + static_cast<std::size_t>(binding_count) * BindingWords;
    return true;
}

void WriteU64(std::uint32_t*& cursor, const std::uint64_t value) {
    *cursor++ = static_cast<std::uint32_t>(value);
    *cursor++ = static_cast<std::uint32_t>(value >> 32u);
}

std::uint64_t ReadU64(const std::uint32_t*& cursor) {
    const std::uint64_t low = *cursor++;
    const std::uint64_t high = *cursor++;
    return low | (high << 32u);
}

void WriteDigest(std::uint32_t*& cursor, const laplace_digest256& value) {
    for (std::size_t word = 0u; word < DigestWords; ++word) {
        const std::size_t offset = word * 4u;
        *cursor++ =
            static_cast<std::uint32_t>(value.bytes[offset]) |
            (static_cast<std::uint32_t>(value.bytes[offset + 1u]) << 8u) |
            (static_cast<std::uint32_t>(value.bytes[offset + 2u]) << 16u) |
            (static_cast<std::uint32_t>(value.bytes[offset + 3u]) << 24u);
    }
}

laplace_digest256 ReadDigest(const std::uint32_t*& cursor) {
    laplace_digest256 value{};
    for (std::size_t word = 0u; word < DigestWords; ++word) {
        const std::uint32_t bits = *cursor++;
        const std::size_t offset = word * 4u;
        value.bytes[offset] = static_cast<std::uint8_t>(bits);
        value.bytes[offset + 1u] = static_cast<std::uint8_t>(bits >> 8u);
        value.bytes[offset + 2u] = static_cast<std::uint8_t>(bits >> 16u);
        value.bytes[offset + 3u] = static_cast<std::uint8_t>(bits >> 24u);
    }
    return value;
}

void WriteId(std::uint32_t*& cursor, const laplace_id128& value) {
    for (std::size_t word = 0u; word < 4u; ++word) {
        const std::size_t offset = word * 4u;
        *cursor++ =
            static_cast<std::uint32_t>(value.bytes[offset]) |
            (static_cast<std::uint32_t>(value.bytes[offset + 1u]) << 8u) |
            (static_cast<std::uint32_t>(value.bytes[offset + 2u]) << 16u) |
            (static_cast<std::uint32_t>(value.bytes[offset + 3u]) << 24u);
    }
}

laplace_id128 ReadId(const std::uint32_t*& cursor) {
    laplace_id128 value{};
    for (std::size_t word = 0u; word < 4u; ++word) {
        const std::uint32_t bits = *cursor++;
        const std::size_t offset = word * 4u;
        value.bytes[offset] = static_cast<std::uint8_t>(bits);
        value.bytes[offset + 1u] = static_cast<std::uint8_t>(bits >> 8u);
        value.bytes[offset + 2u] = static_cast<std::uint8_t>(bits >> 16u);
        value.bytes[offset + 3u] = static_cast<std::uint8_t>(bits >> 24u);
    }
    return value;
}

void WriteCoordinate(std::uint32_t*& cursor, const laplace_highway_coordinate& value) {
    WriteId(cursor, value.coordinate);
    WriteDigest(cursor, value.collision_fingerprint);
    *cursor++ = value.kind;
    *cursor++ = value.reserved;
    WriteU64(cursor, value.version);
}

laplace_highway_coordinate ReadCoordinate(const std::uint32_t*& cursor) {
    laplace_highway_coordinate value{};
    value.coordinate = ReadId(cursor);
    value.collision_fingerprint = ReadDigest(cursor);
    value.kind = *cursor++;
    value.reserved = *cursor++;
    value.version = ReadU64(cursor);
    return value;
}

void WriteReference(std::uint32_t*& cursor, const laplace_composition_operand& value) {
    WriteU64(cursor, value.reference_index);
    WriteU64(cursor, value.multiplicity);
    WriteU64(cursor, value.relationship_metadata);
    *cursor++ = value.reference_kind;
    *cursor++ = value.flags;
}

laplace_composition_operand ReadReference(const std::uint32_t*& cursor) {
    laplace_composition_operand value{};
    value.reference_index = ReadU64(cursor);
    value.multiplicity = ReadU64(cursor);
    value.relationship_metadata = ReadU64(cursor);
    value.reference_kind = *cursor++;
    value.flags = *cursor++;
    return value;
}

void HashSemanticBinding(
    blake3_hasher& hasher,
    const laplace_universal_ast_binding& binding) {
    HashCoordinate(hasher, binding.grammar_coordinate);
    HashCoordinate(hasher, binding.role_coordinate);
    HashU64(hasher, binding.parent_binding_index);
    HashU64(hasher, binding.sibling_ordinal);
    HashU32(hasher, binding.disposition);
    HashU32(hasher, binding.has_content);
    if (binding.has_content != 0u) HashReference(hasher, binding.content_reference);
}

void HashWitnessBinding(
    blake3_hasher& hasher,
    const laplace_universal_ast_binding& binding) {
    HashDigest(hasher, binding.provider_fingerprint);
    HashU64(hasher, binding.syntax_span_index);
    HashU64(hasher, binding.byte_start);
    HashU64(hasher, binding.byte_end);
    HashU64(hasher, binding.kind);
    HashU64(hasher, binding.grammar_kind);
    HashU64(hasher, binding.field_kind);
    HashU64(hasher, binding.sibling_ordinal);
    HashU32(hasher, binding.syntax_flags);
}

laplace_digest256 ReceiptId(const laplace_universal_ast_packet_receipt& receipt) {
    blake3_hasher hasher;
    BeginHash(hasher, "laplace-universal-ast-packet-receipt-v1");
    HashDigest(hasher, receipt.grammar_registry_fingerprint);
    HashDigest(hasher, receipt.recipe_fingerprint);
    HashDigest(hasher, receipt.plan_fingerprint);
    HashDigest(hasher, receipt.witness_fingerprint);
    HashDigest(hasher, receipt.composition_trace_fingerprint);
    HashU64(hasher, receipt.binding_count);
    HashU64(hasher, receipt.preserved_count);
    HashU64(hasher, receipt.declared_loss_count);
    HashU64(hasher, receipt.content_binding_count);
    HashU64(hasher, receipt.missing_binding_count);
    HashU64(hasher, receipt.error_binding_count);
    HashU64(hasher, receipt.atom_count);
    HashU64(hasher, receipt.request_count);
    HashU32(hasher, receipt.version);
    HashU32(hasher, receipt.status);
    return Finish(hasher);
}

bool ReadBinding(
    const std::uint32_t*& cursor,
    const std::uint64_t index,
    const std::uint64_t atom_count,
    const std::uint64_t request_count,
    std::uint64_t& previous_syntax_span,
    laplace_universal_ast_binding& binding,
    std::uint64_t& preserved,
    std::uint64_t& declared_loss,
    std::uint64_t& content,
    std::uint64_t& missing,
    std::uint64_t& error_count) {
    binding = laplace_universal_ast_binding{};
    binding.grammar_coordinate = ReadCoordinate(cursor);
    binding.role_coordinate = ReadCoordinate(cursor);
    binding.provider_fingerprint = ReadDigest(cursor);
    binding.content_reference = ReadReference(cursor);
    binding.syntax_span_index = ReadU64(cursor);
    binding.parent_binding_index = ReadU64(cursor);
    binding.byte_start = ReadU64(cursor);
    binding.byte_end = ReadU64(cursor);
    binding.kind = ReadU64(cursor);
    binding.grammar_kind = ReadU64(cursor);
    binding.field_kind = ReadU64(cursor);
    binding.sibling_ordinal = ReadU64(cursor);
    binding.syntax_flags = *cursor++;
    binding.disposition = *cursor++;
    binding.has_content = *cursor++;
    binding.reserved = *cursor++;

    if (!CoordinateValid(
            binding.grammar_coordinate, LAPLACE_HIGHWAY_KIND_GRAMMAR_SYMBOL) ||
        DigestZero(binding.provider_fingerprint) || binding.reserved != 0u ||
        binding.has_content > 1u ||
        (binding.syntax_flags & ~LAPLACE_DECOMPOSITION_KNOWN_SYNTAX_FLAGS) != 0u ||
        binding.byte_start > binding.byte_end || binding.syntax_span_index == 0u ||
        (index != 0u && binding.syntax_span_index <= previous_syntax_span) ||
        (binding.parent_binding_index != NoBinding &&
         binding.parent_binding_index >= index)) {
        return false;
    }
    previous_syntax_span = binding.syntax_span_index;

    if (binding.disposition == LAPLACE_AST_RULE_PRESERVE) {
        if (!CoordinateValid(binding.role_coordinate, LAPLACE_HIGHWAY_KIND_AST_ROLE)) {
            return false;
        }
        ++preserved;
    } else if (binding.disposition == LAPLACE_AST_RULE_DECLARED_LOSS) {
        if (!CoordinateZero(binding.role_coordinate)) return false;
        ++declared_loss;
    } else {
        return false;
    }

    if (binding.has_content != 0u) {
        const bool known_entity =
            binding.content_reference.reference_kind ==
                LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY &&
            binding.content_reference.reference_index < atom_count;
        const bool prior_result =
            binding.content_reference.reference_kind ==
                LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT &&
            binding.content_reference.reference_index < request_count;
        if (binding.byte_start >= binding.byte_end ||
            binding.content_reference.multiplicity != 1u ||
            binding.content_reference.relationship_metadata != 0u ||
            binding.content_reference.flags != 0u ||
            (!known_entity && !prior_result)) {
            return false;
        }
        ++content;
    } else if (!ReferenceZero(binding.content_reference) ||
               binding.byte_start != binding.byte_end ||
               (binding.syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_MISSING) == 0u) {
        return false;
    }

    if ((binding.syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_MISSING) != 0u) {
        if (binding.has_content != 0u) return false;
        ++missing;
    }
    if ((binding.syntax_flags &
         (LAPLACE_DECOMPOSITION_SYNTAX_ERROR |
          LAPLACE_DECOMPOSITION_SYNTAX_HAS_ERROR)) != 0u) {
        ++error_count;
    }
    return true;
}

}  // namespace

extern "C" laplace_universal_ast_status laplace_universal_ast_plan_packet_measure(
    const laplace_universal_ast_plan* const plan,
    size_t* const required_words) {
    if (plan == nullptr || required_words == nullptr) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }
    laplace_universal_ast_plan_view view{};
    if (laplace_universal_ast_plan_view_get(plan, &view) != LAPLACE_UNIVERSAL_AST_OK ||
        !AddWords(view.binding_count, *required_words)) {
        return LAPLACE_UNIVERSAL_AST_OVERFLOW;
    }
    return LAPLACE_UNIVERSAL_AST_OK;
}

extern "C" laplace_universal_ast_status laplace_universal_ast_plan_packet_encode(
    const laplace_universal_ast_plan* const plan,
    std::uint32_t* const output_words,
    const size_t output_capacity_words,
    size_t* const output_words_written) {
    if (output_words_written != nullptr) *output_words_written = 0u;
    if (plan == nullptr || output_words_written == nullptr) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }
    laplace_universal_ast_plan_view view{};
    size_t required = 0u;
    if (laplace_universal_ast_plan_view_get(plan, &view) != LAPLACE_UNIVERSAL_AST_OK ||
        !AddWords(view.binding_count, required)) {
        return LAPLACE_UNIVERSAL_AST_OVERFLOW;
    }
    *output_words_written = required;
    if (output_words == nullptr || output_capacity_words < required) {
        return LAPLACE_UNIVERSAL_AST_PACKET_CAPACITY;
    }
    if (required > std::numeric_limits<std::uint32_t>::max()) {
        return LAPLACE_UNIVERSAL_AST_OVERFLOW;
    }

    std::uint32_t* cursor = output_words;
    *cursor++ = PacketMagic;
    *cursor++ = LAPLACE_UNIVERSAL_AST_PACKET_VERSION;
    *cursor++ = static_cast<std::uint32_t>(required);
    WriteU64(cursor, view.binding_count);
    WriteU64(cursor, view.composition.atom_count);
    WriteU64(cursor, view.composition.request_count);
    WriteU64(cursor, view.preserved_count);
    WriteU64(cursor, view.declared_loss_count);
    WriteU64(cursor, view.content_binding_count);
    WriteU64(cursor, view.missing_binding_count);
    WriteU64(cursor, view.error_binding_count);
    WriteDigest(cursor, view.grammar_registry_fingerprint);
    WriteDigest(cursor, view.recipe_fingerprint);
    WriteDigest(cursor, view.plan_fingerprint);
    WriteDigest(cursor, view.witness_fingerprint);
    WriteDigest(cursor, view.composition.trace_fingerprint);

    for (std::uint64_t index = 0u; index < view.binding_count; ++index) {
        const auto& binding = view.bindings[static_cast<std::size_t>(index)];
        WriteCoordinate(cursor, binding.grammar_coordinate);
        WriteCoordinate(cursor, binding.role_coordinate);
        WriteDigest(cursor, binding.provider_fingerprint);
        WriteReference(cursor, binding.content_reference);
        WriteU64(cursor, binding.syntax_span_index);
        WriteU64(cursor, binding.parent_binding_index);
        WriteU64(cursor, binding.byte_start);
        WriteU64(cursor, binding.byte_end);
        WriteU64(cursor, binding.kind);
        WriteU64(cursor, binding.grammar_kind);
        WriteU64(cursor, binding.field_kind);
        WriteU64(cursor, binding.sibling_ordinal);
        *cursor++ = binding.syntax_flags;
        *cursor++ = binding.disposition;
        *cursor++ = binding.has_content;
        *cursor++ = binding.reserved;
    }
    return static_cast<std::size_t>(cursor - output_words) == required
        ? LAPLACE_UNIVERSAL_AST_OK
        : LAPLACE_UNIVERSAL_AST_OVERFLOW;
}

extern "C" laplace_universal_ast_status laplace_universal_ast_packet_validate_words(
    const std::uint32_t* const words,
    const size_t word_count,
    laplace_universal_ast_packet_receipt* const receipt) {
    if (receipt != nullptr) std::memset(receipt, 0, sizeof(*receipt));
    if (words == nullptr || receipt == nullptr || word_count < HeaderWords) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }

    const std::uint32_t* cursor = words;
    if (*cursor++ != PacketMagic ||
        *cursor++ != LAPLACE_UNIVERSAL_AST_PACKET_VERSION) {
        return LAPLACE_UNIVERSAL_AST_PACKET_INVALID;
    }
    const std::uint32_t declared_words = *cursor++;
    const std::uint64_t binding_count = ReadU64(cursor);
    const std::uint64_t atom_count = ReadU64(cursor);
    const std::uint64_t request_count = ReadU64(cursor);
    const std::uint64_t preserved_count = ReadU64(cursor);
    const std::uint64_t declared_loss_count = ReadU64(cursor);
    const std::uint64_t content_binding_count = ReadU64(cursor);
    const std::uint64_t missing_binding_count = ReadU64(cursor);
    const std::uint64_t error_binding_count = ReadU64(cursor);
    std::size_t expected_words = 0u;
    if (!AddWords(binding_count, expected_words) ||
        expected_words != word_count || declared_words != word_count ||
        preserved_count > binding_count || declared_loss_count > binding_count ||
        preserved_count + declared_loss_count != binding_count ||
        content_binding_count > binding_count || missing_binding_count > binding_count ||
        error_binding_count > binding_count) {
        return LAPLACE_UNIVERSAL_AST_PACKET_INVALID;
    }

    receipt->grammar_registry_fingerprint = ReadDigest(cursor);
    receipt->recipe_fingerprint = ReadDigest(cursor);
    const laplace_digest256 declared_plan = ReadDigest(cursor);
    const laplace_digest256 declared_witness = ReadDigest(cursor);
    receipt->composition_trace_fingerprint = ReadDigest(cursor);
    if (DigestZero(receipt->grammar_registry_fingerprint) ||
        DigestZero(receipt->recipe_fingerprint) || DigestZero(declared_plan) ||
        DigestZero(declared_witness) ||
        DigestZero(receipt->composition_trace_fingerprint)) {
        return LAPLACE_UNIVERSAL_AST_PACKET_INVALID;
    }

    blake3_hasher semantic;
    BeginHash(semantic, "laplace-universal-ast-plan-v1");
    HashDigest(semantic, receipt->grammar_registry_fingerprint);
    HashDigest(semantic, receipt->recipe_fingerprint);
    HashU64(semantic, binding_count);
    const std::uint32_t* binding_start = cursor;
    std::uint64_t actual_preserved = 0u;
    std::uint64_t actual_loss = 0u;
    std::uint64_t actual_content = 0u;
    std::uint64_t actual_missing = 0u;
    std::uint64_t actual_error = 0u;
    std::uint64_t prior_span = 0u;
    for (std::uint64_t index = 0u; index < binding_count; ++index) {
        laplace_universal_ast_binding binding{};
        if (!ReadBinding(
                cursor, index, atom_count, request_count, prior_span, binding,
                actual_preserved, actual_loss, actual_content,
                actual_missing, actual_error)) {
            return LAPLACE_UNIVERSAL_AST_PACKET_INVALID;
        }
        HashSemanticBinding(semantic, binding);
    }
    if (cursor != words + word_count || actual_preserved != preserved_count ||
        actual_loss != declared_loss_count || actual_content != content_binding_count ||
        actual_missing != missing_binding_count || actual_error != error_binding_count) {
        return LAPLACE_UNIVERSAL_AST_PACKET_INVALID;
    }
    receipt->plan_fingerprint = Finish(semantic);
    if (!DigestEqual(receipt->plan_fingerprint, declared_plan)) {
        return LAPLACE_UNIVERSAL_AST_PACKET_INVALID;
    }

    blake3_hasher witness;
    BeginHash(witness, "laplace-universal-ast-witness-v1");
    HashDigest(witness, receipt->plan_fingerprint);
    HashDigest(witness, receipt->composition_trace_fingerprint);
    cursor = binding_start;
    prior_span = 0u;
    std::uint64_t ignored_preserved = 0u;
    std::uint64_t ignored_loss = 0u;
    std::uint64_t ignored_content = 0u;
    std::uint64_t ignored_missing = 0u;
    std::uint64_t ignored_error = 0u;
    for (std::uint64_t index = 0u; index < binding_count; ++index) {
        laplace_universal_ast_binding binding{};
        if (!ReadBinding(
                cursor, index, atom_count, request_count, prior_span, binding,
                ignored_preserved, ignored_loss, ignored_content,
                ignored_missing, ignored_error)) {
            return LAPLACE_UNIVERSAL_AST_PACKET_INVALID;
        }
        HashWitnessBinding(witness, binding);
    }
    receipt->witness_fingerprint = Finish(witness);
    if (!DigestEqual(receipt->witness_fingerprint, declared_witness)) {
        return LAPLACE_UNIVERSAL_AST_PACKET_INVALID;
    }

    receipt->binding_count = binding_count;
    receipt->preserved_count = preserved_count;
    receipt->declared_loss_count = declared_loss_count;
    receipt->content_binding_count = content_binding_count;
    receipt->missing_binding_count = missing_binding_count;
    receipt->error_binding_count = error_binding_count;
    receipt->atom_count = atom_count;
    receipt->request_count = request_count;
    receipt->version = LAPLACE_UNIVERSAL_AST_PACKET_VERSION;
    receipt->status = LAPLACE_UNIVERSAL_AST_OK;
    receipt->receipt_id = ReceiptId(*receipt);
    return LAPLACE_UNIVERSAL_AST_OK;
}