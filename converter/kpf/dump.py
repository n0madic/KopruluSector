"""Readable dump of any Koprulu container, with decoders for known sections.

Used for golden comparisons and debugging: `python3 -m converter dump FILE [--section XXXX]`.
"""
from __future__ import annotations

import struct

from ..derive import contours, regions
from . import container as kpf
from . import kpm

HEX_PREVIEW = 32


def _meta(payload: bytes) -> list[str]:
    (version, era, width, height, ownr, side, forc, name, desc, colr, flags) = kpm.META.unpack(payload)
    lines = [
        f"version {version}  era {era} (tileset {era & 7})  size {width}x{height}",
        f"owners  {list(ownr)}",
        f"races   {list(side)}",
        f"forces  players {list(forc[:8])}  names {list(struct.unpack('<4H', forc[8:16]))}  flags {list(forc[16:20])}",
        f"strings name {name}  description {desc}",
    ]
    if flags & kpm.META_COLR:
        lines.append(f"colors  {list(colr)}")
    return lines


def _rule(payload: bytes) -> list[str]:
    lines, pos = [], 0
    while pos < len(payload):
        tag, size = struct.unpack_from("<4sI", payload, pos)
        lines.append(f"{tag.decode('latin1')} {size} bytes")
        pos += 8 + size
    return lines


def _wavs(payload: bytes) -> list[str]:
    count = struct.unpack_from("<H", payload, 0)[0]
    out = []
    for i in range(count):
        sid, index, length, rate = kpm.WAVS_ENTRY.unpack_from(payload, 2 + kpm.WAVS_ENTRY.size * i)
        out.append(f"string {sid} -> " + ("missing" if index == kpm.NO_SOUND else
                                          f"sound {index} ({length} bytes, {rate} bytes/s)"))
    return out


def _records(size: int):
    def decode(payload: bytes) -> list[str]:
        return [f"{len(payload) // size} records of {size} bytes"]
    return decode


def _walk(payload: bytes) -> list[str]:
    blocked = sum(1 for v in payload if v & 0x80)
    return [f"walk {len(payload)} mini-cells: {blocked} blocked, {len(payload) - blocked} walkable"]


def _rgns(payload: bytes) -> list[str]:
    g = regions.decode(payload)
    groups = {x for x in g.groups if x < regions.GROUP_RESERVED}
    lines = [f"{g.width}x{g.height}: {g.count} regions, {len(groups)} groups, {len(g.splits)} splits, "
             f"{sum(map(len, g.neighbors))} neighbour words"]
    for i in range(min(g.count, 4)):
        cx, cy = g.centers[i]
        lines.append(f"region {i}: class {g.classes[i]:#06x} group {g.groups[i]:#x} tiles {g.tiles[i]} "
                     f"center ({cx / 256:g},{cy / 256:g}) bounds {list(g.bounds[i])} rank {g.cut_ranks[i]} "
                     f"neighbours {g.neighbors[i]} (ground {g.ground[i]}, zero {g.zero_neighbors[i]})")
    return lines


def _cont(payload: bytes) -> list[str]:
    c = contours.decode(payload)
    lines = [f"contours top/right/bottom/left {c.counts()}, native capacity {c.capacities}"]
    for name, records in zip(("top", "right", "bottom", "left"), c.arrays):
        if records:
            lines.append(f"{name} first {list(records[0])} last {list(records[-1])}")
    return lines


def _pack(payload: bytes) -> list[str]:
    names, pos = [], 1
    for _ in range(payload[0]):
        names.append(payload[pos + 1 : pos + 1 + payload[pos]].decode("ascii"))
        pos += 1 + payload[pos]
    return ["packs " + ", ".join(names)]


def _tbl(payload: bytes) -> list[str]:
    from ..formats import tbl

    entries = tbl.decode_tbl(payload)
    offsets = set(struct.unpack_from(f"<{len(entries)}H", payload, 2))
    lines = [f"{len(entries)} entries, {len(offsets)} distinct offsets"]
    lines += [f"  {i + 1}: {e[:60]!r}" for i, e in enumerate(entries[:3])]
    return lines


