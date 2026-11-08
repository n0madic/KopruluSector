"""A language's speech (ADR 0016, localization's stage L5): `python3 -m converter speech --scr DIR --locale LOC`.

From a StarCraft: Remastered install holding the language, over the base's converted data (`missions`, `data` and
`packs` ran), it writes under <out>/speech/<locale>/:
  missions/<campaign>/<mission>.kpm         MISN overlay: TRIG and MBRF, the base's records with the language's TIME
                                            (an action's +0x0C: a transmission's and a wait's milliseconds)
  missions/<campaign>/<mission>_sounds.kpd  MSND by the base's WAVS sound indices: the language's spoken line, else
                                            the base's sound (a beep the language has no file of)
  units.kpd                                 MSND by sfx id: the language's unit voices (the base's packs keep the rest)
  sounds.kpd                                SNDS overlay: SFX, every sfx id's RIFF record, the language's where it has
                                            the file (the interface's talking and response durations)
and reports/speech-<locale>.json. The base's WAVS stays: its string ids and sound indices are the language's too, and
the engine plays by the index alone. Remastered's lines are Ogg Vorbis at 44.1 kHz and its voices 16-bit WAV: both go
to 22,050 Hz mono, then through the packs' OggCache (`oggenc -q 3`), as the base's sounds; a file the last run
converted from the same source is neither decoded nor encoded again (Encoder, sounds/speech/sources.json).
"""
from __future__ import annotations

import hashlib
import json
import logging
import struct
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .. import CONVERTER_VERSION
from ..formats import ffmpeg, tbl, wav
from ..kpf import kpd, kpm, resources
from ..kpf.container import Container, parse
from . import locale, sound
from .files import write_if_changed
from .locale import LocaleError
from .packs import mission_ogg_names, mission_report, ogg_name

log = logging.getLogger(__name__)

OUT_DIR = "speech"
SOUND_ROOT = Path("sounds") / "speech"  # the encoded OGG files and their OggCache, apart from the packs'
SOURCES = "sources.json"  # under SOUND_ROOT: the digest of each OGG's source (Encoder)
TRIGGER_RECORD = 2400  # CHK TRIG and MBRF
CONDITIONS = 16 * 20  # a record's 16 conditions of 20 bytes before its actions
ACTION = 32
ACTIONS = 64
ACTION_TIME = 0x0C  # KP_ACTION_TIME: u32 milliseconds
TIME_SIZE = 4
TRIGGER_SECTIONS = ("TRIG", "MBRF")
SAMPLE_RATE = "22050"  # the base's spoken lines' rate (missions.json: nAvgBytesPerSec 44100 of 16-bit mono)
UNITS = "units.kpd"
SOUNDS = "sounds.kpd"
SFX_PATHS = ("arr\\sfxdata.dat", "arr\\sfxdata.tbl")


class SpeechError(LocaleError):
    """The language's speech fails a check, or the base's data it lays over is not there."""


def time_merged(where: str, base: bytes, theirs: bytes, problems: list[str]) -> tuple[bytes, int, list[str]]:
    """(the base's records with the language's TIME, the actions whose TIME changed, the differences other than TIME):
    an action that differs from the base's in more than its TIME keeps the base's whole; a different size is a problem
    and gives the base."""
    if len(base) != len(theirs) or len(base) % TRIGGER_RECORD:
        problems.append(f"{where}: {len(theirs)} bytes for the base's {len(base)}")
        return base, 0, []
    out, changed, differ = bytearray(base), 0, []
    for r in range(0, len(base), TRIGGER_RECORD):
        if base[r:r + CONDITIONS] != theirs[r:r + CONDITIONS]:
            differ.append(f"record {r // TRIGGER_RECORD} conditions")
        tail = r + CONDITIONS + ACTIONS * ACTION
        if base[tail:r + TRIGGER_RECORD] != theirs[tail:r + TRIGGER_RECORD]:
            differ.append(f"record {r // TRIGGER_RECORD} flags")
        for k in range(ACTIONS):
            at = r + CONDITIONS + k * ACTION
            time = at + ACTION_TIME
            if base[at:time] + base[time + TIME_SIZE:at + ACTION] != theirs[at:time] + theirs[time + TIME_SIZE:at + ACTION]:
                differ.append(f"record {r // TRIGGER_RECORD} action {k}")
            elif base[time:time + TIME_SIZE] != theirs[time:time + TIME_SIZE]:
                out[time:time + TIME_SIZE] = theirs[time:time + TIME_SIZE]
                changed += 1
    return bytes(out), changed, differ


