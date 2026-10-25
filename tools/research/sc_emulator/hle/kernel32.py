"""KERNEL32 HLE: process, modules, memory, files, synchronization, time, locale."""
from __future__ import annotations

import logging
import struct
import time as _time

from unicorn import x86_const as X

from .. import memory as M
from .. import vfs
from ..machine import INFINITE, WAIT_TIMEOUT, Call, Event, Mutex, Semaphore, Thread, Wait
from . import api

log = logging.getLogger(__name__)
K = "kernel32.dll"

# Virtual time: guest clock starts at this calendar instant (UTC) and tick count.
EPOCH_UNIX = 946_684_800  # 2000-01-01 00:00:00 UTC
TICK_BASE_MS = 3_600_000  # GetTickCount() at virtual time 0 (one hour of uptime)
FILETIME_UNIX_OFFSET = 116_444_736_000_000_000

ERROR_SUCCESS = 0
ERROR_INVALID_HANDLE = 6
ERROR_NO_MORE_FILES = 18
ERROR_INSUFFICIENT_BUFFER = 122
ERROR_MOD_NOT_FOUND = 126
ERROR_PROC_NOT_FOUND = 127
ERROR_INVALID_PARAMETER = 87
ERROR_IO_PENDING = 997
INVALID_HANDLE_VALUE = 0xFFFFFFFF

SYSTEM_DLLS = {
    "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll", "version.dll", "imm32.dll",
    "winmm.dll", "ddraw.dll", "dsound.dll", "wintrust.dll", "crypt32.dll", "psapi.dll", "ntdll.dll",
    "tapi32.dll", "wsock32.dll", "ws2_32.dll", "ole32.dll", "comctl32.dll", "dbghelp.dll", "msvcrt.dll",
    "kernelbase.dll", "uxtheme.dll", "dwmapi.dll", "shlwapi.dll", "mscoree.dll",
}
PSEUDO_MODULE_BASE = 0x70000000


def set_error(m, code):
    if m.current is not None:
        m.w32(m.current.teb + 0x34, code)


def last_error(m):
    return m.r32(m.current.teb + 0x34)


def fs(m) -> vfs.FileSystem:
    return m.services["fs"]


# ----------------------------------------------------------------------------- modules
def pseudo_module(m, dll: str) -> int:
    mods = m.services.setdefault("pseudo_modules", {})
    dll = dll.lower()
    if dll not in mods:
        mods[dll] = PSEUDO_MODULE_BASE + 0x100000 * len(mods)
    return mods[dll]


def module_name_from_handle(m, h):
    if h == 0 or (m.exe and h == m.exe.base):
        return m.exe
    for img in m.images.values():
        if img.base == h:
            return img
    for name, base in m.services.get("pseudo_modules", {}).items():
        if base == h:
            return name
    return None


def guest_module_path(m, h) -> str | None:
    x = module_name_from_handle(m, h)
    if x is None:
        return None
    if isinstance(x, str):
        return "C:\\Windows\\System32\\" + x
    if x is m.exe:
        return vfs.INSTALL_DIR + "\\StarCraft.exe"
    return vfs.INSTALL_DIR + "\\" + x.path.name


def _dll_file_name(name: str) -> str:
    n = name.replace("/", "\\").rsplit("\\", 1)[-1].lower()
    if "." not in n:
        n += ".dll"
    return n


def load_library(m, name: str):
    """Shared implementation of LoadLibrary*: returns a generator result (module handle)."""
    n = _dll_file_name(name)
    if n in SYSTEM_DLLS:
        return pseudo_module(m, n)
    img = m.find_image(n)
    if img is not None:
        return img.base
    path = vfs.norm(name if ("\\" in name or ":" in name) else vfs.INSTALL_DIR + "\\" + name)
    key = path.upper()
    host = fs(m).host_files.get(key)
    if host is None and not key.endswith(".DLL") and "." not in key.rsplit("\\", 1)[-1]:
        host = fs(m).host_files.get(key + ".DLL")
    if host is None:
        return None
    img = m.load_image(host)
    m.bind(img)
    return img


@api(K, 1)
def GetModuleHandleA(m, name):
    """NULL -> StarCraft.exe base; loaded original images -> their base; Windows DLL names -> a
    stable pseudo handle (unmapped address used only as a key for GetProcAddress)."""
    if not name:
        return m.exe.base
    n = _dll_file_name(m.cstr(name).decode("latin-1"))
    img = m.find_image(n)
    if img:
        return img.base
    if n in SYSTEM_DLLS:
        return pseudo_module(m, n)
    set_error(m, ERROR_MOD_NOT_FOUND)
    return 0


@api(K, 1)
def GetModuleHandleW(m, name):
    """Wide-character GetModuleHandleA."""
    if not name:
        return m.exe.base
    n = _dll_file_name(m.wstr(name))
    img = m.find_image(n)
    if img:
        return img.base
    if n in SYSTEM_DLLS:
        return pseudo_module(m, n)
    set_error(m, ERROR_MOD_NOT_FOUND)
    return 0


@api(K, 3)
def GetModuleHandleExW(m, flags, name, out):
    """Flag 4 (FROM_ADDRESS) maps an address to the containing original image; otherwise as
    GetModuleHandleW. Writes the handle to *out."""
    if flags & 4:
        img = m.image_at(name)
        h = img.base if img else 0
    else:
        h = GetModuleHandleW(m, name)
    if out:
        m.w32(out, h)
    return 1 if h else 0


def _module_file_name(m, h, buf, size, wide):
    p = guest_module_path(m, h)
    if p is None:
        set_error(m, ERROR_MOD_NOT_FOUND)
        return 0
    if wide:
        data = p.encode("utf-16-le")
        n = min(len(p), size - 1)
        m.write(buf, data[:2 * n] + b"\0\0")
    else:
        data = p.encode("latin-1")
        n = min(len(data), size - 1)
        m.write(buf, data[:n] + b"\0")
    return n


@api(K, 3)
def GetModuleFileNameA(m, h, buf, size):
    """Module paths: StarCraft.exe and game DLLs live in C:\\StarCraft; Windows DLLs in
    C:\\Windows\\System32."""
    return _module_file_name(m, h, buf, size, False)


@api(K, 3)
def GetModuleFileNameW(m, h, buf, size):
    """Wide-character GetModuleFileNameA."""
    return _module_file_name(m, h, buf, size, True)


def _load_library_gen(m, name):
    r = load_library(m, name)
    if r is None:
        set_error(m, ERROR_MOD_NOT_FOUND)
        log.info("LoadLibrary(%s) -> not found", name)
        return 0
    if isinstance(r, int):
        log.info("LoadLibrary(%s) -> %s %#x", name, "already loaded" if m.image_at(r) else "HLE module", r)
        return r
    img = r
    log.info("LoadLibrary(%s) -> original image %s at %#x", name, img.name, img.base)
    if img.entry and img.is_dll:
        yield Call(img.entry, (img.base, 1, 0))  # DLL_PROCESS_ATTACH
    return img.base


@api(K, 1)
def LoadLibraryA(m, name):
    """Windows DLL names return pseudo handles served by HLE (this includes ddraw.dll and dsound.dll even
    though the game folder holds third-party wrappers of those names). Any other DLL is loaded
    from the game directory as an original image: mapped, relocated, imports bound, DllMain
    (PROCESS_ATTACH) run on the calling thread."""
    return (yield from _load_library_gen(m, m.cstr(name).decode("latin-1")))


@api(K, 1)
def LoadLibraryW(m, name):
    """Wide-character LoadLibraryA."""
    return (yield from _load_library_gen(m, m.wstr(name)))


