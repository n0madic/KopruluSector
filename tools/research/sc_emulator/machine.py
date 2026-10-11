"""The emulated Win32 process: CPU, memory, images, threads and the HLE boundary.

Design summary (see docs/research/full-game-emulation.md for the full description):

* One Unicorn x86-32 CPU in protected mode with a flat GDT. CS/DS/ES/SS use flat 4 GiB
  descriptors; every thread gets its own FS descriptor whose base is that thread's TEB.
* Original images (StarCraft.exe, storm.dll, standard.snp, local.dll, smackw32.dll) are
  mapped at their preferred bases with relocations applied. Imports from Windows DLLs
  are bound to 16-byte stubs in ``STUB_REGION``; a code hook restricted to that range
  dispatches to the Python HLE implementation and returns to the caller.
* HLE functions are plain Python functions (return the EAX value) or generators. A
  generator may ``yield Call(...)`` to run guest code (window procedures, DllMain,
  thread-less callbacks) and receive its EAX, or ``yield Wait(...)`` to block the
  calling guest thread. Guest calls are implemented by continuation (the guest returns
  to a CALLBACK_RET stub), never by nested ``emu_start``.
* Threads are cooperative and deterministic: a thread runs until it blocks in an HLE
  wait (WaitForSingleObject, Sleep, GetMessage, a contended critical section, ...);
  then the next ready thread in creation order runs. The virtual clock only advances
  when explicitly charged (Sleep, waits that time out, idle message polling).
"""
from __future__ import annotations

import inspect
import logging
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from unicorn import (UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_INVALID, UC_MODE_32, UC_PROT_ALL, Uc,
                     UcError)
from unicorn import x86_const as X

from . import memory as M
from .pe import Image, bind_imports, load_image

log = logging.getLogger(__name__)

# special stubs (offsets inside STUB_REGION)
STUB_CALLBACK_RET = M.STUB_REGION + 0x0
STUB_THREAD_EXIT = M.STUB_REGION + 0x10
STUB_HOST_RETURN = M.STUB_REGION + 0x20
STUB_FIRST_API = M.STUB_REGION + 0x100
STUB_STRIDE = 0x10

WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 0x102
INFINITE = 0xFFFFFFFF


class EmulationStop(Exception):
    """Raised by HLE code (or hooks) to end the run, e.g. ExitProcess."""


class GuestFault(Exception):
    pass


class UnimplementedApi(Exception):
    pass


@dataclass
class Call:
    """Request from an HLE generator: call guest code ``addr`` with stack ``args``."""
    addr: int
    args: tuple = ()
    regs: dict = field(default_factory=dict)
    stack: int | None = None  # highest address the callback frame may use (default: below the API frame)


@dataclass
class Wait:
    """Request from an HLE generator: block until a handle is signalled or a timeout.

    ``handles`` empty means a plain sleep. ``wait_all`` mirrors WaitForMultipleObjects.
    ``predicate`` (optional) is an extra host condition (e.g. message queue not empty);
    the result is then WAIT_OBJECT_0 + len(handles).
    """
    handles: tuple = ()
    timeout_ms: int = INFINITE
    wait_all: bool = False
    predicate: Callable[[], bool] | None = None


@dataclass
class ApiSpec:
    dll: str
    name: str
    fn: Callable
    nargs: int
    cdecl: bool = False
    doc: str = ""


@dataclass
class Frame:
    gen: Any
    spec: ApiSpec | None
    esp: int
    ret: int
    on_finish: Callable[[Any], None] | None = None  # replaces the stdcall return (SEH dispatch)


class Waitable:
    """Kernel object that a thread can wait on."""

    signaled = False

    def consume(self, thread: "Thread") -> None:
        """Called when a wait on this object is satisfied."""


class Event(Waitable):
    def __init__(self, manual: bool, signaled: bool, name: str | None = None):
        self.manual, self.signaled, self.name = manual, signaled, name

    def consume(self, thread):
        if not self.manual:
            self.signaled = False


