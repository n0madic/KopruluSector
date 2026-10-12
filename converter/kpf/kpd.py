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
from dataclasses import dataclass
from pathlib import Path

from .. import CONVERTER_VERSION
from ..derive import terrain
from ..formats import dat, dialog, fnt, palette, pcx, scripts, smk, tbl, tileset, wav
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
PALETTES_KIND = b"PALS"
PALETTES_SCHEMA = 1
# Remap records 1..7 of 0x508828 in order; LoadRemapTables 0x466810 loads
# Tileset\<tileset>\<name>.pcx into a table of (height + 1) * 256 bytes whose first row
# is never initialized natively and never read by stock data (stored as zeros).
REMAP_TABLES = ("ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green")
DARK_SIZE = 0x2000  # LoadDarkTable 0x4668f0: dark.pcx into a 0x2000-byte buffer
SHIFT_SIZE = 256  # shift.pcx into a 256-byte buffer (atlas P04 0x4101b0 step 6)
# Global lookup PCXs: section -> (MPQ path, bytes read by SBmpLoadImage into a buffer of that size).
GAME_TABLES = {
    "TUNI": ("game\\tunit.pcx", 0x60),  # 12 player colour rows of 8 indices (0x41d570)
    "TMIN": ("game\\tminimap.pcx", 0x0C),  # 12 minimap player colours (0x41d570)
    "TSEL": ("game\\tselect.pcx", 24),  # selection-circle colours (0x40fef0)
    "THPB": ("game\\thpbar.pcx", 19),  # health-bar colours (atlas P22)
}
FONTS_KIND = b"FONT"
FONTS_SCHEMA = 1
UI_KIND = b"UI  "
UI_SCHEMA = 1
NONE16 = 0xFFFF
NONE32 = 0xFFFFFFFF
# Widget: parent, type, identifier, flags, x1, y1, x2, y2, width, height, text offset,
# text length, first SMK, SMK count, responsive[4], extra[4], shared bitmap record offset.
UI_WIDGET = struct.Struct("<HHHI4h2HIHHH4H4HI")
# SMK node: flags, file name offset, length, offset x, offset y, raw unknown words[4].
UI_SMK = struct.Struct("<HIHhh4I")
SOUNDS_KIND = b"SNDS"
SOUNDS_SCHEMA = 1
# Per sfx id: flags (bit 0 file present, bit 1 parsed), format tag, channels, sample rate,
# nAvgBytesPerSec, block align, bits per sample, data length.
SOUND_RECORD = struct.Struct("<HHHIIHHI")
SOUND_PRESENT, SOUND_PARSED = 0x01, 0x02
PORTRAITS_KIND = b"PORT"
PORTRAITS_SCHEMA = 1
PORTRAIT_VARIANTS = 10  # variant digits probed per stem
PORTRAIT_KINDS = ("idle_portrait_file", "talking_portrait_file")
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


def _pcx_prefix(data: bytes, size: int, what: str) -> bytes:
    """SBmpLoadImage into a `size`-byte buffer: the first `size` pixel bytes, row-major."""
    pixels = pcx.decode(data).pixels
    if len(pixels) < size:
        raise pcx.PcxError(f"{what}: {len(pixels)} pixels, the native buffer expects {size}")
    return pixels[:size]


def tileset_palette_tables(read) -> bytes:
    """One tileset's PAL<n> payload. `read(name)` returns tileset\\<tileset>\\<name>.pcx.

    Per remap table: u16 rows (PCX height + 1), then rows * 256 bytes with row 0 zero;
    then dark (32 * 256) and shift (256) bytes.
    """
    out = bytearray()
    for name in REMAP_TABLES:
        p = pcx.decode(read(name))
        if p.width != 256:
            raise pcx.PcxError(f"{name}.pcx is {p.width} wide; the native table assumes 256")
        out += struct.pack("<H", p.height + 1) + bytes(256) + p.pixels
    out += _pcx_prefix(read("dark"), DARK_SIZE, "dark.pcx") + _pcx_prefix(read("shift"), SHIFT_SIZE, "shift.pcx")
    return bytes(out)


