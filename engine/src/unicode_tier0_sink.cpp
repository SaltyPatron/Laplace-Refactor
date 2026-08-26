#include "laplace/unicode_tier0_sink.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "laplace/perfcache_modules.h"
#include "laplace/perfcache_registry.h"

struct laplace_unicode_tier0_sink {
    std::string target_path;
    laplace_unicode_root_stream_expectation expectation{};
    laplace_perfcache_contract contract{};
    laplace_perfcache_module_v2 module{};
    laplace_unicode_root_stream_validator* validator{};
    laplace_perfcache_file_builder* builder{};
    laplace_unicode_tier0_sink_result result{};
    std::uint64_t expected_root_frames{};
    std::uint64_t expected_root_bytes{};
    std::uint64_t staged_root_frames{};
    std::uint64_t staged_root_bytes{};
    std::uint64_t atom_count{};
    std::uint64_t metadata_bytes{};
    bool begun{};
    bool sealed{};
    bool aborted{};
};

namespace {

constexpr std::size_t Tier0RecordBytes =
    LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_BYTES +
    LAPLACE_PERFCACHE_UNICODE_TIER0_VALUE_BYTES;

bool DigestEqual(
    const laplace_digest256& left,
    const laplace_digest256& right) noexcept {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

void WriteU32(std::uint8_t* destination, const std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void WriteU64(std::uint8_t* destination, const std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void ResetRuntime(laplace_unicode_tier0_sink* const state) noexcept {
    if (state == nullptr) {
        return;
    }
    laplace_perfcache_file_builder_destroy(&state->builder);
    laplace_unicode_root_stream_validator_destroy(state->validator);
    state->validator = nullptr;
    state->expected_root_frames = 0U;
    state->expected_root_bytes = 0U;
    state->staged_root_frames = 0U;
    state->staged_root_bytes = 0U;
    state->atom_count = 0U;
    state->metadata_bytes = 0U;
    state->begun = false;
}

laplace_perfcache_status BuildContract(
    const laplace_unicode_tier0_sink_configuration& configuration,
    laplace_perfcache_module_v2* const module,
    laplace_perfcache_contract* const contract) noexcept {
    if (module == nullptr || contract == nullptr ||
        laplace_perfcache_unicode_tier0_module(module) !=
            LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_PERFCACHE_CONTRACT_MISMATCH;
    }
    *contract = laplace_perfcache_contract{};
    contract->module_id = module->module_id;
    contract->key_schema_id = module->key_schema_id;
    contract->value_schema_id = module->value_schema_id;
    contract->activation_epoch_id = configuration.activation_epoch_id;
    contract->activation_epoch_fingerprint =
        configuration.activation_epoch_fingerprint;
    contract->module_contract_fingerprint =
        module->module_contract_fingerprint;
    contract->source_fingerprint =
        configuration.root_expectation.source_fingerprint;
    contract->recipe_fingerprint =
        configuration.root_expectation.recipe_fingerprint;
    if (laplace_perfcache_dependency_fingerprint(
            nullptr, 0U, &contract->dependency_fingerprint) !=
        LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_PERFCACHE_CONTRACT_MISMATCH;
    }
    contract->key_bytes = module->key_bytes;
    contract->value_bytes = module->value_bytes;
    contract->access_law = module->access_law;
    return LAPLACE_PERFCACHE_OK;
}

laplace_framework_status Begin(
    void* const opaque,
    const laplace_framework_context* const context,
    const std::uint32_t record_type,
    const std::uint64_t total_records,
    const std::uint64_t total_bytes) noexcept {
    auto* const state = static_cast<laplace_unicode_tier0_sink*>(opaque);
    if (state == nullptr || context == nullptr || state->begun ||
        state->sealed || state->builder != nullptr ||
        state->validator != nullptr ||
        record_type != LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE ||
        total_records <= LAPLACE_UNICODE_ROOT_POPULATION ||
        total_bytes == 0U || total_bytes > std::numeric_limits<std::size_t>::max()) {
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    state->aborted = false;
    if (laplace_unicode_root_stream_validator_create(
            &state->expectation, &state->validator) != LAPLACE_UNICODE_OK ||
        laplace_perfcache_file_builder_create(
            state->target_path.c_str(), &state->contract,
            LAPLACE_UNICODE_ROOT_POPULATION, total_bytes,
            &state->builder) != LAPLACE_PERFCACHE_OK) {
        ResetRuntime(state);
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    state->expected_root_frames = total_records;
    state->expected_root_bytes = total_bytes;
    state->begun = true;
    return LAPLACE_FRAMEWORK_OK;
}

bool AppendAtom(
    laplace_unicode_tier0_sink* const state,
    const laplace_unicode_root_frame_view& frame,
    const std::uint64_t expected_atom_index,
    std::vector<std::uint8_t>* const records,
    std::vector<std::uint8_t>* const metadata) {
    laplace_unicode_atom_record_view atom{};
    std::size_t consumed = 0U;
    if (state == nullptr || records == nullptr || metadata == nullptr ||
        laplace_unicode_atom_record_open(
            frame.value.payload, frame.value.payload_bytes,
            &atom, &consumed) != LAPLACE_UNICODE_OK ||
        consumed != frame.value.payload_bytes ||
        atom.value.codepoint_position != expected_atom_index ||
        expected_atom_index >= LAPLACE_UNICODE_ROOT_POPULATION ||
        state->metadata_bytes >
            std::numeric_limits<std::uint64_t>::max() - metadata->size() ||
        state->metadata_bytes + metadata->size() >
            std::numeric_limits<std::uint64_t>::max() - consumed) {
        return false;
    }
    const std::uint64_t metadata_offset =
        state->metadata_bytes + metadata->size();
    const std::size_t record_offset = records->size();
    records->resize(record_offset + Tier0RecordBytes, 0U);
    std::uint8_t* const record = records->data() + record_offset;
    std::uint8_t* const value =
        record + LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_BYTES;
    WriteU32(record, atom.value.codepoint_position);
#if defined(LAPLACE_TEST_UNICODE_TIER0_SINK_WRONG_METADATA_OFFSET)
    WriteU64(value, metadata_offset + 1U);
#else
    WriteU64(value, metadata_offset);
#endif
    WriteU32(value + 8U, frame.value.payload_bytes);
    WriteU32(value + 12U, atom.value.placement_rank);
    value[16U] = atom.value.position_class;
    value[17U] = atom.value.lup_v1_length;
    std::memcpy(value + 20U, atom.value.lup_v1_bytes, 4U);
    std::memcpy(value + 24U, atom.value.content_id.bytes, 16U);
    std::memcpy(
        value + 40U, atom.value.identity_preimage_fingerprint.bytes, 32U);
    for (std::size_t axis = 0U; axis < 4U; ++axis) {
        std::uint64_t bits = 0U;
        std::memcpy(
            &bits, &atom.value.coordinate.component[axis], sizeof(bits));
        WriteU64(value + 72U + axis * 8U, bits);
    }
    std::memcpy(value + 104U, atom.value.hilbert_key, 16U);
    std::memcpy(value + 120U, atom.value.physicality_id.bytes, 32U);
#if defined(LAPLACE_TEST_UNICODE_TIER0_SINK_WRONG_PHYSICALITY_ID)
    value[120U] ^= 0x01U;
#endif
#if defined(LAPLACE_TEST_UNICODE_TIER0_SINK_OUTER_FRAME_METADATA)
    metadata->insert(
        metadata->end(), frame.encoded_frame,
        frame.encoded_frame + frame.encoded_bytes);
#else
    metadata->insert(
        metadata->end(), frame.value.payload,
        frame.value.payload + frame.value.payload_bytes);
#endif
    return true;
}

laplace_framework_status StageFailed(
    laplace_unicode_tier0_sink* const state) noexcept {
    if (state != nullptr) {
        state->aborted = true;
    }
    return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
}

laplace_framework_status Stage(
    void* const opaque,
    const laplace_framework_canonical_batch* const batch) noexcept {
    auto* const state = static_cast<laplace_unicode_tier0_sink*>(opaque);
    if (state == nullptr || batch == nullptr || !state->begun ||
        state->sealed || state->aborted || batch->canonical_bytes == nullptr ||
        batch->record_type != LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE ||
        batch->flags != LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS ||
        batch->byte_count == 0U || batch->record_count == 0U ||
        batch->byte_count > std::numeric_limits<std::size_t>::max() ||
        batch->record_count >
            std::numeric_limits<std::size_t>::max() / Tier0RecordBytes ||
        state->staged_root_frames != batch->first_ordinal ||
        state->staged_root_frames >
            std::numeric_limits<std::uint64_t>::max() - batch->record_count ||
        state->staged_root_bytes >
            std::numeric_limits<std::uint64_t>::max() - batch->byte_count) {
        return StageFailed(state);
    }
    if (laplace_unicode_root_stream_validator_consume(
            state->validator, batch->canonical_bytes,
            static_cast<std::size_t>(batch->byte_count), batch->record_count,
            batch->first_ordinal) != LAPLACE_UNICODE_OK) {
        return StageFailed(state);
    }
    try {
        std::vector<std::uint8_t> records;
        std::vector<std::uint8_t> metadata;
        records.reserve(
            static_cast<std::size_t>(batch->record_count) * Tier0RecordBytes);
        metadata.reserve(static_cast<std::size_t>(batch->byte_count));
        std::size_t offset = 0U;
        std::uint64_t atom_count = 0U;
        for (std::uint64_t frame_index = 0U;
             frame_index < batch->record_count; ++frame_index) {
            laplace_unicode_root_frame_view frame{};
            std::size_t consumed = 0U;
            if (offset >= batch->byte_count ||
                laplace_unicode_root_frame_open(
                    batch->canonical_bytes + offset,
                    static_cast<std::size_t>(batch->byte_count) - offset,
                    &frame, &consumed) != LAPLACE_UNICODE_OK ||
                consumed == 0U ||
                consumed > static_cast<std::size_t>(batch->byte_count) - offset) {
                return StageFailed(state);
            }
            if (frame.value.kind == LAPLACE_UNICODE_ROOT_FRAME_ATOM) {
                if (!AppendAtom(
                        state, frame, state->atom_count + atom_count,
                        &records, &metadata)) {
                    return StageFailed(state);
                }
                ++atom_count;
            }
            offset += consumed;
        }
        if (offset != batch->byte_count) {
            return StageFailed(state);
        }
        if (atom_count != 0U) {
            if (laplace_perfcache_file_builder_append(
                    state->builder, state->atom_count, records.data(), atom_count,
                    metadata.data(), metadata.size()) != LAPLACE_PERFCACHE_OK) {
                return StageFailed(state);
            }
            state->atom_count += atom_count;
            state->metadata_bytes += metadata.size();
        }
        state->staged_root_frames += batch->record_count;
        state->staged_root_bytes += batch->byte_count;
        return LAPLACE_FRAMEWORK_OK;
    } catch (const std::bad_alloc&) {
        return StageFailed(state);
    } catch (...) {
        return StageFailed(state);
    }
}

laplace_framework_status Seal(
    void* const opaque,
    const laplace_digest256* const stream_fingerprint,
    laplace_digest256* const artifact_fingerprint) noexcept {
    auto* const state = static_cast<laplace_unicode_tier0_sink*>(opaque);
    if (state == nullptr || stream_fingerprint == nullptr ||
        artifact_fingerprint == nullptr || !state->begun || state->sealed ||
        state->aborted ||
        state->staged_root_frames != state->expected_root_frames ||
        state->staged_root_bytes != state->expected_root_bytes ||
        state->atom_count != LAPLACE_UNICODE_ROOT_POPULATION) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    laplace_unicode_root_stream_summary root_summary{};
    if (laplace_unicode_root_stream_validator_finish(
            state->validator, &root_summary) != LAPLACE_UNICODE_OK ||
        root_summary.section_counts[0] != LAPLACE_UNICODE_ROOT_POPULATION ||
        root_summary.manifest.atom_count != LAPLACE_UNICODE_ROOT_POPULATION ||
        root_summary.total_frame_count != state->staged_root_frames ||
        root_summary.total_encoded_bytes != state->staged_root_bytes ||
        !DigestEqual(
            root_summary.manifest.source_fingerprint,
            state->contract.source_fingerprint) ||
        !DigestEqual(
            root_summary.manifest.recipe_fingerprint,
            state->contract.recipe_fingerprint)) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    std::uint64_t invalid_record_index = UINT64_MAX;
    std::size_t artifact_bytes = 0U;
    laplace_digest256 artifact_digest{};
    if (laplace_perfcache_file_builder_seal(
            state->builder, state->module.validate_record,
            state->module.state, state->module.validate_view,
            state->module.state, &invalid_record_index, &artifact_bytes,
            &artifact_digest) != LAPLACE_PERFCACHE_OK) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    laplace_perfcache_generation_artifact artifact{};
    artifact.path = state->target_path.c_str();
    artifact.contract = state->contract;
    artifact.expected_artifact_digest = artifact_digest;
    artifact.flags = LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED;
    laplace_perfcache_generation_request request{};
    request.artifacts = &artifact;
    request.artifact_count = 1U;
    request.activation_epoch_id = state->contract.activation_epoch_id;
    request.epoch_fingerprint =
        state->contract.activation_epoch_fingerprint;
    laplace_digest256 artifact_set_fingerprint{};
    if (laplace_perfcache_generation_artifact_set_fingerprint(
            &request, &artifact_set_fingerprint) !=
        LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    state->result.contract = state->contract;
    state->result.root_summary = root_summary;
    state->result.root_framework_stream_fingerprint = *stream_fingerprint;
    state->result.artifact_digest = artifact_digest;
    state->result.artifact_set_fingerprint = artifact_set_fingerprint;
    state->result.root_frame_count = state->staged_root_frames;
    state->result.root_encoded_bytes = state->staged_root_bytes;
    state->result.atom_count = state->atom_count;
    state->result.atom_metadata_bytes = state->metadata_bytes;
    state->result.artifact_bytes = artifact_bytes;
    state->result.sealed = 1U;
    *artifact_fingerprint = artifact_set_fingerprint;
    state->sealed = true;
    state->begun = false;
    laplace_perfcache_file_builder_destroy(&state->builder);
    laplace_unicode_root_stream_validator_destroy(state->validator);
    state->validator = nullptr;
    return LAPLACE_FRAMEWORK_OK;
}

void Abort(void* const opaque) noexcept {
    auto* const state = static_cast<laplace_unicode_tier0_sink*>(opaque);
    if (state == nullptr) {
        return;
    }
    ResetRuntime(state);
    state->result = laplace_unicode_tier0_sink_result{};
    state->sealed = false;
    state->aborted = true;
}

}  // namespace

extern "C" laplace_perfcache_status laplace_unicode_tier0_sink_create(
    const laplace_unicode_tier0_sink_configuration* const configuration,
    laplace_unicode_tier0_sink** const output_state,
    laplace_framework_sink_v1* const sink) {
    if (configuration == nullptr || output_state == nullptr || sink == nullptr ||
        configuration->target_path == nullptr ||
        configuration->target_path[0] == '\0' ||
        configuration->abi_major != LAPLACE_UNICODE_TIER0_SINK_ABI_MAJOR ||
        configuration->abi_minor > LAPLACE_UNICODE_TIER0_SINK_ABI_MINOR ||
        configuration->flags != 0U || configuration->reserved != 0U) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    *output_state = nullptr;
    *sink = laplace_framework_sink_v1{};
    try {
        auto state = std::make_unique<laplace_unicode_tier0_sink>();
        state->target_path = configuration->target_path;
        state->expectation = configuration->root_expectation;
        const laplace_perfcache_status contract_status = BuildContract(
            *configuration, &state->module, &state->contract);
        if (contract_status != LAPLACE_PERFCACHE_OK) {
            return contract_status;
        }
        laplace_unicode_root_stream_validator* expectation_validator = nullptr;
        if (laplace_unicode_root_stream_validator_create(
                &state->expectation, &expectation_validator) !=
            LAPLACE_UNICODE_OK) {
            return LAPLACE_PERFCACHE_CONTRACT_MISMATCH;
        }
        laplace_unicode_root_stream_validator_destroy(expectation_validator);
        sink->state = state.get();
        sink->begin = Begin;
        sink->stage = Stage;
        sink->seal = Seal;
        sink->abort = Abort;
        sink->abi_major = LAPLACE_FRAMEWORK_SINK_ABI_MAJOR;
        sink->abi_minor = LAPLACE_FRAMEWORK_SINK_ABI_MINOR;
        *output_state = state.release();
        return LAPLACE_PERFCACHE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_PERFCACHE_FILE_IO_FAILED;
    } catch (...) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
}

extern "C" laplace_perfcache_status laplace_unicode_tier0_sink_result_get(
    const laplace_unicode_tier0_sink* const state,
    laplace_unicode_tier0_sink_result* const result) {
    if (state == nullptr || result == nullptr || !state->sealed ||
        state->result.sealed == 0U) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    *result = state->result;
    return LAPLACE_PERFCACHE_OK;
}

extern "C" void laplace_unicode_tier0_sink_destroy(
    laplace_unicode_tier0_sink** const state) {
    if (state == nullptr || *state == nullptr) {
        return;
    }
    ResetRuntime(*state);
    delete *state;
    *state = nullptr;
}