def speech_exceptions(defs: dict, locale_id: str) -> dict[str, str]:
    """locales.json's `speech`: a language's missions whose triggers may differ from the base's in more than TIME
    (Remastered's own fixes), mission -> why."""
    return {e["mission"]: e["why"] for e in defs.get("speech", ()) if e["locale"] == locale_id}


def pcm(data: bytes, what: str) -> bytes:
    """A sound file (Ogg Vorbis or WAV) as 16-bit mono WAV at the base's rate; SpeechError when FFmpeg fails."""
    run = subprocess.run(["ffmpeg", "-v", "error", "-i", "pipe:0", "-map_metadata", "-1", "-ac", "1", "-ar", SAMPLE_RATE,
                          "-c:a", "pcm_s16le", "-fflags", "+bitexact", "-flags:a", "+bitexact", "-f", "wav", "pipe:1"],
                         input=data, capture_output=True, check=False)
    if run.returncode or not run.stdout:
        raise SpeechError(f"{what}: ffmpeg failed: {run.stderr.decode(errors='replace').strip()}")
    return run.stdout


def decoded(jobs: dict[Path, tuple[bytes, str]]) -> dict[Path, bytes]:
    """OGG path -> the WAV bytes of its source, decoded in parallel."""
    with ThreadPoolExecutor() as pool:
        return dict(zip(jobs, pool.map(lambda job: pcm(*job), jobs.values())))


class Encoder:
    """The language's files to OGG through the packs' OggCache, decoded only when they changed: <root>/sources.json maps
    each OGG (relative to the cache's root) to the SHA-256 of the language's file it was decoded from, with the rate and
    the encoder's settings. The cache's own key is the decoded WAV, so without this every run would decode every file."""

    def __init__(self, cache: sound.OggCache):
        self.cache = cache
        self.path = cache.root / SOURCES
        self.sources: dict[str, str] = json.loads(self.path.read_text()) if self.path.exists() else {}

    def encode(self, jobs: dict[Path, tuple[bytes, str]]) -> None:
        """(OGG path -> the language's file and its name): the OGGs that are not current decoded and encoded."""
        todo, digests = {}, {}
        for ogg, (data, what) in jobs.items():
            key = ogg.relative_to(self.cache.root).as_posix()
            digest = hashlib.sha256(data + f"r{SAMPLE_RATE}q{sound.OGG_QUALITY}s{sound.serial(ogg)}".encode()).hexdigest()
            if not (ogg.exists() and key in self.cache.entries and self.sources.get(key) == digest):
                todo[ogg] = (data, what)
                digests[key] = digest
        if not todo:
            return
        self.cache.convert([(wav, ogg) for ogg, wav in decoded(todo).items()])
        self.sources.update(digests)
        write_if_changed(self.path, json.dumps(self.sources, indent=0, sort_keys=True) + "\n")


def language_path(locale_id: str, mpq_path: str, suffix: str) -> str:
    """'campaign\\terran\\terran01\\staredit\\wav\\T1M00jim.wav' -> its file under the language's assets, `suffix` in
    place of `.wav` (the install's names are looked up without regard to case)."""
    stem = mpq_path.replace("\\", "/")
    stem = stem[:-len(".wav")] if stem.lower().endswith(".wav") else stem
    return locale.ASSETS.format(locale=locale_id) + stem + suffix


def base_sounds(path: Path) -> dict[int, bytes]:
    """An MSND's SNDS, id -> OGG bytes."""
    payload = parse(locale.base_file(path, "packs")).section("SNDS").payload
    count, at, out = struct.unpack_from("<H", payload)[0], 2, {}
    for _ in range(count):
        ident, size = struct.unpack_from("<HI", payload, at)
        out[ident] = payload[at + 6:at + 6 + size]
        at += 6 + size
    return out


