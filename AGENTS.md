# Repository Guidelines

## Project Structure & Module Organization
The project is CMake-based with the root `CMakeLists.txt` orchestrating all targets. Core gameplay and editor code lives in `src/`, with major subsystems split into `src/interface/`, `src/magic/`, `src/ui/`, `src/engine/`, and `src/imgui/`. Runtime text and localization assets are in `lang/`. CI helper scripts are in `ci/`. Platform-specific project files are under `VS/`, `VS.2015/`, and `xcode/`. Start with `README.md`, `INSTALL.md`, and `CONTRIBUTING.md` for workflow context.

## Build, Test, and Development Commands
Use out-of-source builds:

```bash
mkdir -p build && cd build
cmake ..
cmake --build . -- -j
```

Common build variants:

- `cmake -DCMAKE_BUILD_TYPE=Release -DFMOD_ENABLED=ON ..` builds release with FMOD.
- `cmake -DFMOD_ENABLED=OFF ..` builds without FMOD.

CI-like Linux scripts (require CI secrets and packaged dependencies):

- `cd ci && ./build-linux_fmod_steam.sh`
- `cd ci && ./build-linux_fmod_steam_eos-barony.sh`

After building, run targets from the build directory (for example `./barony` or `./editor`).

## Local macOS Run (Steam Assets)
For local gameplay/smoke validation on macOS, use the built binary but run it from the Steam app bundle so assets/config paths match normal runtime behavior.

1. Build:

```bash
cmake -S . -B build-mac -G Ninja -DFMOD_ENABLED=OFF
cmake --build build-mac -j8
```

2. Copy the built executable into the Steam install (make a backup first):

```bash
src="$PWD/build-mac/barony.app/Contents/MacOS/barony"
dstdir="$HOME/Library/Application Support/Steam/steamapps/common/Barony/Barony.app/Contents/MacOS"
cp "$dstdir/Barony" "$dstdir/Barony.backup-$(date +%Y%m%d-%H%M%S)"
cp "$src" "$dstdir/Barony"
chmod +x "$dstdir/Barony"
```

3. Run from the Steam path:

```bash
"$HOME/Library/Application Support/Steam/steamapps/common/Barony/Barony.app/Contents/MacOS/Barony" -windowed -size=1280x720
```

## Coding Style & Naming Conventions
Follow surrounding style in touched files; this codebase mixes legacy C/C++ patterns. Avoid broad formatting-only edits. Tabs are common in older files, while newer edits may use spaces; preserve local consistency. File naming is mostly subsystem-based lowercase (examples: `act*.cpp`, `monster_*.cpp`). Use `PascalCase` for types/classes (for example `Entity`) and uppercase names for macros/constants.

## Testing Guidelines
There is no dedicated unit-test suite in this repository. Required validation is:

- Build success for affected targets (`barony`, `editor` when relevant).
- Manual smoke test of the changed flow (menu/load/gameplay/editor path you touched).
- Keep GitHub Actions Linux build checks green for PRs.
- For multiplayer-expansion work, update `/Users/sayhiben/dev/Barony-8p/AGENTS.md` and the relevant `/Users/sayhiben/dev/Barony-8p/merge-prs/PR*.md` ticket inline as progress happens (checklist state + artifact paths + notable caveats).

## Commit & Pull Request Guidelines
Create a topic branch per change. For bugfix work, target `master` (per `README.md`). Keep commits focused and message subjects short, imperative, and specific (recent history includes messages like `update hash` and `fix one who knocks achievement when parrying`). In PRs, include: what changed, why, test steps/results, and linked issues. Add screenshots for visible UI/editor changes.

## Security & Configuration Tips
Do not commit secrets or environment tokens. CI scripts rely on `DEPENDENCIES_ZIP_KEY` and `DEPENDENCIES_ZIP_IV`; provide them via environment variables only.

## Codex Sandbox / Permissions
When running in Codex with sandboxing, ask for sandbox breakout/escalation permission before running commands that may require access outside the sandbox or external services.

- Common examples: `git ...`, `gh ...`, Steam app binary runs, and other commands that touch restricted paths/resources.
- If a command is blocked by sandboxing, rerun with escalation rather than changing the intended workflow.
- If launches fail with `Abort trap: 6` during smoke runs, treat it as a likely sandbox restriction signal and rerun with escalation.

