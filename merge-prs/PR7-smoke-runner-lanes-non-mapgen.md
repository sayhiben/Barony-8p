# [PR7] Smoke Runner Suite (Non-Mapgen, Python CLI)

## Ticket Metadata
- Type: Engineering
- Priority: Medium
- Epic: Multiplayer Expansion 1-15
- Risk: Low-Medium
- Status (Updated 2026-03-22): Implemented on branch; backend handshake follow-ups are tracked historically but are not a release gate
- Depends On: PR6
- Blocks: Reliable networking/combat/splitscreen regression evidence

## Progress Snapshot (2026-02-14)
- Legacy `tests/smoke/*.sh` wrappers have been removed on branch.
- Non-mapgen smoke orchestration is now argparse-based with a thin CLI entrypoint in `tests/smoke/smoke_runner.py`.
- Execution/parsing logic is split into `tests/smoke/smoke_framework/*` modules (core, lobby_remote, churn_statusfx, lan_helo_chunk, splitscreen, shared helpers).
- Shared DRY helpers were introduced for nested lane invocation, CSV I/O, process lifecycle, and lane pass/fail bookkeeping (`lane_matrix.py`).
- `tests/smoke/pyproject.toml` and `tests/smoke/.python-version` were added for tool bootstrap consistency.
- `lan-helo-chunk` internals are decomposed into focused helpers (`args`, `launch`, `runtime`, `post`, `summary`) to keep orchestration readable.

## Validation Snapshot (2026-02-26)
- `remote-combat-slot-bounds` (LAN) passed with required remote contexts (`client-ENHP`, `client-DAMI`, `client-DMGG`).
  - Artifact: `tests/smoke/artifacts/remote-combat-fix-20260226-001704`
- `save-reload-compat` passed after duck ownership encoding hardening changes.
  - Artifact: `tests/smoke/artifacts/save-reload-compat-duck-fix-20260226-002659`
- `splitscreen-cap` passed (`requested=8`, enforced cap `4`).
  - Artifact: `tests/smoke/artifacts/splitscreen-cap-duck-fix-20260226-002744`
- `inventory-fast-pass` passed all three sub-lanes (`lifecycle`, `edge`, `churn`).
  - Artifact: `tests/smoke/artifacts/inventory-fast-pass-duck-fix-20260226-002822`
- Historical backend handshake follow-ups were attempted but blocked by missing room key prerequisites in this local build/runtime context:
  - Steam: `tests/smoke/artifacts/steam-remote-combat-fix-20260226-001807` (`roomKeyFound=0`, `launchBlocked=1`)
  - EOS: `tests/smoke/artifacts/eos-remote-combat-fix-20260226-002141` (`roomKeyFound=0`, `launchBlocked=1`, historical only)

## Windows Validation Snapshot (2026-03-14)
- Framework sanity:
  - `py -3 tests/smoke/smoke_runner.py framework-self-check` passed.
  - `py -3 -m unittest discover -s tests/smoke/tests -p "test_*.py"` passed.
- Windows no-Steam lane artifacts:
  - 15p lobby / HELO / account-label coverage pass: `tests/smoke/artifacts/win-helo15-lobby-20260314-20260314-132233`
  - 4p gameplay + mapgen pass: `tests/smoke/artifacts/win-helo4-mapgen-delay2-20260314-20260314-133614`
  - 9p gameplay + mapgen pass: `tests/smoke/artifacts/win-helo9-mapgen-delay2-20260314-20260314-133709`
  - 9p gameplay + mapgen pass after v5.0.1/v5.0.2 hash-compat patch: `tests/smoke/artifacts/win-helo9-mapgen-v501compat-20260314-142002`
- Windows-specific runner caveats validated here:
  - Launch each instance with `cwd=<home>/.barony`; otherwise relative config/log paths collapse into the repo root and multi-instance signal is corrupted.
  - `--auto-start-delay=0` is too aggressive for Windows gameplay lanes and produces false `timeout-before-mapgen-samples` failures. Example failing artifacts:
    - `tests/smoke/artifacts/win-helo2-mapgen-20260314-20260314-133105`
    - `tests/smoke/artifacts/win-helo4-mapgen-20260314-20260314-132443`
