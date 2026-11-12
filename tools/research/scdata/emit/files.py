"""Writing the converter's outputs: stable bytes, written only when they change."""
from __future__ import annotations

import hashlib
import os
import tempfile
from collections.abc import Callable, Iterable, Sequence
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
COMPARE_CHUNK = 1 << 20  # the bytes of a file compared with its new content at a time


def project_path(path: Path) -> str:
    """The path of a file inside the repository ("/generated/..."); the OGG streams' serials derive from it."""
    return "/" + path.resolve().relative_to(ROOT).as_posix()


def write_atomic(path: Path, data: bytes) -> None:
    """Write data to path whole or not at all: into a temporary file beside it, then renamed over it."""
    write_parts_atomic(path, (data,))


def write_parts_atomic(path: Path, parts: Iterable[bytes]) -> None:
    """write_atomic of the pieces' bytes one after the other (a large pack is never joined in memory)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.", suffix=".part")
    try:
        with os.fdopen(fd, "wb") as f:
            for part in parts:
                f.write(part)
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def write_if_changed(path: Path, text: str | bytes) -> bool:
    """Write only when the content differs, so incremental steps see stable inputs."""
    return write_parts_if_changed(path, (text.encode() if isinstance(text, str) else text,))


def _file_holds(path: Path, parts: Sequence[bytes]) -> bool:
    """Whether the file's bytes are the pieces' joined, read COMPARE_CHUNK bytes at a time."""
    if not path.exists() or path.stat().st_size != sum(len(p) for p in parts):
        return False
    with path.open("rb") as f:
        for part in parts:
            view = memoryview(part)
            for at in range(0, len(view), COMPARE_CHUNK):
                chunk = view[at:at + COMPARE_CHUNK]
                if f.read(len(chunk)) != chunk:
                    return False
    return True


def write_parts_if_changed(path: Path, parts: Sequence[bytes]) -> bool:
    """write_if_changed of the pieces' bytes one after the other, compared and written without joining them."""
    if _file_holds(path, parts):
        return False
    write_parts_atomic(path, parts)
    return True


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