def line_sources(install, locale_id: str, paths: list[str], ogg_dir: Path, fallback: dict[int, bytes],
                 mission: str) -> tuple[dict[Path, tuple[bytes, str]], list[tuple[int, Path | bytes]], list[str]]:
    """A mission's lines by the base's WAVS index (the order of its existing sounds, as packs.py has them): (OGG path ->
    the language's file and its name, [(index, its OGG path or the base's OGG bytes)], the lines the base's sound
    keeps). A line the install lacks, or keeps empty (Remastered's silent nullsound files), keeps the base's sound; one
    the base lacks too is left out."""
    names = mission_ogg_names(mission, paths)
    jobs: dict[Path, tuple[bytes, str]] = {}
    files: list[tuple[int, Path | bytes]] = []
    kept: list[str] = []
    for index, path in enumerate(paths):
        source = language_path(locale_id, path, ".ogg")
        data = install.read(source) if install.has(source) else b""
        if data:
            ogg = ogg_dir / names[path]
            jobs.setdefault(ogg, (data, source))
            files.append((index, ogg))
        elif index in fallback:
            kept.append(path)
            files.append((index, fallback[index]))
    return jobs, files, kept


def exception_problems(excepted: dict[str, str], report: dict) -> list[str]:
    """locales.json's speech exceptions that name no mission, or one whose triggers now differ from the base's in TIME
    alone (a stale exception would let a later difference there pass)."""
    problems = []
    for name in sorted(excepted):
        if name not in report:
            problems.append(f"locales.json's speech names no mission {name}")
        elif report[name].get("map", True) and "differ" not in report[name]:
            problems.append(f"locales.json's speech excepts {name}, whose triggers differ from the base's in TIME alone")
    return problems


def convert_missions(install: locale.Install, locale_id: str, out: Path, encoder: Encoder, problems: list[str],
                     excepted: dict[str, str]) -> tuple[dict[str, bytes], dict[str, bytes], dict]:
    """(mission -> its TRIG and MBRF overlay, mission -> its MSND, the report's per mission)."""
    from ..formats import chk

    tables = chk.LoaderTables.load(parse(locale.base_file(out / "data" / "strings.kpd", "data")).profile_id)
    overlays, sounds, report = {}, {}, {}
    for m in mission_report(out):
        name = m["mission"]
        base = parse(locale.base_file(out / "missions" / f"{name}.kpm", "missions"))
        path = locale.MISSION.format(locale=locale_id, name=name)
        entry = report.setdefault(name, {"times": 0, "lines": 0, "base_sounds": []})
        if install.has(path):
            try:
                theirs = chk.load(install.read(path), tables, vcod_sha256=tables.vcod_sha256).sections
            except chk.ChkError as e:
                problems.append(f"{name}: {e}")
                theirs = None
            if theirs is not None:
                c = Container(kpm.KIND, kpm.SCHEMA, CONVERTER_VERSION, base.profile_id, source_hash=base.source_hash)
                differ = []
                for tag in TRIGGER_SECTIONS:
                    section = base.section(tag)
                    merged, changed, apart = time_merged(f"{name} {tag}", section.payload, theirs.get(tag, b""), problems)
                    differ += [f"{tag} {d}" for d in apart]
                    entry["times"] += changed
                    c.add(tag, merged, schema_version=section.schema_version)
                if differ:
                    entry["differ"] = differ
                    if name not in excepted:
                        problems.append(f"{name}: the triggers differ from the base's in more than TIME: "
                                        f"{', '.join(differ[:4])} (locales.json's speech lists a mission's own fixes)")
                    else:
                        entry["why"] = excepted[name]
                overlays[name] = c.to_bytes()
        else:
            entry["map"] = False
        existing = [s for s in sorted(m["sounds"], key=lambda s: s["string"]) if s["exists"]]
        jobs, files, entry["base_sounds"] = line_sources(
            install, locale_id, [s["path"] for s in existing], out / SOUND_ROOT / locale_id / "missions" / name,
            base_sounds(out / "missions" / f"{name}_sounds.kpd"), name)
        encoder.encode(jobs)
        entry["lines"] = len(jobs)
        sounds[name] = resources.build_mission_sounds(
            [(i, f.read_bytes() if isinstance(f, Path) else f) for i, f in files], profile_id=base.profile_id,
            data_set_hash=base.source_hash).to_bytes()
    problems += exception_problems(excepted, report)
    return overlays, sounds, report


