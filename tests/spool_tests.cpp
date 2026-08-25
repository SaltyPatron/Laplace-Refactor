#include "laplace/spool.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::array<char, 64> pattern{};
        const std::string value = "/tmp/laplace-spool-test-XXXXXX";
        std::memcpy(pattern.data(), value.c_str(), value.size() + 1u);
        char* created = ::mkdtemp(pattern.data());
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

void FillDigest(laplace_digest256* digest, std::uint8_t seed) {
    for (std::size_t index = 0u; index < sizeof(digest->bytes); ++index) {
        digest->bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
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
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_IDENTITY], 0x10u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY], 0x30u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE], 0x40u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC], 0x50u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_PACKAGE], 0x70u);
    FillDigest(&context.authority_fingerprint, 0x90u);
    context.resource_grant = laplace_execution_grant{1u << 20u, 2u, 1u};
    return context;
}

laplace_framework_canonical_batch Batch(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    std::uint64_t record_count,
    std::uint64_t first_ordinal) {
    return laplace_framework_canonical_batch{
        bytes,
        static_cast<std::uint64_t>(byte_count),
        record_count,
        first_ordinal,
        LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
}

struct CollectingSink final {
    std::vector<std::uint8_t> bytes;
    std::uint64_t expected_bytes{};
    std::uint32_t record_type{};
    bool begun{};
    bool sealed{};
    bool aborted{};
};

laplace_framework_status BeginSink(
    void* opaque,
    const laplace_framework_context*,
    std::uint32_t record_type,
    std::uint64_t,
    std::uint64_t total_bytes) {
    auto* sink = static_cast<CollectingSink*>(opaque);
    sink->bytes.clear();
    sink->expected_bytes = total_bytes;
    sink->record_type = record_type;
    sink->begun = true;
    sink->sealed = false;
    sink->aborted = false;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status StageSink(
    void* opaque,
    const laplace_framework_canonical_batch* batch) {
    auto* sink = static_cast<CollectingSink*>(opaque);
    if (!sink->begun || sink->sealed || batch == nullptr ||
        batch->record_type != sink->record_type) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    try {
        sink->bytes.insert(
            sink->bytes.end(), batch->canonical_bytes,
            batch->canonical_bytes + batch->byte_count);
    } catch (...) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SealSink(
    void* opaque,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint) {
    auto* sink = static_cast<CollectingSink*>(opaque);
    if (!sink->begun || sink->sealed || stream_fingerprint == nullptr ||
        artifact_fingerprint == nullptr ||
        sink->bytes.size() != sink->expected_bytes) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    *artifact_fingerprint = *stream_fingerprint;
    sink->sealed = true;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortSink(void* opaque) {
    auto* sink = static_cast<CollectingSink*>(opaque);
    sink->bytes.clear();
    sink->sealed = false;
    sink->aborted = true;
}

laplace_framework_sink_v1 Sink(CollectingSink* state) {
    return laplace_framework_sink_v1{
        state,
        BeginSink,
        StageSink,
        SealSink,
        AbortSink,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR,
        0u,
        0u};
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
        0u};
}

struct SpoolHandle final {
    laplace_canonical_spool* value{};
    ~SpoolHandle() { laplace_canonical_spool_destroy(&value); }
};

laplace_canonical_spool_summary BuildSpool(
    const fs::path& directory,
    const laplace_digest256& source,
    const laplace_digest256& recipe,
    const std::array<std::uint8_t, 10>& bytes,
    bool alternate_partition,
    SpoolHandle* handle) {
    EXPECT_EQ(laplace_canonical_spool_create(
                  directory.c_str(), LAPLACE_ISA_VALUE_U32_VECTOR,
                  &source, &recipe, &handle->value),
              LAPLACE_SPOOL_OK);
    if (alternate_partition) {
        const auto first = Batch(bytes.data(), 7u, 4u, 0u);
        const auto second = Batch(bytes.data() + 7u, 3u, 1u, 4u);
        EXPECT_EQ(laplace_canonical_spool_append(handle->value, &first),
                  LAPLACE_SPOOL_OK);
        EXPECT_EQ(laplace_canonical_spool_append(handle->value, &second),
                  LAPLACE_SPOOL_OK);
    } else {
        const auto first = Batch(bytes.data(), 4u, 2u, 0u);
        const auto second = Batch(bytes.data() + 4u, 6u, 3u, 2u);
        EXPECT_EQ(laplace_canonical_spool_append(handle->value, &first),
                  LAPLACE_SPOOL_OK);
        EXPECT_EQ(laplace_canonical_spool_append(handle->value, &second),
                  LAPLACE_SPOOL_OK);
    }
    laplace_canonical_spool_summary summary{};
    EXPECT_EQ(laplace_canonical_spool_seal(handle->value, &summary),
              LAPLACE_SPOOL_OK);
    return summary;
}

}  // namespace

TEST(CanonicalSpool, ReplaysExactAnonymousMappedBytesThroughFrameworkProducer) {
    TemporaryDirectory directory;
    ASSERT_FALSE(directory.Path().empty());
    laplace_digest256 source{};
    laplace_digest256 recipe{};
    FillDigest(&source, 0x21u);
    FillDigest(&recipe, 0x61u);
    std::array<std::uint8_t, 10> bytes{{
        0x10u, 0x11u, 0x12u, 0x13u, 0x20u,
        0x21u, 0x22u, 0x23u, 0x24u, 0x25u}};
    const auto expected = bytes;
    SpoolHandle handle{};
    const auto summary = BuildSpool(
        directory.Path(), source, recipe, bytes, false, &handle);

    EXPECT_TRUE(fs::is_empty(directory.Path()));
    EXPECT_EQ(summary.batch_count, 2u);
    EXPECT_EQ(summary.total_records, 5u);
    EXPECT_EQ(summary.total_bytes, expected.size());
    bytes.fill(0xffu);

    laplace_framework_producer_v1 producer{};
    ASSERT_EQ(laplace_canonical_spool_producer(handle.value, &producer),
              LAPLACE_SPOOL_OK);
    auto context = Context();
    CollectingSink collecting{};
    auto sink = Sink(&collecting);
    auto control = ProducerControl();
    laplace_framework_producer_receipt receipt{};
    ASSERT_EQ(laplace_framework_run_producer(
                  &context, &source, &recipe, &producer, &control,
                  &sink, 1u, &receipt),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(collecting.bytes,
              std::vector<std::uint8_t>(expected.begin(), expected.end()));
    EXPECT_TRUE(collecting.sealed);
    EXPECT_FALSE(collecting.aborted);
    EXPECT_TRUE(SameDigest(
        receipt.stream.stream_fingerprint, summary.stream_fingerprint));
    EXPECT_TRUE(SameDigest(
        receipt.completion_fingerprint, summary.spool_fingerprint));

    auto wrong_source = source;
    wrong_source.bytes[0] ^= 0x01u;
    laplace_framework_producer_plan rejected{};
    EXPECT_EQ(producer.prepare(
                  producer.state, &context, &wrong_source, &recipe, &rejected),
              LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);

    laplace_canonical_spool_destroy(&handle.value);
    EXPECT_EQ(handle.value, nullptr);
}

TEST(CanonicalSpool, StreamIdentityIgnoresPartitionsWhileSpoolReceiptBindsThem) {
    TemporaryDirectory directory;
    ASSERT_FALSE(directory.Path().empty());
    laplace_digest256 source{};
    laplace_digest256 recipe{};
    FillDigest(&source, 0x31u);
    FillDigest(&recipe, 0x71u);
    const std::array<std::uint8_t, 10> bytes{{
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u}};
    SpoolHandle first{};
    SpoolHandle repeated{};
    SpoolHandle repartitioned{};
    const auto first_summary = BuildSpool(
        directory.Path(), source, recipe, bytes, false, &first);
    const auto repeated_summary = BuildSpool(
        directory.Path(), source, recipe, bytes, false, &repeated);
    const auto repartitioned_summary = BuildSpool(
        directory.Path(), source, recipe, bytes, true, &repartitioned);

    EXPECT_TRUE(SameDigest(
        first_summary.stream_fingerprint,
        repeated_summary.stream_fingerprint));
    EXPECT_TRUE(SameDigest(
        first_summary.spool_fingerprint,
        repeated_summary.spool_fingerprint));
    EXPECT_TRUE(SameDigest(
        first_summary.producer_fingerprint,
        repeated_summary.producer_fingerprint));
    EXPECT_TRUE(SameDigest(
        first_summary.stream_fingerprint,
        repartitioned_summary.stream_fingerprint));
    EXPECT_FALSE(SameDigest(
        first_summary.spool_fingerprint,
        repartitioned_summary.spool_fingerprint));
    EXPECT_FALSE(SameDigest(
        first_summary.producer_fingerprint,
        repartitioned_summary.producer_fingerprint));
    EXPECT_TRUE(fs::is_empty(directory.Path()));
}

TEST(CanonicalSpool, RejectsOrdinalAndTypeDefectsBeforeWritingAnyBytes) {
    TemporaryDirectory directory;
    ASSERT_FALSE(directory.Path().empty());
    laplace_digest256 source{};
    laplace_digest256 recipe{};
    FillDigest(&source, 0x41u);
    FillDigest(&recipe, 0x81u);
    SpoolHandle handle{};
    ASSERT_EQ(laplace_canonical_spool_create(
                  directory.Path().c_str(), LAPLACE_ISA_VALUE_U32_VECTOR,
                  &source, &recipe, &handle.value),
              LAPLACE_SPOOL_OK);
    const std::array<std::uint8_t, 4> bytes{{1u, 2u, 3u, 4u}};
    const auto ordinal_gap = Batch(bytes.data(), bytes.size(), 1u, 1u);
    EXPECT_EQ(laplace_canonical_spool_append(handle.value, &ordinal_gap),
              LAPLACE_SPOOL_BATCH_INVALID);
    auto wrong_type = Batch(bytes.data(), bytes.size(), 1u, 0u);
    wrong_type.record_type = LAPLACE_ISA_VALUE_ID128_VECTOR;
    EXPECT_EQ(laplace_canonical_spool_append(handle.value, &wrong_type),
              LAPLACE_SPOOL_BATCH_INVALID);

    const auto valid = Batch(bytes.data(), bytes.size(), 1u, 0u);
    ASSERT_EQ(laplace_canonical_spool_append(handle.value, &valid),
              LAPLACE_SPOOL_OK);
    laplace_canonical_spool_summary summary{};
    ASSERT_EQ(laplace_canonical_spool_seal(handle.value, &summary),
              LAPLACE_SPOOL_OK);
    EXPECT_EQ(summary.batch_count, 1u);
    EXPECT_EQ(summary.total_records, 1u);
    EXPECT_EQ(summary.total_bytes, bytes.size());
    EXPECT_TRUE(fs::is_empty(directory.Path()));
}
