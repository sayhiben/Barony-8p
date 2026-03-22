# [PR4] Lobby/Join Protocol Hardening for High Player Counts

## Ticket Metadata
- Type: Engineering
- Priority: Critical
- Epic: Multiplayer Expansion 1-15
- Risk: High
- Status (Updated 2026-02-14): Planned, not yet isolated for review
- Depends On: PR2, PR3
- Blocks: PR6-PR10 confidence

## Background
Large-lobby join/start reliability is a core blocker for 1-15 support. Existing join paths are fragile under high slot counts and payload pressure, especially around HELO capability signaling and chunk handling.

## Field Report Follow-Up (2026-03-21)
- A real-world lobby report showed one client rendering different wall geometry while host collision still followed the shared map. Code inspection and smoke repro isolated a second sync risk beyond HELO/join: level load previously sent only seed/level metadata, while clients re-derived mapgen inputs from local slot state.
- Current branch follow-up implements authoritative level-load mapgen inputs (`connected-player` mask + final tile checksum) in `LVLC` / `LVLR`, with client-side checksum verification after load.
- Targeted LAN smoke repro with a client-only smoke override of `5` connected players now stays synchronized:
  - artifact: `tests/smoke/artifacts/map-desync-authoritative-launch-20260321-210316`
  - summary: `tests/smoke/artifacts/map-desync-authoritative-launch-20260321-210316/summary.env`
  - host/client both generated `The Mines` with `players=2`; client received `mask=0x0003 checksum=2380154547` before load and logged no mismatch
- Current branch follow-up now also implements the host-authoritative geometry recovery path for checksum failure:
  - client requests a snapshot when post-load tile checksum mismatches the host
  - host streams a reliable chunked geometry snapshot and client rebuilds tiles/pathing/chunks from that authoritative payload
- Current branch follow-up now extends the parity signal itself:
  - the shared tile checksum includes `tileAttributes`, not just dimensions/flags/tile layers
  - `LVLC` / `LVLR` metadata now carries a second initial entity checksum for the post-load entity scene before clients remove `NOUPDATE` placeholders
  - clients log entity-scene drift separately from geometry drift so reports can distinguish “same walls, different placements/content” from true tile mismatches
- Targeted LAN smoke recovery lane with a client-only forced mismatch passed:
  - artifact: `tests/smoke/artifacts/map-desync-snapshot-recovery-20260321-213455`
  - summary: `tests/smoke/artifacts/map-desync-snapshot-recovery-20260321-213455/summary.env`
  - host received the request and streamed `19621` bytes in `11` chunks; client recovered from `local_checksum=1494320743` back to host checksum `2380154547`
- Post-hardening regression check remained green after the checksum scope/version bump:
  - smoke-enabled rebuild: `cmake --build build-mac-smoke -j8 --target barony`
  - artifact: `tests/smoke/artifacts/map-desync-entity-checksum-20260321-215354`
  - summary: `tests/smoke/artifacts/map-desync-entity-checksum-20260321-215354/summary.env`
- Broader follow-up smoke pass stayed green on the stable regression lanes:
  - 2-instance baseline dungeon transition: `tests/smoke/artifacts/level-sync-baseline-2p-20260321-221439`
  - 4-instance dungeon/mapgen transition with a 3-second auto-start delay: `tests/smoke/artifacts/level-sync-4p-mapgen-delay3-20260321-221847`
  - 3-run 4-instance HELO soak: `tests/smoke/artifacts/helo-soak-level-sync-20260321-221956`
  - HELO adversarial matrix expectations matched in all pass/fail cases: `tests/smoke/artifacts/helo-adversarial-level-sync-20260321-222152`
  - standard 6-instance churn/ready-sync lane: `tests/smoke/artifacts/join-leave-churn-standard-20260321-222825`
  - save/reload compatibility sweep: `tests/smoke/artifacts/save-reload-compat-level-sync-20260321-223036`
- Follow-up smoke hardening closed the zero-delay start caveat:
  - smoke host auto-start now waits for connected-slot parity and host-observed lobby-entry acks before firing
  - 4-instance zero-delay auto-start lane now passes cleanly with all clients loading `start.lmp`: `tests/smoke/artifacts/level-sync-4p-mapgen-delay0-smokegate-20260321-230540`
  - summary: `tests/smoke/artifacts/level-sync-4p-mapgen-delay0-smokegate-20260321-230540/summary.env`
