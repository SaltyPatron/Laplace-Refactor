#!/usr/bin/env python3
from pathlib import Path

path = Path("integrations/postgresql/extension/src/semantic_cognition_pg.c")
text = path.read_text(encoding="utf-8")

include_anchor = '#include "fmgr.h"\n'
if '#include "lib/stringinfo.h"\n' not in text:
    if text.count(include_anchor) != 1:
        raise SystemExit("fmgr include anchor not unique")
    text = text.replace(
        include_anchor,
        include_anchor + '#include "lib/stringinfo.h"\n',
        1,
    )

start_marker = '    static const char query[] =\n'
end_marker = ';\n    Datum* source_values;'
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("semantic query block not found")
block = text[start:end + 1]
lines = block.splitlines()
if lines[0] != '    static const char query[] =':
    raise SystemExit("unexpected semantic query declaration")
split_line = '        "), standing_lanes AS MATERIALIZED ("'
try:
    split_index = lines.index(split_line)
except ValueError as error:
    raise SystemExit("standing-lanes query split point not found") from error

first = lines[1:split_index]
second = lines[split_index:]
if not first or not second:
    raise SystemExit("semantic query split produced an empty fragment")
if second[-1].endswith(';'):
    second[-1] = second[-1][:-1]

replacement = (
    '    static const char query_part1[] =\n'
    + '\n'.join(first)
    + ';\n'
    + '    static const char query_part2[] =\n'
    + '\n'.join(second)
    + ';\n'
    + '    StringInfoData query'
)
text = text[:start] + replacement + text[end + 1:]

execute_anchor = '    result = SPI_execute_with_args(\n        query, 4, argument_types, argument_values, NULL, true, row_limit);'
execute_replacement = (
    '    initStringInfo(&query);\n'
    '    appendStringInfoString(&query, query_part1);\n'
    '    appendStringInfoString(&query, query_part2);\n'
    '    result = SPI_execute_with_args(\n'
    '        query.data, 4, argument_types, argument_values, NULL, true, row_limit);'
)
if text.count(execute_anchor) != 1:
    raise SystemExit("SPI semantic query execution anchor not unique")
text = text.replace(execute_anchor, execute_replacement, 1)

path.write_text(text, encoding="utf-8")
Path("tools/repair_semantic_query_literal.py").unlink()
Path(".github/workflows/repair-semantic-query-literal.yml").unlink()