class Mutex(Waitable):
    def __init__(self, owner=None, name=None):
        self.owner, self.count, self.name = owner, (1 if owner else 0), name

    @property
    def signaled(self):  # type: ignore[override]
        return self.owner is None

    def is_signaled_for(self, thread):
        return self.owner is None or self.owner is thread

    def consume(self, thread):
        self.owner = thread
        self.count += 1


class Semaphore(Waitable):
    def __init__(self, count, maximum, name=None):
        self.count, self.maximum, self.name = count, maximum, name

    @property
    def signaled(self):  # type: ignore[override]
        return self.count > 0

    def consume(self, thread):
        self.count -= 1


@dataclass(eq=False)
class Thread(Waitable):
    tid: int
    handle: int
    teb: int
    fs_selector: int
    stack_base: int
    stack_top: int
    start: int = 0
    param: int = 0
    state: str = "ready"       # ready | blocked | suspended | done | host
    ctx: Any = None
    wait: Wait | None = None
    wait_deadline: int | None = None   # virtual microseconds
    frames: list = field(default_factory=list)
    exit_code: int = 0x103
    suspend_count: int = 0
    name: str = ""
    pending_resume: Any = None  # value delivered to the top frame on resume
    messages: list = field(default_factory=list)

    @property
    def signaled(self):  # type: ignore[override]
        return self.state == "done"