## Multiplayer Expansion (PR 940) Working Notes
- Expansion target is `MAXPLAYERS=15` (not 16). Preserve nibble-packed ownership assumptions unless a deliberate encoding refactor is planned.
- Keep smoke instrumentation isolated to `/Users/sayhiben/dev/Barony-8p/src/smoke/SmokeHooks*.cpp` and `/Users/sayhiben/dev/Barony-8p/src/smoke/SmokeTestHooks.hpp` with minimal call sites in gameplay/UI/network files.
- Keep headless mapgen integration plumbing (`-smoke-mapgen-integration*` parsing/validation/runner) in smoke hook implementation files; `src/game.cpp` should stay wiring-only for those options.
- Avoid adding ad-hoc smoke utility logic directly in core gameplay files; prefer hook APIs declared in `SmokeTestHooks.hpp` and keep base-game paths clean.
- Preferred local validation path is local build binary + Steam assets datadir (`--app .../build-mac/.../barony --datadir .../Barony.app/Contents/Resources`) instead of replacing the Steam executable.
- After long or high-instance smoke runs, clean generated cache bloat (especially `models.cache` under smoke artifact homes) while preserving logs/artifacts needed for debugging.
- If host performance degrades during smoke campaigns, check for lingering `smoke_runner.py mapgen-sweep`, `smoke_runner.py lan-helo-chunk`, and `barony` processes; terminate stale runs before launching new lanes.
- Known intermittent issue: churn/rejoin can show transient `lobby full` / join retries (`error code 16`). Track with artifacts and summaries, and avoid conflating it with unrelated feature-lane pass/fail unless assertions require it.
- Add and maintain compile-time gating for smoke hooks/call sites so smoke instrumentation compiles or executes only when a dedicated smoke-test flag is enabled.
- Smoke validation requires a smoke-enabled build (`-DBARONY_SMOKE_TESTS=ON`); if expected `[SMOKE]` logs are missing, verify generated config/build mode and rebuild the smoke target before rerunning tests.
- Keep generated `Config.hpp` build-local on Windows; writing it into `src/` cross-contaminates smoke/non-smoke build trees and can make OFF builds link smoke hooks by accident.
- Windows smoke launches must run with `cwd` set to the per-instance `.barony` home; otherwise relative config/log paths collapse back into the repo root and lane signal becomes unusable.
- Fresh per-instance smoke homes can stall in intro/title flow; ensure smoke homes are pre-seeded with profile data (`skipintro=true`, `mods=[]`, and compiled books cache) so autopilot reaches lobby/gameplay deterministically.
- Local splitscreen is a legacy path and should stay hard-capped at 4 players; retain dedicated smoke coverage for `/splitscreen > 4` clamp behavior and over-cap leakage checks.
- When parsing smoke status lines with similarly named keys (for example `connected` vs `over_cap_connected`), parse exact `key=value` tokens to avoid false negatives and lane hangs.
- During style/contribution cleanup, treat `#ifdef BARONY_SMOKE_TESTS` guards around smoke-hook callsites as an acceptable and idiomatic exception.
- Windows LAN gameplay lanes are timing-sensitive if `--auto-start-delay=0`; use the default `2` second delay (or higher) for reliable `GAMESTART`/`MAPGEN` assertions.
- Preferred balancing loop for mapgen tuning: hook-owned in-process integration preflight (`levels=1,7,16,33`, fixed seed) -> single-runtime matrix confirmation -> runs=5 volatility gate -> full-lobby confirmation.

### Validation Summary (2026-02-12)
- Overall expansion status is near-finish: core LAN networking validation is green (HELO correctness, adversarial fail modes, soak/churn, high-slot regression lanes).
- Completed/green lanes include: save/reload owner-encoding sweep (`1..15`), lobby regression lanes (kick-target, slot-lock/copy, page navigation), remote-combat slot bounds, local splitscreen baseline, and `/splitscreen > 4` cap clamp.
- Steam backend handshake was validated for host-room/key flow; local same-account multi-instance joins remain a known Steam limitation.
- EOS-specific validation is intentionally not a release gate for this mod release; Epic players can use the matching Steam or NoDRM package.
- Known intermittent issue remains: churn/rejoin can hit transient `lobby full` / `error code 16` retries before recovery; track with artifacts and do not conflate with unrelated lane failures.
- Smoke compile/runtime gating is in place (`BARONY_SMOKE_TESTS`), and the preferred local lane path is local build binary + Steam `--datadir` assets.

