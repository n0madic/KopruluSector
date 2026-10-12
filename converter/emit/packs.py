"""Resource packs (ADR 0004) and mission assets: the Defold side of grpmeta.kpd and sounds.

Layout and naming: docs/formats/packs.md. Outputs under <out>/:
  atlases/<pack>/<animation>/<frame>.png    L8 GRP frame rectangles, index 0 transparent (ADR 0003)
  atlases/<atlas>.atlas                     one flipbook animation per GRP, every frame in order
  atlases/<atlas>_mask.atlas (+ frames)     only for an atlas with an opaque index 0 (none in stock data)
  sounds/<pack>/<file>.ogg, sfx<id>.sound   sfxdata sounds of the pack
  sounds/<pack>/sounds<k>.go                sound components sfx<id> for ids k*128 .. k*128+127
  packs/<pack>/atlases.go                   one sprite per atlas of the pack (incl. portraits), so the
                                            factory pulls the atlases in
  packs/<pack>.collection                   instances "atlases" and "sounds<k>"
  packs/packs.go                            one collection factory per pack (Load Dynamically)
  sounds/missions/<campaign>/<mission>/...  mission WAVs as OGG, wav<index>.sound, wavs<k>.go
  missions/<campaign>/<mission>_assets.collection
  missions/mission_assets.go                one collection proxy per mission
  reports/packs.json                        contents, sizes and the engine minimums the packs need
"""
from __future__ import annotations

import json
import logging
import re
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .. import packs as pack_rules
from ..analyze.profile import PORTRAIT_GROUP_PACK
from ..formats import tbl, wav
from ..kpf import kpd
from ..mpq.profile import Resolver
from . import atlas, profiles, sound
from .defold import project_path, quote, write_if_changed

log = logging.getLogger(__name__)

MATERIAL = "/render/materials/indexed.material"
SAMPLER = "texture_sampler"
SOUNDS_PER_OBJECT = 128
PACK_SOUND_GROUP = "sfx"
MISSION_SOUND_GROUP = "speech"
PORTRAIT_GROUPS = ("t", "u", "p", "z", "n")


class PacksError(RuntimeError):
    """Inputs of the pack resources are missing."""


# --- atlases --------------------------------------------------------------------------

def write_pack_atlases(gs: kpd.GrpSet, out: Path) -> dict[str, dict]:
    """Frame PNGs and .atlas files of every pack atlas; returns per atlas name
    {pack, animations, frames, pixels, mask, first animation}."""
    atlases = {name: {"pack": pack, "mask": mask, "animations": [], "masks": [], "frames": 0, "pixels": 0}
               for name, pack, mask in gs.atlases}

    def frames_of(e: kpd.GrpEntry):
        a = atlases[e.atlas]
        anim = atlas.animation_id(e.path)
        index = atlas.write_grp_frames(e.grp, out / "atlases" / e.pack / anim, atlas.Layout.CROP,
                                       atlas.Encoding.INDEX, opaque_zero=a["mask"])
        mask = (atlas.write_grp_frames(e.grp, out / "atlases" / f"{e.pack}_mask" / anim, atlas.Layout.CROP,
                                       atlas.Encoding.MASK) if a["mask"] else None)
        return e, anim, index, mask

    with ThreadPoolExecutor() as pool:
        for e, anim, index, mask in pool.map(frames_of, gs.grps):
            a = atlases[e.atlas]
            a["animations"].append(atlas.Animation(anim, index))
            if mask:
                a["masks"].append(atlas.Animation(anim, mask))
            a["frames"] += len(e.grp.frames)
            a["pixels"] += sum(max(1, f.width) * max(1, f.height) for f in e.grp.frames)
    for name, a in atlases.items():
        atlas.write_atlas(out / "atlases" / f"{name}.atlas", atlas.AtlasSpec(a["animations"]))
        if a["mask"]:
            atlas.write_atlas(out / "atlases" / f"{name}_mask.atlas", atlas.AtlasSpec(a.pop("masks")))
        else:
            a.pop("masks")
        a["first"] = a["animations"][0].id
        a["animations"] = len(a.pop("animations"))
    return atlases


