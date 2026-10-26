"""Compare StarCraft: Remastered's copies of the rules files with the effective copies of a profile."""
from __future__ import annotations

from dataclasses import dataclass

from ..formats.dat import load_layouts
from ..mpq.profile import Resolver
from .storage import CascStorage

SCRIPTS = ("scripts\\iscript.bin", "scripts\\aiscript.bin", "scripts\\bwscript.bin")


@dataclass(frozen=True)
class Comparison:
    name: str
    provider: str  # the profile archive that provides the effective copy
    profile_size: int
    casc_size: int
    equal: bool


def rules_files() -> tuple[str, ...]:
    """The profile's DAT tables (from their layouts) and the three script files."""
    return (*(layout.path for layout in load_layouts()), *SCRIPTS)


def compare(resolver: Resolver, storage: CascStorage, names: tuple[str, ...]) -> list[Comparison]:
    result = []
    for name in names:
        provider, original = resolver.read(name)
        remastered = storage.read(name)
        result.append(Comparison(name, provider, len(original), len(remastered), original == remastered))
    return result
