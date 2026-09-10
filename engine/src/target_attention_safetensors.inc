#include "laplace/target_attention_safetensors.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr char AttentionSafeTensorsDomain[] =
    "laplace-target-attention-safetensors-v1";
constexpr char AttentionSafeTensorsSchema[] =
    "laplace.target-attention-safetensors/v1";

struct AttentionSafeLayout final {
    std::string header;
    std::vector<laplace_target_attention_head_receipt> heads;
    std::size_t embedding_values{};
    std::size_t data_bytes{};
    std::uint64_t tensor_count{};
};

bool AttentionSafeSameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) noexcept {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

std::string AttentionSafeTensorName(
    const char* const role,
    const laplace_target_attention_head_receipt& head) {
    return std::string("laplace.") + role + ".layer_" +
        std::to_string(head.layer_index) + ".head_" +
        std::to_string(head.head_index) + ".expert_" +
        std::to_string(head.expert_index);
}

laplace_target_attention_safetensors_status AttentionSafePreflightVector(
    const laplace_target_attention_projection_result* const projection,
    const std::size_t head_index,
    laplace_target_attention_projection_status (*reader)(
        const laplace_target_attention_projection_result*,
        std::size_t, double*, std::size_t, std::size_t*),
    const std::size_t expected) {
    std::size_t required = 0U;
    const auto status = reader(projection, head_index, nullptr, 0U, &required);
    if (status != LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT ||
        required != expected || required == 0U) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
    }
    return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK;
}

