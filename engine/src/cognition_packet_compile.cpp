#include "laplace/cognition_packet_compile.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

constexpr std::array<std::uint8_t, 8> RequestMagic{
    'L', 'A', 'P', 'C', 'O', 'G', 'Q', '1'};
constexpr std::array<std::uint8_t, 8> ResultMagic{
    'L', 'A', 'P', 'C', 'O', 'G', 'R', '1'};
constexpr std::uint32_t PacketVersion = 1U;
constexpr std::uint32_t PacketFlags = 0U;
constexpr std::size_t RequestFixedBytes = 344U;
constexpr std::size_t FieldBytes = 160U;
constexpr std::size_t ConstraintBytes = 264U;
constexpr std::size_t ResultFixedBytes = 580U;

bool FitsSize(const std::uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= static_cast<std::uint64_t>(SIZE_MAX);
#else
    (void)value;
    return true;
#endif
}

bool AddMul(
    std::size_t* const value,
    const std::size_t count,
    const std::size_t width) {
    if (value == nullptr ||
        (width != 0U && count >
            (std::numeric_limits<std::size_t>::max() - *value) / width)) {
        return false;
    }
    *value += count * width;
    return true;
}

bool RequestBytes(
    const laplace_cognition_runtime_request& request,
    std::size_t* const bytes) {
    if (bytes == nullptr || request.fields == nullptr ||
        request.constraints == nullptr || request.initial_state == nullptr ||
        request.operator_program.eligible_relation_families == nullptr ||
        request.operator_program.eligible_relation_family_count == 0U ||
        request.field_count == 0U || request.constraint_count == 0U ||
        request.initial_state_count != request.field_count ||
        !FitsSize(request.field_count) || !FitsSize(request.constraint_count) ||
        !FitsSize(request.initial_state_count)) {
        return false;
    }
    std::size_t total = RequestFixedBytes;
    return AddMul(
               &total,
               request.operator_program.eligible_relation_family_count,
               sizeof(std::uint32_t)) &&
        AddMul(&total, static_cast<std::size_t>(request.field_count), FieldBytes) &&
        AddMul(
            &total,
            static_cast<std::size_t>(request.constraint_count),
            ConstraintBytes) &&
        AddMul(
            &total,
            static_cast<std::size_t>(request.initial_state_count),
            sizeof(double)) &&
        ((*bytes = total), true);
}

class ByteWriter {
public:
    ByteWriter(std::uint8_t* const data, const std::size_t capacity)
        : data_(data), capacity_(capacity) {}

    bool Bytes(const void* const input, const std::size_t count) {
        if (input == nullptr || position_ > capacity_ ||
            count > capacity_ - position_) {
            return false;
        }
        std::memcpy(data_ + position_, input, count);
        position_ += count;
        return true;
    }

    bool U32(const std::uint32_t value) {
        const std::array<std::uint8_t, 4> bytes{{
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8U),
            static_cast<std::uint8_t>(value >> 16U),
            static_cast<std::uint8_t>(value >> 24U)}};
        return Bytes(bytes.data(), bytes.size());
    }

    bool U64(const std::uint64_t value) {
        std::array<std::uint8_t, 8> bytes{};
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
        return Bytes(bytes.data(), bytes.size());
    }

    bool F64(const double value) {
        return U64(std::bit_cast<std::uint64_t>(value));
    }

    bool Digest(const laplace_digest256& value) {
        return Bytes(value.bytes, sizeof(value.bytes));
    }

    bool Id(const laplace_id128& value) {
        return Bytes(value.bytes, sizeof(value.bytes));
    }

    std::size_t Size() const { return position_; }

private:
    std::uint8_t* data_{};
    std::size_t capacity_{};
    std::size_t position_{};
};

class ByteReader {
public:
    ByteReader(const std::uint8_t* const data, const std::size_t size)
        : data_(data), size_(size) {}

    bool Bytes(void* const output, const std::size_t count) {
        if (output == nullptr || position_ > size_ || count > size_ - position_) {
            return false;
        }
        std::memcpy(output, data_ + position_, count);
        position_ += count;
        return true;
    }

