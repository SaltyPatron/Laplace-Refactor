#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "engine/src/cognition_packet.cpp",
    """    constexpr std::size_t OperatorReceiptBytesStanding =\n        5U * 32U + 8U * 8U + 4U * 4U;\n""",
    """    constexpr std::size_t OperatorReceiptBytesStanding =\n        5U * 32U + 7U * 8U + 4U * 4U;\n""",
)
replace_once(
    "engine/src/cognition_packet.cpp",
    """    if (packet_version >= PacketVersionStanding &&\n        !writer->U64(receipt.standing_constraint_count)) {\n        return false;\n    }\n    return writer->U32(receipt.relation_plane_count) &&\n""",
    """    return writer->U32(receipt.relation_plane_count) &&\n""",
)
replace_once(
    "engine/src/cognition_packet_compile.cpp",
    "constexpr std::size_t ResultFixedBytesStanding = 588U;\n",
    "constexpr std::size_t ResultFixedBytesStanding = 580U;\n",
)
replace_once(
    "engine/src/cognition_packet_compile.cpp",
    """    if (packet_version >= PacketVersionStanding &&\n        !reader->U64(&receipt->standing_constraint_count)) {\n        return false;\n    }\n    return reader->U32(&receipt->relation_plane_count) &&\n""",
    """    return reader->U32(&receipt->relation_plane_count) &&\n""",
)

Path("tools/remove_invented_standing_receipt_count.py").unlink()
Path(".github/workflows/remove-invented-standing-receipt-count.yml").unlink()
