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
The wrapper preserves the lock file's creator and group permissions and retains
the reservation through foreground execution and owned cleanup. The canonical
managed-build child closes its inherited reservation descriptor on execution and
disables persistent MSBuild/compiler servers; its supervising owner keeps the
reservation until the build finishes.
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

## Latest measured hart-server configuration (2026-09-17)

[Candidate calibration 35168534734](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35168534734)
measured the actual hart-server environment again and activated the resulting
profiles. This later finite sweep selected:

| Workload | Best measured setting in this sweep | Median result |
| --- | --- | --- |
| Official Stockfish 51-position depth-12 suite | Threads=2; Hash=64 MiB | 1,149,291.198 wall-clock nodes/second; 2.388286 seconds |
| CuteChess complete Stockfish self-play, depth 8, time control 60 | Concurrency=8; each engine Threads=1 and Hash=16 MiB; ponder off | 4.982292 generated complete games/second |

The selected game configuration completed 48 games and 7,488 plies across three
measured samples, with zero capped diagnostic games. Sample rates ranged from
4.640343 to 5.009943 generated complete games/second. The machine was the same
Intel Core i7-6850K with six physical cores and twelve logical CPUs. These are
environment-bound selections from the measured workload and resource grant;
the changed analysis result does not establish a universal optimum or a
performance regression against an identical searched workload.

The measured official source executable and NNUE identities match the hashes in
the historical calibration below. Actual invocation controls subsequently used
the activated analysis profile's Threads=2/Hash=64 defaults and verified that an
explicit Threads=1/Hash=16 caller setting wins. The games profile selected
concurrency eight with Threads=1/Hash=16 and invoked CuteChess with the direct
official source executable. Its retained controls completed full legal games,
including two 156-ply games, and recorded the applied options, actual argv,
profile/receipt identities and PGN hash. These controls prove configuration
consumption; they do not repeat the measured throughput boundary.

The calibration receipt SHA256 is
`f55c41b1d1766b73615c366e8f516860a0b933fdbb5005b0b07d66d7b8dc3482`.
The full evidence is retained in
[artifact 10476786113](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35168534734/artifacts/10476786113),
8,086,884 bytes; ZIP SHA256
`bbe1e2caaa063f6b5d8be3d0f6ff7be113fbfcdc692167bd7bcdb7dbeb529608`.
[Reader 35171351580](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35171351580)
authenticated the receipt, actual arguments, input, transcript and PGN linkage.
Recorded games and recorded games/second remain null. Neither 4.982292 nor the
historical generated-game rates establish a 2,500-recorded-games/second result.
Candidate calibration and profile application do not assert product activation.

## Historical accepted-main calibration (2026-09-17)


[Accepted-main run 35163097838](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35163097838)
installed source `b042de4216e1d674effe5bd3e8ecf3ebc7aeddac` and completed
the separate deployed machine calibration on hart-server. The host is an Intel
Core i7-6850K with six physical cores and twelve logical CPUs. That retained
finite sweep produced these provisional selections:

| Workload | Best measured setting in this sweep | Median result |
| --- | --- | --- |
| Official Stockfish 51-position depth-12 suite | Threads=4; Hash=256 MiB | 2,216,083 wall-clock nodes/second; 2.220134 seconds |
| CuteChess complete Stockfish self-play, depth 8, time control 60 | Concurrency=8; each engine Threads=1 and Hash=16 MiB; ponder off | 5.017007 generated complete games/second; 782.653027 plies/second |

The engine sweep tested Threads=1,2,4,6,8,12 with Hash=16,64,256 MiB.
The game sweep tested concurrency=1,2,4,6,8,12. Each configuration had one excluded
warmup and three measured samples. Each tournament sample contained 16 games from
the standard starting position, with both color assignments and no maximum-move
adjudication. Independent legal PGN replay and final-board outcome checks were
required for completion.

At concurrency eight, all three measured samples completed 48 games and 7,488
plies in 9.575395 seconds total, with zero capped diagnostic games. Sample rates
ranged from 4.997025 to 5.024594 generated games/second. The median sample took
3.189153 seconds; sampled peak RSS was 3,853,979,648 bytes.

The selected engine configuration's wall-clock NPS ranged from 2,135,914 to
2,309,242. Its median searched-node count was 5,103,673 and sampled peak RSS was
596,750,336 bytes. SMP/hash settings can change the searched workload. These
measurements do not establish identical-work parallel speedup, playing strength,
or a universal machine optimum. Other applications, quotas, affinity, and memory
headroom can change the applicable configuration.

The source executable is built directly from official Stockfish commit
`edb0d9db6731067ec50ce619ff372b463bc4dd5d`; the measured executable SHA256 is
`fe5f294eadb777e975aab62779edb78270aefb80b9cb125d5d31905233f3fc5a`.
The selected NNUE SHA256 is
`1a298aa575a085434d29027978dc36867fe9c5bcea9376654b7a8eba1e52dfc2`.
A different build on the same host requires its own reconciled measurement.

