"""IScript and AI script binaries: table validation by the native lookup rules.

The bytecode is kept unchanged; programs address it by byte offset.
- iscript.bin: DWORD at 0 = offset of the id index, WORD pairs {id, header offset}
  ending at id 0xffff, searched from the start (`0x40f340`); a header starts with `SCPE`
  and its type byte, animation PCs from header +8 (atlas P04).
- aiscript.bin / bwscript.bin: DWORD at 0 = directory offset; AIScript records are
  16 bytes {id, offset, string, flags}, BWScript records 8 bytes {id, offset}, both
  ending at id 0; resolution `0x494d20` prefers a nonzero AIScript offset, else BWScript.
"""
from __future__ import annotations

import struct

from ..errors import ConverterError

ISCRIPT_END = 0xFFFF
HEADER_TAG = b"SCPE"
AI_RECORD = 16
BW_RECORD = 8


class ScriptError(ConverterError, ValueError):
    """A script table does not satisfy the native lookup."""


def iscript_headers(data: bytes) -> list[tuple[int, int]]:
    """(iscript id, header offset) in index order, first occurrence of each id only."""
    pos = struct.unpack_from("<I", data, 0)[0]
    seen: dict[int, int] = {}
    while True:
        if pos + 4 > len(data):
            raise ScriptError("iscript id index runs past the end without the 0xffff terminator")
        sid, offset = struct.unpack_from("<HH", data, pos)
        pos += 4
        if sid == ISCRIPT_END:
            break
        if data[offset : offset + 4] != HEADER_TAG:
            raise ScriptError(f"iscript {sid}: header at {offset:#x} does not start with SCPE")
        seen.setdefault(sid, offset)  # the native search stops at the first match
    return list(seen.items())


def ai_directory(data: bytes, stride: int) -> list[tuple[bytes, int]]:
    """(four-byte id, code offset) records up to the zero-id terminator."""
    pos = struct.unpack_from("<I", data, 0)[0]
    out = []
    while True:
        if pos + 4 > len(data):
            raise ScriptError("AI directory runs past the end without a zero id")
        sid = data[pos : pos + 4]
        if sid == b"\0\0\0\0":
            return out
        if pos + stride > len(data):
            raise ScriptError("AI directory record truncated")
        offset = struct.unpack_from("<I", data, pos + 4)[0]
        if offset >= len(data):
            raise ScriptError(f"AI script {sid!r}: offset {offset:#x} outside the file")
        out.append((sid, offset))
        pos += stride


def resolve_ai(aiscript: bytes, bwscript: bytes, sid: bytes) -> tuple[int, int] | None:
    """`0x494d20`: (offset, bin selector 0 = aiscript / 1 = bwscript) or None."""
    for rid, offset in ai_directory(aiscript, AI_RECORD):
        if rid == sid:
            if offset:
                return offset, 0
            break
    for rid, offset in ai_directory(bwscript, BW_RECORD):
        if rid == sid and offset:
            return offset, 1
    return None