class Machine:
    def __init__(self, game_dir: Path, exe_path: Path, *, log_api: bool = False):
        self.game_dir = Path(game_dir)
        self.exe_path = Path(exe_path)
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.log_api = log_api
        self.images: dict[str, Image] = {}
        self.exe: Image | None = None
        self.heap = M.HeapAllocator(M.HEAP_REGION, M.HEAP_REGION_SIZE)
        self.vregion = M.RegionAllocator(M.VIRTUAL_REGION, M.VIRTUAL_REGION_SIZE, 0x10000)
        self.stacks = M.RegionAllocator(M.STACK_REGION, M.STACK_REGION_SIZE, 0x10000)
        self.stub_specs: dict[int, ApiSpec] = {}
        self.stub_by_key: dict[tuple[str, str | int], int] = {}
        self.next_stub = STUB_FIRST_API
        self.handles: dict[int, Any] = {}
        self.next_handle = 0x100
        self.threads: list[Thread] = []
        self.current: Thread | None = None
        self.clock_us = 0
        self.halted = False
        self.exit_code: int | None = None
        self.need_switch = False
        self.host_result: int | None = None
        self.host_waiting = False
        self.fault: dict | None = None
        self.api_counts: dict[str, int] = {}
        self.unimplemented: set[str] = set()
        self.on_api: list[Callable[[ApiSpec, tuple], None]] = []
        self.services: dict[str, Any] = {}  # HLE subsystems (filesystem, windows, ddraw, ...)
        self._map_system()
        self._hooks()

    # ------------------------------------------------------------------ memory
    def _map_system(self):
        uc = self.uc
        for base, size in ((M.STACK_REGION, M.STACK_REGION_SIZE), (M.VIRTUAL_REGION, M.VIRTUAL_REGION_SIZE),
                           (M.HEAP_REGION, M.HEAP_REGION_SIZE), (M.STUB_REGION, M.STUB_REGION_SIZE),
                           (M.SYS_REGION, M.SYS_REGION_SIZE)):
            uc.mem_map(base, size, UC_PROT_ALL)
        # every stub is "jmp $" so that an accidental execution is harmless
        uc.mem_write(M.STUB_REGION, b"\xeb\xfe" * (M.STUB_REGION_SIZE // 2))
        # GDT: 0 null, 1 code, 2 data, 3.. per-thread FS
        gdt = bytearray(8 * (3 + M.MAX_THREADS))
        gdt[8:16] = self._descriptor(0, 0xFFFFF, 0x9B)
        gdt[16:24] = self._descriptor(0, 0xFFFFF, 0x93)
        uc.mem_write(M.GDT_ADDR, bytes(gdt))
        uc.reg_write(X.UC_X86_REG_GDTR, (0, M.GDT_ADDR, len(gdt) - 1, 0))
        for reg in (X.UC_X86_REG_DS, X.UC_X86_REG_ES, X.UC_X86_REG_SS, X.UC_X86_REG_GS):
            uc.reg_write(reg, 0x10)

    @staticmethod
    def _descriptor(base, limit, access):
        return struct.pack("<HHBBBB", limit & 0xFFFF, base & 0xFFFF, (base >> 16) & 0xFF, access,
                           0xC0 | ((limit >> 16) & 0xF), (base >> 24) & 0xFF)

    def r8(self, a):
        return self.uc.mem_read(a, 1)[0]

    def r16(self, a):
        return struct.unpack("<H", self.uc.mem_read(a, 2))[0]

    def r32(self, a):
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]

    def rs32(self, a):
        return struct.unpack("<i", self.uc.mem_read(a, 4))[0]

    def w8(self, a, v):
        self.uc.mem_write(a, bytes([v & 0xFF]))

    def w16(self, a, v):
        self.uc.mem_write(a, struct.pack("<H", v & 0xFFFF))

    def w32(self, a, v):
        self.uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def read(self, a, n) -> bytes:
        return bytes(self.uc.mem_read(a, n))

    def write(self, a, data: bytes):
        if data:
            self.uc.mem_write(a, bytes(data))

    def cstr(self, a, limit=0x10000) -> bytes:
        if not a:
            return b""
        out = bytearray()
        while len(out) < limit:
            chunk = self.uc.mem_read(a + len(out), 64)
            i = chunk.find(b"\0")
            if i >= 0:
                out += chunk[:i]
                return bytes(out)
            out += chunk
        return bytes(out)

    def wstr(self, a, limit=0x8000) -> str:
        if not a:
            return ""
        out = bytearray()
        while len(out) < limit * 2:
            chunk = self.uc.mem_read(a + len(out), 64)
            for i in range(0, 64, 2):
                if chunk[i] == 0 and chunk[i + 1] == 0:
                    out += chunk[:i]
                    return out.decode("utf-16-le", "replace")
            out += chunk
        return out.decode("utf-16-le", "replace")

    def alloc(self, n, zero=True) -> int:
        p, fresh = self.heap.alloc(n)
        if zero and not fresh:
            self.uc.mem_write(p, bytes(M.align(n, 16)))
        return p

    def alloc_str(self, s: bytes) -> int:
        p = self.alloc(len(s) + 1)
        self.write(p, s + b"\0")
        return p

    # ------------------------------------------------------------------ registers
    def reg(self, r):
        return self.uc.reg_read(r)

    def set_reg(self, r, v):
        self.uc.reg_write(r, v & 0xFFFFFFFF)

    @property
    def eip(self):
        return self.uc.reg_read(X.UC_X86_REG_EIP)

    @property
    def esp(self):
        return self.uc.reg_read(X.UC_X86_REG_ESP)

    # ------------------------------------------------------------------ handles
    def new_handle(self, obj) -> int:
        h = self.next_handle
        self.next_handle += 4
        self.handles[h] = obj
        return h

    def obj(self, h):
        return self.handles.get(h)

    # ------------------------------------------------------------------ images
    def load_image(self, path: Path, base: int | None = None) -> Image:
        img = load_image(lambda b, s: self.uc.mem_map(b, s, UC_PROT_ALL), self.uc.mem_write, path, base)
        self.images[img.name] = img
        return img

    def find_image(self, name: str) -> Image | None:
        n = name.lower().replace("/", "\\").rsplit("\\", 1)[-1]
        if n in self.images:
            return self.images[n]
        if "." not in n:
            return self.images.get(n + ".dll")
        return None

    def image_at(self, addr) -> Image | None:
        for img in self.images.values():
            if img.contains(addr):
                return img
        return None

    def bind(self, img: Image):
        bind_imports(img, self.uc.mem_write, self.resolve_import)

    def resolve_import(self, dll: str, key: str | int) -> int:
        dll = dll.lower()
        img = self.find_image(dll)
        if img is not None:
            addr = img.export(key)
            if addr is None:
                raise KeyError(f"{dll} has no export {key}")
            return addr
        return self.stub_for(dll, key)

    def stub_for(self, dll: str, key: str | int) -> int:
        from .hle import REGISTRY  # late import: hle modules register on import
        dll = dll.lower()
        if not dll.endswith((".dll", ".drv")):
            dll += ".dll"
        k = (dll, key)
        if k in self.stub_by_key:
            return self.stub_by_key[k]
        spec = REGISTRY.get(k)
        if spec is None:
            spec = ApiSpec(dll, str(key), None, 0)
        addr = self.next_stub
        self.next_stub += STUB_STRIDE
        self.stub_specs[addr] = spec
        self.stub_by_key[k] = addr
        return addr

    # ------------------------------------------------------------------ hooks
    def _hooks(self):
        uc = self.uc
        uc.hook_add(UC_HOOK_CODE, self._on_stub, begin=M.STUB_REGION, end=M.STUB_REGION + M.STUB_REGION_SIZE - 1)
        uc.hook_add(UC_HOOK_MEM_INVALID, self._on_invalid)
        uc.hook_add(UC_HOOK_INTR, self._on_intr)

    def _on_invalid(self, uc, access, address, size, value, _):
        self.fault = {"kind": "memory", "access": access, "address": address, "size": size,
                      "eip": uc.reg_read(X.UC_X86_REG_EIP)}
        return False

    def _on_intr(self, uc, intno, _):
        self.fault = {"kind": "interrupt", "intno": intno, "eip": uc.reg_read(X.UC_X86_REG_EIP)}
        uc.emu_stop()

    def _on_stub(self, uc, address, size, _):
        try:
            if address == STUB_CALLBACK_RET:
                frame = self.current.frames[-1]
                self._advance(frame, uc.reg_read(X.UC_X86_REG_EAX))
                return
            if address == STUB_THREAD_EXIT:
                self.exit_thread(self.current, uc.reg_read(X.UC_X86_REG_EAX))
                return
            if address == STUB_HOST_RETURN:
                self.host_result = uc.reg_read(X.UC_X86_REG_EAX)
                self.current.state = "host"
                self.need_switch = True
                uc.emu_stop()
                return
            spec = self.stub_specs.get(address)
            if spec is None:
                raise GuestFault(f"execution in stub region at {address:#x}")
            self._enter_api(spec)
        except BaseException as e:  # never let an exception escape into Unicorn
            self.pending_exception = e
            uc.emu_stop()

    pending_exception: BaseException | None = None

    def _enter_api(self, spec: ApiSpec):
        esp = self.esp
        ret = self.r32(esp)
        key = f"{spec.dll}!{spec.name}"
        self.api_counts[key] = self.api_counts.get(key, 0) + 1
        if spec.fn is None:
            self.unimplemented.add(key)
            raise UnimplementedApi(f"{key} called from {ret:#x}")
        n = spec.nargs if spec.nargs >= 0 else 0
        args = struct.unpack(f"<{n}I", self.uc.mem_read(esp + 4, 4 * n)) if n else ()
        if self.log_api:
            log.debug("[%x] %s(%s) <- %#x", self.current.tid, key, ", ".join(self._fmt_arg(a) for a in args), ret)
        for cb in self.on_api:
            cb(spec, args)
        result = spec.fn(self, *args)
        if inspect.isgenerator(result):
            frame = Frame(result, spec, esp, ret)
            self.current.frames.append(frame)
            self._advance(frame, None)
        else:
            self._api_return(spec, esp, ret, result)

    def _fmt_arg(self, a):
        if a >= 0x10000:
            try:
                b = bytes(self.uc.mem_read(a, 48))
            except UcError:
                return f"{a:#x}"
            z = b.find(b"\0")
            if z >= 3 and all(32 <= c < 127 for c in b[:z]):
                return f"{a:#x}{b[:z]!r}"
            if len(b) > 6 and b[1] == 0 and b[3] == 0 and 32 <= b[0] < 127 and 32 <= b[2] < 127:
                try:
                    return f"{a:#x}L{self.wstr(a, 64)!r}"
                except Exception:
                    pass
        return f"{a:#x}"

    def _api_return(self, spec, esp, ret, result):
        uc = self.uc
        if isinstance(result, tuple):
            uc.reg_write(X.UC_X86_REG_EAX, result[0] & 0xFFFFFFFF)
            uc.reg_write(X.UC_X86_REG_EDX, result[1] & 0xFFFFFFFF)
        else:
            uc.reg_write(X.UC_X86_REG_EAX, (result or 0) & 0xFFFFFFFF)
        pop = 4 if spec.cdecl or spec.nargs < 0 else 4 + 4 * spec.nargs
        uc.reg_write(X.UC_X86_REG_ESP, esp + pop)
        uc.reg_write(X.UC_X86_REG_EIP, ret)

    def _advance(self, frame: Frame, value):
        """Resume an HLE generator with ``value`` and act on what it yields next."""
        try:
            req = frame.gen.send(value)
        except StopIteration as stop:
            if frame in self.current.frames:
                self.current.frames.remove(frame)
            if frame.on_finish is not None:
                frame.on_finish(stop.value)
            else:
                self._api_return(frame.spec, frame.esp, frame.ret, stop.value)
            return
        if isinstance(req, Call):
            top = req.stack if req.stack is not None else frame.esp - 0x40
            sp = top - 4 * (len(req.args) + 1)
            # keep the callee's view of the stack 16-byte aligned like Windows does
            sp &= ~0xF
            self.write(sp, struct.pack(f"<{len(req.args) + 1}I", STUB_CALLBACK_RET, *[a & 0xFFFFFFFF for a in req.args]))
            for r, v in req.regs.items():
                self.uc.reg_write(r, v & 0xFFFFFFFF)
            self.uc.reg_write(X.UC_X86_REG_ESP, sp)
            self.uc.reg_write(X.UC_X86_REG_EIP, req.addr)
        elif isinstance(req, Wait):
            t = self.current
            if self._wait_ready(t, req):
                self._advance(frame, self._wait_complete(t, req))
                return
            if req.timeout_ms == 0:
                if req.handles or req.predicate is not None:
                    self._advance(frame, WAIT_TIMEOUT)
                    return
                # Sleep(0): give every other ready thread a turn first
                t.pending_resume = WAIT_TIMEOUT
                self.need_switch = True
                self.uc.emu_stop()
                return
            t.state = "blocked"
            t.wait = req
            t.wait_deadline = None if req.timeout_ms == INFINITE else self.clock_us + req.timeout_ms * 1000
            self.need_switch = True
            self.uc.emu_stop()
        else:
            raise TypeError(f"HLE generator yielded {req!r}")

    # ------------------------------------------------------------------ waits
    def _signaled_for(self, obj, t):
        if isinstance(obj, Mutex):
            return obj.is_signaled_for(t)
        return obj is not None and obj.signaled

    def _wait_ready(self, t: Thread, w: Wait) -> bool:
        objs = [self.handles.get(h) for h in w.handles]
        if w.handles:
            if w.wait_all:
                if all(self._signaled_for(o, t) for o in objs):
                    return True
            elif any(self._signaled_for(o, t) for o in objs):
                return True
        return bool(w.predicate and w.predicate())

    def _wait_complete(self, t: Thread, w: Wait) -> int:
        objs = [self.handles.get(h) for h in w.handles]
        if w.handles and w.wait_all and all(self._signaled_for(o, t) for o in objs):
            for o in objs:
                o.consume(t)
            return WAIT_OBJECT_0
        for i, o in enumerate(objs):
            if not w.wait_all and self._signaled_for(o, t):
                o.consume(t)
                return WAIT_OBJECT_0 + i
        return WAIT_OBJECT_0 + len(w.handles)

    # ------------------------------------------------------------------ threads
    def create_thread(self, start: int, param: int, stack_size: int = 0x40000, suspended=False, name="") -> Thread:
        idx = len(self.threads)
        if idx >= M.MAX_THREADS:
            raise RuntimeError("too many guest threads")
        stack_size = max(M.align(stack_size or 0x40000, 0x10000), 0x40000)
        base = self.stacks.alloc(stack_size + 0x10000) + 0x10000  # 64 KiB guard below
        top = base + stack_size
        teb = M.TEB_BASE + idx * M.TEB_STRIDE
        sel = (3 + idx) * 8
        self.uc.mem_write(M.GDT_ADDR + sel, self._descriptor(teb, 0xFFF, 0x93))
        tid = 0x1000 + 4 * idx
        t = Thread(tid=tid, handle=0, teb=teb, fs_selector=sel, stack_base=base, stack_top=top, start=start,
                   param=param, state="suspended" if suspended else "ready", name=name)
        t.suspend_count = 1 if suspended else 0
        t.handle = self.new_handle(t)
        self._init_teb(t)
        self.threads.append(t)
        if start:
            sp = top - 0x10
            self.write(sp, struct.pack("<II", STUB_THREAD_EXIT, param))
            t.ctx = self._fresh_context(sp, start, sel)
        return t

    def _fresh_context(self, esp, eip, fs_sel):
        uc = self.uc
        saved = uc.context_save()
        for r in (X.UC_X86_REG_EAX, X.UC_X86_REG_EBX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX, X.UC_X86_REG_ESI,
                  X.UC_X86_REG_EDI, X.UC_X86_REG_EBP):
            uc.reg_write(r, 0)
        uc.reg_write(X.UC_X86_REG_EFLAGS, 0x202)
        # Windows starts every thread with x87 control word 0x27F (all exceptions masked, 53-bit
        # precision) and MXCSR 0x1F80; Unicorn's reset value 0 would unmask every FP exception.
        uc.reg_write(X.UC_X86_REG_FPCW, 0x27F)
        uc.reg_write(X.UC_X86_REG_MXCSR, 0x1F80)
        uc.reg_write(X.UC_X86_REG_FS, fs_sel)
        uc.reg_write(X.UC_X86_REG_ESP, esp)
        uc.reg_write(X.UC_X86_REG_EIP, eip)
        ctx = uc.context_save()
        uc.context_restore(saved)
        return ctx

    def _init_teb(self, t: Thread):
        teb = t.teb
        self.write(teb, bytes(M.TEB_STRIDE))
        self.w32(teb + 0x00, 0xFFFFFFFF)        # SEH chain end
        self.w32(teb + 0x04, t.stack_top)       # StackBase
        self.w32(teb + 0x08, t.stack_base)      # StackLimit
        self.w32(teb + 0x18, teb)               # Self
        self.w32(teb + 0x20, 0x1000)            # ClientId.UniqueProcess
        self.w32(teb + 0x24, t.tid)             # ClientId.UniqueThread
        self.w32(teb + 0x2C, teb + 0x1000)      # ThreadLocalStoragePointer (no static TLS)
        self.w32(teb + 0x30, M.PEB_ADDR)        # PEB

    def exit_thread(self, t: Thread, code: int):
        t.state = "done"
        t.exit_code = code
        t.frames.clear()
        self.need_switch = True
        self.uc.emu_stop()

    def tls_slot_addr(self, t: Thread, index: int) -> int:
        if index < 64:
            return t.teb + 0xE10 + 4 * index
        return t.teb + 0x1100 + 4 * (index - 64)

    # ------------------------------------------------------------------ scheduler
    def _pick(self) -> Thread | None:
        n = len(self.threads)
        start = self.threads.index(self.current) + 1 if self.current in self.threads else 0
        for k in range(n):
            t = self.threads[(start + k) % n]
            if t.state == "ready":
                return t
            if t.state == "blocked" and self._wait_ready(t, t.wait):
                return t
        return None

    def _advance_clock_to_next_deadline(self) -> bool:
        deadlines = [t.wait_deadline for t in self.threads if t.state == "blocked" and t.wait_deadline is not None]
        hook = self.services.get("idle")
        timer_due = hook.next_due_us() if hook else None
        cands = [d for d in deadlines + ([timer_due] if timer_due is not None else []) if d is not None]
        if not cands:
            return False
        target = min(cands)
        if target > self.clock_us:
            self.clock_us = target
        for t in self.threads:
            if t.state == "blocked" and t.wait_deadline is not None and t.wait_deadline <= self.clock_us:
                if not self._wait_ready(t, t.wait):
                    t.state = "ready"
                    t.pending_resume = WAIT_TIMEOUT
                    t.wait = None
        return True

    def _activate(self, t: Thread):
        """Make ``t`` the running thread and deliver a pending wait result to it."""
        self.uc.context_restore(t.ctx)
        self.current = t
        if t.state == "blocked":
            value = self._wait_complete(t, t.wait)
            t.wait = None
            t.wait_deadline = None
            t.pending_resume = value
        t.state = "ready"
        if t.pending_resume is not None:
            value, t.pending_resume = t.pending_resume, None
            self._advance(t.frames[-1], value)

    stop_requested = False
    slice_us = 2_000_000       # wall-clock slice (only without a native block counter)
    slice_blocks = 4_000_000   # deterministic slice: executed translation blocks
    native = None              # coverage.Coverage: native block counter / coverage maps
    _skip_hook_pc = None
    _last_hook = (None, -1)

    def request_stop(self):
        """Stop the run loop at the current instruction (call from a hook)."""
        self.stop_requested = True
        self.uc.emu_stop()

    def add_hook(self, addr: int, fn: Callable[[], None]):
        """Python code hook at one guest address, safe across stops: when the run loop stopped
        right after the hook fired (the hooked instruction not yet executed), the hook is not
        fired a second time on resume."""
        def hook(uc, address, size, _):
            if self._skip_hook_pc == address:
                self._skip_hook_pc = None
                return
            self._last_hook = (address, self.native.blocks_executed() if self.native else 0)
            try:
                fn()
            except BaseException as e:
                self.pending_exception = e
                uc.emu_stop()
        return self.uc.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)

    def inject_call(self, addr: int, args=(), regs: dict | None = None):
        """From a code hook: make the guest call ``addr`` now and then continue at the hooked
        instruction (args pushed right to left, return address = current EIP)."""
        names = {"eax": X.UC_X86_REG_EAX, "ecx": X.UC_X86_REG_ECX, "edx": X.UC_X86_REG_EDX}
        sp = self.esp - 4 * (len(args) + 1)
        self.write(sp, struct.pack(f"<{len(args) + 1}I", self.eip, *[a & 0xFFFFFFFF for a in args]))
        for r, v in (regs or {}).items():
            self.uc.reg_write(names[r], v & 0xFFFFFFFF)
        self.uc.reg_write(X.UC_X86_REG_ESP, sp)
        self.uc.reg_write(X.UC_X86_REG_EIP, addr)

    def _emu(self):
        """One emulation slice of the current thread."""
        self._last_hook = (None, -1)
        if self.native is not None:
            self.native.set_budget(self.native.blocks_executed() + self.slice_blocks)
            self.uc.emu_start(self.eip, 0xFFFFFFFF)
            self.native.set_budget(0xFFFFFFFFFFFFFFFF)
        else:
            self.uc.emu_start(self.eip, 0xFFFFFFFF, timeout=self.slice_us)
        pc, count = self._last_hook
        blocks = self.native.blocks_executed() if self.native else 0
        self._skip_hook_pc = pc if pc is not None and pc == self.eip and blocks == count else None

    def run(self, stop: Callable[[], bool] | None = None):
        """Run guest threads until a host call returns, the process halts, a hook requests a
        stop, or ``stop()`` (checked at thread switches and every host-time slice) is true."""
        while not self.halted:
            cur = self.current
            if self.need_switch or cur is None or cur.state != "ready":
                self.need_switch = False
                if cur is not None and cur.state != "done":
                    cur.ctx = self.uc.context_save()
                if cur is not None and cur.state == "host":
                    return self.host_result
                nxt = self._pick()
                while nxt is None:
                    if not self._advance_clock_to_next_deadline():
                        raise GuestFault("deadlock: every guest thread is blocked without a timeout: " +
                                         ", ".join(f"{x.tid:#x}:{x.state}" for x in self.threads))
                    nxt = self._pick()
                self._activate(nxt)
                if self.need_switch:  # the resumed generator blocked again immediately
                    continue
            try:
                self._emu()
            except UcError as e:
                if self.pending_exception is None:
                    self._raise_fault(e)
            if self.pending_exception is not None:
                exc, self.pending_exception = self.pending_exception, None
                raise exc
            if self.fault is not None:
                self._raise_fault(None)
            if self.stop_requested:
                self.stop_requested = False
                return None
            if stop is not None and stop():
                return None
        return None

    def _raise_fault(self, err):
        f = self.fault or {"kind": "unicorn", "error": str(err)}
        f.setdefault("eip", self.eip)
        f["thread"] = self.current.tid if self.current else None
        f["stack"] = self.backtrace()
        self.fault = None
        raise GuestFault(f)

    def backtrace(self, depth=12):
        """Best-effort return-address scan of the current stack (no frame pointers needed)."""
        out = []
        sp = self.esp
        top = self.current.stack_top if self.current else sp + 0x1000
        while sp < top and len(out) < depth:
            try:
                v = self.r32(sp)
            except UcError:
                break
            img = self.image_at(v)
            if img is not None:
                out.append(f"{v:#x}")
            sp += 4
        return out

    def call(self, addr: int, *args, regs: dict | None = None, thread: Thread | None = None) -> int:
        """Run guest function ``addr`` on an idle thread (default: the main thread); return EAX.

        Other guest threads may run while the call blocks. The thread returns to the
        idle "host" state when the function returns to STUB_HOST_RETURN.
        """
        t = thread or self.threads[0]
        assert t.state == "host", f"thread {t.tid:#x} is busy ({t.state})"
        if self.current is not None and self.current is not t and self.current.state != "done":
            self.current.ctx = self.uc.context_save()
        self.uc.context_restore(t.ctx)
        self.current = t
        sp = (self.esp - 0x100) & ~0xF
        self.write(sp, struct.pack(f"<{len(args) + 1}I", STUB_HOST_RETURN, *[a & 0xFFFFFFFF for a in args]))
        self.uc.reg_write(X.UC_X86_REG_ESP, sp)
        for r, v in (regs or {}).items():
            self.uc.reg_write(r, v & 0xFFFFFFFF)
        self.uc.reg_write(X.UC_X86_REG_EIP, addr)
        t.state = "ready"
        self.need_switch = False
        return self.run()
