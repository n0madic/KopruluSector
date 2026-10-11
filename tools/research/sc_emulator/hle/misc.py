"""HLE for the smaller Windows DLLs: ADVAPI32 (registry, security), VERSION, SHELL32,
IMM32, WINMM, WINTRUST, CRYPT32, PSAPI and TAPI32."""
from __future__ import annotations

import logging
import struct

from .. import vfs
from . import api
from .kernel32 import module_name_from_handle, set_error

log = logging.getLogger(__name__)
A = "advapi32.dll"

HKEY_ROOTS = {0x80000000: "HKCR", 0x80000001: "HKCU", 0x80000002: "HKLM", 0x80000003: "HKU"}
ERROR_FILE_NOT_FOUND = 2
ERROR_MORE_DATA = 234
ERROR_NO_MORE_ITEMS = 259


class Registry:
    """In-memory registry. It starts with only the values supplied by the run
    configuration (default: none, so every option takes its built-in default) and keeps
    whatever the guest writes during the run."""

    def __init__(self, preset: dict | None = None):
        self.keys: dict[str, dict[str, tuple[int, bytes]]] = {}
        for path, values in (preset or {}).items():
            k = self.keys.setdefault(path.upper(), {})
            for name, (typ, data) in values.items():
                k[name.upper()] = (typ, data)
        self.handles: dict[int, str] = {}
        self.next = 0x50000

    def path(self, h, sub: str) -> str | None:
        base = HKEY_ROOTS.get(h) or self.handles.get(h)
        if base is None:
            return None
        sub = sub.strip("\\")
        return (base + ("\\" + sub if sub else "")).upper()

    def exists(self, p):
        return p in self.keys or any(k.startswith(p + "\\") for k in self.keys)

    def open(self, p) -> int:
        h = self.next
        self.next += 4
        self.handles[h] = p
        return h


def reg(m) -> Registry:
    return m.services["registry"]


def _open(m, root, sub, out, create):
    r = reg(m)
    p = r.path(root, sub)
    if p is None:
        return 6
    if not r.exists(p):
        if not create:
            return ERROR_FILE_NOT_FOUND
        r.keys[p] = {}
    if out:
        m.w32(out, r.open(p))
    return 0


@api(A, 5)
def RegOpenKeyExA(m, root, sub, opts, sam, out):
    """Opens a key that exists in the in-memory registry; ERROR_FILE_NOT_FOUND otherwise."""
    return _open(m, root, m.cstr(sub).decode("latin-1"), out, False)


@api(A, 9)
def RegCreateKeyExA(m, root, sub, res, cls, opts, sam, sa, out, disp):
    """Creates/opens a key."""
    if disp:
        m.w32(disp, 1)
    return _open(m, root, m.cstr(sub).decode("latin-1"), out, True)


@api(A, 1)
def RegCloseKey(m, h):
    """Forgets the handle."""
    reg(m).handles.pop(h, None)
    return 0


@api(A, 1)
def RegFlushKey(m, h):
    """0."""
    return 0


@api(A, 6)
def RegQueryValueExA(m, h, name, res, typ_p, data_p, size_p):
    """Reads a stored value (ERROR_MORE_DATA when the buffer is short)."""
    r = reg(m)
    p = r.handles.get(h) or HKEY_ROOTS.get(h, "").upper()
    vals = r.keys.get(p, {})
    n = m.cstr(name).decode("latin-1").upper() if name else ""
    if n not in vals:
        return ERROR_FILE_NOT_FOUND
    typ, data = vals[n]
    if typ_p:
        m.w32(typ_p, typ)
    if size_p:
        cap = m.r32(size_p)
        m.w32(size_p, len(data))
        if data_p:
            if cap < len(data):
                return ERROR_MORE_DATA
            m.write(data_p, data)
    return 0


@api(A, 6)
def RegSetValueExA(m, h, name, res, typ, data, size):
    """Stores the value in memory."""
    r = reg(m)
    p = r.handles.get(h)
    if p is None:
        return 6
    n = m.cstr(name).decode("latin-1").upper() if name else ""
    r.keys.setdefault(p, {})[n] = (typ, m.read(data, size) if size else b"")
    m.services.setdefault("registry_writes", []).append((p, n))
    return 0


@api(A, 2)
def RegDeleteValueA(m, h, name):
    """Deletes a stored value."""
    r = reg(m)
    p = r.handles.get(h) or HKEY_ROOTS.get(h, "").upper()
    n = m.cstr(name).decode("latin-1").upper() if name else ""
    return 0 if r.keys.get(p, {}).pop(n, None) is not None else ERROR_FILE_NOT_FOUND


