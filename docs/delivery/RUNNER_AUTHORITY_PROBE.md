# Host path diagnostics

`python3 tools/delivery/host_prerequisite_probe.py PATH` reports the current account’s access to a path and each parent component. It distinguishes absence (`ENOENT`) from inaccessible parents (`EACCES`) and includes numeric owner, group and access bits. `--require-readable-file` returns a nonzero exit status when the requested file cannot be read.

The manual `runner-authority-probe` workflow runs this operation as the Refactor runner against configured host paths. It preserves its diagnostic JSON under `/build/laplace/work/refactor-scratch` and includes it in the run summary. It performs no activation or database operations and does not run on builds or pull requests.
