"""PE image loading: map sections, apply base relocations, bind imports.

Only what the StarCraft 1.17 process needs: 32-bit DLL/EXE images without TLS
callbacks. Imports are bound through a resolver callback supplied by the machine,
which returns either an address inside another loaded original image (Storm.dll,
standard.snp, ...) or the address of an HLE stub.
"""
from __future__ import annotations

import logging
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import pefile

log = logging.getLogger(__name__)


@dataclass
class Image:
    """A PE image mapped into guest memory."""

    name: str  # lower-case file name, e.g. "storm.dll"
    path: Path
    base: int
    size: int
    entry: int
    pe: pefile.PE
    exports_by_name: dict[str, int] = field(default_factory=dict)
    exports_by_ordinal: dict[int, int] = field(default_factory=dict)
    is_dll: bool = True

    def export(self, name_or_ordinal: str | int) -> int | None:
        if isinstance(name_or_ordinal, int):
            return self.exports_by_ordinal.get(name_or_ordinal)
        return self.exports_by_name.get(name_or_ordinal)

    def contains(self, addr: int) -> bool:
        return self.base <= addr < self.base + self.size


Resolver = Callable[[str, "str | int"], int]


def load_image(mem_map: Callable[[int, int], None], mem_write: Callable[[int, bytes], None], path: Path,
               base: int | None = None) -> Image:
    """Map ``path`` at ``base`` (default: its preferred base) and apply relocations.

    Imports are not bound here; call :func:`bind_imports` once every image that may
    satisfy them is loaded.
    """
    raw = Path(path).read_bytes()
    pe = pefile.PE(data=raw, fast_load=False)
    preferred = pe.OPTIONAL_HEADER.ImageBase
    base = preferred if base is None else base
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
    image = bytearray(pe.get_memory_mapped_image(ImageBase=preferred))
    image += bytes(size - len(image))
    delta = (base - preferred) & 0xFFFFFFFF
    if delta and hasattr(pe, "DIRECTORY_ENTRY_BASERELOC"):
        for block in pe.DIRECTORY_ENTRY_BASERELOC:
            for entry in block.entries:
                if entry.type == 3:  # IMAGE_REL_BASED_HIGHLOW
                    off = entry.rva
                    value = struct.unpack_from("<I", image, off)[0]
                    struct.pack_into("<I", image, off, (value + delta) & 0xFFFFFFFF)
                elif entry.type != 0:
                    raise ValueError(f"unsupported relocation type {entry.type} in {path}")
    mem_map(base, size)
    mem_write(base, bytes(image))
    img = Image(name=Path(path).name.lower(), path=Path(path), base=base, size=size,
                entry=base + pe.OPTIONAL_HEADER.AddressOfEntryPoint if pe.OPTIONAL_HEADER.AddressOfEntryPoint else 0,
                pe=pe, is_dll=bool(pe.FILE_HEADER.Characteristics & 0x2000))
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if sym.forwarder:
                continue
            addr = base + sym.address
            img.exports_by_ordinal[sym.ordinal] = addr
            if sym.name:
                img.exports_by_name[sym.name.decode()] = addr
    log.info("mapped %s at %#x (size %#x, %d exports)", img.name, base, size, len(img.exports_by_ordinal))
    return img


def bind_imports(img: Image, mem_write: Callable[[int, bytes], None], resolve: Resolver) -> list[tuple[str, str | int]]:
    """Write resolved addresses into the image's import address table."""
    bound = []
    if not hasattr(img.pe, "DIRECTORY_ENTRY_IMPORT"):
        return bound
    preferred = img.pe.OPTIONAL_HEADER.ImageBase
    for entry in img.pe.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode().lower()
        for imp in entry.imports:
            key: str | int = imp.name.decode() if imp.name else imp.ordinal
            target = resolve(dll, key)
            iat = img.base + (imp.address - preferred)
            mem_write(iat, struct.pack("<I", target))
            bound.append((dll, key))
    return bound
