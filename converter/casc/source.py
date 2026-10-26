"""The profile's original files read from a StarCraft: Remastered install (`--scr`).

Remastered keeps most of 1.17's files byte for byte, some under `SD/` or the enUS locale, some moved, and
grows the tilesets by appending (docs/research/remastered.md). `ScrSource` looks an MPQ path up in that order,
rebuilds what is derivable, and checks every result against the manifest `defs/scr/inputs-<profile>.json`
(SHA-256 and size of each file the conversion reads from the 1.17 install, written by `scr-manifest`):

- `exact`: a CASC copy has the manifest's hash;
- `derived`: rebuilt from a CASC file to the manifest's hash (a tileset's original prefix, `.vx4` from `.vx4ex`);
- `differs`: no candidate reaches the hash; the first readable copy is used (an approximation);
- `unlisted`: a name the manifest does not hold, read from CASC unchecked.

A name the manifest lists as absent stays absent, and one it lists as unreadable stays unreadable, so the
conversion sees the data set the 1.17 install has. `reports/scr-import.json` records each file's status.
"""
from __future__ import annotations

import hashlib
import json
import logging
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from ..emit.files import write_if_changed
from ..formats import dds, grp, kpv, pcx
from ..mpq.profile import DEFAULT_PROFILE, Profile
from ..provenance import DEFS_ROOT
from ..source import SourceError, SourceUnreadable
from . import rebuild
from .rebuild import OPAQUE
from .storage import CascError, CascStorage, normalize

log = logging.getLogger(__name__)

MANIFEST_DIR = DEFS_ROOT / "scr"
DIALOG_FRAMES = re.compile(r"^SD/glue/pal[a-z]+/dlg\.dds\.grp$", re.I)
REPORT = Path("reports") / "scr-import.json"
CACHE = Path("cache") / "scr"  # under the output directory
KPV_RULE = b"kpv1 screen palette, lowest index, alpha 128"  # a change of the conversion invalidates the cache
# Where Remastered keeps the profile's files: its SD copies (sounds, campaign maps, briefing texts) first,
# since the copies at the root are its own where both exist (16-bit sounds); the enUS locale last.
PREFIXES = ("SD/", "", "locales/enUS/Assets/SD/", "locales/enUS/Assets/")
# Original files Remastered keeps under another name (found by content).
MOVED = {
    "dlgs/protoss.grp": ("game/consoles/protoss/dlg.grp",),
    "dlgs/terran.grp": ("game/consoles/terran/dlg.grp",),
    "dlgs/zerg.grp": ("game/consoles/zerg/dlg.grp",),
    "files/font/font8.fnt": ("SD/font/font8.fnt",),
    "files/font/font10.fnt": ("SD/font/font10.fnt",),
    "files/font/font14.fnt": ("SD/font/font14.fnt",),
    "files/font/font16.fnt": ("SD/font/font16.fnt",),
    "files/font/font16x.fnt": ("SD/font/font16x.fnt",),
    "glue/palnl/popopup.pcx": ("glue/Battlenet/ToonRegistry/popopup.pcx",),
    "unit/cmdbtns/cmdicons.grp": ("unit/cmdicons/cmdicons.grp",),
    "unit/cmdbtns/pcmdbtns.grp": ("game/consoles/protoss/cmdbtns.grp",),
    "unit/cmdbtns/tcmdbtns.grp": ("game/consoles/terran/cmdbtns.grp",),
    "unit/cmdbtns/zcmdbtns.grp": ("game/consoles/zerg/cmdbtns.grp",),
    "unit/cmdbtns/ticon.pcx": ("game/ticon.pcx",),
}
# Original GRPs Remastered dropped, rebuilt from another GRP drawn with another palette: the glue's arrow cursors
# are the game's (cursor\\arrow.grp, in the game palette) in each glue palette.
REMAPPED = {"arrow.grp": ("cursor\\arrow.grp", "tileset\\badlands.wpe")}
# Tileset files Remastered grows by appending records: the original is the prefix of the manifest's size.
GROWN = (".cv5", ".vf4", ".vr4")
VX4EX = ".vx4ex"  # `.vx4`'s 16-bit minitile references widened to 32 bits
CV5_GROUP = 52
# CV5 groups that are empty (all zero) in 1.17 and that Remastered fills with its own terrain: [first, end).
CV5_FILLED = {"desert": (770, 1024), "platform": (933, 1024), "twilight": (797, 1024)}


