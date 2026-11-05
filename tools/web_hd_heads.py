"""The web build's heads (ADR 0013, stage R4b; ADR 0015 item 4): the web page preloads only the first bytes of the
files it fetches, the HD2 art's it opens from their head (packs, ui.kpd, portraits.kpd) and the cinematics' packs, up to
their first bulk section, and the size of every such file (web_sizes.txt: "path size" per line, paths under generated/);
the rest is fetched as byte ranges from the files served beside the page (native/kp_app/sdl/kp_app_fetch.c).

    python3 tools/web_hd_heads.py GENERATED STAGE_GENERATED [--video hd2 sd]
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


def check_video_pack(path: Path) -> None:
    """The player reads every section of a cinematics pack but DATA at once, from the head (ADR 0015 item 4): DATA must
    be the last, or the page would have no movies and say so only in its console."""
    with path.open("rb") as f:
        last = max(section_table(f), key=lambda s: s[1])[0]
    if last != b"DATA":
        raise SystemExit(f"{path}: {last.decode('ascii', 'replace')} lies after DATA: the web would not have it at hand")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("generated", type=Path)
    parser.add_argument("stage", type=Path, help="the staged generated/ directory")
    parser.add_argument("--video", nargs="*", default=[], help="the cinematics' packs staged (sd, hd2)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    lines, heads = [], 0
    files = sorted((args.generated / VARIANT).rglob("*.kpd"))
    videos = [args.generated / "video" / f"cinematics-{v}.kpd" for v in args.video]
    for path in videos:
        check_video_pack(path)
    files += videos
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