@api(K, 3)
def LoadLibraryExW(m, name, hfile, flags):
    """As LoadLibraryW; flags ignored."""
    return (yield from _load_library_gen(m, m.wstr(name)))


@api(K, 1)
def FreeLibrary(m, h):
    """No-op (images stay mapped); returns TRUE."""
    return 1


@api(K, 2)
def GetProcAddress(m, h, name):
    """Original images: their export (by name or ordinal). Pseudo (Windows) modules: an HLE stub
    when the function is implemented, else NULL with ERROR_PROC_NOT_FOUND (so optional
    imports such as FlsAlloc fall back exactly as on an older Windows)."""
    from . import REGISTRY
    key = name if name < 0x10000 else m.cstr(name).decode("latin-1")
    x = module_name_from_handle(m, h)
    if x is None:
        set_error(m, ERROR_INVALID_HANDLE)
        return 0
    if isinstance(x, str):
        if (x, key) in REGISTRY:
            return m.stub_for(x, key)
        m.services.setdefault("missing_procs", set()).add(f"{x}!{key}")
        set_error(m, ERROR_PROC_NOT_FOUND)
        return 0
    addr = x.export(key)
    if addr is None:
        set_error(m, ERROR_PROC_NOT_FOUND)
        return 0
    return addr


@api(K, 1)
def DisableThreadLibraryCalls(m, h):
    """Returns TRUE (thread attach notifications are never sent anyway)."""
    return 1


# ----------------------------------------------------------------------------- resources
def _resource_entry(img, type_, name, lang=None):
    pe = img.pe
    if not hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        return None

    def match(entry, key):
        if isinstance(key, int):
            return entry.id == key and entry.name is None
        return entry.name is not None and str(entry.name).upper() == key.upper()

    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if not match(t, type_):
            continue
        for n in t.directory.entries:
            if not match(n, name):
                continue
            langs = n.directory.entries
            pick = None
            for l in langs:
                if lang is None or l.id == lang:
                    pick = l
                    break
            pick = pick or (langs[0] if langs else None)
            if pick:
                return pick.data.struct.OffsetToData, pick.data.struct.Size
    return None


def _res_key(m, v):
    if v < 0x10000:
        return v
    s = m.cstr(v).decode("latin-1")
    if s.startswith("#"):
        return int(s[1:])
    return s


def _find_resource(m, h, name, type_, lang=None):
    x = module_name_from_handle(m, h)
    if x is None or isinstance(x, str):
        return 0
    found = _resource_entry(x, _res_key(m, type_), _res_key(m, name), lang)
    if not found:
        set_error(m, 1814)  # ERROR_RESOURCE_NAME_NOT_FOUND
        return 0
    rva, size = found
    rec = m.alloc(8)
    m.w32(rec, x.base + rva)
    m.w32(rec + 4, size)
    return rec


@api(K, 3)
def FindResourceA(m, h, name, type_):
    """Real lookup in the image's resource directory; returns a small guest record
    {data address, size} used as HRSRC."""
    return _find_resource(m, h, name, type_)


@api(K, 4)
def FindResourceExA(m, h, type_, name, lang):
    """FindResourceA with a language preference."""
    return _find_resource(m, h, name, type_, lang)


@api(K, 2)
def LoadResource(m, h, res):
    """Returns the resource data address inside the mapped image."""
    return m.r32(res) if res else 0


@api(K, 1)
def LockResource(m, h):
    """Identity (resource memory is the image itself)."""
    return h


@api(K, 2)
def SizeofResource(m, h, res):
    """Size from the HRSRC record."""
    return m.r32(res + 4) if res else 0


@api(K, 1)
def FreeResource(m, h):
    """No-op."""
    return 0


# ----------------------------------------------------------------------------- process / threads
@api(K, 0)
def GetCurrentProcess(m):
    """Pseudo handle -1."""
    return 0xFFFFFFFF


@api(K, 0)
def GetCurrentProcessId(m):
    """Constant process id 0x1000."""
    return 0x1000


@api(K, 0)
def GetCurrentThread(m):
    """Pseudo handle -2."""
    return 0xFFFFFFFE


@api(K, 0)
def GetCurrentThreadId(m):
    """Deterministic thread ids 0x1000 + 4 * creation index."""
    return m.current.tid


@api(K, 6)
def CreateThread(m, sa, stack, start, param, flags, tid_out):
    """Creates a cooperative guest thread (own stack, TEB and FS selector). It first runs when the
    creating thread blocks. CREATE_SUSPENDED (4) is honoured."""
    t = m.create_thread(start, param, stack, suspended=bool(flags & 4))
    if tid_out:
        m.w32(tid_out, t.tid)
    log.info("CreateThread start=%#x param=%#x -> tid %#x", start, param, t.tid)
    return t.handle


@api(K, 1)
def ExitThread(m, code):
    """Ends the calling guest thread."""
    m.exit_thread(m.current, code)
    return 0


@api(K, 1)
def ResumeThread(m, h):
    """Decrements the suspend count; at zero a suspended thread becomes ready."""
    t = m.obj(h)
    if not isinstance(t, Thread):
        return 0xFFFFFFFF
    prev = t.suspend_count
    if t.suspend_count > 0:
        t.suspend_count -= 1
        if t.suspend_count == 0 and t.state == "suspended":
            t.state = "ready"
    return prev


@api(K, 1)
def SuspendThread(m, h):
    """Only the bookkeeping count (threads are cooperative); returns the previous count."""
    t = m.obj(h)
    if not isinstance(t, Thread):
        return 0xFFFFFFFF
    t.suspend_count += 1
    return t.suspend_count - 1


@api(K, 3)
def OpenThread(m, access, inherit, tid):
    """Fails (NULL); only used by the crash reporter."""
    return 0


@api(K, 2)
def SetThreadPriority(m, h, prio):
    """No effect on the cooperative scheduler; returns TRUE."""
    return 1


@api(K, 2)
def GetThreadContext(m, h, ctx):
    """Fails; only used by the crash reporter."""
    return 0


@api(K, 1)
def RtlCaptureContext(m, ctx):
    """Leaves the record zeroed (crash reporter only)."""
    return 0


@api(K, 2)
def TerminateProcess(m, h, code):
    """Ends the emulation with the given exit code."""
    return ExitProcess(m, code)


@api(K, 1)
def ExitProcess(m, code):
    """Ends the emulation (the run loop returns) with the given exit code."""
    from ..machine import EmulationStop
    m.exit_code = code
    m.halted = True
    raise EmulationStop(f"ExitProcess({code})")


@api(K, 2)
def GetExitCodeProcess(m, h, out):
    """STILL_ACTIVE."""
    m.w32(out, 0x103)
    return 1


@api(K, 10)
def CreateProcessA(m, *a):
    """Always fails: no child processes are started."""
    set_error(m, 2)
    return 0


@api(K, 10)
def CreateProcessW(m, *a):
    """Always fails: no child processes are started."""
    set_error(m, 2)
    return 0


@api(K, 3)
def OpenProcess(m, access, inherit, pid):
    """Fails (no other processes exist)."""
    set_error(m, 87)
    return 0


@api(K, 1)
def GetProcessId(m, h):
    """0x1000 for the current process."""
    return 0x1000


@api(K, 5)
def GetProcessTimes(m, h, c, e, k, u):
    """All four FILETIMEs from the virtual clock (kernel/user time = elapsed virtual time)."""
    ft = filetime_now(m)
    for p, v in ((c, ft - m.clock_us * 10), (e, 0), (k, 0), (u, m.clock_us * 10)):
        if p:
            m.write(p, struct.pack("<Q", v))
    return 1


@api(K, 2)
def CreateToolhelp32Snapshot(m, flags, pid):
    """Returns INVALID_HANDLE_VALUE (no process/thread/module enumeration)."""
    return INVALID_HANDLE_VALUE


