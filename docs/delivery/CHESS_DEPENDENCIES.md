# Chess source dependencies

The default host setup now builds the official Stockfish and Cute Chess Git sources,
checks their actual executable behavior, and records the paths used to launch them.
The standalone entry point is `bash scripts/setup-chess.sh`. With administrator
access it also installs the compiler, CMake, Python environment and OS library
prerequisites. Missing Qt is acquired as an SDK; both chess programs are built
from source.

Source ownership follows `dependencies/roots.json`: `LAPLACE_VERIFIED_SOURCE_ROOT`
selects an existing verified source estate. Without that variable, the native build
and chess installer share the persisted source-parent selection inside the resolved
`/opt/laplace/external` estate. Each generation is named by the SHA-256 of
`dependencies/lock.json`. An unrelated or modified checkout is
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
bash scripts/setup-chess.sh --tool stockfish --stockfish-source /vault/External/Stockfish/SF_19
bash scripts/setup-chess.sh --tool cutechess --qt-prefix /path/to/Qt/6.11.2/gcc_64

# Launch the exact recorded source executable, or use its path directly.
python3 tools/dependencies/chess_tools.py run --tool stockfish
python3 tools/dependencies/chess_tools.py run --tool cutechess -- --version
```

`--stockfish-source` selects an existing Stockfish Git checkout at its actual
location. `LAPLACE_STOCKFISH_SOURCE` supplies the same selection when the flag is
absent; the explicit flag takes precedence. Setup verifies the checkout's origin,
tracked bytes, modes, locked revision, archive and licenses through the same source
owner, then updates and builds `src/stockfish` there. The matching locked NNUE is
verified before the real UCI/search probe. Modified sources are preserved and cause
an explicit failure. A missing or non-Git selected path fails without creating a
replacement checkout. This override affects Stockfish only; Cute Chess continues
to use the selected source estate. With no override, the existing
`<source-root>/stockfish` acquisition remains the default. `check` and `run` use the
actual source/executable paths retained in `current.json`.

The online freshness check fails when a newer stable release exists, so an old
selection cannot silently be described as current. Update the Git/network/version
locks together and rebuild. `--offline` deliberately reuses the exact selected
source and cached network; it makes no claim about the newest upstream release.

Lichess is a hosted API, with no Lichess server package required for a bot client.
`check --online` uses `LICHESS_TOKEN` for sequential, read-only account and scope
verification: GET `/api/account` then POST `/api/token/test`. The second request
tests the existing credential; it creates no account, title, challenge, move or
chat. Requests target the fixed official HTTPS origin without proxy discovery or
redirects. Each network operation has a 15-second socket timeout and responses
are limited to 64 KiB; this is not an aggregate wall-time guarantee. HTTP failures,
including rate limiting, stop the check without a retry. A 429 retains a minimum
60-second retry delay (or a longer numeric Retry-After). No token, response body,
or upstream exception detail is printed. Offline checks never contact Lichess.

The receipt distinguishes token validity, BOT title, exact `bot:play` scope,
matching account identity, and `account_prerequisites_ready`. Even a successful
account/scope observation leaves `product_gameplay_ready=false` and
`product_bot_api_adapter=not-implemented`. Refactor has no product bot service
or operator stop-marker owner yet. Its qualified native rules/trace adapter and
canonical LINE planner do not implement the required chess forward/search
program, UCI product adapter, or BOT game/session lifecycle. Original's independent
service and external Stockfish cannot stand in for that missing native program.
A separate `lichess-bot` bridge and Python `chess` package are not dependencies
of the native Refactor implementation.

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
law. Chess calibration runs manually, for selected chess changes on a
same-repository candidate, or after successful deployment of those changes.
Unrelated PRs run the small resource/receipt failure controls.

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
  --games 8 --game-depth 8 --game-time-control 60 --timeout 600 --overall-timeout 1800
```

The numeric budget in the example must fit the machine on which it runs. Without
explicit sweep values, thread and concurrency candidates include powers of two,
the observed physical-core boundary, and the final observed CPU bound. A game's
thread count adjusts the corresponding concurrency boundary. CPU capacity is the minimum of process affinity and
every visible cgroup ancestor quota, including fractional quota values.
Sub-CPU quotas cannot admit a full search thread and receive an explicit rejection
instead of rounding the observed CPU capacity upward. Memory
headroom is the minimum of host `MemAvailable` and every visible ancestor's
`memory.max - memory.current`. The default memory grant covers the largest admitted
engine or tournament configuration plus a bounded margin; it leaves an explicitly
reported 512 MiB reserve outside that grant. It does not claim all free host memory
for a small workload. Namespace-hidden limits remain unobservable. A resource grant
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
move or clock failure. The default has no maximum-move adjudication. An independent
hash-locked `python-chess` provider replays every legal move from the standard
initial position and requires the final board outcome to match the serialized
result. Its exact source archive, imported runtime files and license are retained
with the measurement. Normal games/second and plies/second include process and UCI
lifecycle cost; they do not include database recording or readback.

A capped process diagnostic requires explicit `--diagnostic --max-moves N`.
It reports diagnostic episodes/second and never supplies a normal-game capacity
recommendation. Historical 24-ply, maximum-length-adjudicated measurements remain
capped diagnostics; their rates cannot establish full-game or recorded-game
throughput. Per-case `--timeout` and `--overall-timeout` bound wall time without
turning an incomplete game into a successful result.

Warmups are excluded and measured order is deterministically shuffled to reduce
order bias. Recommendations select the best observed median for the serial
fixed-depth suite and complete normal-game throughput separately, with the full range
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