def _cols(payload: bytes) -> list[str]:
    lines, pos = [], 2
    for _ in range(struct.unpack_from("<H", payload, 0)[0]):
        fourcc, records, ncols = payload[pos : pos + 4].decode("latin1"), *struct.unpack_from("<HH", payload, pos + 4)
        pos += 8
        names = []
        for _ in range(ncols):
            size, per_id, count, first, _off, length = struct.unpack_from("<BBHHIB", payload, pos)
            names.append(payload[pos + 11 : pos + 11 + length].decode("ascii") + f":{size}x{count}"
                         + (f"@{first}" if first else "") + (f"/{per_id}" if per_id > 1 else ""))
            pos += 11 + length
        lines.append(f"{fourcc} {records} records: " + ", ".join(names))
    return lines


def _hdrs(payload: bytes) -> list[str]:
    count = struct.unpack_from("<H", payload, 0)[0]
    first = [struct.unpack_from("<HH", payload, 2 + 4 * i) for i in range(min(count, 4))]
    return [f"{count} iscript headers, first {first}"]


def _slot(payload: bytes) -> list[str]:
    pairs = [(payload[1 + 2 * i], payload[2 + 2 * i]) for i in range(payload[0])]
    return [f"{payload[0]} header types (type, animation slots): {pairs}"]


def _ai_bin(stride: int):
    def decode(payload: bytes) -> list[str]:
        from ..formats import scripts

        records = scripts.ai_directory(payload, stride)
        return [f"{len(records)} scripts, first " + ", ".join(f"{sid.decode('latin1')}@{off:#x}" for sid, off in records[:5])]
    return decode


def _mflg(payload: bytes) -> list[str]:
    flags = struct.unpack(f"<{len(payload) // 4}I", payload)
    walkable = sum(1 for f in flags if f & 0x10000)
    return [f"{len(flags)} megatiles, {walkable} walkable"]


def _cycl(payload: bytes) -> list[str]:
    return [f"speed {payload[1]}, ranges " + ", ".join(f"{payload[2 + 2 * i]}-{payload[3 + 2 * i]}" for i in range(payload[0]))]


def _pal(payload: bytes) -> list[str]:
    from .kpd import REMAP_TABLES

    lines, pos = [], 0
    for name in REMAP_TABLES:
        rows = struct.unpack_from("<H", payload, pos)[0]
        lines.append(f"{name}: {rows} rows")
        pos += 2 + 256 * rows
    return ["; ".join(lines) + f"; dark {0x2000} bytes, shift 256 bytes"]


def _font(payload: bytes) -> list[str]:
    from ..formats import fnt

    f = fnt.decode(payload)
    return [f"codes {f.low}..{f.high}, max {f.max_width}x{f.max_height}, {len(f.glyphs)} glyphs"]


def _dlgs(payload: bytes) -> list[str]:
    from .kpd import UI_SMK, UI_WIDGET

    lines, pos = [], 2
    for _ in range(struct.unpack_from("<H", payload, 0)[0]):
        name = payload[pos + 1 : pos + 1 + payload[pos]].decode("ascii")
        pos += 1 + payload[pos]
        widgets, smks = struct.unpack_from("<HH", payload, pos)
        pos += 4 + widgets * UI_WIDGET.size + smks * UI_SMK.size
        lines.append(f"{name}: {widgets} widgets, {smks} SMK nodes")
    return [f"{len(lines)} dialogs"] + lines[:5] + (["..."] if len(lines) > 5 else [])


def _sfx(payload: bytes) -> list[str]:
    from .kpd import SOUND_RECORD

    count = struct.unpack_from("<H", payload, 0)[0]
    present = sum(SOUND_RECORD.unpack_from(payload, 2 + i * SOUND_RECORD.size)[0] & 1 for i in range(count))
    return [f"{count} sfx ids, {present} with a file"]