Database recording was not measured: recorded games and recorded games/second
are explicitly null. The 5.017007 value measures complete depth-eight game
generation and cannot establish a 2,500-recorded-games/second result.

That run's raw transcripts, PGNs, machine observations and receipt are retained
in [artifact chess-dependencies-35163097838-1](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35163097838/artifacts/10474698366),
8,060,347 bytes; ZIP SHA256
`5d9e6f96d52eb5e504828f7d4fe80873457ed4f4e372ca3e4d4e3dbb37ba0058`.
The earlier
[candidate run 35115292050](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35115292050)
remains historical evidence; its Threads=6/Hash=64 choice was superseded by the
b042 sweep, which was in turn followed by the later candidate measurement above. The
[2026-09-15 run 34958542147](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/34958542147)
capped games at 24 plies and remains a launch diagnostic.

## Applying a measured profile

A completed calibration is activated through the existing source-tool entrypoint:

```bash
python3 tools/dependencies/chess_tools.py activate-profile \
  --calibration-receipt /absolute/path/receipt.json \
  --calibration-sha256 <receipt-sha256> \
  --profile-mode both
```

Activation checks the explicit receipt digest, recomputes the recommendations
from repeated completed samples, and verifies the selected source, executable,
NNUE, Qt, benchmark contract and machine/resource identities. The completed
calibration workflow performs this activation after the benchmark succeeds.
Receipt bytes remain under
`/opt/laplace/tools/chess/calibrations/<sha256>/receipt.json`; addressed profiles
and the two current selections remain under `profiles/` in that same prefix.

`run-profile` revalidates the selected profile before each invocation. A finite
Stockfish command file can contain:

```text
position startpos
go depth 8
quit
```

Run it against the actual source executable:

```bash
python3 tools/dependencies/chess_tools.py run-profile \
  --profile-mode analysis \
  --uci-input /absolute/path/search.uci \
  --profile-output /build/laplace/work/stockfish-profile-invocation-UNIQUE \
  --profile-timeout 60
```

Measured Threads/Hash defaults are sent before the caller's UCI commands. Explicit
caller settings therefore win, subject to the current resource grant. The protocol
owner waits for each finite search's `bestmove` before sending the following
command. It invokes the recorded source executable directly.

For a two-game invocation with explicit lower resource settings:

```bash
python3 tools/dependencies/chess_tools.py run-profile \
  --profile-mode games \
  --profile-output /build/laplace/work/cutechess-profile-invocation-UNIQUE \
  --profile-timeout 600 \
  -- -games 2 -concurrency 1 -each option.Threads=1 option.Hash=16
```

Omitting those explicit overrides consumes the measured game configuration.
Both engine slots use the recorded source Stockfish executable. Each invocation
retains its actual argv, applied configuration, profile and calibration identities,
logs, completion and generated PGN hash. Analysis also retains the caller input
and exact UCI input sent. This invocation evidence does not reproduce the calibration
rate or measure database recording. The default calibration lifetime is seven days;
`--profile-max-age` explicitly selects another finite lifetime during activation.
Changed binaries, NNUE, benchmark contract, machine controls or insufficient
resources cause a visible rejection instead of silent reuse.

## Installed Stockfish source corpus and exact replay

Historical accepted-main run 35163097838 at source b042 completed actual installed admission
of all 119 tracked
files from the locked official Stockfish Git tree: 1,172,144 bytes, including 72
C++ files. The installed CLI then reconstructed every file exactly. Repeating the
same admission produced zero new entities, physicalities, attestations, source
occurrences and structural witnesses.

| Retained result | Actual observation |
| --- | --- |
| Structural witnesses | 571,626 |
| Source occurrence rows | 239 |
| Readback, each pass | 119 records; 119 resolved roots; 968,195 trajectory carriers |
| First CLI admission plus exact readback | 129.581977 seconds |
| Repeated CLI admission plus exact readback | 60.710632 seconds |
| Repeat growth | Entity=0; physicality=0; attestation=0; occurrence=0; witness=0 |

These timings include the complete CLI operation and exact readback; they are not
isolated SQL insertion rates. The final database observation contained 1,172,698
entities, 1,172,837 physicalities and 1,114,449 attestations. Those existing-state
counts are separate denominators and do not imply one physicality per entity.

