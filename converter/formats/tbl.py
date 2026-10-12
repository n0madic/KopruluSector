"""TBL string tables and fixed-layout DAT columns (only what the converter needs so far)."""
from __future__ import annotations

import struct


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


def dat_column(data: bytes, offset: int, element_size: int, count: int) -> tuple[int, ...]:
    """Read one DAT column (docs/research/dat-schemas.md) as unsigned integers."""
    fmt = {1: "B", 2: "H", 4: "I"}[element_size]
    return struct.unpack_from(f"<{count}{fmt}", data, offset)


# images.dat column 0 (grp_file, 999 DWORDs) and sfxdata.dat column 0 (sound_file, 1144 DWORDs).
IMAGES_COUNT = 999
SFXDATA_COUNT = 1144


def image_grp_paths(images_dat: bytes, images_tbl: bytes) -> list[str | None]:
    """GRP MPQ path per image id: `unit\\` + images.tbl entry (1-based), or None."""
    names = decode_tbl(images_tbl)
    ids = dat_column(images_dat, 0, 4, IMAGES_COUNT)
    return [("unit\\" + names[g - 1].decode("latin1")) if g else None for g in ids]


def sfx_paths(sfxdata_dat: bytes, sfxdata_tbl: bytes) -> list[str | None]:
    """Sound MPQ path per sfx id: `sound\\` + sfxdata.tbl entry (1-based), or None."""
    names = decode_tbl(sfxdata_tbl)
    ids = dat_column(sfxdata_dat, 0, 4, SFXDATA_COUNT)
    return [("sound\\" + names[s - 1].decode("latin1")) if s else None for s in ids]
