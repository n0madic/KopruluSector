#!/usr/bin/env python3
"""Compare the original smackw32.dll Smacker decoder with FFmpeg, frame by frame.

The repack's smackw32.dll (used by Storm SVid for portraits, glue animations and
cinematics) is mapped at its preferred base in Unicorn. Its Windows imports are served by
small host implementations (file reads from a host byte buffer, heap memory, CRT start-up
queries); the decoder itself runs as original x86 code. For each selected SMK, the tool
opens it with SmackOpen (no audio tracks), decodes every frame into a guest buffer with
SmackToBuffer/SmackDoFrame/SmackNextFrame, converts the 8-bit pixels through the movie's
current palette and compares them with FFmpeg's rgb24 decode of the same file.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import pefile
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, Uc, UcError
from unicorn.x86_const import (UC_X86_REG_DS, UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ES, UC_X86_REG_ESP,
                               UC_X86_REG_GDTR, UC_X86_REG_SS)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inspect_resource_manifest import ARCHIVES, Storm  # noqa: E402

SMACK_HASH = None  # recorded in the report
STUB = 0x60000000
HEAP = 0x20000000
HEAP_SIZE = 0x08000000
STACK = 0x70000000
STOP = 0x7F000000
GDT = 0x7E000000
FILE_HANDLE = 0x1234


class Host:
    def __init__(self, dll_path):
        self.raw = Path(dll_path).read_bytes()
        self.pe = pefile.PE(data=self.raw)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        size = (self.pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
        self.u.mem_map(self.base, size)
        self.u.mem_write(self.base, self.pe.get_memory_mapped_image())
        for addr, length in ((STUB, 0x10000), (HEAP, HEAP_SIZE), (STACK - 0x100000, 0x100000), (STOP, 0x1000), (GDT, 0x1000)):
            self.u.mem_map(addr, length)
        # Flat 32-bit protected-mode data segment, so that the decoder's `mov es, ds` copies a
        # valid 32-bit selector (without a GDT Unicorn falls back to 16-bit stack addressing).
        def descriptor(access):
            limit, base = 0xFFFFF, 0
            return struct.pack("<HHBBBB", limit & 0xFFFF, base & 0xFFFF, (base >> 16) & 0xFF, access,
                               0xC0 | ((limit >> 16) & 0xF), (base >> 24) & 0xFF)
        self.u.mem_write(GDT, bytes(8) + descriptor(0x9A) + descriptor(0x92))
        self.u.reg_write(UC_X86_REG_GDTR, (0, GDT, 3 * 8 - 1, 0))
        for reg in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            self.u.reg_write(reg, 0x10)
        self.heap_top = HEAP + 0x1000
        self.stubs = {}
        self.file = b""
        self.pos = 0
        self.missing = set()
        slot = 0
        for imp in self.pe.DIRECTORY_ENTRY_IMPORT:
            for i in imp.imports:
                name = (i.name or b"").decode()
                stub = STUB + slot * 16
                slot += 1
                self.stubs[stub] = name
                self.u.mem_write(stub, b"\xc3")
                self.u.mem_write(i.address, struct.pack("<I", stub))
        self.exports = {(s.name or b"").decode(): self.base + s.address for s in self.pe.DIRECTORY_ENTRY_EXPORT.symbols}
        self.u.hook_add(UC_HOOK_CODE, self._hook, begin=STUB, end=STUB + 0x10000)
        self.strings = {}

    # --- guest memory helpers
    def alloc(self, n, zero=True):
        n = (n + 15) & ~15
        p = self.heap_top
        self.heap_top += n
        if self.heap_top > HEAP + HEAP_SIZE:
            raise MemoryError("guest heap exhausted")
        if zero:
            self.u.mem_write(p, bytes(n))
        return p

    def cstr(self, s):
        if s not in self.strings:
            p = self.alloc(len(s) + 1)
            self.u.mem_write(p, s + b"\0")
            self.strings[s] = p
        return self.strings[s]

    def arg(self, i):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        return struct.unpack("<I", self.u.mem_read(esp + 4 + 4 * i, 4))[0]

    def ret(self, value, nargs):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        retaddr = struct.unpack("<I", self.u.mem_read(esp, 4))[0]
        self.u.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + 4 * nargs)
        self.u.reg_write(UC_X86_REG_EIP, retaddr)

    def _hook(self, uc, address, size, _):
        name = self.stubs.get(address)
        if name is None:
            return
        a = self.arg
        if name == "HeapCreate":
            return self.ret(0x5000, 3)
        if name == "HeapAlloc":
            return self.ret(self.alloc(a(2), zero=bool(a(1) & 8)), 3)
        if name == "HeapFree":
            return self.ret(1, 3)
        if name == "HeapDestroy":
            return self.ret(1, 1)
        if name == "GetVersion":
            return self.ret(0x0A280105, 0)
        if name in ("GetCommandLineA", "GetEnvironmentStrings"):
            return self.ret(self.cstr(b"smk"), 0)
        if name == "GetEnvironmentStringsW":
            return self.ret(0, 0)
        if name in ("FreeEnvironmentStringsA", "FreeEnvironmentStringsW"):
            return self.ret(1, 1)
        if name == "GetStartupInfoA":
            self.u.mem_write(a(0), struct.pack("<I", 68) + bytes(64))
            return self.ret(0, 1)
        if name == "GetStdHandle":
            return self.ret(0xFFFFFFFF, 1)
        if name == "GetFileType":
            return self.ret(0, 1)
        if name == "SetHandleCount":
            return self.ret(a(0), 1)
        if name == "GetModuleFileNameA":
            self.u.mem_write(a(1), b"C:\\smackw32.dll\0")
            return self.ret(15, 3)
        if name == "GetModuleHandleA":
            return self.ret(self.base, 1)
        if name == "GetACP" or name == "GetOEMCP":
            return self.ret(1252, 0)
        if name == "GetCPInfo":
            self.u.mem_write(a(1), struct.pack("<I", 1) + b"?\0" + bytes(12))
            return self.ret(1, 2)
        if name in ("GetStringTypeA", "GetStringTypeW", "LCMapStringA", "LCMapStringW",
                    "MultiByteToWideChar", "WideCharToMultiByte"):
            self.missing.add(name)
            n = {"GetStringTypeA": 5, "GetStringTypeW": 4, "LCMapStringA": 6, "LCMapStringW": 6,
                 "MultiByteToWideChar": 6, "WideCharToMultiByte": 8}[name]
            return self.ret(0, n)
        if name == "DisableThreadLibraryCalls":
            return self.ret(1, 1)
        if name == "GetCurrentProcessId":
            return self.ret(1, 0)
        if name == "GetCurrentProcess":
            return self.ret(0xFFFFFFFF, 0)
        if name == "GetProfileIntA":
            return self.ret(a(2), 3)
        if name == "SetErrorMode":
            return self.ret(0, 1)
        if name == "timeGetTime":
            return self.ret(0, 0)
        if name == "CreateFileA":
            self.pos = 0
            return self.ret(FILE_HANDLE, 7)
        if name == "ReadFile":
            n = a(2)
            chunk = self.file[self.pos:self.pos + n]
            self.pos += len(chunk)
            self.u.mem_write(a(1), chunk)
            if a(3):
                self.u.mem_write(a(3), struct.pack("<I", len(chunk)))
            return self.ret(1, 5)
        if name == "SetFilePointer":
            dist, method = struct.unpack("<i", struct.pack("<I", a(1)))[0], a(3)
            base = {0: 0, 1: self.pos, 2: len(self.file)}[method]
            self.pos = base + dist
            return self.ret(self.pos, 4)
        if name == "CloseHandle":
            return self.ret(1, 1)
        if name == "GetSystemMetrics":
            return self.ret(640 if a(0) == 0 else 480, 1)
        if name == "GetDC":
            return self.ret(0x1000, 1)
        if name == "ReleaseDC":
            return self.ret(1, 2)
        if name == "GetDeviceCaps":
            return self.ret(8, 2)
        if name == "GetProcAddress":
            self.missing.add(name)
            return self.ret(0, 2)
        if name == "LoadLibraryA":
            self.missing.add(name)
            return self.ret(0, 1)
        if name == "FreeLibrary":
            return self.ret(1, 1)
        if name == "OutputDebugStringA":
            return self.ret(0, 1)
        raise RuntimeError(f"unimplemented import {name}")

    def call(self, addr, *args):
        sp = STACK - 0x100
        frame = struct.pack("<" + "I" * (len(args) + 1), STOP, *args)
        self.u.mem_write(sp, frame)
        self.u.reg_write(UC_X86_REG_ESP, sp)
        self.u.emu_start(addr, STOP, count=500_000_000)
        return self.u.reg_read(UC_X86_REG_EAX)


def ffmpeg_frames(path, w, h):
    p = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                       capture_output=True, check=True)
    size = w * h * 3
    return [p.stdout[i:i + size] for i in range(0, len(p.stdout), size)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", required=True, type=Path, help="extracted game directory with smackw32.dll and MPQs")
    ap.add_argument("--corpus", required=True, type=Path, help="smk-corpus.json")
    ap.add_argument("--stormlib", default="/opt/homebrew/lib/libstorm.dylib")
    ap.add_argument("--limit", type=int, default=0, help="0 = all files")
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    dll = a.app / "smackw32.dll"
    st = Storm(a.stormlib)
    arcs = [st.open(a.app / n) for n in ARCHIVES if (a.app / n).exists()]
    corpus = json.loads(a.corpus.read_text())["files"]
    if a.limit:
        corpus = corpus[:a.limit]
    host = Host(dll)
    host.call(host.base + host.pe.OPTIONAL_HEADER.AddressOfEntryPoint, host.base, 1, 0)
    rows = []
    for item in corpus:
        data = None
        for h in arcs:
            data = st.read(h, item["name"])
            if data:
                break
        host.file = data
        host.heap_top = HEAP + 0x200000 if host.heap_top < HEAP + 0x200000 else host.heap_top
        name_ptr = host.cstr(b"C:\\movie.smk")
        smk = host.call(host.exports["_SmackOpen@12"], name_ptr, 0, 0xFFFFFFFF)
        if not smk:
            rows.append({"name": item["name"], "error": "SmackOpen failed"})
            continue
        w, h, frames = struct.unpack("<III", host.u.mem_read(smk + 4, 12))
        buf = host.alloc(w * h)
        host.call(host.exports["_SmackToBuffer@28"], smk, 0, 0, w, h, buf, 0)
        ring = bool(struct.unpack("<I", host.u.mem_read(smk + 0x14, 4))[0] & 1)
        total = frames + (1 if ring else 0)
        native = []
        for f in range(total):
            host.call(host.exports["_SmackDoFrame@4"], smk)
            pal = bytes(host.u.mem_read(smk + 0x6C, 768))
            pix = bytes(host.u.mem_read(buf, w * h))
            native.append(b"".join(pal[3 * p:3 * p + 3] for p in pix))
            if f + 1 < total:
                host.call(host.exports["_SmackNextFrame@4"], smk)
        host.call(host.exports["_SmackClose@4"], smk)
        with tempfile.NamedTemporaryFile(suffix=".smk") as t:
            t.write(data)
            t.flush()
            ref = ffmpeg_frames(t.name, w, h)
        equal = sum(1 for x, y in zip(native, ref) if x == y)
        diffpix = 0
        for x, y in zip(native, ref):
            if x != y:
                diffpix += sum(1 for k in range(0, len(x), 3) if x[k:k + 3] != y[k:k + 3])
        rows.append({"name": item["name"], "width": w, "height": h, "frames": frames, "ring_frame": ring, "native_frames": len(native), "ffmpeg_frames": len(ref),
                     "identical_frames": equal, "differing_pixels": diffpix,
                     "native_sha256": hashlib.sha256(b"".join(native)).hexdigest()})
        print(item["name"], frames, equal, diffpix, flush=True)
    summary = {"files": len(rows), "all_frames_identical": sum(1 for r in rows if r.get("identical_frames") == r.get("native_frames") == r.get("ffmpeg_frames")),
               "native_frames": sum(r.get("native_frames", 0) for r in rows), "differing_pixels": sum(r.get("differing_pixels", 0) for r in rows),
               "smackw32_sha256": hashlib.sha256(dll.read_bytes()).hexdigest(),
               "stubbed_unimplemented_locale_calls": sorted(host.missing)}
    a.out.write_text(json.dumps({"summary": summary, "files": rows}, indent=1))
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
