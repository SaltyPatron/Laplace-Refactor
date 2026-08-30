#!/usr/bin/env python3
from pathlib import Path

program = Path('tests/dotnet/Program.cs')
text = program.read_text(encoding='utf-8')
text = text.replace('Require(LaplaceIsaContract.Minor == 10,', 'Require(LaplaceIsaContract.Minor == 11,')
text = text.replace('Require(LaplaceIsaContract.Operations.Length == 10,', 'Require(LaplaceIsaContract.Operations.Length == 11,')
needle = '''        Require(ReferenceMappingResolveBatch.Descriptor == LaplaceIsaContract.Operations[9],\n            "generated reference-mapping declaration differs from descriptor inventory");\n'''
replacement = needle + '''        Require(CognitionSolvePacket.Descriptor == LaplaceIsaContract.Operations[10],\n            "generated cognition declaration differs from descriptor inventory");\n'''
if needle not in text:
    raise SystemExit('reference-mapping descriptor anchor not found')
program.write_text(text.replace(needle, replacement), encoding='utf-8')