@api(A, 8)
def RegEnumKeyExA(m, h, idx, name, name_size, res, cls, cls_size, ft):
    """Enumerates direct subkeys in sorted order."""
    r = reg(m)
    p = r.handles.get(h)
    subs = sorted({k[len(p) + 1:].split("\\")[0] for k in r.keys if p and k.startswith(p + "\\")})
    if idx >= len(subs):
        return ERROR_NO_MORE_ITEMS
    s = subs[idx].encode("latin-1")
    m.write(name, s + b"\0")
    m.w32(name_size, len(s))
    return 0


@api(A, 12)
def RegQueryInfoKeyA(m, h, cls, cls_size, res, nsub, maxsub, maxcls, nval, maxname, maxval, sec, ft):
    """Counts of subkeys and values."""
    r = reg(m)
    p = r.handles.get(h, "")
    vals = r.keys.get(p, {})
    for ptr, v in ((nsub, 0), (maxsub, 64), (maxcls, 0), (nval, len(vals)), (maxname, 64), (maxval, 1024)):
        if ptr:
            m.w32(ptr, v)
    return 0


@api(A, 2)
def GetUserNameA(m, buf, size_p):
    """"Player"."""
    m.write(buf, b"Player\0")
    m.w32(size_p, 7)
    return 1


for _n, _k in (("InitializeAcl", 3), ("CheckTokenMembership", 3), ("OpenProcessToken", 3), ("GetTokenInformation", 5),
               ("AllocateAndInitializeSid", 11), ("AddAccessDeniedAce", 4), ("AddAccessAllowedAce", 4)):
    def _sec(m, *a):
        """Security APIs fail (FALSE); the process-access hardening step is then skipped."""
        set_error(m, 5)
        return 0
    api(A, _k, name=_n)(_sec)


@api(A, 1)
def FreeSid(m, p):
    """NULL."""
    return 0


# ----------------------------------------------------------------------------- VERSION
V = "version.dll"


def _version_block(m, path: str):
    """VS_VERSIONINFO resource of a loaded original image (by file name)."""
    from .kernel32 import _resource_entry
    name = path.replace("/", "\\").rsplit("\\", 1)[-1].lower()
    img = m.find_image(name)
    if img is None:
        return None
    found = _resource_entry(img, 16, 1)
    if not found:
        return None
    rva, size = found
    return m.read(img.base + rva, size)


@api(V, 2)
def GetFileVersionInfoSizeA(m, path, handle):
    """Size of the image's real RT_VERSION resource (loaded original images only)."""
    if handle:
        m.w32(handle, 0)
    b = _version_block(m, m.cstr(path).decode("latin-1"))
    return len(b) if b else 0


@api(V, 4)
def GetFileVersionInfoA(m, path, handle, n, buf):
    """Copies the RT_VERSION resource."""
    b = _version_block(m, m.cstr(path).decode("latin-1"))
    if not b:
        return 0
    m.write(buf, b[:n])
    return 1


@api(V, 4)
def VerQueryValueA(m, block, sub, out, out_len):
    """"\\" returns the VS_FIXEDFILEINFO (offset 40); other paths are not supported (FALSE)."""
    s = m.cstr(sub)
    if s == b"\\":
        m.w32(out, block + 40)
        m.w32(out_len, 52)
        return 1
    log.info("VerQueryValueA(%r) unsupported", s)
    return 0


# ----------------------------------------------------------------------------- SHELL32
S = "shell32.dll"
CSIDL = {5: vfs.DOCUMENTS_DIR, 0x1C: vfs.LOCALAPPDATA_DIR, 0x1A: vfs.APPDATA_DIR, 0x28: vfs.USER_DIR,
         0x23: "C:\\ProgramData", 0x26: "C:\\Program Files", 0x2A: "C:\\Program Files"}


@api(S, 5)
def SHGetFolderPathA(m, hwnd, csidl, token, flags, path):
    """Personal = C:\\Users\\Player\\Documents, (Local) AppData under C:\\Users\\Player\\AppData;
    other folders fail (E_FAIL)."""
    p = CSIDL.get(csidl & 0xFF)
    if p is None:
        return 0x80004005
    m.write(path, p.encode() + b"\0")
    return 0


@api(S, 3)
def SHGetSpecialFolderLocation(m, hwnd, csidl, out):
    """Returns a PIDL that is really the folder path string (only SHGetPathFromIDListA reads it)."""
    p = CSIDL.get(csidl & 0xFF)
    if p is None:
        return 0x80004005
    m.w32(out, m.alloc_str(p.encode()))
    return 0


