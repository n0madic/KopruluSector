"""Guest memory layout and deterministic allocators.

All addresses handed to the guest (heap blocks, VirtualAlloc regions, stacks, TEBs,
kernel-object handles) are produced by deterministic host-side allocators, so two runs
with the same inputs see identical pointers.
"""
from __future__ import annotations

import struct

# --- fixed guest layout -------------------------------------------------------------
PAGE = 0x1000
STACK_REGION = 0x05000000          # thread stacks (main thread first)
STACK_REGION_SIZE = 0x03000000
VIRTUAL_REGION = 0x20000000        # VirtualAlloc / file mappings (64 KiB granularity)
VIRTUAL_REGION_SIZE = 0x10000000
HEAP_REGION = 0x30000000           # HeapAlloc/LocalAlloc/GlobalAlloc arena
HEAP_REGION_SIZE = 0x30000000
STUB_REGION = 0x7E000000           # one 16-byte stub per HLE entry point
STUB_REGION_SIZE = 0x00040000
SYS_REGION = 0x7E100000            # GDT, PEB, TEBs, process parameters
SYS_REGION_SIZE = 0x00100000
GDT_ADDR = SYS_REGION
PEB_ADDR = SYS_REGION + 0x1000
TEB_BASE = SYS_REGION + 0x10000    # TEB i at TEB_BASE + i * 0x2000
TEB_STRIDE = 0x2000
MAX_THREADS = 64


def align(n: int, a: int) -> int:
    return (n + a - 1) & ~(a - 1)


class HeapAllocator:
    """First-fit-by-size-class allocator over a fixed guest arena.

    Freed blocks are kept in exact-size free lists (size rounded to 16 bytes) and
    reused LIFO; fresh blocks come from a bump pointer. Block sizes are remembered for
    HeapSize/HeapReAlloc. The policy is simple and fully deterministic.
    """

    def __init__(self, base: int, size: int):
        self.base = base
        self.end = base + size
        self.top = base + 0x10
        self.sizes: dict[int, int] = {}      # address -> requested size
        self.rounded: dict[int, int] = {}    # address -> rounded block size
        self.free: dict[int, list[int]] = {}
        self.live_bytes = 0

    def alloc(self, n: int) -> tuple[int, bool]:
        """Return (address, fresh). Fresh memory is known to be zero."""
        r = align(max(n, 1), 16)
        lst = self.free.get(r)
        if lst:
            p = lst.pop()
            fresh = False
        else:
            p = self.top
            self.top += r + 16  # 16-byte gap catches small overruns in dumps
            if self.top > self.end:
                raise MemoryError("guest heap arena exhausted")
            fresh = True
        self.sizes[p] = n
        self.rounded[p] = r
        self.live_bytes += r
        return p, fresh

    def release(self, p: int) -> bool:
        r = self.rounded.pop(p, None)
        if r is None:
            return False
        del self.sizes[p]
        self.free.setdefault(r, []).append(p)
        self.live_bytes -= r
        return True

    def size_of(self, p: int) -> int | None:
        return self.sizes.get(p)

    def owns(self, p: int) -> bool:
        return p in self.sizes


class RegionAllocator:
    """Page-granular allocator for VirtualAlloc, file mappings and stacks."""

    def __init__(self, base: int, size: int, granularity: int):
        self.base, self.end, self.gran = base, base + size, granularity
        self.top = base
        self.blocks: dict[int, int] = {}
        self.free: dict[int, list[int]] = {}

    def alloc(self, n: int) -> int:
        r = align(max(n, 1), self.gran)
        lst = self.free.get(r)
        if lst:
            p = lst.pop()
        else:
            p = self.top
            self.top += r
            if self.top > self.end:
                raise MemoryError(f"guest region {self.base:#x} exhausted")
        self.blocks[p] = r
        return p

    def release(self, p: int) -> int | None:
        r = self.blocks.pop(p, None)
        if r is not None:
            self.free.setdefault(r, []).append(p)
        return r

    def find(self, addr: int) -> tuple[int, int] | None:
        for p, r in self.blocks.items():
            if p <= addr < p + r:
                return p, r
        return None


def u32(b: bytes, off: int = 0) -> int:
    return struct.unpack_from("<I", b, off)[0]
