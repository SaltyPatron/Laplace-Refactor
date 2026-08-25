#include "laplace/framework.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

void FillDigest(laplace_digest256* digest, std::uint8_t seed) {
    for (std::size_t index = 0; index < sizeof(digest->bytes); ++index) {
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
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_NUMERIC) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PACKAGE);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_IDENTITY], 0x10u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY], 0x30u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC], 0x50u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_PACKAGE], 0x70u);
    FillDigest(&context.authority_fingerprint, 0x90u);
    context.resource_grant = laplace_execution_grant{1u << 20u, 4u, 1u};
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

laplace_framework_canonical_stream Stream(
    const laplace_framework_canonical_batch* batches,
    std::size_t batch_count,
    std::uint8_t source_seed = 0xc0u,
    std::uint8_t recipe_seed = 0xe0u) {
    laplace_framework_canonical_stream stream{};
    stream.batches = batches;
    stream.batch_count = static_cast<std::uint64_t>(batch_count);
    stream.flags = LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS;
    FillDigest(&stream.source_fingerprint, source_seed);
    FillDigest(&stream.recipe_fingerprint, recipe_seed);
    return stream;
}

struct MemorySink final {
    std::vector<std::uint8_t> bytes;
    std::uint64_t expected_records{};
    std::uint64_t expected_bytes{};
    std::uint32_t record_type{};
    bool begun{};
    bool sealed{};
    bool aborted{};
    bool fail_seal{};
};

