#!/usr/bin/env python3
"""The keys of tools/ci.sh's remembered passes: a digest of what a step's result depends on.

    ci_cache.py key [--content PATH ...] [--meta PATH ...] [--text STRING ...]

--content hashes the bytes of the files under each path, --meta only their names, sizes and modification times (the
converted data's 4 GB: touching a file only reruns the step, the safe side), --text the versions and settings that
are no files. A path that does not exist counts as such, so its later appearance changes the key.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import sys
from collections.abc import Iterable, Iterator
from pathlib import Path

READ_SIZE = 1 << 20


def _files(path: Path) -> Iterator[Path]:
    """The files under a path in a fixed order; its symbolic links to directories are not followed."""
    if path.is_dir():
        for folder, dirs, names in os.walk(path):
            dirs.sort()
            for name in sorted(names):
                yield Path(folder) / name
    elif path.exists():
        yield path


def _feed(h: "hashlib._Hash", *fields: object) -> None:
    for field in fields:
        h.update(f"{field}\0".encode("utf-8", "surrogateescape"))


def digest(content: Iterable[Path] = (), meta: Iterable[Path] = (), text: Iterable[str] = ()) -> str:
    """The hexadecimal SHA-256 of the given inputs. The three kinds never collide: each is labelled in the digest."""
    h = hashlib.sha256()
    for path in content:
        _feed(h, "content", path, path.exists())
        for file in _files(path):
            _feed(h, file)
            with file.open("rb") as f:
                while chunk := f.read(READ_SIZE):
                    h.update(chunk)
    for path in meta:
        _feed(h, "meta", path, path.exists())
        for file in _files(path):
            stat = file.lstat()
            _feed(h, file, stat.st_size, stat.st_mtime_ns)
    for item in text:
        _feed(h, "text", item)
    return h.hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    key = commands.add_parser("key", help="print the key of the given inputs")
    key.add_argument("--content", nargs="*", type=Path, default=[])
    key.add_argument("--meta", nargs="*", type=Path, default=[])
    key.add_argument("--text", nargs="*", default=[])
    args = parser.parse_args(argv)
    sys.stdout.write(digest(args.content, args.meta, args.text) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