The source profile retains 102 Tree-sitter `ERROR` or `MISSING` syntax witnesses.
The count excludes ancestor-only `HAS_ERROR` propagation. Their exact byte spans
and flags are retained. The separate [diagnostic classification](STOCKFISH_SYNTAX_DIAGNOSTICS.md)
records that historical profile, the three-rule native plan with 96 diagnostics,
and the later pointer-member correction with 95. The pointer-member derivative
passed 18 exact precedence/shape controls and reconstructed all 119 files through
the selected native runtime; its [qualification record](STOCKFISH_CPP_POINTER_MEMBER_QUALIFICATION.json)
binds the source and executed evidence. These native qualifications did not change the
historical database's 102 retained witnesses or, by themselves, establish an installed successor
profile. The same installed-profile mismatch was subsequently repaired and verified without
changing that provider declaration; the completed current result is recorded below. Exact byte reconstruction and zero-growth replay
do not prove C++ preprocessing, name/type resolution, executable semantics or chess strength. The separately
authenticated NNUE is an engine build input; this code-source profile does not
claim to have admitted its tensors as a model corpus.

The full acceptance report and both command outputs are retained in
[artifact stockfish-corpus-acceptance-35163097838-1](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35163097838/artifacts/10474447094),
3,016,206 bytes; ZIP SHA256
`a462210322043e84d68bf5d7ba441337f7542f2a5945ce645d464aa1559ac007`.
[Reader run 35165647185](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35165647185)
independently authenticated the artifact and evidence digests, installed activation
tuple, both readback denominators and all five zero replay deltas.

## Historical installed source replay and service

[Accepted-main run 35216221036](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35216221036)
completed at source `d2d8a3533d1000cd31f56c6fd92bc11365f0ebac`, package
`b13c3ca7dd94cb691f00371e1bfe28496f8d231405bc4f3e2daac4b023916903`.
The generated public reconciliation program passed after its finite byte envelope
was corrected. The original source profile
`2368be2a20fd01a5cdfca1f90a79b923741e521e64718ae996a70af98f4ec651`
then admitted and reconstructed all 119 official Stockfish files, 1,172,144 bytes,
including 72 C++ files. The parser declaration was unchanged for this proof.

The repair preserves canonical structural witnesses and their historical v3 receipt,
and records the current provider's authenticated execution separately. The installed
readback contains 571,626 canonical witnesses, 571,626 current execution observations,
one current v4 receipt and 239 current source-occurrence rows. Its current receipt
is `77b5a4a0b4aa5519dcd4baeb889d93d7ab04867516edbc613b216e0a6dcb89d5`;
the historical baseline is
`7153d1a91f8f7ec3b8e30b23897225b6b4c3464964316bf1bea9d92f0e928365`.

| Installed CLI boundary | Actual result |
| --- | --- |
| Admission and exact readback | 85.732859 seconds |
| Identical-provider repeat and exact readback | 55.953709 seconds |
| Both returned source files/bytes | 119 / 1,172,144 |
| Repeat Entity / Physicality / Attestation growth | 0 / 0 / 0 |
| Repeat source-occurrence / canonical-witness growth | 0 / 0 |
| Repeat execution-observation / execution-receipt growth | 0 / 0 |
| Full admission and repeat JSON outputs | Byte-identical; SHA-256 `041129ae6c375adb709ab6bfeb0d5d648e2a0a0587d865f11474ed19118a6da0` |

These are complete CLI operation timings, not isolated insertion rates or a
recorded-games benchmark. The first current-provider admission and its identical
repeat are distinct boundaries: the repeat proves no amplification. An independent
pre-first-pass global Entity/Physicality count was not captured. Actual post-admission
counts were 1,172,698 Entities, 1,172,837 Physicalities and 1,114,688 Attestations.
Canonical content, structural form and execution/source observations remain separate
identities; these counts do not imply one Physicality per Entity.

The same installed package passed persistent PostgreSQL, Unicode, Highway, cognition
and web readiness checks. `laplace-refactor-cognition.service` is enabled and
active/running in the `laplace-runner` user manager. Its health binds the selected
package and reports Unicode and Highway ready. Its verified HTTP endpoint is
`http://127.0.0.1:55434`, on the server itself. Exact installed web assets and GET
health passed there; external reachability was not tested and no external route is
claimed. PostgreSQL is the persistent `laplace_refactor` instance on port 55433,
system identifier `7682119102860556519`. Warm restart to verified service health
was 0.771293256 seconds; a cold machine boot was not tested.

[Reader run 35220249735](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35220249735)
authenticated source artifact 10495964088 (3,017,055 bytes, ZIP SHA-256
`ef5c5cb5e1defbe2c916110ec68b9ae870693933e030c17d1b4224e3b47113a4`)
and the matching service restart and HTTP artifacts. The
[compact replay qualification](STOCKFISH_EXECUTION_REPLAY_QUALIFICATION.json)
retains exact source, package, artifact, receipt, count and timing identities.
The pointer-member grammar is a subsequent provider change. Its installed
successor readback is recorded below; the historical replay proof above remains
bound to its original profile and package.