@dataclass(frozen=True)
class Entry:
    sha256: str
    size: int
    name: str  # as the converter spells it
    extras: dict  # what rebuild.describe keeps (images, SMKs), a GRP's palette


@dataclass(frozen=True)
class Manifest:
    profile: str
    files: dict[str, Entry]  # by normalized name
    absent: frozenset[str]
    unreadable: frozenset[str]
    texts: dict  # defs/scr/texts-<profile>.json: string tables ("tbl") and dialogs ("bin")
    palettes: dict[str, list[str]]  # palette hash -> the PCX files with it

    @classmethod
    def load(cls, profile_id: str = DEFAULT_PROFILE) -> "Manifest":
        path = manifest_path(profile_id)
        if not path.is_file():
            raise SourceError(f"no Remastered input manifest {path} (python3 -m converter scr-manifest)")
        raw = json.loads(path.read_text())
        texts_file = texts_path(profile_id)
        texts = json.loads(texts_file.read_text()) if texts_file.is_file() else {"tbl": {}, "bin": {}}
        files = {normalize(row[0]): Entry(row[2], row[1], row[0], row[3] if len(row) > 3 else {}) for row in raw["files"]}
        return cls(raw["profile"], files, frozenset(normalize(n) for n in raw["absent"]),
                   frozenset(normalize(n) for n in raw["unreadable"]),
                   {kind: {normalize(n): v for n, v in texts.get(kind, {}).items()} for kind in ("tbl", "bin")},
                   raw.get("palettes", {}))


def manifest_path(profile_id: str) -> Path:
    return MANIFEST_DIR / f"inputs-{profile_id}.json"


def texts_path(profile_id: str) -> Path:
    return MANIFEST_DIR / f"texts-{profile_id}.json"


def palette_groups(files: dict[str, tuple[str, int, dict | None]]) -> tuple[dict[str, list[str]], dict[str, str]]:
    """The PCX files of each palette (by its hash), and for each GRP of a directory holding PCX files the palette most
    of them have: a rebuilt image takes its palette from a PCX of its group that Remastered keeps exactly."""
    groups: dict[str, list[str]] = {}
    by_dir: dict[str, list[str]] = {}
    for n, (_, _, extras) in sorted(files.items(), key=lambda kv: kv[0].lower()):
        if n.lower().endswith(".pcx") and extras:
            groups.setdefault(extras["palette"], []).append(n)
            by_dir.setdefault(n.lower().rsplit("\\", 1)[0], []).append(extras["palette"])
    grp_palettes = {}
    for n in files:
        folder = n.lower().rsplit("\\", 1)[0]
        if n.lower().endswith(".grp") and folder in by_dir:
            palettes = by_dir[folder]
            grp_palettes[n] = max(sorted(set(palettes)), key=palettes.count)
    return groups, grp_palettes