for _n in ("Thread32First", "Thread32Next", "Process32First", "Process32Next", "Module32FirstW", "Module32NextW"):
    def _enum(m, h, entry):
        """Enumeration over an empty snapshot: FALSE."""
        set_error(m, ERROR_NO_MORE_FILES)
        return 0
    api(K, 2, name=_n)(_enum)


# ----------------------------------------------------------------------------- synchronization
class CriticalSection:
    def __init__(self):
        self.owner: Thread | None = None
        self.recursion = 0


def _cs(m, addr) -> CriticalSection:
    table = m.services.setdefault("critical_sections", {})
    cs = table.get(addr)
    if cs is None:
        cs = table[addr] = CriticalSection()
    return cs


def _cs_sync(m, addr, cs):
    m.write(addr + 4, struct.pack("<iII", -1 if cs.owner is None else cs.recursion - 1, cs.recursion,
                                  cs.owner.tid if cs.owner else 0))


@api(K, 1)
def InitializeCriticalSection(m, addr):
    """Host-side lock record keyed by the CRITICAL_SECTION address; guest fields mirrored."""
    m.write(addr, struct.pack("<IiIIII", 0, -1, 0, 0, 0, 0))
    m.services.setdefault("critical_sections", {})[addr] = CriticalSection()


@api(K, 2)
def InitializeCriticalSectionAndSpinCount(m, addr, spin):
    """As InitializeCriticalSection; returns TRUE."""
    InitializeCriticalSection(m, addr)
    return 1


@api(K, 1)
def DeleteCriticalSection(m, addr):
    """Forgets the lock record."""
    m.services.setdefault("critical_sections", {}).pop(addr, None)


@api(K, 1)
def EnterCriticalSection(m, addr):
    """Recursive ownership; a lock held by another thread blocks the caller (cooperative switch)."""
    cs = _cs(m, addr)
    t = m.current
    if cs.owner is not None and cs.owner is not t:
        yield Wait(predicate=lambda: cs.owner is None)
    cs.owner = t
    cs.recursion += 1
    _cs_sync(m, addr, cs)
    return 0


@api(K, 1)
def LeaveCriticalSection(m, addr):
    """Releases one recursion level."""
    cs = _cs(m, addr)
    if cs.owner is m.current and cs.recursion > 0:
        cs.recursion -= 1
        if cs.recursion == 0:
            cs.owner = None
    _cs_sync(m, addr, cs)


def _create_event(m, manual, initial, name):
    named = m.services.setdefault("named_objects", {})
    if name and name in named:
        set_error(m, 183)
        return m.new_handle(named[name])
    ev = Event(bool(manual), bool(initial), name)
    if name:
        named[name] = ev
    set_error(m, 0)
    return m.new_handle(ev)


@api(K, 4)
def CreateEventA(m, sa, manual, initial, name):
    """Event object (manual/auto reset). Named events are process-global; reopening sets
    ERROR_ALREADY_EXISTS (so the single-instance check sees no other instance)."""
    return _create_event(m, manual, initial, m.cstr(name).decode("latin-1") if name else None)


@api(K, 4)
def CreateEventW(m, sa, manual, initial, name):
    """Wide-character CreateEventA."""
    return _create_event(m, manual, initial, m.wstr(name) if name else None)


@api(K, 1)
def SetEvent(m, h):
    """Signals the event; waiting threads become ready (they run when the caller blocks). An event named by one of
    ``m.services["concurrent_events"]`` (guest addresses holding event handles) belongs to a worker that a real
    machine runs on another core at once: the ready threads run now, until they block again, without charging their
    virtual time to the caller (the clock is restored)."""
    ev = m.obj(h)
    if not isinstance(ev, Event):
        return 0
    ev.signaled = True
    concurrent = any(m.r32(a) == h for a in m.services.get("concurrent_events", ()))
    if concurrent:  # the worker may also not have started yet (a thread first runs when its creator blocks)
        return _run_concurrent(m, h)
    return 1


CONCURRENT_TURNS = 64  # SetEvent's turns for a concurrent worker that blocks on something else on its way


def _run_concurrent(m, h):
    """SetEvent's continuation for a concurrent worker: turns for the ready threads until a thread waits on the event
    again (the worker is done) or the turns run out; the caller's clock kept."""
    clock = m.clock_us
    for _ in range(CONCURRENT_TURNS):
        yield Wait((), 0)
        if any(t is not m.current and t.state == "blocked" and t.wait is not None and h in t.wait.handles
               and not m.obj(h).signaled for t in m.threads):
            break
    m.clock_us = clock
    return 1


@api(K, 1)
def ResetEvent(m, h):
    """Clears the event."""
    ev = m.obj(h)
    if isinstance(ev, Event):
        ev.signaled = False
        return 1
    return 0


@api(K, 2)
def WaitForSingleObject(m, h, timeout):
    """Cooperative wait: returns at once when signalled (auto-reset events are consumed);
    otherwise the thread blocks and other threads run; timeouts are measured on the virtual
    clock. Non-waitable handles (files) count as signalled."""
    o = m.obj(h)
    if h in (0xFFFFFFFF, 0xFFFFFFFE):
        return 0
    if o is None:
        set_error(m, ERROR_INVALID_HANDLE)
        return 0xFFFFFFFF
    if not hasattr(o, "signaled"):
        return 0
    return (yield Wait((h,), timeout))


@api(K, 4)
def WaitForMultipleObjects(m, n, handles, wait_all, timeout):
    """Cooperative WaitForMultipleObjects (any/all)."""
    hs = struct.unpack(f"<{n}I", m.read(handles, 4 * n))
    return (yield Wait(tuple(hs), timeout, bool(wait_all)))


@api(K, 1)
def Sleep(m, ms):
    """Blocks the thread for ``ms`` of virtual time (Sleep(0) yields to other ready threads)."""
    yield Wait((), ms)
    return 0


@api(K, 1)
def CloseHandle(m, h):
    """Closes files (releasing host file objects) and forgets other handles."""
    o = m.handles.pop(h, None)
    if o is None:
        return 1 if h in (0xFFFFFFFF, 0xFFFFFFFE) else 0
    if isinstance(o, vfs.OpenFile) and o.fh is not None:
        o.fh.close()
    if isinstance(o, Thread):
        m.handles[h] = o  # keep thread objects for later waits/exit codes
    return 1


@api(K, 1)
def InterlockedIncrement(m, p):
    """Atomic by construction (single CPU, cooperative threads)."""
    v = (m.rs32(p) + 1) & 0xFFFFFFFF
    m.w32(p, v)
    return v


@api(K, 1)
def InterlockedDecrement(m, p):
    """Atomic by construction."""
    v = (m.rs32(p) - 1) & 0xFFFFFFFF
    m.w32(p, v)
    return v


# ----------------------------------------------------------------------------- TLS / pointers
@api(K, 0)
def TlsAlloc(m):
    """Lowest free slot index (TEB TlsSlots, 64 slots then expansion area)."""
    used = m.services.setdefault("tls_used", set())
    i = 0
    while i in used:
        i += 1
    used.add(i)
    for t in m.threads:
        m.w32(m.tls_slot_addr(t, i), 0)
    return i


@api(K, 1)
def TlsFree(m, i):
    """Releases the slot."""
    m.services.setdefault("tls_used", set()).discard(i)
    return 1


@api(K, 1)
def TlsGetValue(m, i):
    """Reads the calling thread's slot (and sets last error 0)."""
    set_error(m, 0)
    return m.r32(m.tls_slot_addr(m.current, i))


@api(K, 2)
def TlsSetValue(m, i, v):
    """Writes the calling thread's slot."""
    m.w32(m.tls_slot_addr(m.current, i), v)
    return 1