### Validation Addendum (2026-02-26)
- Implemented canonical duck ownership/color encoding for Hermit ducks (4 color variants, owner-safe at `MAXPLAYERS=15`) and removed duck-owner/color decode dependence on `items[TOOL_DUCK].variations`.
- Added runtime compatibility warning when `tool_duck` variations are below canonical span (`MAXPLAYERS * 4`); observed warning with Steam datadir (`variations=16`, canonical span `60`).
- Enemy HP bar forwarding now uses remote-slot/disconnect/local-player guards only; it no longer hard-blocks send on `net_clients[].host/port` zero values.
- LAN remote-combat validation passed with `client-ENHP` and `client-DAMI` contexts present.
  - Artifact: `tests/smoke/artifacts/remote-combat-fix-20260226-001704`
- Save/reload owner-encoding compatibility lane passed after duck encoding changes.
  - Artifact: `tests/smoke/artifacts/save-reload-compat-duck-fix-20260226-002659`
- Regression lanes passed:
  - Splitscreen cap clamp (`/splitscreen 8 -> 4`): `tests/smoke/artifacts/splitscreen-cap-duck-fix-20260226-002744`
  - Inventory fast-pass (lifecycle/edge/churn): `tests/smoke/artifacts/inventory-fast-pass-duck-fix-20260226-002822`
- Historical backend handshake follow-up lanes were attempted but could not enter room-key handshake in this local build context (no room key captured, launch prerequisites blocked).
  - Steam artifact: `tests/smoke/artifacts/steam-remote-combat-fix-20260226-001807`
  - EOS artifact: `tests/smoke/artifacts/eos-remote-combat-fix-20260226-002141` (tracked for history only; not a release gate)

### Windows Validation Snapshot (2026-03-14)
- VS2022 x64 Windows release build (`build-vs2022-x64`) now coexists cleanly with a smoke build (`build-vs2022-x64-smoke-nosteam`) after moving generated `Config.hpp` to the build directory.
- Windows no-Steam smoke passes recorded at:
  - `tests/smoke/artifacts/win-helo15-lobby-20260314-20260314-132233` (15p lobby / HELO / account-label coverage)
  - `tests/smoke/artifacts/win-helo4-mapgen-delay2-20260314-20260314-133614` (4p gameplay + mapgen)
  - `tests/smoke/artifacts/win-helo9-mapgen-delay2-20260314-20260314-133709` (9p gameplay + mapgen)
  - `tests/smoke/artifacts/win-helo9-mapgen-v501compat-20260314-142002` (9p gameplay + mapgen after v5.0.1/v5.0.2 hash-compat patch)
- Windows false-fail examples with `--auto-start-delay=0`:
  - `tests/smoke/artifacts/win-helo2-mapgen-20260314-20260314-133105`
  - `tests/smoke/artifacts/win-helo4-mapgen-20260314-20260314-132443`
- Local Windows Steam install (`appmanifest_371970.acf`: `buildid=21759608`, `LastUpdated=2026-02-04 19:12:45 -08:00`) contains a fully self-consistent v5.0.1 map set: all 1922 `maps/*.lmp` files hash-match the upstream `v5.0.1` table, and exactly 19 files differ from the upstream `v5.0.2` table.
- Keep `v5.0.2` hashes canonical in `src/files.cpp`, but accept the 19 changed `v5.0.1` hashes as official compatibility values cross-platform until upstream asset packs catch up. Full audit artifact: `tests/smoke/artifacts/win-steam-map-hash-audit-20260314-142142` (`ACCEPTED_FILES=1922`, `COMPAT_HIT_FILES=19`).
- Because the broadly distributed asset packs still include those v5.0.1-era variants, these runs are treated as valid runtime-stability and compatibility signal rather than a release blocker pending exact upstream v5.0.2 asset certification.
- Windows overlay release artifacts were packaged from fresh full-feature build trees with `scripts/mod_release/package_windows_release.ps1`:
  - `release-artifacts/barony-8p-windows-steam-20260314-195941.zip`
  - `release-artifacts/barony-8p-windows-nodrm-20260314-195941.zip`

### Validation Addendum (2026-03-21)
- Implemented authoritative level-load mapgen metadata for network clients:
  - host now freezes a connected-player slot mask for the level load, computes a final tile checksum after generation, and appends both to `LVLC` / `LVLR`
  - clients now consume that authoritative mask during map scaling/spawn filtering and compare their post-load tile checksum against the host value
