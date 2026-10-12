"""Game data containers: `dat.kpd` (`DAT `), `strings.kpd` (`STRS`), `iscript.kpd` (`ISCR`),
`aiscript.kpd` (`AISC`).

Layouts: docs/formats/kpd.md. All keep the original bytes unchanged: a DAT file is
already column-major (one field = one contiguous array), and a TBL file is the exact
table the native lookup `0x4c13b0` reads (count, offsets including shared ones, and
allocated blocks with embedded NULs).
"""
from __future__ import annotations

import hashlib
import logging
import struct
from dataclasses import dataclass

from .. import CONVERTER_VERSION
from ..derive import terrain
from .. import packs
from ..provenance import DEFS_ROOT, provenance_hash
from ..formats import dat, dialog, fnt, grp, lo, palette, pcx, scripts, smk, tbl, tileset, wav
from .container import Container

log = logging.getLogger(__name__)

DAT_KIND = b"DAT "
DAT_SCHEMA = 1
STRINGS_KIND = b"STRS"
STRINGS_SCHEMA = 1
COLUMNS = "COLS"
ISCRIPT_KIND = b"ISCR"
ISCRIPT_SCHEMA = 2
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
# text length, first SMK, SMK count, responsive[4], extra[4], shared-bitmap reference (raw
# negative u32 relative to the file base, dialog.NO_SHARED_BITMAP none).
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
GRPMETA_KIND = b"GRPM"
GRPMETA_SCHEMA = 1
# images.dat columns holding images.tbl indices of LO files (atlas P04 `0x40ff40`).
LO_COLUMNS = ("attack_overlay", "damage_overlay", "special_overlay", "landing_dust_overlay",
              "lift_off_dust_overlay", "shield_overlay")
ATLAS_MASK = 0x01  # ATLS flag: the atlas has an identically packed opacity atlas `<name>_mask`
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


def build_iscript(data: bytes, header_animations: dict[int, int], *, profile_id: str,
                  data_set_hash: bytes) -> Container:
    """CODE: iscript.bin unchanged; HDRS: u16 count, {u16 id, u16 header offset} sorted by id;
    SLOT: u8 count, {u8 header type, u8 animation slots} sorted by type (defs/iscript)."""
    headers = sorted(scripts.iscript_headers(data))
    c = Container(ISCRIPT_KIND, ISCRIPT_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash,
                  provenance_hash=provenance_hash("iscript"))
    c.add("CODE", data)
    c.add("HDRS", struct.pack("<H", len(headers)) + b"".join(struct.pack("<HH", i, o) for i, o in headers))
    slots = sorted(header_animations.items())
    c.add("SLOT", bytes([len(slots)]) + b"".join(bytes([kind, count]) for kind, count in slots))
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
        widgets = dialog.parse(data)  # rejects repeated records (native traversal order)
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
            shared = dialog.NO_SHARED_BITMAP if w.shared_bitmap is None else w.shared_bitmap
            records += UI_WIDGET.pack(NONE16 if w.parent < 0 else w.parent, w.type, w.identifier, w.flags, *w.rect,
                                      *w.size, t_off, t_len, first, len(w.smks), *w.responsive, *w.extra, shared)
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
    """One `SFX ` record. A file the native parser rejects is present but not parsed: the
    original keeps the sound and never plays it."""
    if data is None:
        return SOUND_RECORD.pack(0, 0, 0, 0, 0, 0, 0, 0)
    try:
        info = wav.read_info(data)
    except wav.WavError as exc:
        log.warning("sound rejected by the native WAV parser, kept unparsed: %s", exc)
        return SOUND_RECORD.pack(SOUND_PRESENT, 0, 0, 0, 0, 0, 0, 0)
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


@dataclass(frozen=True)
class GrpEntry:
    path: str  # spelling of the first images.tbl entry naming the file
    data: bytes
    grp: grp.Grp
    pack: str
    atlas: str
    opaque_zero: bool  # some frame draws opaque index 0


def draws_opaque_zero(g: grp.Grp) -> bool:
    """Indices are 0 under every transparent pixel, so more zeros than transparent pixels
    means an opaque index 0."""
    return any(f.indices.count(0) > f.mask.count(grp.TRANSPARENT) for f in g.frames)


@dataclass(frozen=True)
class GrpSet:
    """The GRP and LO files images.dat names, deduplicated by case-insensitive path (the
    archives resolve paths case-insensitively, so equal paths are one file)."""
    tbl_count: int
    grps: list[GrpEntry]  # in order of the first images.tbl index naming each file
    los: list[tuple[str, bytes]]
    grp_of_tbl: dict[int, int]  # images.tbl index (1-based) -> index into grps
    lo_of_tbl: dict[int, int]

    @property
    def atlases(self) -> list[tuple[str, str, bool]]:
        """(atlas name, pack, needs a mask atlas) in first-use order."""
        out: dict[str, list] = {}
        for e in self.grps:
            a = out.setdefault(e.atlas, [e.pack, False])
            a[1] = a[1] or e.opaque_zero
        return [(name, pack, mask) for name, (pack, mask) in out.items()]