    bool U32(std::uint32_t* const output) {
        std::array<std::uint8_t, 4> bytes{};
        if (output == nullptr || !Bytes(bytes.data(), bytes.size())) return false;
        *output = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
        return true;
    }

    bool U64(std::uint64_t* const output) {
        std::array<std::uint8_t, 8> bytes{};
        if (output == nullptr || !Bytes(bytes.data(), bytes.size())) return false;
        std::uint64_t value = 0U;
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
        }
        *output = value;
        return true;
    }

    bool F64(double* const output) {
        std::uint64_t bits = 0U;
        if (output == nullptr || !U64(&bits)) return false;
        *output = std::bit_cast<double>(bits);
        return true;
    }

    bool Digest(laplace_digest256* const output) {
        return output != nullptr && Bytes(output->bytes, sizeof(output->bytes));
    }

    bool Complete() const { return position_ == size_; }

private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    std::size_t position_{};
};

bool WriteOperatorProgram(
    ByteWriter* const writer,
    const laplace_cognition_operator_program& program) {
    return writer != nullptr && writer->Digest(program.program_id) &&
        writer->Digest(program.boundary_id) &&
        writer->Digest(program.context_fingerprint) &&
        writer->Digest(program.evidence_epoch) &&
        writer->Digest(program.result_contract_fingerprint) &&
        writer->U32(program.eligible_source_mask) && writer->U32(program.flags) &&
        writer->F64(program.numeric_tolerance) && writer->U32(program.version) &&
        writer->U32(program.reserved);
}

bool WriteSolverProgram(
    ByteWriter* const writer,
    const laplace_cognition_solver_program& program) {
    return writer != nullptr && writer->Digest(program.program_id) &&
        writer->Digest(program.result_contract_fingerprint) &&
        writer->U64(program.max_iterations) &&
        writer->F64(program.absolute_residual_tolerance) &&
        writer->F64(program.relative_residual_tolerance) &&
        writer->F64(program.regularization) && writer->U32(program.method) &&
        writer->U32(program.flags) && writer->U32(program.version) &&
        writer->U32(program.reserved);
}

bool WriteField(
    ByteWriter* const writer,
    const laplace_cognition_operator_field& field) {
    return writer != nullptr && writer->Digest(field.field_id) &&
        writer->Id(field.entity_id) && writer->Digest(field.physicality_id) &&
        writer->Digest(field.role_id) && writer->Digest(field.recipe_fingerprint) &&
        writer->U64(field.ordinal) && writer->U32(field.value_dimension) &&
        writer->U32(field.flags);
}

bool WriteConstraint(
    ByteWriter* const writer,
    const laplace_cognition_operator_constraint& constraint) {
    return writer != nullptr && writer->Digest(constraint.constraint_id) &&
        writer->Digest(constraint.plane_id) &&
        writer->Digest(constraint.law_fingerprint) &&
        writer->Digest(constraint.units_fingerprint) &&
        writer->Digest(constraint.evidence_root_id) &&
        writer->Digest(constraint.calculation_receipt_id) &&
        writer->U64(constraint.source_field_index) &&
        writer->U64(constraint.target_field_index) &&
        writer->F64(constraint.transport_scale) &&
        writer->F64(constraint.transport_offset) &&
        writer->F64(constraint.target_value) && writer->F64(constraint.precision) &&
        writer->U32(constraint.relation_family) &&
        writer->U32(constraint.source_class) && writer->U32(constraint.direction) &&
        writer->U32(constraint.transport_kind) && writer->U32(constraint.flags) &&
        writer->U32(constraint.reserved);
}