- Added a shared tile checksum helper over width/height/skybox/flags/tile layers and a client warning path when host/local tile checksums disagree.
- Implemented automatic checksum-mismatch recovery for level-load map geometry:
  - client now requests an authoritative host snapshot on checksum failure
  - host streams map geometry snapshot chunks (`name/author/filename`, `width/height/skybox`, flags, tiles, tile attributes) over reliable packets
  - client applies the snapshot, rebuilds pathing/chunks, and confirms the recovered checksum
- Extended level-load parity checks beyond raw tiles:
  - the shared tile checksum now also covers `tileAttributes`, so slippery/slow/grease/treasure-room drift is detected together with wall/layout drift
  - `LVLC` / `LVLR` level-load metadata now carries a second initial entity checksum covering the post-load entity scene before clients discard `NOUPDATE` placeholders
  - clients log an `entity sync mismatch` when initial placements/content diverge even if final geometry still matches
- Smoke-only connected-player overrides now remain available for host/single-runtime mapgen lanes, but network clients no longer override the host's authoritative level-load mask.
- Targeted 2-instance LAN repro passed with a client-only smoke override of `5` connected players:
  - host authoritative inputs: `players=2 mask=0x0003 checksum=2380154547`
  - client received the same authoritative inputs before loading and generated the same `The Mines` floor (`players=2`, identical room/economy summary)
  - artifact: `tests/smoke/artifacts/map-desync-authoritative-launch-20260321-210316`
  - summary: `tests/smoke/artifacts/map-desync-authoritative-launch-20260321-210316/summary.env`
- Targeted 2-instance LAN recovery lane passed with a client-only forced checksum mismatch:
  - client intentionally flipped one tile after local load, logged `local_checksum=1494320743` vs host `2380154547`, requested host recovery, and applied transfer `1`
  - host streamed an authoritative `19621` byte snapshot in `11` reliable chunks; client rebuilt geometry and finished with checksum `2380154547`
  - artifact: `tests/smoke/artifacts/map-desync-snapshot-recovery-20260321-213455`
  - summary: `tests/smoke/artifacts/map-desync-snapshot-recovery-20260321-213455/summary.env`
- Post-hardening regression checks passed after adding tile-attribute coverage and the initial entity checksum:
  - smoke-enabled target rebuild succeeded: `cmake --build build-mac-smoke -j8 --target barony`
  - 2-instance forced-recovery lane remained green on the updated packet/checksum format
  - artifact: `tests/smoke/artifacts/map-desync-entity-checksum-20260321-215354`
  - summary: `tests/smoke/artifacts/map-desync-entity-checksum-20260321-215354/summary.env`
- Broader smoke pass on the same branch stayed green for the stable/high-signal lanes:
  - 2-instance baseline dungeon transition: `tests/smoke/artifacts/level-sync-baseline-2p-20260321-221439`
  - 4-instance dungeon/mapgen transition with a 3-second lobby settle delay: `tests/smoke/artifacts/level-sync-4p-mapgen-delay3-20260321-221847`
  - 3-run 4-instance HELO soak: `tests/smoke/artifacts/helo-soak-level-sync-20260321-221956`
  - HELO adversarial matrix (reverse/even-odd/duplicate-first expected-pass, drop-last/duplicate-conflict-first expected-fail): `tests/smoke/artifacts/helo-adversarial-level-sync-20260321-222152`
  - standard 6-instance join/leave churn with ready-sync assertions: `tests/smoke/artifacts/join-leave-churn-standard-20260321-222825`
  - save/reload owner-encoding compatibility: `tests/smoke/artifacts/save-reload-compat-level-sync-20260321-223036`
- Follow-up smoke gating closed the zero-delay lobby-start race:
  - host smoke auto-start now waits for `connected` and `joined` parity, where `joined` means each remote client has actually entered the lobby (`JACK`-ack path) before auto-start fires
  - 4-instance zero-delay auto-start lane now passes cleanly with all clients loading `start.lmp`: `tests/smoke/artifacts/level-sync-4p-mapgen-delay0-smokegate-20260321-230540`
  - summary: `tests/smoke/artifacts/level-sync-4p-mapgen-delay0-smokegate-20260321-230540/summary.env`