def grp_references(images_dat: bytes, images_tbl: bytes) -> tuple[int, list[str], set[int], set[int]]:
    """(images.tbl count, its entries, tbl indices used as GRP, tbl indices used as LO)."""
    layout = next(f for f in dat.load_layouts() if f.table == "images")
    names = [n.decode("latin1") for n in tbl.decode_tbl(images_tbl)]
    grps = set(dat.values(layout, images_dat, "grp_file")) - {0}
    los = set().union(*(dat.values(layout, images_dat, c) for c in LO_COLUMNS)) - {0}
    return len(names), names, grps, los


def grp_set(tbl_count: int, names: list[str], grp_refs: set[int], lo_refs: set[int], read) -> GrpSet:
    """Load and decode every referenced file. `read(path)` returns the effective file."""
    def dedupe(refs: set[int]) -> tuple[dict[str, str], dict[int, int]]:
        first: dict[str, str] = {}
        position: dict[str, int] = {}
        index: dict[int, int] = {}
        for i in sorted(refs):
            if not 1 <= i <= tbl_count:
                raise tbl.TblError(f"images.tbl index {i} out of range 1..{tbl_count}")
            path = "unit\\" + names[i - 1]
            key = path.lower()
            if key not in first:
                first[key], position[key] = path, len(first)
            index[i] = position[key]
        return first, index

    grp_paths, grp_of_tbl = dedupe(grp_refs)
    lo_paths, lo_of_tbl = dedupe(lo_refs)
    decoded = []
    for path in grp_paths.values():
        data = read(path)
        decoded.append((path, data, grp.decode(data)))
    atlas_of = packs.assign_atlases([(p, sum(max(1, f.width) * max(1, f.height) for f in g.frames))
                                     for p, _, g in decoded])
    entries = [GrpEntry(p, d, g, packs.pack_of_grp(p), atlas_of[p], draws_opaque_zero(g)) for p, d, g in decoded]
    los = []
    for path in lo_paths.values():
        data = read(path)
        lo.check(data)
        los.append((path, data))
    return GrpSet(tbl_count, entries, los, grp_of_tbl, lo_of_tbl)


def _label(text: str) -> bytes:
    raw = text.encode("latin1")
    if len(raw) > 255:
        raise ValueError(f"name longer than 255 bytes: {text!r}")
    return bytes([len(raw)]) + raw


def build_grpmeta(gs: GrpSet, *, profile_id: str, data_set_hash: bytes) -> Container:
    """grpmeta.kpd: GRP frame geometry, opacity runs for `0x40fd50`, atlas names and raw LO files."""
    from ..emit.atlas import animation_id

    tblx = bytearray(struct.pack("<H", gs.tbl_count))
    for i in range(1, gs.tbl_count + 1):
        tblx += struct.pack("<HH", gs.grp_of_tbl.get(i, NONE16), gs.lo_of_tbl.get(i, NONE16))
    atlases = gs.atlases
    atlas_index = {name: k for k, (name, _, _) in enumerate(atlases)}
    atls = bytearray([len(atlases)])
    for name, pack, mask in atlases:
        atls += bytes([ATLAS_MASK if mask else 0]) + _label(pack) + _label(name)
    grps = bytearray(struct.pack("<H", len(gs.grps)))
    frms, opac = bytearray(), bytearray()
    frame_count = 0
    for e in gs.grps:
        grps += _label(e.path) + struct.pack("<3H", len(e.grp.frames), e.grp.width, e.grp.height)
        grps += bytes([atlas_index[e.atlas]]) + _label(animation_id(e.path)) + struct.pack("<I", frame_count)
        for f in range(len(e.grp.frames)):
            x, y, w, h, _ = grp.frame_record(e.data, f)
            frms += struct.pack("<4BI", x, y, w, h, len(opac))
            opac += grp.opacity_block(e.data, f)
        frame_count += len(e.grp.frames)
    lofs = bytearray(struct.pack("<H", len(gs.los)))
    for path, data in gs.los:
        lofs += _label(path) + struct.pack("<I", len(data)) + data
    c = Container(GRPMETA_KIND, GRPMETA_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("TBLX", bytes(tblx))
    c.add("ATLS", bytes(atls))
    c.add("GRPS", bytes(grps))
    c.add("FRMS", struct.pack("<I", frame_count) + bytes(frms))
    c.add("OPAC", bytes(opac))
    c.add("LOFS", bytes(lofs))
    return c
