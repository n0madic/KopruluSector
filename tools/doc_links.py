#!/usr/bin/env python3
"""The documents' links: every relative link of a tracked Markdown file names a file that exists and, with a
fragment into a Markdown file, one of its headings by GitHub's slug (or an explicit `<a id>`). A link inside code (a
fence or a span) is no link; a URL with a scheme is not checked.

    python3 tools/doc_links.py [ROOT]

Exits 1 with each broken link as FILE:LINE.
"""
from __future__ import annotations

import logging
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

log = logging.getLogger("doc_links")

LINK = re.compile(r"(?<!!)\[(?:[^\[\]]|\[[^\]]*\])*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE = re.compile(r"^\s*(```|~~~)")
HTML_ID = re.compile(r"<a\s+(?:name|id)=\"([^\"]+)\"")
CODE_SPAN = re.compile(r"`[^`]*`")
SCHEME = re.compile(r"^[a-z][a-z0-9+.-]*:", re.IGNORECASE)


def slug(heading: str) -> str:
    """GitHub's anchor of a heading's text: its words lowercased, punctuation but '-' and '_' dropped, spaces '-'."""
    text = re.sub(r"<[^>]+>", "", heading)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # a link's text
    text = text.replace("`", "").strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


def prose_lines(text: str) -> list[tuple[int, str]]:
    """The lines outside code fences, numbered from 1."""
    lines, fenced = [], False
    for no, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line):
            fenced = not fenced
        elif not fenced:
            lines.append((no, line))
    return lines


def anchors(text: str) -> set[str]:
    """A document's anchors: its headings' slugs (a repeated one numbered -1, -2, ...) and its explicit ids."""
    found: set[str] = set()
    counts: dict[str, int] = {}
    for _, line in prose_lines(text):
        found.update(HTML_ID.findall(line))
        m = HEADING.match(line)
        if not m:
            continue
        s = slug(m.group(2))
        n = counts.get(s, 0)
        counts[s] = n + 1
        found.add(s if n == 0 else f"{s}-{n}")
    return found


def broken_links(root: Path, files: list[str]) -> list[str]:
    """Each broken link of files (paths relative to root) as 'FILE:LINE: what'."""
    cache: dict[Path, set[str]] = {}
    broken = []
    for rel in files:
        src = root / rel
        for no, line in prose_lines(src.read_text(encoding="utf-8", errors="replace")):
            for target in LINK.findall(CODE_SPAN.sub("", line)):
                if SCHEME.match(target):
                    continue
                file_part, _, fragment = target.partition("#")
                dest = src if not file_part else (src.parent / unquote(file_part)).resolve()
                if file_part and not dest.exists():
                    broken.append(f"{rel}:{no}: no file {target}")
                    continue
                if not fragment or dest.suffix != ".md":
                    continue
                if dest not in cache:
                    cache[dest] = anchors(dest.read_text(encoding="utf-8", errors="replace"))
                if unquote(fragment).lower() not in cache[dest]:
                    broken.append(f"{rel}:{no}: no heading {target}")
    return broken


def main(argv: list[str]) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    root = Path(argv[1] if len(argv) > 1 else Path(__file__).resolve().parents[1]).resolve()
    listed = subprocess.run(["git", "ls-files", "*.md"], cwd=root, capture_output=True, text=True, check=True)
    files = listed.stdout.split()
    broken = broken_links(root, files)
    for line in broken:
        log.error("%s", line)
    log.info("doc_links: %d documents, %d broken links", len(files), len(broken))
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
