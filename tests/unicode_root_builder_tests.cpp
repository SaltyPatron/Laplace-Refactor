#include "laplace/unicode_root_builder.h"

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
    ValidatingSink left{};
    ValidatingSink right{};
    left.expectation = expectation;
    right.expectation = expectation;
    std::array<laplace_framework_sink_v1, 2> sinks{{
        Sink(&left), Sink(&right)}};
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
    EXPECT_TRUE(left.sealed);
    EXPECT_TRUE(right.sealed);
    EXPECT_FALSE(left.aborted);
    EXPECT_FALSE(right.aborted);
    EXPECT_EQ(left.staged_batches, build.spool.batch_count);
    EXPECT_EQ(right.staged_batches, build.spool.batch_count);
    EXPECT_TRUE(SameDigest(left.summary.receipt_id, build.stream.receipt_id));
    EXPECT_TRUE(SameDigest(right.summary.receipt_id, build.stream.receipt_id));
    ASSERT_EQ(artifact_output.count, artifacts.size());
    EXPECT_TRUE(SameDigest(artifacts[0], build.stream.receipt_id));
    EXPECT_TRUE(SameDigest(artifacts[1], build.stream.receipt_id));
    EXPECT_TRUE(SameDigest(
        replay.stream.stream_fingerprint, build.spool.stream_fingerprint));

    const std::array<laplace_digest256, 8> expected{{
        Digest("025aeb839533cc304565c1329f59101373690e862cbd0a90bee0a6c97fe2cc3b"),
        Digest("e72d810a95c0e33a2b1ccd4066453348c0cd73cdcdc3fe369fa78c66204008be"),
        Digest("451b59a4b602d252a2b61948437d889d5b6af0b82a22141791b237c69cee109d"),
        Digest("3cbe00190e398a1af96b08b2ac865b30f45f1cf78566f7201a648ea6887b6260"),
        Digest("186cd29226145590d60baa091774a05646ed08ede5c56dc3c256c833eaa4614b"),
        Digest("29138acfc6520443aa30c8e69cb3732f9a434b3787c16612244521395a08c883"),
        Digest("687592ea35ecdd2394ee2b72f28ab2d2731f4c1d2d0408220dcaed7088f5891c"),
        Digest("2bf0846966dae06373d58c35ab51b5d2a2cc3093c98333bda099a57ced3e9b5b")}};
    const std::array<const laplace_digest256*, 8> observed{{
        &build.receipt_id,
        &build.stream.receipt_id,
        &build.spool.stream_fingerprint,
        &build.spool.spool_fingerprint,
        &build.stream.section_fingerprints[0],
        &build.stream.section_fingerprints[1],
        &build.stream.section_fingerprints[2],
        &build.stream.section_fingerprints[3]}};
    for (std::size_t index = 0U; index < observed.size(); ++index) {
        EXPECT_TRUE(SameDigest(*observed[index], expected[index]))
            << index << ":" << Hex(*observed[index]);
    }
    EXPECT_EQ(build.spool.total_bytes, UINT64_C(670047407));
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
