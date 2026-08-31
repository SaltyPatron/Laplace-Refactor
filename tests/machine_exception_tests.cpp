#include "laplace/machine_exception.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

void FillDigest(laplace_digest256* digest, std::uint8_t seed) {
    for (std::size_t index = 0; index < sizeof(digest->bytes); ++index) {
        digest->bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_machine_exception_binding Binding() {
    laplace_machine_exception_binding binding{};
    FillDigest(&binding.program_fingerprint, 0x10u);
    FillDigest(&binding.physical_plan_fingerprint, 0x20u);
    FillDigest(&binding.provider_fingerprint, 0x30u);
    FillDigest(&binding.node_fingerprint, 0x40u);
    FillDigest(&binding.world_scope_fingerprint, 0x50u);
    FillDigest(&binding.transaction_fingerprint, 0x60u);
    FillDigest(&binding.last_valid_receipt_fingerprint, 0x70u);
    FillDigest(&binding.durable_boundary_fingerprint, 0x80u);
    FillDigest(&binding.replay_origin_fingerprint, 0x90u);
    FillDigest(&binding.invalidated_output_fingerprint, 0xa0u);
    binding.presence_mask = LAPLACE_MACHINE_EXCEPTION_BIND_KNOWN_MASK;
    binding.affected_instruction_index = 17u;
    binding.invalidated_output_count = 3u;
    return binding;
}

TEST(MachineExceptionRegistry, GeneratedRegistryPreservesDistinctMachineConditions) {
    ASSERT_EQ(laplace_machine_exception_registry_validate(),
              LAPLACE_MACHINE_EXCEPTION_OK);
    ASSERT_EQ(laplace_machine_exception_descriptor_count(),
              static_cast<std::size_t>(LAPLACE_MACHINE_CONDITION_COUNT));

    const auto* hardware =
        laplace_machine_exception_find(LAPLACE_MACHINE_CONDITION_HARDWARE_FAULT);
    const auto* unknown =
        laplace_machine_exception_find(LAPLACE_MACHINE_CONDITION_UNKNOWN);
    const auto* resource =
        laplace_machine_exception_find(LAPLACE_MACHINE_CONDITION_RESOURCE_EXHAUSTED);
    const auto* deadline =
        laplace_machine_exception_find(LAPLACE_MACHINE_CONDITION_DEADLINE_EXCEEDED);
    const auto* contradiction = laplace_machine_exception_find(
        LAPLACE_MACHINE_CONDITION_SEMANTIC_CONTRADICTION);
    const auto* provider = laplace_machine_exception_find(
        LAPLACE_MACHINE_CONDITION_PROVIDER_UNAVAILABLE);
    const auto* invalid = laplace_machine_exception_find(
        LAPLACE_MACHINE_CONDITION_INVALID_INSTRUCTION);
    const auto* cancelled =
        laplace_machine_exception_find(LAPLACE_MACHINE_CONDITION_CANCELLED);

    ASSERT_NE(hardware, nullptr);
    ASSERT_NE(unknown, nullptr);
    ASSERT_NE(resource, nullptr);
    ASSERT_NE(deadline, nullptr);
    ASSERT_NE(contradiction, nullptr);
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(invalid, nullptr);
    ASSERT_NE(cancelled, nullptr);

    EXPECT_EQ(hardware->condition, LAPLACE_MACHINE_CONDITION_HARDWARE_FAULT);
    EXPECT_EQ(hardware->kind, LAPLACE_MACHINE_KIND_FAULT);
    EXPECT_NE(hardware->condition, unknown->condition);
    EXPECT_NE(resource->condition, deadline->condition);
    EXPECT_NE(contradiction->condition, provider->condition);
    EXPECT_EQ(invalid->kind, LAPLACE_MACHINE_KIND_TRAP);
    EXPECT_EQ(cancelled->kind, LAPLACE_MACHINE_KIND_CANCELLATION);
    EXPECT_EQ(hardware->publication_disposition,
              LAPLACE_MACHINE_PUBLICATION_NONE);
    EXPECT_NE(hardware->capability_flags & LAPLACE_MACHINE_CAPABILITY_REPLAYABLE,
              0u);

    for (std::size_t index = 0;
         index < laplace_machine_exception_descriptor_count(); ++index) {
        EXPECT_EQ(laplace_machine_exception_descriptor_validate(
                      &laplace_machine_exception_descriptors()[index]),
                  LAPLACE_MACHINE_EXCEPTION_OK);
    }

    auto flattened_hardware = *hardware;
    flattened_hardware.condition = LAPLACE_MACHINE_CONDITION_UNKNOWN;
    EXPECT_EQ(laplace_machine_exception_descriptor_validate(&flattened_hardware),
              LAPLACE_MACHINE_EXCEPTION_INVALID_ARGUMENT);

    auto capability_drift = *hardware;
    capability_drift.capability_flags = unknown->capability_flags;
    EXPECT_EQ(laplace_machine_exception_descriptor_validate(&capability_drift),
              LAPLACE_MACHINE_EXCEPTION_INVALID_ARGUMENT);
}

TEST(MachineExceptionRegistry, PrioritySelectionRetainsEveryObservedConditionAndBinding) {
    const std::array<std::uint32_t, 3> observed{{
        LAPLACE_MACHINE_CONDITION_DURABILITY_FAULT,
        LAPLACE_MACHINE_CONDITION_CANCELLED,
        LAPLACE_MACHINE_CONDITION_INVALID_INSTRUCTION}};
    const auto binding = Binding();
    laplace_machine_exception_receipt receipt{};

    ASSERT_EQ(laplace_machine_exception_classify(
                  observed.data(), observed.size(), &binding, &receipt),
              LAPLACE_MACHINE_EXCEPTION_OK);
    EXPECT_EQ(receipt.selected.condition,
              LAPLACE_MACHINE_CONDITION_INVALID_INSTRUCTION);
    EXPECT_NE(receipt.observed_condition_mask &
                  (UINT64_C(1) << LAPLACE_MACHINE_CONDITION_DURABILITY_FAULT),
              0u);
    EXPECT_NE(receipt.observed_condition_mask &
                  (UINT64_C(1) << LAPLACE_MACHINE_CONDITION_CANCELLED),
              0u);
    EXPECT_NE(receipt.observed_condition_mask &
                  (UINT64_C(1) << LAPLACE_MACHINE_CONDITION_INVALID_INSTRUCTION),
              0u);
    EXPECT_EQ(receipt.binding.presence_mask, binding.presence_mask);
    EXPECT_EQ(receipt.binding.affected_instruction_index,
              binding.affected_instruction_index);
    EXPECT_EQ(receipt.binding.invalidated_output_count,
              binding.invalidated_output_count);
    EXPECT_EQ(std::memcmp(receipt.binding.durable_boundary_fingerprint.bytes,
                          binding.durable_boundary_fingerprint.bytes,
                          sizeof(binding.durable_boundary_fingerprint.bytes)),
              0);
}

TEST(MachineExceptionReceipt, ReplayBoundaryAndFailedPublicationRemainExplicit) {
    const std::uint32_t observed = LAPLACE_MACHINE_CONDITION_HARDWARE_FAULT;
    const auto binding = Binding();
    laplace_machine_exception_receipt receipt{};
    laplace_digest256 obligations{};
    laplace_digest256 continuation{};
    laplace_machine_why_not why_not{};
    FillDigest(&obligations, 0xb0u);
    FillDigest(&continuation, 0xc0u);

    ASSERT_EQ(laplace_machine_exception_classify(
                  &observed, 1u, &binding, &receipt),
              LAPLACE_MACHINE_EXCEPTION_OK);
    EXPECT_NE(receipt.selected.capability_flags &
                  LAPLACE_MACHINE_CAPABILITY_REROUTABLE,
              0u);
    EXPECT_NE(receipt.selected.capability_flags &
                  LAPLACE_MACHINE_CAPABILITY_REPLAYABLE,
              0u);
    EXPECT_EQ(receipt.selected.recovery_disposition,
              LAPLACE_MACHINE_RECOVERY_REROUTE_REPLAY);
    EXPECT_EQ(receipt.selected.publication_disposition,
              LAPLACE_MACHINE_PUBLICATION_NONE);
    EXPECT_NE(receipt.binding.presence_mask &
                  LAPLACE_MACHINE_EXCEPTION_BIND_DURABLE_BOUNDARY,
              0u);
    EXPECT_NE(receipt.binding.presence_mask &
                  LAPLACE_MACHINE_EXCEPTION_BIND_REPLAY_ORIGIN,
              0u);
    EXPECT_NE(receipt.binding.presence_mask &
                  LAPLACE_MACHINE_EXCEPTION_BIND_INVALIDATED_OUTPUT,
              0u);

    ASSERT_EQ(laplace_machine_exception_why_not(
                  &receipt, 42u, 2u, &obligations, &continuation, &why_not),
              LAPLACE_MACHINE_EXCEPTION_OK);
    EXPECT_EQ(why_not.limiting_condition,
              LAPLACE_MACHINE_CONDITION_HARDWARE_FAULT);
    EXPECT_EQ(why_not.recovery_disposition,
              LAPLACE_MACHINE_RECOVERY_REROUTE_REPLAY);
    EXPECT_EQ(why_not.completed_work_units, 42u);
    EXPECT_EQ(why_not.open_obligation_count, 2u);
    EXPECT_EQ(std::memcmp(why_not.open_obligations_fingerprint.bytes,
                          obligations.bytes, sizeof(obligations.bytes)),
              0);
    EXPECT_EQ(std::memcmp(why_not.continuation_condition_fingerprint.bytes,
                          continuation.bytes, sizeof(continuation.bytes)),
              0);
}

TEST(MachineExceptionRegistry, TypedLimitsCannotImpersonateSemanticSuccess) {
    const auto* partial =
        laplace_machine_exception_find(LAPLACE_MACHINE_CONDITION_PARTIAL_RESULT);
    const auto* upper = laplace_machine_exception_find(
        LAPLACE_MACHINE_CONDITION_KNOWN_UPPER_BOUND);
    const auto* contradiction = laplace_machine_exception_find(
        LAPLACE_MACHINE_CONDITION_SEMANTIC_CONTRADICTION);
    ASSERT_NE(partial, nullptr);
    ASSERT_NE(upper, nullptr);
    ASSERT_NE(contradiction, nullptr);
    EXPECT_EQ(partial->publication_disposition,
              LAPLACE_MACHINE_PUBLICATION_PARTIAL);
    EXPECT_EQ(upper->publication_disposition,
              LAPLACE_MACHINE_PUBLICATION_UPPER_BOUND);
    EXPECT_EQ(contradiction->publication_disposition,
              LAPLACE_MACHINE_PUBLICATION_NONE);
    EXPECT_EQ(partial->kind, LAPLACE_MACHINE_KIND_TERMINAL_DISPOSITION);
    EXPECT_EQ(upper->kind, LAPLACE_MACHINE_KIND_TERMINAL_DISPOSITION);
    EXPECT_EQ(contradiction->kind,
              LAPLACE_MACHINE_KIND_TERMINAL_DISPOSITION);
}

}  // namespace
