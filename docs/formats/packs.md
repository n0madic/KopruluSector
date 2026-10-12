# Resource packs and mission assets

Defold resources that `python3 -m converter resources --data DIR` writes after `missions` and `data` (`converter/emit/packs.py`). Decisions: [ADR 0003](../adr/0003-atlases-and-graphics-base.md) (indexed atlases), [ADR 0004](../adr/0004-resource-packs.md) (packs). Pack membership: `converter/packs.py`. GRP geometry and atlas names per GRP: [`grpmeta.kpd`](kpd.md#grpmetakpd-schema-1).

## Layout

| Path under `generated/` | Content |
|---|---|
| `atlases/<pack>/<animation>/<frame>.png` | L8 GRP frame rectangle, palette index, 0 transparent; an empty frame is one transparent pixel |
| `atlases/<atlas>.atlas` | one animation per GRP (`terran_marine`), every frame in order, single page |
| `atlases/<atlas>_mask.atlas`, `atlases/<pack>_mask/…` | opacity atlas, only for an atlas with an opaque index 0 (none in stock data) |
| `sounds/<pack>/<file>.ogg`, `sounds/<pack>/sfx<id>.sound` | `sfxdata.dat` sounds of the pack (OGG, `oggenc -q 3`) |
| `packs/<pack>/atlases.go` | one sprite per atlas of the pack, its mask atlas and portrait atlases; material `/render/materials/indexed.material` |
| `sounds/<pack>/sounds<k>.go` | sound components of sfx ids k·128 … k·128 + 127, see naming |
| `packs/<pack>.collection` | instances `atlases` and `sounds<k>` |
| `packs/packs.go` | one collection factory per pack, id = pack name, Load Dynamically |
| `sounds/missions/<campaign>/<mission>/` | mission WAVs as OGG, `wav<index>.sound`, `wavs<k>.go` |
| `missions/<campaign>/<mission>_assets.collection` | collection `assets_<campaign>_<mission>`, instances `wavs<k>` |
| `missions/mission_assets.go` | one collection proxy per mission, id `<campaign>_<mission>` |
| `viewer/sprite_probe.bin` | frames of five probe animations for the viewer's sprite readback check (`converter/emit/sprite_probe.py`) |
| `reports/packs.json` | contents, sizes and the engine minimums below |

`textures.texture_profiles` maps `generated/atlases/**` to uncompressed L8 without mipmaps.

bob packs some frames turned by 90 degrees (`geometries[i].rotated`, e.g. the badlands tree `thingy_tileset_badlands_hdtree01` and `neutral_min01`). `mesh_texture.atlas_frames` takes each corner's UV from the atlas geometry and flags these frames, and `indexed_vt.vp` swaps the texture axes for them.

## Naming

**Pack sounds.** Sfx id `n` is component `sfx<n>` on game object `sounds<n // 128>` of the pack instance. After `collectionfactory.create`, the instance id is `ids[hash("/sounds" .. n // 128)]` and the URL `msg.url(nil, id, "sfx" .. n)`. The pack is `pack_of_sound(path)`: `sound\<race>\…` → that race, everything else → `effects`. Ids without an existing file (0, 86, 104, 105, 1066) have no component. Ids naming the same file share one OGG.

**Mission sounds.** Sound index `i` of the mission's `WAVS` section (the position among existing sounds in string-id order, [kpm](kpm.md#wavs)) is component `wav<i>` on game object `wavs<i // 128>` of the assets collection. The proxy for mission `<campaign>/<mission>` is component `<campaign>_<mission>` of `mission_assets.go`.

**Atlases.** The holder sprites in `atlases.go` exist so that the factory pulls the atlases into memory; they are named after the atlas (`terran`, `portraits_t`). Instantiating code disables or deletes the `atlases` object. Portrait atlases follow `PORTRAIT_GROUP_PACK`: t, u → terran; p → protoss; z → zerg; n → neutral.

## Stock contents

Atlas pages as built by bob 1.13.2 and reported by `resource.get_texture_info` (`.texturec` is width × height + 49 bytes, L8):

| Pack | Atlas | GRPs | Frames | Frame pixels | Page | Portrait atlases | Sounds | OGG files | OGG bytes |
|---|---|---|---|---|---|---|---|---|---|
| terran | `terran` | 98 | 3,510 | 3.67 M | 2048² | `portraits_t` 4096×2048, `portraits_u` 2048² | 378 | 375 | 5.37 MB |
| protoss | `protoss` | 72 | 3,400 | 6.22 M | 4096×2048 | `portraits_p` 2048² | 341 | 339 | 5.06 MB |
| zerg | `zerg` | 99 | 6,171 | 11.18 M | 4096² | `portraits_z` 4096×2048 | 275 | 267 | 3.36 MB |
| neutral | `neutral` | 59 | 2,706 | 3.12 M | 2048² | `portraits_n` 2048×1024 | 0 | 0 | 0 |
| effects | `effects` | 201 | 4,358 | 11.01 M | 4096² | — | 145 | 138 | 1.56 MB |
| doodads | `doodads` | 303 | 453 | 2.08 M | 2048² | — | 0 | 0 | 0 |

Mission assets: 64 collections, 1,105 sounds (at most 80 per mission), 57.3 MB OGG from 435 MB WAV.

## Engine minimums

`reports/packs.json` lists what the packs need when every pack is instantiated in one world and one mission assets collection is loaded (ADR 0004). `game.project` covers them:

| Setting | Needed | `game.project` |
|---|---|---|
| `resource.max_resources` | 2,489 pack resources, plus the rest of the game (default 1,024 fails: "The max number of resources (1024) has been passed") | 4,096 |
| `sound.max_sound_data` | 1,199 (all pack OGG files + the largest mission) | 2,048 |
| `sound.max_component_count` | 1,139 (all pack sounds in one world) | 2,048 |
| `collection_proxy.max_count` | 72 (64 mission proxies + 8 tileset proxies) | 72 |
| `collectionfactory.max_count` | 6 | default 128 |
| `sprite.max_count` | 11 holder sprites | 8,192 |

## Check

The map viewer references `packs.go` and `mission_assets.go`, so `tools/spike.sh viewer …` builds every pack. With `[viewer] packcheck = 1` (`viewer/packcheck.script`) it loads each pack, prints its page sizes, creates and deletes the instances, unloads and probes residency, then loads all packs at once, each pack again, and every mission assets proxy. Results on 2026-10-12 (`build/spikes/viewer/packcheck-{macos,web}.log`), 0 failures:

| | first load (ms) | second load (ms) | all 6 packs (ms) | mission assets load / unload (ms) |
|---|---|---|---|---|
| macOS (M4 Max, Vulkan) | zerg 550, terran 154, protoss 115, effects 71, doodads 13, neutral 12 | zerg 63, terran 77, protoss 70, effects 38, neutral 11, doodads 10 | 288 | 15–40 (median 20) / 0.5–7 |
| Web (headless Chrome, WebGL 2) | zerg 186, terran 59, protoss 48, effects 23, neutral 18, doodads 6 | zerg 74, terran 57, protoss 48, effects 21, neutral 17, doodads 6 | 234 | 19–21 / 12–14 |

Every atlas was absent before load and after unload. The first load also creates the shared material and reads cold files, which is why Zerg (the first pack) is slow.
