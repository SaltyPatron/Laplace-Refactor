#include "laplace/isa.h"
#include "context_fixture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

laplace_isa_receipt Execute(
    const laplace_framework_context* context,
    laplace_id128* output,
    laplace_isa_status* status) {
    std::uint32_t position = UINT32_C(0x41);
    std::array<laplace_isa_value_view, 2> values{{
        {&position, 1u, 1u, static_cast<std::uint32_t>(sizeof(position)),
         LAPLACE_ISA_VALUE_U32_VECTOR, LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {output, 0u, 1u, static_cast<std::uint32_t>(sizeof(*output)),
         LAPLACE_ISA_VALUE_ID128_VECTOR, LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction instruction{
        LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    laplace_isa_program program{
        &instruction,
        values.data(),
        context,
        1u,
        values.size(),
        LAPLACE_ISA_MAJOR,
        LAPLACE_ISA_MINOR,
        LAPLACE_ISA_KNOWN_PROGRAM_FLAGS,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL,
        0u};
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};
    *status = laplace_isa_execute(&program, &receipt, &error);
    return receipt;
}

}  // namespace

int main() {
    const auto context_a = laplace_test_context(0u);
    const auto context_b = laplace_test_context(1u);
    laplace_id128 output_a{};
    laplace_id128 output_b{};
    laplace_id128 output_missing{};
    laplace_isa_status status_a{};
    laplace_isa_status status_b{};
    laplace_isa_status status_missing{};
    const auto receipt_a = Execute(&context_a, &output_a, &status_a);
    const auto receipt_b = Execute(&context_b, &output_b, &status_b);
    (void)Execute(nullptr, &output_missing, &status_missing);

    if (status_a != LAPLACE_ISA_OK || status_b != LAPLACE_ISA_OK ||
        status_missing != LAPLACE_ISA_CONTEXT_INVALID ||
        std::memcmp(receipt_a.context_fingerprint.bytes,
                    receipt_b.context_fingerprint.bytes,
                    sizeof(receipt_a.context_fingerprint.bytes)) == 0 ||
        std::memcmp(receipt_a.program_fingerprint.bytes,
                    receipt_b.program_fingerprint.bytes,
                    sizeof(receipt_a.program_fingerprint.bytes)) == 0 ||
        std::memcmp(receipt_a.receipt_id.bytes,
                    receipt_b.receipt_id.bytes,
                    sizeof(receipt_a.receipt_id.bytes)) == 0) {
        std::fputs("isa-context-binding\n", stderr);
        return 2;
    }
    return 0;
}