def write_manifest(path: Path, profile_id: str, files: dict[str, tuple[str, int, dict | None]], absent: set[str],
                   unreadable: set[str]) -> None:
    """One file per line: [name, size, sha256] and its extras (rebuild.describe; a GRP's palette), sorted by name;
    the PCX files of each palette."""
    groups, grp_palettes = palette_groups(files)
    pcx_palettes = {e[2]["palette"]: e[2]["_palette_rgb"] for n, e in files.items()
                    if n.lower().endswith(".pcx") and e[2]}
    rows = []
    for name, (sha, size, extras) in sorted(files.items(), key=lambda kv: kv[0].lower()):
        extras = dict(extras or {})
        if name in grp_palettes:
            extras["palette"] = grp_palettes[name]
        if "_used" in extras:
            screen = rebuild.screen_palette(extras["_palette_rgb"], extras["_used"], pcx_palettes)
            if screen is not None:
                extras["palette"] = screen
        extras = {k: v for k, v in extras.items() if not k.startswith("_")}  # kept for the steps above only
        rows.append([name, size, sha, extras] if extras else [name, size, sha])
    lines = [json.dumps(row) for row in rows]
    absent_lines = [json.dumps(n) for n in sorted(absent - set(files), key=str.lower)]
    text = (
        "{\n"
        f' "comment": "The files profile {profile_id}\'s conversion reads from the 1.17 install: name, size, SHA-256 '
        '(hashes only). converter/casc/source.py checks a StarCraft: Remastered install against them; written by '
        'python3 -m converter scr-manifest --data DIR.",\n'
        f' "profile": {json.dumps(profile_id)},\n'
        ' "files": [\n  ' + ",\n  ".join(lines) + "\n ],\n"
        ' "absent": [\n  ' + ",\n  ".join(absent_lines) + "\n ],\n"
        ' "palettes": {\n  ' + ",\n  ".join(f"{json.dumps(k)}: {json.dumps(v)}" for k, v in sorted(groups.items()))
        + "\n },\n"
        f' "unreadable": {json.dumps(sorted(unreadable, key=str.lower))}\n'
        "}\n"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(path, text)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def vx4_from_vx4ex(data: bytes, size: int) -> bytes | None:
    """The first `size` bytes of `.vx4` from `.vx4ex`; None when the prefix is not 16-bit references."""
    count = size // 2
    if size % 2 or len(data) < count * 4:
        return None
    refs = struct.unpack_from(f"<{count}I", data)
    if any(r > 0xFFFF for r in refs):
        return None
    return struct.pack(f"<{count}H", *refs)


class ScrSource:
    """A StarCraft: Remastered install as a source of profile `profile`'s files."""

    def __init__(self, root: str | Path, profile: Profile | None = None, manifest: Manifest | None = None,
                 cache: Path | None = None):
        self.profile = profile or Profile.load()
        self.manifest = manifest or Manifest.load(self.profile.id)
        if self.manifest.profile != self.profile.id:
            raise SourceError(f"manifest of {self.manifest.profile}, profile {self.profile.id}")
        self.storage = CascStorage(root)
        self.version = self.storage.version
        self.status: dict[str, tuple[str, str]] = {}  # name -> (status, CASC name)
        self._rebuilt: dict[str, tuple[str, str, bytes] | None] = {}  # None: cannot be rebuilt
        self._making: set[str] = set()
        self.cache = cache  # movies converted from WebM, by their inputs' hash (they take FFmpeg and a while)
        self._xml: dict[str, dict[str, bytes]] = {}

    def close(self) -> None:
        self.storage.close()

    def __enter__(self) -> "ScrSource":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def data_set_hash(self) -> bytes:
        build = self.storage.build_info.get("Build Key", "")
        return hashlib.sha256(f"scr {self.version} {build} {self.profile.id}".encode()).digest()

    def _candidates(self, name: str) -> list[str]:
        key = normalize(name)
        names = list(MOVED.get(key, ()))
        names += [p + key for p in PREFIXES]
        if key.endswith(".vx4"):
            names += [p + key + "ex" for p in PREFIXES]
        return [n for n in names if self.storage.has(n)]

    def has(self, name: str) -> bool:
        key = normalize(name)
        if key in self.manifest.files or key in self.manifest.unreadable:
            return True
        if key in self.manifest.absent:
            return False
        return bool(self._candidates(name))

    def _rebuild(self, key: str, casc_name: str, data: bytes, size: int) -> bytes | None:
        if casc_name.lower().endswith(VX4EX):
            return vx4_from_vx4ex(data, size)
        if not key.endswith(GROWN) or len(data) < size:
            return None
        data = data[:size]
        filled = CV5_FILLED.get(Path(key).stem) if key.endswith(".cv5") else None
        if filled:
            first, end = (g * CV5_GROUP for g in filled)
            data = data[:first] + bytes(end - first) + data[end:]
        return data

    def read(self, name: str) -> tuple[str, bytes]:
        key = normalize(name)
        if key in self.manifest.unreadable:
            self.status[name] = ("unreadable", "")
            raise SourceUnreadable(f"{name}: unreadable in profile {self.profile.id}'s install")
        if key in self.manifest.absent:
            raise SourceError(f"{name}: not in profile {self.profile.id}")
        if self._rebuilt.get(key) is not None:
            status, casc_name, data = self._rebuilt[key]
            return self._found(name, status, casc_name, data)
        expected = self.manifest.files.get(key)
        fallback = None
        for casc_name in self._candidates(name):
            data = self._casc(casc_name)
            if data is None:
                continue
            if expected is None:
                return self._found(name, "unlisted", casc_name, data)
            if _sha(data) == expected.sha256:
                return self._found(name, "exact", casc_name, data)
            rebuilt = self._rebuild(key, casc_name, data, expected.size)
            if rebuilt is not None and _sha(rebuilt) == expected.sha256:
                return self._found(name, "derived", casc_name, rebuilt)
            if fallback is None and not casc_name.lower().endswith(VX4EX):
                fallback = (casc_name, data)
        if expected is not None and key not in self._making and key not in self._rebuilt:
            self._making.add(key)
            try:
                made = self._remake(key, expected)
            finally:
                self._making.discard(key)
            if made is not None or not self._making:  # a nested attempt may fail only for want of its donors
                self._rebuilt[key] = made
            if made is not None:
                return self._found(name, *made)
        if fallback is not None:
            return self._found(name, "differs", *fallback)
        self.status[name] = ("missing", "")
        raise SourceError(f"{name} not found in StarCraft: Remastered {self.version}")

    def _casc(self, casc_name: str) -> bytes | None:
        try:
            return self.storage.read(casc_name)
        except CascError as exc:  # listed in the root, not installed locally
            log.debug("%s: %s", casc_name, exc)
            return None

    def _first(self, names: list[str]) -> tuple[str, bytes] | None:
        for n in names:
            if self.storage.has(n):
                data = self._casc(n)
                if data is not None:
                    return n, data
        return None

    def _palette(self, entry: Entry) -> rebuild.Palette | None:
        """The palette of the first donor Remastered keeps exactly, else of one it rebuilds with the original's
        palette from such a donor (one level: a donor's donors must be exact)."""
        folder = entry.name.lower().rsplit("\\", 1)[0]
        donors = sorted((d for d in self.manifest.palettes.get(entry.extras.get("palette", ""), []) if d != entry.name),
                        key=lambda d: (d.lower().rsplit("\\", 1)[0] != folder, d.lower()))
        for donor in donors:
            data = self._exact_copy(donor)
            if data is not None:
                return pcx.decode(data).palette
        if len(self._making) > 1:
            return None
        for donor in donors:
            if normalize(donor) in self._making:
                continue
            try:
                _, data = self.read(donor)
            except SourceError:
                continue
            if self.status[donor][0] in ("derived", "equivalent"):  # rebuilt with the original's palette
                return pcx.decode(data).palette
        for neighbour in sorted(self.manifest.files.values(), key=lambda e: e.name.lower()):
            # no PCX shares its palette: a neighbour's (pixels of colours it lacks take the nearest)
            if (neighbour.name.lower().rsplit("\\", 1)[0] == folder and neighbour.name.lower().endswith(".pcx")
                    and normalize(neighbour.name) not in self._making and len(self._making) == 1):
                try:
                    _, data = self.read(neighbour.name)
                except SourceError:
                    continue
                if self.status[neighbour.name][0] in ("exact", "derived", "equivalent"):
                    return pcx.decode(data).palette
        return None

    def _exact_copy(self, name: str) -> bytes | None:
        """`name`'s bytes when a Remastered copy is exact or derived, without rebuilding it."""
        expected = self.manifest.files.get(normalize(name))
        if expected is None:
            return None
        for casc_name in self._candidates(name):
            data = self._casc(casc_name)
            if data is None:
                continue
            if _sha(data) == expected.sha256:
                return data
            rebuilt = self._rebuild(normalize(name), casc_name, data, expected.size)
            if rebuilt is not None and _sha(rebuilt) == expected.sha256:
                return rebuilt
        return None

    def _remake(self, key: str, entry: Entry) -> tuple[str, str, bytes] | None:
        """(status, CASC name, bytes) of a file rebuilt from Remastered's own formats (casc/rebuild.py), or None."""
        stem = key.rsplit(".", 1)[0]
        ext = key.rsplit(".", 1)[-1]
        if ext == "pcx" and "pixels" in entry.extras:
            return self._remake_pcx(stem, entry)
        if ext == "grp":
            return self._remake_grp(key, stem, entry)
        if ext == "tbl" and key in self.manifest.texts.get("tbl", {}):
            return self._remake_tbl(key, entry)
        if ext == "bin" and key in self.manifest.texts.get("bin", {}):
            table = self.manifest.texts["bin"][key]
            data = rebuild.dialog_from_table(table, self._text)
            return self._judged(entry, data), "defs/scr texts", data
        if ext == "smk" and "smk" in entry.extras:
            found = self._first([p + stem + ".webm" for p in ("SD/", "")])
            if found is None:
                return None
            width, height, frames, ring, rate = entry.extras["smk"]
            palette = self._palette(entry) if "palette" in entry.extras else None
            rgb = b"".join(bytes(c) for c in palette) if palette is not None else b""
            cached = None
            if self.cache is not None:
                cached = self.cache / (_sha(found[1] + rgb + json.dumps(entry.extras["smk"]).encode()
                                            + KPV_RULE) + ".kpv")
                if cached.is_file():
                    return "approximation", found[0], cached.read_bytes()
            if palette is None:  # a palette of its own
                data = kpv.from_webm(found[1], rate, frames, bool(ring))
            else:  # drawn in its screen's palette: the movie's indices are that palette's
                index = rebuild.IndexMap(palette)
                data = kpv.from_webm(found[1], rate, frames, bool(ring), rgb,
                                     lambda r, g, b, a: index(r, g, b, OPAQUE if a >= kpv.ALPHA_CUT else 0))
            if cached is not None:
                cached.parent.mkdir(parents=True, exist_ok=True)
                cached.write_bytes(data)
            return "approximation", found[0], data
        return None

    @staticmethod
    def _judged(entry: Entry, data: bytes) -> str:
        return "derived" if _sha(data) == entry.sha256 else "approximation"

    def _remake_pcx(self, stem: str, entry: Entry) -> tuple[str, str, bytes] | None:
        found = self._first([p + stem + ".dds" for p in ("SD/", "")])
        palette = self._palette(entry)
        if found is None or palette is None:
            return None
        try:
            data = rebuild.pcx_from_dds(found[1], tuple(entry.extras["size"]), palette,
                                        bytes.fromhex(entry.extras["header"]))
        except dds.DdsError as exc:
            log.debug("%s: %s", found[0], exc)
            return None
        if _sha(data) == entry.sha256:
            return "derived", found[0], data
        image = pcx.decode(data)
        size = (image.width, image.height)
        if rebuild.plane_hash(*size, image.pixels) == entry.extras["pixels"]:
            return "equivalent", found[0], data
        if rebuild.plane_hash(*size, rebuild.rgb_plane(image.pixels, image.palette)) == entry.extras["rgb"]:
            return "colour-equal", found[0], data
        return "approximation", found[0], data

    def _remake_grp(self, key: str, stem: str, entry: Entry) -> tuple[str, str, bytes] | None:
        palette = self._palette(entry)
        if palette is None:
            return None
        if "frames" in entry.extras:  # a dialog GRP of plain rows: Remastered's DXT frames of it
            rows = entry.extras["frames"]
            # its own theme's frames, else another theme's (the same art in other colours)
            others = sorted(n for n in self.storage.names() if DIALOG_FRAMES.match(n))
            found = None
            for candidate in [p + stem + ".dds.grp" for p in ("SD/", "")] + others:
                found = self._first([candidate])
                if found is not None and len(images := dds.container(found[1])) >= len(rows) and all(
                        (w, h) == (iw, ih) or rebuild.fits(iw, ih, w, h)
                        for (_, _, w, h, _), (iw, ih, _) in zip(rows, images)):
                    break
                found = None
            if found is None:
                return None
            frames = []
            for (_, _, w, h, _), (_, _, image) in zip(rows, images):
                fitted, above = rebuild.fit(dds.decode(image), w, h)
                frames.append(bytes(w * above) + rebuild.indices(fitted, palette))
            data = rebuild.uncompressed_grp(entry.extras, frames, entry.size)
            return self._judged(entry, data), found[0], data
        source = REMAPPED.get(key.rsplit("/", 1)[-1]) if key.startswith("glue/") else None
        if source is None:
            return None
        grp_name, palette_name = source
        try:
            original = grp.decode(self.read(grp_name)[1])
            wpe = self.read(palette_name)[1]
        except (SourceError, grp.GrpError):
            return None
        data = rebuild.remap_grp(original, wpe, palette)
        return self._judged(entry, data), grp_name, data

    def _remake_tbl(self, key: str, entry: Entry) -> tuple[str, str, bytes] | None:
        table = self.manifest.texts["tbl"][key]
        xml = self._casc(table["xml"]) if self.storage.has(table["xml"]) else None
        if xml is None:
            return None
        base = None
        if table["base"]:
            raw = self._casc(table["base"]) if self.storage.has(table["base"]) else None
            base = rebuild.tbl_spans(raw) if raw is not None else None
        data = rebuild.tbl_from_remastered(table["ids"], {int(k): v for k, v in table["nuls"].items()},
                                           rebuild.remastered_xml(lambda n: xml, table["xml"]), base)
        return self._judged(entry, data), table["xml"], data

    def _text(self, source: str) -> bytes:
        """A Remastered string by its manifest source `id:<xml>#<id>` (b"" when it has none)."""
        if not source.startswith("id:"):
            return b""
        path, key = source[3:].split("#", 1)
        if path not in self._xml:
            raw = self._casc(path) if self.storage.has(path) else None
            self._xml[path] = dict(rebuild.remastered_xml(lambda n: raw, path)) if raw is not None else {}
        return self._xml[path].get(key, b"")

    def _found(self, name: str, status: str, casc_name: str, data: bytes) -> tuple[str, bytes]:
        if status in ("differs", "unlisted") and name not in self.status:
            log.debug("%s: %s (CASC %s)", name, status, casc_name)
        self.status[name] = (status, casc_name)
        return f"CASC {casc_name}", data

    def write_report(self, out: Path) -> dict[str, int]:
        """Merge this run's statuses into out/reports/scr-import.json; return the counts by status."""
        path = out / REPORT
        files = json.loads(path.read_text())["files"] if path.is_file() else {}
        files.update({name: list(v) for name, v in self.status.items()})
        counts: dict[str, int] = {}
        for status, _ in files.values():
            counts[status] = counts.get(status, 0) + 1
        report = {"build": self.version, "profile": self.profile.id, "counts": counts,
                  "files": dict(sorted(files.items(), key=lambda kv: kv[0].lower()))}
        path.parent.mkdir(parents=True, exist_ok=True)
        write_if_changed(path, json.dumps(report, indent=1) + "\n")
        return counts


def build_texts(read_original, names: list[str], storage: CascStorage) -> dict:
    """defs/scr/texts-<profile>.json from both installs: for each TBL its Remastered XML, string ids and inner NULs;
    for each dialog BIN its skeleton and where Remastered has its texts."""
    def local(*candidates: str) -> str | None:
        for c in candidates:
            if storage.has(c):
                try:
                    storage.read(c)
                    return c
                except CascError:
                    continue
        return None

    rc3 = rebuild.remastered_xml(storage.read, rebuild.RC3)
    everywhere: dict[bytes, str] = {}
    for n in sorted(storage.names(), key=str.lower):
        if n.lower().startswith("sd/rez/") and n.lower().endswith(".xml") and local(n):
            for k, v in rebuild.remastered_xml(storage.read, n):
                everywhere.setdefault(v, f"id:{n}#{k}")
    out: dict[str, dict] = {"tbl": {}, "bin": {}}
    for name in sorted(names, key=str.lower):
        lower = normalize(name)
        stem = lower.rsplit("/", 1)[-1].rsplit(".", 1)[0]
        if lower.startswith("rez/") and lower.endswith(".tbl"):
            xml = local(f"SD/rez/{stem}.xml", f"locales/enUS/Assets/rez/{stem}.xml")
            if xml is None:
                continue
            base = f"rez/{stem}.tbl" if local(f"rez/{stem}.tbl") else None
            out["tbl"][name] = rebuild.string_table(rebuild.tbl_spans(read_original(name)), xml,
                                                    rebuild.remastered_xml(storage.read, xml), base)
        elif lower.startswith("rez/") and lower.endswith(".bin"):
            own = [(k, v) for k, v in rc3 if k.lower().startswith(stem + "-")]
            own.sort(key=lambda kv: int(kv[0].rsplit("-", 1)[1]) if kv[0].rsplit("-", 1)[1].isdigit() else 1 << 30)
            out["bin"][name] = rebuild.dialog_table(read_original(name), own, everywhere)
    return out


def write_texts(path: Path, profile_id: str, texts: dict) -> None:
    payload = {"comment": f"How profile {profile_id}'s string tables and dialogs are rebuilt from StarCraft: Remastered "
                          "(converter/casc/rebuild.py): Remastered string ids, NUL positions, the dialogs' skeletons "
                          "(their bytes with every string zeroed, zlib, base64) and their strings' sources. Ids and "
                          "names only; written by python3 -m converter scr-manifest --data DIR --scr DIR.",
               "profile": profile_id, **texts}
    write_if_changed(path, json.dumps(payload, indent=1) + "\n")
