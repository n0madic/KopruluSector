"""Minimal ctypes binding to StormLib for read-only MPQ access."""
from __future__ import annotations

import ctypes
import ctypes.util
import logging
import os
import sys
from pathlib import Path

log = logging.getLogger(__name__)

STREAM_FLAG_READ_ONLY = 0x00000100
SFILE_OPEN_FROM_MPQ = 0x00000000
ERROR_FILE_NOT_FOUND = 2

# Search order: explicit override, then platform library names on the default loader path.
LIBRARY_ENV = "KOPRULU_STORMLIB"
_PLATFORM_NAMES = {
    "darwin": ["libstorm.dylib", "/opt/homebrew/lib/libstorm.dylib", "/usr/local/lib/libstorm.dylib"],
    "win32": ["StormLib.dll", "storm.dll"],
}
_DEFAULT_NAMES = ["libstorm.so", "libstorm.so.9"]


class StormLibError(RuntimeError):
    """Raised when StormLib cannot be loaded or an MPQ operation fails."""


class MpqFileNotFound(StormLibError):
    """Raised when a path is absent from an archive."""


def _candidates() -> list[str]:
    override = os.environ.get(LIBRARY_ENV)
    if override:
        return [override]
    names = list(_PLATFORM_NAMES.get(sys.platform, _DEFAULT_NAMES))
    found = ctypes.util.find_library("storm") or ctypes.util.find_library("StormLib")
    if found:
        names.insert(0, found)
    return names


def _load() -> ctypes.CDLL:
    errors = []
    for name in _candidates():
        try:
            lib = ctypes.CDLL(name)
        except OSError as exc:
            errors.append(f"{name}: {exc}")
            continue
        _declare(lib)
        log.debug("loaded StormLib from %s", name)
        return lib
    raise StormLibError(
        f"StormLib not found (set {LIBRARY_ENV}); tried: " + "; ".join(errors)
    )


def _declare(lib: ctypes.CDLL) -> None:
    handle = ctypes.c_void_p
    lib.SFileOpenArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(handle)]
    lib.SFileOpenArchive.restype = ctypes.c_bool
    lib.SFileCloseArchive.argtypes = [handle]
    lib.SFileCloseArchive.restype = ctypes.c_bool
    lib.SFileHasFile.argtypes = [handle, ctypes.c_char_p]
    lib.SFileHasFile.restype = ctypes.c_bool
    lib.SFileOpenFileEx.argtypes = [handle, ctypes.c_char_p, ctypes.c_uint, ctypes.POINTER(handle)]
    lib.SFileOpenFileEx.restype = ctypes.c_bool
    lib.SFileGetFileSize.argtypes = [handle, ctypes.POINTER(ctypes.c_uint)]
    lib.SFileGetFileSize.restype = ctypes.c_uint
    lib.SFileReadFile.argtypes = [handle, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint), ctypes.c_void_p]
    lib.SFileReadFile.restype = ctypes.c_bool
    lib.SFileCloseFile.argtypes = [handle]
    lib.SFileCloseFile.restype = ctypes.c_bool


_lib: ctypes.CDLL | None = None


def library() -> ctypes.CDLL:
    global _lib
    if _lib is None:
        _lib = _load()
    return _lib


def _last_error() -> int:
    if sys.platform == "win32":
        return ctypes.GetLastError()  # type: ignore[attr-defined]
    return ctypes.get_errno()


def _encode(name: str) -> bytes:
    # MPQ paths are byte strings in the original code page; keep backslashes as-is.
    return name.encode("latin-1")


class Archive:
    """A read-only MPQ archive. Use as a context manager."""

    def __init__(self, path: str | Path):
        self.path = Path(path)
        self._handle = ctypes.c_void_p()
        lib = library()
        if not lib.SFileOpenArchive(str(self.path).encode(), 0, STREAM_FLAG_READ_ONLY, ctypes.byref(self._handle)):
            raise StormLibError(f"cannot open MPQ {self.path} (error {_last_error()})")

    def close(self) -> None:
        if self._handle:
            library().SFileCloseArchive(self._handle)
            self._handle = ctypes.c_void_p()

    def __enter__(self) -> "Archive":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def has(self, name: str) -> bool:
        return bool(library().SFileHasFile(self._handle, _encode(name)))

    def read(self, name: str) -> bytes:
        lib = library()
        file_handle = ctypes.c_void_p()
        if not lib.SFileOpenFileEx(self._handle, _encode(name), SFILE_OPEN_FROM_MPQ, ctypes.byref(file_handle)):
            raise MpqFileNotFound(f"{name} not in {self.path.name}")
        try:
            high = ctypes.c_uint()
            size = lib.SFileGetFileSize(file_handle, ctypes.byref(high))
            if high.value:
                raise StormLibError(f"{name}: file larger than 4 GiB")
            buf = ctypes.create_string_buffer(size)
            got = ctypes.c_uint()
            if not lib.SFileReadFile(file_handle, buf, size, ctypes.byref(got), None) or got.value != size:
                raise StormLibError(f"{name}: short read {got.value}/{size} from {self.path.name}")
            return buf.raw[:size]
        finally:
            lib.SFileCloseFile(file_handle)
