#include "laplace/target_package.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <tuple>
#include <vector>

namespace {

constexpr std::array<std::uint8_t, 8> TargetPackageMagic{{
    'L', 'P', 'T', 'N', 'P', 'K', 'G', '1'}};
constexpr char TargetPackageDomain[] = "laplace-target-neutral-package-v1";
constexpr std::size_t TargetPackageIdOffset = 24U;
constexpr std::size_t TargetPackageIdBytes = 32U;
constexpr std::size_t TargetPackageHeaderBytes = 56U;
constexpr std::size_t TargetCompileReceiptEncodedBytes = 240U;
constexpr std::size_t TargetSlotReceiptEncodedBytes = 344U;
static_assert(sizeof(double) == 8U);

bool TargetPackageAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t* const result) noexcept {
    if (result == nullptr || right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    *result = left + right;
    return true;
}

bool TargetPackageMultiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t* const result) noexcept {
    if (result == nullptr ||
        (left != 0U && right > std::numeric_limits<std::size_t>::max() / left)) {
        return false;
    }
    *result = left * right;
    return true;
}

class TargetPackageWriter final {
public:
    TargetPackageWriter(std::uint8_t* const output, const std::size_t capacity) noexcept
        : output_(output), capacity_(capacity) {}

    bool Bytes(const void* const value, const std::size_t count) noexcept {
        if (value == nullptr || count > capacity_ - offset_) return false;
        std::memcpy(output_ + offset_, value, count);
        offset_ += count;
        return true;
    }

    bool U32(const std::uint32_t value) noexcept {
        const std::array<std::uint8_t, 4> bytes{{
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8U),
            static_cast<std::uint8_t>(value >> 16U),
            static_cast<std::uint8_t>(value >> 24U)}};
        return Bytes(bytes.data(), bytes.size());
    }

    bool U64(const std::uint64_t value) noexcept {
        std::array<std::uint8_t, 8> bytes{};
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
        return Bytes(bytes.data(), bytes.size());
    }

    bool Digest(const laplace_digest256& value) noexcept {
        return Bytes(value.bytes, sizeof(value.bytes));
    }

    bool F64(const double value) noexcept {
        std::uint64_t bits{};
        std::memcpy(&bits, &value, sizeof(bits));
        return U64(bits);
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

private:
    std::uint8_t* output_{};
    std::size_t capacity_{};
    std::size_t offset_{};
};

class TargetPackageReader final {
public:
    TargetPackageReader(
        const std::uint8_t* const input,
        const std::size_t size) noexcept
        : input_(input), size_(size) {}

    bool Bytes(void* const output, const std::size_t count) noexcept {
        if (output == nullptr || count > size_ - offset_) return false;
        std::memcpy(output, input_ + offset_, count);
        offset_ += count;
        return true;
    }

    bool U32(std::uint32_t* const value) noexcept {
        std::array<std::uint8_t, 4> bytes{};
        if (value == nullptr || !Bytes(bytes.data(), bytes.size())) return false;
        *value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
        return true;
    }

    bool U64(std::uint64_t* const value) noexcept {
        std::array<std::uint8_t, 8> bytes{};
        if (value == nullptr || !Bytes(bytes.data(), bytes.size())) return false;
        std::uint64_t result = 0U;
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            result |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
        }
        *value = result;
        return true;
    }

    bool Digest(laplace_digest256* const value) noexcept {
        return value != nullptr && Bytes(value->bytes, sizeof(value->bytes));
    }

    bool F64(double* const value) noexcept {
        std::uint64_t bits{};
        if (value == nullptr || !U64(&bits)) return false;
        std::memcpy(value, &bits, sizeof(bits));
        return true;
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }

private:
    const std::uint8_t* input_{};
    std::size_t size_{};
    std::size_t offset_{};
};

void TargetPackageDigest(
    const std::uint8_t* const bytes,
    const std::size_t size,
    laplace_digest256* const output) {
    const std::array<std::uint8_t, TargetPackageIdBytes> zero{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetPackageDomain, sizeof(TargetPackageDomain) - 1U);
    blake3_hasher_update(&hasher, bytes, TargetPackageIdOffset);
    blake3_hasher_update(&hasher, zero.data(), zero.size());
    if (size > TargetPackageHeaderBytes) {
        blake3_hasher_update(
            &hasher,
            bytes + TargetPackageHeaderBytes,
            size - TargetPackageHeaderBytes);
    }
    Finish(&hasher, output);
}

