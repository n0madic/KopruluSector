"""Guest file system: a read-only view of the game directory plus an in-memory overlay.

Guest paths are Windows paths. ``C:\\StarCraft\\`` is the installation directory and
maps (case-insensitively) onto the host game directory, which is never modified. Every
write goes to an in-memory overlay that starts empty on each run, so runs are
reproducible and the private workspace stays untouched. Other guest directories
(user documents, temp) exist only in the overlay.
"""
from __future__ import annotations

import fnmatch
import logging
from dataclasses import dataclass, field
from pathlib import Path

log = logging.getLogger(__name__)

INSTALL_DIR = "C:\\StarCraft"
USER_DIR = "C:\\Users\\Player"
DOCUMENTS_DIR = USER_DIR + "\\Documents"
APPDATA_DIR = USER_DIR + "\\AppData\\Roaming"
LOCALAPPDATA_DIR = USER_DIR + "\\AppData\\Local"
TEMP_DIR = USER_DIR + "\\AppData\\Local\\Temp"

ERROR_FILE_NOT_FOUND = 2
ERROR_PATH_NOT_FOUND = 3
ERROR_ACCESS_DENIED = 5
ERROR_FILE_EXISTS = 80
ERROR_ALREADY_EXISTS = 183

FILE_ATTRIBUTE_DIRECTORY = 0x10
FILE_ATTRIBUTE_ARCHIVE = 0x20
FILE_ATTRIBUTE_READONLY = 0x1


def norm(path: str) -> str:
    p = path.replace("/", "\\")
    if p.startswith("\\\\?\\"):
        p = p[4:]
    parts: list[str] = []
    drive = ""
    if len(p) >= 2 and p[1] == ":":
        drive, p = p[:2].upper(), p[2:]
    for comp in p.split("\\"):
        if comp in ("", "."):
            continue
        if comp == "..":
            if parts:
                parts.pop()
            continue
        parts.append(comp)
    return (drive or "C:") + "\\" + "\\".join(parts)


@dataclass
class OpenFile:
    path: str
    data: bytearray | None = None       # overlay contents (writable)
    host: Path | None = None            # read-only host file
    fh: object = None
    pos: int = 0
    writable: bool = False
    size_cache: int = 0

    def size(self) -> int:
        return len(self.data) if self.data is not None else self.size_cache

    def read(self, n: int) -> bytes:
        if self.data is not None:
            chunk = bytes(self.data[self.pos:self.pos + n])
        else:
            self.fh.seek(self.pos)
            chunk = self.fh.read(n)
        self.pos += len(chunk)
        return chunk

    def write(self, b: bytes) -> int:
        if self.data is None:
            raise PermissionError(self.path)
        end = self.pos + len(b)
        if end > len(self.data):
            self.data.extend(bytes(end - len(self.data)))
        self.data[self.pos:end] = b
        self.pos = end
        return len(b)