- Same-level reload follow-up is now green on a networked lane:
  - 2-instance procedural reload/regeneration lane with `mapgen-reload-same-level=1` passed and matched both requested reload seeds (`100100`, `100101`)
  - artifact: `tests/smoke/artifacts/reload-procedural-level-sync-2p-20260321-230638`
  - summary: `tests/smoke/artifacts/reload-procedural-level-sync-2p-20260321-230638/summary.env`
- Experimental lanes that were not counted toward confidence:
  - churn-with-gameplay auto-start drifted into midgame-rejoin retries instead of standard lobby churn: `tests/smoke/artifacts/join-leave-churn-level-sync-20260321-222429`
  - remote-combat lane aborted before gameplay because host launch never completed: `tests/smoke/artifacts/remote-combat-level-sync-20260321-223515`
- Current caveat: the fallback is geometry-scoped. It guarantees tile/flag/tile-attribute parity after load, but it is not a full host-authoritative level bootstrap for static entity/content drift.
- Cleanup follow-up (2026-03-22):
  - extracted level-load authority and recovery ownership into `src/level_load_sync.cpp/.hpp`, so `game.cpp`, `net.cpp`, and `maps.cpp` no longer carry the packet-layout and snapshot-recovery state directly
  - split map snapshot application so `files.cpp` now owns only geometry-data copy plus HDR/lightmap/minimap cache reset, while `level_load_sync.cpp` owns vismap/shoparea buffer replacement during recovery
  - build verification passed: `cmake --build build-mac -j8 --target barony editor`
  - no new smoke artifact for this cleanup-only pass; residual caveat remains that HDR/lightmap reset still lives in `files.cpp` because the ambience console variables are anchored there today
- Release smoke suite follow-up (2026-03-22):
  - added `release-suite` orchestration in `tests/smoke/smoke_runner.py` plus `tests/smoke/smoke_framework/release_suite_lane.py` / `release_suite_parser.py`
  - suite profiles are now `sanity`, `release`, and `full`; `release` is the intended cross-platform RC gate, while `full` adds soak, kick-target, and full-lobby mapgen sweep coverage
  - added operator wrappers at `scripts/smoke/run_release_smoke_macos.sh` and `scripts/smoke/run_release_smoke_windows.ps1`
  - mapgen suite lanes now emit top-level `summary.env` files, so suite-level rollups can treat mapgen and non-mapgen steps consistently
  - verification on this tooling pass: `python3 -m py_compile tests/smoke/smoke_runner.py tests/smoke/smoke_framework/*.py tests/smoke/tests/*.py`, `python3 tests/smoke/smoke_runner.py release-suite --help`, `python3 tests/smoke/smoke_runner.py framework-self-check`, `python3 -m unittest discover -s tests/smoke/tests -p 'test_*.py'`
  - no new gameplay smoke artifact yet for the wrapper/suite layer itself; next artifact-bearing pass should use the new wrapper entrypoints so release evidence lands under one root suite directory
- Windows release RC follow-up (2026-03-24):
  - Windows `release` smoke suite passed end-to-end from `build-vs2022-x64-smoke-nosteam\Release\barony.exe` against `D:\SteamLibrary\steamapps\common\Barony`.
  - Root artifact: `tests/smoke/artifacts/release-suite-windows-release-20260324-131100`
    - `summary.env`: `RESULT=pass`, `PASS_STEPS=15`, `FAIL_STEPS=0`
    - includes `suite_results.csv` and `release_suite_report.html`
  - Fixed a Windows-only in-process mapgen preflight regression in `src/smoke/SmokeHooksMapgen.cpp`: the control-file override now refreshes from `SDL_getenv()` instead of freezing an empty `std::getenv()` snapshot before `SDL_setenv()` updates land.
  - Targeted verification artifact for that hook fix: `tests/smoke/artifacts/mapgen-integration-preflight-fix2-20260324-131000`
    - `mapgen_players_observed` now matches `1..15` with zero failing rows
  - Fresh Windows overlay release artifacts were packaged from rebuilt release trees:
    - `release-artifacts/barony-8p-windows-steam-20260324-134215.zip`
    - `release-artifacts/barony-8p-windows-nodrm-20260324-134215.zip`
  - Post-package Windows install validation now exists at `scripts/mod_release/validate_windows_install.ps1` and passed against those freshly packaged zips:
    - root artifact: `tests/smoke/artifacts/windows-install-validation-20260324-shipcheck60`
    - validated flow: copy local Barony install -> extract package -> overlay files per `docs/mod_release/README.txt` -> verify `SHA256SUMS.txt` before/after overlay -> launch installed `barony.exe` for a 60-second startup window
    - Steam package passed against a copied local Steam install; NoDRM package passed against a sanitized copy of the same install with Steam-only root files removed before overlay
    - current caveat: both installed copies created a live window and remained up for the full validation window with fresh logs, but first-run initialization did not reach `LoadMap ... mainmenu3.lmp` within 60 seconds in this environment
  - Local host caveat: MSBuild/vcpkg `applocal.ps1` still targets a stale WindowsApps PowerShell path (`7.5.4.0`), so Windows build commands currently exit nonzero after link with code `3` even though `barony.exe` / `editor.exe` are produced successfully.
  - Local smoke-build caveat: if that stale `applocal` step leaves DLL staging incomplete, smoke app launches can fail with `0xC0000135` and empty per-instance logs until the missing runtime DLLs are restaged or the `pwsh.exe` path is fixed.