def _port_rows(payload: bytes) -> list[str]:
    count = struct.unpack_from("<H", payload, 0)[0]
    rows = [struct.unpack_from("<4H", payload, 2 + 8 * i) for i in range(count)]
    return [f"{count} portrait rows, {sum(1 for r in rows if r[0] != 0xFFFF)} with idle movies, "
            f"{sum(1 for r in rows if r[2] != 0xFFFF)} with talking movies"]


def _count16(what: str):
    def decode(payload: bytes) -> list[str]:
        return [f"{struct.unpack_from('<H', payload, 0)[0]} {what}"]
    return decode


def _bytes(what: str):
    def decode(payload: bytes) -> list[str]:
        return [f"{len(payload)} bytes of {what}"]
    return decode


DECODERS = {
    b"DAT ": {b"COLS": _cols},
    b"STRS": {fourcc.encode(): _tbl for fourcc in ("STAT", "NETW", "GLUA", "GHST", "TIPS", "HELP")},
    b"ISCR": {b"HDRS": _hdrs, b"CODE": _bytes("IScript bytecode"), b"SLOT": _slot},
    b"AISC": {b"AISC": _ai_bin(16), b"BWSC": _ai_bin(8)},
    b"TSET": {b"MFLG": _mflg, b"CYCL": _cycl, b"CV5 ": _records(52), b"VF4 ": _records(32), b"WPE ": _bytes("palette")},
    b"PALS": {**{f"PAL{i}".encode(): _pal for i in range(8)},
              b"TUNI": _bytes("player colours"), b"TMIN": _bytes("minimap colours"),
              b"TSEL": _bytes("selection colours"), b"THPB": _bytes("health bar colours")},
    b"FONT": {**{f.encode(): _font for f in ("F8  ", "F10 ", "F14 ", "F16 ", "F16X")},
              b"TFGM": _bytes("colour rows"), b"TFNT": _bytes("colour rows")},
    b"UI  ": {b"DLGS": _dlgs, b"POOL": _bytes("strings"), b"TFNT": _bytes("theme colour banks"),
              b"TEFF": _bytes("theme effect tables"), b"DGRP": _count16("dialog GRP headers")},
    b"SNDS": {b"SFX ": _sfx},
    b"PORT": {b"ROWS": _port_rows, b"SMKS": _count16("movie entries"), b"PALS": _count16("palettes")},
    kpm.KIND: {
        b"META": _meta, b"RULE": _rule, b"WAVS": _wavs, b"SPRT": _records(10), b"UNIT": _records(36),
        b"LOCS": _records(20), b"TRIG": _records(kpm.TRIGGER_SIZE), b"MBRF": _records(kpm.TRIGGER_SIZE),
        b"WALK": _walk, b"RGNS": _rgns, b"CONT": _cont, b"PACK": _pack,
    },
}


def dump(data: bytes, section: str | None = None) -> str:
    c = kpf.parse(data)
    out = [
        f"kind {c.kind.decode('latin1')}  schema {c.schema_version}  converter {c.converter_version}  profile {c.profile_id}",
        f"source {c.source_hash.hex()}",
        f"provenance {c.provenance_hash.hex()}",
    ]
    decoders = DECODERS.get(c.kind, {})
    for s in c.sections:
        name = s.fourcc.decode("latin1")
        if section and name != section:
            continue
        cls = "derived" if s.section_class == kpf.CLASS_DERIVED else "canonical"
        out.append(f"[{name}] {cls} schema {s.schema_version} size {len(s.payload)} crc {s.crc32:08x} flags {s.flags:#x}")
        if s.section_class == kpf.CLASS_DERIVED:
            out.append(f"  producer {s.algorithm_id.decode('latin1')} v{s.algorithm_version} inputs {s.inputs_hash.hex()}")
        if s.fourcc in decoders:
            out += ["  " + line for line in decoders[s.fourcc](s.payload)]
        else:
            out.append("  " + s.payload[:HEX_PREVIEW].hex(" ") + (" ..." if len(s.payload) > HEX_PREVIEW else ""))
    return "\n".join(out)
