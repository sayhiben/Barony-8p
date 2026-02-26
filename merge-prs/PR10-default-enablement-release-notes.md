# [PR10] Default Enablement Flip and Release Notes

## Ticket Metadata
- Type: Engineering
- Priority: High
- Epic: Multiplayer Expansion 1-15
- Risk: Medium
- Status (Updated 2026-02-14): Planned, blocked on PR1-PR9 completion
- Depends On: PR1-PR9 complete and gated
- Blocks: Release readiness

## Background
Through PR9, 1-15 support is intentionally scaffolded behind defaults to reduce blast radius. The final step is enabling the new path by default only after all stability and balance gates are satisfied.

## What and Why
Flip `BARONY_SUPER_MULTIPLAYER` default ON and publish concise operator/reviewer release notes once all technical gates are green.

## Release Note Addendum (User-Facing Bugfix Callout)
When this stack is released, include a short multiplayer bugfix note:
- Hermit duck ownership now uses canonical 15-player-safe encoding and no longer aliases through asset variation counts.
- Added runtime warning for mod/datadir mismatches where duck asset variation count is below canonical span (`MAXPLAYERS * 4`), so users can self-diagnose mixed-version installs.
- Remote clients now reliably receive enemy HP bar/damage indicator updates in LAN sessions even when peer host/port metadata is zeroed in P2P-style flows.

## Scope
### In Scope
- One-line default flip in `CMakeLists.txt` (`BARONY_SUPER_MULTIPLAYER`)
- Concise release-facing notes in `README.md`, `INSTALL.md`, and/or
  `mod-changelog.txt` (if needed)

### Out of Scope
- Any gameplay/network/mapgen logic changes
- Smoke framework/script changes
- Additional refactors

## Implementation Instructions
1. Confirm all prior PR gates are complete and documented.
2. Change only the default value of `BARONY_SUPER_MULTIPLAYER` in `CMakeLists.txt`.
3. Update docs with short release notes:
   - 1-15 support now default-enabled.
   - Splitscreen remains capped at 4.
   - Smoke flags remain optional and compile-gated.
4. Keep this PR intentionally tiny and reviewable.

## Suggested Commit Structure
1. Default flip commit (`CMakeLists.txt` only).
2. Optional short release note commit (`README.md`/`INSTALL.md`).

## Validation Plan
- Full CI pass.
- Smoke sanity:
  - Baseline 4p lane.
  - 15p lane.
- Manual startup sanity with default settings.
- Operator packaging sanity:
  - Steam and NoDRM Windows overlay zips contain the built executables, adjacent runtime DLLs, the mod README, and `SHA256SUMS.txt`.

## Notes (2026-03-14)
- Windows overlay packaging helper now exists at `scripts/mod_release/package_windows_release.ps1`.
- Latest local artifacts:
  - `release-artifacts/barony-8p-windows-steam-20260314-195941.zip`
  - `release-artifacts/barony-8p-windows-nodrm-20260314-195941.zip`

## Notes (2026-03-15)
- Added `mod-changelog.txt` to summarize the upstream `v5.0.1 -> v5.0.2`
  sync plus the mod-side compatibility/bug-fix work that accompanies it.

## Acceptance Criteria
- [ ] `BARONY_SUPER_MULTIPLAYER` default is ON.
- [ ] No logic changes outside default/config/docs are present.
- [ ] CI is fully green.
- [ ] 4p and 15p smoke sanity lanes pass.
- [ ] Splitscreen cap remains 4.

## Review Focus
- Tiny scoped diff.
- Correct default and clear docs.
- No accidental extra behavior changes.

## Rollback Strategy
Flip the default back OFF with a single revert commit if post-merge telemetry/regression indicates risk.

## Extraction Plan (from PR #940 / 8p-mod)
Do not cherry-pick mixed historical commits; create a fresh commit on top of the merged stack.
