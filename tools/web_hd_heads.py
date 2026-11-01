"""The web build's HD2 heads (ADR 0013, stage R4b): the web page preloads only the first bytes of the HD2 files it
opens from their head (packs, ui.kpd, portraits.kpd: up to their first bulk section) and the size of every HD2 file
(sizes.txt: "path size" per line, paths under generated/); the rest is fetched as byte ranges from the files served
beside the page (native/kp_app/sdl/kp_app_fetch.c).

    python3 tools/web_hd_heads.py GENERATED STAGE_GENERATED
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
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    lines, heads = [], 0
    for path in sorted((args.generated / VARIANT).rglob("*.kpd")):
        rel = path.relative_to(args.generated)
        lines.append(f"generated/{rel.as_posix()} {path.stat().st_size}")
        n = head_size(path)
        if n is not None:
            out = args.stage / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            with path.open("rb") as f:
                out.write_bytes(f.read(n))
            heads += n
    (args.stage / VARIANT).mkdir(parents=True, exist_ok=True)
    (args.stage / VARIANT / "sizes.txt").write_text("\n".join(lines) + "\n")
    log.info("web HD2: %d files, %d bytes of heads preloaded", len(lines), heads)


if __name__ == "__main__":
    main()