bool WriteCompileReceipt(
    TargetPackageWriter* const writer,
    const laplace_target_compile_receipt& receipt) noexcept {
    return writer != nullptr &&
        writer->Digest(receipt.receipt_id) &&
        writer->Digest(receipt.request_fingerprint) &&
        writer->Digest(receipt.evidence_boundary) &&
        writer->Digest(receipt.evidence_epoch) &&
        writer->Digest(receipt.recipe_fingerprint) &&
        writer->Digest(receipt.target_contract_fingerprint) &&
        writer->U64(receipt.slot_count) &&
        writer->U64(receipt.distinct_operator_count) &&
        writer->U64(receipt.matrix_value_count) &&
        writer->U64(receipt.rhs_value_count) &&
        writer->U32(receipt.flags) &&
        writer->U32(receipt.status) &&
        writer->U32(receipt.version) &&
        writer->U32(receipt.reserved);
}

bool ReadCompileReceipt(
    TargetPackageReader* const reader,
    laplace_target_compile_receipt* const receipt) noexcept {
    if (reader == nullptr || receipt == nullptr) return false;
    *receipt = laplace_target_compile_receipt{};
    return reader->Digest(&receipt->receipt_id) &&
        reader->Digest(&receipt->request_fingerprint) &&
        reader->Digest(&receipt->evidence_boundary) &&
        reader->Digest(&receipt->evidence_epoch) &&
        reader->Digest(&receipt->recipe_fingerprint) &&
        reader->Digest(&receipt->target_contract_fingerprint) &&
        reader->U64(&receipt->slot_count) &&
        reader->U64(&receipt->distinct_operator_count) &&
        reader->U64(&receipt->matrix_value_count) &&
        reader->U64(&receipt->rhs_value_count) &&
        reader->U32(&receipt->flags) &&
        reader->U32(&receipt->status) &&
        reader->U32(&receipt->version) &&
        reader->U32(&receipt->reserved);
}

bool WriteSlotReceipt(
    TargetPackageWriter* const writer,
    const laplace_target_compile_slot_receipt& slot) noexcept {
    return writer != nullptr &&
        writer->Digest(slot.slot_id) &&
        writer->Digest(slot.role_fingerprint) &&
        writer->Digest(slot.operator_id) &&
        writer->Digest(slot.operator_receipt_id) &&
        writer->Digest(slot.program_fingerprint) &&
        writer->Digest(slot.field_set_fingerprint) &&
        writer->Digest(slot.constraint_set_fingerprint) &&
        writer->Digest(slot.materialization_receipt_id) &&
        writer->Digest(slot.matrix_fingerprint) &&
        writer->U64(slot.field_count) &&
        writer->U64(slot.selected_constraint_count) &&
        writer->U64(slot.matrix_value_count) &&
        writer->U64(slot.rhs_value_count) &&
        writer->U32(slot.target_role) &&
        writer->U32(slot.layer_index) &&
        writer->U32(slot.head_index) &&
        writer->U32(slot.expert_index) &&
        writer->U32(slot.relation_plane_count) &&
        writer->U32(slot.status);
}

bool ReadSlotReceipt(
    TargetPackageReader* const reader,
    laplace_target_compile_slot_receipt* const slot) noexcept {
    if (reader == nullptr || slot == nullptr) return false;
    *slot = laplace_target_compile_slot_receipt{};
    return reader->Digest(&slot->slot_id) &&
        reader->Digest(&slot->role_fingerprint) &&
        reader->Digest(&slot->operator_id) &&
        reader->Digest(&slot->operator_receipt_id) &&
        reader->Digest(&slot->program_fingerprint) &&
        reader->Digest(&slot->field_set_fingerprint) &&
        reader->Digest(&slot->constraint_set_fingerprint) &&
        reader->Digest(&slot->materialization_receipt_id) &&
        reader->Digest(&slot->matrix_fingerprint) &&
        reader->U64(&slot->field_count) &&
        reader->U64(&slot->selected_constraint_count) &&
        reader->U64(&slot->matrix_value_count) &&
        reader->U64(&slot->rhs_value_count) &&
        reader->U32(&slot->target_role) &&
        reader->U32(&slot->layer_index) &&
        reader->U32(&slot->head_index) &&
        reader->U32(&slot->expert_index) &&
        reader->U32(&slot->relation_plane_count) &&
        reader->U32(&slot->status);
}