laplace_target_attention_safetensors_status AttentionSafeLayoutBuild(
    const laplace_target_compile_receipt* const compile_receipt,
    const laplace_target_attention_projection_result* const projection,
    const laplace_target_attention_projection_receipt* const projection_receipt,
    AttentionSafeLayout* const layout) {
    if (compile_receipt == nullptr || projection == nullptr ||
        projection_receipt == nullptr || layout == nullptr ||
        compile_receipt->status != LAPLACE_TARGET_COMPILE_OK ||
        compile_receipt->version != LAPLACE_TARGET_COMPILE_VERSION ||
        projection_receipt->status != LAPLACE_TARGET_ATTENTION_PROJECTION_OK ||
        projection_receipt->version != LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION ||
        projection_receipt->field_count == 0U ||
        projection_receipt->hidden_width == 0U ||
        projection_receipt->head_count == 0U) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
    }
    const std::size_t head_count =
        laplace_target_attention_projection_head_count(projection);
    if (projection_receipt->head_count != static_cast<std::uint64_t>(head_count) ||
        head_count == 0U ||
        projection_receipt->field_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        projection_receipt->hidden_width >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
    }

    std::size_t embedding_values = 0U;
    if (!TargetPackageMultiply(
            static_cast<std::size_t>(projection_receipt->field_count),
            static_cast<std::size_t>(projection_receipt->hidden_width),
            &embedding_values)) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OVERFLOW;
    }
    std::size_t observed_embedding_values = 0U;
    if (laplace_target_attention_projection_embedding(
            projection, nullptr, 0U, &observed_embedding_values) !=
            LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT ||
        observed_embedding_values != embedding_values) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
    }

    try {
        layout->heads.assign(head_count, laplace_target_attention_head_receipt{});
        std::size_t total_values = embedding_values;
        for (std::size_t index = 0U; index < head_count; ++index) {
            auto& head = layout->heads[index];
            if (laplace_target_attention_projection_head_receipt(
                    projection, index, &head) !=
                    LAPLACE_TARGET_ATTENTION_PROJECTION_OK ||
                head.status != LAPLACE_TARGET_ATTENTION_PROJECTION_OK ||
                head.hidden_width != projection_receipt->hidden_width ||
                head.field_count != projection_receipt->field_count ||
                head.head_rank == 0U ||
                head.head_rank >
                    static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
                return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
            }
            std::size_t values_per_tensor = 0U;
            if (!TargetPackageMultiply(
                    static_cast<std::size_t>(head.head_rank),
                    static_cast<std::size_t>(head.hidden_width),
                    &values_per_tensor)) {
                return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OVERFLOW;
            }
            if (AttentionSafePreflightVector(
                    projection, index, laplace_target_attention_projection_q,
                    values_per_tensor) != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK ||
                AttentionSafePreflightVector(
                    projection, index, laplace_target_attention_projection_k,
                    values_per_tensor) != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK ||
                AttentionSafePreflightVector(
                    projection, index, laplace_target_attention_projection_v,
                    values_per_tensor) != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK ||
                AttentionSafePreflightVector(
                    projection, index, laplace_target_attention_projection_o,
                    values_per_tensor) != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK) {
                return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
            }
            for (unsigned tensor = 0U; tensor < 4U; ++tensor) {
                if (!TargetPackageAdd(total_values, values_per_tensor, &total_values)) {
                    return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OVERFLOW;
                }
            }
        }
        if (!TargetPackageMultiply(total_values, sizeof(double), &layout->data_bytes) ||
            head_count >
                static_cast<std::size_t>((std::numeric_limits<std::uint64_t>::max() - 1U) / 4U)) {
            return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OVERFLOW;
        }
        layout->embedding_values = embedding_values;
        layout->tensor_count = 1U + static_cast<std::uint64_t>(head_count) * 4U;

        auto& header = layout->header;
        header.clear();
        header.reserve(1536U + head_count * 2048U);
        header.append("{\"__metadata__\":{");
        bool first_metadata = true;
        TargetSafeTensorMetadata(
            &header, "laplace_schema", AttentionSafeTensorsSchema, &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_compile_receipt_id",
            TargetSafeTensorHex(compile_receipt->receipt_id), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_compile_request_fingerprint",
            TargetSafeTensorHex(compile_receipt->request_fingerprint), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_evidence_boundary",
            TargetSafeTensorHex(compile_receipt->evidence_boundary), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_evidence_epoch",
            TargetSafeTensorHex(compile_receipt->evidence_epoch), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_recipe_fingerprint",
            TargetSafeTensorHex(compile_receipt->recipe_fingerprint), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_target_contract_fingerprint",
            TargetSafeTensorHex(compile_receipt->target_contract_fingerprint),
            &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_projection_id",
            TargetSafeTensorHex(projection_receipt->projection_id), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_embedding_fingerprint",
            TargetSafeTensorHex(projection_receipt->embedding_fingerprint),
            &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_head_set_fingerprint",
            TargetSafeTensorHex(projection_receipt->head_set_fingerprint),
            &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_field_count",
            std::to_string(projection_receipt->field_count), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_hidden_width",
            std::to_string(projection_receipt->hidden_width), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_semantic_basis_rank",
            std::to_string(projection_receipt->semantic_basis_rank), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_null_basis_rank",
            std::to_string(projection_receipt->null_basis_rank), &first_metadata);
        TargetSafeTensorMetadata(
            &header, "laplace_head_count",
            std::to_string(projection_receipt->head_count), &first_metadata);
        for (std::size_t index = 0U; index < head_count; ++index) {
            const auto& head = layout->heads[index];
            const auto prefix = std::string("laplace_head_") + std::to_string(index);
            TargetSafeTensorMetadata(
                &header, prefix + "_projection_id",
                TargetSafeTensorHex(head.head_projection_id), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_qk_slot_id",
                TargetSafeTensorHex(head.qk_slot_id), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_vo_slot_id",
                TargetSafeTensorHex(head.vo_slot_id), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_qk_operator_id",
                TargetSafeTensorHex(head.qk_operator_id), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_vo_operator_id",
                TargetSafeTensorHex(head.vo_operator_id), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_qk_factorization_id",
                TargetSafeTensorHex(head.qk_factorization_id), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_vo_factorization_id",
                TargetSafeTensorHex(head.vo_factorization_id), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_q_fingerprint",
                TargetSafeTensorHex(head.q_tensor_fingerprint), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_k_fingerprint",
                TargetSafeTensorHex(head.k_tensor_fingerprint), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_v_fingerprint",
                TargetSafeTensorHex(head.v_tensor_fingerprint), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_o_fingerprint",
                TargetSafeTensorHex(head.o_tensor_fingerprint), &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_layer", std::to_string(head.layer_index),
                &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_head", std::to_string(head.head_index),
                &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_expert", std::to_string(head.expert_index),
                &first_metadata);
            TargetSafeTensorMetadata(
                &header, prefix + "_rank", std::to_string(head.head_rank),
                &first_metadata);
        }
        header.push_back('}');

        std::uint64_t data_offset = 0U;
        const std::uint64_t embedding_bytes =
            static_cast<std::uint64_t>(embedding_values) * sizeof(double);
        header.append(",\"laplace.embedding\":{\"dtype\":\"F64\",\"shape\":[");
        header.append(std::to_string(projection_receipt->field_count));
        header.push_back(',');
        header.append(std::to_string(projection_receipt->hidden_width));
        header.append("],\"data_offsets\":[0,");
        header.append(std::to_string(embedding_bytes));
        header.append("]}");
        data_offset = embedding_bytes;

        for (const auto& head : layout->heads) {
            const std::uint64_t tensor_bytes =
                head.head_rank * head.hidden_width * sizeof(double);
            const char* const roles[] = {"q", "k", "v", "o"};
            for (const char* const role : roles) {
                if (tensor_bytes > std::numeric_limits<std::uint64_t>::max() - data_offset) {
                    return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OVERFLOW;
                }
                const std::uint64_t end = data_offset + tensor_bytes;
                header.append(",\"");
                header.append(AttentionSafeTensorName(role, head));
                header.append("\":{\"dtype\":\"F64\",\"shape\":[");
                if (std::strcmp(role, "o") == 0) {
                    header.append(std::to_string(head.hidden_width));
                    header.push_back(',');
                    header.append(std::to_string(head.head_rank));
                } else {
                    header.append(std::to_string(head.head_rank));
                    header.push_back(',');
                    header.append(std::to_string(head.hidden_width));
                }
                header.append("],\"data_offsets\":[");
                header.append(std::to_string(data_offset));
                header.push_back(',');
                header.append(std::to_string(end));
                header.append("]}");
                data_offset = end;
            }
        }
        if (data_offset != static_cast<std::uint64_t>(layout->data_bytes)) {
            return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
        }
        header.push_back('}');
        while ((header.size() % 8U) != 0U) header.push_back(' ');
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_MEMORY_FAILURE;
    }
}

