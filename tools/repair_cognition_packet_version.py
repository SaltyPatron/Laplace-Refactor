#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


core = "engine/src/cognition_packet.cpp"
compile = "engine/src/cognition_packet_compile.cpp"

replace_once(
    core,
    """constexpr std::uint32_t PacketVersion = 1U;\nconstexpr std::uint32_t PacketFlags = 0U;\n""",
    """constexpr std::uint32_t PacketVersionV1 = 1U;\nconstexpr std::uint32_t PacketVersionStanding = 2U;\nconstexpr std::uint32_t PacketFlags = 0U;\n\nbool PacketVersionValid(const std::uint32_t version) {\n    return version == PacketVersionV1 || version == PacketVersionStanding;\n}\n""",
)
replace_once(
    core,
    """    std::vector<laplace_cognition_operator_constraint> constraints;\n    std::vector<double> initial_state;\n};\n""",
    """    std::vector<laplace_cognition_operator_constraint> constraints;\n    std::vector<double> initial_state;\n    std::uint32_t packet_version{};\n};\n""",
)
replace_once(
    core,
    """bool ReadConstraint(\n    Reader* const reader,\n    laplace_cognition_operator_constraint* const constraint) {\n""",
    """bool ReadConstraint(\n    Reader* const reader,\n    laplace_cognition_operator_constraint* const constraint,\n    const std::uint32_t packet_version) {\n""",
)
replace_once(
    core,
    """    if (!base) return false;\n    return constraint->source_class != LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING ||\n        ReadStandingState(reader, &constraint->standing);\n}\n""",
    """    if (!base) return false;\n    if (constraint->source_class != LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING) {\n        return true;\n    }\n    return packet_version >= PacketVersionStanding &&\n        ReadStandingState(reader, &constraint->standing);\n}\n""",
)
replace_once(
    core,
    """        !reader.U32(&version) || version != PacketVersion ||\n""",
    """        !reader.U32(&version) || !PacketVersionValid(version) ||\n""",
)
replace_once(
    core,
    """        if (!ReadConstraint(&reader, &constraint)) {\n""",
    """        if (!ReadConstraint(&reader, &constraint, version)) {\n""",
)
replace_once(
    core,
    """    decoded->solver_program.operator_id = laplace_digest256{};\n    return LAPLACE_COGNITION_PACKET_OK;\n}\n""",
    """    decoded->solver_program.operator_id = laplace_digest256{};\n    decoded->packet_version = version;\n    return LAPLACE_COGNITION_PACKET_OK;\n}\n""",
)
replace_once(
    core,
    """bool ResultSize(const std::size_t solution_count, std::size_t* const result_bytes) {\n    constexpr std::size_t HeaderBytes = 8U + 4U + 4U + 8U;\n    constexpr std::size_t OperatorReceiptBytes =\n        5U * 32U + 8U * 8U + 4U * 4U;\n    constexpr std::size_t SolverReceiptBytes =\n        8U * 32U + 2U * 8U + 4U * 8U + 5U * 4U;\n    constexpr std::size_t FixedBytes =\n        HeaderBytes + OperatorReceiptBytes + SolverReceiptBytes;\n    if (result_bytes == nullptr ||\n        solution_count > (std::numeric_limits<std::size_t>::max() - FixedBytes) / sizeof(double)) {\n        return false;\n    }\n    *result_bytes = FixedBytes + solution_count * sizeof(double);\n    return true;\n}\n""",
    """bool ResultSize(\n    const std::size_t solution_count,\n    const std::uint32_t packet_version,\n    std::size_t* const result_bytes) {\n    constexpr std::size_t HeaderBytes = 8U + 4U + 4U + 8U;\n    constexpr std::size_t OperatorReceiptBytesV1 =\n        5U * 32U + 7U * 8U + 4U * 4U;\n    constexpr std::size_t OperatorReceiptBytesStanding =\n        5U * 32U + 8U * 8U + 4U * 4U;\n    constexpr std::size_t SolverReceiptBytes =\n        8U * 32U + 2U * 8U + 4U * 8U + 5U * 4U;\n    if (result_bytes == nullptr || !PacketVersionValid(packet_version)) return false;\n    const std::size_t operator_receipt_bytes =\n        packet_version >= PacketVersionStanding\n            ? OperatorReceiptBytesStanding\n            : OperatorReceiptBytesV1;\n    const std::size_t fixed_bytes =\n        HeaderBytes + operator_receipt_bytes + SolverReceiptBytes;\n    if (solution_count >\n        (std::numeric_limits<std::size_t>::max() - fixed_bytes) / sizeof(double)) {\n        return false;\n    }\n    *result_bytes = fixed_bytes + solution_count * sizeof(double);\n    return true;\n}\n""",
)
replace_once(
    core,
    """bool WriteOperatorReceipt(\n    Writer* const writer,\n    const laplace_cognition_operator_receipt& receipt) {\n    return writer != nullptr &&\n        writer->Digest(receipt.receipt_id) &&\n        writer->Digest(receipt.operator_id) &&\n        writer->Digest(receipt.program_fingerprint) &&\n        writer->Digest(receipt.field_set_fingerprint) &&\n        writer->Digest(receipt.constraint_set_fingerprint) &&\n        writer->U64(receipt.field_count) &&\n        writer->U64(receipt.input_constraint_count) &&\n        writer->U64(receipt.selected_constraint_count) &&\n        writer->U64(receipt.deduplicated_dependent_count) &&\n        writer->U64(receipt.physicality_constraint_count) &&\n        writer->U64(receipt.testimony_constraint_count) &&\n        writer->U64(receipt.derived_constraint_count) &&\n        writer->U64(receipt.standing_constraint_count) &&\n        writer->U32(receipt.relation_plane_count) &&\n        writer->U32(receipt.status) &&\n        writer->U32(receipt.version) &&\n        writer->U32(receipt.flags);\n}\n""",
    """bool WriteOperatorReceipt(\n    Writer* const writer,\n    const laplace_cognition_operator_receipt& receipt,\n    const std::uint32_t packet_version) {\n    if (writer == nullptr || !PacketVersionValid(packet_version) ||\n        !writer->Digest(receipt.receipt_id) ||\n        !writer->Digest(receipt.operator_id) ||\n        !writer->Digest(receipt.program_fingerprint) ||\n        !writer->Digest(receipt.field_set_fingerprint) ||\n        !writer->Digest(receipt.constraint_set_fingerprint) ||\n        !writer->U64(receipt.field_count) ||\n        !writer->U64(receipt.input_constraint_count) ||\n        !writer->U64(receipt.selected_constraint_count) ||\n        !writer->U64(receipt.deduplicated_dependent_count) ||\n        !writer->U64(receipt.physicality_constraint_count) ||\n        !writer->U64(receipt.testimony_constraint_count) ||\n        !writer->U64(receipt.derived_constraint_count)) {\n        return false;\n    }\n    if (packet_version >= PacketVersionStanding &&\n        !writer->U64(receipt.standing_constraint_count)) {\n        return false;\n    }\n    return writer->U32(receipt.relation_plane_count) &&\n        writer->U32(receipt.status) &&\n        writer->U32(receipt.version) &&\n        writer->U32(receipt.flags);\n}\n""",
)
replace_once(
    core,
    """    if (!ResultSize(decoded.fields.size(), &required)) {\n""",
    """    if (!ResultSize(decoded.fields.size(), decoded.packet_version, &required)) {\n""",
)
replace_once(
    core,
    """        !writer.U32(PacketVersion) || !writer.U32(PacketFlags) ||\n""",
    """        !writer.U32(decoded.packet_version) || !writer.U32(PacketFlags) ||\n""",
)
replace_once(
    core,
    """        !WriteOperatorReceipt(&writer, result.operator_receipt) ||\n""",
    """        !WriteOperatorReceipt(\n            &writer, result.operator_receipt, decoded.packet_version) ||\n""",
)
replace_once(
    core,
    """        if (!ResultSize(decoded.fields.size(), required_result_bytes)) {\n""",
    """        if (!ResultSize(\n                decoded.fields.size(), decoded.packet_version, required_result_bytes)) {\n""",
)

