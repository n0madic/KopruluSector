"""Dialog BIN files and the native fixup `0x4c33e0` (docs/research/native-dialog-bin.md).

Records are 86 bytes (pinned PyMS DialogBIN layout `<L6H4LH5L4HLL4HLL`); SMK nodes are
30 bytes (`<LH3LHHLL`). Traversal: the root is the record at offset 0; a type-0 record
continues with its first child at +0x42, any other record with its next link at +0.

The fixup the original applies, in record order:
- next link (+0) and text (+0x14) offsets become pointers;
- with flag 0x200 the first text byte is ASCII-lowercased (`0x4d1375`, locale 0);
- type 14: SMK node links and filenames are relocated and the control x/y are added
  (u16 wrap) to the words at +0x46/+0x4a and +0x48/+0x4c;
- callbacks +0x2a/+0x2e come from the type tables `0x4f10f8`/`0x4f1148`;
- a type-5 image with text loads its bitmap (the converter records the file name);
- a negative signed bitmap value at +0x10 shares another record's bitmap (flag 0x20);
- type 0 relocates its child link; other types store the parent pointer at +0x32.
`fixup` reproduces those byte mutations for verification against the native evidence;
`parse` returns the normalized widgets that ui.kpd stores.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from ..errors import ConverterError

RECORD = struct.Struct("<L6H4LH5L4HLL4HLL")
SMK = struct.Struct("<LH3LHHLL")
assert RECORD.size == 86 and SMK.size == 30

TYPE_CONTAINER = 0
TYPE_IMAGE = 5
TYPE_HTML = 14  # widgets with SMK overlays (briefing-room portraits)
FLAG_HOTKEY = 0x200
FLAG_SHARED_BITMAP = 0x20
NO_SHARED_BITMAP = 0xFFFFFFFF  # ui.kpd value of a record that shares no bitmap
LOWERCASE = range(65, 91)


class DialogError(ConverterError, ValueError):
    """Malformed dialog BIN."""


@dataclass(frozen=True)
class SmkNode:
    flags: int
    filename: bytes
    offset_x: int
    offset_y: int
    unknown: tuple[int, int, int, int]  # raw +0x06, +0x0e, +0x16, +0x1a


@dataclass
class Widget:
    offset: int
    parent: int  # index of the container widget, -1 for the root
    type: int
    identifier: int
    flags: int
    rect: tuple[int, int, int, int]  # x1, y1, x2, y2 as stored (signed)
    size: tuple[int, int]
    text: bytes | None  # after the hotkey lowercase; for type 5 the bitmap file name
    responsive: tuple[int, int, int, int]  # +0x36 words
    extra: tuple[int, int, int, int]  # +0x46..+0x4c words after the type-14 x/y addition
    shared_bitmap: int | None  # raw u32 at +0x10 when negative (see parse), else None
    smks: list[SmkNode] = field(default_factory=list)
    word1c: int = 0  # +0x1c u16 (unused by the original)
    delay: int = 0  # +0x1e u16: show delay in milliseconds (ControlDefaultInteract 0x4c1e90, codes 8/10/13)
    user: int = 0  # +0x26 u32 user value


def _cstring(data: bytes, offset: int) -> bytes:
    end = data.find(b"\0", offset)
    if end < 0:
        raise DialogError(f"text at {offset:#x} is not NUL-terminated")
    return data[offset:end]


def _order(data: bytes) -> list[tuple[int, int]]:
    """(record offset, parent index) in native traversal order."""
    out, seen, at, parent, stack = [], set(), 0, -1, []
    while True:
        if at in seen or at + RECORD.size > len(data):
            raise DialogError(f"record at {at:#x} repeats or runs past the end")
        seen.add(at)
        values = RECORD.unpack_from(data, at)
        kind = values[12]
        out.append((at, parent))
        if kind == TYPE_CONTAINER:
            parent, at = len(out) - 1, values[22]
        else:
            at = values[0]
        if not at:
            return out


def record(nxt=0, kind=1, ident=1, flags=0, string=0, x1=0, y1=0, child_or_smk=0, extra=(0, 0, 0, 0), bitmap=0,
           width=10, height=10, responsive=(1, 2, 3, 4), x2=None, y2=None) -> bytes:
    """One 86-byte record (the fields `parse` reads; the callbacks and the parent link zero)."""
    x2 = x1 + width - 1 if x2 is None else x2
    y2 = y1 + height - 1 if y2 is None else y2
    values = [nxt, x1 & 0xFFFF, y1 & 0xFFFF, x2 & 0xFFFF, y2 & 0xFFFF, width, height, bitmap, string, flags, 0,
              ident & 0xFFFF, kind, 0, 0, 0, 0, *responsive, 0, child_or_smk, *extra, 0, 0]
    return RECORD.pack(*values)


def build(widgets: list[dict]) -> bytes:
    """A dialog BIN from its widgets: the container first (its rect x, y with its size), then its children in order;
    each a dict of type, id, flags, rect [x, y, width, height] and text (str, Windows-1252, or absent). The container's
    x2/y2 are its width/height - 1 and its bitmap size its own (InitDialog's rules, as the original's BINs have them).
    The inverse of `parse` for a flat dialog (no SMKs, images or shared bitmaps)."""
    if not widgets or widgets[0]["type"] != TYPE_CONTAINER or any(w["type"] == TYPE_CONTAINER for w in widgets[1:]):
        raise DialogError("a dialog is one container followed by its children")
    texts, strings = bytearray(), []
    start = len(widgets) * RECORD.size
    for w in widgets:
        text = w.get("text")
        if text is None:
            strings.append(0)
            continue
        strings.append(start + len(texts))
        texts += text.encode("cp1252") + b"\0"
    out = bytearray()
    for k, w in enumerate(widgets):
        x, y, width, height = w["rect"]
        if k == 0:
            out += record(kind=TYPE_CONTAINER, ident=w["id"], flags=w["flags"], string=strings[k], x1=x, y1=y,
                          x2=width - 1, y2=height - 1, width=width, height=height, responsive=(width, height, 0, 0),
                          child_or_smk=RECORD.size if len(widgets) > 1 else 0)
        else:
            nxt = (k + 1) * RECORD.size if k + 1 < len(widgets) else 0
            out += record(nxt=nxt, kind=w["type"], ident=w["id"], flags=w["flags"], string=strings[k], x1=x, y1=y,
                          width=width, height=height, responsive=(0, 0, 0, 0))
    return bytes(out + texts)


def parse(data: bytes) -> list[Widget]:
    widgets = []
    for at, parent in _order(data):
        v = RECORD.unpack_from(data, at)
        nxt, x1, y1, x2, y2, width, height, bitmap, string, flags, word1c_delay, ident, kind = v[:13]
        text = None
        if string:
            text = _cstring(data, string)
            if flags & FLAG_HOTKEY and text and text[0] in LOWERCASE:
                text = bytes([text[0] + 32]) + text[1:]
        extra = list(v[23:27])
        smks = []
        if kind == TYPE_HTML:
            node = v[22]
            while node:
                if node + SMK.size > len(data):
                    raise DialogError(f"SMK node at {node:#x} runs past the end")
                n = SMK.unpack_from(data, node)
                smks.append(SmkNode(n[1], _cstring(data, n[3]) if n[3] else b"", n[5], n[6], (n[2], n[4], n[7], n[8])))
                node = n[0]
            extra = [(extra[0] + x1) & 0xFFFF, (extra[1] + y1) & 0xFFFF, (extra[2] + x1) & 0xFFFF, (extra[3] + y1) & 0xFFFF]
        shared = None
        if bitmap & 0x80000000:
            # A negative signed value refers to another record's bitmap relative to the file
            # base: the fixup reads the bitmap at file base + value + 0x10. Kept as the raw u32.
            if bitmap == NO_SHARED_BITMAP:
                raise DialogError(f"record at {at:#x}: shared-bitmap reference -1 is the ui.kpd 'none' value")
            shared = bitmap
            flags |= FLAG_SHARED_BITMAP
        signed = [x - 0x10000 if x & 0x8000 else x for x in (x1, y1, x2, y2)]
        widgets.append(Widget(at, parent, kind, ident, flags, tuple(signed), (width, height), text,
                              tuple(v[17:21]), tuple(extra), shared, smks, word1c_delay & 0xFFFF, word1c_delay >> 16,
                              v[13]))
    return widgets


def fixup(data: bytes, first: int, base: int, interactions: tuple[int, ...], draws: tuple[int, ...]) -> tuple[bytes, bool]:
    """Byte mutations of `0x4c33e0` on a buffer loaded at `base`, stopping (False) at the
    first type-5 image with text, where the original calls the bitmap loader."""
    out = bytearray(data)

    def val(at: int, size: int = 4) -> int:
        return int.from_bytes(out[at : at + size], "little")

    def put(at: int, value: int, size: int = 4) -> None:
        out[at : at + size] = (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little")

    at: int | None = first
    while at is not None:
        nxt, string, flags, kind = val(at), val(at + 0x14), val(at + 0x18), val(at + 0x22, 2)
        if nxt:
            put(at, base + nxt)
        if string:
            if flags & FLAG_HOTKEY and val(string, 1) in LOWERCASE:
                put(string, val(string, 1) + 32, 1)
            put(at + 0x14, base + string)
        if kind == TYPE_HTML:
            cell = at + 0x42
            while val(cell):
                node = val(cell)
                put(cell, base + node)
                if val(node + 0xA):
                    put(node + 0xA, base + val(node + 0xA))
                cell = node
            x, y = val(at + 4, 2), val(at + 6, 2)
            for o in (0x46, 0x4A):
                put(at + o, val(at + o, 2) + x, 2)
            for o in (0x48, 0x4C):
                put(at + o, val(at + o, 2) + y, 2)
        put(at + 0x2A, interactions[kind])
        put(at + 0x2E, draws[kind])
        if kind == TYPE_IMAGE and string:
            return bytes(out), False
        bitmap = val(at + 0x10)
        if bitmap & 0x80000000:
            put(at + 0x10, val(bitmap - (1 << 32) + 0x10))
            put(at + 0x18, flags | FLAG_SHARED_BITMAP)
        if kind == TYPE_CONTAINER:
            child = val(at + 0x42)
            if child:
                put(at + 0x42, base + child)
            at = child or None
        else:
            put(at + 0x32, base)
            at = nxt or None
    return bytes(out), True
