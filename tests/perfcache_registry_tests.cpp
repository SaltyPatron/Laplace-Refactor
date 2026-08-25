#include "laplace/perfcache_registry.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

namespace {

void Fill(std::uint8_t* output, std::size_t size, std::uint8_t seed) {
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_id128 Id(std::uint8_t seed) {
    laplace_id128 value{};
    Fill(value.bytes, sizeof(value.bytes), seed);
    return value;
}

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    Fill(value.bytes, sizeof(value.bytes), seed);
    return value;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_perfcache_status ValidateRecord(
    void*, std::uint64_t, const std::uint8_t* record,
    std::uint32_t record_stride) {
    return record != nullptr && record_stride == 12u
        ? LAPLACE_PERFCACHE_OK
        : LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
}

laplace_perfcache_status RejectLookup(
    void*, const laplace_perfcache_view*, const std::uint8_t*, std::size_t,
    std::uint64_t*, std::uint8_t*) {
    return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
}

laplace_perfcache_module_v1 Module(std::uint8_t seed) {
    laplace_perfcache_module_v1 module{};
    module.module_id = Id(seed);
    module.key_schema_id = Id(static_cast<std::uint8_t>(seed + 0x10u));
    module.value_schema_id = Id(static_cast<std::uint8_t>(seed + 0x20u));
    module.module_contract_fingerprint =
        Digest(static_cast<std::uint8_t>(seed + 0x30u));
    module.validate_record = ValidateRecord;
    module.access_law = LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED;
    module.key_bytes = 4u;
    module.value_bytes = 8u;
    module.flags = LAPLACE_PERFCACHE_MODULE_REQUIRED;
    module.abi_major = LAPLACE_PERFCACHE_MODULE_ABI_MAJOR;
    module.abi_minor = LAPLACE_PERFCACHE_MODULE_ABI_MINOR;
    return module;
}

laplace_framework_context Context() {
    laplace_framework_context context{};
    context.major = LAPLACE_FRAMEWORK_MAJOR;
    context.minor = LAPLACE_FRAMEWORK_MINOR;
    context.flags = 0u;
    context.epoch_mask =
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_IDENTITY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_DATABASE) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_NUMERIC) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PACKAGE);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_IDENTITY] = Digest(0x10u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY] = Digest(0x30u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE] = Digest(0x40u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE] = Digest(0x60u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC] = Digest(0x50u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_PACKAGE] = Digest(0x70u);
    context.authority_fingerprint = Digest(0x90u);
    context.resource_grant = laplace_execution_grant{
        UINT64_C(1) << 20u, 4u, 1u};
    return context;
}

void StoreU32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4u; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
}

void StoreU64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8u; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
}