bool ReadOperatorReceipt(
    ByteReader* const reader,
    laplace_cognition_operator_receipt* const receipt) {
    return reader != nullptr && receipt != nullptr &&
        reader->Digest(&receipt->receipt_id) && reader->Digest(&receipt->operator_id) &&
        reader->Digest(&receipt->program_fingerprint) &&
        reader->Digest(&receipt->field_set_fingerprint) &&
        reader->Digest(&receipt->constraint_set_fingerprint) &&
        reader->U64(&receipt->field_count) &&
        reader->U64(&receipt->input_constraint_count) &&
        reader->U64(&receipt->selected_constraint_count) &&
        reader->U64(&receipt->deduplicated_dependent_count) &&
        reader->U64(&receipt->physicality_constraint_count) &&
        reader->U64(&receipt->testimony_constraint_count) &&
        reader->U64(&receipt->derived_constraint_count) &&
        reader->U32(&receipt->relation_plane_count) && reader->U32(&receipt->status) &&
        reader->U32(&receipt->version) && reader->U32(&receipt->flags);
}

bool ReadSolverReceipt(
    ByteReader* const reader,
    laplace_cognition_solver_receipt* const receipt) {
    return reader != nullptr && receipt != nullptr &&
        reader->Digest(&receipt->receipt_id) &&
        reader->Digest(&receipt->program_fingerprint) &&
        reader->Digest(&receipt->operator_id) &&
        reader->Digest(&receipt->operator_receipt_id) &&
        reader->Digest(&receipt->evidence_precision_fingerprint) &&
        reader->Digest(&receipt->input_fingerprint) &&
        reader->Digest(&receipt->iteration_trace_fingerprint) &&
        reader->Digest(&receipt->output_fingerprint) &&
        reader->U64(&receipt->field_count) && reader->U64(&receipt->iteration_count) &&
        reader->F64(&receipt->initial_residual_l2) &&
        reader->F64(&receipt->final_residual_l2) &&
        reader->F64(&receipt->final_energy) && reader->F64(&receipt->regularization) &&
        reader->U32(&receipt->method) && reader->U32(&receipt->disposition) &&
        reader->U32(&receipt->status) && reader->U32(&receipt->version) &&
        reader->U32(&receipt->flags);
}