def build_palettes(read, *, profile_id: str, data_set_hash: bytes) -> Container:
    """palettes.kpd. `read(path)` returns the effective file for an MPQ path."""
    c = Container(PALETTES_KIND, PALETTES_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    for i, name in enumerate(palette.TILESETS):
        c.add(f"PAL{i}", tileset_palette_tables(lambda t, n=name: read(f"tileset\\{n}\\{t}.pcx")))
    for fourcc, (path, size) in GAME_TABLES.items():
        c.add(fourcc, _pcx_prefix(read(path), size, path))
    return c


def font_defs() -> dict:
    import json

    return json.loads((DEFS_ROOT / "fonts" / "bw-1.17.0.1.json").read_text())


def decoded_font(plaintext: bytes, expected_sha256: str, name: str) -> bytes:
    """The decoded effective font: plaintext + one NUL, checked against the native-decoded hash."""
    data = plaintext + b"\0"
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected_sha256:
        raise fnt.FntError(f"{name}: plaintext + NUL hashes to {actual}, the decoded font is {expected_sha256}")
    fnt.decode(data)
    return data


def build_fonts(read, *, profile_id: str, data_set_hash: bytes) -> Container:
    """fonts.kpd: decoded FNT bytes per font and the text colour rows. `read(path)` -> bytes."""
    defs = font_defs()
    c = Container(FONTS_KIND, FONTS_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash,
                  provenance_hash=provenance_hash("fonts"))
    for f in defs["fonts"]:
        c.add(f["section"], decoded_font(read(f["plaintext"]), f["decoded_sha256"], f["name"]))
    for row in defs["colour_rows"]:
        c.add(row["section"], _pcx_prefix(read(row["path"]), row["bytes"], row["path"]))
    return c


def ui_defs() -> dict:
    import json

    return json.loads((DEFS_ROOT / "ui" / "bw-1.17.0.1.json").read_text())


def dialogs_section(bins: dict[str, bytes]) -> tuple[bytes, bytes]:
    """(DLGS, POOL) payloads for the dialogs in `bins` (name -> BIN bytes), in order."""
    pool = bytearray()

    def text(raw: bytes | None) -> tuple[int, int]:
        if raw is None:
            return NONE32, 0
        pool.extend(raw + b"\0")
        return len(pool) - len(raw) - 1, len(raw)

    out = bytearray(struct.pack("<H", len(bins)))
    for name, data in bins.items():
        widgets = dialog.parse(data)
        offsets = {w.offset: i for i, w in enumerate(widgets)}
        smks = bytearray()
        records = bytearray()
        smk_count = 0
        for w in widgets:
            t_off, t_len = text(w.text)
            first = smk_count if w.smks else NONE16
            for n in w.smks:
                f_off, f_len = text(n.filename)
                smks += UI_SMK.pack(n.flags, f_off, f_len, *(v - 0x10000 if v & 0x8000 else v for v in (n.offset_x, n.offset_y)),
                                    *n.unknown)
                smk_count += 1
            shared = NONE32 if w.shared_bitmap is None else w.shared_bitmap
            records += UI_WIDGET.pack(NONE16 if w.parent < 0 else w.parent, w.type, w.identifier, w.flags, *w.rect,
                                      *w.size, t_off, t_len, first, len(w.smks), *w.responsive, *w.extra, shared)
        if len(offsets) != len(widgets):
            raise dialog.DialogError(f"{name}: repeated record")
        label = name.encode("ascii")
        out += bytes([len(label)]) + label + struct.pack("<HH", len(widgets), smk_count) + records + smks
    return bytes(out), bytes(pool)


def build_ui(read, *, profile_id: str, data_set_hash: bytes) -> Container:
    """ui.kpd: dialogs after the native fixup rules, theme colour banks, dialog GRP headers."""
    defs = ui_defs()
    c = Container(UI_KIND, UI_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash,
                  provenance_hash=provenance_hash("ui"))
    dlgs, pool = dialogs_section({name: read(name) for name in defs["bins"]})
    c.add("DLGS", dlgs)
    c.add("POOL", pool)
    c.add("TFNT", b"".join(_pcx_prefix(read(f"glue\\{t}\\tFont.pcx"), 48, "tFont.pcx") for t in defs["themes"]))
    effects = [pcx.decode(read(f"glue\\{t}\\tEffect.pcx")).pixels for t in defs["themes"]]
    if len({len(e) for e in effects}) != 1:
        raise pcx.PcxError("tEffect.pcx sizes differ between themes")
    c.add("TEFF", struct.pack("<H", len(effects[0])) + b"".join(effects))
    grps = [(f"glue\\{t}\\Dlg.grp") for t in defs["themes"]] + defs["dialog_grps"]
    out = bytearray(struct.pack("<H", len(grps)))
    for path in grps:
        data = read(path)
        count = struct.unpack_from("<H", data, 0)[0]
        header = data[: 6 + 8 * count]
        label = path.encode("ascii")
        out += bytes([len(label)]) + label + struct.pack("<I", len(header)) + header
    c.add("DGRP", bytes(out))
    return c


def sound_record(data: bytes | None) -> bytes:
    if data is None:
        return SOUND_RECORD.pack(0, 0, 0, 0, 0, 0, 0, 0)
    info = wav.read_info(data)
    return SOUND_RECORD.pack(SOUND_PRESENT | SOUND_PARSED, info.format_tag, info.channels, info.sample_rate,
                             info.avg_bytes_per_sec, info.block_align, info.bits_per_sample, info.data_length)


def build_sounds(read, has, *, profile_id: str, data_set_hash: bytes) -> Container:
    """sounds.kpd: RIFF metadata of every sfxdata.dat sound (`SFX `), indexed by sfx id.

    Path: `sound\\` + sfxdata.tbl entry; id 0 and empty entries have no file. The
    duration the original caches is u32(data length * 1000) / nAvgBytesPerSec (`0x49da70`).
    """
    paths = tbl.sfx_paths(read("arr\\sfxdata.dat"), read("arr\\sfxdata.tbl"))
    records = b"".join(sound_record(read(p) if p and has(p) else None) for p in paths)
    c = Container(SOUNDS_KIND, SOUNDS_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("SFX ", struct.pack("<H", len(paths)) + records)
    return c


@dataclass(frozen=True)
class PortraitSet:
    """Existing portrait SMKs: per portdata row and kind (idle, talking) the present variant
    digits, and the decoded movies in that order."""
    rows: list[tuple[tuple[int, int], tuple[int, int]]]  # ((first smk, variant mask) idle, talking)
    paths: list[str]


def portrait_set(read, has) -> PortraitSet:
    """`portrait\\` + portdata.tbl stem + variant digit + `.smk` (opening path of `0x4a6b40`/`0x4a6c50`)."""
    layout = next(f for f in dat.load_layouts() if f.table == "portdata")
    data = read("arr\\portdata.dat")
    names = tbl.decode_tbl(read("arr\\portdata.tbl"))
    columns = [dat.values(layout, data, k) for k in PORTRAIT_KINDS]
    rows, paths = [], []
    for row in range(layout.records):
        entry = []
        for col in columns:
            ref, first, mask = col[row], NONE16, 0
            if ref:
                stem = names[ref - 1].decode("latin1")
                for v in range(PORTRAIT_VARIANTS):
                    path = f"portrait\\{stem}{v}.smk"
                    if has(path):
                        if first == NONE16:
                            first = len(paths)
                        paths.append(path)
                        mask |= 1 << v
            entry.append((first, mask))
        rows.append(tuple(entry))
    return PortraitSet(rows, paths)


def build_portraits(read, has, *, profile_id: str,
                    data_set_hash: bytes) -> tuple[Container, dict[str, tuple[smk.Header, list[smk.Frame]]]]:
    """portraits.kpd and the decoded movie per SMK path (for the portrait atlases)."""
    ps = portrait_set(read, has)
    palettes: dict[bytes, int] = {}
    smks = bytearray(struct.pack("<H", len(ps.paths)))
    frames_by_path = {}
    for path in ps.paths:
        h, frames = smk.decode(read(path))
        frames_by_path[path] = (h, frames)
        label = path.encode("latin1")
        smks += bytes([len(label)]) + label + struct.pack("<HHHHBI", h.width, h.height, h.decoded_frames, h.frames,
                                                          int(h.ring_frame), round(h.ms_per_frame * 1000))
        for f in frames:
            smks += struct.pack("<H", palettes.setdefault(f.palette, len(palettes)))
    rows = bytearray(struct.pack("<H", len(ps.rows)))
    for (idle_first, idle_mask), (talk_first, talk_mask) in ps.rows:
        rows += struct.pack("<4H", idle_first, idle_mask, talk_first, talk_mask)
    c = Container(PORTRAITS_KIND, PORTRAITS_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("ROWS", bytes(rows))
    c.add("SMKS", bytes(smks))
    c.add("PALS", struct.pack("<H", len(palettes)) + b"".join(palettes))
    return c, frames_by_path
