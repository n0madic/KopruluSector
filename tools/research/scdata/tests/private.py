"""Tests that read private inputs (the gitignored research evidence and golden oracle data) skip without them."""
from __future__ import annotations

import unittest
from pathlib import Path


def needs(*paths: Path):
    """Skips the test or class unless every path exists (a public checkout has none of the private inputs)."""
    missing = [p for p in paths if not p.exists()]
    names = ", ".join(p.name for p in missing)
    return unittest.skipIf(bool(missing), f"{names}: a private input, not in the repository")