@api(K, 1)
def EncodePointer(m, p):
    """Identity transform (pointer encoding with a zero cookie)."""
    return p


@api(K, 1)
def DecodePointer(m, p):
    """Identity transform."""
    return p


# ----------------------------------------------------------------------------- memory
HEAP_HANDLE = 0x00050000


@api(K, 0)
def GetProcessHeap(m):
    """Constant heap handle; all heaps share one deterministic arena."""
    return HEAP_HANDLE


@api(K, 3)
def HeapCreate(m, opts, initial, maximum):
    """Returns a new heap handle backed by the shared arena."""
    n = m.services.get("heaps", 0) + 1
    m.services["heaps"] = n
    return HEAP_HANDLE + 0x10000 * n


@api(K, 1)
def HeapDestroy(m, h):
    """No-op (blocks are not tracked per heap)."""
    return 1


def _heap_alloc(m, flags, n):
    p = m.alloc(n, zero=bool(flags & 8))
    return p


@api(K, 3)
def HeapAlloc(m, h, flags, n):
    """Deterministic allocation (16-byte aligned, exact-size LIFO reuse); HEAP_ZERO_MEMORY honoured."""
    return _heap_alloc(m, flags, n)


@api(K, 3)
def HeapFree(m, h, flags, p):
    """Returns the block to its size-class free list."""
    if p:
        m.heap.release(p)
    return 1


@api(K, 3)
def HeapSize(m, h, flags, p):
    """Requested size of the block."""
    s = m.heap.size_of(p)
    return 0xFFFFFFFF if s is None else s


@api(K, 4)
def HeapReAlloc(m, h, flags, p, n):
    """Grows/shrinks in place when the rounded block fits, else allocate + copy + free.
    Newly exposed bytes are zeroed with HEAP_ZERO_MEMORY."""
    old = m.heap.size_of(p)
    if old is None:
        return 0
    if M.align(max(n, 1), 16) == m.heap.rounded[p]:
        if flags & 8 and n > old:
            m.write(p + old, bytes(n - old))
        m.heap.sizes[p] = n
        return p
    if flags & 0x10:  # HEAP_REALLOC_IN_PLACE_ONLY
        return 0
    q = m.alloc(n, zero=bool(flags & 8))
    m.write(q, m.read(p, min(old, n)))
    m.heap.release(p)
    return q


@api(K, 2)
def LocalAlloc(m, flags, n):
    """Fixed memory from the heap arena (LMEM_ZEROINIT honoured)."""
    return m.alloc(n, zero=bool(flags & 0x40))


@api(K, 1)
def LocalFree(m, p):
    """Frees a LocalAlloc block; returns NULL."""
    if p:
        m.heap.release(p)
    return 0


@api(K, 4)
def VirtualAlloc(m, addr, size, typ, prot):
    """Reserve/commit from a 64 KiB-granular region (memory is always committed and zero).
    Committing inside an existing reservation returns the requested address."""
    if addr:
        found = m.vregion.find(addr)
        if found:
            return addr
    p = m.vregion.alloc(size)
    m.write(p, bytes(M.align(size, 0x1000)))
    return p


@api(K, 3)
def VirtualFree(m, addr, size, typ):
    """MEM_RELEASE returns the region; MEM_DECOMMIT zeroes the pages."""
    if typ & 0x8000:
        m.vregion.release(addr)
    elif size:
        m.write(addr, bytes(size))
    return 1


@api(K, 3)
def VirtualQuery(m, addr, buf, size):
    """MEMORY_BASIC_INFORMATION for images, regions and stacks (committed, RWX)."""
    img = m.image_at(addr)
    if img:
        base, rsize = img.base, img.size
    else:
        f = m.vregion.find(addr) or m.stacks.find(addr)
        if f:
            base, rsize = f
        else:
            base, rsize = addr & ~0xFFF, 0x1000
    m.write(buf, struct.pack("<IIIIIII", addr & ~0xFFF, base, 0x40, base + rsize - (addr & ~0xFFF), 0x1000, 0x40,
                             0x1000000 if img else 0x20000))
    return 28


@api(K, 2)
def VirtualLock(m, a, n):
    """TRUE."""
    return 1


@api(K, 2)
def VirtualUnlock(m, a, n):
    """TRUE."""
    return 1


@api(K, 1)
def GlobalMemoryStatus(m, p):
    """Reports 1 GiB physical / 2 GiB page file / 2 GiB virtual, all free."""
    m.write(p, struct.pack("<IIIIIIII", 32, 0, 1 << 30, 1 << 30, 2 << 30, 2 << 30, 2 << 30, 2 << 30))
    return 0


def _probe(m, p, n):
    try:
        m.read(p, max(n, 1))
        return True
    except Exception:
        return False


@api(K, 2)
def IsBadReadPtr(m, p, n):
    """TRUE when any byte of the range is unmapped."""
    return 0 if p and _probe(m, p, n) else 1


@api(K, 2)
def IsBadWritePtr(m, p, n):
    """TRUE when any byte of the range is unmapped."""
    return 0 if p and _probe(m, p, n) else 1


@api(K, 2)
def IsBadStringPtrA(m, p, n):
    """TRUE when the string is not readable."""
    return 0 if p and _probe(m, p, 1) else 1


class FileMapping:
    def __init__(self, f, size):
        self.file, self.size = f, size


@api(K, 6)
def CreateFileMappingA(m, hfile, sa, prot, hi, lo, name):
    """Read-only mapping object over an open file (contents copied into guest memory on MapViewOfFile)."""
    f = m.obj(hfile)
    if not isinstance(f, vfs.OpenFile):
        set_error(m, ERROR_INVALID_HANDLE)
        return 0
    return m.new_handle(FileMapping(f, lo or f.size()))


@api(K, 5)
def MapViewOfFile(m, h, access, hi, lo, n):
    """Copies the requested file range into a fresh region (writes are not written back)."""
    fm = m.obj(h)
    if not isinstance(fm, FileMapping):
        return 0
    f = fm.file
    n = n or (fm.size - lo)
    p = m.vregion.alloc(n)
    pos = f.pos
    f.pos = lo
    m.write(p, f.read(n))
    f.pos = pos
    return p


@api(K, 1)
def UnmapViewOfFile(m, p):
    """Releases the region."""
    m.vregion.release(p)
    return 1


# ----------------------------------------------------------------------------- files
STD_HANDLES = {0xFFFFFFF6: 0x10, 0xFFFFFFF5: 0x14, 0xFFFFFFF4: 0x18}


def _create_file(m, path, access, disposition):
    f, err = fs(m).open(path, bool(access & 0x40000000), disposition)
    if f is None:
        set_error(m, err)
        log.debug("CreateFile %s -> error %d", path, err)
        return INVALID_HANDLE_VALUE
    set_error(m, err)
    return m.new_handle(f)


@api(K, 7)
def CreateFileA(m, name, access, share, sa, disposition, flags, tmpl):
    """Files under C:\\StarCraft read from the game directory; writes and all other paths go to
    the in-memory overlay. Directories cannot be opened."""
    return _create_file(m, m.cstr(name).decode("latin-1"), access, disposition)


@api(K, 7)
def CreateFileW(m, name, access, share, sa, disposition, flags, tmpl):
    """Wide-character CreateFileA."""
    return _create_file(m, m.wstr(name), access, disposition)


def _signal_overlapped(m, ov, n):
    if ov:
        m.w32(ov, 0)
        m.w32(ov + 4, n)
        hev = m.r32(ov + 16)
        ev = m.obj(hev)
        if isinstance(ev, Event):
            ev.signaled = True