void ReidentifyCompileReceipt(
    const laplace_target_compile_receipt& receipt,
    const std::vector<laplace_target_compile_slot_receipt>& slots,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetReceiptDomain, sizeof(TargetReceiptDomain) - 1U);
    HashDigest(&hasher, receipt.request_fingerprint);
    for (const auto& slot : slots) {
        HashDigest(&hasher, slot.slot_id);
        HashDigest(&hasher, slot.materialization_receipt_id);
        HashDigest(&hasher, slot.matrix_fingerprint);
    }
    HashU64(&hasher, receipt.slot_count);
    HashU64(&hasher, receipt.distinct_operator_count);
    HashU64(&hasher, receipt.matrix_value_count);
    HashU64(&hasher, receipt.rhs_value_count);
    HashU32(&hasher, receipt.flags);
    HashU32(&hasher, receipt.version);
    Finish(&hasher, output);
}

laplace_target_package_status ValidateCompileArtifact(
    const laplace_target_compile_result& result,
    const laplace_target_compile_receipt& receipt) {
    if (receipt.status != LAPLACE_TARGET_COMPILE_OK ||
        receipt.version != LAPLACE_TARGET_COMPILE_VERSION ||
        receipt.reserved != 0U ||
        (receipt.flags & ~LAPLACE_TARGET_COMPILE_KNOWN_FLAGS) != 0U ||
        Zero(receipt.receipt_id) || Zero(receipt.request_fingerprint) ||
        Zero(receipt.evidence_boundary) || Zero(receipt.evidence_epoch) ||
        Zero(receipt.recipe_fingerprint) || Zero(receipt.target_contract_fingerprint) ||
        result.slots.empty() ||
        result.slots.size() != result.matrices.size() ||
        result.slots.size() != result.rhs.size() ||
        receipt.slot_count != static_cast<std::uint64_t>(result.slots.size())) {
        return LAPLACE_TARGET_PACKAGE_INVALID_COMPILE_RESULT;
    }

    std::set<DigestKey> distinct_operators;
    std::uint64_t matrix_values = 0U;
    std::uint64_t rhs_values = 0U;
    for (std::size_t index = 0U; index < result.slots.size(); ++index) {
        const auto& slot = result.slots[index];
        const auto& matrix = result.matrices[index];
        const auto& rhs = result.rhs[index];
        if (!TargetRoleValid(slot.target_role) ||
            slot.status != LAPLACE_TARGET_COMPILE_OK ||
            slot.field_count != static_cast<std::uint64_t>(rhs.size()) ||
            slot.matrix_value_count != static_cast<std::uint64_t>(matrix.size()) ||
            slot.rhs_value_count != static_cast<std::uint64_t>(rhs.size()) ||
            slot.field_count > std::numeric_limits<std::uint64_t>::max() /
                slot.field_count ||
            slot.matrix_value_count != slot.field_count * slot.field_count) {
            return LAPLACE_TARGET_PACKAGE_INVALID_COMPILE_RESULT;
        }
        if (index != 0U) {
            const auto& prior = result.slots[index - 1U];
            const auto prior_key = std::tie(
                prior.target_role, prior.layer_index, prior.head_index,
                prior.expert_index);
            const auto current_key = std::tie(
                slot.target_role, slot.layer_index, slot.head_index,
                slot.expert_index);
            if (!(prior_key < current_key)) {
                return LAPLACE_TARGET_PACKAGE_INVALID_COMPILE_RESULT;
            }
        }

        laplace_digest256 matrix_fingerprint{};
        TargetMatrixFingerprint(slot, matrix, rhs, &matrix_fingerprint);
        if (!Same(matrix_fingerprint, slot.matrix_fingerprint)) {
            return LAPLACE_TARGET_PACKAGE_CORRUPT;
        }
        auto identified = slot;
        TargetSlotIdentify(&identified);
        if (!Same(identified.slot_id, slot.slot_id)) {
            return LAPLACE_TARGET_PACKAGE_CORRUPT;
        }

        if (matrix_values > std::numeric_limits<std::uint64_t>::max() -
                slot.matrix_value_count ||
            rhs_values > std::numeric_limits<std::uint64_t>::max() -
                slot.rhs_value_count) {
            return LAPLACE_TARGET_PACKAGE_OVERFLOW;
        }
        matrix_values += slot.matrix_value_count;
        rhs_values += slot.rhs_value_count;
        distinct_operators.insert(Key(slot.operator_id));
    }

    if (receipt.distinct_operator_count !=
            static_cast<std::uint64_t>(distinct_operators.size()) ||
        receipt.matrix_value_count != matrix_values ||
        receipt.rhs_value_count != rhs_values) {
        return LAPLACE_TARGET_PACKAGE_INVALID_COMPILE_RESULT;
    }
    laplace_digest256 receipt_id{};
    ReidentifyCompileReceipt(receipt, result.slots, &receipt_id);
    if (!Same(receipt_id, receipt.receipt_id)) {
        return LAPLACE_TARGET_PACKAGE_CORRUPT;
    }
    return LAPLACE_TARGET_PACKAGE_OK;
}

