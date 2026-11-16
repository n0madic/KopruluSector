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


def upcase_unit(c: int) -> int:
    """A UTF-16 unit in upper case as NTFS's $UpCase table has it, for the scripts of the languages: ASCII, Latin-1,
    Latin Extended-A, Greek and Cyrillic; any other unit as it is (Koprulu's kp_compare_ntfs, kp_session.c)."""
    if ord("a") <= c <= ord("z"):
        return c - 32
    if c < 0xE0:
        return 0x39C if c == 0xB5 else c  # the micro sign: Greek Mu
    if c <= 0xFE:
        return c if c == 0xF7 else c - 32  # but the division sign
    if c == 0xFF:
        return 0x178
    if c == 0x131:
        return ord("I")
    if c == 0x17F:
        return ord("S")
    if 0x100 <= c <= 0x137 or 0x14A <= c <= 0x177:
        return c & ~1  # pairs from an even capital
    if 0x139 <= c <= 0x148 or 0x179 <= c <= 0x17E:
        return c if c & 1 else c - 1  # from an odd one
    if c == 0x3C2:
        return 0x3A3  # final sigma
    if 0x3B1 <= c <= 0x3C1 or 0x3C3 <= c <= 0x3CB:
        return c - 32
    if c == 0x3AC:
        return 0x386
    if 0x3AD <= c <= 0x3AF:
        return c - 37
    if c == 0x3CC:
        return 0x38C
    if c in (0x3CD, 0x3CE):
        return c - 63
    if 0x430 <= c <= 0x44F:
        return c - 32
    if 0x450 <= c <= 0x45F:
        return c - 80
    if c == 0x4CF:
        return 0x4C0
    if 0x460 <= c <= 0x481 or 0x48A <= c <= 0x4BF or 0x4D0 <= c <= 0x52F:
        return c & ~1
    if 0x4C1 <= c <= 0x4CE:
        return c if c & 1 else c - 1
    return c


def ntfs_key(name: str) -> tuple[int, ...]:
    """The order in which NTFS keeps a directory's names, so FindFirstFile's: their UTF-16 units after its upper case
    ('_' after the letters, a character past the Basic Multilingual Plane by its surrogates)."""
    b = name.encode("utf-16-le", "surrogatepass")
    return tuple(upcase_unit(b[k] | b[k + 1] << 8) for k in range(0, len(b), 2))


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
        k = self._key(path)
        self.overlay_dirs.add(k)
        self.dir_names.setdefault(k, norm(path).rsplit("\\", 1)[-1])  # the name as given, as Windows keeps it
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

    def preload(self, path: str, data: bytes):
        """Puts a file into the overlay before the guest runs (its directories are created)."""
        self.mkdirs(norm(path).rsplit("\\", 1)[0])
        k = self._key(path)
        self.overlay[k] = bytearray(data)
        self.overlay_names[k] = norm(path)
        self.deleted.discard(k)

    def snapshot(self, path: str) -> bytes | None:
        """The overlay's current contents of a file the guest wrote, or None."""
        data = self.overlay.get(self._key(path))
        return None if data is None else bytes(data)

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
        """Entries (name, is_dir, size) matching a FindFirstFile pattern. A directory below a drive's root also has
        "." and "..", first, as NTFS gives them: the original's map list makes its "[Up One Level]" from ".."
        (AddFolderEntry 0x45b080)."""
        k = self._key(pattern_path)
        directory, pat = k.rsplit("\\", 1)
        pat_l = pat.lower()
        dots = [(d, True, 0) for d in (".", "..")] if "\\" in directory and self.is_dir(directory) else []
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
        out.sort(key=lambda e: ntfs_key(e[0]))
        return [d for d in dots if fnmatch.fnmatch(d[0], pat_l.replace("*.*", "*"))] + out
