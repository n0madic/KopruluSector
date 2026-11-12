"""Names and checks shared by the pack pages (the Python converter's kpf/resources.py, kp_packs.c's twin until ADR 0019's stage C7) and grpmeta (scdata/kpf/kpd.py).

Plan §5.1 / D10: transparency comes from the RLE packet type and index 0 may be opaque; the pack pages store index
0 as transparent, so a GRP that draws opaque index 0 is refused (none does in the stock data).
"""
from __future__ import annotations

from ..errors import ConverterError


class OpaqueZeroError(ConverterError, ValueError):
    """A GRP draws opaque index 0, which the pack pages cannot hold."""


def animation_id(mpq_path: str) -> str:
    """'unit\\terran\\marine.grp' -> 'terran_marine' (lower case, unique per GRP path)."""
    stem = mpq_path.lower().removeprefix("unit\\").removesuffix(".grp")
    return stem.replace("\\", "_")
