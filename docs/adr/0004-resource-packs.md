# ADR 0004: Resource packs

Status: accepted (phase 0, 2026-10-11). Evidence: spike 08, spike 11.

Superseded in part by [ADR 0012](0012-sdl3-runtime.md) (phase S, 2026-10-25): collection factories, proxies, sound components and the engine's resource limits give way to `PACK` containers loaded by `kp_app`.

## Decision

- **A pack is a collection factory with "Load Dynamically"** (plan D21, §2.2). Packs: one per race, neutral + critters, shared effects/projectiles, the player race's HUD.
  The pack collection references the pack's atlases, prototypes and sounds.
  - Before a mission, `mission.script` calls `collectionfactory.load` for every pack in `PACK` in parallel and starts when all callbacks report success.
  - After the mission it deletes the pack instances and calls `collectionfactory.unload`.
  - Spike 08 confirmed that a race atlas is absent from the resource cache before `load` and after `unload` (macOS, web, Android emulator). All three races load in 55–100 ms.
- **Collection proxies** stay for what needs its own world or a whole-collection lifetime: the mission collection per tileset and the mission assets collection (speech/WAV of the mission). Spike 11 measured this at 17–34 ms load and 8–17 ms unload.
  Proxies are not used for race packs: they would create a separate world per pack, with extra message routing, for no memory benefit (spike 08 showed identical residency behaviour).
- **No always-loaded component may reference a pack resource statically.** Bootstrap and the mission template reference packs only through dynamically loading factories.
- **Engine limits are part of the contract.** Pack sizes drive `game.project` settings:
  - `sound.max_sound_data` (the default 128 cannot hold one race's 375 sounds);
  - `sound.max_component_count`, `sound.max_sound_instances`;
  - `sound.stream_enabled = 0`, set in `game.project`. Defold's streamed read skips archive decompression, so every sound that bob stored LZ4-compressed fails to load (spike 11);
  - `collection_proxy.max_count`;
  - `tilemap.max_tile_count`;
  - `resource.max_resources` (the default 1,024 cannot hold all packs at once).

  The converter, which knows the pack contents, reports the required minimums (`generated/reports/packs.json`, [resource packs](../formats/packs.md#engine-minimums)).

## Consequences

- Residency is checked by resource-cache lookups (`resource.get_atlas`). RSS does not drop after unload because allocators keep freed memory, so memory budgets must use resource sizes, not RSS deltas.
- Spike 08 on a Galaxy Z Fold7 (Adreno 830): same residency behaviour, race packs load in 32–95 ms.
- Not verified: iOS, a mid-range Android device, GPU memory read from the driver, reference counting when two packs share a resource.