laplace_target_package_status MeasureCompileArtifact(
    const laplace_target_compile_result& result,
    const laplace_target_compile_receipt& receipt,
    std::size_t* const required_bytes) {
    const auto validation = ValidateCompileArtifact(result, receipt);
    if (validation != LAPLACE_TARGET_PACKAGE_OK) return validation;

    std::size_t total = TargetPackageHeaderBytes;
    if (!TargetPackageAdd(total, TargetCompileReceiptEncodedBytes, &total)) {
        return LAPLACE_TARGET_PACKAGE_OVERFLOW;
    }
    for (std::size_t index = 0U; index < result.slots.size(); ++index) {
        std::size_t value_count{};
        std::size_t value_bytes{};
        if (!TargetPackageAdd(
                result.matrices[index].size(), result.rhs[index].size(),
                &value_count) ||
            !TargetPackageMultiply(value_count, sizeof(double), &value_bytes) ||
            !TargetPackageAdd(total, TargetSlotReceiptEncodedBytes, &total) ||
            !TargetPackageAdd(total, value_bytes, &total)) {
            return LAPLACE_TARGET_PACKAGE_OVERFLOW;
        }
    }
    *required_bytes = total;
    return LAPLACE_TARGET_PACKAGE_OK;
}

void FillPackageReceipt(
    const laplace_digest256& package_id,
    const std::size_t byte_count,
    const laplace_target_compile_receipt& compile_receipt,
    laplace_target_package_receipt* const receipt) {
    *receipt = laplace_target_package_receipt{};
    receipt->package_id = package_id;
    receipt->compile_receipt_id = compile_receipt.receipt_id;
    receipt->request_fingerprint = compile_receipt.request_fingerprint;
    receipt->evidence_boundary = compile_receipt.evidence_boundary;
    receipt->evidence_epoch = compile_receipt.evidence_epoch;
    receipt->recipe_fingerprint = compile_receipt.recipe_fingerprint;
    receipt->target_contract_fingerprint = compile_receipt.target_contract_fingerprint;
    receipt->byte_count = static_cast<std::uint64_t>(byte_count);
    receipt->slot_count = compile_receipt.slot_count;
    receipt->distinct_operator_count = compile_receipt.distinct_operator_count;
    receipt->matrix_value_count = compile_receipt.matrix_value_count;
    receipt->rhs_value_count = compile_receipt.rhs_value_count;
    receipt->flags = compile_receipt.flags;
    receipt->status = LAPLACE_TARGET_PACKAGE_OK;
    receipt->version = LAPLACE_TARGET_PACKAGE_VERSION;
}

laplace_target_package_status CopyTargetValues(
    const std::vector<double>& values,
    double* const output,
    const std::size_t output_capacity,
    std::size_t* const required_count) {
    if (required_count == nullptr) return LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT;
    *required_count = values.size();
    if (values.empty()) return LAPLACE_TARGET_PACKAGE_OK;
    if (output == nullptr || output_capacity < values.size()) {
        return LAPLACE_TARGET_PACKAGE_CAPACITY_INSUFFICIENT;
    }
    std::copy(values.begin(), values.end(), output);
    return LAPLACE_TARGET_PACKAGE_OK;
}

}  // namespace

struct laplace_target_package {
    laplace_target_compile_result compiled;
    laplace_target_compile_receipt compile_receipt{};
    laplace_target_package_receipt receipt{};
};

extern "C" laplace_target_package_status laplace_target_package_measure(
    const laplace_target_compile_result* const result,
    const laplace_target_compile_receipt* const compile_receipt,
    size_t* const required_bytes) {
    if (result == nullptr || compile_receipt == nullptr || required_bytes == nullptr) {
        return LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT;
    }
    *required_bytes = 0U;
    return MeasureCompileArtifact(*result, *compile_receipt, required_bytes);
}