void AttentionSafeDigest(
    const std::uint8_t* const artifact,
    const std::size_t artifact_size,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, AttentionSafeTensorsDomain, sizeof(AttentionSafeTensorsDomain) - 1U);
    blake3_hasher_update(&hasher, artifact, artifact_size);
    Finish(&hasher, output);
}

laplace_target_attention_safetensors_status AttentionSafeWriteVector(
    const laplace_target_attention_projection_result* const projection,
    const std::size_t head_index,
    laplace_target_attention_projection_status (*reader)(
        const laplace_target_attention_projection_result*,
        std::size_t, double*, std::size_t, std::size_t*),
    const std::size_t expected,
    TargetPackageWriter* const writer) {
    try {
        std::vector<double> values(expected);
        std::size_t required = expected;
        if (reader(
                projection, head_index, values.data(), values.size(), &required) !=
                LAPLACE_TARGET_ATTENTION_PROJECTION_OK || required != expected) {
            return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
        }
        for (const double value : values) {
            if (!writer->F64(value)) {
                return LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT;
            }
        }
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_MEMORY_FAILURE;
    }
}

void AttentionSafeFillReceipt(
    const laplace_target_compile_receipt& compile_receipt,
    const laplace_target_attention_projection_receipt& projection_receipt,
    const laplace_digest256& artifact_id,
    const AttentionSafeLayout& layout,
    const std::size_t byte_count,
    laplace_target_attention_safetensors_receipt* const receipt) {
    *receipt = laplace_target_attention_safetensors_receipt{};
    receipt->artifact_id = artifact_id;
    receipt->compile_receipt_id = compile_receipt.receipt_id;
    receipt->compile_request_fingerprint = compile_receipt.request_fingerprint;
    receipt->projection_id = projection_receipt.projection_id;
    receipt->embedding_fingerprint = projection_receipt.embedding_fingerprint;
    receipt->head_set_fingerprint = projection_receipt.head_set_fingerprint;
    receipt->byte_count = static_cast<std::uint64_t>(byte_count);
    receipt->header_byte_count = static_cast<std::uint64_t>(layout.header.size());
    receipt->data_byte_count = static_cast<std::uint64_t>(layout.data_bytes);
    receipt->tensor_count = layout.tensor_count;
    receipt->head_count = projection_receipt.head_count;
    receipt->field_count = projection_receipt.field_count;
    receipt->hidden_width = projection_receipt.hidden_width;
    receipt->max_relative_residual = projection_receipt.max_relative_residual;
    receipt->status = LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK;
    receipt->version = LAPLACE_TARGET_ATTENTION_SAFETENSORS_VERSION;
}

}  // namespace

