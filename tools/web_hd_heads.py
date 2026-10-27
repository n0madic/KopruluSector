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

from converter.kpf.container import parse  # noqa: E402

log = logging.getLogger(__name__)

VARIANT = Path("hd") / "hd2"
BULK = {b"DIFF", b"TEAM", b"IMGS", b"DATA"}  # the sections read as ranges


def head_size(data: bytes) -> int | None:
    """The bytes before a container's first bulk section, or None when it has none (read whole)."""
    sections = sorted(parse(data, verify_crc=False).sections, key=lambda s: s.offset)
    for s in sections:
        if s.fourcc in BULK:
            return s.offset
    return None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("generated", type=Path)
    parser.add_argument("stage", type=Path, help="the staged generated/ directory")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    lines, heads = [], 0
    for path in sorted((args.generated / VARIANT).rglob("*.kpd")):
        rel = path.relative_to(args.generated)
        data = path.read_bytes()
        lines.append(f"generated/{rel.as_posix()} {len(data)}")
        n = head_size(data)
        if n is not None:
            out = args.stage / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(data[:n])
            heads += n
    (args.stage / VARIANT).mkdir(parents=True, exist_ok=True)
    (args.stage / VARIANT / "sizes.txt").write_text("\n".join(lines) + "\n")
    log.info("web HD2: %d files, %d bytes of heads preloaded", len(lines), heads)


if __name__ == "__main__":
    main()