@api(K, 5)
def ReadFile(m, h, buf, n, nread, ov):
    """Synchronous read. With an OVERLAPPED the offset is taken from it, the transfer completes
    immediately and its event is signalled."""
    f = m.obj(h)
    if not isinstance(f, vfs.OpenFile):
        set_error(m, ERROR_INVALID_HANDLE)
        return 0
    if ov:
        f.pos = m.r32(ov + 8) | (m.r32(ov + 12) << 32)
    data = f.read(n)
    m.write(buf, data)
    if nread:
        m.w32(nread, len(data))
    _signal_overlapped(m, ov, len(data))
    return 1


@api(K, 5)
def WriteFile(m, h, buf, n, nwritten, ov):
    """Writes into the overlay; console/std handles are logged and discarded."""
    data = m.read(buf, n)
    if h in STD_HANDLES.values() or h in STD_HANDLES:
        log.info("guest stdout: %r", data[:200])
        if nwritten:
            m.w32(nwritten, n)
        return 1
    f = m.obj(h)
    if not isinstance(f, vfs.OpenFile) or not f.writable:
        set_error(m, 5)
        return 0
    if ov:
        f.pos = m.r32(ov + 8)
    f.write(data)
    if nwritten:
        m.w32(nwritten, n)
    _signal_overlapped(m, ov, n)
    return 1


@api(K, 4)
def GetOverlappedResult(m, h, ov, out, wait):
    """Transfers always complete synchronously: returns the recorded byte count."""
    if out:
        m.w32(out, m.r32(ov + 4))
    return 1


@api(K, 4)
def SetFilePointer(m, h, dist, hi_ptr, method):
    """FILE_BEGIN/CURRENT/END with optional high dword."""
    f = m.obj(h)
    if not isinstance(f, vfs.OpenFile):
        set_error(m, ERROR_INVALID_HANDLE)
        return 0xFFFFFFFF
    d = struct.unpack("<i", struct.pack("<I", dist))[0]
    if hi_ptr:
        d = (m.rs32(hi_ptr) << 32) | dist
    base = {0: 0, 1: f.pos, 2: f.size()}[method]
    f.pos = max(0, base + d)
    if hi_ptr:
        m.w32(hi_ptr, f.pos >> 32)
    set_error(m, 0)
    return f.pos & 0xFFFFFFFF


@api(K, 5)
def SetFilePointerEx(m, h, lo, hi, out, method):
    """64-bit SetFilePointer."""
    f = m.obj(h)
    if not isinstance(f, vfs.OpenFile):
        return 0
    d = struct.unpack("<q", struct.pack("<II", lo, hi))[0]
    base = {0: 0, 1: f.pos, 2: f.size()}[method]
    f.pos = max(0, base + d)
    if out:
        m.write(out, struct.pack("<Q", f.pos))
    return 1


@api(K, 2)
def GetFileSize(m, h, hi):
    """Size of the open file."""
    f = m.obj(h)
    if not isinstance(f, vfs.OpenFile):
        set_error(m, ERROR_INVALID_HANDLE)
        return 0xFFFFFFFF
    if hi:
        m.w32(hi, 0)
    return f.size()


@api(K, 1)
def SetEndOfFile(m, h):
    """Truncates an overlay file at the current position."""
    f = m.obj(h)
    if isinstance(f, vfs.OpenFile) and f.data is not None:
        del f.data[f.pos:]
        return 1
    return 0


@api(K, 1)
def FlushFileBuffers(m, h):
    """TRUE."""
    return 1


@api(K, 1)
def GetFileType(m, h):
    """FILE_TYPE_DISK for files, FILE_TYPE_CHAR for the standard handles."""
    if h in STD_HANDLES.values():
        return 2
    return 1 if isinstance(m.obj(h), vfs.OpenFile) else 0


def _attrs(m, path):
    a = fs(m).attributes(path)
    if a is None:
        set_error(m, vfs.ERROR_FILE_NOT_FOUND if fs(m).parent_exists(path) else vfs.ERROR_PATH_NOT_FOUND)
        return 0xFFFFFFFF
    return a


@api(K, 1)
def GetFileAttributesA(m, name):
    """DIRECTORY or ARCHIVE from the virtual file system; INVALID_FILE_ATTRIBUTES when absent."""
    return _attrs(m, m.cstr(name).decode("latin-1"))


@api(K, 1)
def GetFileAttributesW(m, name):
    """Wide-character GetFileAttributesA."""
    return _attrs(m, m.wstr(name))


@api(K, 2)
def SetFileAttributesA(m, name, attrs):
    """Accepted for existing paths; attributes are not stored."""
    return 1 if fs(m).attributes(m.cstr(name).decode("latin-1")) is not None else 0


@api(K, 1)
def DeleteFileA(m, name):
    """Deletes from the overlay (host files are only hidden)."""
    err = fs(m).delete(m.cstr(name).decode("latin-1"))
    if err:
        set_error(m, err)
        return 0
    return 1


def _copy(m, src, dst, fail_exists):
    if not fs(m).is_file(src):
        set_error(m, 2)
        return 0
    if fail_exists and fs(m).is_file(dst):
        set_error(m, 80)
        return 0
    f, err = fs(m).open(src, False, 3)
    data = f.read(f.size())
    g, err = fs(m).open(dst, True, 2)
    if g is None:
        set_error(m, err)
        return 0
    g.write(data)
    return 1


@api(K, 3)
def CopyFileA(m, src, dst, fail):
    """Copies within the virtual file system (destination in the overlay)."""
    return _copy(m, m.cstr(src).decode("latin-1"), m.cstr(dst).decode("latin-1"), fail)


@api(K, 3)
def CopyFileW(m, src, dst, fail):
    """Wide-character CopyFileA."""
    return _copy(m, m.wstr(src), m.wstr(dst), fail)


def _mkdir(m, path):
    err = fs(m).mkdir(path)
    if err:
        set_error(m, err)
        return 0
    return 1


@api(K, 2)
def CreateDirectoryA(m, name, sa):
    """Creates an overlay directory."""
    return _mkdir(m, m.cstr(name).decode("latin-1"))


@api(K, 2)
def CreateDirectoryW(m, name, sa):
    """Wide-character CreateDirectoryA."""
    return _mkdir(m, m.wstr(name))


class FindHandle:
    def __init__(self, entries, wide):
        self.entries, self.pos, self.wide = entries, 0, wide


def _fill_find(m, buf, entry, wide):
    name, is_dir, size = entry
    attrs = vfs.FILE_ATTRIBUTE_DIRECTORY if is_dir else vfs.FILE_ATTRIBUTE_ARCHIVE
    ft = filetime_now(m)
    head = struct.pack("<IQQQIIII", attrs, ft, ft, ft, 0, size, 0, 0)
    if wide:
        m.write(buf, head + (name.encode("utf-16-le") + b"\0\0").ljust(520, b"\0") + bytes(28))
    else:
        m.write(buf, head + (name.encode("latin-1") + b"\0").ljust(260, b"\0") + bytes(14))


def _find_first(m, pattern, buf, wide):
    entries = fs(m).listdir(pattern)
    if not entries:
        set_error(m, 2)
        return INVALID_HANDLE_VALUE
    fh = FindHandle(entries, wide)
    _fill_find(m, buf, entries[0], wide)
    fh.pos = 1
    return m.new_handle(fh)


@api(K, 2)
def FindFirstFileA(m, pattern, buf):
    """Lists the virtual directory (host directory merged with overlay), sorted by name."""
    return _find_first(m, m.cstr(pattern).decode("latin-1"), buf, False)


@api(K, 2)
def FindFirstFileW(m, pattern, buf):
    """Wide-character FindFirstFileA."""
    return _find_first(m, m.wstr(pattern), buf, True)