def portrait_atlases(out: Path) -> dict[str, list[tuple[str, str]]]:
    """Pack -> [(portrait atlas name, its first animation)], from the atlases `data` wrote."""
    found: dict[str, list[tuple[str, str]]] = {}
    for g in PORTRAIT_GROUPS:
        path = out / "atlases" / f"portraits_{g}.atlas"
        if not path.exists():
            raise PacksError(f"{path} is missing; run `python3 -m converter data` first")
        first = re.search(r'^  id: "([^"]+)"', path.read_text(), re.M)
        if not first:
            raise PacksError(f"{path} has no animation")
        found.setdefault(PORTRAIT_GROUP_PACK[g], []).append((f"portraits_{g}", first.group(1)))
    return found


# --- Defold text ----------------------------------------------------------------------

def sprite_component(cid: str, atlas_path: str, animation: str) -> str:
    data = (f"default_animation: {quote(animation)}\nmaterial: {quote(MATERIAL)}\n"
            f"textures {{\n  sampler: {quote(SAMPLER)}\n  texture: {quote(atlas_path)}\n}}\n")
    return f"embedded_components {{\n  id: {quote(cid)}\n  type: \"sprite\"\n  data: {quote(data)}\n}}\n"


def embedded_component(cid: str, kind: str, data: str) -> str:
    return f"embedded_components {{\n  id: {quote(cid)}\n  type: {quote(kind)}\n  data: {quote(data)}\n}}\n"


def collection_text(name: str, instances: list[tuple[str, Path]]) -> str:
    out = f"name: {quote(name)}\nscale_along_z: 0\n"
    for iid, proto in instances:
        out += f"instances {{\n  id: {quote(iid)}\n  prototype: {quote(project_path(proto))}\n}}\n"
    return out