extern "C" laplace_target_attention_safetensors_status
laplace_target_attention_safetensors_encode(
    const laplace_target_compile_receipt* const compile_receipt,
    const laplace_target_attention_projection_result* const projection,
    const laplace_target_attention_projection_receipt* const projection_receipt,
    uint8_t* const output,
    const size_t output_capacity,
    size_t* const required_bytes,
    laplace_target_attention_safetensors_receipt* const receipt) {
    if (compile_receipt == nullptr || projection == nullptr ||
        projection_receipt == nullptr || required_bytes == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_ARGUMENT;
    }
    *required_bytes = 0U;
    *receipt = laplace_target_attention_safetensors_receipt{};
    AttentionSafeLayout layout;
    const auto layout_status = AttentionSafeLayoutBuild(
        compile_receipt, projection, projection_receipt, &layout);
    if (layout_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK) {
        return layout_status;
    }
    std::size_t total = 8U;
    if (!TargetPackageAdd(total, layout.header.size(), &total) ||
        !TargetPackageAdd(total, layout.data_bytes, &total)) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OVERFLOW;
    }
    *required_bytes = total;
    if (output == nullptr || output_capacity < total) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT;
    }

    TargetPackageWriter writer(output, output_capacity);
    if (!writer.U64(static_cast<std::uint64_t>(layout.header.size())) ||
        !writer.Bytes(layout.header.data(), layout.header.size())) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT;
    }
    try {
        std::vector<double> embedding(layout.embedding_values);
        std::size_t embedding_required = embedding.size();
        if (laplace_target_attention_projection_embedding(
                projection, embedding.data(), embedding.size(), &embedding_required) !=
                LAPLACE_TARGET_ATTENTION_PROJECTION_OK ||
            embedding_required != embedding.size()) {
            return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION;
        }
        for (const double value : embedding) {
            if (!writer.F64(value)) {
                return LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT;
            }
        }
        for (std::size_t index = 0U; index < layout.heads.size(); ++index) {
            const auto& head = layout.heads[index];
            std::size_t values_per_tensor = 0U;
            if (!TargetPackageMultiply(
                    static_cast<std::size_t>(head.head_rank),
                    static_cast<std::size_t>(head.hidden_width),
                    &values_per_tensor)) {
                return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OVERFLOW;
            }
            const auto q_status = AttentionSafeWriteVector(
                projection, index, laplace_target_attention_projection_q,
                values_per_tensor, &writer);
            if (q_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK) return q_status;
            const auto k_status = AttentionSafeWriteVector(
                projection, index, laplace_target_attention_projection_k,
                values_per_tensor, &writer);
            if (k_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK) return k_status;
            const auto v_status = AttentionSafeWriteVector(
                projection, index, laplace_target_attention_projection_v,
                values_per_tensor, &writer);
            if (v_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK) return v_status;
            const auto o_status = AttentionSafeWriteVector(
                projection, index, laplace_target_attention_projection_o,
                values_per_tensor, &writer);
            if (o_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK) return o_status;
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_MEMORY_FAILURE;
    }
    if (writer.offset() != total) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_CORRUPT;
    }

    laplace_digest256 artifact_id{};
    AttentionSafeDigest(output, total, &artifact_id);
    AttentionSafeFillReceipt(
        *compile_receipt, *projection_receipt, artifact_id, layout, total, receipt);
    return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK;
}

extern "C" laplace_target_attention_safetensors_status
laplace_target_attention_safetensors_validate(
    const laplace_target_compile_receipt* const compile_receipt,
    const laplace_target_attention_projection_result* const projection,
    const laplace_target_attention_projection_receipt* const projection_receipt,
    const uint8_t* const artifact,
    const size_t artifact_size,
    laplace_target_attention_safetensors_receipt* const receipt) {
    if (compile_receipt == nullptr || projection == nullptr ||
        projection_receipt == nullptr || artifact == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_ARGUMENT;
    }
    *receipt = laplace_target_attention_safetensors_receipt{};
    std::size_t required = 0U;
    laplace_target_attention_safetensors_receipt measured{};
    const auto measure_status = laplace_target_attention_safetensors_encode(
        compile_receipt, projection, projection_receipt,
        nullptr, 0U, &required, &measured);
    if (measure_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT ||
        required == 0U) {
        return measure_status;
    }
    try {
        std::vector<std::uint8_t> canonical(required);
        laplace_target_attention_safetensors_receipt canonical_receipt{};
        const auto encode_status = laplace_target_attention_safetensors_encode(
            compile_receipt, projection, projection_receipt,
            canonical.data(), canonical.size(), &required, &canonical_receipt);
        if (encode_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK) {
            return encode_status;
        }
        if (artifact_size != canonical.size() ||
            std::memcmp(artifact, canonical.data(), canonical.size()) != 0) {
            return LAPLACE_TARGET_ATTENTION_SAFETENSORS_CORRUPT;
        }
        laplace_digest256 observed{};
        AttentionSafeDigest(artifact, artifact_size, &observed);
        if (!AttentionSafeSameDigest(observed, canonical_receipt.artifact_id)) {
            return LAPLACE_TARGET_ATTENTION_SAFETENSORS_CORRUPT;
        }
        *receipt = canonical_receipt;
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_ATTENTION_SAFETENSORS_MEMORY_FAILURE;
    }
}
