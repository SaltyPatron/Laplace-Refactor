"""Read one exact, active source-admission context from the installed schema."""
from __future__ import annotations

import json
import re

# Unicode generation content is linked to an activated cache by its deposit
# receipt. Activation columns belong to that receipt, not to root generation.
# Highway and Unicode expose separate active execution-context dimensions.
LIVE_RUNTIME_SQL = """
SELECT pg_catalog.json_build_object(
  'activation_epoch_id',pg_catalog.encode(u.activation_epoch_id,'hex'),
  'activation_epoch_fingerprint',pg_catalog.encode(u.epoch_fingerprint,'hex'),
  'geometry_epoch',pg_catalog.encode(g.geometry_epoch,'hex'),
  'perfcache_epoch',pg_catalog.encode(u.epoch_fingerprint,'hex'),
  'numeric_epoch',pg_catalog.encode(h.activation_epoch_fingerprint,'hex')
)::text
FROM laplace.perfcache_active_control AS u
JOIN laplace.unicode_root_deposit_receipt AS d
  ON d.activation_epoch_id=u.activation_epoch_id
 AND d.activation_epoch_fingerprint=u.epoch_fingerprint
 AND d.perfcache_manifest_fingerprint=u.manifest_fingerprint
JOIN laplace.unicode_root_generation AS g
  ON g.root_receipt=d.root_receipt
 AND g.plan_manifest_fingerprint=d.plan_manifest_fingerprint
 AND g.postgresql_artifact_fingerprint=d.postgresql_artifact_fingerprint
CROSS JOIN laplace.highway_registry_active_control AS h
JOIN laplace.highway_registry_generation AS hg
  ON hg.activation_epoch_id=h.activation_epoch_id
 AND hg.activation_epoch_fingerprint=h.activation_epoch_fingerprint
WHERE u.singleton AND u.active_present
  AND h.singleton AND h.active_present;
"""


class RuntimeStateError(ValueError):
    pass


def parse_live_state(output: str) -> dict[str, str]:
    rows = [line.strip() for line in output.splitlines() if line.strip()]
    if len(rows) != 1:
        raise RuntimeStateError(
            f"live activation-state query returned {len(rows)} rows; expected one")
    try:
        value = json.loads(rows[0])
    except json.JSONDecodeError as error:
        raise RuntimeStateError("live activation-state query returned invalid JSON") from error
    if not isinstance(value, dict):
        raise RuntimeStateError("live activation-state query did not return an object")
    widths = {
        "activation_epoch_id": 32,
        "activation_epoch_fingerprint": 64,
        "geometry_epoch": 64,
        "perfcache_epoch": 64,
        "numeric_epoch": 64,
    }
    result = {}
    for field, width in widths.items():
        candidate = value.get(field)
        if not isinstance(candidate, str) or re.fullmatch(
                "[0-9a-f]{" + str(width) + "}", candidate) is None:
            raise RuntimeStateError(f"live activation state has invalid {field}")
        result[field] = candidate
    return result