def write_sound_objects(sounds: list[tuple[int, Path]], directory: Path, prefix: str, component: str,
                        group: str) -> list[tuple[str, Path]]:
    """`.sound` per (index, ogg) and game objects `<prefix><index // 128>` holding components
    `<component><index>`; returns the (object id, .go path) instances."""
    objects: dict[int, list[tuple[str, Path]]] = {}
    for index, ogg in sounds:
        snd = directory / f"{component}{index}.sound"
        write_if_changed(snd, sound.sound_text(ogg, group))
        objects.setdefault(index // SOUNDS_PER_OBJECT, []).append((f"{component}{index}", snd))
    instances = []
    for k, comps in sorted(objects.items()):
        go = directory / f"{prefix}{k}.go"
        write_if_changed(go, sound.go_text(comps))
        instances.append((f"{prefix}{k}", go))
    return instances


# --- packs ----------------------------------------------------------------------------

def pack_sounds(r: Resolver) -> dict[str, list[tuple[int, str]]]:
    """Pack -> [(sfx id, path)] of every sfxdata sound whose file exists."""
    paths = tbl.sfx_paths(r.read("arr\\sfxdata.dat")[1], r.read("arr\\sfxdata.tbl")[1])
    out: dict[str, list[tuple[int, str]]] = {}
    for sid, path in enumerate(paths):
        if path and r.has(path):
            out.setdefault(pack_rules.pack_of_sound(path), []).append((sid, path))
    return out


def ogg_name(mpq_path: str, prefix: str) -> str:
    """'sound\\Terran\\MARINE\\TMaDth00.wav' -> 'terran_marine_tmadth00.ogg' (prefix removed)."""
    stem = mpq_path.lower().removeprefix(prefix).removesuffix(".wav")
    return stem.replace("\\", "_") + ".ogg"


def write_packs(r: Resolver, gs: kpd.GrpSet, out: Path, cache: sound.OggCache) -> dict[str, dict]:
    atlases = write_pack_atlases(gs, out)
    portraits = portrait_atlases(out)
    sounds = pack_sounds(r)
    report: dict[str, dict] = {}
    names = [p for p in pack_rules.PACKS if any(a["pack"] == p for a in atlases.values()) or p in sounds]
    for pack in names:
        directory = out / "packs" / pack
        sprites, entry = "", {"atlases": [], "portrait_atlases": [], "sounds": 0, "ogg_bytes": 0}
        for name, a in atlases.items():
            if a["pack"] != pack:
                continue
            sprites += sprite_component(name, project_path(out / "atlases" / f"{name}.atlas"), a["first"])
            if a["mask"]:
                sprites += sprite_component(f"{name}_mask", project_path(out / "atlases" / f"{name}_mask.atlas"),
                                            a["first"])
            entry["atlases"].append({"name": name, **{k: a[k] for k in ("animations", "frames", "pixels", "mask")}})
        for name, first in portraits.get(pack, []):
            sprites += sprite_component(name, project_path(out / "atlases" / f"{name}.atlas"), first)
            entry["portrait_atlases"].append(name)
        instances = []
        if sprites:
            write_if_changed(directory / "atlases.go", sprites)
            instances.append(("atlases", directory / "atlases.go"))
        jobs, files = {}, []
        for sid, path in sounds.get(pack, []):
            ogg = out / "sounds" / pack / ogg_name(path, "sound\\")
            jobs.setdefault(ogg, r.read(path)[1])
            files.append((sid, ogg))
        cache.convert([(data, ogg) for ogg, data in jobs.items()])
        instances += write_sound_objects(files, out / "sounds" / pack, "sounds", "sfx", PACK_SOUND_GROUP)
        entry["objects"] = len(instances)
        entry["sounds"] = len(files)
        entry["ogg_files"] = len(jobs)
        entry["ogg_bytes"] = sum(p.stat().st_size for p in jobs)
        entry["wav_bytes"] = sum(len(d) for d in jobs.values())
        write_if_changed(out / "packs" / f"{pack}.collection", collection_text(f"pack_{pack}", instances))
        report[pack] = entry
        log.info("pack %s: atlases %s, %d sounds (%d OGG, %d bytes)", pack,
                 ", ".join(a["name"] for a in entry["atlases"]) or "-", entry["sounds"], entry["ogg_files"],
                 entry["ogg_bytes"])
    factories = "".join(embedded_component(p, "collectionfactory",
                                           f"prototype: {quote(project_path(out / 'packs' / f'{p}.collection'))}\n"
                                           "load_dynamically: true\n") for p in report)
    write_if_changed(out / "packs" / "packs.go", factories)
    return report


# --- mission assets -------------------------------------------------------------------

def mission_report(out: Path) -> list[dict]:
    path = out / "reports" / "missions.json"
    if not path.exists():
        raise PacksError(f"{path} is missing; run `python3 -m converter missions` first")
    return json.loads(path.read_text())["missions"]


def mission_id(mission: str) -> str:
    """'expterran/terran05a' -> 'expterran_terran05a' (component and collection names)."""
    return mission.replace("/", "_")


def write_mission_assets(r: Resolver, out: Path, cache: sound.OggCache) -> dict[str, dict]:
    """Per mission: OGG of each existing WAVS sound; component `wav<index>` is sound index
    `index` of the mission's WAVS section (the position among existing sounds in string-id
    order, docs/formats/kpm.md)."""
    report, proxies = {}, ""
    for m in mission_report(out):
        name = m["mission"]
        directory = out / "sounds" / "missions" / name
        existing = [s for s in sorted(m["sounds"], key=lambda s: s["string"]) if s["exists"]]
        jobs, files = {}, []
        for index, s in enumerate(existing):
            data = r.read(s["path"])[1]
            info = wav.read_info(data)
            if (info.data_length, info.avg_bytes_per_sec) != (s["data_length"], s["avg_bytes_per_sec"]):
                raise PacksError(f"{s['path']}: RIFF data differs from missions.json; rerun `missions`")
            ogg = directory / ogg_name(s["path"].rsplit("\\", 1)[1], "")
            jobs.setdefault(ogg, data)
            files.append((index, ogg))
        cache.convert([(data, ogg) for ogg, data in jobs.items()])
        instances = write_sound_objects(files, directory, "wavs", "wav", MISSION_SOUND_GROUP)
        collection = out / "missions" / f"{name}_assets.collection"
        write_if_changed(collection, collection_text(f"assets_{mission_id(name)}", instances))
        proxies += embedded_component(mission_id(name), "collectionproxy",
                                      f"collection: {quote(project_path(collection))}\nexclude: false\n")
        report[name] = {"objects": len(instances), "sounds": len(files), "ogg_files": len(jobs),
                        "ogg_bytes": sum(p.stat().st_size for p in jobs),
                        "wav_bytes": sum(len(d) for d in jobs.values())}
    write_if_changed(out / "missions" / "mission_assets.go", proxies)
    log.info("mission assets: %d missions, %d sounds, %d OGG bytes", len(report),
             sum(v["sounds"] for v in report.values()), sum(v["ogg_bytes"] for v in report.values()))
    return report


# --- limits ---------------------------------------------------------------------------

def minimums(packs: dict[str, dict], missions: dict[str, dict]) -> dict[str, int]:
    """Engine limits needed when every pack is instantiated in one world and one mission
    assets collection is loaded (ADR 0004)."""
    largest = max((v for v in missions.values()), key=lambda v: v["sounds"],
                  default={"sounds": 0, "ogg_files": 0, "objects": 0})
    atlases = sum(len(p["atlases"]) + sum(a["mask"] for a in p["atlases"]) + len(p["portrait_atlases"])
                  for p in packs.values())
    # Resource-cache entries: per pack its collection, game objects, one sprite and two texture
    # resources per atlas, every .sound and OGG; the same for the largest mission. The rest of
    # the game comes on top.
    resources = sum(1 + p["objects"] + p["sounds"] + p["ogg_files"] for p in packs.values()) + 3 * atlases
    resources += 1 + largest["objects"] + largest["sounds"] + largest["ogg_files"]
    return {
        "resource.max_resources": resources,
        # Sound resources resident at once: every pack's OGG files plus the largest mission's.
        "sound.max_sound_data": sum(p["ogg_files"] for p in packs.values()) + largest["ogg_files"],
        # Sound components per collection: all pack instances in the mission world.
        "sound.max_component_count": max(sum(p["sounds"] for p in packs.values()), largest["sounds"]),
        # Collection proxies in the world holding missions/mission_assets.go (plus the 8 tileset proxies).
        "collection_proxy.max_count": len(missions) + 8,
        "collectionfactory.max_count": len(packs),
        "sprite.max_count": atlases,
    }


def generate(r: Resolver, out: Path) -> dict:
    started = time.perf_counter()
    gs = kpd.grp_set(*kpd.grp_references(r.read("arr\\images.dat")[1], r.read("arr\\images.tbl")[1]),
                     lambda p: r.read(p)[1])
    cache = sound.OggCache(out / "sounds")
    pack_report = write_packs(r, gs, out, cache)
    mission_assets = write_mission_assets(r, out, cache)
    profiles.write(out)
    report = {"packs": pack_report, "missions": mission_assets, "minimums": minimums(pack_report, mission_assets)}
    write_if_changed(out / "reports" / "packs.json", json.dumps(report, indent=1, sort_keys=True) + "\n")
    for key, value in report["minimums"].items():
        log.info("minimum %s = %d", key, value)
    log.info("packs written in %.1f s (%d OGG files encoded)", time.perf_counter() - started, cache.converted)
    return report
