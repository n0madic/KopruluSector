"""TBL string tables, and MPQ paths that DAT columns name through them."""
from __future__ import annotations

import struct

from . import dat


class TblError(ValueError):
    """Malformed TBL data."""


def decode_tbl(data: bytes) -> list[bytes]:
    """Return raw entries up to the first NUL (byte strings, not re-encoded)."""
    if len(data) < 2:
        raise TblError("shorter than count")
    count = struct.unpack_from("<H", data, 0)[0]
    if 2 + 2 * count > len(data):
        raise TblError("offset table out of bounds")
    out = []
    for i in range(count):
        off = struct.unpack_from("<H", data, 2 + 2 * i)[0]
        end = data.find(b"\0", off)
        out.append(data[off : end if end >= 0 else len(data)])
    return out


def _paths(dat_bytes: bytes, tbl_bytes: bytes, path: str, column: str, prefix: str) -> list[str | None]:
    layout = next(f for f in dat.load_layouts() if f.path == path)
    names = decode_tbl(tbl_bytes)
    return [(prefix + names[i - 1].decode("latin1")) if i else None for i in dat.values(layout, dat_bytes, column)]


def image_grp_paths(images_dat: bytes, images_tbl: bytes) -> list[str | None]:
    """GRP MPQ path per image id: `unit\\` + images.tbl entry (1-based), or None."""
    return _paths(images_dat, images_tbl, "arr\\images.dat", "grp_file", "unit\\")


def sfx_paths(sfxdata_dat: bytes, sfxdata_tbl: bytes) -> list[str | None]:
    """Sound MPQ path per sfx id: `sound\\` + sfxdata.tbl entry (1-based), or None."""
    return _paths(sfxdata_dat, sfxdata_tbl, "arr\\sfxdata.dat", "sound_file", "sound\\")
