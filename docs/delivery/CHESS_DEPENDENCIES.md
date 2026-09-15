# Chess source dependencies

The default host setup now builds the official Stockfish and Cute Chess Git sources,
checks their actual executable behavior, and records the paths used to launch them.
The standalone entry point is `bash scripts/setup-chess.sh`. With administrator
access it also installs the compiler, CMake, Python environment and OS library
prerequisites. Missing Qt is acquired as an SDK; both chess programs are built
from source.

Source ownership follows `dependencies/roots.json`: `LAPLACE_VERIFIED_SOURCE_ROOT`
selects an existing verified source estate. Without that variable the source root is
`/opt/laplace/external/source-generations/<SHA-256 of dependencies/lock.json>`, the
same convention used by the native build. An unrelated or modified checkout is
preserved and reported. Clean checkouts fetch and select their exact locked upstream
revision, with archive and license hashes checked before and after compilation.
The source consumers in custom-stack CI and the composition benchmark now fill
missing entries through the existing locked Git acquisition tool before verifying
the full generation. A changed chess lock no longer requires manually creating a
new thirteen-dependency directory first. Existing modified sources are preserved.

The selections verified on 2026-09-15 are Stockfish `sf_19`, Cute Chess `v1.5.1`,
Qt SDK `6.11.2` and SDK acquisition tool `aqtinstall 3.3.0`. Stockfish 19 uses one
NNUE file, `nn-1a298aa575a0.nnue`; its full hash and size are in
`dependencies/artifact-lock.json`. The obsolete Stockfish 18 secondary network is
no longer selected. Stockfish's default native build matches the build host CPU;
use `--arch` for a different deployment CPU and rebuild on that target as needed.

The Stockfish executable is the source build's `stockfish/src/stockfish`.
Cute Chess's CLI is the CMake output under `/build/laplace/build/chess/cutechess/`.
There is no copied release binary or second executable installation. The paths,
source revisions, build commands, logs and executable hashes are retained in
`/opt/laplace/tools/chess/current.json`. Rebuild before a new experiment; keep
the recorded binary and recipe identity with every prior result.

```bash
# Check newest upstream stable releases against the selected source locks.
python3 tools/dependencies/chess_tools.py latest

# Reverify exact source/binary identities and execute protocol probes.
python3 tools/dependencies/chess_tools.py check

# Build only Stockfish or select an already installed compatible Qt SDK.
bash scripts/setup-chess.sh --tool stockfish
bash scripts/setup-chess.sh --tool cutechess --qt-prefix /path/to/Qt/6.11.2/gcc_64

# Launch the exact recorded source executable, or use its path directly.
python3 tools/dependencies/chess_tools.py run --tool stockfish
python3 tools/dependencies/chess_tools.py run --tool cutechess -- --version
```

The online freshness check fails when a newer stable release exists, so an old
selection cannot silently be described as current. Update the Git/network/version
locks together and rebuild. `--offline` deliberately reuses the exact selected
source and cached network; it makes no claim about the newest upstream release.

Lichess is a hosted API, with no Lichess server package required for a bot client.
`check --online` can read `/api/account` using `LICHESS_TOKEN`; it never creates
an account, changes a title, sends a move, accepts a challenge or posts chat.
A bot needs a BOT account and `bot:play` scope. Account readback alone does not
verify that scope. A separate `lichess-bot` bridge and Python `chess` package are
not dependencies of the native Refactor implementation.

Syzygy files remain a separate selected dataset. `SYZYGY_PATH` only identifies a
location. For exact byte verification pass `--syzygy-manifest` with an object
containing `root`, an explicit `coverage` description, and `files`; each file names
`path`, `size` and `sha256`. Both `.rtbw` and `.rtbz` entries must be present.
The checker reports byte verification separately from a real native tablebase
probe; it does not claim arbitrary material coverage from a few files.

These source builds supply external chess tools. The current Refactor source does
not implement the full native chess forward pass, UCI product adapter, PGN chess
admission, or Lichess Bot API service. Installing tools cannot claim those public
product programs are functional. The readback includes this boundary explicitly.

## Machine configuration and measured benchmarks

`tools/dependencies/chess_benchmark.py` observes the actual Linux execution
environment and measures the recorded executable paths. Its governing contract is
`contracts/chess-benchmark.json`, under the common `contracts/benchmark-suite.json`
law. The full benchmark runs only when explicitly requested; CI runs the small
resource/receipt failure controls.

```bash
# Capability and resource observation, without running the timed sweeps.
scripts/benchmark-chess.sh observe \
  --output /build/laplace/work/chess-capabilities

# Explicit bounded sweep; choose a NEW output directory for each experiment.
scripts/benchmark-chess.sh run \
  --output /build/laplace/work/chess-measurement \
  --cpu-budget 8 --memory-mib 8192 \
  --threads 1,2,4,8 --hash-mib 16,64 \
  --samples 3 --warmups 1 --depth 10 \
  --concurrency 1,2,4,8 --game-threads 1 --game-hash-mib 16 \
  --games 8 --game-depth 6 --max-moves 20 --timeout 120
```

