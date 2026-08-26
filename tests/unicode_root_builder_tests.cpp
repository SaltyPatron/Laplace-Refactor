#include "laplace/unicode_root_builder.h"
#include "laplace/perfcache_modules.h"
#include "laplace/perfcache_registry.h"
#include "laplace/unicode_tier0_sink.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::array<char, 80> pattern{};
        const std::string value =
            "/tmp/laplace-unicode-root-builder-test-XXXXXX";
        std::memcpy(pattern.data(), value.c_str(), value.size() + 1U);
        char* const created = ::mkdtemp(pattern.data());
        if (created != nullptr) {
            path_ = created;
        }
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path& Path() const { return path_; }

private:
    fs::path path_;
};

struct SpoolHandle {
    laplace_canonical_spool* value{};
    ~SpoolHandle() { laplace_canonical_spool_destroy(&value); }
};

struct Tier0SinkHandle {
    laplace_unicode_tier0_sink* value{};
    ~Tier0SinkHandle() { laplace_unicode_tier0_sink_destroy(&value); }
};

struct PerfcacheRegistryHandle {
    laplace_perfcache_registry* value{};
    ~PerfcacheRegistryHandle() {
        if (value != nullptr) {
            EXPECT_EQ(laplace_perfcache_registry_destroy(value),
                      LAPLACE_PERFCACHE_REGISTRY_OK);
        }
    }
};

bool SameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

std::string Hex(const laplace_digest256& value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint8_t byte : value.bytes) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

laplace_digest256 Digest(const std::string_view hex) {
    laplace_digest256 result{};
    EXPECT_EQ(hex.size(), sizeof(result.bytes) * 2U);
    const auto nibble = [](const char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(10 + value - 'a');
        }
        return 0xffU;
    };
    for (std::size_t index = 0U; index < sizeof(result.bytes); ++index) {
        const std::uint8_t high = nibble(hex[index * 2U]);
        const std::uint8_t low = nibble(hex[index * 2U + 1U]);
        EXPECT_LE(high, 0x0fU);
        EXPECT_LE(low, 0x0fU);
        result.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned>(high) * 16U + low);
    }
    return result;
}

void FillDigest(laplace_digest256* const digest, const std::uint8_t seed) {
    for (std::size_t index = 0U; index < sizeof(digest->bytes); ++index) {
        digest->bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

void FillId(laplace_id128* const id, const std::uint8_t seed) {
    for (std::size_t index = 0U; index < sizeof(id->bytes); ++index) {
        id->bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_framework_context Context() {
    laplace_framework_context context{};
    context.major = LAPLACE_FRAMEWORK_MAJOR;
    context.minor = LAPLACE_FRAMEWORK_MINOR;
    context.flags = LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY;
    context.epoch_mask =
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_IDENTITY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_DATABASE) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_NUMERIC) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PACKAGE);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_IDENTITY], 0x10U);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY], 0x30U);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE], 0x40U);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC], 0x50U);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_PACKAGE], 0x70U);
    FillDigest(&context.authority_fingerprint, 0x90U);
    context.resource_grant = laplace_execution_grant{
        UINT64_C(1) << 31U, 6U, 2U};
    return context;
}

int NeverCancel(void*) { return 0; }

void IgnoreProgress(
    void*, const laplace_framework_replay_checkpoint*) {}

laplace_framework_producer_control_v1 ProducerControl() {
    return laplace_framework_producer_control_v1{
        nullptr,
        nullptr,
        NeverCancel,
        IgnoreProgress,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS,
        0U};
}

laplace_framework_producer_control_v1 ReplayControl(
    const laplace_framework_replay_checkpoint* const checkpoint) {
    return laplace_framework_producer_control_v1{
        nullptr,
        checkpoint,
        NeverCancel,
        IgnoreProgress,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS,
        0U};
}

struct CancelAfterFirstBatchState {
    bool cancel{};
};

int CancelAfterFirstBatch(void* const opaque) {
    return static_cast<CancelAfterFirstBatchState*>(opaque)->cancel ? 1 : 0;
}

