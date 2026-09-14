#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one match in {path}: {old!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "engine/src/cognition_packet.cpp",
    """    constexpr std::size_t OperatorReceiptBytes =\n        5U * 32U + 7U * 8U + 4U * 4U;\n""",
    """    constexpr std::size_t OperatorReceiptBytes =\n        5U * 32U + 8U * 8U + 4U * 4U;\n""",
)
replace_once(
    "engine/src/cognition_packet.cpp",
    """        writer->U64(receipt.testimony_constraint_count) &&\n        writer->U64(receipt.derived_constraint_count) &&\n        writer->U32(receipt.relation_plane_count) &&\n""",
    """        writer->U64(receipt.testimony_constraint_count) &&\n        writer->U64(receipt.derived_constraint_count) &&\n        writer->U64(receipt.standing_constraint_count) &&\n        writer->U32(receipt.relation_plane_count) &&\n""",
)
replace_once(
    "engine/src/cognition_packet_compile.cpp",
    "constexpr std::size_t ResultFixedBytes = 580U;\n",
    "constexpr std::size_t ResultFixedBytes = 588U;\n",
)
replace_once(
    "engine/src/cognition_packet_compile.cpp",
    """        reader->U64(&receipt->testimony_constraint_count) &&\n        reader->U64(&receipt->derived_constraint_count) &&\n        reader->U32(&receipt->relation_plane_count) && reader->U32(&receipt->status) &&\n""",
    """        reader->U64(&receipt->testimony_constraint_count) &&\n        reader->U64(&receipt->derived_constraint_count) &&\n        reader->U64(&receipt->standing_constraint_count) &&\n        reader->U32(&receipt->relation_plane_count) && reader->U32(&receipt->status) &&\n""",
)

Path("tools/fix_typed_standing_wire.py").unlink()
Path(".github/workflows/fix-typed-standing-wire.yml").unlink()