void BytesToWords(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t* const words) {
    const std::size_t word_count = bytes.size() / 4U;
    for (std::size_t index = 0U; index < word_count; ++index) {
        const std::size_t offset = index * 4U;
        words[index] = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
            (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
            (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    }
}

void WordsToBytes(
    const std::uint32_t* const words,
    const std::size_t word_count,
    std::vector<std::uint8_t>* const bytes) {
    bytes->resize(word_count * 4U);
    for (std::size_t index = 0U; index < word_count; ++index) {
        const std::uint32_t word = words[index];
        const std::size_t offset = index * 4U;
        (*bytes)[offset] = static_cast<std::uint8_t>(word);
        (*bytes)[offset + 1U] = static_cast<std::uint8_t>(word >> 8U);
        (*bytes)[offset + 2U] = static_cast<std::uint8_t>(word >> 16U);
        (*bytes)[offset + 3U] = static_cast<std::uint8_t>(word >> 24U);
    }
}

}  // namespace

extern "C" laplace_cognition_packet_status
laplace_cognition_packet_request_required_words(
    const laplace_cognition_runtime_request* const request,
    size_t* const required_words) {
    if (request == nullptr || required_words == nullptr) {
        return LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT;
    }
    *required_words = 0U;
    std::size_t bytes = 0U;
    if (!RequestBytes(*request, &bytes) || (bytes & 3U) != 0U) {
        return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
    }
    *required_words = bytes / 4U;
    return LAPLACE_COGNITION_PACKET_OK;
}

extern "C" laplace_cognition_packet_status
laplace_cognition_packet_encode_request_words(
    const laplace_cognition_runtime_request* const request,
    std::uint32_t* const words,
    const size_t word_capacity,
    size_t* const word_count) {
    if (request == nullptr || words == nullptr || word_count == nullptr) {
        return LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT;
    }
    *word_count = 0U;
    std::size_t required_bytes = 0U;
    if (!RequestBytes(*request, &required_bytes) || (required_bytes & 3U) != 0U) {
        return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
    }
    const std::size_t required_words = required_bytes / 4U;
    if (word_capacity < required_words) {
        *word_count = required_words;
        return LAPLACE_COGNITION_PACKET_RESULT_CAPACITY;
    }
    try {
        std::vector<std::uint8_t> bytes(required_bytes);
        ByteWriter writer(bytes.data(), bytes.size());
        if (!writer.Bytes(RequestMagic.data(), RequestMagic.size()) ||
            !writer.U32(PacketVersion) || !writer.U32(PacketFlags) ||
            !writer.U64(request->operator_program.eligible_relation_family_count) ||
            !writer.U64(request->field_count) ||
            !writer.U64(request->constraint_count) ||
            !writer.U64(request->initial_state_count) ||
            !WriteOperatorProgram(&writer, request->operator_program) ||
            !WriteSolverProgram(&writer, request->solver_program)) {
            return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
        }
        for (std::size_t index = 0U;
             index < request->operator_program.eligible_relation_family_count;
             ++index) {
            if (!writer.U32(request->operator_program.eligible_relation_families[index])) {
                return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
            }
        }
        for (std::size_t index = 0U;
             index < static_cast<std::size_t>(request->field_count);
             ++index) {
            if (!WriteField(&writer, request->fields[index])) {
                return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
            }
        }
        for (std::size_t index = 0U;
             index < static_cast<std::size_t>(request->constraint_count);
             ++index) {
            if (!WriteConstraint(&writer, request->constraints[index])) {
                return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
            }
        }
        for (std::size_t index = 0U;
             index < static_cast<std::size_t>(request->initial_state_count);
             ++index) {
            if (!writer.F64(request->initial_state[index])) {
                return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
            }
        }
        if (writer.Size() != required_bytes) {
            return LAPLACE_COGNITION_PACKET_RANGE;
        }
        BytesToWords(bytes, words);
        *word_count = required_words;
        return LAPLACE_COGNITION_PACKET_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PACKET_MEMORY_FAILURE;
    }
}

extern "C" laplace_cognition_packet_status
laplace_cognition_packet_decode_result_words(
    const std::uint32_t* const words,
    const size_t word_count,
    laplace_cognition_runtime_result* const result) {
    if (words == nullptr || word_count == 0U || result == nullptr ||
        result->solution == nullptr) {
        return LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT;
    }
    if (word_count > std::numeric_limits<std::size_t>::max() / 4U) {
        return LAPLACE_COGNITION_PACKET_RANGE;
    }
    try {
        std::vector<std::uint8_t> bytes;
        WordsToBytes(words, word_count, &bytes);
        ByteReader reader(bytes.data(), bytes.size());
        std::array<std::uint8_t, 8> magic{};
        std::uint32_t version = 0U;
        std::uint32_t flags = 0U;
        std::uint64_t solution_count = 0U;
        laplace_cognition_operator_receipt operator_receipt{};
        laplace_cognition_solver_receipt solver_receipt{};
        if (!reader.Bytes(magic.data(), magic.size()) || magic != ResultMagic ||
            !reader.U32(&version) || version != PacketVersion ||
            !reader.U32(&flags) || flags != PacketFlags ||
            !reader.U64(&solution_count) ||
            solution_count > result->solution_capacity ||
            !FitsSize(solution_count) ||
            !ReadOperatorReceipt(&reader, &operator_receipt) ||
            !ReadSolverReceipt(&reader, &solver_receipt)) {
            return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
        }
        std::vector<double> solution(static_cast<std::size_t>(solution_count));
        for (auto& value : solution) {
            if (!reader.F64(&value)) {
                return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
            }
        }
        if (!reader.Complete() || bytes.size() !=
                ResultFixedBytes + solution.size() * sizeof(double)) {
            return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
        }
        std::copy(solution.begin(), solution.end(), result->solution);
        result->solution_count = solution_count;
        result->operator_receipt = operator_receipt;
        result->solver_receipt = solver_receipt;
        result->status = LAPLACE_COGNITION_RUNTIME_OK;
        result->reserved = 0U;
        return LAPLACE_COGNITION_PACKET_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PACKET_MEMORY_FAILURE;
    }
}