std::vector<std::uint8_t> Artifact(
    const laplace_perfcache_contract& contract, std::uint64_t value_base) {
    std::vector<std::uint8_t> records(36u, 0u);
    for (std::uint32_t index = 0; index < 3u; ++index) {
        StoreU32(records.data() + static_cast<std::size_t>(index) * 12u, index);
        StoreU64(records.data() + static_cast<std::size_t>(index) * 12u + 4u,
                 value_base + index);
    }
    const laplace_perfcache_spec spec{
        contract, records.data(), 3u, nullptr, 0u};
    std::size_t size = 0;
    EXPECT_EQ(laplace_perfcache_measure(&spec, &size), LAPLACE_PERFCACHE_OK);
    std::vector<std::uint8_t> artifact(size, 0u);
    std::size_t written = 0;
    EXPECT_EQ(laplace_perfcache_write(
                  &spec, artifact.data(), artifact.size(), &written),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(written, artifact.size());
    return artifact;
}

laplace_digest256 ArtifactDigest(
    const std::vector<std::uint8_t>& artifact,
    const laplace_perfcache_contract& contract) {
    laplace_perfcache_view view{};
    EXPECT_EQ(laplace_perfcache_validate(
                  artifact.data(), artifact.size(), &contract, &view),
              LAPLACE_PERFCACHE_OK);
    return view.artifact_digest;
}

std::string NewDirectory() {
    std::array<char, 64> path{};
    const char pattern[] = "/tmp/laplace-perfcache-registry.XXXXXX";
    std::memcpy(path.data(), pattern, sizeof(pattern));
    char* created = mkdtemp(path.data());
    EXPECT_NE(created, nullptr);
    return created == nullptr ? std::string{} : std::string(created);
}

void Publish(const std::string& path,
             const std::vector<std::uint8_t>& artifact,
             const laplace_perfcache_contract& contract) {
    std::uint64_t invalid = std::numeric_limits<std::uint64_t>::max();
    ASSERT_EQ(laplace_perfcache_publish_file(
                  path.c_str(), artifact.data(), artifact.size(), &contract,
                  ValidateRecord, nullptr, &invalid),
              LAPLACE_PERFCACHE_OK);
}

struct SinkState {
    laplace_digest256 artifact{};
};

laplace_framework_status BeginSink(
    void*, const laplace_framework_context*, std::uint32_t,
    std::uint64_t, std::uint64_t) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status StageSink(
    void*, const laplace_framework_canonical_batch*) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SealSink(
    void* opaque, const laplace_digest256*, laplace_digest256* artifact) {
    *artifact = static_cast<SinkState*>(opaque)->artifact;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortSink(void*) {}

laplace_framework_stream_receipt StageReceipt(
    const laplace_framework_context& context,
    const laplace_digest256& source,
    const laplace_digest256& recipe,
    const laplace_digest256& artifact) {
    const std::array<std::uint8_t, 4> bytes{{1u, 2u, 3u, 4u}};
    const laplace_framework_canonical_batch batch{
        bytes.data(), bytes.size(), 1u, 0u, LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
    laplace_framework_canonical_stream stream{};
    stream.batches = &batch;
    stream.source_fingerprint = source;
    stream.recipe_fingerprint = recipe;
    stream.batch_count = 1u;
    stream.flags = LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS;
    SinkState state{artifact};
    laplace_framework_sink_v1 sink{
        &state, BeginSink, StageSink, SealSink, AbortSink,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR, 0u, 0u};
    laplace_framework_stream_receipt receipt{};
    EXPECT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream, &sink, 1u, &receipt),
              LAPLACE_FRAMEWORK_OK);
    return receipt;
}

laplace_perfcache_contract Contract(
    const laplace_perfcache_module_v1& module,
    const laplace_id128& activation,
    const laplace_digest256& epoch,
    const laplace_digest256& source,
    const laplace_digest256& recipe) {
    laplace_perfcache_contract contract{};
    contract.module_id = module.module_id;
    contract.key_schema_id = module.key_schema_id;
    contract.value_schema_id = module.value_schema_id;
    contract.activation_epoch_id = activation;
    contract.activation_epoch_fingerprint = epoch;
    contract.module_contract_fingerprint = module.module_contract_fingerprint;
    contract.source_fingerprint = source;
    contract.recipe_fingerprint = recipe;
    EXPECT_EQ(laplace_perfcache_dependency_fingerprint(
                  nullptr, 0u, &contract.dependency_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    contract.key_bytes = 4u;
    contract.value_bytes = 8u;
    contract.access_law = module.access_law;
    return contract;
}

struct PreparedGeneration {
    laplace_perfcache_prepared_generation* value{};
    laplace_perfcache_generation_receipt receipt{};
    laplace_framework_context context{};
    laplace_framework_stream_receipt staged{};
};

PreparedGeneration Prepare(
    laplace_perfcache_registry* registry,
    const laplace_perfcache_artifact_provider_v1& provider,
    const laplace_perfcache_module_v1* modules,
    std::size_t module_count,
    const laplace_perfcache_module_v1& module,
    const std::string& path,
    const laplace_id128& activation,
    const laplace_digest256& epoch,
    std::uint8_t source_seed,
    std::uint64_t value_base,
    laplace_perfcache_registry_status expected_status =
        LAPLACE_PERFCACHE_REGISTRY_OK,
    std::uint32_t context_flags = 0u,
    bool corrupt_receipt = false,
    bool unrelated_artifact_set = false,
    const laplace_digest256* current_perfcache_epoch = nullptr) {
    laplace_framework_context context = Context();
    context.flags = context_flags;
    if (current_perfcache_epoch != nullptr) {
        context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE] =
            *current_perfcache_epoch;
    }
    const laplace_digest256 source = Digest(source_seed);
    const laplace_digest256 recipe =
        Digest(static_cast<std::uint8_t>(source_seed + 0x20u));
    const laplace_perfcache_contract contract =
        Contract(module, activation, epoch, source, recipe);
    const auto bytes = Artifact(contract, value_base);
    const laplace_digest256 artifact_digest = ArtifactDigest(bytes, contract);
    Publish(path, bytes, contract);
    laplace_perfcache_generation_artifact requested_artifact{};
    requested_artifact.path = path.c_str();
    requested_artifact.contract = contract;
    requested_artifact.expected_artifact_digest = artifact_digest;
    requested_artifact.flags = LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED;
    laplace_perfcache_generation_request request{};
    request.artifacts = &requested_artifact;
    request.artifact_count = 1u;
    request.activation_epoch_id = activation;
    request.epoch_fingerprint = epoch;
    EXPECT_EQ(laplace_perfcache_required_module_set_fingerprint(
                  modules, module_count,
                  &request.required_module_set_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(laplace_perfcache_generation_artifact_set_fingerprint(
                  &request, &request.sink_artifact_set_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const laplace_digest256 staged_artifact = unrelated_artifact_set
        ? Digest(0xf0u)
        : request.sink_artifact_set_fingerprint;
    laplace_framework_stream_receipt staged =
        StageReceipt(context, source, recipe, staged_artifact);
    if (corrupt_receipt) {
        staged.receipt_id.bytes[0] ^= 1u;
    }
    request.staged_receipt_id = staged.receipt_id;
    request.stream_fingerprint = staged.stream_fingerprint;
    request.staged_sink_artifacts_fingerprint =
        staged.sink_artifacts_fingerprint;
    PreparedGeneration result{};
    result.context = context;
    result.staged = staged;
    const auto status = laplace_perfcache_registry_prepare(
        registry, &context, &staged, &provider, &request,
        &result.value, &result.receipt);
    EXPECT_EQ(status, expected_status);
    return result;
}

struct AdmittedGeneration {
    laplace_perfcache_activation* activation{};
    laplace_framework_activation_provider_v1 provider{};
    laplace_framework_activation_request request{};
    laplace_framework_activation_receipt framework_receipt{};
};

AdmittedGeneration Admit(
    laplace_perfcache_registry* registry,
    PreparedGeneration* prepared,
    const laplace_perfcache_epoch* expected_epoch) {
    AdmittedGeneration result{};
    EXPECT_EQ(laplace_perfcache_activation_create(
                  registry, &prepared->value,
                  expected_epoch == nullptr ? 0u : 1u, expected_epoch,
                  &result.activation, &result.provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    result.request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_PERFCACHE;
    result.request.expected_epoch =
        prepared->context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE];
    result.request.next_epoch = prepared->receipt.epoch_fingerprint;
    EXPECT_EQ(laplace_framework_admit_staged_stream(
                  &prepared->context, &prepared->staged, &result.request,
                  &result.provider, &result.framework_receipt),
              LAPLACE_FRAMEWORK_OK);
    return result;
}

void Commit(
    PreparedGeneration* prepared,
    AdmittedGeneration* admitted,
    laplace_perfcache_generation_receipt* receipt) {
    ASSERT_EQ(laplace_framework_commit_admitted_stream(
                  &prepared->context, &admitted->request, &admitted->provider,
                  &admitted->framework_receipt),
              LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_perfcache_activation_receipt_get(
                  admitted->activation, receipt),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(receipt->disposition, LAPLACE_PERFCACHE_GENERATION_ACTIVATED);
    laplace_perfcache_activation_destroy(admitted->activation);
    admitted->activation = nullptr;
}

void Activate(
    laplace_perfcache_registry* registry,
    PreparedGeneration* prepared,
    const laplace_perfcache_epoch* expected_epoch,
    laplace_perfcache_generation_receipt* receipt) {
    auto admitted = Admit(registry, prepared, expected_epoch);
    EXPECT_EQ(admitted.framework_receipt.effect_disposition,
              LAPLACE_FRAMEWORK_EFFECT_ACTIVATION_ADMITTED);
    Commit(prepared, &admitted, receipt);
}

void Cleanup(const std::string& directory,
             const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        EXPECT_EQ(unlink(path.c_str()), 0);
    }
    EXPECT_EQ(rmdir(directory.c_str()), 0);
}

struct NullHandleProviderState {
    laplace_perfcache_artifact_provider_v1 inner{};
    laplace_perfcache_artifact_handle inner_handle{};
    std::uint32_t open_count{};
    std::uint32_t prefault_count{};
    std::uint32_t close_count{};
    bool is_open{};
};

laplace_perfcache_status NullHandleOpen(
    void* opaque, const char* path,
    const laplace_perfcache_contract* expected_contract,
    const laplace_digest256* expected_artifact_digest,
    laplace_perfcache_record_validator validator, void* validator_context,
    std::uint64_t* invalid_record_index,
    laplace_perfcache_artifact_handle* handle) {
    auto* state = static_cast<NullHandleProviderState*>(opaque);
    ++state->open_count;
    const auto status = state->inner.open(
        state->inner.state, path, expected_contract, expected_artifact_digest,
        validator, validator_context, invalid_record_index,
        &state->inner_handle);
    if (status != LAPLACE_PERFCACHE_OK) {
        return status;
    }
    state->is_open = true;
    handle->view = state->inner_handle.view;
    handle->loaded_identity = state->inner_handle.loaded_identity;
    handle->provider_handle = nullptr;
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status NullHandlePrefault(
    void* opaque, laplace_perfcache_artifact_handle*,
    const laplace_execution_grant* grant, std::uint64_t* touched_bytes,
    std::uint64_t* touched_pages) {
    auto* state = static_cast<NullHandleProviderState*>(opaque);
    ++state->prefault_count;
    return state->inner.prefault(
        state->inner.state, &state->inner_handle, grant,
        touched_bytes, touched_pages);
}

void NullHandleClose(
    void* opaque, laplace_perfcache_artifact_handle*) {
    auto* state = static_cast<NullHandleProviderState*>(opaque);
    ++state->close_count;
    if (state->is_open) {
        state->inner.close(state->inner.state, &state->inner_handle);
        state->is_open = false;
    }
}

laplace_perfcache_artifact_provider_v1 NullHandleProvider(
    NullHandleProviderState* state) {
    EXPECT_EQ(laplace_perfcache_file_provider(&state->inner),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    provider.state = state;
    provider.open = NullHandleOpen;
    provider.prefault = NullHandlePrefault;
    provider.close = NullHandleClose;
    provider.provider_fingerprint = Digest(0xe0u);
    provider.abi_major = LAPLACE_PERFCACHE_ARTIFACT_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_PERFCACHE_ARTIFACT_PROVIDER_ABI_MINOR;
    return provider;
}

TEST(PerfcacheRegistry, NullProviderHandleStillPrefaultsAndClosesExactlyOnce) {
    const auto module = Module(0x10u);
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    NullHandleProviderState provider_state{};
    const auto provider = NullHandleProvider(&provider_state);
    const std::string directory = NewDirectory();
    const std::string path = directory + "/null-handle.bin";
    auto prepared = Prepare(
        registry, provider, &module, 1u, module, path, Id(0x70u),
        Digest(0x90u), 0x30u, 100u);
    ASSERT_NE(prepared.value, nullptr);
    EXPECT_EQ(provider_state.open_count, 1u);
    EXPECT_EQ(provider_state.prefault_count, 1u);
    EXPECT_EQ(provider_state.close_count, 0u);
    laplace_perfcache_registry_discard_prepared(&prepared.value);
    EXPECT_EQ(provider_state.close_count, 1u);
    EXPECT_FALSE(provider_state.is_open);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {path});
}

TEST(PerfcacheRegistry, PreparedGenerationHasOneActivationOwner) {
    const auto module = Module(0x10u);
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string path = directory + "/single-owner.bin";
    auto prepared = Prepare(
        registry, provider, &module, 1u, module, path, Id(0x70u),
        Digest(0x90u), 0x30u, 100u);

    laplace_perfcache_activation* activation = nullptr;
    laplace_framework_activation_provider_v1 activation_provider{};
    ASSERT_EQ(laplace_perfcache_activation_create(
                  registry, &prepared.value, 0u, nullptr, &activation,
                  &activation_provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_EQ(prepared.value, nullptr);

    laplace_perfcache_activation* duplicate = nullptr;
    laplace_framework_activation_provider_v1 duplicate_provider{};
    EXPECT_EQ(laplace_perfcache_activation_create(
                  registry, &prepared.value, 0u, nullptr, &duplicate,
                  &duplicate_provider),
              LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT);
    EXPECT_EQ(duplicate, nullptr);
    laplace_perfcache_registry_discard_prepared(&prepared.value);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_BUSY);

    laplace_perfcache_activation_destroy(activation);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {path});
}

TEST(PerfcacheRegistry, PrepareReserveCommitIsInactiveUntilCommit) {
    auto module = Module(0x10u);
    module.lookup_batch = RejectLookup;
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string path = directory + "/generation.bin";
    const laplace_digest256 epoch = Digest(0x90u);
    auto prepared = Prepare(
        registry, provider, &module, 1u, module, path, Id(0x70u), epoch,
        0x30u, 100u);
    ASSERT_NE(prepared.value, nullptr);
    EXPECT_EQ(prepared.receipt.disposition,
              LAPLACE_PERFCACHE_GENERATION_PREPARED);
    auto admitted = Admit(registry, &prepared, nullptr);
    EXPECT_EQ(laplace_perfcache_activation_commit_ready(admitted.activation),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(laplace_framework_admitted_stream_validate(
                  &prepared.context, &admitted.request, &admitted.provider,
                  &admitted.framework_receipt),
              LAPLACE_FRAMEWORK_OK);
    laplace_perfcache_registry_snapshot snapshot{};
    ASSERT_EQ(laplace_perfcache_registry_snapshot_get(registry, &snapshot),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(snapshot.has_active_generation, 0u);
    Commit(&prepared, &admitted, &prepared.receipt);
    EXPECT_EQ(prepared.receipt.disposition,
              LAPLACE_PERFCACHE_GENERATION_ACTIVATED);

    laplace_perfcache_pin pin{};
    const laplace_perfcache_epoch expected_epoch{Id(0x70u), epoch};
    ASSERT_EQ(laplace_perfcache_registry_pin(
                  registry, 1u, &expected_epoch, &pin),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(std::memcmp(pin.epoch.activation_epoch_id.bytes,
                          expected_epoch.activation_epoch_id.bytes,
                          sizeof(pin.epoch.activation_epoch_id.bytes)), 0);
    EXPECT_TRUE(Same(pin.epoch.epoch_fingerprint,
                     expected_epoch.epoch_fingerprint));
    const std::array<std::uint8_t, 8> keys{{0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u}};
    std::array<std::uint64_t, 2> indexes{};
    std::array<std::uint8_t, 2> found{};
    EXPECT_EQ(laplace_perfcache_pin_lookup_batch(
                  &pin, &module.module_id, keys.data(), 2u,
                  indexes.data(), found.data()),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(found, (std::array<std::uint8_t, 2>{{1u, 1u}}));
    EXPECT_EQ(indexes, (std::array<std::uint64_t, 2>{{0u, 2u}}));
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_BUSY);
    EXPECT_EQ(laplace_perfcache_pin_release(&pin),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {path});
}

TEST(PerfcacheRegistry, PinnedReadersDrainOnlyAfterCollection) {
    const auto module = Module(0x10u);
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string first_path = directory + "/first.bin";
    const std::string second_path = directory + "/second.bin";
    const laplace_digest256 first_epoch = Digest(0x80u);
    const laplace_digest256 second_epoch = Digest(0x90u);
    const laplace_perfcache_epoch first_expected{Id(0x50u), first_epoch};
    auto first = Prepare(
        registry, provider, &module, 1u, module, first_path, Id(0x50u),
        first_epoch, 0x20u, 100u);
    laplace_perfcache_generation_receipt receipt{};
    Activate(registry, &first, nullptr, &receipt);
    auto second = Prepare(
        registry, provider, &module, 1u, module, second_path, Id(0x60u),
        second_epoch, 0x30u, 200u, LAPLACE_PERFCACHE_REGISTRY_OK,
        0u, false, false, &first_epoch);
    laplace_perfcache_pin old_pin{};
    ASSERT_EQ(laplace_perfcache_registry_pin(
                  registry, 1u, &first_expected, &old_pin),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Activate(registry, &second, &first_expected, &receipt);
    laplace_perfcache_registry_snapshot snapshot{};
    ASSERT_EQ(laplace_perfcache_registry_snapshot_get(registry, &snapshot),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(snapshot.retired_generation_count, 1u);
    EXPECT_EQ(snapshot.retired_reader_count, 1u);
    const laplace_perfcache_view* old_view = nullptr;
    ASSERT_EQ(laplace_perfcache_pin_view(
                  &old_pin, &module.module_id, &old_view),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(old_view->records[4], 100u);
    EXPECT_EQ(laplace_perfcache_pin_release(&old_pin),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    std::uint64_t generations = 0;
    std::uint64_t artifacts = 0;
    std::uint64_t bytes = 0;
    ASSERT_EQ(laplace_perfcache_registry_collect(
                  registry, &generations, &artifacts, &bytes),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(generations, 1u);
    EXPECT_EQ(artifacts, 1u);
    EXPECT_GT(bytes, 0u);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {first_path, second_path});
}

TEST(PerfcacheRegistry, ConcurrentActivationPreservesPinnedReaderGeneration) {
    const auto module = Module(0x10u);
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string first_path = directory + "/concurrent-first.bin";
    const std::string second_path = directory + "/concurrent-second.bin";
    const laplace_digest256 first_epoch = Digest(0x80u);
    const laplace_digest256 second_epoch = Digest(0x90u);
    const laplace_perfcache_epoch first_expected{Id(0x50u), first_epoch};
    auto first = Prepare(
        registry, provider, &module, 1u, module, first_path, Id(0x50u),
        first_epoch, 0x20u, 100u);
    laplace_perfcache_generation_receipt receipt{};
    Activate(registry, &first, nullptr, &receipt);
    auto second = Prepare(
        registry, provider, &module, 1u, module, second_path, Id(0x60u),
        second_epoch, 0x30u, 200u, LAPLACE_PERFCACHE_REGISTRY_OK,
        0u, false, false, &first_epoch);

    std::mutex mutex;
    std::condition_variable condition;
    bool reader_pinned = false;
    bool release_reader = false;
    laplace_perfcache_registry_status pin_status =
        LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    laplace_perfcache_registry_status view_status =
        LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    laplace_perfcache_registry_status release_status =
        LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    std::uint8_t observed_value = 0u;
    std::thread reader([&] {
        laplace_perfcache_pin pin{};
        pin_status = laplace_perfcache_registry_pin(
            registry, 1u, &first_expected, &pin);
        if (pin_status == LAPLACE_PERFCACHE_REGISTRY_OK) {
            const laplace_perfcache_view* view = nullptr;
            view_status = laplace_perfcache_pin_view(
                &pin, &module.module_id, &view);
            if (view_status == LAPLACE_PERFCACHE_REGISTRY_OK) {
                observed_value = view->records[4];
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            reader_pinned = true;
        }
        condition.notify_one();
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&] { return release_reader; });
        }
        if (pin_status == LAPLACE_PERFCACHE_REGISTRY_OK) {
            release_status = laplace_perfcache_pin_release(&pin);
        }
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return reader_pinned; });
    }

    laplace_perfcache_activation* activation = nullptr;
    laplace_framework_activation_provider_v1 activation_provider{};
    const auto create_status = laplace_perfcache_activation_create(
        registry, &second.value, 1u, &first_expected, &activation,
        &activation_provider);
    laplace_framework_activation_request request{};
    request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_PERFCACHE;
    request.expected_epoch = first_epoch;
    request.next_epoch = second_epoch;
    laplace_framework_activation_receipt framework_receipt{};
    const auto admit_status = create_status == LAPLACE_PERFCACHE_REGISTRY_OK
        ? laplace_framework_admit_staged_stream(
              &second.context, &second.staged, &request,
              &activation_provider, &framework_receipt)
        : LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
    const auto commit_status = admit_status == LAPLACE_FRAMEWORK_OK
        ? laplace_framework_commit_admitted_stream(
              &second.context, &request, &activation_provider,
              &framework_receipt)
        : LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
    laplace_perfcache_registry_status receipt_status =
        LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    if (commit_status == LAPLACE_FRAMEWORK_OK) {
        receipt_status = laplace_perfcache_activation_receipt_get(
            activation, &receipt);
    }
    laplace_perfcache_activation_destroy(activation);
    std::uint64_t generations = 0u;
    std::uint64_t artifacts = 0u;
    std::uint64_t bytes = 0u;
    const auto pinned_collect_status = laplace_perfcache_registry_collect(
        registry, &generations, &artifacts, &bytes);
    const auto pinned_collected_generations = generations;
    const auto pinned_collected_artifacts = artifacts;
    const auto pinned_released_bytes = bytes;

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_reader = true;
    }
    condition.notify_one();
    reader.join();
    EXPECT_EQ(create_status, LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(admit_status, LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(commit_status, LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(receipt_status, LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(pinned_collect_status, LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(pinned_collected_generations, 0u);
    EXPECT_EQ(pinned_collected_artifacts, 0u);
    EXPECT_EQ(pinned_released_bytes, 0u);
    EXPECT_EQ(pin_status, LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(view_status, LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(release_status, LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(observed_value, 100u);

    ASSERT_EQ(laplace_perfcache_registry_collect(
                  registry, &generations, &artifacts, &bytes),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(generations, 1u);
    EXPECT_EQ(artifacts, 1u);
    EXPECT_GT(bytes, 0u);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {first_path, second_path});
}

TEST(PerfcacheRegistry, ReservationIsSingleWriterCasAndAbortKeepsOldEpoch) {
    const auto module = Module(0x10u);
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string first_path = directory + "/first.bin";
    const std::string second_path = directory + "/second.bin";
    const std::string third_path = directory + "/third.bin";
    const laplace_digest256 first_epoch = Digest(0x80u);
    const laplace_perfcache_epoch first_expected{Id(0x50u), first_epoch};
    auto first = Prepare(
        registry, provider, &module, 1u, module, first_path, Id(0x50u),
        first_epoch, 0x20u, 100u);
    laplace_perfcache_generation_receipt receipt{};
    Activate(registry, &first, nullptr, &receipt);
    auto second = Prepare(
        registry, provider, &module, 1u, module, second_path, Id(0x60u),
        Digest(0x90u), 0x30u, 200u, LAPLACE_PERFCACHE_REGISTRY_OK,
        0u, false, false, &first_epoch);
    auto admitted = Admit(registry, &second, &first_expected);
    auto third = Prepare(
        registry, provider, &module, 1u, module, third_path, Id(0x70u),
        Digest(0xb0u), 0x40u, 300u, LAPLACE_PERFCACHE_REGISTRY_OK,
        0u, false, false, &first_epoch);
    laplace_perfcache_activation* rejected_activation = nullptr;
    laplace_framework_activation_provider_v1 rejected_provider{};
    ASSERT_EQ(laplace_perfcache_activation_create(
                  registry, &third.value, 1u, &first_expected,
                  &rejected_activation, &rejected_provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_framework_activation_request rejected_request{};
    rejected_request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_PERFCACHE;
    rejected_request.expected_epoch = first_epoch;
    rejected_request.next_epoch = Digest(0xb0u);
    laplace_framework_activation_receipt rejected_receipt{};
    EXPECT_EQ(laplace_framework_admit_staged_stream(
                  &third.context, &third.staged, &rejected_request,
                  &rejected_provider, &rejected_receipt),
              LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED);
    EXPECT_EQ(laplace_framework_abort_admitted_stream(
                  &second.context, &admitted.request, &admitted.provider,
                  &admitted.framework_receipt),
              LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_perfcache_activation_receipt_get(
                  admitted.activation, &receipt),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(receipt.disposition, LAPLACE_PERFCACHE_GENERATION_ABORTED);
    laplace_perfcache_activation_destroy(admitted.activation);
    laplace_perfcache_activation_destroy(rejected_activation);
    laplace_perfcache_registry_snapshot snapshot{};
    ASSERT_EQ(laplace_perfcache_registry_snapshot_get(registry, &snapshot),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_TRUE(Same(snapshot.active_epoch_fingerprint, first_epoch));
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {first_path, second_path, third_path});
}

TEST(PerfcacheRegistry, RequiredModuleSetRejectsPartialGeneration) {
    std::array<laplace_perfcache_module_v1, 2> modules{{
        Module(0x10u), Module(0x20u)}};
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(
                  modules.data(), modules.size(), &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string path = directory + "/partial.bin";
    auto partial = Prepare(
        registry, provider, modules.data(), modules.size(), modules[0], path,
        Id(0x70u), Digest(0x90u), 0x30u, 100u,
        LAPLACE_PERFCACHE_REGISTRY_MODULE_SET_MISMATCH);
    EXPECT_EQ(partial.value, nullptr);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {path});
}

TEST(PerfcacheRegistry, DependencyGraphAcceptsAcyclicAndRejectsCycles) {
    std::array<laplace_perfcache_module_v1, 2> modules{{
        Module(0x10u), Module(0x20u)}};
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(
                  modules.data(), modules.size(), &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::array<std::string, 2> paths{{
        directory + "/root.bin", directory + "/dependent.bin"}};
    const laplace_id128 activation = Id(0x70u);
    const laplace_digest256 epoch = Digest(0x90u);
    const laplace_digest256 source = Digest(0x30u);
    const laplace_digest256 recipe = Digest(0x50u);
    std::array<laplace_perfcache_contract, 2> contracts{{
        Contract(modules[0], activation, epoch, source, recipe),
        Contract(modules[1], activation, epoch, source, recipe)}};
    const auto root_bytes = Artifact(contracts[0], 100u);
    const laplace_digest256 root_digest =
        ArtifactDigest(root_bytes, contracts[0]);
    const laplace_perfcache_generation_dependency dependency{
        modules[0].module_id, root_digest};
    ASSERT_EQ(laplace_perfcache_dependency_fingerprint(
                  &dependency, 1u, &contracts[1].dependency_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const auto dependent_bytes = Artifact(contracts[1], 200u);
    const laplace_digest256 dependent_digest =
        ArtifactDigest(dependent_bytes, contracts[1]);
    Publish(paths[0], root_bytes, contracts[0]);
    Publish(paths[1], dependent_bytes, contracts[1]);
    std::array<laplace_perfcache_generation_artifact, 2> artifacts{};
    artifacts[0].path = paths[0].c_str();
    artifacts[0].contract = contracts[0];
    artifacts[0].expected_artifact_digest = root_digest;
    artifacts[0].flags = LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED;
    artifacts[1].path = paths[1].c_str();
    artifacts[1].contract = contracts[1];
    artifacts[1].expected_artifact_digest = dependent_digest;
    artifacts[1].dependencies = &dependency;
    artifacts[1].dependency_count = 1u;
    artifacts[1].flags = LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED;
    laplace_perfcache_generation_request request{};
    request.artifacts = artifacts.data();
    request.artifact_count = artifacts.size();
    request.activation_epoch_id = activation;
    request.epoch_fingerprint = epoch;
    ASSERT_EQ(laplace_perfcache_required_module_set_fingerprint(
                  modules.data(), modules.size(),
                  &request.required_module_set_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_EQ(laplace_perfcache_generation_artifact_set_fingerprint(
                  &request, &request.sink_artifact_set_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    auto context = Context();
    auto staged = StageReceipt(
        context, source, recipe, request.sink_artifact_set_fingerprint);
    request.staged_receipt_id = staged.receipt_id;
    request.stream_fingerprint = staged.stream_fingerprint;
    request.staged_sink_artifacts_fingerprint =
        staged.sink_artifacts_fingerprint;
    laplace_perfcache_prepared_generation* prepared = nullptr;
    laplace_perfcache_generation_receipt receipt{};
    ASSERT_EQ(laplace_perfcache_registry_prepare(
                  registry, &context, &staged, &provider, &request,
                  &prepared, &receipt),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_NE(prepared, nullptr);
    laplace_perfcache_registry_discard_prepared(&prepared);

    const std::array<laplace_digest256, 2> declared_digests{{
        Digest(0xd0u), Digest(0xe0u)}};
    const std::array<laplace_perfcache_generation_dependency, 2>
        cyclic_dependencies{{
            {modules[1].module_id, declared_digests[1]},
            {modules[0].module_id, declared_digests[0]}}};
    ASSERT_EQ(laplace_perfcache_dependency_fingerprint(
                  &cyclic_dependencies[0], 1u,
                  &artifacts[0].contract.dependency_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_EQ(laplace_perfcache_dependency_fingerprint(
                  &cyclic_dependencies[1], 1u,
                  &artifacts[1].contract.dependency_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    artifacts[0].expected_artifact_digest = declared_digests[0];
    artifacts[0].dependencies = &cyclic_dependencies[0];
    artifacts[0].dependency_count = 1u;
    artifacts[1].expected_artifact_digest = declared_digests[1];
    artifacts[1].dependencies = &cyclic_dependencies[1];
    ASSERT_EQ(laplace_perfcache_generation_artifact_set_fingerprint(
                  &request, &request.sink_artifact_set_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    staged = StageReceipt(
        context, source, recipe, request.sink_artifact_set_fingerprint);
    request.staged_receipt_id = staged.receipt_id;
    request.stream_fingerprint = staged.stream_fingerprint;
    request.staged_sink_artifacts_fingerprint =
        staged.sink_artifacts_fingerprint;
    prepared = nullptr;
    EXPECT_EQ(laplace_perfcache_registry_prepare(
                  registry, &context, &staged, &provider, &request,
                  &prepared, &receipt),
              LAPLACE_PERFCACHE_REGISTRY_DEPENDENCY_INVALID);
    EXPECT_EQ(prepared, nullptr);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {paths[0], paths[1]});
}

TEST(PerfcacheRegistry, ForgedOrUnrelatedStagedReceiptCannotPrepare) {
    const auto module = Module(0x10u);
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string forged_path = directory + "/forged.bin";
    const std::string unrelated_path = directory + "/unrelated.bin";
    auto forged = Prepare(
        registry, provider, &module, 1u, module, forged_path, Id(0x60u),
        Digest(0x80u), 0x30u, 100u,
        LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT, 0u, true, false);
    auto unrelated = Prepare(
        registry, provider, &module, 1u, module, unrelated_path, Id(0x70u),
        Digest(0x90u), 0x40u, 200u,
        LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT, 0u, false, true);
    EXPECT_EQ(forged.value, nullptr);
    EXPECT_EQ(unrelated.value, nullptr);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {forged_path, unrelated_path});
}

TEST(PerfcacheRegistry, CanonicalManifestReconstructsARealGeneration) {
    const auto module = Module(0x10u);
    const std::string directory = NewDirectory();
    const std::string path = directory + "/manifest.bin";
    const laplace_id128 activation = Id(0x70u);
    const laplace_digest256 epoch = Digest(0x90u);
    const laplace_digest256 source = Digest(0x30u);
    const laplace_digest256 recipe = Digest(0x50u);
    const laplace_perfcache_contract contract =
        Contract(module, activation, epoch, source, recipe);
    const auto artifact_bytes = Artifact(contract, 700u);
    const laplace_digest256 artifact_digest =
        ArtifactDigest(artifact_bytes, contract);
    Publish(path, artifact_bytes, contract);
    laplace_perfcache_generation_artifact artifact{};
    artifact.path = path.c_str();
    artifact.contract = contract;
    artifact.expected_artifact_digest = artifact_digest;
    artifact.flags = LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED;
    laplace_perfcache_generation_request request{};
    request.artifacts = &artifact;
    request.artifact_count = 1u;
    request.activation_epoch_id = activation;
    request.epoch_fingerprint = epoch;
    ASSERT_EQ(laplace_perfcache_required_module_set_fingerprint(
                  &module, 1u, &request.required_module_set_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_EQ(laplace_perfcache_generation_artifact_set_fingerprint(
                  &request, &request.sink_artifact_set_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    auto context = Context();
    auto staged = StageReceipt(
        context, source, recipe, request.sink_artifact_set_fingerprint);
    request.staged_receipt_id = staged.receipt_id;
    request.stream_fingerprint = staged.stream_fingerprint;
    request.staged_sink_artifacts_fingerprint =
        staged.sink_artifacts_fingerprint;

    std::size_t manifest_bytes = 0u;
    ASSERT_EQ(laplace_perfcache_generation_manifest_measure(
                  &context, &staged, &request, &manifest_bytes),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_GT(manifest_bytes, 32u);
    std::vector<std::uint8_t> encoded(manifest_bytes, 0u);
    laplace_digest256 encoded_fingerprint{};
    std::size_t written = 0u;
    ASSERT_EQ(laplace_perfcache_generation_manifest_write(
                  &context, &staged, &request, encoded.data(), encoded.size(),
                  &written, &encoded_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_EQ(written, encoded.size());
    EXPECT_EQ(laplace_perfcache_generation_manifest_write(
                  &context, &staged, &request, encoded.data(),
                  encoded.size() - 1u, &written, &encoded_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_BUFFER_TOO_SMALL);

    laplace_perfcache_generation_manifest* decoded = nullptr;
    laplace_digest256 decoded_fingerprint{};
    ASSERT_EQ(laplace_perfcache_generation_manifest_open(
                  encoded.data(), encoded.size(), &decoded,
                  &decoded_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_NE(decoded, nullptr);
    EXPECT_TRUE(Same(encoded_fingerprint, decoded_fingerprint));
    const laplace_framework_context* decoded_context = nullptr;
    const laplace_framework_stream_receipt* decoded_staged = nullptr;
    const laplace_perfcache_generation_request* decoded_request = nullptr;
    ASSERT_EQ(laplace_perfcache_generation_manifest_view(
                  decoded, &decoded_context, &decoded_staged,
                  &decoded_request),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_NE(decoded_context, nullptr);
    ASSERT_NE(decoded_staged, nullptr);
    ASSERT_NE(decoded_request, nullptr);
    EXPECT_EQ(decoded_request->artifact_count, 1u);
    EXPECT_EQ(std::string(decoded_request->artifacts[0].path), path);

    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_prepared_generation* prepared = nullptr;
    laplace_perfcache_generation_receipt prepared_receipt{};
    ASSERT_EQ(laplace_perfcache_registry_prepare(
                  registry, decoded_context, decoded_staged, &provider,
                  decoded_request, &prepared, &prepared_receipt),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_NE(prepared, nullptr);
    laplace_perfcache_registry_discard_prepared(&prepared);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_generation_manifest_close(decoded);

    encoded.back() ^= 1u;
    decoded = nullptr;
    EXPECT_EQ(laplace_perfcache_generation_manifest_open(
                  encoded.data(), encoded.size(), &decoded,
                  &decoded_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_MANIFEST_INVALID);
    EXPECT_EQ(decoded, nullptr);
    Cleanup(directory, {path});
}

TEST(PerfcacheRegistry, DurableGenerationMaterializesWithoutForgingActivation) {
    const auto module = Module(0x10u);
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string path = directory + "/materialized.bin";
    const laplace_id128 activation = Id(0x71u);
    const laplace_digest256 epoch_fingerprint = Digest(0x91u);
    auto prepared = Prepare(
        registry, provider, &module, 1u, module, path, activation,
        epoch_fingerprint, 0x31u, 900u);
    laplace_perfcache_generation_receipt receipt{};
    ASSERT_EQ(laplace_perfcache_registry_materialize_prepared(
                  registry, &prepared.value, &receipt),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(receipt.disposition, LAPLACE_PERFCACHE_GENERATION_MATERIALIZED);
    EXPECT_EQ(prepared.value, nullptr);

    laplace_perfcache_registry_snapshot snapshot{};
    ASSERT_EQ(laplace_perfcache_registry_snapshot_get(registry, &snapshot),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(snapshot.has_active_generation, 0u);
    EXPECT_EQ(snapshot.retired_generation_count, 1u);
    laplace_perfcache_pin active_pin{};
    EXPECT_EQ(laplace_perfcache_registry_pin(
                  registry, 0u, nullptr, &active_pin),
              LAPLACE_PERFCACHE_REGISTRY_NO_ACTIVE_GENERATION);

    const laplace_perfcache_epoch epoch{activation, epoch_fingerprint};
    laplace_perfcache_pin exact_pin{};
    ASSERT_EQ(laplace_perfcache_registry_pin_epoch(
                  registry, &epoch, &exact_pin),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::array<std::uint8_t, 8> keys{{
        0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u}};
    std::array<std::uint64_t, 2> indexes{};
    std::array<std::uint8_t, 2> found{};
    ASSERT_EQ(laplace_perfcache_pin_lookup_batch(
                  &exact_pin, &module.module_id, keys.data(), 2u,
                  indexes.data(), found.data()),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(indexes, (std::array<std::uint64_t, 2>{{0u, 2u}}));
    EXPECT_EQ(found, (std::array<std::uint8_t, 2>{{1u, 1u}}));
    ASSERT_EQ(laplace_perfcache_pin_release(&exact_pin),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {path});
}

TEST(PerfcacheRegistry, ReadOnlyContextCannotReserveAndPreparedStateBlocksDestroy) {
    const auto module = Module(0x10u);
    laplace_perfcache_registry* registry = nullptr;
    ASSERT_EQ(laplace_perfcache_registry_create(&module, 1u, &registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const std::string directory = NewDirectory();
    const std::string path = directory + "/read-only.bin";
    auto prepared = Prepare(
        registry, provider, &module, 1u, module, path, Id(0x70u),
        Digest(0x90u), 0x30u, 100u, LAPLACE_PERFCACHE_REGISTRY_OK,
        LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY);
    laplace_perfcache_activation* activation = nullptr;
    laplace_framework_activation_provider_v1 activation_provider{};
    ASSERT_EQ(laplace_perfcache_activation_create(
                  registry, &prepared.value, 0u, nullptr, &activation,
                  &activation_provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_framework_activation_request request{};
    request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_PERFCACHE;
    request.expected_epoch =
        prepared.context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE];
    request.next_epoch = prepared.receipt.epoch_fingerprint;
    laplace_framework_activation_receipt framework_receipt{};
    EXPECT_EQ(laplace_framework_admit_staged_stream(
                  &prepared.context, &prepared.staged, &request,
                  &activation_provider, &framework_receipt),
              LAPLACE_FRAMEWORK_EFFECT_NOT_AUTHORIZED);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_BUSY);
    laplace_perfcache_activation_destroy(activation);
    EXPECT_EQ(laplace_perfcache_registry_destroy(registry),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    Cleanup(directory, {path});
}

}  // namespace