laplace_framework_status BeginSink(
    void* state,
    const laplace_framework_context*,
    std::uint32_t record_type,
    std::uint64_t total_records,
    std::uint64_t total_bytes) {
    auto* sink = static_cast<MemorySink*>(state);
    sink->bytes.clear();
    sink->expected_records = total_records;
    sink->expected_bytes = total_bytes;
    sink->record_type = record_type;
    sink->begun = true;
    sink->sealed = false;
    sink->aborted = false;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status StageSink(
    void* state,
    const laplace_framework_canonical_batch* batch) {
    auto* sink = static_cast<MemorySink*>(state);
    if (!sink->begun || sink->sealed || batch->record_type != sink->record_type) {
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
    void* state,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint) {
    auto* sink = static_cast<MemorySink*>(state);
    if (sink->fail_seal || !sink->begun ||
        sink->bytes.size() != sink->expected_bytes) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    *artifact_fingerprint = *stream_fingerprint;
    sink->sealed = true;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortSink(void* state) {
    auto* sink = static_cast<MemorySink*>(state);
    sink->bytes.clear();
    sink->sealed = false;
    sink->aborted = true;
}

laplace_framework_sink_v1 Sink(MemorySink* state) {
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

TEST(FrameworkRegistry, GeneratedDescriptorsMatchIsaContract) {
    ASSERT_EQ(laplace_framework_registry_validate(), LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_framework_operation_count(), LAPLACE_ISA_OPERATION_COUNT);
    const auto* identity = laplace_framework_operation_find(
        LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH);
    ASSERT_NE(identity, nullptr);
    EXPECT_EQ(identity->input_type, LAPLACE_ISA_VALUE_U32_VECTOR);
    EXPECT_EQ(identity->output_type, LAPLACE_ISA_VALUE_ID128_VECTOR);
    EXPECT_EQ(identity->instruction_version,
              LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH);
    EXPECT_EQ(laplace_framework_operation_find(UINT32_MAX), nullptr);
}

TEST(FrameworkContext, AllBoundFactsAffectFingerprint) {
    auto context = Context();
    laplace_digest256 initial{};
    laplace_digest256 changed{};
    ASSERT_EQ(laplace_framework_context_fingerprint(&context, &initial),
              LAPLACE_FRAMEWORK_OK);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC].bytes[4] ^= 0x80u;
    ASSERT_EQ(laplace_framework_context_fingerprint(&context, &changed),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_NE(std::memcmp(initial.bytes, changed.bytes, sizeof(initial.bytes)), 0);

    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_SOURCE], 0xb0u);
    EXPECT_EQ(laplace_framework_context_validate(&context),
              LAPLACE_FRAMEWORK_CONTEXT_INVALID);
}

TEST(CanonicalStream, PartitionBoundariesDoNotChangeFingerprint) {
    const std::array<std::uint8_t, 6> bytes{{1u, 2u, 3u, 4u, 5u, 6u}};
    const auto whole = Batch(bytes.data(), bytes.size(), 3u, 0u);
    const std::array<laplace_framework_canonical_batch, 2> partitions{{
        Batch(bytes.data(), 2u, 1u, 0u),
        Batch(bytes.data() + 2u, 4u, 2u, 1u)}};
    laplace_digest256 whole_digest{};
    laplace_digest256 partition_digest{};
    std::uint32_t whole_type = 0;
    std::uint32_t partition_type = 0;
    std::uint64_t whole_records = 0;
    std::uint64_t partition_records = 0;
    std::uint64_t whole_bytes = 0;
    std::uint64_t partition_bytes = 0;
    ASSERT_EQ(laplace_framework_canonical_stream_fingerprint(
                  &whole, 1u, &whole_digest, &whole_type,
                  &whole_records, &whole_bytes),
              LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_framework_canonical_stream_fingerprint(
                  partitions.data(), partitions.size(), &partition_digest,
                  &partition_type, &partition_records, &partition_bytes),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(std::memcmp(
                  whole_digest.bytes, partition_digest.bytes,
                  sizeof(whole_digest.bytes)),
              0);
    EXPECT_EQ(whole_type, partition_type);
    EXPECT_EQ(whole_records, partition_records);
    EXPECT_EQ(whole_bytes, partition_bytes);
}

TEST(CanonicalStream, FansOneStreamOutToEverySink) {
    auto context = Context();
    const std::array<std::uint8_t, 5> bytes{{9u, 8u, 7u, 6u, 5u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 2u, 0u);
    const auto stream = Stream(&batch, 1u);
    MemorySink left{};
    MemorySink right{};
    std::array<laplace_framework_sink_v1, 2> sinks{{Sink(&left), Sink(&right)}};
    laplace_framework_stream_receipt receipt{};
    ASSERT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream, sinks.data(), sinks.size(), &receipt),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_TRUE(left.sealed);
    EXPECT_TRUE(right.sealed);
    EXPECT_EQ(left.bytes, right.bytes);
    EXPECT_EQ(left.bytes, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    EXPECT_EQ(receipt.total_records, 2u);
    EXPECT_EQ(receipt.total_bytes, bytes.size());
    EXPECT_EQ(receipt.sink_count, sinks.size());
    EXPECT_EQ(receipt.effect_disposition, LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT);
}

TEST(CanonicalStream, SourceAndRecipeAreMandatoryReceiptBindings) {
    auto context = Context();
    const std::array<std::uint8_t, 2> bytes{{7u, 9u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 1u, 0u);
    auto stream_a = Stream(&batch, 1u, 0xc0u, 0xe0u);
    auto stream_b = Stream(&batch, 1u, 0xc1u, 0xe0u);
    auto stream_c = Stream(&batch, 1u, 0xc0u, 0xe1u);
    MemorySink memory_a{};
    MemorySink memory_b{};
    MemorySink memory_c{};
    auto sink_a = Sink(&memory_a);
    auto sink_b = Sink(&memory_b);
    auto sink_c = Sink(&memory_c);
    laplace_framework_stream_receipt receipt_a{};
    laplace_framework_stream_receipt receipt_b{};
    laplace_framework_stream_receipt receipt_c{};
    ASSERT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream_a, &sink_a, 1u, &receipt_a),
              LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream_b, &sink_b, 1u, &receipt_b),
              LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream_c, &sink_c, 1u, &receipt_c),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(std::memcmp(receipt_a.stream_fingerprint.bytes,
                          receipt_b.stream_fingerprint.bytes,
                          sizeof(receipt_a.stream_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.source_fingerprint.bytes,
                          receipt_b.source_fingerprint.bytes,
                          sizeof(receipt_a.source_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          receipt_b.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.recipe_fingerprint.bytes,
                          receipt_c.recipe_fingerprint.bytes,
                          sizeof(receipt_a.recipe_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          receipt_c.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);

    std::memset(&stream_a.source_fingerprint, 0, sizeof(stream_a.source_fingerprint));
    MemorySink rejected{};
    auto rejected_sink = Sink(&rejected);
    laplace_framework_stream_receipt rejected_receipt{};
    EXPECT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream_a, &rejected_sink, 1u, &rejected_receipt),
              LAPLACE_FRAMEWORK_STREAM_INVALID);
    EXPECT_FALSE(rejected.begun);
    EXPECT_EQ(rejected_receipt.effect_disposition, LAPLACE_FRAMEWORK_EFFECT_NONE);
}

TEST(CanonicalStream, SealFailureAbortsEveryStagedSink) {
    auto context = Context();
    const std::array<std::uint8_t, 3> bytes{{3u, 2u, 1u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 1u, 0u);
    const auto stream = Stream(&batch, 1u);
    MemorySink left{};
    MemorySink right{};
    right.fail_seal = true;
    std::array<laplace_framework_sink_v1, 2> sinks{{Sink(&left), Sink(&right)}};
    laplace_framework_stream_receipt receipt{};
    EXPECT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream, sinks.data(), sinks.size(), &receipt),
              LAPLACE_FRAMEWORK_SINK_SEAL_FAILED);
    EXPECT_TRUE(left.aborted);
    EXPECT_TRUE(right.aborted);
    EXPECT_FALSE(left.sealed);
    EXPECT_FALSE(right.sealed);
    EXPECT_TRUE(left.bytes.empty());
    EXPECT_TRUE(right.bytes.empty());
    EXPECT_EQ(receipt.failed_sink_index, 1u);
}

TEST(CanonicalStream, RejectsOrdinalGapsBeforeOpeningSinks) {
    auto context = Context();
    const std::array<std::uint8_t, 2> bytes{{1u, 2u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 1u, 4u);
    const auto stream = Stream(&batch, 1u);
    MemorySink memory{};
    auto sink = Sink(&memory);
    laplace_framework_stream_receipt receipt{};
    EXPECT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream, &sink, 1u, &receipt),
              LAPLACE_FRAMEWORK_STREAM_INVALID);
    EXPECT_FALSE(memory.begun);
}

}  // namespace