@dataclass
class FileSystem:
    game_dir: Path
    hidden: tuple = ()     # game-directory file names that are absent from the guest view
    overlay: dict[str, bytearray] = field(default_factory=dict)       # upper path -> data
    overlay_names: dict[str, str] = field(default_factory=dict)       # upper path -> display path
    overlay_dirs: set[str] = field(default_factory=set)
    deleted: set[str] = field(default_factory=set)
    opened: list[str] = field(default_factory=list)

    def __post_init__(self):
        self.host_files: dict[str, Path] = {}
        self.host_dirs: set[str] = {INSTALL_DIR.upper()}
        self.dir_names: dict[str, str] = {}
        root = Path(self.game_dir)
        hidden = {h.upper() for h in self.hidden}
        for p in root.rglob("*"):
            rel = p.relative_to(root)
            if rel.parts[0].upper() in hidden:
                continue
            key = (INSTALL_DIR + "\\" + "\\".join(rel.parts)).upper()
            if p.is_dir():
                self.host_dirs.add(key)
                self.dir_names[key] = p.name
            else:
                self.host_files[key] = p
        for d in (USER_DIR, DOCUMENTS_DIR, APPDATA_DIR, LOCALAPPDATA_DIR, TEMP_DIR):
            self.mkdirs(d)

    # -- queries
    def _key(self, path: str) -> str:
        return norm(path).upper()

    def is_dir(self, path: str) -> bool:
        k = self._key(path)
        return k in self.overlay_dirs or k in self.host_dirs or k == "C:\\"

    def is_file(self, path: str) -> bool:
        k = self._key(path)
        if k in self.overlay:
            return True
        return k in self.host_files and k not in self.deleted

    def attributes(self, path: str) -> int | None:
        if self.is_dir(path):
            return FILE_ATTRIBUTE_DIRECTORY
        if self.is_file(path):
            return FILE_ATTRIBUTE_ARCHIVE
        return None

    def parent_exists(self, path: str) -> bool:
        k = self._key(path)
        parent = k.rsplit("\\", 1)[0]
        return self.is_dir(parent) or parent.endswith(":")

    def file_size(self, path: str) -> int:
        k = self._key(path)
        if k in self.overlay:
            return len(self.overlay[k])
        return self.host_files[k].stat().st_size

    # -- mutation (overlay only)
    def mkdirs(self, path: str):
        k = self._key(path)
        parts = k.split("\\")
        for i in range(2, len(parts) + 1):
            d = "\\".join(parts[:i])
            self.overlay_dirs.add(d)
            self.dir_names.setdefault(d, norm(path).split("\\")[i - 1])

    def mkdir(self, path: str) -> int:
        if self.is_dir(path) or self.is_file(path):
            return ERROR_ALREADY_EXISTS
        if not self.parent_exists(path):
            return ERROR_PATH_NOT_FOUND
        self.overlay_dirs.add(self._key(path))
        return 0

    def delete(self, path: str) -> int:
        k = self._key(path)
        if k in self.overlay:
            del self.overlay[k]
            return 0
        if k in self.host_files and k not in self.deleted:
            self.deleted.add(k)
            return 0
        return ERROR_FILE_NOT_FOUND

    # -- open
    def open(self, path: str, write: bool, disposition: int) -> tuple[OpenFile | None, int]:
        """CreateFile semantics. Returns (file, error) where error is a Win32 code
        (0, or ERROR_ALREADY_EXISTS for successful OPEN_ALWAYS/CREATE_ALWAYS on an
        existing file)."""
        k = self._key(path)
        exists = self.is_file(path)
        if self.is_dir(path):
            return None, ERROR_ACCESS_DENIED
        CREATE_NEW, CREATE_ALWAYS, OPEN_EXISTING, OPEN_ALWAYS, TRUNCATE_EXISTING = 1, 2, 3, 4, 5
        if disposition == CREATE_NEW and exists:
            return None, ERROR_FILE_EXISTS
        if disposition in (OPEN_EXISTING, TRUNCATE_EXISTING) and not exists:
            return None, ERROR_FILE_NOT_FOUND if self.parent_exists(path) else ERROR_PATH_NOT_FOUND
        if not exists and not self.parent_exists(path):
            return None, ERROR_PATH_NOT_FOUND
        err = ERROR_ALREADY_EXISTS if exists and disposition in (CREATE_ALWAYS, OPEN_ALWAYS) else 0
        truncate = disposition in (CREATE_ALWAYS, TRUNCATE_EXISTING)
        self.opened.append(norm(path))
        if not write and not truncate and exists and k not in self.overlay:
            host = self.host_files[k]
            f = OpenFile(norm(path), host=host, fh=open(host, "rb"), size_cache=host.stat().st_size)
            return f, err
        if k not in self.overlay:
            src = b""
            if exists and not truncate:
                src = self.host_files[k].read_bytes()
            self.overlay[k] = bytearray(src)
            self.overlay_names[k] = norm(path)
            self.deleted.discard(k)
        elif truncate:
            self.overlay[k] = bytearray()
        return OpenFile(norm(path), data=self.overlay[k], writable=write), err

    # -- directory listing
    def listdir(self, pattern_path: str) -> list[tuple[str, bool, int]]:
        """Entries (name, is_dir, size) matching a FindFirstFile pattern."""
        k = self._key(pattern_path)
        directory, pat = k.rsplit("\\", 1)
        pat_l = pat.lower()
        names: dict[str, tuple[str, bool, int]] = {}
        prefix = directory + "\\"
        for key, p in self.host_files.items():
            if key.startswith(prefix) and "\\" not in key[len(prefix):] and key not in self.deleted:
                names[key] = (p.name, False, p.stat().st_size)
        for key in self.host_dirs | self.overlay_dirs:
            if key.startswith(prefix) and "\\" not in key[len(prefix):]:
                names[key] = (self.dir_names.get(key, key[len(prefix):]), True, 0)
        for key, data in self.overlay.items():
            if key.startswith(prefix) and "\\" not in key[len(prefix):]:
                names[key] = (self.overlay_names[key].rsplit("\\", 1)[1], False, len(data))
        out = [v for v in names.values() if fnmatch.fnmatch(v[0].lower(), pat_l.replace("*.*", "*"))]
        out.sort(key=lambda e: e[0].lower())
        return out
