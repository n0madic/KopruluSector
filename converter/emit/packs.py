"""Resource packs (ADR 0004, ADR 0012): the pack, music and mission sound containers of the runtime.

Layout and naming: docs/formats/packs.md. Outputs under <out>/:
  packs/<pack>.kpd                          kind PACK: index pages of the pack's atlases, frame records
                                            and the OGG files of its sounds (converter/kpf/resources.py)
  packs/music.kpd                           kind MUSC: the music tracks' OGG files
  missions/<campaign>/<mission>_sounds.kpd  kind MSND: the mission's WAVS sounds as OGG
  sounds/...                                the encoded OGG files (a cache of sound.OggCache)
  reports/packs.json                        contents and sizes of the containers
"""
from __future__ import annotations

import json
import logging
import time
from pathlib import Path

from .. import packs as pack_rules
from .. import ui_tables
from ..formats import tbl, wav
from ..kpf import kpd, resources
from ..kpf.container import Container
from ..mpq.profile import Resolver
from . import pages, sound
from .files import write_if_changed
from ..errors import ConverterError

log = logging.getLogger(__name__)

MUSIC_PACK = "music"
MUSIC_LOOPS = 1  # MUSI kind 1: StartMusicTrack 0x46f1b0 opens the stream with SFileDdaBeginEx's loop flag 0x40000


class PacksError(ConverterError, RuntimeError):
    """Inputs of the pack resources are missing."""


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


Ident = tuple[str, bytes]  # (profile id, data-set hash) of every container of the data set


def write_container(path: Path, container: Container) -> int:
    data = container.to_bytes()
    changed = write_if_changed(path, data)
    log.info("%s %s: %d bytes", "wrote" if changed else "same ", path, len(data))
    return len(data)


def write_pack_container(gs: kpd.GrpSet, pack: str, files: list[tuple[int, Path]], out: Path, ident: Ident) -> dict:
    """packs/<pack>.kpd from the pack's GRPs and its (sfx id, OGG path) sounds; returns its report fields."""
    container, stats = resources.build_pack(gs, pack, [(sid, ogg.read_bytes()) for sid, ogg in files],
                                            profile_id=ident[0], data_set_hash=ident[1])
    size = write_container(out / "packs" / f"{pack}.kpd", container)
    log.info("pack %s: %d frames (%d distinct), %d pages of %d^2, fill %.1f%%", pack, stats.frames,
             stats.unique_frames, stats.pages, pages.PAGE_SIZE, 100 * stats.fill)
    return {"kpd_bytes": size, "pages": stats.pages, "page_fill": round(stats.fill, 4),
            "page_pixels": stats.pixels, "distinct_frames": stats.unique_frames}


def write_packs(r: Resolver, gs: kpd.GrpSet, out: Path, cache: sound.OggCache, ident: Ident) -> dict[str, dict]:
    sounds = pack_sounds(r)
    report: dict[str, dict] = {}
    names = [p for p in pack_rules.PACKS if any(pack == p for _name, pack, _mask in gs.atlases) or p in sounds]
    for pack in names:
        jobs, files = {}, []
        for sid, path in sounds.get(pack, []):
            ogg = out / "sounds" / pack / ogg_name(path, "sound\\")
            jobs.setdefault(ogg, r.read(path)[1])
            files.append((sid, ogg))
        cache.convert([(data, ogg) for ogg, data in jobs.items()])
        entry = {"atlases": sorted(name for name, p, _mask in gs.atlases if p == pack), "sounds": len(files),
                 "ogg_files": len(jobs), "ogg_bytes": sum(p.stat().st_size for p in jobs),
                 "wav_bytes": sum(len(d) for d in jobs.values())}
        entry.update(write_pack_container(gs, pack, files, out, ident))
        report[pack] = entry
        log.info("pack %s: atlases %s, %d sounds (%d OGG, %d bytes)", pack, ", ".join(entry["atlases"]) or "-",
                 entry["sounds"], entry["ogg_files"], entry["ogg_bytes"])
    return report


def write_music(r: Resolver, out: Path, cache: sound.OggCache, ident: Ident) -> dict:
    """The music tracks (ui.kpd MUSI, `0x508c58`): packs/music.kpd carries every track, the OGG bytes repeated for
    tracks of one file."""
    jobs, tracks = {}, []
    for path, kind, _fade, _next in ui_tables.load(ui_tables.PROFILE)["music"]:
        if not path or not r.has(path):
            tracks.append((kind == MUSIC_LOOPS, None))
            continue
        ogg = out / "sounds" / "music" / ogg_name(path, "music\\")
        tracks.append((kind == MUSIC_LOOPS, ogg))
        jobs.setdefault(ogg, r.read(path)[1])
    cache.convert([(data, ogg) for ogg, data in jobs.items()])
    log.info("music: %d tracks, %d OGG files", len(tracks), len(jobs))
    size = write_container(out / "packs" / f"{MUSIC_PACK}.kpd", resources.build_music(
        [(loops, ogg.read_bytes() if ogg else b"") for loops, ogg in tracks], profile_id=ident[0],
        data_set_hash=ident[1]))
    return {"sounds": sum(ogg is not None for _loops, ogg in tracks), "ogg_files": len(jobs),
            "ogg_bytes": sum(p.stat().st_size for p in jobs), "wav_bytes": sum(len(d) for d in jobs.values()),
            "atlases": [], "tracks": len(tracks), "kpd_bytes": size}