- Windows asset audit:
  - `D:\SteamLibrary\steamapps\common\Barony` is internally consistent with the upstream `v5.0.1` map table, not the upstream `v5.0.2` table. Audit artifact: `tests/smoke/artifacts/win-steam-map-hash-audit-20260314-142142` (`TOTAL_FILES=1922`, `ACCEPTED_FILES=1922`, `COMPAT_HIT_FILES=19`).
  - `src/files.cpp` now keeps `v5.0.2` hashes canonical and accepts the 19 changed `v5.0.1` hashes as official compatibility values cross-platform, which removes hash-warning noise from current smoke runs and avoids treating those assets as modded.
  - Because broadly shipped asset packs still include those 19 variants, this compatibility path is treated as valid release signal rather than a blocker pending exact upstream `v5.0.2` asset certification.

## Release-Suite Follow-up (2026-03-22)
- Added a top-level `release-suite` smoke command that composes the existing high-signal lanes into `sanity`, `release`, and `full` profiles.
- Added platform wrappers:
  - `scripts/smoke/run_release_smoke_macos.sh`
  - `scripts/smoke/run_release_smoke_windows.ps1`
- `mapgen-sweep` and `mapgen-level-matrix` now emit root `summary.env` files, which lets suite-level reporting treat mapgen and non-mapgen steps consistently.
- Verification on this tooling/docs pass:
  - `python3 -m py_compile tests/smoke/smoke_runner.py tests/smoke/smoke_framework/*.py tests/smoke/tests/*.py`
  - `python3 tests/smoke/smoke_runner.py release-suite --help`
  - `python3 tests/smoke/smoke_runner.py framework-self-check`
  - `python3 -m unittest discover -s tests/smoke/tests -p 'test_*.py'`
- No new gameplay smoke artifact yet for the new suite wrapper itself; next RC validation pass should use `release-suite` so evidence lands under one artifact root with `suite_results.csv` and `release_suite_report.html`.

## Background
PR6 provides compile-gated smoke hooks in C++, but repeatable validation for networking/combat/splitscreen/save-reload needs runner tooling. The original shell-wrapper approach became high-duplication and hard to maintain. The current direction is a single Python CLI orchestrator with modular helpers.

## What and Why
Land reproducible non-mapgen smoke lanes in a unified Python framework (`smoke_runner.py`) so reviewers and maintainers can run deterministic checks with lower maintenance overhead and less parser drift.

## Scope
### In Scope
- Non-mapgen lane orchestration and parser/command wiring in `tests/smoke/smoke_runner.py` + `tests/smoke/smoke_framework/*`, including:
  - `lan-helo-chunk`
  - `helo-soak`
  - `helo-adversarial`
  - `lobby-kick-target`
  - `save-reload-compat`
  - `join-leave-churn`
  - `status-effect-queue-init`
  - `lobby-page-navigation`
  - `remote-combat-slot-bounds`
  - `lobby-slot-lock-kick-copy`
  - `splitscreen-baseline`
  - `splitscreen-cap`
- Smoke runner docs/bootstrap:
  - `tests/smoke/README.md`
  - `tests/smoke/pyproject.toml`
  - `tests/smoke/.python-version`
- Deletion of legacy wrappers/helpers:
  - `tests/smoke/run_*_smoke_mac.sh`
  - `tests/smoke/lib/common.sh`

### Out of Scope
- C++ gameplay/network source behavior changes
- Mapgen gameplay/balance policy changes
- `-smoke-mapgen-integration*` C++ plumbing work (PR8)
- Default enablement/build-policy changes

## Implementation Instructions
1. Keep `smoke_runner.py` as the single argparse CLI entrypoint; keep lane logic in `smoke_framework/*`.
2. Keep orchestration DRY:
   - use shared helpers for nested lane command construction
   - use shared CSV/summary helpers
   - use shared process lifecycle helpers
   - centralize lane result/count matrix logic in shared helpers (`lane_matrix.py`)