### Balancing Lessons and Guardrails
- Hard rule: preserve `1..4p` gameplay parity; all new mapgen balancing logic must be overflow-only (`connectedPlayers > 4`).
- Use sweep confidence policy consistently: `runs=3` for directional iteration, `runs=5` for volatility gate/promotion decisions.
- Promotion requires both simulated and full-lobby confirmation: integration/single-runtime can pass while full-lobby diverges (observed in pass15g level-1 comparison), so do not skip full-lobby gates.
- Keep mapgen tuning explainable and measurable via telemetry (`rooms`, `monsters`, `gold`, `items`, `decorations`, food metrics, value metrics, seed/regeneration diagnostics).
- Current target bands for `p15 vs p4` reviews:
  - rooms `1.62x-1.75x`
  - monsters `1.38x-1.46x`
  - monsters/room `0.82x-0.92x`
  - gold/player `0.70x-0.80x`
  - items/player `0.70x-0.80x`
  - food/player `0.65x-0.78x`
  - decorations `1.85x-2.25x`, blocking share `<= 45%`
- Maintain integration ownership boundaries: integration parser/validator/runner belong in smoke hook implementation files (`src/smoke/SmokeHooksMapgen.cpp`); `src/game.cpp` remains wiring-only.
- Keep operational hygiene between long runs: prune generated `models.cache`, and terminate stale `smoke_runner.py` lane processes plus `barony` before relaunch.

### Technical Commands and Config Reference
- Windows overlay packaging after staging fresh release builds:
```powershell
powershell -ExecutionPolicy Bypass -File scripts\mod_release\package_windows_release.ps1 `
  -Label 20260314-195941 `
  -SteamBuildDir build-vs2022-x64-release-steam `
  -NoDrmBuildDir build-vs2022-x64-release-nodrm
```
- Windows post-package install validation:
```powershell
powershell -ExecutionPolicy Bypass -File scripts\mod_release\validate_windows_install.ps1 `
  -Label 20260324-shipcheck60 `
  -LaunchSeconds 60 `
  -InputIdleSeconds 20
```
- Smoke-enabled build (required for `[SMOKE]` hooks/logs):
```bash
cmake -S . -B build-mac-smoke -G Ninja -DFMOD_ENABLED=OFF -DBARONY_SMOKE_TESTS=ON
cmake --build build-mac-smoke -j8 --target barony
```
- Preferred release-suite entrypoints:
```bash
./scripts/smoke/run_release_smoke_macos.sh --profile release
python3 tests/smoke/smoke_runner.py release-suite --app build-mac-smoke/barony.app/Contents/MacOS/Barony --datadir "$HOME/Library/Application Support/Steam/steamapps/common/Barony/Barony.app/Contents/Resources" --profile release
```
```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke\run_release_smoke_windows.ps1 -Profile release
```
- Preferred mapgen tuning loop commands:
  - Fast in-process integration preflight (`runs=2`):
```bash
USER_HOME="$HOME"
OUT="tests/smoke/artifacts/mapgen-integration-preflight-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT/home"
HOME="$OUT/home" build-mac-smoke/barony.app/Contents/MacOS/barony \
  -windowed -size=1280x720 -nosound \
  -datadir="$USER_HOME/Library/Application Support/Steam/steamapps/common/Barony/Barony.app/Contents/Resources" \
  -smoke-mapgen-integration \
  -smoke-mapgen-integration-csv="$OUT/mapgen_level_matrix.csv" \
  -smoke-mapgen-integration-levels=1,7,16,33 \
  -smoke-mapgen-integration-min-players=1 \
  -smoke-mapgen-integration-max-players=15 \
  -smoke-mapgen-integration-runs=2
```
  - Volatility gate (`runs=5`): same command with `-smoke-mapgen-integration-runs=5`.
  - Low-player parity guard (`2..4p`): same command with `-smoke-mapgen-integration-min-players=2`, `-smoke-mapgen-integration-max-players=4`, `-smoke-mapgen-integration-runs=5`.
  - Scripted single-runtime matrix lane (`simulate-mapgen-players=1`, `runs=2/5`):
