#!/usr/bin/env python3
"""Select chess calibration for a same-repository candidate or accepted deployment."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


def select(paths: list[str], policy: dict, before, after) -> list[str]:
    reasons = sorted(set(paths) & set(policy['direct_inputs']))
    for lock, rule in policy['focused_locks'].items():
        if lock not in paths:
            continue
        previous = before(lock).get(rule['collection'], {})
        current = after(lock).get(rule['collection'], {})
        names = set(rule.get('names', []))
        if 'prefix' in rule:
            names.update(name for name in set(previous) | set(current) if name.startswith(rule['prefix']))
        changed = sorted(name for name in names if previous.get(name) != current.get(name))
        reasons.extend(f'{lock}:{name}' for name in changed)
    return reasons


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--classification', type=Path, required=True)
    parser.add_argument('--contract', type=Path, default=Path('contracts/chess-benchmark.json'))
    parser.add_argument('--base', required=True)
    parser.add_argument('--head', required=True)
    arguments = parser.parse_args()
    for revision in (arguments.base, arguments.head):
        subprocess.run(['git', 'cat-file', '-e', f'{revision}^{{commit}}'], check=True, capture_output=True)
    def document(revision: str, path: str) -> dict:
        result = subprocess.run(['git', 'show', f'{revision}:{path}'], capture_output=True, text=True)
        if result.returncode:
            # New chess lock files have no predecessor; Git has already verified both commits.
            exists = subprocess.run(['git', 'cat-file', '-e', f'{revision}:{path}'], capture_output=True)
            if exists.returncode:
                return {}
            raise RuntimeError(result.stderr)
        return json.loads(result.stdout)
    classification = json.loads(arguments.classification.read_text())
    policy = json.loads(arguments.contract.read_text())['deployment_calibration']
    reasons = select(classification['paths'], policy, lambda path: document(arguments.base, path), lambda path: document(arguments.head, path))
    print(json.dumps({'schema': 'laplace.chess-calibration-request/v1', 'base': arguments.base, 'head': arguments.head, 'requested': bool(reasons), 'reasons': reasons, 'execution_gate': 'same-repository candidate after hosted proof with calibration-owned source verification and build, or accepted main after successful product activation'}))


if __name__ == '__main__':
    main()