## Installed successor profile and authenticated surface

PR 402 installed source `df158f21bfecaf4045d065b10c21cd92464ce3ec`, package
`d12af51d6367e03045e12599d2195894868f8408ebe17e5b313db3032f516490`,
with the qualified pointer-member grammar. Its initial command committed the
new profile, then exceeded the 1,000-second client readback deadline. The abandoned
read-only backend was identified and cancelled; its absence was verified. That
failure is retained in main run 35231752869.

The committed profile was subsequently verified without another admission.
[Readback run 35242450404](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35242450404)
used the normal full-corpus query and parser, a server deadline and cleanup of its
own session. Every file reconstructed to the manifest's exact byte count and SHA-256.

| Installed successor observation | Actual result |
| --- | --- |
| Profile | `c6ec7000cd7d923361f0f2505c35852cc78c21205598af726706e7a3ef25ddab` |
| Files / bytes / resolved roots | 119 / 1,172,144 / 119 |
| Canonical witnesses / current execution observations | 571,627 / 571,627 |
| Current v4 execution receipts / source occurrences | 1 / 239 |
| Retained syntax error count | 95 |
| Trajectory carriers / database operations | 968,195 / 4,948 |
| Complete readback query / query plus owned-session cleanup | 8.751249 / 8.770346 seconds |
| Remaining owned backend after readback | None |

The current v4 receipt is
`d1f9cd77cda9b20f220d60644472a513e9e182ff279470e113844c0972667843`;
its historical v3 baseline for this new profile is
`3612ef294343de1761f9846cf5bbab1b606cc3f0102d59e13351ef84e17bf8fb`.
This read-only resume did not execute admission or measure replay growth. It does
not inherit the older profile's zero-growth replay claim. Actual global counts at
this observation were 1,172,710 Entities, 1,231,471 Physicalities and 1,114,927
Attestations; these are separate denominators.

[Authenticated reader 35243222289](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35243222289)
verified artifact 10505599545 (90,291 bytes; ZIP SHA-256
`ed7c878b2f576c13fe27fb4420ad472ef63173f55c234e6ee735ee74f1fcf0c4`).
The [compact successor proof](STOCKFISH_SUCCESSOR_READBACK_QUALIFICATION.json)
retains exact profile, receipt, output, count and timing identities. The 95 syntax
diagnostics remain unresolved source syntax observations; exact reconstruction
does not establish executable C++ semantics or chess strength.

The same package's authenticated surface is available through the existing
https://hart-server:8443/refactor/ route, verified from the server with hostname
and certificate validation. A remote client was not tested.
The [operator guide](COGNITION_SURFACE.md) describes the actual service,
operator bearer, loopback endpoint and retained route evidence.

## Current installed readback correction and repeat

[Main run 35257154254](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35257154254)
installed source `32f65922abfe358b9d690645781af676e8a8f575`, package
`c2e6432f85c72d390b2cfd3cc343106f4fe7a4d34238b48e2cda2bebbc9758a6`,
and completed all selected native, PostgreSQL, activation and source checks.
The readback query now avoids the demonstrated stale-statistics left-join plan
while preserving complete canonical/execution witness matching and exposing
extra same-receipt rows. All 18 actual corruption controls passed.

The unchanged current profile reconstructs all 119 files / 1,172,144 bytes and
retains 571,627 canonical witnesses, 239 current source-occurrence rows and
1,143,254 execution observations across two receipts. The measured repeat adds
zero Entities, Physicalities, Attestations, source occurrences, canonical
witnesses, execution observations or execution receipts. Complete first/repeat
CLI operations took 57.821330 / 57.228730 seconds; both JSON outputs have SHA-256
`d00a2ba185d9f2f8e519863fd32626d5e66b283eebdcf23852dccc161c3e75d0`.
These are source-admission/readback timings, not recorded chess throughput.
The [current installed proof](STOCKFISH_READBACK_JOIN_INSTALLED_20260917.md)
retains exact provenance and the unmeasured initial cross-package write boundary.

The same package passed actual anonymous HTTP 401, authenticated health 200 and
complete authenticated package-bound SSE, native cognition, OpenAI and MCP.
Use the existing https://hart-server:8443/refactor/ route described in the
[surface guide](COGNITION_SURFACE.md). Its latest server-local TLS observation
belongs to the preceding c7cd package; this package's loopback service/protocol
proof is current. Remote-client reachability was not tested.

The installed grammar remains at 95 persisted syntax diagnostics. A separate
[GNU-alias qualification](STOCKFISH_GNU_ALIAS_QUALIFICATION_20260917.md)
reconstructed the same 119 source files with 71 candidate diagnostics, preserving
an explicitly bounded recovery-leaf difference and all historical failures.
That candidate is not installed, has no finalized profile, and does not establish
executable semantics or chess strength.
