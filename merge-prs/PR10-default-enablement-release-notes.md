# [PR10] Default Enablement Flip and Release Notes

## Ticket Metadata
- Type: Engineering
- Priority: High
- Epic: Multiplayer Expansion 1-15
- Risk: Medium
- Status (Updated 2026-03-22): In progress on branch; default is already ON and release-doc/package cleanup is the remaining work
- Depends On: PR1-PR9 complete and gated
- Blocks: Release readiness

## Background
Through PR9, 1-15 support is intentionally scaffolded behind defaults to reduce blast radius. The final step is enabling the new path by default only after all stability and balance gates are satisfied.

## What and Why
Keep `BARONY_SUPER_MULTIPLAYER` default ON and finish the release-facing docs/package cleanup once the remaining release-hygiene work is green.

## Release Note Addendum (User-Facing Bugfix Callout)
When this stack is released, include a short multiplayer bugfix note:
- Hermit duck ownership now uses canonical 15-player-safe encoding and no longer aliases through asset variation counts.
- Added runtime warning for mod/datadir mismatches where duck asset variation count is below canonical span (`MAXPLAYERS * 4`), so users can self-diagnose mixed-version installs.
- Remote clients now reliably receive enemy HP bar/damage indicator updates in LAN sessions even when peer host/port metadata is zeroed in P2P-style flows.

## Scope
### In Scope
- Release-facing docs/package cleanup around the already-enabled `BARONY_SUPER_MULTIPLAYER` default
- Canonical Steam Workshop copy in `STEAM-WORKSHOP.txt`
- Packaged changelogs (`mod-changelog.txt` plus detailed release notes)

### Out of Scope
- Any gameplay/network/mapgen logic changes
- Smoke framework/script changes
- Additional refactors

## Implementation Instructions
1. Confirm all prior PR gates are complete and documented.
2. Keep the current `BARONY_SUPER_MULTIPLAYER` default/value as-is unless a regression forces rollback.
3. Update docs with short release notes:
   - 1-15 support now default-enabled.
   - Splitscreen remains capped at 4.
   - Smoke flags remain optional and compile-gated.
4. Keep the Steam Workshop source single-sourced as `STEAM-WORKSHOP.txt`; do not maintain a parallel Markdown version.
5. Ensure release packaging scripts stage the README plus changelog assets.

## Suggested Commit Structure
1. Docs/package cleanup commit.
2. Optional tiny follow-up if any release-note wording still needs adjustment after packaging validation.

## Validation Plan
- Full CI pass.
- Smoke sanity:
  - Baseline 4p lane.
  - 15p lane.
- Manual startup sanity with default settings.
- Operator packaging sanity:
  - Steam and NoDRM Windows overlay zips contain the built executables, adjacent runtime DLLs, the mod README, packaged changelog files, and `SHA256SUMS.txt`.
- Post-package install validation:
  - Follow the packaged README flow on Windows against the freshly built zip(s): copy a Barony install, extract the matching package, overlay files, verify `SHA256SUMS.txt`, and launch the installed executable long enough to catch startup/runtime dependency issues.

## Notes (2026-03-14)
- Windows overlay packaging helper now exists at `scripts/mod_release/package_windows_release.ps1`.
- Latest local artifacts:
  - `release-artifacts/barony-8p-windows-steam-20260314-195941.zip`
  - `release-artifacts/barony-8p-windows-nodrm-20260314-195941.zip`

## Notes (2026-03-15)
- Added a concise packaged `docs/mod_release/mod-changelog.txt` alongside the
  more detailed `docs/mod_release/changelog_v5.0.2.md`.

## Notes (2026-03-22)
- The duplicate Markdown workshop draft was removed; `STEAM-WORKSHOP.txt` is now the single source of truth for Steam markup copy.
- EOS-specific validation is intentionally de-scoped as a release gate; Epic players can use the matching Steam or NoDRM package.
- The official-map compatibility path remains canonical-v5.0.2-first while accepting the 19 official v5.0.1-era hashes until broadly distributed asset packs catch up.
- Cross-platform release-smoke entrypoints now exist at `scripts/smoke/run_release_smoke_macos.sh` and `scripts/smoke/run_release_smoke_windows.ps1`; use the `release` profile for RC gating and archive the root suite artifact directory with its `summary.env`, `suite_results.csv`, and `release_suite_report.html`.

## Notes (2026-03-24)
- Windows RC smoke suite passed with the intended `release` profile:
  - Root artifact: `tests/smoke/artifacts/release-suite-windows-release-20260324-131100`
  - `summary.env`: `RESULT=pass`, `PASS_STEPS=15`, `FAIL_STEPS=0`
- The prior Windows-only `mapgen-integration-preflight` false fail was fixed in smoke plumbing (`src/smoke/SmokeHooksMapgen.cpp`) and verified by the targeted artifact:
  - `tests/smoke/artifacts/mapgen-integration-preflight-fix2-20260324-131000`
- Fresh Windows overlay packaging artifacts from rebuilt release trees:
  - `release-artifacts/barony-8p-windows-steam-20260324-134215.zip`
  - `release-artifacts/barony-8p-windows-nodrm-20260324-134215.zip`
- Fresh zip install validation passed with the new Windows post-package validator:
  - Command: `powershell -ExecutionPolicy Bypass -File scripts\mod_release\validate_windows_install.ps1 -Label 20260324-shipcheck60 -LaunchSeconds 60 -InputIdleSeconds 20`
  - Root artifact: `tests/smoke/artifacts/windows-install-validation-20260324-shipcheck60`
  - Steam zip passed against a copied local Steam install with manifest verification before/after overlay and a live 60-second startup window.
  - NoDRM zip passed against a sanitized copy of that same install with Steam-only root files removed before overlay.
  - Caveat: both installed copies stayed up with a live window and fresh logs, but neither reached `LoadMap ... mainmenu3.lmp` within the 60-second first-launch window in this local environment.
- Local operator caveat: current VS/vcpkg post-build `applocal.ps1` calls still point at a stale WindowsApps PowerShell package path (`7.5.4.0`), so build commands return nonzero after link even though the rebuilt `barony.exe` / `editor.exe` outputs are present and were packaged successfully.

## Acceptance Criteria
- [x] `BARONY_SUPER_MULTIPLAYER` default is ON.
- [ ] No logic changes outside default/config/docs are present.
- [ ] CI is fully green.
- [x] 4p and 15p smoke sanity lanes pass.
- [x] Splitscreen cap remains 4.

## Review Focus
- Tiny scoped diff.
- Correct default and clear docs.
- No accidental extra behavior changes.

## Rollback Strategy
Flip the default back OFF with a single revert commit if post-merge telemetry/regression indicates risk.

## Extraction Plan (from PR #940 / 8p-mod)
Do not cherry-pick mixed historical commits; create a fresh commit on top of the merged stack.
