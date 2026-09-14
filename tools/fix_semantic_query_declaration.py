#!/usr/bin/env python3
from pathlib import Path

path = Path("integrations/postgresql/extension/src/semantic_cognition_pg.c")
text = path.read_text(encoding="utf-8")
old = "    StringInfoData query\n    Datum* source_values;"
new = "    StringInfoData query;\n    Datum* source_values;"
if text.count(old) != 1:
    raise SystemExit("semantic StringInfo declaration anchor not unique")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
Path("tools/fix_semantic_query_declaration.py").unlink()
Path(".github/workflows/fix-semantic-query-declaration.yml").unlink()
