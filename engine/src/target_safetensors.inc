#include "laplace/target_safetensors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr char TargetSafeTensorsDomain[] = "laplace-target-safetensors-v1";
constexpr char TargetSafeTensorsSchema[] = "laplace.target-neutral-safetensors/v1";

const char* TargetSafeTensorRoleName(const std::uint32_t role) noexcept {
    switch (role) {
        case LAPLACE_TARGET_ROLE_COMPATIBILITY_QK: return "qk";
        case LAPLACE_TARGET_ROLE_CONTRIBUTION_VO: return "vo";
        case LAPLACE_TARGET_ROLE_TRANSFORM_FFN: return "ffn";
        case LAPLACE_TARGET_ROLE_ROUTING: return "routing";
        case LAPLACE_TARGET_ROLE_POSITION: return "position";
        case LAPLACE_TARGET_ROLE_EMBEDDING: return "embedding";
        case LAPLACE_TARGET_ROLE_OUTPUT: return "output";
        default: return nullptr;
    }
}

std::string TargetSafeTensorHex(const laplace_digest256& digest) {
    constexpr char Hex[] = "0123456789abcdef";
    std::string result(sizeof(digest.bytes) * 2U, '0');
    for (std::size_t index = 0U; index < sizeof(digest.bytes); ++index) {
        result[index * 2U] = Hex[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = Hex[digest.bytes[index] & 0x0fU];
    }
    return result;
}

void TargetSafeTensorMetadata(
    std::string* const header,
    const std::string& key,
    const std::string& value,
    bool* const first) {
    if (!*first) header->push_back(',');
    *first = false;
    header->append("\"");
    header->append(key);
    header->append("\":\"");
    header->append(value);
    header->push_back('"');
}

std::string TargetSafeTensorName(
    const laplace_target_compile_slot_receipt& slot,
    const char* const suffix) {
    const char* const role = TargetSafeTensorRoleName(slot.target_role);
    if (role == nullptr) return {};
    return std::string("laplace.") + role + ".layer_" +
        std::to_string(slot.layer_index) + ".head_" +
        std::to_string(slot.head_index) + ".expert_" +
        std::to_string(slot.expert_index) + "." + suffix;
}

laplace_target_safetensors_status BuildSafeTensorsHeader(
    const laplace_target_package& package,
    std::string* const header,
    std::size_t* const data_bytes,
    std::uint64_t* const tensor_count) {
    if (header == nullptr || data_bytes == nullptr || tensor_count == nullptr ||
        package.compiled.slots.empty()) {
        return LAPLACE_TARGET_SAFETENSORS_INVALID_PACKAGE;
    }
    try {
        std::size_t values = 0U;
        for (std::size_t index = 0U; index < package.compiled.slots.size(); ++index) {
            const auto& slot = package.compiled.slots[index];
            if (TargetSafeTensorRoleName(slot.target_role) == nullptr ||
                package.compiled.matrices[index].size() !=
                    static_cast<std::size_t>(slot.matrix_value_count) ||
                package.compiled.rhs[index].size() !=
                    static_cast<std::size_t>(slot.rhs_value_count) ||
                !TargetPackageAdd(
                    values, package.compiled.matrices[index].size(), &values) ||
                !TargetPackageAdd(
                    values, package.compiled.rhs[index].size(), &values)) {
                return LAPLACE_TARGET_SAFETENSORS_INVALID_PACKAGE;
            }
        }
        if (!TargetPackageMultiply(values, sizeof(double), data_bytes) ||
            package.compiled.slots.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max() / 2U)) {
            return LAPLACE_TARGET_SAFETENSORS_OVERFLOW;
        }
        *tensor_count =
            static_cast<std::uint64_t>(package.compiled.slots.size()) * 2U;

        header->clear();
        header->reserve(1024U + package.compiled.slots.size() * 768U);
        header->append("{\"__metadata__\":{");
        bool first_metadata = true;
        TargetSafeTensorMetadata(
            header, "laplace_schema", TargetSafeTensorsSchema, &first_metadata);
        TargetSafeTensorMetadata(
            header, "laplace_package_id",
            TargetSafeTensorHex(package.receipt.package_id), &first_metadata);
        TargetSafeTensorMetadata(
            header, "laplace_compile_receipt_id",
            TargetSafeTensorHex(package.compile_receipt.receipt_id), &first_metadata);
        TargetSafeTensorMetadata(
            header, "laplace_request_fingerprint",
            TargetSafeTensorHex(package.compile_receipt.request_fingerprint),
            &first_metadata);
        TargetSafeTensorMetadata(
            header, "laplace_evidence_boundary",
            TargetSafeTensorHex(package.compile_receipt.evidence_boundary),
            &first_metadata);
        TargetSafeTensorMetadata(
            header, "laplace_evidence_epoch",
            TargetSafeTensorHex(package.compile_receipt.evidence_epoch),
            &first_metadata);
        TargetSafeTensorMetadata(
            header, "laplace_recipe_fingerprint",
            TargetSafeTensorHex(package.compile_receipt.recipe_fingerprint),
            &first_metadata);
        TargetSafeTensorMetadata(
            header, "laplace_target_contract_fingerprint",
            TargetSafeTensorHex(package.compile_receipt.target_contract_fingerprint),
            &first_metadata);
        TargetSafeTensorMetadata(
            header, "laplace_slot_count",
            std::to_string(package.compiled.slots.size()), &first_metadata);
        for (std::size_t index = 0U; index < package.compiled.slots.size(); ++index) {
            const auto& slot = package.compiled.slots[index];
            const auto prefix = std::string("laplace_slot_") + std::to_string(index);
            TargetSafeTensorMetadata(
                header, prefix + "_id", TargetSafeTensorHex(slot.slot_id),
                &first_metadata);
            TargetSafeTensorMetadata(
                header, prefix + "_operator_id",
                TargetSafeTensorHex(slot.operator_id), &first_metadata);
            TargetSafeTensorMetadata(
                header, prefix + "_matrix_fingerprint",
                TargetSafeTensorHex(slot.matrix_fingerprint), &first_metadata);
            TargetSafeTensorMetadata(
                header, prefix + "_role", std::to_string(slot.target_role),
                &first_metadata);
            TargetSafeTensorMetadata(
                header, prefix + "_layer", std::to_string(slot.layer_index),
                &first_metadata);
            TargetSafeTensorMetadata(
                header, prefix + "_head", std::to_string(slot.head_index),
                &first_metadata);
            TargetSafeTensorMetadata(
                header, prefix + "_expert", std::to_string(slot.expert_index),
                &first_metadata);
        }
        header->push_back('}');

        std::uint64_t data_offset = 0U;
        for (std::size_t index = 0U; index < package.compiled.slots.size(); ++index) {
            const auto& slot = package.compiled.slots[index];
            const auto matrix_name = TargetSafeTensorName(slot, "matrix");
            const auto rhs_name = TargetSafeTensorName(slot, "rhs");
            if (matrix_name.empty() || rhs_name.empty()) {
                return LAPLACE_TARGET_SAFETENSORS_INVALID_PACKAGE;
            }
            const std::uint64_t matrix_bytes = slot.matrix_value_count * sizeof(double);
            const std::uint64_t rhs_bytes = slot.rhs_value_count * sizeof(double);
            if (matrix_bytes > std::numeric_limits<std::uint64_t>::max() - data_offset) {
                return LAPLACE_TARGET_SAFETENSORS_OVERFLOW;
            }
            const std::uint64_t matrix_end = data_offset + matrix_bytes;
            if (rhs_bytes > std::numeric_limits<std::uint64_t>::max() - matrix_end) {
                return LAPLACE_TARGET_SAFETENSORS_OVERFLOW;
            }
            const std::uint64_t rhs_end = matrix_end + rhs_bytes;

            header->append(",\"");
            header->append(matrix_name);
            header->append("\":{\"dtype\":\"F64\",\"shape\":[");
            header->append(std::to_string(slot.field_count));
            header->push_back(',');
            header->append(std::to_string(slot.field_count));
            header->append("],\"data_offsets\":[");
            header->append(std::to_string(data_offset));
            header->push_back(',');
            header->append(std::to_string(matrix_end));
            header->append("]}");

            header->append(",\"");
            header->append(rhs_name);
            header->append("\":{\"dtype\":\"F64\",\"shape\":[");
            header->append(std::to_string(slot.field_count));
            header->append("],\"data_offsets\":[");
            header->append(std::to_string(matrix_end));
            header->push_back(',');
            header->append(std::to_string(rhs_end));
            header->append("]}");
            data_offset = rhs_end;
        }
        header->push_back('}');
        while ((header->size() % 8U) != 0U) header->push_back(' ');
        return LAPLACE_TARGET_SAFETENSORS_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_SAFETENSORS_MEMORY_FAILURE;
    }
}

