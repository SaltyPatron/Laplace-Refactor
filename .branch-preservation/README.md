# Preserved branch history

inventory.json maps every original branch and pull-request head to its exact commit.
The parents of this archive commit retain their complete Git histories and file trees.
This is a recovery snapshot, not an assertion that all changes are active on main.

Restore any listed head with `git fetch origin refs/tags/branch-preservation/5152968f57565c011c84` followed by
`git switch -c recovered-work <listed-commit-sha>`.