The reusable `chess-calibration.yml` workflow builds from the configured source
estate and repeats this measurement on the actual self-hosted runner. It supports
manual dispatch and the user-authorized chess-specific deployment calibration. It keeps
all raw evidence in the `chess-calibration-<run>-<attempt>` artifact. The deployment call requires a successful accepted-main activation and
a change to chess source/network selections, tooling, or calibration configuration.
Unrelated dependency-lock and documentation changes do not launch it. Selected
same-repository PRs use the candidate call described below. The common benchmark suite retains its existing explicit-only scheduling
policy; this bounded chess calibration has its own declared deployment policy.
Blank resource inputs use the observed bounds; explicit inputs must fit them.

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
estate and the known `/opt/laplace/receipts` and `/build/laplace/recovery` archives. It neither queries nor mutates the database, follows symlinks, reconstructs
an admission, or promotes an available file into valid replay authority. Additional
retained archives can be selected explicitly with `--search-root` on
`tools/delivery/highway_receipt_diagnostic.py`. Recovery still requires the native
controller's exact system, epoch, sequence and canonical request checks; the
existing `tests/highway_retained_context_live.py` proves two rollback replays and
the deliberate active-to-active rejection after genuine retained evidence is restored.

The source selector (`tools/dependencies/source_estate.py`) resolves the configured
external estate and persists its selected parent in `refactor-source-selection.json`
inside that estate. A writable shared `source-generations` parent stays selected.
When operator-owned parent or lock permissions prevent publication, the selector
creates `refactor-source-generations` inside the same physical external estate and
records that reason. Existing operator generations, locks, owners, and aliases are
preserved. Later source acquisition and chess builds use the persisted selection;
`LAPLACE_VERIFIED_SOURCE_ROOT` and `--source-root` remain explicit source overrides.
The usual exact commit, archive, and license verification still applies to every
generation. Root host setup can repair shared parent access through the common
`prepare-source-parents.sh` helper, while recurring runner acquisition requires no
new privilege. A completed existing generation is verified without a publication
lock or source mutation.

The configured `/opt/laplace/external` estate may be an existing directory alias
onto another volume. Source setup resolves that alias, reports its physical target,
and preserves the alias and every existing owner. It repairs group access only on
the physical estate and `source-generations` parents and the acquisition lock;
generation leaves and lock files cannot redirect elsewhere. Host setup and recurring
source acquisition use the same preparation helper.

The Highway diagnostic also retains a bounded directory index of actual host
archives. Each explicit root gets its own entry budget; named receipt/Highway
subtrees and an independently supplied request identity are visited first. This
supports narrowing subsequent recovery searches without treating a truncated scan
or a missing file as proof that historical admission never occurred.

A same-repository pull request that changes the selected chess sources, network,
installer, or calibration configuration also runs **Candidate chess dependency
calibration on hart-server** after hosted checks succeed. Calibration verifies and
builds its own official source inputs under the shared machine lock. It can run
while the independent PostgreSQL custom-stack proof is incomplete or failing.
The product-path gate still requires both the selected custom-stack proof and
selected candidate calibration to succeed;
a failed or missing calibration cannot produce passing protected check aliases.
Its `candidate-chess-dependencies-<run>-<attempt>` artifact retains the exact
checkout and input hashes in `execution-context.json`, alongside measured compiler,
engine, source, network, resource, and PGN evidence. Candidate dependency results do
not assert that product activation succeeded. Accepted main changes still run the
separate calibration after successful product deployment; core benchmark scheduling
is unchanged.

## Measured hart-server configuration (2026-09-15)

[Candidate calibration run 34958542147](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/34958542147)
completed on the actual Intel Core i7-6850K host: six physical cores, twelve logical
CPUs, affinity 0–11 and one NUMA node. The accepted observation ran from
10:51:49.006804 to 10:55:57.770316 UTC (248.764 seconds). Its memory grant was
7,040 MiB including a 512 MiB margin; peak sampled RSS was 6,154.48 MiB. Binary
identities and the observed resource envelope remained stable.

| Workload | Best measured starting setting | Median result |
| --- | --- | --- |
| One Stockfish process, 51-position depth-12 suite | Threads=4, Hash=64 MiB | 2.186 seconds |
| CuteChess short-game throughput | Concurrency=4; each engine Threads=1, Hash=16 MiB; ponder off | 11.518 games/second |
| Next CuteChess concurrency point | Concurrency=6; same engine settings | 11.297 games/second |

The full grid tested Stockfish Threads=1,2,4,6,8,12 with Hash=16,64,256 MiB, and
CuteChess concurrency=1,2,4,6,8,12. Each profile has one warmup and three measured
samples: 96 timed processes, 24 PGNs, and 384 games including warmups. Every game
ended at the imposed 24-ply limit. All transcript/PGN hashes and the medians were
independently checked. The single-process winner's measured range was
2.128–2.511 seconds. CuteChess concurrency four led six by about 1.96%, with
overlapping ranges. Repeat calibration for a different machine, CPU reservation,
search budget, opening suite, tablebase configuration or executable.

These settings describe two different measured workloads. Do not turn the
single-process four-thread result into four threads for every concurrent game,
or treat the CuteChess result as a measured Laplace evaluation-worker count.
The captured fresh Stockfish defaults were Threads=1, Hash=16 MiB and
NumaPolicy=auto. This candidate receipt does not establish the deployed services'
configuration. It also does not establish Laplace playing strength or Elo.

Retained evidence is the `candidate-chess-dependencies-34958542147-1` artifact,
ID `10392833423`, ZIP SHA-256
`15b902f010a6a4a9d1d0f5dd65e562ca91a4293af8337a34a879a69aef3afb62`.
The earlier candidate attempt failed its final memory-headroom check and remains
failed; only this completed attempt supports these provisional settings.