void TargetSafeTensorsDigest(
    const std::uint8_t* const artifact,
    const std::size_t artifact_size,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetSafeTensorsDomain, sizeof(TargetSafeTensorsDomain) - 1U);
    blake3_hasher_update(&hasher, artifact, artifact_size);
    Finish(&hasher, output);
}

void FillSafeTensorsReceipt(
    const laplace_target_package& package,
    const laplace_digest256& artifact_id,
    const std::size_t byte_count,
    const std::size_t header_bytes,
    const std::size_t data_bytes,
    const std::uint64_t tensor_count,
    laplace_target_safetensors_receipt* const receipt) {
    *receipt = laplace_target_safetensors_receipt{};
    receipt->artifact_id = artifact_id;
    receipt->target_package_id = package.receipt.package_id;
    receipt->compile_receipt_id = package.compile_receipt.receipt_id;
    receipt->request_fingerprint = package.compile_receipt.request_fingerprint;
    receipt->byte_count = static_cast<std::uint64_t>(byte_count);
    receipt->header_byte_count = static_cast<std::uint64_t>(header_bytes);
    receipt->data_byte_count = static_cast<std::uint64_t>(data_bytes);
    receipt->tensor_count = tensor_count;
    receipt->slot_count = static_cast<std::uint64_t>(package.compiled.slots.size());
    receipt->status = LAPLACE_TARGET_SAFETENSORS_OK;
    receipt->version = LAPLACE_TARGET_SAFETENSORS_VERSION;
}

