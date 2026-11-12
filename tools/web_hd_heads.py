"""The web build's heads (ADR 0013, stage R4b): the web page preloads only the first bytes of the files it fetches,
the HD2 interface's it opens from their head (ui.kpd, portraits.kpd), up to their first bulk section, and the size of
every such file (web_sizes.txt: "path size" per line, paths under generated/); the rest is fetched as byte ranges from
the files served beside the page (native/kp_app/sdl/kp_app_fetch.c). The cinematics and the HD world are the repack's
(ADR 0019, stages C4 and C5): the core's listing places each movie and HD container in its group, which the page
fetches by those ranges (an HD pack opened from its head in the core); the groups beside the page are named in
web_sizes.txt too (--ranged), so that the game plays the sets that are there.

    python3 tools/web_hd_heads.py GENERATED STAGE_GENERATED [--ranged repack/koprulu-hd2.mpq ...]
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from converter.kpf.container import section_table  # noqa: E402

log = logging.getLogger(__name__)

VARIANT = Path("hd") / "hd2"
INTERFACE = ("ui.kpd", "portraits.kpd")  # TODO: from Remastered's files at the runtime (ADR 0019, stage C6)
BULK = {b"DIFF", b"TEAM", b"IMGS", b"DATA"}  # the sections read as ranges


def head_size(path: Path) -> int | None:
    """The bytes before a container's first bulk section, or None when it has none (read whole). Only the header and
    the section table are read (the HD2 packs are hundreds of megabytes)."""
    with path.open("rb") as f:
        sections = sorted(section_table(f), key=lambda s: s[1])
    return next((offset for fourcc, offset, _size in sections if fourcc in BULK), None)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("generated", type=Path)
    parser.add_argument("stage", type=Path, help="the staged generated/ directory")
    parser.add_argument("--ranged", nargs="*", default=[], type=Path,
                        help="files under GENERATED fetched by ranges with no head (the repack's cinematics' and HD groups)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    lines, heads = [], 0
    files = sorted(p for p in (args.generated / VARIANT / name for name in INTERFACE) if p.is_file())
    for path in [args.generated / p for p in args.ranged]:  # no head: their places are the core's listing's
        lines.append(f"generated/{path.relative_to(args.generated).as_posix()} {path.stat().st_size}")
    for path in files:
        rel = path.relative_to(args.generated)
        lines.append(f"generated/{rel.as_posix()} {path.stat().st_size}")
        n = head_size(path)
        if n is not None:
            out = args.stage / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            with path.open("rb") as f:
                out.write_bytes(f.read(n))
            heads += n
    args.stage.mkdir(parents=True, exist_ok=True)
    (args.stage / "web_sizes.txt").write_text("\n".join(lines) + "\n")
    log.info("web: %d files fetched, %d bytes of heads preloaded", len(lines), heads)


if __name__ == "__main__":
    main()