@api(K, 2)
def FindNextFileA(m, h, buf):
    """Next entry or ERROR_NO_MORE_FILES."""
    fh = m.obj(h)
    if not isinstance(fh, FindHandle) or fh.pos >= len(fh.entries):
        set_error(m, ERROR_NO_MORE_FILES)
        return 0
    _fill_find(m, buf, fh.entries[fh.pos], fh.wide)
    fh.pos += 1
    return 1


@api(K, 1)
def FindClose(m, h):
    """Releases the search handle."""
    m.handles.pop(h, None)
    return 1


@api(K, 4)
def GetFullPathNameA(m, name, size, buf, filepart):
    """Normalises relative paths against the current directory C:\\StarCraft."""
    s = m.cstr(name).decode("latin-1")
    if not (len(s) > 1 and s[1] == ":") and not s.startswith("\\"):
        s = vfs.INSTALL_DIR + "\\" + s
    full = vfs.norm(s).encode("latin-1")
    if len(full) + 1 > size:
        return len(full) + 1
    m.write(buf, full + b"\0")
    if filepart:
        m.w32(filepart, buf + full.rfind(b"\\") + 1)
    return len(full)


@api(K, 2)
def GetCurrentDirectoryW(m, size, buf):
    """C:\\StarCraft."""
    s = vfs.INSTALL_DIR
    if size < len(s) + 1:
        return len(s) + 1
    m.write(buf, s.encode("utf-16-le") + b"\0\0")
    return len(s)


def _temp_path(m, size, buf, wide):
    s = vfs.TEMP_DIR + "\\"
    if size < len(s) + 1:
        return len(s) + 1
    m.write(buf, (s.encode("utf-16-le") + b"\0\0") if wide else (s.encode() + b"\0"))
    return len(s)


@api(K, 2)
def GetTempPathA(m, size, buf):
    """C:\\Users\\Player\\AppData\\Local\\Temp\\ (overlay)."""
    return _temp_path(m, size, buf, False)


@api(K, 2)
def GetTempPathW(m, size, buf):
    """Wide-character GetTempPathA."""
    return _temp_path(m, size, buf, True)


@api(K, 4)
def GetTempFileNameA(m, path, prefix, unique, buf):
    """<path>\\<prefix><counter>.tmp with a deterministic counter."""
    n = m.services.get("tempcount", 0) + 1
    m.services["tempcount"] = n
    s = m.cstr(path) + b"\\" + m.cstr(prefix)[:3] + b"%04X.tmp" % (unique or n)
    m.write(buf, s + b"\0")
    return unique or n


def _drive_type(path):
    return 3 if path.upper().startswith("C") else 1


@api(K, 1)
def GetDriveTypeA(m, root):
    """C: is a fixed disk (3); every other drive does not exist (1), i.e. no CD-ROM drives."""
    return _drive_type(m.cstr(root).decode("latin-1") if root else "C")


@api(K, 1)
def GetDriveTypeW(m, root):
    """Wide-character GetDriveTypeA."""
    return _drive_type(m.wstr(root) if root else "C")


@api(K, 5)
def GetDiskFreeSpaceA(m, root, spc, bps, free, total):
    """8 sectors/cluster, 512 bytes/sector, 1 Mi free of 4 Mi clusters (4 GiB free)."""
    for p, v in ((spc, 8), (bps, 512), (free, 1 << 20), (total, 4 << 20)):
        if p:
            m.w32(p, v)
    return 1


@api(K, 2)
def GetLogicalDriveStringsA(m, size, buf):
    """Only C:\\."""
    s = b"C:\\\0\0"
    if size >= len(s):
        m.write(buf, s)
    return len(s) - 1


@api(K, 8)
def GetVolumeInformationA(m, root, name, nsize, serial, maxlen, flags, fsname, fsize):
    """Volume "SYSTEM", serial 0x12345678, NTFS."""
    if name and nsize:
        m.write(name, b"SYSTEM\0")
    if serial:
        m.w32(serial, 0x12345678)
    if maxlen:
        m.w32(maxlen, 255)
    if flags:
        m.w32(flags, 0)
    if fsname and fsize:
        m.write(fsname, b"NTFS\0")
    return 1


@api(K, 2)
def CompareFileTime(m, a, b):
    """-1/0/1 comparison of two FILETIMEs."""
    x = struct.unpack("<Q", m.read(a, 8))[0]
    y = struct.unpack("<Q", m.read(b, 8))[0]
    return 0xFFFFFFFF if x < y else (1 if x > y else 0)