void ObserveUntilFirstBatch(
    void* const opaque,
    const laplace_framework_replay_checkpoint* const checkpoint) {
    auto* const state = static_cast<CancelAfterFirstBatchState*>(opaque);
    if (checkpoint->completed_batches == 1U) {
        state->cancel = true;
    }
}

laplace_framework_producer_control_v1 CancelAfterOneBatchControl(
    CancelAfterFirstBatchState* const state) {
    return laplace_framework_producer_control_v1{
        state,
        nullptr,
        CancelAfterFirstBatch,
        ObserveUntilFirstBatch,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS,
        0U};
}

struct ValidatingSink {
    laplace_unicode_root_stream_expectation expectation{};
    laplace_unicode_root_stream_validator* validator{};
    laplace_unicode_root_stream_summary summary{};
    std::uint64_t expected_records{};
    std::uint64_t expected_bytes{};
    std::uint64_t staged_records{};
    std::uint64_t staged_bytes{};
    std::uint64_t staged_batches{};
    bool begun{};
    bool sealed{};
    bool aborted{};

    ~ValidatingSink() {
        laplace_unicode_root_stream_validator_destroy(validator);
    }
};

laplace_framework_status BeginSink(
    void* const opaque,
    const laplace_framework_context*,
    const std::uint32_t record_type,
    const std::uint64_t total_records,
    const std::uint64_t total_bytes) {
    auto* const sink = static_cast<ValidatingSink*>(opaque);
    if (sink == nullptr || sink->validator != nullptr ||
        record_type != LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE) {
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    if (laplace_unicode_root_stream_validator_create(
            &sink->expectation, &sink->validator) != LAPLACE_UNICODE_OK) {
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    sink->expected_records = total_records;
    sink->expected_bytes = total_bytes;
    sink->begun = true;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status StageSink(
    void* const opaque,
    const laplace_framework_canonical_batch* const batch) {
    auto* const sink = static_cast<ValidatingSink*>(opaque);
    if (sink == nullptr || !sink->begun || sink->sealed || batch == nullptr ||
        laplace_unicode_root_stream_validator_consume(
            sink->validator, batch->canonical_bytes,
            static_cast<std::size_t>(batch->byte_count), batch->record_count,
            batch->first_ordinal) != LAPLACE_UNICODE_OK) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    sink->staged_records += batch->record_count;
    sink->staged_bytes += batch->byte_count;
    ++sink->staged_batches;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SealSink(
    void* const opaque,
    const laplace_digest256*,
    laplace_digest256* const artifact_fingerprint) {
    auto* const sink = static_cast<ValidatingSink*>(opaque);
    if (sink == nullptr || !sink->begun || sink->sealed ||
        sink->staged_records != sink->expected_records ||
        sink->staged_bytes != sink->expected_bytes ||
        laplace_unicode_root_stream_validator_finish(
            sink->validator, &sink->summary) != LAPLACE_UNICODE_OK) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    *artifact_fingerprint = sink->summary.receipt_id;
    sink->sealed = true;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortSink(void* const opaque) {
    auto* const sink = static_cast<ValidatingSink*>(opaque);
    if (sink != nullptr) {
        sink->aborted = true;
    }
}

laplace_framework_sink_v1 Sink(ValidatingSink* const state) {
    return laplace_framework_sink_v1{
        state,
        BeginSink,
        StageSink,
        SealSink,
        AbortSink,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR,
        0U,
        0U};
}

}  // namespace

TEST(UnicodeRootBuilder, BuildsOneCanonicalRootAndReplaysToSiblingSinks) {
    TemporaryDirectory directory;
    ASSERT_FALSE(directory.Path().empty());
    laplace_unicode_numeric_provider_v1 provider{};
    ASSERT_EQ(laplace_unicode_numeric_oneapi_provider(&provider),
              LAPLACE_UNICODE_OK);
    const laplace_unicode_root_build_request request{
        "/vault/Data/UCD/Public/UCD/latest",
        directory.Path().c_str(),
        &provider,
        UINT64_C(4) * 1024U * 1024U,
        4096U,
        LAPLACE_UNICODE_ROOT_BUILDER_ABI_MAJOR,
        LAPLACE_UNICODE_ROOT_BUILDER_ABI_MINOR,
        0U,
        0U};
    SpoolHandle spool{};
    laplace_unicode_root_build_summary build{};
    ASSERT_EQ(laplace_unicode_root_build_canonical_spool(
                  &request, &spool.value, &build),
              LAPLACE_UNICODE_ROOT_BUILD_OK)
        << "stage=" << build.stage << " unicode=" << build.unicode_status
        << " spool=" << build.spool_status
        << " numeric-status=" << build.numeric.status
        << " threading=" << build.numeric.threading_layer
        << " branch=" << build.numeric.instruction_branch
        << " vml=" << build.numeric.vml_status
        << " fp=" << build.numeric.floating_exceptions
        << " errno=" << build.numeric.system_error;
    ASSERT_NE(spool.value, nullptr);
    EXPECT_TRUE(fs::is_empty(directory.Path()));
    EXPECT_EQ(build.stage, LAPLACE_UNICODE_ROOT_BUILD_STAGE_COMPLETE);
    EXPECT_EQ(build.status, LAPLACE_UNICODE_ROOT_BUILD_OK);
    EXPECT_EQ(build.abi_major, LAPLACE_UNICODE_ROOT_BUILDER_ABI_MAJOR);
    EXPECT_EQ(build.abi_minor, LAPLACE_UNICODE_ROOT_BUILDER_ABI_MINOR);
    EXPECT_EQ(build.stream.manifest.atom_count,
              LAPLACE_UNICODE_ROOT_POPULATION);
    EXPECT_EQ(build.stream.manifest.ducet_position_count,
              LAPLACE_UNICODE_ROOT_POPULATION);
    EXPECT_EQ(build.stream.manifest.ducet_contraction_count, 964U);
    EXPECT_EQ(build.stream.manifest.normalization_composition_count, 961U);
    EXPECT_EQ(build.stream.total_frame_count,
              UINT64_C(2230150));
    EXPECT_EQ(build.spool.total_records, build.stream.total_frame_count);
    EXPECT_EQ(build.spool.total_bytes, build.stream.total_encoded_bytes);
    EXPECT_EQ(build.spool.record_type,
              LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE);

    laplace_unicode_root_stream_expectation expectation{};
    expectation.source_fingerprint = build.source.source_fingerprint;
    expectation.recipe_fingerprint = build.source.recipe_fingerprint;
    expectation.numeric_provider_receipt = build.numeric.receipt_id;
    expectation.stream_contract_fingerprint =
        build.stream.manifest.stream_contract_fingerprint;
    expectation.algorithmic_hangul_rule_fingerprint =
        build.stream.manifest.algorithmic_hangul_rule_fingerprint;
    expectation.atom_record_contract_fingerprint =
        build.stream.manifest.atom_record_contract_fingerprint;
    expectation.physicality_recipe_fingerprint =
        build.stream.manifest.physicality_recipe_fingerprint;
    expectation.placement_rank_permutation_fingerprint =
        build.placement.rank_permutation_fingerprint;
    expectation.coordinate_table_fingerprint =
        build.stream.manifest.coordinate_table_fingerprint;
    expectation.geometry_epoch = build.stream.manifest.geometry_epoch;
    expectation.physicality_recipe_version =
        build.stream.manifest.physicality_recipe_version;
    ValidatingSink validating{};
    validating.expectation = expectation;
    laplace_unicode_tier0_sink_configuration tier0_configuration{};
    const auto tier0_path = directory.Path() / "unicode-tier0.lpc";
    tier0_configuration.target_path = tier0_path.c_str();
    tier0_configuration.root_expectation = expectation;
    FillId(&tier0_configuration.activation_epoch_id, 0xa0U);
    FillDigest(
        &tier0_configuration.activation_epoch_fingerprint, 0xb0U);
    tier0_configuration.abi_major = LAPLACE_UNICODE_TIER0_SINK_ABI_MAJOR;
    tier0_configuration.abi_minor = LAPLACE_UNICODE_TIER0_SINK_ABI_MINOR;
    Tier0SinkHandle tier0{};
    laplace_framework_sink_v1 tier0_framework_sink{};
    ASSERT_EQ(laplace_unicode_tier0_sink_create(
                  &tier0_configuration, &tier0.value,
                  &tier0_framework_sink),
              LAPLACE_PERFCACHE_OK);
    std::array<laplace_framework_sink_v1, 2> sinks{{
        Sink(&validating), tier0_framework_sink}};
    laplace_framework_producer_v1 producer{};
    ASSERT_EQ(laplace_canonical_spool_producer(spool.value, &producer),
              LAPLACE_SPOOL_OK);
    auto context = Context();
    auto control = ProducerControl();
    std::array<laplace_digest256, 2> artifacts{};
    laplace_framework_sink_artifact_output artifact_output{
        artifacts.data(), artifacts.size(), 0U, 0U};
    laplace_framework_producer_receipt replay{};
    ASSERT_EQ(laplace_framework_run_producer_with_artifacts(
                  &context, &build.source.source_fingerprint,
                  &build.source.recipe_fingerprint, &producer, &control,
                  sinks.data(), sinks.size(), &artifact_output, &replay),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_TRUE(validating.sealed);
    EXPECT_FALSE(validating.aborted);
    EXPECT_EQ(validating.staged_batches, build.spool.batch_count);
    EXPECT_TRUE(SameDigest(
        validating.summary.receipt_id, build.stream.receipt_id));
    ASSERT_EQ(artifact_output.count, artifacts.size());
    EXPECT_TRUE(SameDigest(artifacts[0], build.stream.receipt_id));
    EXPECT_TRUE(SameDigest(
        replay.stream.stream_fingerprint, build.spool.stream_fingerprint));

    laplace_unicode_tier0_sink_result tier0_result{};
    ASSERT_EQ(laplace_unicode_tier0_sink_result_get(
                  tier0.value, &tier0_result),
              LAPLACE_PERFCACHE_OK);
    EXPECT_TRUE(SameDigest(
        artifacts[1], tier0_result.artifact_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        tier0_result.root_summary.receipt_id, build.stream.receipt_id));
    EXPECT_TRUE(SameDigest(
        tier0_result.root_framework_stream_fingerprint,
        build.spool.stream_fingerprint));
    EXPECT_EQ(tier0_result.atom_count,
              static_cast<std::uint64_t>(LAPLACE_UNICODE_ROOT_POPULATION));
    EXPECT_EQ(tier0_result.root_frame_count, build.spool.total_records);
    EXPECT_EQ(tier0_result.root_encoded_bytes, build.spool.total_bytes);
    EXPECT_GT(tier0_result.atom_metadata_bytes, 0U);
    EXPECT_EQ(
        Hex(tier0_result.artifact_digest),
        "bfc3c51d53b98731dee62c16cb6018d7a40baee25eb4db3341771ccadc2662cc");
    EXPECT_EQ(tier0_result.atom_metadata_bytes, UINT64_C(588784718));
    EXPECT_EQ(tier0_result.artifact_bytes, UINT64_C(762586574));
    EXPECT_TRUE(fs::is_regular_file(tier0_path));

    laplace_perfcache_module_v2 tier0_module{};
    ASSERT_EQ(laplace_perfcache_unicode_tier0_module(&tier0_module),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    PerfcacheRegistryHandle registry{};
    ASSERT_EQ(laplace_perfcache_registry_create(
                  &tier0_module, 1U, &registry.value),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_artifact_provider_v1 artifact_provider{};
    ASSERT_EQ(laplace_perfcache_file_provider(&artifact_provider),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_generation_artifact generation_artifact{};
    generation_artifact.path = tier0_path.c_str();
    generation_artifact.contract = tier0_result.contract;
    generation_artifact.expected_artifact_digest =
        tier0_result.artifact_digest;
    generation_artifact.flags =
        LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED;
    laplace_perfcache_generation_request generation_request{};
    generation_request.artifacts = &generation_artifact;
    generation_request.staged_sink_artifact_fingerprints = artifacts.data();
    generation_request.artifact_count = 1U;
    generation_request.staged_sink_count = artifacts.size();
    generation_request.perfcache_sink_index = 1U;
    generation_request.activation_epoch_id =
        tier0_configuration.activation_epoch_id;
    generation_request.epoch_fingerprint =
        tier0_configuration.activation_epoch_fingerprint;
    generation_request.staged_receipt_id = replay.stream.receipt_id;
    generation_request.stream_fingerprint =
        replay.stream.stream_fingerprint;
    generation_request.staged_sink_artifacts_fingerprint =
        replay.stream.sink_artifacts_fingerprint;
    generation_request.sink_artifact_set_fingerprint = artifacts[1];
    ASSERT_EQ(laplace_perfcache_required_module_set_fingerprint(
                  &tier0_module, 1U,
                  &generation_request.required_module_set_fingerprint),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    laplace_perfcache_prepared_generation* prepared = nullptr;
    laplace_perfcache_generation_receipt prepare_receipt{};
    ASSERT_EQ(laplace_perfcache_registry_prepare(
                  registry.value, &context, &replay.stream,
                  &artifact_provider, &generation_request, &prepared,
                  &prepare_receipt),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_NE(prepared, nullptr);
    EXPECT_EQ(prepare_receipt.artifact_count, 1U);
    EXPECT_EQ(prepare_receipt.mapped_bytes, tier0_result.artifact_bytes);
    EXPECT_EQ(prepare_receipt.prefaulted_bytes, tier0_result.artifact_bytes);
    laplace_perfcache_registry_discard_prepared(&prepared);
    EXPECT_EQ(prepared, nullptr);

    Tier0SinkHandle replay_tier0{};
    laplace_framework_sink_v1 replay_tier0_framework_sink{};
    ASSERT_EQ(laplace_unicode_tier0_sink_create(
                  &tier0_configuration, &replay_tier0.value,
                  &replay_tier0_framework_sink),
              LAPLACE_PERFCACHE_OK);
    ValidatingSink replay_validating{};
    replay_validating.expectation = expectation;
    std::array<laplace_framework_sink_v1, 2> replay_sinks{{
        Sink(&replay_validating), replay_tier0_framework_sink}};
    std::array<laplace_digest256, 2> replay_artifacts{};
    laplace_framework_sink_artifact_output replay_artifact_output{
        replay_artifacts.data(), replay_artifacts.size(), 0U, 0U};
    laplace_framework_producer_receipt deterministic_rerun{};
    ASSERT_EQ(laplace_framework_run_producer_with_artifacts(
                  &context, &build.source.source_fingerprint,
                  &build.source.recipe_fingerprint, &producer, &control,
                  replay_sinks.data(), replay_sinks.size(),
                  &replay_artifact_output, &deterministic_rerun),
              LAPLACE_FRAMEWORK_OK);
    laplace_unicode_tier0_sink_result replay_tier0_result{};
    ASSERT_EQ(laplace_unicode_tier0_sink_result_get(
                  replay_tier0.value, &replay_tier0_result),
              LAPLACE_PERFCACHE_OK);
    EXPECT_TRUE(SameDigest(
        replay_tier0_result.artifact_digest, tier0_result.artifact_digest));
    EXPECT_TRUE(SameDigest(replay_artifacts[1], artifacts[1]));

    const auto cancelled_path = directory.Path() / "cancelled-tier0.lpc";
    tier0_configuration.target_path = cancelled_path.c_str();
    Tier0SinkHandle cancelled_tier0{};
    laplace_framework_sink_v1 cancelled_sink{};
    ASSERT_EQ(laplace_unicode_tier0_sink_create(
                  &tier0_configuration, &cancelled_tier0.value,
                  &cancelled_sink),
              LAPLACE_PERFCACHE_OK);
    CancelAfterFirstBatchState cancellation{};
    auto cancel_control = CancelAfterOneBatchControl(&cancellation);
    laplace_framework_producer_receipt cancelled_receipt{};
    EXPECT_EQ(laplace_framework_run_producer(
                  &context, &build.source.source_fingerprint,
                  &build.source.recipe_fingerprint, &producer,
                  &cancel_control, &cancelled_sink, 1U,
                  &cancelled_receipt),
              LAPLACE_FRAMEWORK_PRODUCER_CANCELLED);
    EXPECT_FALSE(fs::exists(cancelled_path));
    for (const auto& entry : fs::directory_iterator(directory.Path())) {
        EXPECT_EQ(entry.path().filename(), tier0_path.filename());
    }

    tier0_configuration.target_path = tier0_path.c_str();
    Tier0SinkHandle resumed_tier0{};
    laplace_framework_sink_v1 resumed_tier0_framework_sink{};
    ASSERT_EQ(laplace_unicode_tier0_sink_create(
                  &tier0_configuration, &resumed_tier0.value,
                  &resumed_tier0_framework_sink),
              LAPLACE_PERFCACHE_OK);
    ValidatingSink resumed_validating{};
    resumed_validating.expectation = expectation;
    std::array<laplace_framework_sink_v1, 2> resumed_sinks{{
        Sink(&resumed_validating), resumed_tier0_framework_sink}};
    std::array<laplace_digest256, 2> resumed_artifacts{};
    laplace_framework_sink_artifact_output resumed_artifact_output{
        resumed_artifacts.data(), resumed_artifacts.size(), 0U, 0U};
    auto resume_control = ReplayControl(&cancelled_receipt.checkpoint);
    laplace_framework_producer_receipt resumed{};
    ASSERT_EQ(laplace_framework_run_producer_with_artifacts(
                  &context, &build.source.source_fingerprint,
                  &build.source.recipe_fingerprint, &producer,
                  &resume_control, resumed_sinks.data(), resumed_sinks.size(),
                  &resumed_artifact_output, &resumed),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(resumed.replay_verified, 1U);
    EXPECT_TRUE(SameDigest(
        resumed.replay_checkpoint_id, cancelled_receipt.checkpoint.checkpoint_id));
    EXPECT_TRUE(SameDigest(resumed_artifacts[1], artifacts[1]));
    laplace_unicode_tier0_sink_result resumed_tier0_result{};
    ASSERT_EQ(laplace_unicode_tier0_sink_result_get(
                  resumed_tier0.value, &resumed_tier0_result),
              LAPLACE_PERFCACHE_OK);
    EXPECT_TRUE(SameDigest(
        resumed_tier0_result.artifact_digest, tier0_result.artifact_digest));

    const std::array<laplace_digest256, 13> expected{{
        Digest("a390dd47fce00fab8fa836fc4b8f0474212d714bc6654d42bf8e28ec3fd036e3"),
        Digest("1f3b0ddf7283401bcf91e9a8ed70f00e1bd317eff5c78dcebca22d5ad6b9c75e"),
        Digest("a5296dd7a249bd88caed034e5347f156482580e113d6eee63217b044db6a3a83"),
        Digest("2b8bcaf463a118c6979983767c6efb08f8c6c468f99f695b76c450eeccc1b4d6"),
        Digest("e28ee38888d5a1944fa9d2f87e6686b8bd6c091d9ac152f8eb7d0880184790ed"),
        Digest("d0dc5ae88a7e3a8d42aba57286b8c248033f6ed6eb7dd9ea5509390a1503b4ef"),
        Digest("a14749f49928f3e8f0201100accae46650036abc233b0534b276ff0d32327ae1"),
        Digest("449f0a8551dbe3eb736c2f2a026813ba36137b2bd77a386252b603110a60716c"),
        Digest("dd4e92caaf4314bceaec0a04724fa233babebb257173e3a52e6d4c8e61218397"),
        Digest("c888a3bfc929f66d12b56980bef98a22851927d311221ff22174a793ddc37d18"),
        Digest("873e6c09b1bd3e45666295f8a9997a92a30ef7378a4ea852f2f521c20968f38e"),
        Digest("042ba321f69cbe4531277601e7242ba8ac07d5bb2635ee8b5f5b62f99f0d9df2"),
        Digest("d529d0b912b7689cfb0000b878fe7dc1d337a3c42e9ed7278b5fcd90d9ef3b06")}};
    const std::array<const laplace_digest256*, 13> observed{{
        &build.receipt_id,
        &build.stream.receipt_id,
        &build.spool.stream_fingerprint,
        &build.spool.spool_fingerprint,
        &build.stream.section_fingerprints[0],
        &build.stream.section_fingerprints[1],
        &build.stream.section_fingerprints[2],
        &build.stream.section_fingerprints[3],
        &build.stream.manifest.atom_record_contract_fingerprint,
        &build.stream.manifest.physicality_recipe_fingerprint,
        &build.stream.manifest.placement_rank_permutation_fingerprint,
        &build.stream.manifest.coordinate_table_fingerprint,
        &build.stream.manifest.geometry_epoch}};
    for (std::size_t index = 0U; index < observed.size(); ++index) {
        EXPECT_TRUE(SameDigest(*observed[index], expected[index]))
            << index << ":" << Hex(*observed[index]);
    }
    EXPECT_EQ(build.spool.total_bytes, UINT64_C(741350735));
    EXPECT_EQ(build.spool.batch_count, UINT64_C(546));
}

TEST(UnicodeRootBuilder, RejectsUnversionedNumericProviderBeforeSourceWork) {
    laplace_unicode_numeric_provider_v1 provider{};
    provider.abi_major = 0U;
    const laplace_unicode_root_build_request request{
        "/vault/Data/UCD/Public/UCD/latest",
        "/tmp",
        &provider,
        4096U,
        1U,
        LAPLACE_UNICODE_ROOT_BUILDER_ABI_MAJOR,
        LAPLACE_UNICODE_ROOT_BUILDER_ABI_MINOR,
        0U,
        0U};
    laplace_canonical_spool* spool = nullptr;
    laplace_unicode_root_build_summary summary{};
    EXPECT_EQ(laplace_unicode_root_build_canonical_spool(
                  &request, &spool, &summary),
              LAPLACE_UNICODE_ROOT_BUILD_INVALID_ARGUMENT);
    EXPECT_EQ(spool, nullptr);
    EXPECT_EQ(summary.stage, LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC);
    EXPECT_EQ(summary.unicode_status, LAPLACE_UNICODE_INVALID_ARGUMENT);
}

TEST(UnicodeRootBuilder, RejectsUnversionedBuilderBeforeSourceWork) {
    laplace_unicode_numeric_provider_v1 provider{};
    ASSERT_EQ(laplace_unicode_numeric_oneapi_provider(&provider),
              LAPLACE_UNICODE_OK);
    const laplace_unicode_root_build_request request{
        "/path/that/must/not/be-opened",
        "/tmp",
        &provider,
        4096U,
        1U,
        0U,
        0U,
        0U,
        0U};
    laplace_canonical_spool* spool = nullptr;
    laplace_unicode_root_build_summary summary{};
    EXPECT_EQ(laplace_unicode_root_build_canonical_spool(
                  &request, &spool, &summary),
              LAPLACE_UNICODE_ROOT_BUILD_INVALID_ARGUMENT);
    EXPECT_EQ(spool, nullptr);
    EXPECT_EQ(summary.stage, LAPLACE_UNICODE_ROOT_BUILD_STAGE_NONE);
    EXPECT_EQ(summary.unicode_status, LAPLACE_UNICODE_INVALID_ARGUMENT);
}