extern "C" laplace_target_package_status laplace_target_package_encode(
    const laplace_target_compile_result* const result,
    const laplace_target_compile_receipt* const compile_receipt,
    uint8_t* const output,
    const size_t output_capacity,
    laplace_target_package_receipt* const receipt) {
    if (result == nullptr || compile_receipt == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT;
    }
    *receipt = laplace_target_package_receipt{};
    std::size_t required = 0U;
    const auto measure = MeasureCompileArtifact(*result, *compile_receipt, &required);
    if (measure != LAPLACE_TARGET_PACKAGE_OK) return measure;
    if (output == nullptr || output_capacity < required) {
        return LAPLACE_TARGET_PACKAGE_CAPACITY_INSUFFICIENT;
    }

    TargetPackageWriter writer(output, output_capacity);
    const std::array<std::uint8_t, TargetPackageIdBytes> zero{};
    if (!writer.Bytes(TargetPackageMagic.data(), TargetPackageMagic.size()) ||
        !writer.U32(LAPLACE_TARGET_PACKAGE_VERSION) || !writer.U32(0U) ||
        !writer.U64(static_cast<std::uint64_t>(required)) ||
        !writer.Bytes(zero.data(), zero.size()) ||
        !WriteCompileReceipt(&writer, *compile_receipt)) {
        return LAPLACE_TARGET_PACKAGE_CAPACITY_INSUFFICIENT;
    }
    for (std::size_t index = 0U; index < result->slots.size(); ++index) {
        if (!WriteSlotReceipt(&writer, result->slots[index])) {
            return LAPLACE_TARGET_PACKAGE_CAPACITY_INSUFFICIENT;
        }
        for (const double value : result->matrices[index]) {
            if (!writer.F64(value)) return LAPLACE_TARGET_PACKAGE_CAPACITY_INSUFFICIENT;
        }
        for (const double value : result->rhs[index]) {
            if (!writer.F64(value)) return LAPLACE_TARGET_PACKAGE_CAPACITY_INSUFFICIENT;
        }
    }
    if (writer.offset() != required) return LAPLACE_TARGET_PACKAGE_CORRUPT;

    laplace_digest256 package_id{};
    TargetPackageDigest(output, required, &package_id);
    std::memcpy(
        output + TargetPackageIdOffset, package_id.bytes, sizeof(package_id.bytes));
    FillPackageReceipt(package_id, required, *compile_receipt, receipt);
    return LAPLACE_TARGET_PACKAGE_OK;
}