replace_once(
    compile,
    """constexpr std::uint32_t PacketVersion = 1U;\nconstexpr std::uint32_t PacketFlags = 0U;\nconstexpr std::size_t RequestFixedBytes = 344U;\nconstexpr std::size_t FieldBytes = 160U;\nconstexpr std::size_t ConstraintBytes = 264U;\nconstexpr std::size_t StandingStateBytes = 240U;\nconstexpr std::size_t ResultFixedBytes = 588U;\n""",
    """constexpr std::uint32_t PacketVersionV1 = 1U;\nconstexpr std::uint32_t PacketVersionStanding = 2U;\nconstexpr std::uint32_t PacketFlags = 0U;\nconstexpr std::size_t RequestFixedBytes = 344U;\nconstexpr std::size_t FieldBytes = 160U;\nconstexpr std::size_t ConstraintBytes = 264U;\nconstexpr std::size_t StandingStateBytes = 240U;\nconstexpr std::size_t ResultFixedBytesV1 = 580U;\nconstexpr std::size_t ResultFixedBytesStanding = 588U;\n\nbool PacketVersionValid(const std::uint32_t version) {\n    return version == PacketVersionV1 || version == PacketVersionStanding;\n}\n\nstd::size_t ResultFixedBytes(const std::uint32_t version) {\n    return version >= PacketVersionStanding\n        ? ResultFixedBytesStanding\n        : ResultFixedBytesV1;\n}\n\nstd::uint32_t RequestPacketVersion(\n    const laplace_cognition_runtime_request& request) {\n    for (std::size_t index = 0U;\n         index < static_cast<std::size_t>(request.constraint_count); ++index) {\n        if (request.constraints[index].source_class ==\n            LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING) {\n            return PacketVersionStanding;\n        }\n    }\n    return PacketVersionV1;\n}\n""",
)
replace_once(
    compile,
    """bool ReadOperatorReceipt(\n    ByteReader* const reader,\n    laplace_cognition_operator_receipt* const receipt) {\n    return reader != nullptr && receipt != nullptr &&\n        reader->Digest(&receipt->receipt_id) && reader->Digest(&receipt->operator_id) &&\n        reader->Digest(&receipt->program_fingerprint) &&\n        reader->Digest(&receipt->field_set_fingerprint) &&\n        reader->Digest(&receipt->constraint_set_fingerprint) &&\n        reader->U64(&receipt->field_count) &&\n        reader->U64(&receipt->input_constraint_count) &&\n        reader->U64(&receipt->selected_constraint_count) &&\n        reader->U64(&receipt->deduplicated_dependent_count) &&\n        reader->U64(&receipt->physicality_constraint_count) &&\n        reader->U64(&receipt->testimony_constraint_count) &&\n        reader->U64(&receipt->derived_constraint_count) &&\n        reader->U64(&receipt->standing_constraint_count) &&\n        reader->U32(&receipt->relation_plane_count) && reader->U32(&receipt->status) &&\n        reader->U32(&receipt->version) && reader->U32(&receipt->flags);\n}\n""",
    """bool ReadOperatorReceipt(\n    ByteReader* const reader,\n    laplace_cognition_operator_receipt* const receipt,\n    const std::uint32_t packet_version) {\n    if (reader == nullptr || receipt == nullptr ||\n        !PacketVersionValid(packet_version)) {\n        return false;\n    }\n    *receipt = laplace_cognition_operator_receipt{};\n    if (!reader->Digest(&receipt->receipt_id) ||\n        !reader->Digest(&receipt->operator_id) ||\n        !reader->Digest(&receipt->program_fingerprint) ||\n        !reader->Digest(&receipt->field_set_fingerprint) ||\n        !reader->Digest(&receipt->constraint_set_fingerprint) ||\n        !reader->U64(&receipt->field_count) ||\n        !reader->U64(&receipt->input_constraint_count) ||\n        !reader->U64(&receipt->selected_constraint_count) ||\n        !reader->U64(&receipt->deduplicated_dependent_count) ||\n        !reader->U64(&receipt->physicality_constraint_count) ||\n        !reader->U64(&receipt->testimony_constraint_count) ||\n        !reader->U64(&receipt->derived_constraint_count)) {\n        return false;\n    }\n    if (packet_version >= PacketVersionStanding &&\n        !reader->U64(&receipt->standing_constraint_count)) {\n        return false;\n    }\n    return reader->U32(&receipt->relation_plane_count) &&\n        reader->U32(&receipt->status) &&\n        reader->U32(&receipt->version) &&\n        reader->U32(&receipt->flags);\n}\n""",
)
replace_once(
    compile,
    """            !writer.U32(PacketVersion) || !writer.U32(PacketFlags) ||\n""",
    """            !writer.U32(RequestPacketVersion(*request)) ||\n            !writer.U32(PacketFlags) ||\n""",
)
replace_once(
    compile,
    """            !reader.U32(&version) || version != PacketVersion ||\n""",
    """            !reader.U32(&version) || !PacketVersionValid(version) ||\n""",
)
replace_once(
    compile,
    """            !ReadOperatorReceipt(&reader, &operator_receipt) ||\n""",
    """            !ReadOperatorReceipt(&reader, &operator_receipt, version) ||\n""",
)
replace_once(
    compile,
    """        if (!reader.Complete() || bytes.size() !=\n                ResultFixedBytes + solution.size() * sizeof(double)) {\n""",
    """        if (!reader.Complete() || bytes.size() !=\n                ResultFixedBytes(version) + solution.size() * sizeof(double)) {\n""",
)

Path("tools/repair_cognition_packet_version.py").unlink()
Path(".github/workflows/repair-cognition-packet-version.yml").unlink()
