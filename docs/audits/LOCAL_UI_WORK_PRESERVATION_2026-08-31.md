# Interrupted local and UI-agent work preservation audit

Date: 2026-08-31

Status: preservation complete; semantic reconciliation pending

## Governing result

The dirty `Laplace-Refactor-pr93` and `Laplace-Refactor-pr94` worktrees are not
abandoned scratch. The inventor directly identified them as work performed during the
local coding session that hit its session limit, while GitHub/UI-agent work continued
through separate remote branches and pull requests.

Accordingly, pull-request supersession is only a delivery-history fact. It cannot be
used as a deletion instruction or as evidence that an uncommitted local hunk is
obsolete. The local and remote lines must be compared by behavior, requirement,
receipt, and file-level content.

## Preserved interrupted work

| Worktree | Interrupted base | Dirty boundary | Preservation snapshot | Remote preservation ref |
| --- | --- | --- | --- | --- |
| `Laplace-Refactor-pr93` | `e8de3e44b59eed0b87deb302c224db194a9e4f55`; two ahead and six behind its upstream | 14 final paths; 1,271 insertions and 10 deletions; status fingerprint `aaef3d06a8776597095e831de24f9cb5addae756fbd89f3a7d090f4115ecca7f` | `822bb9b35697ff65104bb1b269872b2d49967cbc` | `origin/preserve/pr93-local-wip-20260831` |
| `Laplace-Refactor-pr94` | `fe80778b8c93b71102db0a5691160a96976dd072`; one behind `origin/main` | 11 final paths; 147 insertions and 52 deletions; status fingerprint `e39a0dd13063975acdf2abe7c777121a8ff89351e722d80a4bc70aaa39963bbf` | `fe23b5ee9b374b0d637dbbbdf525040ddc06ecca` | `origin/preserve/pr94-local-wip-20260831` |

The snapshots were produced with `git stash create`. That operation created commits
without changing either worktree, index, branch, or local stash ref. In each snapshot:

- parent one is the interrupted branch HEAD;
- parent two is the staged index state;
- the snapshot commit tree is the complete working-tree state.

This retains the staged/unstaged distinction as well as the final bytes. The exact
trees, parents, delta digests, and recovery rules are in
`state/local-work-preservation-2026-08-31.json`.

## Preserved prior archive

The earlier `refs/archive/20260829/*` namespace contained 34 refs, including the
preserved pre-cleanup stash. It is now mirrored additively under
`origin/preserve/archive-20260829/*`. The stash commit is also directly available as
`origin/preserve/archive-stash-20260829`.

After fetching those preservation refs, this command returned zero commits:

```sh
git rev-list --all --not --remotes=origin
```

This proves that every commit reachable from a current local ref is reachable from a
remote ref at the observed boundary. It does not prove correctness, relevance,
acceptance, or mergeability.

## Safe reconciliation sequence

1. Keep the two original dirty worktrees untouched as physical interruption evidence.
2. Compare each snapshot with its original HEAD, its remote PR head, current `main`,
   and the newer decomposition/canonical-witness PRs.
3. Classify each hunk as independently retained, remotely equivalent, superseded by a
   stronger implementation, still required but absent, conflicting, or invalid under
   current authority.
4. Reapply retained behavior only in isolated clean worktrees based on current main.
5. Require current contracts, positive tests, deliberate defects, and complete-route
   receipts before calling any recovered behavior implemented.

The preservation branches are immutable recovery coordinates. They must not become a
shortcut around clean-room authority or the common ISA/recipe/product lifecycle.
