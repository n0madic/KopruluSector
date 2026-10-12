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


DECODERS = {
    kpm.KIND: {
        b"META": _meta, b"RULE": _rule, b"WAVS": _wavs, b"SPRT": _records(10), b"UNIT": _records(36),
        b"LOCS": _records(20), b"TRIG": _records(kpm.TRIGGER_SIZE), b"MBRF": _records(kpm.TRIGGER_SIZE),
        b"WALK": _walk, b"RGNS": _rgns, b"CONT": _cont,
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
