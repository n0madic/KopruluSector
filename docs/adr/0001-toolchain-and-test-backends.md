# ADR 0001: Toolchain pin and Lua test backends

Status: accepted (phase 0, 2026-10-11)

## Decision

- **Defold 1.13.2** (engine/editor SHA-1 `055a6e163cb952d023ffe7becf9eab88d7e84a74`) is
  pinned. `tools/toolchain/fetch.sh` fetches `bob.jar` and `dmengine_headless` of that
  build and builds PUC Lua 5.1.5 from source (checksum-verified). The toolchain directory
  is gitignored.
- Every simulation module is plain Lua 5.1 without `go.*`/`msg.*` (plan D7), so the same
  suites (`tests/lua/`, listed in `tests/lua/runner.lua`) run on four backends:
  1. LuaJIT with JIT (desktop, Android);
  2. LuaJIT with `-joff` (the interpreter mode used on iOS);
  3. PUC Lua 5.1.5 (the HTML5 runtime);
  4. the headless Defold engine (`tests/defold/`, built with
     `--settings tests/defold/tests.settings`), which also checks custom resources and
     `sys.load_buffer[_async]`.
- `tools/ci.sh` runs all of them plus the converter's pytest suite.
- Bit operations use the BitOp `bit` module that Defold ships on every platform. PUC Lua
  has no `bit`, so tests use `tests/lua/bitshim.lua`. The shim is cross-checked against
  LuaJIT's native module and is never part of the game.

## Numeric core

- `game/simulation/int.lua` gives exact x86 integer arithmetic on doubles:
  - 16-bit limb products: `mul` gives the low 32 bits, `mulhi` the high 32 bits.
  - Division truncates toward zero; `sar` floors.
  - Shift counts are taken mod 32.
  - Values wrap at 8, 16 and 32 bits.
- `game/simulation/rng.lua` implements the three independent generators of plan D14.
  They are checked against golden vectors that `tools/oracle/export_leaf_vectors.py`
  exported from the original-code probes in `docs/research/evidence`:
  - 22 game RNG cases (raw and range, enabled and disabled);
  - 420 portrait RNG cases.

  The CRT generator is checked against the MSVC sequence for seed 1.
- `game/simulation/pool.lua` reproduces the native intrusive list primitives (0x4119a0,
  0x411930, 0x411900, 0x411970) and the new-game free order. Handles add a generation that
  changes on release.
- `game/simulation/snapshot.lua` is a deterministic structural encoder. Map keys are sorted
  and only integral numbers are accepted. Fingerprints use FNV-1a 32. A 6000-step
  pool+RNG run gives fingerprint `0xc7d3a80d` on all four backends, including the
  continuation after save/load.

## Consequences

- An arithmetic or ordering difference between backends fails CI instead of surfacing as
  a desync in a mission.
- Device-specific measurements (iOS interpreter speed, Android, HTML5 in a browser) are
  not covered by these runs; they belong to the phase-0 spikes that need devices.
