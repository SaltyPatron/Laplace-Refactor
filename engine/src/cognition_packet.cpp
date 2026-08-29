#include "laplace/cognition_packet.h"

#include "laplace/cognition_runtime.h"

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

class Reader {
public:
    Reader(const std::uint8_t* const data, const std::size_t size)
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

    bool Id(laplace_id128* const output) {
        return output != nullptr && Bytes(output->bytes, sizeof(output->bytes));
    }

    bool Complete() const { return position_ == size_; }

private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    std::size_t position_{};
};

class Writer {
public:
    Writer(std::uint8_t* const data, const std::size_t capacity)
        : data_(data), capacity_(capacity) {}

    bool Bytes(const void* const input, const std::size_t count) {
        if (input == nullptr || position_ > capacity_ || count > capacity_ - position_) {
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

    std::size_t Size() const { return position_; }

private:
    std::uint8_t* data_{};
    std::size_t capacity_{};
    std::size_t position_{};
};

struct DecodedRequest {
    laplace_cognition_operator_program operator_program{};
    laplace_cognition_solver_program solver_program{};
    std::vector<std::uint32_t> relation_families;
    std::vector<laplace_cognition_operator_field> fields;
    std::vector<laplace_cognition_operator_constraint> constraints;
    std::vector<double> initial_state;
};

bool CountFits(const std::uint64_t count, const std::size_t element_bytes) {
    return count <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) &&
        (element_bytes == 0U ||
         count <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / element_bytes));
}

bool ReadOperatorProgram(
    Reader* const reader,
    laplace_cognition_operator_program* const program) {
    if (reader == nullptr || program == nullptr) return false;
    return reader->Digest(&program->program_id) &&
        reader->Digest(&program->boundary_id) &&
        reader->Digest(&program->context_fingerprint) &&
        reader->Digest(&program->evidence_epoch) &&
        reader->Digest(&program->result_contract_fingerprint) &&
        reader->U32(&program->eligible_source_mask) &&
        reader->U32(&program->flags) &&
        reader->F64(&program->numeric_tolerance) &&
        reader->U32(&program->version) &&
        reader->U32(&program->reserved);
}

bool ReadSolverProgram(
    Reader* const reader,
    laplace_cognition_solver_program* const program) {
    if (reader == nullptr || program == nullptr) return false;
    return reader->Digest(&program->program_id) &&
        reader->Digest(&program->result_contract_fingerprint) &&
        reader->U64(&program->max_iterations) &&
        reader->F64(&program->absolute_residual_tolerance) &&
        reader->F64(&program->relative_residual_tolerance) &&
        reader->F64(&program->regularization) &&
        reader->U32(&program->method) &&
        reader->U32(&program->flags) &&
        reader->U32(&program->version) &&
        reader->U32(&program->reserved);
}

bool ReadField(
    Reader* const reader,
    laplace_cognition_operator_field* const field) {
    if (reader == nullptr || field == nullptr) return false;
    return reader->Digest(&field->field_id) &&
        reader->Id(&field->entity_id) &&
        reader->Digest(&field->physicality_id) &&
        reader->Digest(&field->role_id) &&
        reader->Digest(&field->recipe_fingerprint) &&
        reader->U64(&field->ordinal) &&
        reader->U32(&field->value_dimension) &&
        reader->U32(&field->flags);
}

bool ReadConstraint(
    Reader* const reader,
    laplace_cognition_operator_constraint* const constraint) {
    if (reader == nullptr || constraint == nullptr) return false;
    return reader->Digest(&constraint->constraint_id) &&
        reader->Digest(&constraint->plane_id) &&
        reader->Digest(&constraint->law_fingerprint) &&
        reader->Digest(&constraint->units_fingerprint) &&
        reader->Digest(&constraint->evidence_root_id) &&
        reader->Digest(&constraint->calculation_receipt_id) &&
        reader->U64(&constraint->source_field_index) &&
        reader->U64(&constraint->target_field_index) &&
        reader->F64(&constraint->transport_scale) &&
        reader->F64(&constraint->transport_offset) &&
        reader->F64(&constraint->target_value) &&
        reader->F64(&constraint->precision) &&
        reader->U32(&constraint->relation_family) &&
        reader->U32(&constraint->source_class) &&
        reader->U32(&constraint->direction) &&
        reader->U32(&constraint->transport_kind) &&
        reader->U32(&constraint->flags) &&
        reader->U32(&constraint->reserved);
}