def _systemtime(ft):
    secs = (ft - FILETIME_UNIX_OFFSET) // 10_000_000
    ms = ((ft - FILETIME_UNIX_OFFSET) // 10_000) % 1000
    t = _time.gmtime(secs)
    return struct.pack("<8H", t.tm_year, t.tm_mon, (t.tm_wday + 1) % 7, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, ms)


@api(K, 2)
def FileTimeToSystemTime(m, ft, st):
    """Calendar conversion (UTC)."""
    m.write(st, _systemtime(struct.unpack("<Q", m.read(ft, 8))[0]))
    return 1


@api(K, 2)
def FileTimeToLocalFileTime(m, ft, out):
    """The guest time zone is UTC: identity."""
    m.write(out, m.read(ft, 8))
    return 1


@api(K, 2)
def SystemTimeToFileTime(m, st, out):
    """Calendar conversion (UTC)."""
    y, mo, _, d, h, mi, s, ms = struct.unpack("<8H", m.read(st, 16))
    import calendar
    secs = calendar.timegm((y, mo, d, h, mi, s, 0, 0, 0))
    m.write(out, struct.pack("<Q", secs * 10_000_000 + ms * 10_000 + FILETIME_UNIX_OFFSET))
    return 1


# ----------------------------------------------------------------------------- console / std
@api(K, 1)
def GetStdHandle(m, which):
    """Fixed pseudo handles for stdin/stdout/stderr (writes are logged)."""
    return STD_HANDLES.get(which, 0xFFFFFFFF)


@api(K, 2)
def SetStdHandle(m, which, h):
    """TRUE."""
    return 1


@api(K, 2)
def GetConsoleMode(m, h, out):
    """FALSE (no console)."""
    return 0


@api(K, 0)
def GetConsoleCP(m):
    """437."""
    return 437


@api(K, 5)
def WriteConsoleW(m, h, buf, n, out, res):
    """Logged and discarded."""
    if out:
        m.w32(out, n)
    return 1


@api(K, 5)
def ReadConsoleW(m, h, buf, n, out, ctl):
    """FALSE."""
    return 0


@api(K, 2)
def SetConsoleCtrlHandler(m, fn, add):
    """TRUE (no console events)."""
    return 1


@api(K, 1)
def OutputDebugStringA(m, s):
    """Logged."""
    log.info("OutputDebugString: %s", m.cstr(s)[:300])


@api(K, 1)
def OutputDebugStringW(m, s):
    """Logged."""
    log.info("OutputDebugString: %s", m.wstr(s)[:300])


# ----------------------------------------------------------------------------- environment / system
def _cmdline(m):
    return m.services["command_line"]


@api(K, 0)
def GetCommandLineA(m):
    """The configured command line (default ``"C:\\StarCraft\\StarCraft.exe"``)."""
    p = m.services.get("cmdline_a")
    if p is None:
        p = m.services["cmdline_a"] = m.alloc_str(_cmdline(m).encode("latin-1"))
    return p


@api(K, 0)
def GetCommandLineW(m):
    """Wide-character GetCommandLineA."""
    p = m.services.get("cmdline_w")
    if p is None:
        data = _cmdline(m).encode("utf-16-le") + b"\0\0"
        p = m.services["cmdline_w"] = m.alloc(len(data))
        m.write(p, data)
    return p


def _startup(m, p):
    m.write(p, struct.pack("<I", 68) + bytes(64))
    m.w32(p + 44, 0x1)       # STARTF_USESHOWWINDOW
    m.w16(p + 48, 1)         # SW_SHOWNORMAL


@api(K, 1)
def GetStartupInfoA(m, p):
    """Zeroed STARTUPINFO with SW_SHOWNORMAL."""
    _startup(m, p)


@api(K, 1)
def GetStartupInfoW(m, p):
    """Zeroed STARTUPINFO with SW_SHOWNORMAL."""
    _startup(m, p)


ENVIRONMENT = ["PATH=C:\\Windows\\System32", "SystemRoot=C:\\Windows", "TEMP=" + vfs.TEMP_DIR,
               "TMP=" + vfs.TEMP_DIR, "USERNAME=Player", "USERPROFILE=" + vfs.USER_DIR]


@api(K, 0)
def GetEnvironmentStrings(m):
    """Small fixed environment block (PATH, SystemRoot, TEMP, TMP, USERNAME, USERPROFILE)."""
    data = b"".join(e.encode() + b"\0" for e in ENVIRONMENT) + b"\0"
    p = m.alloc(len(data))
    m.write(p, data)
    return p


@api(K, 0)
def GetEnvironmentStringsW(m):
    """Wide-character GetEnvironmentStrings."""
    data = b"".join(e.encode("utf-16-le") + b"\0\0" for e in ENVIRONMENT) + b"\0\0"
    p = m.alloc(len(data))
    m.write(p, data)
    return p


@api(K, 1)
def FreeEnvironmentStringsA(m, p):
    """Frees the block."""
    m.heap.release(p)
    return 1


@api(K, 1)
def FreeEnvironmentStringsW(m, p):
    """Frees the block."""
    m.heap.release(p)
    return 1


@api(K, 2)
def SetEnvironmentVariableA(m, name, value):
    """Accepted and ignored."""
    return 1


@api(K, 1)
def SetHandleCount(m, n):
    """Returns n."""
    return n


@api(K, 0)
def GetVersion(m):
    """Windows XP SP3 (5.1, build 2600)."""
    return 0x0A280105


@api(K, 1)
def GetVersionExA(m, p):
    """Windows XP SP3 (5.1.2600, platform NT)."""
    size = m.r32(p)
    m.write(p + 4, struct.pack("<IIII", 5, 1, 2600, 2) + b"Service Pack 3".ljust(128, b"\0"))
    if size >= 156:
        m.write(p + 148, struct.pack("<HHHBB", 3, 0, 0x100, 1, 0))
    return 1


@api(K, 1)
def GetSystemInfo(m, p):
    """One x86 (Pentium-class, type 586) processor, 4 KiB pages, 64 KiB granularity."""
    m.write(p, struct.pack("<HHIIIIIIIHH", 0, 0, 0x1000, 0x10000, 0x7FFEFFFF, 1, 1, 586, 0x10000, 6, 0x0F01))
    return 0


@api(K, 1)
def IsProcessorFeaturePresent(m, f):
    """TRUE for the classic features (MMX, SSE, SSE2, RDTSC, CMPXCHG8B), FALSE otherwise."""
    return 1 if f in (3, 6, 8, 10, 2) else 0


@api(K, 0)
def IsDebuggerPresent(m):
    """FALSE."""
    return 0


@api(K, 1)
def SetUnhandledExceptionFilter(m, fn):
    """Remembers the filter; returns the previous one."""
    prev = m.services.get("unhandled_filter", 0)
    m.services["unhandled_filter"] = fn
    return prev


@api(K, 1)
def UnhandledExceptionFilter(m, p):
    """EXCEPTION_CONTINUE_SEARCH."""
    return 0


@api(K, 1)
def SetErrorMode(m, mode):
    """Returns the previous mode (0)."""
    return 0


@api(K, 4)
def RaiseException(m, code, flags, n, args):
    """Software exceptions are dispatched through the guest SEH chain (see seh.py)."""
    from ..seh import raise_software_exception
    return (yield from raise_software_exception(m, code, flags, n, args))


@api(K, 4)
def RtlUnwind(m, target_frame, target_ip, record, value):
    """Calls the intervening SEH handlers with EXCEPTION_UNWINDING, unlinks them and returns."""
    from ..seh import rtl_unwind
    return (yield from rtl_unwind(m, target_frame, target_ip, record, value))


@api(K, 2)
def GetComputerNameA(m, buf, size_p):
    """"EMULATOR"."""
    m.write(buf, b"EMULATOR\0")
    m.w32(size_p, 8)
    return 1


@api(K, 0)
def GetLastError(m):
    """TEB LastErrorValue."""
    return last_error(m)


@api(K, 1)
def SetLastError(m, code):
    """TEB LastErrorValue."""
    set_error(m, code)


@api(K, 7)
def FormatMessageA(m, flags, src, msgid, lang, buf, size, args):
    """Formats "Error <id>" (system message tables are not available)."""
    s = b"Error %d\0" % msgid
    if flags & 0x100:
        p = m.alloc(len(s))
        m.write(p, s)
        m.w32(buf, p)
    else:
        m.write(buf, s[:max(size, 1) - 1] + b"\0")
    return len(s) - 1


@api(K, 3)
def GetProfileIntA(m, app, key, default):
    """Returns the default."""
    return default


@api(K, 2)
def LoadModule(m, name, params):
    """Fails."""
    return 2


# ----------------------------------------------------------------------------- locale / strings
@api(K, 0)
def GetACP(m):
    """Code page 1252."""
    return 1252


@api(K, 0)
def GetOEMCP(m):
    """Code page 437."""
    return 437


@api(K, 2)
def GetCPInfo(m, cp, p):
    """Single-byte code page (MaxCharSize 1, default char '?', no lead bytes)."""
    m.write(p, struct.pack("<I", 1) + b"?\0" + bytes(12))
    return 1


@api(K, 1)
def IsValidCodePage(m, cp):
    """TRUE for 1252/437/65001."""
    return 1 if cp in (1252, 437, 65001, 0) else 0


@api(K, 1)
def IsDBCSLeadByte(m, c):
    """FALSE (single-byte code page)."""
    return 0


@api(K, 0)
def AreFileApisANSI(m):
    """TRUE."""
    return 1


def _codec(cp):
    return "cp437" if cp == 437 or cp == 1 else ("utf-8" if cp == 65001 else "cp1252")


@api(K, 6)
def MultiByteToWideChar(m, cp, flags, src, n, dst, dn):
    """cp1252 (or the given code page) to UTF-16; n = -1 includes the terminator."""
    data = m.cstr(src) + b"\0" if n == 0xFFFFFFFF else m.read(src, n)
    s = data.decode(_codec(cp), "replace")
    out = s.encode("utf-16-le")
    if dn == 0:
        return len(s)
    if len(s) > dn:
        set_error(m, ERROR_INSUFFICIENT_BUFFER)
        return 0
    m.write(dst, out)
    return len(s)


@api(K, 8)
def WideCharToMultiByte(m, cp, flags, src, n, dst, dn, default, used):
    """UTF-16 to cp1252 (unmappable characters become '?')."""
    if n == 0xFFFFFFFF:
        s = m.wstr(src) + "\0"
    else:
        s = m.read(src, 2 * n).decode("utf-16-le", "replace")
    out = s.encode(_codec(cp), "replace")
    if used:
        m.w32(used, 0)
    if dn == 0:
        return len(out)
    if len(out) > dn:
        set_error(m, ERROR_INSUFFICIENT_BUFFER)
        return 0
    m.write(dst, out)
    return len(out)


def _lcmap(s: str, flags: int) -> str:
    if flags & 0x200:
        return s.upper()
    if flags & 0x100:
        return s.lower()
    return s


@api(K, 6)
def LCMapStringW(m, lcid, flags, src, n, dst, dn):
    """LCMAP_UPPERCASE / LCMAP_LOWERCASE (other mappings copy)."""
    s = m.wstr(src) + "\0" if n == 0xFFFFFFFF else m.read(src, 2 * n).decode("utf-16-le", "replace")
    r = _lcmap(s, flags)
    if dn == 0:
        return len(r)
    m.write(dst, r.encode("utf-16-le")[:2 * dn])
    return min(len(r), dn)


@api(K, 6)
def LCMapStringA(m, lcid, flags, src, n, dst, dn):
    """cp1252 LCMapStringW."""
    data = m.cstr(src) + b"\0" if n == 0xFFFFFFFF else m.read(src, n)
    r = _lcmap(data.decode("cp1252", "replace"), flags).encode("cp1252", "replace")
    if dn == 0:
        return len(r)
    m.write(dst, r[:dn])
    return min(len(r), dn)


def _ctype1(ch: str) -> int:
    c = 0
    if ch.isupper():
        c |= 0x1
    if ch.islower():
        c |= 0x2
    if ch.isdigit():
        c |= 0x4
    if ch.isspace():
        c |= 0x8
    if not ch.isalnum() and ch.isprintable() and not ch.isspace():
        c |= 0x10
    if ord(ch) < 32 or ord(ch) == 127:
        c |= 0x20
    if ch == " ":
        c |= 0x40
    if ch in "0123456789abcdefABCDEF":
        c |= 0x80
    if ch.isalpha():
        c |= 0x100
    return c


@api(K, 4)
def GetStringTypeW(m, typ, src, n, out):
    """CT_CTYPE1 classification from Unicode character properties."""
    s = m.wstr(src) if n == 0xFFFFFFFF else m.read(src, 2 * n).decode("utf-16-le", "replace")
    m.write(out, b"".join(struct.pack("<H", _ctype1(c)) for c in s))
    return 1


@api(K, 5)
def GetStringTypeA(m, lcid, typ, src, n, out):
    """cp1252 GetStringTypeW."""
    data = m.cstr(src) if n == 0xFFFFFFFF else m.read(src, n)
    s = data.decode("cp1252", "replace")
    m.write(out, b"".join(struct.pack("<H", _ctype1(c)) for c in s))
    return 1


@api(K, 6)
def CompareStringW(m, lcid, flags, a, na, b, nb):
    """Ordinal comparison (NORM_IGNORECASE honoured); returns 1/2/3."""
    x = m.wstr(a) if na == 0xFFFFFFFF else m.read(a, 2 * na).decode("utf-16-le")
    y = m.wstr(b) if nb == 0xFFFFFFFF else m.read(b, 2 * nb).decode("utf-16-le")
    if flags & 1:
        x, y = x.lower(), y.lower()
    return 1 if x < y else (3 if x > y else 2)


@api(K, 0)
def GetUserDefaultLangID(m):
    """0x0409 (English, United States)."""
    return 0x409


@api(K, 0)
def GetUserDefaultLCID(m):
    """0x0409."""
    return 0x409


@api(K, 2)
def IsValidLocale(m, lcid, flags):
    """TRUE for 0x409."""
    return 1 if lcid == 0x409 else 0


@api(K, 4)
def GetLocaleInfoW(m, lcid, typ, buf, n):
    """Fails (no locale database); callers fall back to defaults."""
    return 0


@api(K, 3)
def EnumSystemLocalesW(m, fn, flags):
    """TRUE without callbacks."""
    return 1


def _fmt_date(m, st, fmt_time):
    if st:
        y, mo, _, d, h, mi, s, ms = struct.unpack("<8H", m.read(st, 16))
    else:
        t = _time.gmtime(EPOCH_UNIX + m.clock_us // 1_000_000)
        y, mo, d, h, mi, s = t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec
    return (f"{h:02d}:{mi:02d}:{s:02d}" if fmt_time else f"{mo}/{d}/{y}").encode()


@api(K, 6)
def GetDateFormatA(m, lcid, flags, st, fmt, buf, n):
    """M/D/YYYY."""
    s = _fmt_date(m, st, False) + b"\0"
    if n == 0:
        return len(s)
    m.write(buf, s[:n])
    return len(s)


@api(K, 6)
def GetTimeFormatA(m, lcid, flags, st, fmt, buf, n):
    """HH:MM:SS."""
    s = _fmt_date(m, st, True) + b"\0"
    if n == 0:
        return len(s)
    m.write(buf, s[:n])
    return len(s)


@api(K, 1)
def GetTimeZoneInformation(m, p):
    """UTC, no daylight saving (TIME_ZONE_ID_UNKNOWN)."""
    m.write(p, bytes(172))
    return 0


@api(K, 3)
def MulDiv(m, a, b, c):
    """(a*b)/c rounded half away from zero; -1 on division by zero."""
    sa, sb, sc = (struct.unpack("<i", struct.pack("<I", v))[0] for v in (a, b, c))
    if sc == 0:
        return 0xFFFFFFFF
    p = sa * sb
    q = (abs(p) + abs(sc) // 2) // abs(sc)
    return (q if (p >= 0) == (sc > 0) else -q) & 0xFFFFFFFF


@api(K, 2)
def lstrcpyA(m, dst, src):
    """strcpy."""
    m.write(dst, m.cstr(src) + b"\0")
    return dst


@api(K, 3)
def lstrcpynA(m, dst, src, n):
    """Copies at most n-1 characters and terminates."""
    if n == 0:
        return dst
    m.write(dst, m.cstr(src)[:n - 1] + b"\0")
    return dst


# ----------------------------------------------------------------------------- time
def filetime_now(m) -> int:
    return (EPOCH_UNIX * 1_000_000 + m.clock_us) * 10 + FILETIME_UNIX_OFFSET


TIME_QUERY_COST_US = 50  # virtual time charged per clock query (lets busy-wait loops terminate)


@api(K, 0)
def GetTickCount(m):
    """Virtual milliseconds (TICK_BASE_MS + clock). Every query first charges
    TIME_QUERY_COST_US (50 us) to the clock, so loops that spin on the clock make progress."""
    m.clock_us += TIME_QUERY_COST_US
    return (TICK_BASE_MS + m.clock_us // 1000) & 0xFFFFFFFF


@api(K, 1)
def QueryPerformanceCounter(m, p):
    """Virtual microseconds (frequency 1 MHz); charges TIME_QUERY_COST_US like GetTickCount."""
    m.clock_us += TIME_QUERY_COST_US
    m.write(p, struct.pack("<Q", TICK_BASE_MS * 1000 + m.clock_us))
    return 1


@api(K, 1)
def QueryPerformanceFrequency(m, p):
    """1,000,000."""
    m.write(p, struct.pack("<Q", 1_000_000))
    return 1


@api(K, 1)
def GetSystemTimeAsFileTime(m, p):
    """2000-01-01 00:00:00 UTC plus the virtual clock."""
    m.write(p, struct.pack("<Q", filetime_now(m)))


@api(K, 1)
def GetLocalTime(m, p):
    """As GetSystemTime (UTC time zone)."""
    m.write(p, _systemtime(filetime_now(m)))


@api(K, 1)
def GetSystemTime(m, p):
    """Virtual calendar time."""
    m.write(p, _systemtime(filetime_now(m)))


# ----------------------------------------------------------------------------- comm (modem provider, unused)
for _n, _k in (("GetCommState", 2), ("SetCommState", 2), ("SetCommTimeouts", 2)):
    def _comm(m, *a):
        """Serial ports do not exist: FALSE."""
        return 0
    api(K, _k, name=_n)(_comm)
