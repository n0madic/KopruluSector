"""Sound resources for the sound spike (11): WAV -> OGG (and WAV copies for comparison),
.sound files, pack game objects and a JSON manifest with the native RIFF metadata.

Plan §5.1: sounds.kpd keeps the source data length and nAvgBytesPerSec; the manifest here
carries the same fields for the spike until sounds.kpd exists.
"""
from __future__ import annotations

import json
import logging
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from ..formats import tbl, wav
from ..mpq.profile import Resolver
from .defold import ROOT, project_path, quote, write_if_changed

log = logging.getLogger(__name__)

OGG_QUALITY = "3"  # oggenc -q; mono 22 kHz speech/effects
RACE = "terran"
MISSION = "campaign\\terran\\terran01"
MUSIC = "music\\terran1.wav"
MEDIA_EVIDENCE = ROOT / "docs/research/evidence/campaign-media.json"


def to_ogg(data: bytes, out: Path) -> None:
    if out.exists():
        return
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["oggenc", "-Q", "-q", OGG_QUALITY, "-o", str(out), "-"], input=data, check=True)


def sound_text(path: Path, group: str, looping: bool = False) -> str:
    return (f"sound: {quote(project_path(path))}\nlooping: {int(looping)}\ngroup: {quote(group)}\n"
            "gain: 1.0\npan: 0.0\nspeed: 1.0\nloopcount: 0\n")


def go_text(component_paths: list[tuple[str, Path]]) -> str:
    return "".join(f"components {{\n  id: {quote(cid)}\n  component: {quote(project_path(p))}\n}}\n"
                   for cid, p in component_paths)


def collection_text(name: str, prototype: Path) -> str:
    return (f"name: {quote(name)}\nscale_along_z: 0\ninstances {{\n  id: \"sounds\"\n"
            f"  prototype: {quote(project_path(prototype))}\n}}\n")


def _file_id(mpq_path: str) -> str:
    return mpq_path.lower().replace("\\", "_").removesuffix(".wav")


def build_pack(r: Resolver, out: Path, name: str, paths: list[str], group: str, fmt: str) -> list[dict]:
    """Write one sound per path (ogg or wav), a pack .go and .collection; return manifest rows."""
    rows, components, jobs = [], [], []
    for i, mpq_path in enumerate(paths):
        data = r.read(mpq_path)[1]
        info = wav.read_info(data)
        fid = _file_id(mpq_path)
        audio = out / name / f"{fid}.{fmt}"
        if fmt == "ogg":
            jobs.append((data, audio))
        else:
            write_if_changed(audio, data)
        snd = out / name / f"{fid}.sound"
        write_if_changed(snd, sound_text(audio, group))
        cid = f"s{i:03d}"
        components.append((cid, snd))
        rows.append({"id": cid, "path": mpq_path, "data_length": info.data_length,
                     "avg_bytes_per_sec": info.avg_bytes_per_sec, "duration_ms": info.duration_ms,
                     "channels": info.channels, "sample_rate": info.sample_rate, "file": audio.name})
    with ThreadPoolExecutor() as pool:
        list(pool.map(lambda j: to_ogg(*j), jobs))
    for row in rows:
        row["bytes"] = (out / name / row["file"]).stat().st_size
    go = out / f"{name}.go"
    write_if_changed(go, go_text(components))
    write_if_changed(out / f"{name}.collection", collection_text(name, go))
    log.info("%s: %d sounds, %d bytes (%s)", name, len(rows), sum(x["bytes"] for x in rows), fmt)
    return rows


def generate(r: Resolver, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    sfx = tbl.sfx_paths(r.read("arr\\sfxdata.dat")[1], r.read("arr\\sfxdata.tbl")[1])
    race = sorted({p for p in sfx if p and p.lower().startswith(f"sound\\{RACE}\\")}, key=str.lower)
    race = list({p.lower(): p for p in race}.values())
    media = json.loads(MEDIA_EVIDENCE.read_text())
    mission_map = next(m for m in media["per_map"] if m["map"] == MISSION)
    mission = [MISSION + "\\" + w["path"] for w in mission_map["wav_files"]]
    manifest = {
        f"{RACE}_ogg": build_pack(r, out, f"{RACE}_ogg", race, "sfx", "ogg"),
        f"{RACE}_wav": build_pack(r, out, f"{RACE}_wav", race, "sfx", "wav"),
        "mission_ogg": build_pack(r, out, "mission_ogg", mission, "speech", "ogg"),
        "music_ogg": build_pack(r, out, "music_ogg", [MUSIC], "music", "ogg"),
    }
    write_if_changed(out / "sounds.json", json.dumps(manifest, indent=1))