laplace_cognition_packet_status DecodeRequest(
    const std::uint8_t* const request_bytes,
    const std::size_t request_byte_count,
    DecodedRequest* const decoded) {
    if (request_bytes == nullptr || request_byte_count == 0U || decoded == nullptr) {
        return LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT;
    }
    Reader reader(request_bytes, request_byte_count);
    std::array<std::uint8_t, 8> magic{};
    std::uint32_t version = 0U;
    std::uint32_t flags = 0U;
    std::uint64_t family_count = 0U;
    std::uint64_t field_count = 0U;
    std::uint64_t constraint_count = 0U;
    std::uint64_t initial_state_count = 0U;
    if (!reader.Bytes(magic.data(), magic.size()) || magic != RequestMagic ||
        !reader.U32(&version) || version != PacketVersion ||
        !reader.U32(&flags) || flags != PacketFlags ||
        !reader.U64(&family_count) || !reader.U64(&field_count) ||
        !reader.U64(&constraint_count) || !reader.U64(&initial_state_count) ||
        family_count == 0U || field_count == 0U || constraint_count == 0U ||
        initial_state_count != field_count ||
        !CountFits(family_count, sizeof(std::uint32_t)) ||
        !CountFits(field_count, sizeof(laplace_cognition_operator_field)) ||
        !CountFits(constraint_count, sizeof(laplace_cognition_operator_constraint)) ||
        !CountFits(initial_state_count, sizeof(double)) ||
        !ReadOperatorProgram(&reader, &decoded->operator_program) ||
        !ReadSolverProgram(&reader, &decoded->solver_program) ||
        decoded->operator_program.reserved != 0U || decoded->solver_program.reserved != 0U) {
        return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
    }
    try {
        decoded->relation_families.resize(static_cast<std::size_t>(family_count));
        decoded->fields.resize(static_cast<std::size_t>(field_count));
        decoded->constraints.resize(static_cast<std::size_t>(constraint_count));
        decoded->initial_state.resize(static_cast<std::size_t>(initial_state_count));
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PACKET_MEMORY_FAILURE;
    }
    for (auto& family : decoded->relation_families) {
        if (!reader.U32(&family)) return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
    }
    for (auto& field : decoded->fields) {
        if (!ReadField(&reader, &field)) return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
    }
    for (auto& constraint : decoded->constraints) {
        if (!ReadConstraint(&reader, &constraint)) {
            return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
        }
    }
    for (auto& value : decoded->initial_state) {
        if (!reader.F64(&value)) return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
    }
    if (!reader.Complete()) return LAPLACE_COGNITION_PACKET_INVALID_REQUEST;
    decoded->operator_program.eligible_relation_families =
        decoded->relation_families.data();
    decoded->operator_program.eligible_relation_family_count =
        decoded->relation_families.size();
    decoded->solver_program.operator_id = laplace_digest256{};
    return LAPLACE_COGNITION_PACKET_OK;
}

bool ResultSize(const std::size_t solution_count, std::size_t* const result_bytes) {
    constexpr std::size_t HeaderBytes = 8U + 4U + 4U + 8U;
    constexpr std::size_t OperatorReceiptBytes =
        5U * 32U + 7U * 8U + 4U * 4U;
    constexpr std::size_t SolverReceiptBytes =
        8U * 32U + 2U * 8U + 4U * 8U + 5U * 4U;
    constexpr std::size_t FixedBytes =
        HeaderBytes + OperatorReceiptBytes + SolverReceiptBytes;
    if (result_bytes == nullptr ||
        solution_count > (std::numeric_limits<std::size_t>::max() - FixedBytes) / sizeof(double)) {
        return false;
    }
    *result_bytes = FixedBytes + solution_count * sizeof(double);
    return true;
}

bool WriteOperatorReceipt(
    Writer* const writer,
    const laplace_cognition_operator_receipt& receipt) {
    return writer != nullptr &&
        writer->Digest(receipt.receipt_id) &&
        writer->Digest(receipt.operator_id) &&
        writer->Digest(receipt.program_fingerprint) &&
        writer->Digest(receipt.field_set_fingerprint) &&
        writer->Digest(receipt.constraint_set_fingerprint) &&
        writer->U64(receipt.field_count) &&
        writer->U64(receipt.input_constraint_count) &&
        writer->U64(receipt.selected_constraint_count) &&
        writer->U64(receipt.deduplicated_dependent_count) &&
        writer->U64(receipt.physicality_constraint_count) &&
        writer->U64(receipt.testimony_constraint_count) &&
        writer->U64(receipt.derived_constraint_count) &&
        writer->U32(receipt.relation_plane_count) &&
        writer->U32(receipt.status) &&
        writer->U32(receipt.version) &&
        writer->U32(receipt.flags);
}

