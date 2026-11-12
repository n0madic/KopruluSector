"""Compare StarCraft: Remastered's copies of the rules files with the effective copies of a profile."""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

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


@dataclass(frozen=True)
class OutputComparison:
    path: str  # relative to the output directories
    status: str  # "same" (bytes), "payloads" (equal sections; only header and inputs hashes differ), "differs", "only"
    sections: tuple[str, ...] = ()  # the differing sections' fourccs, or the side holding an unmatched file


def _sections(data: bytes) -> list[tuple[bytes, bytes]] | None:
    from ..kpf.container import ContainerError, parse

    try:
        return [(s.fourcc, s.payload) for s in parse(data).sections]
    except ContainerError:
        return None


def compare_outputs(a: Path, b: Path, patterns: tuple[str, ...] = ("**/*.kpd", "**/*.kpm")) -> list[OutputComparison]:
    """Containers converted from two sources, file by file and section by section."""
    names = {p.relative_to(root).as_posix(): root for root in (a, b) for pattern in patterns for p in root.glob(pattern)}
    result = []
    for name in sorted(names):
        pa, pb = a / name, b / name
        if not (pa.is_file() and pb.is_file()):
            result.append(OutputComparison(name, "only", (str(a if pa.is_file() else b),)))
            continue
        da, db = pa.read_bytes(), pb.read_bytes()
        if da == db:
            result.append(OutputComparison(name, "same"))
            continue
        sa, sb = _sections(da), _sections(db)
        if sa is None or sb is None:
            result.append(OutputComparison(name, "differs"))
            continue
        fa, fb = dict(sa), dict(sb)
        differ = tuple(sorted({f.decode("latin1") for f in fa.keys() | fb.keys() if fa.get(f) != fb.get(f)}))
        result.append(OutputComparison(name, "differs" if differ else "payloads", differ))
    return result