laplace_target_safetensors_status DecodeSafeTensorPackage(
    const std::uint8_t* const target_package_bytes,
    const std::size_t target_package_size,
    laplace_target_package** const package) {
    laplace_target_package_receipt package_receipt{};
    const auto status = laplace_target_package_decode(
        target_package_bytes, target_package_size, package, &package_receipt);
    return status == LAPLACE_TARGET_PACKAGE_OK
        ? LAPLACE_TARGET_SAFETENSORS_OK
        : LAPLACE_TARGET_SAFETENSORS_INVALID_PACKAGE;
}

}  // namespace

extern "C" laplace_target_safetensors_status laplace_target_safetensors_encode(
    const uint8_t* const target_package_bytes,
    const size_t target_package_size,
    uint8_t* const output,
    const size_t output_capacity,
    size_t* const required_bytes,
    laplace_target_safetensors_receipt* const receipt) {
    if (target_package_bytes == nullptr || target_package_size == 0U ||
        required_bytes == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_SAFETENSORS_INVALID_ARGUMENT;
    }
    *required_bytes = 0U;
    *receipt = laplace_target_safetensors_receipt{};
    laplace_target_package* raw_package = nullptr;
    const auto decode_status = DecodeSafeTensorPackage(
        target_package_bytes, target_package_size, &raw_package);
    if (decode_status != LAPLACE_TARGET_SAFETENSORS_OK) return decode_status;
    std::unique_ptr<laplace_target_package, void (*)(laplace_target_package*)> package(
        raw_package,
        [](laplace_target_package* value) {
            laplace_target_package* owned = value;
            laplace_target_package_destroy(&owned);
        });

    std::string header;
    std::size_t data_bytes = 0U;
    std::uint64_t tensor_count = 0U;
    const auto header_status = BuildSafeTensorsHeader(
        *package, &header, &data_bytes, &tensor_count);
    if (header_status != LAPLACE_TARGET_SAFETENSORS_OK) return header_status;
    std::size_t total = 8U;
    if (!TargetPackageAdd(total, header.size(), &total) ||
        !TargetPackageAdd(total, data_bytes, &total)) {
        return LAPLACE_TARGET_SAFETENSORS_OVERFLOW;
    }
    *required_bytes = total;
    if (output == nullptr || output_capacity < total) {
        return LAPLACE_TARGET_SAFETENSORS_CAPACITY_INSUFFICIENT;
    }

    TargetPackageWriter writer(output, output_capacity);
    if (!writer.U64(static_cast<std::uint64_t>(header.size())) ||
        !writer.Bytes(header.data(), header.size())) {
        return LAPLACE_TARGET_SAFETENSORS_CAPACITY_INSUFFICIENT;
    }
    for (std::size_t index = 0U; index < package->compiled.slots.size(); ++index) {
        for (const double value : package->compiled.matrices[index]) {
            if (!writer.F64(value)) return LAPLACE_TARGET_SAFETENSORS_CAPACITY_INSUFFICIENT;
        }
        for (const double value : package->compiled.rhs[index]) {
            if (!writer.F64(value)) return LAPLACE_TARGET_SAFETENSORS_CAPACITY_INSUFFICIENT;
        }
    }
    if (writer.offset() != total) return LAPLACE_TARGET_SAFETENSORS_CORRUPT;

    laplace_digest256 artifact_id{};
    TargetSafeTensorsDigest(output, total, &artifact_id);
    FillSafeTensorsReceipt(
        *package, artifact_id, total, header.size(), data_bytes,
        tensor_count, receipt);
    return LAPLACE_TARGET_SAFETENSORS_OK;
}