```bash
OUT="tests/smoke/artifacts/mapgen-level-matrix-passNN-$(date +%Y%m%d-%H%M%S)"
python3 tests/smoke/smoke_runner.py mapgen-level-matrix \
  --app "build-mac-smoke/barony.app/Contents/MacOS/barony" \
  --datadir "$HOME/Library/Application Support/Steam/steamapps/common/Barony/Barony.app/Contents/Resources" \
  --levels "1,7,16,33" \
  --min-players 1 --max-players 15 --runs-per-player 2 \
  --simulate-mapgen-players 1 --inprocess-sim-batch 1 --inprocess-player-sweep 1 \
  --mapgen-reload-same-level 1 \
  --outdir "$OUT"
```
  - Full-lobby confirmation (`simulate-mapgen-players=0`):
```bash
python3 tests/smoke/smoke_runner.py mapgen-sweep \
  --min-players 1 --max-players 15 --runs-per-player 5 \
  --simulate-mapgen-players 0 --auto-enter-dungeon 1 \
  --outdir "tests/smoke/artifacts/mapgen-full-posttune-$(date +%Y%m%d-%H%M%S)"
```
- Additional validation lane commands:
  - Same-level mapgen regeneration sanity lane (procedural floor):
```bash
python3 tests/smoke/smoke_runner.py lan-helo-chunk \
  --instances 1 --auto-start 1 --auto-start-delay 0 \
  --auto-enter-dungeon 1 --auto-enter-dungeon-delay 3 \
  --mapgen-samples 3 --require-mapgen 1 \
  --mapgen-reload-same-level 1 --mapgen-reload-seed-base 100100 \
  --start-floor 1 \
  --outdir "tests/smoke/artifacts/reload-procedural-verify-$(date +%Y%m%d-%H%M%S)"
```
  - Churn/rejoin retry investigation (`error code 16`):
```bash
python3 tests/smoke/smoke_runner.py join-leave-churn \
  --instances 8 --churn-cycles 3 --churn-count 2 \
  --force-chunk 1 --chunk-payload-max 200 \
  --auto-ready 1 --trace-ready-sync 1 --require-ready-sync 1 \
  --trace-join-rejects 1 \
  --outdir "tests/smoke/artifacts/churn-retry-investigation-$(date +%Y%m%d-%H%M%S)"
```
  - Optional historical Steam handshake lane:
```bash
python3 tests/smoke/smoke_runner.py lan-helo-chunk \
  --network-backend steam --instances 2 \
  --force-chunk 1 --chunk-payload-max 200 --timeout 360 \
  --outdir "tests/smoke/artifacts/steam-handshake-multiacct-$(date +%Y%m%d-%H%M%S)"
```
- Technical config/guardrails:
  - Use procedural floors for balancing sweeps (`1,7,16,33`); fixed/story floors may report `MAPGEN_WAIT_REASON=reload-complete-no-mapgen-samples`.
  - Keep smoke-run homes isolated (`HOME="$OUT/home"`) to avoid cross-run config/data leakage.
  - Integration seed root is now auto-generated per invocation; do not rely on the removed `-smoke-mapgen-integration-base-seed` override.
  - Maintain compile-time/runtime gating with `BARONY_SMOKE_TESTS`; keep non-smoke gameplay paths clean.
  - Keep integration parser/validator/runner in `src/smoke/SmokeHooksMapgen.cpp` (API in `src/smoke/SmokeTestHooks.hpp`); keep `src/game.cpp` wiring-only for `-smoke-mapgen-integration*`.
- Post-run hygiene commands:
```bash
find tests/smoke/artifacts -type f -name models.cache -delete
ps -Ao pid,ppid,etime,command | rg "smoke_runner.py (mapgen-level-matrix|mapgen-sweep|lan-helo-chunk)|barony.app/Contents/MacOS/barony"
```
