# 8p-mod Changelog (v5.0.1 -> v5.0.2)

Date: 2026-02-26
Branch: `codex/8p-mod-5.0.2`

## Summary
This prep cycle rebased the mod to upstream `v5.0.2`, then hardened compatibility and stability for 1-15 player behavior. The guiding policy remained:
- preserve strict 1-4 parity with upstream behavior
- tune only overflow paths (5-15)
- resolve carry-over multiplayer regressions discovered during post-reconcile validation

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

### 6) Post-Reconcile Bugfix Pass (Hermit Ducks + Remote Enemy HP Bars)
Files:
- `src/items.hpp`
- `src/items.cpp`
- `src/charclass.cpp`
- `src/actplayer.cpp`
- `src/item_tool.cpp`
- `src/mod_tools.cpp`
- `src/net.cpp`
- `src/interface/drawstatus.cpp`

Changes:
- Added canonical duck encoding/decoding helpers (`4` color variants, canonical span `MAXPLAYERS * 4`) and migrated duck owner/color callsites away from `items[TOOL_DUCK].variations`-based decoding.
- Updated Hermit duck appearance generation and duck-in-hand checks to use canonical color/owner helpers.
- Preserved canonical duck appearance when converting duck item -> summoned duck stat attributes.
- Clamped incoming `DUCK` packet color requests to canonical color range.
- Added runtime warning when `tool_duck` variation count is below canonical duck span, so mixed mod/datadir installs are explicit in logs.
- Relaxed enemy HP bar send guard for remote players to avoid over-blocking on `net_clients[].host/port` zero values in P2P-style paths while preserving slot/disconnect/local-player checks.

Why:
- User-reported issues (host receiving other players' ducks / repeated duck fills, remote enemy HP bars not visible) traced to carry-over assumptions that were fragile under `MAXPLAYERS=15` and mixed-asset runtime setups.

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

Additional post-reconcile validation (2026-02-26):
- Build:
```bash
cmake -S . -B build-mac-smoke -G Ninja -DFMOD_ENABLED=OFF -DBARONY_SMOKE_TESTS=ON
cmake --build build-mac-smoke -j8 --target barony
```
- Remote combat lane (LAN) PASS with remote contexts including `client-ENHP` and `client-DAMI`:
  - `tests/smoke/artifacts/remote-combat-fix-20260226-001704`
- Save/reload owner-encoding lane PASS:
  - `tests/smoke/artifacts/save-reload-compat-duck-fix-20260226-002659`
- Splitscreen cap lane PASS (`requested=8`, cap enforced `4`):
  - `tests/smoke/artifacts/splitscreen-cap-duck-fix-20260226-002744`
- Inventory fast-pass (lifecycle/edge/churn) PASS:
  - `tests/smoke/artifacts/inventory-fast-pass-duck-fix-20260226-002822`
- Steam/EOS follow-up lanes attempted; both blocked by missing local room-key prerequisites in this runtime context:
  - Steam: `tests/smoke/artifacts/steam-remote-combat-fix-20260226-001807`
  - EOS: `tests/smoke/artifacts/eos-remote-combat-fix-20260226-002141`

## Notes
- This changelog documents prep work from the mod `v5.0.1` baseline to upstream-aligned `v5.0.2` compatibility.
- Final promotion should still include one full-lobby confirmation pass on the chosen tuning snapshot before tagging a release artifact.