extern "C" laplace_target_safetensors_status laplace_target_safetensors_validate(
    const uint8_t* const target_package_bytes,
    const size_t target_package_size,
    const uint8_t* const artifact,
    const size_t artifact_size,
    laplace_target_safetensors_receipt* const receipt) {
    if (target_package_bytes == nullptr || target_package_size == 0U ||
        artifact == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_SAFETENSORS_INVALID_ARGUMENT;
    }
    *receipt = laplace_target_safetensors_receipt{};
    laplace_target_package* raw_package = nullptr;
    const auto decode_status = DecodeSafeTensorPackage(
        target_package_bytes, target_package_size, &raw_package);
    if (decode_status != LAPLACE_TARGET_SAFETENSORS_OK) return decode_status;
    std::unique_ptr<laplace_target_package, void (*)(laplace_target_package*)> package(
        raw_package,
        [](laplace_target_package* value) {
            laplace_target_package* owned = value;
            laplace_target_package_destroy(&owned);
        });

    std::string header;
    std::size_t data_bytes = 0U;
    std::uint64_t tensor_count = 0U;
    const auto header_status = BuildSafeTensorsHeader(
        *package, &header, &data_bytes, &tensor_count);
    if (header_status != LAPLACE_TARGET_SAFETENSORS_OK) return header_status;
    std::size_t expected_size = 8U;
    if (!TargetPackageAdd(expected_size, header.size(), &expected_size) ||
        !TargetPackageAdd(expected_size, data_bytes, &expected_size)) {
        return LAPLACE_TARGET_SAFETENSORS_OVERFLOW;
    }
    if (artifact_size != expected_size) return LAPLACE_TARGET_SAFETENSORS_CORRUPT;

    TargetPackageReader reader(artifact, artifact_size);
    std::uint64_t encoded_header_size = 0U;
    if (!reader.U64(&encoded_header_size) ||
        encoded_header_size != static_cast<std::uint64_t>(header.size()) ||
        reader.remaining() < header.size() ||
        std::memcmp(
            artifact + reader.offset(), header.data(), header.size()) != 0) {
        return LAPLACE_TARGET_SAFETENSORS_CORRUPT;
    }
    std::string observed_header(header.size(), '\0');
    if (!reader.Bytes(observed_header.data(), observed_header.size())) {
        return LAPLACE_TARGET_SAFETENSORS_CORRUPT;
    }

    for (std::size_t index = 0U; index < package->compiled.slots.size(); ++index) {
        for (const double expected : package->compiled.matrices[index]) {
            double observed{};
            if (!reader.F64(&observed) ||
                std::memcmp(&observed, &expected, sizeof(expected)) != 0) {
                return LAPLACE_TARGET_SAFETENSORS_CORRUPT;
            }
        }
        for (const double expected : package->compiled.rhs[index]) {
            double observed{};
            if (!reader.F64(&observed) ||
                std::memcmp(&observed, &expected, sizeof(expected)) != 0) {
                return LAPLACE_TARGET_SAFETENSORS_CORRUPT;
            }
        }
    }
    if (reader.remaining() != 0U) return LAPLACE_TARGET_SAFETENSORS_CORRUPT;

    laplace_digest256 artifact_id{};
    TargetSafeTensorsDigest(artifact, artifact_size, &artifact_id);
    FillSafeTensorsReceipt(
        *package, artifact_id, artifact_size, header.size(), data_bytes,
        tensor_count, receipt);
    return LAPLACE_TARGET_SAFETENSORS_OK;
}