@api(S, 2)
def SHGetPathFromIDListA(m, pidl, out):
    """Copies the path string."""
    m.write(out, m.cstr(pidl) + b"\0")
    return 1


@api(S, 3)
def SHCreateDirectoryExA(m, hwnd, path, sa):
    """Creates the overlay directory chain."""
    m.services["fs"].mkdirs(m.cstr(path).decode("latin-1"))
    return 0


@api(S, 6)
def ShellExecuteA(m, hwnd, op, f, params, d, show):
    """Fails (returns SE_ERR_FNF = 2); nothing is launched."""
    return 2


@api(S, 3)
def FindExecutableA(m, f, d, out):
    """Fails (SE_ERR_FNF)."""
    return 2


# ----------------------------------------------------------------------------- IMM32
I = "imm32.dll"


@api(I, 1)
def ImmGetContext(m, hwnd):
    """NULL (no input method)."""
    return 0


@api(I, 2)
def ImmReleaseContext(m, hwnd, ctx):
    """TRUE."""
    return 1


@api(I, 2)
def ImmAssociateContext(m, hwnd, ctx):
    """NULL."""
    return 0


@api(I, 4)
def ImmGetCompositionStringA(m, ctx, idx, buf, n):
    """0 bytes."""
    return 0


@api(I, 6)
def ImmSetCompositionStringA(m, *a):
    """FALSE."""
    return 0


# ----------------------------------------------------------------------------- WINTRUST / CRYPT32 / PSAPI
@api("wintrust.dll", 3)
def WinVerifyTrust(m, hwnd, action, data):
    """TRUST_E_NOSIGNATURE: signatures are never verified (only the System Survey launcher asks)."""
    return 0x800B0100


for _n, _k in (("CryptQueryObject", 11), ("CertGetNameStringW", 6), ("CertFreeCertificateContext", 1),
               ("CertFindCertificateInStore", 6), ("CryptMsgGetParam", 5), ("CertCloseStore", 2),
               ("CryptMsgClose", 1)):
    def _crypt(m, *a):
        """Certificate APIs fail (FALSE/NULL)."""
        return 0
    api("crypt32.dll", _k, name=_n)(_crypt)


@api("psapi.dll", 4)
def GetModuleFileNameExA(m, proc, mod, buf, n):
    """Fails (0)."""
    return 0


# ----------------------------------------------------------------------------- WINMM
W = "winmm.dll"


@api(W, 0)
def timeGetTime(m):
    """Virtual milliseconds (same clock as GetTickCount)."""
    from .kernel32 import GetTickCount
    return GetTickCount(m)


@api(W, 1)
def timeBeginPeriod(m, p):
    """TIMERR_NOERROR."""
    return 0


@api(W, 1)
def timeEndPeriod(m, p):
    """TIMERR_NOERROR."""
    return 0


@api(W, 5)
def timeSetEvent(m, delay, res, proc, user, flags):
    """Multimedia timers are not provided (NULL)."""
    return 0


@api(W, 1)
def timeKillEvent(m, i):
    """MMSYSERR_INVALPARAM."""
    return 11


for _n, _k in (("waveOutOpen", 6), ("waveOutClose", 1), ("waveOutReset", 1), ("waveOutSetVolume", 2),
               ("waveOutGetID", 2), ("waveOutPrepareHeader", 3), ("waveOutUnprepareHeader", 3), ("waveOutWrite", 3)):
    def _wave(m, *a):
        """No wave output device (MMSYSERR_NODRIVER = 6)."""
        return 6
    api(W, _k, name=_n)(_wave)


@api(W, 0)
def waveOutGetNumDevs(m):
    """0 devices."""
    return 0


# ----------------------------------------------------------------------------- TAPI32 (modem provider)
for _n, _k in (("lineInitialize", 5), ("lineShutdown", 1), ("lineNegotiateAPIVersion", 6), ("lineGetDevCaps", 5),
               ("lineOpen", 9), ("lineClose", 1), ("lineMakeCall", 5), ("lineAnswer", 3), ("lineDrop", 3),
               ("lineDeallocateCall", 1), ("lineGetCallStatus", 2), ("lineGetID", 6), ("lineTranslateAddress", 7)):
    def _tapi(m, *a):
        """Telephony is unavailable (LINEERR_NODRIVER)."""
        return 0x80000043
    api("tapi32.dll", _k, name=_n)(_tapi)