def convert_units(install: locale.Install, locale_id: str, out: Path, encoder: Encoder,
                  base: Container) -> tuple[bytes, bytes, dict]:
    """(units.kpd, sounds.kpd, the report's): the language's voices by sfx id and every sfx id's RIFF record."""
    paths = tbl.sfx_paths(*(install.read(name.replace("\\", "/")) for name in SFX_PATHS))
    records = base.section("SFX ").payload
    if struct.unpack_from("<H", records)[0] != len(paths):
        raise SpeechError(f"sounds.kpd has {struct.unpack_from('<H', records)[0]} sounds, sfxdata {len(paths)}")
    jobs, voices, record = {}, [], bytearray(records[:2])
    size = kpd.SOUND_RECORD.size
    for sid, path in enumerate(paths):
        source = language_path(locale_id, path, ".wav") if path else ""
        if path and install.has(source):
            data = install.read(source)
            ogg = out / SOUND_ROOT / locale_id / "units" / ogg_name(path, "sound\\")
            jobs[ogg] = (data, source)
            voices.append((sid, ogg))
            record += kpd.sound_record(data)
        else:
            record += records[2 + sid * size:2 + (sid + 1) * size]
    encoder.encode(jobs)
    units = resources.build_mission_sounds([(sid, ogg.read_bytes()) for sid, ogg in voices],
                                           profile_id=base.profile_id, data_set_hash=base.source_hash)
    sfx = Container(kpd.SOUNDS_KIND, kpd.SOUNDS_SCHEMA, CONVERTER_VERSION, base.profile_id, source_hash=base.source_hash)
    sfx.add("SFX ", bytes(record))
    return units.to_bytes(), sfx.to_bytes(), {"voices": len(voices)}


def generate(storage, locale_id: str, out: Path) -> dict:
    """The language's speech under <out>/speech/<locale>/ and its report; SpeechError when a check fails (then only
    the report is written)."""
    defs = locale.load_locales()
    locale.check_locale(locale_id, defs)
    ffmpeg.require(SpeechError, "decode StarCraft: Remastered's speech")
    install = locale.Install(storage)
    if not install.has(locale.ASSETS.format(locale=locale_id) + "sound/Terran/advisor/TAdUpd00.wav"):
        raise SpeechError(f"the install has no speech of {locale_id}")
    cache = sound.OggCache(out / SOUND_ROOT)
    problems: list[str] = []
    base = parse(locale.base_file(out / "data" / SOUNDS, "data"))
    excepted = speech_exceptions(defs, locale_id)
    encoder = Encoder(cache)
    overlays, sounds, mission_counts = convert_missions(install, locale_id, out, encoder, problems, excepted)
    units, sfx, unit_counts = convert_units(install, locale_id, out, encoder, base)
    report = {"locale": locale_id, "missions": mission_counts, "units": unit_counts, "problems": problems}
    write_if_changed(out / "reports" / f"speech-{locale_id}.json",
                     (json.dumps(report, indent=1, ensure_ascii=False) + "\n").encode("utf-8"))
    if problems:
        raise SpeechError(f"{locale_id}: {len(problems)} problems (reports/speech-{locale_id}.json): "
                          f"{'; '.join(problems[:locale.MAX_LISTED])}")
    root = out / OUT_DIR / locale_id
    written: set[Path] = set()

    def emit(path: Path, data: bytes) -> None:
        write_if_changed(path, data)
        written.add(path)

    for name, data in overlays.items():
        emit(root / "missions" / f"{name}.kpm", data)
    for name, data in sounds.items():
        emit(root / "missions" / f"{name}_sounds.kpd", data)
    emit(root / UNITS, units)
    emit(root / SOUNDS, sfx)
    for path in root.rglob("*"):
        if path.is_file() and path not in written:
            log.info("speech %s: removing %s, which this conversion did not write", locale_id, path)
            path.unlink()
    log.info("speech %s: %d missions' triggers, %d lines, %d voices, %d sounds encoded", locale_id, len(overlays),
             sum(v["lines"] for v in mission_counts.values()), unit_counts["voices"], cache.converted)
    return report
