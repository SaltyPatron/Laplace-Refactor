#include "laplace/isa.h"
#include "laplace/universal_ast.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "context_fixture.h"

namespace {

constexpr std::uint64_t Base = UINT64_C(0x4300000000000000);
constexpr std::size_t ReceiptWords = 66u;

void Fill(laplace_id128& value, const std::uint8_t seed) {
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_highway_key Key(
    const std::uint32_t kind,
    const std::uint8_t seed) {
    laplace_highway_key key{};
    key.kind = kind;
    Fill(key.authority, seed);
    Fill(key.release, static_cast<std::uint8_t>(seed + 0x10u));
    Fill(key.name_space, static_cast<std::uint8_t>(seed + 0x20u));
    Fill(key.local_identifier, static_cast<std::uint8_t>(seed + 0x30u));
    key.version = 1u;
    return key;
}

laplace_decomposition_status Applicable(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *applicable = span->depth == 0u ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status Apply(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span*,
    laplace_decomposition_emit_event_fn emit,
    void* emit_state) {
    if (emit == nullptr) return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    const std::array<laplace_decomposition_event, 2> events{{
        {0u, 2u, UINT64_MAX,
         Base | 1u, Base | 11u, 0u, 0u,
         LAPLACE_DECOMPOSITION_SPAN_TEXT,
         LAPLACE_DECOMPOSITION_SYNTAX_NAMED},
        {0u, 1u, 0u,
         Base | 2u, Base | 12u, 7u, 1u,
         LAPLACE_DECOMPOSITION_SPAN_TEXT,
         LAPLACE_DECOMPOSITION_SYNTAX_NAMED}
    }};
    for (const auto& event : events) {
        if (emit(emit_state, &event) != 0) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }
    }
    return LAPLACE_DECOMPOSITION_OK;
}

struct CompiledFixture {
    std::array<std::uint8_t, 2> bytes{{'x', 'y'}};
    laplace_decomposition_result* decomposition{};
    laplace_grammar_registry* registry{};
    laplace_ast_recipe* recipe{};
    laplace_universal_ast_plan* plan{};
    std::vector<std::uint32_t> packet;
    laplace_universal_ast_packet_receipt direct_receipt{};

    CompiledFixture() {
        laplace_decomposition_provider_v1 provider{};
        provider.provider_fingerprint = Digest(0x11u);
        provider.applicable = Applicable;
        provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
        provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
        provider.apply_events = Apply;

        static constexpr char Media[] = "text/plain";
        laplace_decomposition_content content{};
        content.bytes = bytes.data();
        content.byte_count = bytes.size();
        content.media_type = Media;
        content.media_type_byte_count = sizeof(Media) - 1u;
        laplace_decomposition_input decomposition_input{};
        decomposition_input.content = content;
        decomposition_input.providers = &provider;
        decomposition_input.provider_count = 1u;
        decomposition_input.maximum_spans = 8u;
        decomposition_input.maximum_depth = 8u;
        EXPECT_EQ(laplace_decomposition_run(
                      &decomposition_input, &decomposition),
                  LAPLACE_DECOMPOSITION_OK);

        laplace_grammar_provider_declaration grammar{};
        grammar.grammar_coordinate =
            Key(LAPLACE_HIGHWAY_KIND_GRAMMAR_SYMBOL, 0x21u);
        grammar.provider_fingerprint = provider.provider_fingerprint;
        grammar.release_fingerprint = Digest(0x31u);
        grammar.conformance_fingerprint = Digest(0x41u);
        grammar.kind_base = Base;
        grammar.kind_count = 256u;
        EXPECT_EQ(laplace_grammar_registry_create(&grammar, 1u, &registry),
                  LAPLACE_UNIVERSAL_AST_OK);
        laplace_digest256 registry_fingerprint{};
        EXPECT_EQ(laplace_grammar_registry_fingerprint(
                      registry, &registry_fingerprint),
                  LAPLACE_UNIVERSAL_AST_OK);

        std::array<laplace_ast_recipe_rule, 2> rules{};
        rules[0].provider_fingerprint = provider.provider_fingerprint;
        rules[0].kind = Base | 1u;
        rules[0].grammar_kind = Base | 11u;
        rules[0].field_kind = 0u;
        rules[0].required_syntax_flags = LAPLACE_DECOMPOSITION_SYNTAX_NAMED;
        rules[0].disposition = LAPLACE_AST_RULE_PRESERVE;
        rules[0].role_coordinate = Key(LAPLACE_HIGHWAY_KIND_AST_ROLE, 0x51u);
        rules[1] = rules[0];
        rules[1].kind = Base | 2u;
        rules[1].grammar_kind = Base | 12u;
        rules[1].field_kind = 7u;
        rules[1].role_coordinate = Key(LAPLACE_HIGHWAY_KIND_AST_ROLE, 0x61u);

        laplace_ast_recipe_declaration recipe_declaration{};
        recipe_declaration.recipe_coordinate =
            Key(LAPLACE_HIGHWAY_KIND_RECIPE, 0x71u);
        recipe_declaration.grammar_registry_fingerprint = registry_fingerprint;
        recipe_declaration.conformance_fingerprint = Digest(0x81u);
        EXPECT_EQ(laplace_ast_recipe_create(
                      registry, &recipe_declaration,
                      rules.data(), rules.size(), &recipe),
                  LAPLACE_UNIVERSAL_AST_OK);

        laplace_universal_ast_compile_input compile{};
        compile.grammar_registry = registry;
        compile.recipe = recipe;
        compile.content = &content;
        compile.decomposition = decomposition;
        compile.geometry_epoch = Digest(0x91u);
        compile.occurrence_context_fingerprint = Digest(0xa1u);
        compile.source_ordinal_base = 1u;
        EXPECT_EQ(laplace_universal_ast_plan_create(&compile, &plan),
                  LAPLACE_UNIVERSAL_AST_OK);

        std::size_t packet_words = 0u;
        EXPECT_EQ(laplace_universal_ast_plan_packet_measure(plan, &packet_words),
                  LAPLACE_UNIVERSAL_AST_OK);
        packet.resize(packet_words);
        std::size_t written = 0u;
        EXPECT_EQ(laplace_universal_ast_plan_packet_encode(
                      plan, packet.data(), packet.size(), &written),
                  LAPLACE_UNIVERSAL_AST_OK);
        EXPECT_EQ(written, packet.size());
        EXPECT_EQ(laplace_universal_ast_packet_validate_words(
                      packet.data(), packet.size(), &direct_receipt),
                  LAPLACE_UNIVERSAL_AST_OK);
    }

    ~CompiledFixture() {
        laplace_universal_ast_plan_destroy(&plan);
        laplace_ast_recipe_destroy(&recipe);
        laplace_grammar_registry_destroy(&registry);
        laplace_decomposition_result_destroy(&decomposition);
    }
};

laplace_digest256 DecodeDigest(const std::uint32_t* words) {
    laplace_digest256 value{};
    for (std::size_t word = 0u; word < 8u; ++word) {
        const std::uint32_t bits = words[word];
        const std::size_t offset = word * 4u;
        value.bytes[offset] = static_cast<std::uint8_t>(bits);
        value.bytes[offset + 1u] = static_cast<std::uint8_t>(bits >> 8u);
        value.bytes[offset + 2u] = static_cast<std::uint8_t>(bits >> 16u);
        value.bytes[offset + 3u] = static_cast<std::uint8_t>(bits >> 24u);
    }
    return value;
}

struct IsaRun {
    laplace_framework_context context{laplace_test_context(0u)};
    std::array<std::uint32_t, ReceiptWords> output{};
    std::array<laplace_isa_value_view, 2> values{};
    laplace_isa_instruction instruction{};
    laplace_isa_program program{};

    explicit IsaRun(std::vector<std::uint32_t>& packet) {
        values[0].data = packet.data();
        values[0].count = packet.size();
        values[0].capacity = packet.size();
        values[0].stride_bytes = sizeof(std::uint32_t);
        values[0].type = LAPLACE_ISA_VALUE_U32_VECTOR;
        values[1].data = output.data();
        values[1].capacity = output.size();
        values[1].stride_bytes = sizeof(std::uint32_t);
        values[1].type = LAPLACE_ISA_VALUE_U32_VECTOR;
        instruction.opcode = LAPLACE_ISA_OPCODE_UNIVERSAL_AST_APPLY_PACKET;
        instruction.input_value = 0u;
        instruction.output_value = 1u;
        instruction.version = LAPLACE_ISA_INSTRUCTION_VERSION_UNIVERSAL_AST_APPLY_PACKET;
        program.instructions = &instruction;
        program.values = values.data();
        program.context = &context;
        program.instruction_count = 1u;
        program.value_count = values.size();
        program.major = LAPLACE_ISA_MAJOR;
        program.minor = LAPLACE_ISA_MINOR;
        program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    }
};

TEST(UniversalAstIsa, CompiledPacketReplaysThroughSameIsaReceiptBoundary) {
    CompiledFixture fixture;
    IsaRun run(fixture.packet);
    laplace_isa_error error{};
    ASSERT_EQ(laplace_isa_validate(&run.program, &error), LAPLACE_ISA_OK);
    laplace_isa_receipt isa_receipt{};
    ASSERT_EQ(laplace_isa_execute(&run.program, &isa_receipt, &error),
              LAPLACE_ISA_OK);
    ASSERT_EQ(run.values[1].count, ReceiptWords);
    const auto receipt_id = DecodeDigest(run.output.data());
    const auto plan_id = DecodeDigest(run.output.data() + 24u);
    const auto witness_id = DecodeDigest(run.output.data() + 32u);
    EXPECT_EQ(std::memcmp(
                  receipt_id.bytes, fixture.direct_receipt.receipt_id.bytes,
                  sizeof(receipt_id.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  plan_id.bytes, fixture.direct_receipt.plan_fingerprint.bytes,
                  sizeof(plan_id.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  witness_id.bytes, fixture.direct_receipt.witness_fingerprint.bytes,
                  sizeof(witness_id.bytes)),
              0);
    EXPECT_EQ(isa_receipt.executed_instruction_count, 1u);
    EXPECT_EQ(isa_receipt.minor, LAPLACE_ISA_MINOR);
}

TEST(UniversalAstIsa, PacketCorruptionCannotReachSuccessfulIsaReceipt) {
    CompiledFixture fixture;
    ASSERT_GT(fixture.packet.size(), 70u);
    fixture.packet[70u] ^= UINT32_C(0x00010000);
    IsaRun run(fixture.packet);
    laplace_isa_error error{};
    ASSERT_EQ(laplace_isa_validate(&run.program, &error), LAPLACE_ISA_OK);
    laplace_isa_receipt receipt{};
    EXPECT_EQ(laplace_isa_execute(&run.program, &receipt, &error),
              LAPLACE_ISA_INPUT_OUT_OF_RANGE);
    EXPECT_EQ(run.values[1].count, 0u);
}

TEST(UniversalAstIsa, OutputCapacityAndIntroducedMinorFailBeforeExecution) {
    CompiledFixture fixture;
    IsaRun run(fixture.packet);
    laplace_isa_error error{};
    run.values[1].capacity = ReceiptWords - 1u;
    EXPECT_EQ(laplace_isa_validate(&run.program, &error),
              LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT);
    run.values[1].capacity = ReceiptWords;
    run.program.minor = 13u;
    EXPECT_EQ(laplace_isa_validate(&run.program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
}

}  // namespace