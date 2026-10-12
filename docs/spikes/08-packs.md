# Spike 08: resource packs

Plan: phase 0 item 8, D21, §2.2. Decision record: [ADR 0004](../adr/0004-resource-packs.md).

## Method

- `python3 -m converter.emit.spikes --data <app> packs` builds one L8 index atlas per race from all of its `images.dat` GRPs (Terran, Zerg, Protoss; [ADR 0003](../adr/0003-atlases-and-graphics-base.md)).
- `spikes/08-packs` defines one pack collection per race (a game object with a sprite that uses the race atlas). Each pack is referenced in two ways:
  - a collection factory with `load_dynamically: true` (`<race>_cf`);
  - a collection proxy (`<race>_proxy`).
- **Residency probe.** `resource.get_atlas(path)` looks the texture set up in the resource cache by path. It raises for a resource that is not loaded and does not load it.
  When the atlas is resident, `resource.get_texture_info` gives its size. Process memory (`profiler.get_memory_usage`) is logged as a secondary signal.
- Sequence per race:
  - collection factory: probe → `collectionfactory.load` → probe → `create` → delete instances → `unload` → probe;
  - proxy: `async_load` → probe → `enable` → `disable`/`final`/`unload` → probe;
  - then all three packs through collection factories at once.

## Results

Raw logs are in `build/spikes/08-packs/{macos,web,android}.log`.

**Residency** was identical on all three platforms (macOS Vulkan, web WebGL 2 in headless Chrome, Android emulator):
- No race atlas is resident at start or before its pack is loaded.
- After loading one pack, only that race's atlas is resident.
- After unload, it is absent again.
- This holds for collection factories and proxies alike, and for the all-three case.

Texels when resident:

| Race | Texels | Texture |
|---|---|---|
| Terran | 4,194,304 | 2048² |
| Protoss | 8,388,608 | 4096×2048 |
| Zerg | 16,777,216 | 4096² |

L8 is 1 byte per texel, so all three packs together take 28 MB.

**Load times** (ms; async load to callback or `proxy_loaded`):

| | Terran CF | Zerg CF | Protoss CF | all 3 CF | Terran proxy | Zerg proxy | Protoss proxy | proxy unload |
|---|---|---|---|---|---|---|---|---|
| macOS (M4 Max) | 15.5 | 25.8 | 16.8 | 54.9 | 24.0 | 30.2 | 25.4 | 7.8–8.8 |
| Web (WebGL 2) | 67 | 24 | 17 | 100 | 33 | 41 | 33 | 16–17 |
| Android emulator | 87.1 | 75.8 | 26.6 | 73.6 | 39.5 | 53.9 | 50.0 | 16–19 |

- Process RSS rises on load (macOS: +14 MB Terran, +42 MB Zerg) but does not return on unload: allocators keep the freed memory.
- On the web the wasm heap is a fixed 256 MB, so memory is not observable there. Residency is established by the cache probe, not by RSS.

## Conclusion

Both mechanisms keep race atlases out of memory until load and release them on unload.
Collection factories with "Load Dynamically" are chosen for packs (ADR 0004):
- they load asynchronously with a callback;
- several packs load in parallel;
- instances are created in the mission world without a second world.

## Not measured

- iOS and a mid-range Android device. On the Galaxy Z Fold7 (Snapdragon 8 Elite, Adreno 830, Android 16, Vulkan) residency matched the other platforms: race packs absent before load and after unload; collection-factory loads 31.8–61.1 ms (all three races 94.5 ms), proxy loads 39.9–68.6 ms, unloads 7.8–8.3 ms.
- GPU memory directly. The texel count is computed, not read from the driver.
- Packs sharing one resource: reference counting across two packs was not exercised.