- Same-level reload follow-up is now green:
  - 2-instance procedural reload lane with `mapgen-reload-same-level=1` passed and matched both requested reload seeds (`100100`, `100101`): `tests/smoke/artifacts/reload-procedural-level-sync-2p-20260321-230638`
  - summary: `tests/smoke/artifacts/reload-procedural-level-sync-2p-20260321-230638/summary.env`
- Remaining exploratory lanes were not counted:
  - churn plus gameplay auto-start drifted into midgame-rejoin retries instead of standard lobby churn: `tests/smoke/artifacts/join-leave-churn-level-sync-20260321-222429`
  - remote-combat follow-up did not produce clean gameplay signal in this local harness: `tests/smoke/artifacts/remote-combat-level-sync-20260321-223515`
- Remaining caveat for extraction planning: the recovery is geometry-scoped. It fixes tile/flag/tile-attribute drift after load, but it does not make the full level bootstrap host-authoritative for static entity/content drift.
- Cleanup follow-up (2026-03-22):
  - extracted the level-load authority/recovery path behind `src/level_load_sync.cpp/.hpp`, reducing the upstream surface in `src/game.cpp`, `src/net.cpp`, and `src/maps.cpp`
  - split snapshot application so `src/files.cpp` now handles geometry data copy plus HDR/lightmap/minimap reset, while `src/level_load_sync.cpp` owns vismap/shoparea replacement during recovery
  - verification for this cleanup pass was build-only: `cmake --build build-mac -j8 --target barony editor`
  - no new smoke artifact was generated for the refactor-only follow-up; remaining caveat is unchanged, with ambience-driven lightmap reset still anchored in `src/files.cpp`

## What and Why
Harden join protocol and lobby flow so 4p and 15p sessions remain stable, while preserving compatibility/fallback behavior.

## Scope
### In Scope
- `src/net.cpp`
- `src/net.hpp`
- `src/ui/MainMenu.cpp`
- Minimal supporting changes only if required:
  - `src/game.cpp`
  - `src/interface/drawstatus.cpp`
  - `src/player.cpp`
  - `src/entity.cpp`

### Out of Scope
- Smoke framework architecture
- `tests/smoke/*` script additions
- Mapgen formulas or balance work

## Implementation Instructions
1. Implement/cleanly isolate JOIN capability and HELO chunk handling updates (`JOIN` len/capability path, `HLCN` handling).
2. Update `lobbyPlayerJoinRequest` API/signature in `src/net.hpp` and all required call sites.
3. Ensure chunk reassembly and fallback path both work:
   - New-capability clients use chunk path.
   - Legacy/small payload fallback remains functional.
4. Keep lobby readiness/start flow deterministic under high slots; avoid UI/net drift.
5. Keep this PR free of smoke runner/script additions; only core runtime behavior.

## Suggested Commit Structure
1. Protocol structure and capability-path updates.
2. Lobby/request flow wiring and fallback logic.
3. Small supporting fixes in dependent files.

## Validation Plan
- Manual:
  - 4-player lobby join/start cycle.
  - 15-player lobby join/start cycle.
  - Legacy fallback path sanity.
- Smoke evidence (local only, scripts not part of this PR):
  - HELO chunk lane
  - Legacy fallback lane

## Acceptance Criteria
- [ ] 15-player join/start path succeeds reliably in local validation.
- [ ] 4-player baseline flow remains stable.
- [ ] Capability/chunk path and legacy fallback both function.
- [ ] `src/net.cpp` / `src/ui/MainMenu.cpp` changes are focused and reviewable.
- [ ] No smoke framework/scripts or mapgen/balance changes are included.

## Review Focus
- Packet compatibility and fallback behavior.
- Reassembly safety and edge-case handling.
- Lobby state-machine correctness under churn.

## Rollback Strategy
Revert this PR while keeping PR2/PR3 scaffolding; protocol changes are isolated here.

## Extraction Plan (from PR #940 / 8p-mod)
Selectively extract net/lobby-only hunks from `a46cd9a3`, `780c0287` (ready-sync pieces), `b36471d6`, `4288b719`; reject `tests/smoke/*` and non-networking hunks.