3. Preserve exact `key=value` token parsing semantics to avoid false positives/negatives.
4. Keep tooling dependencies stdlib-only unless maintainers explicitly request otherwise.
5. Keep this PR isolated to `tests/smoke/*` and docs/bootstrap files.
6. Finish modularization only where it materially reduces duplication (YAGNI): prioritize `join-leave-churn` and repeated row/summary assembly.

## Suggested Commit Structure
1. Remove shell wrappers + add Python smoke bootstrap files (`pyproject.toml`, `.python-version`).
2. Add/migrate non-mapgen argparse lane commands and module shims (`smoke_runner.py` + `smoke_framework/*`).
3. DRY helper consolidation (`orchestration`, `csvio`, `summary`, `lane_matrix`, shared parser args).
4. README architecture/usage updates.

## Validation Plan
- Syntax/CLI:
```bash
python3 -m py_compile tests/smoke/smoke_runner.py tests/smoke/smoke_framework/*.py
python3 tests/smoke/smoke_runner.py --help
python3 tests/smoke/smoke_runner.py helo-soak --help
python3 tests/smoke/smoke_runner.py join-leave-churn --help
```
- Lane sanity (short):
  - one short 4p non-mapgen lane
  - one short 15p non-mapgen lane
- Artifact checks:
  - `summary.env` emitted
  - lane CSV emitted
  - optional aggregate HTML generated where expected

## Acceptance Criteria
- [ ] No legacy smoke `.sh` wrappers remain in `tests/smoke/`.
- [ ] Non-mapgen lanes run through argparse subcommands in `smoke_runner.py` (with lane logic in `smoke_framework/*`).
- [ ] Shared helper usage removes repeated command/CSV/parser boilerplate and lane result/count branching.
- [ ] `tests/smoke/README.md` reflects Python CLI usage.
- [ ] No C++ gameplay/network source files are modified by this PR.
- [ ] No mapgen balance policy changes are included.

## Branch Readiness Snapshot (2026-02-14)
- [x] Legacy smoke shell wrappers removed.
- [x] argparse CLI entrypoint in place.
- [x] Non-mapgen lane module split established under `smoke_framework/*`.
- [x] Shared lane-result helpers introduced and adopted in core/lobby/churn paths.
- [ ] Final pass: split remaining large non-mapgen lane internals (`join-leave-churn`) and trim vestigial helpers/tests before cutting PR.

## Remaining Work Before PR Cut
1. Decompose `join-leave-churn` internals in `tests/smoke/smoke_framework/churn_statusfx_lane.py`:
   - slot lifecycle helpers (launch/stop/wait)
   - churn cycle execution loop
   - ready-sync assertion and CSV row helpers
   - summary payload assembly helper
2. Run a vestigial non-mapgen lane/helper audit and remove superseded code paths.
3. Add a small smoke framework self-check path (or equivalent command-level sanity) for module/parser wiring regressions.
4. Run final extraction validation pack:
   - `python3 -m py_compile tests/smoke/smoke_runner.py tests/smoke/smoke_framework/*.py`
   - `python3 tests/smoke/smoke_runner.py --help`
   - `python3 tests/smoke/smoke_runner.py join-leave-churn --help`
   - one short 4p lane and one short 15p non-mapgen lane
5. Freeze PR7 candidate diff to non-mapgen `tests/smoke/*` and docs/bootstrap only.

## Review Focus
- CLI consistency and subcommand ergonomics.
- DRY helper boundaries and readability.
- Parser correctness for similarly named keys.
- Diff isolation from C++ runtime behavior.

## Rollback Strategy
Revert `tests/smoke/*` tooling/docs changes only; runtime C++ behavior remains unaffected.

## Extraction Plan (from PR #940 / 8p-mod / current branch)
- Extract only `tests/smoke/*` non-mapgen runner/doc/bootstrap deltas and shell-wrapper deletions.
- For mixed smoke tooling history, isolate non-mapgen hunks in `smoke_runner.py` and `smoke_framework/*` that do not alter mapgen lane behavior.
- Keep mapgen-lane-specific behavior changes for PR8.