extern "C" laplace_target_package_status laplace_target_package_decode(
    const uint8_t* const input,
    const size_t input_size,
    laplace_target_package** const package,
    laplace_target_package_receipt* const receipt) {
    if (input == nullptr || package == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT;
    }
    *package = nullptr;
    *receipt = laplace_target_package_receipt{};
    if (input_size < TargetPackageHeaderBytes + TargetCompileReceiptEncodedBytes) {
        return LAPLACE_TARGET_PACKAGE_INVALID_FORMAT;
    }

    TargetPackageReader reader(input, input_size);
    std::array<std::uint8_t, TargetPackageMagic.size()> magic{};
    std::uint32_t version{};
    std::uint32_t reserved{};
    std::uint64_t encoded_size{};
    laplace_digest256 stored_package_id{};
    if (!reader.Bytes(magic.data(), magic.size()) ||
        !reader.U32(&version) || !reader.U32(&reserved) ||
        !reader.U64(&encoded_size) || !reader.Digest(&stored_package_id) ||
        magic != TargetPackageMagic || version != LAPLACE_TARGET_PACKAGE_VERSION ||
        reserved != 0U || encoded_size != static_cast<std::uint64_t>(input_size)) {
        return LAPLACE_TARGET_PACKAGE_INVALID_FORMAT;
    }

    laplace_digest256 calculated_package_id{};
    TargetPackageDigest(input, input_size, &calculated_package_id);
    if (!Same(stored_package_id, calculated_package_id)) {
        return LAPLACE_TARGET_PACKAGE_CORRUPT;
    }

    auto decoded = std::unique_ptr<laplace_target_package>(
        new (std::nothrow) laplace_target_package());
    if (!decoded) return LAPLACE_TARGET_PACKAGE_MEMORY_FAILURE;
    if (!ReadCompileReceipt(&reader, &decoded->compile_receipt) ||
        decoded->compile_receipt.slot_count == 0U ||
        decoded->compile_receipt.slot_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return LAPLACE_TARGET_PACKAGE_INVALID_FORMAT;
    }

    const auto slot_count =
        static_cast<std::size_t>(decoded->compile_receipt.slot_count);
    try {
        decoded->compiled.slots.resize(slot_count);
        decoded->compiled.matrices.resize(slot_count);
        decoded->compiled.rhs.resize(slot_count);
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_PACKAGE_MEMORY_FAILURE;
    }

    for (std::size_t index = 0U; index < slot_count; ++index) {
        auto& slot = decoded->compiled.slots[index];
        if (!ReadSlotReceipt(&reader, &slot) ||
            slot.matrix_value_count >
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            slot.rhs_value_count >
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return LAPLACE_TARGET_PACKAGE_INVALID_FORMAT;
        }
        const auto matrix_count = static_cast<std::size_t>(slot.matrix_value_count);
        const auto rhs_count = static_cast<std::size_t>(slot.rhs_value_count);
        std::size_t value_count{};
        std::size_t value_bytes{};
        if (!TargetPackageAdd(matrix_count, rhs_count, &value_count) ||
            !TargetPackageMultiply(value_count, sizeof(double), &value_bytes) ||
            value_bytes > reader.remaining()) {
            return LAPLACE_TARGET_PACKAGE_INVALID_FORMAT;
        }
        try {
            decoded->compiled.matrices[index].resize(matrix_count);
            decoded->compiled.rhs[index].resize(rhs_count);
        } catch (const std::bad_alloc&) {
            return LAPLACE_TARGET_PACKAGE_MEMORY_FAILURE;
        }
        for (double& value : decoded->compiled.matrices[index]) {
            if (!reader.F64(&value)) return LAPLACE_TARGET_PACKAGE_INVALID_FORMAT;
        }
        for (double& value : decoded->compiled.rhs[index]) {
            if (!reader.F64(&value)) return LAPLACE_TARGET_PACKAGE_INVALID_FORMAT;
        }
    }
    if (reader.offset() != input_size) return LAPLACE_TARGET_PACKAGE_INVALID_FORMAT;

    const auto validation =
        ValidateCompileArtifact(decoded->compiled, decoded->compile_receipt);
    if (validation != LAPLACE_TARGET_PACKAGE_OK) return validation;
    FillPackageReceipt(
        calculated_package_id, input_size, decoded->compile_receipt,
        &decoded->receipt);
    *receipt = decoded->receipt;
    *package = decoded.release();
    return LAPLACE_TARGET_PACKAGE_OK;
}

extern "C" void laplace_target_package_destroy(
    laplace_target_package** const package) {
    if (package == nullptr) return;
    delete *package;
    *package = nullptr;
}

extern "C" laplace_target_package_status laplace_target_package_compile_receipt(
    const laplace_target_package* const package,
    laplace_target_compile_receipt* const receipt) {
    if (package == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT;
    }
    *receipt = package->compile_receipt;
    return LAPLACE_TARGET_PACKAGE_OK;
}

extern "C" size_t laplace_target_package_slot_count(
    const laplace_target_package* const package) {
    return package == nullptr ? 0U : package->compiled.slots.size();
}

extern "C" laplace_target_package_status laplace_target_package_slot_receipt(
    const laplace_target_package* const package,
    const size_t slot_index,
    laplace_target_compile_slot_receipt* const receipt) {
    if (package == nullptr || receipt == nullptr ||
        slot_index >= package->compiled.slots.size()) {
        return LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT;
    }
    *receipt = package->compiled.slots[slot_index];
    return LAPLACE_TARGET_PACKAGE_OK;
}

extern "C" laplace_target_package_status laplace_target_package_matrix(
    const laplace_target_package* const package,
    const size_t slot_index,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (package == nullptr || slot_index >= package->compiled.matrices.size()) {
        return LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT;
    }
    return CopyTargetValues(
        package->compiled.matrices[slot_index], output, output_capacity,
        required_count);
}

extern "C" laplace_target_package_status laplace_target_package_rhs(
    const laplace_target_package* const package,
    const size_t slot_index,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (package == nullptr || slot_index >= package->compiled.rhs.size()) {
        return LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT;
    }
    return CopyTargetValues(
        package->compiled.rhs[slot_index], output, output_capacity,
        required_count);
}