bool WriteSolverReceipt(
    Writer* const writer,
    const laplace_cognition_solver_receipt& receipt) {
    return writer != nullptr &&
        writer->Digest(receipt.receipt_id) &&
        writer->Digest(receipt.program_fingerprint) &&
        writer->Digest(receipt.operator_id) &&
        writer->Digest(receipt.operator_receipt_id) &&
        writer->Digest(receipt.evidence_precision_fingerprint) &&
        writer->Digest(receipt.input_fingerprint) &&
        writer->Digest(receipt.iteration_trace_fingerprint) &&
        writer->Digest(receipt.output_fingerprint) &&
        writer->U64(receipt.field_count) &&
        writer->U64(receipt.iteration_count) &&
        writer->F64(receipt.initial_residual_l2) &&
        writer->F64(receipt.final_residual_l2) &&
        writer->F64(receipt.final_energy) &&
        writer->F64(receipt.regularization) &&
        writer->U32(receipt.method) &&
        writer->U32(receipt.disposition) &&
        writer->U32(receipt.status) &&
        writer->U32(receipt.version) &&
        writer->U32(receipt.flags);
}

laplace_cognition_packet_status ExecuteDecoded(
    const DecodedRequest& decoded,
    std::uint8_t* const result_bytes,
    const std::size_t result_capacity,
    std::size_t* const result_byte_count) {
    std::size_t required = 0U;
    if (!ResultSize(decoded.fields.size(), &required)) {
        return LAPLACE_COGNITION_PACKET_RANGE;
    }
    if (result_byte_count != nullptr) *result_byte_count = required;
    if (result_bytes == nullptr || result_capacity < required) {
        return LAPLACE_COGNITION_PACKET_RESULT_CAPACITY;
    }
    std::vector<double> solution;
    try {
        solution.resize(decoded.fields.size());
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PACKET_MEMORY_FAILURE;
    }
    laplace_cognition_runtime_request request{};
    request.operator_program = decoded.operator_program;
    request.fields = decoded.fields.data();
    request.constraints = decoded.constraints.data();
    request.initial_state = decoded.initial_state.data();
    request.field_count = decoded.fields.size();
    request.constraint_count = decoded.constraints.size();
    request.initial_state_count = decoded.initial_state.size();
    request.solver_program = decoded.solver_program;
    laplace_cognition_runtime_result result{};
    result.solution = solution.data();
    result.solution_capacity = solution.size();
    const auto runtime_status = laplace_cognition_runtime_execute(&request, &result);
    if (runtime_status != LAPLACE_COGNITION_RUNTIME_OK ||
        result.solution_count != solution.size()) {
        return LAPLACE_COGNITION_PACKET_RUNTIME_FAILURE;
    }
    Writer writer(result_bytes, result_capacity);
    if (!writer.Bytes(ResultMagic.data(), ResultMagic.size()) ||
        !writer.U32(PacketVersion) || !writer.U32(PacketFlags) ||
        !writer.U64(result.solution_count) ||
        !WriteOperatorReceipt(&writer, result.operator_receipt) ||
        !WriteSolverReceipt(&writer, result.solver_receipt)) {
        return LAPLACE_COGNITION_PACKET_RESULT_CAPACITY;
    }
    for (const auto value : solution) {
        if (!writer.F64(value)) return LAPLACE_COGNITION_PACKET_RESULT_CAPACITY;
    }
    if (writer.Size() != required) return LAPLACE_COGNITION_PACKET_RANGE;
    if (result_byte_count != nullptr) *result_byte_count = writer.Size();
    return LAPLACE_COGNITION_PACKET_OK;
}

}  // namespace

extern "C" laplace_cognition_packet_status
laplace_cognition_packet_required_result_bytes(
    const std::uint8_t* const request_bytes,
    const std::size_t request_byte_count,
    std::size_t* const required_result_bytes) {
    if (required_result_bytes == nullptr) {
        return LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT;
    }
    *required_result_bytes = 0U;
    try {
        DecodedRequest decoded{};
        const auto status = DecodeRequest(
            request_bytes, request_byte_count, &decoded);
        if (status != LAPLACE_COGNITION_PACKET_OK) return status;
        if (!ResultSize(decoded.fields.size(), required_result_bytes)) {
            return LAPLACE_COGNITION_PACKET_RANGE;
        }
        return LAPLACE_COGNITION_PACKET_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PACKET_MEMORY_FAILURE;
    }
}

extern "C" laplace_cognition_packet_status
laplace_cognition_packet_execute(
    const std::uint8_t* const request_bytes,
    const std::size_t request_byte_count,
    std::uint8_t* const result_bytes,
    const std::size_t result_capacity,
    std::size_t* const result_byte_count) {
    if (result_byte_count == nullptr) {
        return LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT;
    }
    *result_byte_count = 0U;
    try {
        DecodedRequest decoded{};
        const auto status = DecodeRequest(
            request_bytes, request_byte_count, &decoded);
        if (status != LAPLACE_COGNITION_PACKET_OK) return status;
        return ExecuteDecoded(
            decoded, result_bytes, result_capacity, result_byte_count);
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PACKET_MEMORY_FAILURE;
    }
}
