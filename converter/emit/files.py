"""Writing the converter's outputs: stable bytes, written only when they change."""
from __future__ import annotations

import hashlib
import os
import tempfile
from collections.abc import Callable
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def project_path(path: Path) -> str:
    """The path of a file inside the repository ("/generated/..."); the OGG streams' serials derive from it."""
    return "/" + path.resolve().relative_to(ROOT).as_posix()


def write_atomic(path: Path, data: bytes) -> None:
    """Write data to path whole or not at all: into a temporary file beside it, then renamed over it."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.", suffix=".part")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def write_if_changed(path: Path, text: str | bytes) -> bool:
    """Write only when the content differs, so incremental steps see stable inputs."""
    data = text.encode() if isinstance(text, str) else text
    if path.exists() and path.read_bytes() == data:
        return False
    write_atomic(path, data)
    return True


def link_file(source: Path, path: Path) -> None:
    """path as a hard link to source (a copy where links are refused), replacing what was there."""
    if path.exists() and path.samefile(source):
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f".{path.name}.link")
    tmp.unlink(missing_ok=True)
    try:
        os.link(source, tmp)
    except OSError:
        write_atomic(path, source.read_bytes())
        return
    os.replace(tmp, path)


def cached(cache: Path | None, key: bytes, suffix: str, make: Callable[[], bytes],
           complete: Callable[[bytes], bool]) -> bytes:
    """make()'s bytes, kept in cache/<SHA-256 of key><suffix> (none without a cache): read back when there and
    complete(data) holds, else made again and written whole (write_atomic)."""
    path = cache / (hashlib.sha256(key).hexdigest() + suffix) if cache is not None else None
    if path is not None and path.is_file():
        data = path.read_bytes()
        if complete(data):
            return data
    data = make()
    if path is not None:
        write_atomic(path, data)
    return data