# --- mission assets -------------------------------------------------------------------

def mission_report(out: Path) -> list[dict]:
    path = out / "reports" / "missions.json"
    if not path.exists():
        raise PacksError(f"{path} is missing; run `python3 -m converter missions` first")
    return json.loads(path.read_text())["missions"]


def mission_ogg_names(mission: str, paths: list[str]) -> dict[str, str]:
    """OGG file name per WAV path of mission `mission` (`terran/terran01`): the WAV's file
    name, or its path below the mission directory when two different files of the mission
    share that file name. Paths equal but for case are one archive file and share a name."""
    prefix = "campaign\\" + mission.replace("/", "\\") + "\\"
    files: dict[str, set[str]] = {}
    for path in paths:
        files.setdefault(path.rsplit("\\", 1)[-1].lower(), set()).add(path.lower())
    names = {}
    for path in paths:
        lower = path.lower()
        unique = len(files[lower.rsplit("\\", 1)[-1]]) == 1
        names[path] = ogg_name(lower.rsplit("\\", 1)[-1] if unique else lower.removeprefix(prefix), "")
    return names


def write_mission_assets(r: Resolver, out: Path, cache: sound.OggCache, ident: Ident) -> dict[str, dict]:
    """Per mission: the OGG of each existing WAVS sound; SNDS entry `index` of missions/<campaign>/<mission>_sounds.kpd
    is sound index `index` of the mission's WAVS section (the position among existing sounds in string-id order,
    docs/formats/kpm.md)."""
    report = {}
    for m in mission_report(out):
        name = m["mission"]
        directory = out / "sounds" / "missions" / name
        existing = [s for s in sorted(m["sounds"], key=lambda s: s["string"]) if s["exists"]]
        names = mission_ogg_names(name, [s["path"] for s in existing])
        jobs, files = {}, []
        for index, s in enumerate(existing):
            data = r.read(s["path"])[1]
            info = wav.read_info(data)
            if (info.data_length, info.avg_bytes_per_sec) != (s["data_length"], s["avg_bytes_per_sec"]):
                raise PacksError(f"{s['path']}: RIFF data differs from missions.json; rerun `missions`")
            ogg = directory / names[s["path"]]
            jobs.setdefault(ogg, data)
            files.append((index, ogg))
        cache.convert([(data, ogg) for ogg, data in jobs.items()])
        size = write_container(out / "missions" / f"{name}_sounds.kpd", resources.build_mission_sounds(
            [(index, ogg.read_bytes()) for index, ogg in files], profile_id=ident[0], data_set_hash=ident[1]))
        report[name] = {"sounds": len(files), "ogg_files": len(jobs), "ogg_bytes": sum(p.stat().st_size for p in jobs),
                        "wav_bytes": sum(len(d) for d in jobs.values()), "kpd_bytes": size}
    log.info("mission sounds: %d missions, %d sounds, %d OGG bytes", len(report),
             sum(v["sounds"] for v in report.values()), sum(v["ogg_bytes"] for v in report.values()))
    return report


def generate(r: Resolver, out: Path) -> dict:
    started = time.perf_counter()
    gs = kpd.grp_set(*kpd.grp_references(r.read("arr\\images.dat")[1], r.read("arr\\images.tbl")[1]),
                     lambda p: r.read(p)[1])
    cache = sound.OggCache(out / "sounds")
    ident = (r.profile.id, r.profile.data_set_hash())
    pack_report = write_packs(r, gs, out, cache, ident)
    pack_report[MUSIC_PACK] = write_music(r, out, cache, ident)
    mission_sounds = write_mission_assets(r, out, cache, ident)
    report = {"packs": pack_report, "missions": mission_sounds,
              "kpd_bytes": {"packs": sum(p["kpd_bytes"] for p in pack_report.values()),
                            "missions": sum(m["kpd_bytes"] for m in mission_sounds.values())}}
    write_if_changed(out / "reports" / "packs.json", json.dumps(report, indent=1, sort_keys=True) + "\n")
    log.info("packs written in %.1f s (%d OGG files encoded)", time.perf_counter() - started, cache.converted)
    return report