The numeric budget in the example must fit the machine on which it runs. Without
explicit sweep values, thread and concurrency candidates use powers of two and
the final observed CPU bound. CPU capacity is the minimum of process affinity and
every visible cgroup ancestor quota, including fractional quota values.
Sub-CPU quotas cannot admit a full search thread and receive an explicit rejection
instead of rounding the observed CPU capacity upward. Memory
headroom is the minimum of host `MemAvailable` and every visible ancestor's
`memory.max - memory.current`; the default plan leaves an explicitly reported
512 MiB reserve. Namespace-hidden limits remain unobservable. A resource grant
does not reserve those resources against other running applications.

The receipt records CPU model/features, visible physical cores and SMT siblings,
NUMA layout, kernel, cgroup CPU and memory settings, actual UCI options and compiler
output, source/binary/network fingerprints and exact commands. Each run records
elapsed time, child CPU and filesystem I/O, sampled process-group RSS/PSS, observed
huge pages, and cgroup throttling/pressure. RSS can count shared pages more than
once. The 50 ms sampler may miss short peaks; unreadable smaps observations make
PSS and huge-page totals null and cannot prove that huge pages were unused. The script does not change
kernel settings, CPU affinity or NUMA policy.
If runtime PIDs disagree with `/proc/self/stat`, per-process metrics and sampled
RSS enforcement are explicitly unavailable; the observer never attributes an
unrelated procfs PID to its child. Shared cgroup counters remain separately
observable in that environment.

Stockfish measurements use the upstream fixed-depth `bench` position suite, with
`NumaPolicy=auto`, full strength and `MultiPV=1`. Threads and Hash are explicit.
Every sample preserves position completion, actual nodes, the engine's search
milliseconds/NPS, whole-process elapsed time and output fingerprints. SMP and Hash
changes can alter the searched node workload and result, so elapsed speedup and
parallel efficiency are descriptive measurements, not identical-work scaling.

Cute Chess measurements use color-balanced, paired Stockfish self-play at each
selected game concurrency. Pondering stays off. CPU admission counts one active
search per game, and concurrency cannot exceed the requested game count. Memory
admission counts two resident engines per game, each with
its Hash allocation plus an explicit `--engine-overhead-mib` estimate (default
256 MiB). This estimate is distinguished from measured RSS/PSS. Every PGN must
complete, preserve both color assignments and contain no recorded crash, illegal
move or clock failure. Games/s and plies/s include process and UCI lifecycle cost.
The maximum-move limit deliberately makes these functional throughput experiments,
not playing-strength or Elo experiments.

Warmups are excluded and measured order is deterministically shuffled to reduce
order bias. Recommendations select the best observed median for the serial
fixed-depth suite and aggregate game throughput separately, with the full range
and sample count retained. They are provisional settings for the measured
environment and inputs; an experiment executed in a development container does
not establish the right settings for a separate user host.
Executable hashes and CPU, affinity, topology and cgroup controls are checked again
at completion. A changed binary or resource boundary prevents a recommendation.
Elapsed time includes leader completion and cleanup of the owned process group;
surviving engine children are terminated even if their Cute Chess leader exits first.

`--stockfish /path/to/another/stockfish` can compare a separately built Stockfish 19
binary while retaining its hash and compiler output. Its source/build provenance
is labeled separately from the recorded Refactor build. `setup-chess.sh` uses
upstream `profile-build` for PGO, while the direct Python build also permits a
regular build. Treat compiler, native ISA selection and PGO as measured build
choices; do not assume a universal gain from one compiler flag.

`receipt.json`, per-run transcripts and paired PGNs remain together in the output
directory. A failure or timeout leaves a failure receipt with completed samples
and no recommendation. Lichess credentials and Syzygy byte/probe status remain
separate capability fields. These external benchmarks do not claim that the
missing Refactor UCI/chess program can already play against Stockfish.

The manual `chess-calibration.yml` workflow builds from the configured source
estate and repeats this measurement on the actual self-hosted runner. It keeps
all raw evidence in the `chess-calibration-<run>-<attempt>` artifact. It is never
triggered by pushes, PRs or merges. Blank resource inputs use the observed bounds;
explicit inputs must fit them.

`scripts/benchmark-chess.sh` holds `/build/laplace/work/host-resource.lock` for the
whole experiment. Refactor custom-stack, PostgreSQL and package proof, composition benchmarking,
and product activation steps use the
same physical-host lock as the other repository's build, deploy and calibration
commands. GitHub workflow concurrency alone cannot coordinate two repositories.
The wrapper preserves the lock file's creator and group permissions and closes
the descriptor in the invoked command so long-lived services cannot retain it.
`LAPLACE_HOST_RESOURCE_LOCK` permits an explicitly selected equivalent lock on
another host or in tests. Call the Python measurement tool directly only when its
caller already owns the host lock or provides equivalent exclusive scheduling.

The custom-stack proof and product activation workflows also retain a bounded
`highway-*-diagnostic-<run>-<attempt>` artifact. Its diagnostic records whether the
configured Highway admission directory is absent, unreadable or a symlink and
captures exact existing Highway request/receipt bytes from the configured receipt
estate. It neither queries nor mutates the database, follows symlinks, reconstructs
an admission, or promotes an available file into valid replay authority. Additional
retained archives can be selected explicitly with `--search-root` on
`tools/delivery/highway_receipt_diagnostic.py`. Recovery still requires the native
controller's exact system, epoch, sequence and canonical request checks; the
existing `tests/highway_retained_context_live.py` proves two rollback replays and
the deliberate active-to-active rejection after genuine retained evidence is restored.
