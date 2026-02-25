# 8p-mod Changelog (v5.0.1 -> v5.0.2)

Date: 2026-02-24
Branch: `codex/8p-mod-5.0.2`

## Summary
This prep cycle rebased the mod to upstream `v5.0.2`, then hardened compatibility and stability for 1-15 player behavior. The guiding policy remained:
- preserve strict 1-4 parity with upstream behavior
- tune only overflow paths (5-15)

## Major Changes

### 1) Owner-Encoding Hardening (Status Effects / XP Attribution)
Files:
- `src/status_effect_owner_encoding.hpp`
- `src/entity.cpp`
- `src/actplayer.cpp`
- `src/smoke/SmokeHooksSaveReload.cpp`

Changes:
- Added helper APIs to centralize player-index validation and packed-owner comparisons:
  - `isValidPlayerIndex(...)`
  - `tryDecodeOwnerNibbleToPlayer(...)`
  - `packedOwnerMatchesPlayer(...)`
- Migrated owner decode callsites to helper-based checks instead of ad-hoc nibble comparisons.
- Hardened XP bonus ownership checks (`EFF_DIVINE_FIRE`) via packed-owner matcher.
- Extended save/reload smoke assertions for full-cap focus slots (1, 8, 15) including negative-attribution checks.

Why:
- Upstream owner nibble handling changed in ways that could reintroduce subtle 15-player attribution regressions without central helper usage.

### 2) Net Item/Equip Temp-Item Lifecycle Normalization
Files:
- `src/net.cpp`

Changes:
- Added net-side temp-item disposal helpers:
  - `disposeNetTempItem(...)`
  - `cleanupNetEquipTempItem(...)`
- Unified post-`equipItem(...)` cleanup behavior across packet handlers (`EQUI`, `EQUS`, `EQUM`, `COOK`).
- Removed inconsistent inline/free-list cleanup branches.

Why:
- Upstream desync fixes introduced mixed cleanup patterns; normalization avoids slot/update edge-case leaks and stale-item behavior under high slot traffic.

### 3) Mod-Only Version Signaling / JOIN Safety
Files:
- `src/game.hpp`
- `src/net.cpp`
- `src/ui/MainMenu.cpp`

Changes:
- Changed mod version string to `v5.0.2m`.
- Added compile-time guard for JOIN version-field budget (`<= 8` bytes including null).
- Added explicit remote/local version logging on JOIN reject.
- Used explicit JOIN version field width constant at write site.

Why:
- Prevent accidental cross-join attempts between vanilla `v5.0.2` and modded builds sharing the same base version.

### 4) Smoke Config Seeding Resilience (Settings Schema Bump)
Files:
- `tests/smoke/smoke_framework/local_lane.py`

Changes:
- Added deterministic defaults for newly expanded controls/settings schema when seeding smoke homes.
- Enforced minimum config/control version values where needed.
- Preserved existing smoke seed behavior (`skipintro=true`, `mods=[]`).

Why:
- Upstream settings schema additions made fresh-vs-legacy smoke homes less deterministic without explicit defaults.

### 5) Overflow-Only Mapgen Rebalance After Upstream v5.0.2 Deltas
Files:
- `src/maps.cpp`

Changes:
- Re-tuned overflow-only mapgen knobs for room growth, monster pressure, loot/gold conversion, and decoration pressure.
- Kept 1-4 logic untouched; all balancing changes stay in overflow paths.
- Iterated using in-process integration runs (`-smoke-mapgen-integration`) for high-speed feedback loops without real clients.

Why:
- Upstream map/item generation changes shifted high-player economy and pacing envelopes; overflow retuning was needed to restore target behavior.

## Validation Used During Prep

Build:
```bash
cmake --build build-mac-smoke-nosteam -j8 --target barony
```

Fast mapgen iteration method:
```bash
.../Barony -smoke-mapgen-integration \
  -smoke-mapgen-integration-csv=<out>/mapgen_level_matrix.csv \
  -smoke-mapgen-integration-levels=1,7,16,33 \
  -smoke-mapgen-integration-min-players=1 \
  -smoke-mapgen-integration-max-players=15 \
  -smoke-mapgen-integration-runs=15
```

Representative artifact pair (same tuning revision):
- `tests/smoke/artifacts/mapgen-integration-iter-r15-20260224-231734`
- `tests/smoke/artifacts/mapgen-integration-iter-r15-20260224-231853`

Combined `p15 vs p4` summary from that pair:
- rooms `1.6778` (target `1.62-1.75`) PASS
- monsters `1.4057` (target `1.38-1.46`) PASS
- monsters/room `0.8029` (target `0.82-0.92`) near-threshold low
- gold/player `0.7422` (target `0.70-0.80`) PASS
- items/player `0.7171` (target `0.70-0.80`) PASS
- food/player `0.7165` (target `0.65-0.78`) PASS
- decorations `1.8968` (target `1.85-2.25`) PASS
- blocking share `0.1857` (target `<=0.45`) PASS

## Notes
- This changelog documents prep work from the mod `v5.0.1` baseline to upstream-aligned `v5.0.2` compatibility.
- Final promotion should still include one full-lobby confirmation pass on the chosen tuning snapshot before tagging a release artifact.
