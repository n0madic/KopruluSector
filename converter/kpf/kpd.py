"""Game data containers: `dat.kpd` (`DAT `), `strings.kpd` (`STRS`), `iscript.kpd` (`ISCR`),
`aiscript.kpd` (`AISC`).

Layouts: docs/formats/kpd.md. All keep the original bytes unchanged: a DAT file is
already column-major (one field = one contiguous array), and a TBL file is the exact
table the native lookup `0x4c13b0` reads (count, offsets including shared ones, and
allocated blocks with embedded NULs).
"""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path

from .. import CONVERTER_VERSION
from ..derive import terrain
from ..formats import dat, palette, scripts, tileset
from .container import Container

DAT_KIND = b"DAT "
DAT_SCHEMA = 1
STRINGS_KIND = b"STRS"
STRINGS_SCHEMA = 1
COLUMNS = "COLS"
ISCRIPT_KIND = b"ISCR"
ISCRIPT_SCHEMA = 1
AISCRIPT_KIND = b"AISC"
AISCRIPT_SCHEMA = 1
ISCRIPT_FILE = "scripts\\iscript.bin"
AISCRIPT_FILE = "scripts\\aiscript.bin"
BWSCRIPT_FILE = "scripts\\bwscript.bin"
TILESET_KIND = b"TSET"
TILESET_SCHEMA = 1
TILESET_EXTENSIONS = ("cv5", "vx4", "vr4", "vf4", "wpe")

# Runtime string tables: section fourcc -> MPQ path.
TBL_FILES = {
    "STAT": "rez\\stat_txt.tbl",
    "NETW": "rez\\network.tbl",
    "GLUA": "rez\\gluAll.tbl",
    "GHST": "rez\\gluHist.tbl",
    "TIPS": "rez\\tips.tbl",
    "HELP": "rez\\help_txt.tbl",
}

DEFS_ROOT = Path(__file__).resolve().parents[2] / "defs"


def provenance_hash(*relative: str) -> bytes:
    digest = hashlib.sha256()
    for rel in relative:
        for path in sorted((DEFS_ROOT / rel).glob("*.json")):
            digest.update(path.name.encode() + b"\0" + path.read_bytes())
    return digest.digest()


def columns_section(layouts: tuple[dat.DatFile, ...]) -> bytes:
    """Self-describing column table so the Lua loader needs no copy of the layout.

    u16 file count; per file: fourcc, u16 records, u16 column count; per column:
    u8 element size, u8 elements per id, u16 count, u16 first id, u32 byte offset,
    u8 name length, name (ASCII).
    """
    out = bytearray(struct.pack("<H", len(layouts)))
    for f in layouts:
        out += f.fourcc.encode("ascii") + struct.pack("<HH", f.records, len(f.columns))
        for c in f.columns:
            name = c.name.encode("ascii")
            out += struct.pack("<BBHHIB", c.element_size, c.per_id, c.count, c.first_id, c.offset, len(name)) + name
    return bytes(out)


def build_dat(files: dict[str, bytes], layouts: tuple[dat.DatFile, ...], *, profile_id: str,
              data_set_hash: bytes) -> Container:
    """`files` maps each layout's MPQ path to the effective file bytes."""
    c = Container(DAT_KIND, DAT_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash,
                  provenance_hash=provenance_hash("dat"))
    c.add(COLUMNS, columns_section(layouts))
    for layout in layouts:
        data = files[layout.path]
        dat.check(layout, data)
        c.add(layout.fourcc, data)
    return c


def build_strings(files: dict[str, bytes], *, profile_id: str, data_set_hash: bytes) -> Container:
    """`files` maps each TBL_FILES path to the effective file bytes."""
    c = Container(STRINGS_KIND, STRINGS_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    for fourcc, path in TBL_FILES.items():
        data = files[path]
        count = struct.unpack_from("<H", data, 0)[0] if len(data) >= 2 else -1
        if count < 0 or 2 + 2 * count > len(data):
            raise ValueError(f"{path}: offset table out of bounds")
        c.add(fourcc, data)
    return c


def build_iscript(data: bytes, *, profile_id: str, data_set_hash: bytes) -> Container:
    """CODE: iscript.bin unchanged; HDRS: u16 count, {u16 id, u16 header offset} sorted by id."""
    headers = sorted(scripts.iscript_headers(data))
    c = Container(ISCRIPT_KIND, ISCRIPT_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("CODE", data)
    c.add("HDRS", struct.pack("<H", len(headers)) + b"".join(struct.pack("<HH", i, o) for i, o in headers))
    return c


def build_aiscript(aiscript: bytes, bwscript: bytes, *, profile_id: str, data_set_hash: bytes) -> Container:
    """AISC/BWSC: the two binaries unchanged (directories included; the runtime resolves ids)."""
    scripts.ai_directory(aiscript, scripts.AI_RECORD)
    scripts.ai_directory(bwscript, scripts.BW_RECORD)
    c = Container(AISCRIPT_KIND, AISCRIPT_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("AISC", aiscript)
    c.add("BWSC", bwscript)
    return c


def build_tileset(name: str, files: dict[str, bytes], *, profile_id: str, data_set_hash: bytes) -> Container:
    """tilesets/<name>.kpd. `files` maps each TILESET_EXTENSIONS entry to the effective file.

    CV5, VF4 and WPE are kept unchanged; MFLG is the derived megatile flag array; CYCL holds
    the stock palette cycle ranges. VX4/VR4 are only used to prerender the tilesource.
    """
    ts = tileset.decode(files["cv5"], files["vx4"], files["vr4"], files["vf4"])  # reference checks
    for group in ts.groups:
        for mega in group:
            if mega >= ts.megatile_count:
                raise tileset.TilesetError(f"{name}: CV5 references megatile {mega} of {ts.megatile_count}")
    palette.decode_wpe(files["wpe"])
    mflags = terrain.megatile_flags(files["vf4"])
    c = Container(TILESET_KIND, TILESET_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("CV5 ", files["cv5"])
    c.add("VF4 ", files["vf4"])
    c.add("WPE ", files["wpe"])
    ranges = palette.CYCLE_RANGES[name]
    c.add("CYCL", bytes([len(ranges), palette.CYCLE_SPEED]) + b"".join(bytes(r) for r in ranges))
    c.add_derived("MFLG", struct.pack(f"<{len(mflags)}I", *mflags), algorithm_id=terrain.MFLAGS_ALGORITHM,
                  algorithm_version=terrain.MFLAGS_VERSION, inputs_hash=hashlib.sha256(files["vf4"]).digest())
    return c
