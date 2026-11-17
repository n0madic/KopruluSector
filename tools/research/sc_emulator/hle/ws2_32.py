"""WS2_32 HLE: UDP/IP sockets on the virtual LAN (lan.py), for standard.snp's "Local Area Network (UDP/IP)".

The machine's stack is ``m.services["net"]`` (lan.NetStack: its virtual address, sockets and outbox); without one (a
single-player run) WSAStartup fails with WSASYSNOTREADY, as on a computer without TCP/IP, and the provider is
not offered as usable. What the provider calls (Udp.cpp, its function table filled by GetProcAddress at 0x1d0084a0):
WinSock 1.1 ordinals of wsock32.dll 2 bind, 3 closesocket, 9 htons, 17 recvfrom, 20 sendto, 21 setsockopt, 23
socket, 111 WSAGetLastError, 115 WSAStartup, 116 WSACleanup, and ws2_32.dll's WSAIoctl by name (SIO_UDP_CONNRESET on
both sockets). Its three receive threads block in recvfrom (0x1d0089d0 on port 6111, two at 0x1d008b80 on 6112); the
blocking wait is a cooperative one (machine.Wait with a predicate) that ends when a datagram's arrival time is reached
on the machine's clock or the socket is closed (closesocket wakes it with WSAEINTR, which the provider's shutdown
0x1d007b40 relies on after its wake-up broadcasts). The rest (select, ioctlsocket, getsockname, names and addresses)
completes the BSD subset for other callers. Errors are the thread's last error (WSAGetLastError = GetLastError).
"""
from __future__ import annotations

import logging
import struct

from .. import lan
from ..machine import INFINITE, Wait
from . import api
from .kernel32 import last_error, set_error

log = logging.getLogger(__name__)
W = "ws2_32.dll"

SOCKET_ERROR = 0xFFFFFFFF
INVALID_SOCKET = 0xFFFFFFFF
INADDR_NONE = 0xFFFFFFFF
SOL_SOCKET = 0xFFFF
SO_BROADCAST = 0x20
FIONBIO, FIONREAD = 0x8004667E, 0x4004667F
SIO_UDP_CONNRESET = 0x9800000C
WSADATA_SIZE = 400
FD_SETSIZE = 64

WSAEINTR, WSAEFAULT, WSAEINVAL, WSAEWOULDBLOCK, WSAENOTSOCK = 10004, 10014, 10022, 10035, 10038
WSAENOTCONN = 10057
WSAEMSGSIZE, WSAEPROTONOSUPPORT, WSAESOCKTNOSUPPORT, WSAEOPNOTSUPP, WSAEAFNOSUPPORT = 10040, 10043, 10044, 10045, 10047
WSASYSNOTREADY, WSAVERNOTSUPPORTED, WSANOTINITIALISED = 10091, 10092, 10093


def stack(m) -> lan.NetStack | None:
    return m.services.get("net")


def _sock(m, s) -> lan.UdpSocket | None:
    st = stack(m)
    if st is None or not st.started:
        set_error(m, WSANOTINITIALISED)
        return None
    sock = st.sockets.get(s)
    if sock is None or sock.closed:
        set_error(m, WSAENOTSOCK)
        return None
    return sock


def _read_sockaddr(m, p, n) -> tuple[int, int] | None:
    if not p or n < 16:
        set_error(m, WSAEFAULT)
        return None
    family, port, addr = struct.unpack("<HHI", m.read(p, 8))
    if family != lan.AF_INET:
        set_error(m, WSAEAFNOSUPPORT)
        return None
    return struct.unpack(">I", struct.pack("<I", addr))[0], struct.unpack(">H", struct.pack("<H", port))[0]


def _sockaddr_room(m, p, lenp) -> bool:
    """An address buffer, if given, needs its length and room for a sockaddr_in; else WSAEFAULT."""
    if p and (not lenp or m.rs32(lenp) < 16):
        set_error(m, WSAEFAULT)
        return False
    return True


def _write_sockaddr(m, p, lenp, ip: int, port: int) -> None:
    """Callers checked the room first (_sockaddr_room)."""
    if not p:
        return
    m.w32(lenp, 16)
    m.write(p, struct.pack("<H", lan.AF_INET) + struct.pack(">HI", port, ip) + bytes(8))


@api(W, 2, ordinal=115)
def WSAStartup(m, version, data):
    """Version 1.1 to 2.2 (the word's low byte major): fills the 400-byte WSADATA (wVersion = the requested version up
    to 2.2, wHighVersion 2.2, "WinSock 2.0", "Running", 0 sockets limit, 65467-byte datagrams) and counts the start-up.
    No virtual LAN on this machine: WSASYSNOTREADY."""
    st = stack(m)
    if st is None:
        return WSASYSNOTREADY
    major, minor = version & 0xFF, (version >> 8) & 0xFF
    if major < 1 or (major == 1 and minor < 1):
        return WSAVERNOTSUPPORTED
    use = version & 0xFFFF if (major, minor) <= (2, 2) else 0x0202
    if data:
        m.write(data, struct.pack("<HH", use, 0x0202) + b"WinSock 2.0".ljust(257, b"\0") + b"Running".ljust(129, b"\0")
                + struct.pack("<HHI", 0, 65467, 0) + bytes(2))
    st.started += 1
    return 0


@api(W, 0, ordinal=116)
def WSACleanup(m):
    """Undoes one WSAStartup; the last one closes every socket (their blocked receivers get WSAEINTR)."""
    st = stack(m)
    if st is None or not st.started:
        set_error(m, WSANOTINITIALISED)
        return SOCKET_ERROR
    st.started -= 1
    if not st.started:
        for h, s in st.sockets.items():
            s.closed = True
            s.queue.clear()
            m.handles.pop(h, None)
        st.sockets.clear()
    return 0


@api(W, 0, ordinal=111)
def WSAGetLastError(m):
    """The calling thread's last error (Winsock shares it with GetLastError)."""
    return last_error(m)


@api(W, 1, ordinal=112)
def WSASetLastError(m, code):
    """Sets the calling thread's last error."""
    set_error(m, code)


@api(W, 3, ordinal=23)
def socket(m, af, kind, proto):
    """AF_INET datagram sockets (protocol 0 or IPPROTO_UDP) only: other families WSAEAFNOSUPPORT (so IPX finds no
    IPX), other types WSAESOCKTNOSUPPORT. The handle comes from the machine's handle space."""
    st = stack(m)
    if st is None or not st.started:
        set_error(m, WSANOTINITIALISED)
        return INVALID_SOCKET
    if af != lan.AF_INET:
        set_error(m, WSAEAFNOSUPPORT)
        return INVALID_SOCKET
    if kind != lan.SOCK_DGRAM:
        set_error(m, WSAESOCKTNOSUPPORT)
        return INVALID_SOCKET
    if proto not in (0, lan.IPPROTO_UDP):
        set_error(m, WSAEPROTONOSUPPORT)
        return INVALID_SOCKET
    sock = lan.UdpSocket(0)
    h = m.new_handle(sock)
    sock.handle = h
    st.sockets[h] = sock
    return h


@api(W, 3, ordinal=2)
def bind(m, s, addr, n):
    """Binds to INADDR_ANY, the host's address or the loopback; port 0 takes an ephemeral port from 49152. A port
    in use: WSAEADDRINUSE; a bound socket: WSAEINVAL; another address: WSAEADDRNOTAVAIL."""
    sock = _sock(m, s)
    if sock is None:
        return SOCKET_ERROR
    a = _read_sockaddr(m, addr, n)
    if a is None:
        return SOCKET_ERROR
    try:
        stack(m).bind(sock, *a)
    except lan.NetError as e:
        set_error(m, e.code)
        return SOCKET_ERROR
    log.info("bind %#x to %s:%d", s, lan.ip_str(sock.addr[0]), sock.addr[1])
    return 0


@api(W, 1, ordinal=3)
def closesocket(m, s):
    """Closes the socket: its queued datagrams are discarded and a thread blocked in recvfrom on it wakes with
    WSAEINTR."""
    sock = _sock(m, s)
    if sock is None:
        return SOCKET_ERROR
    sock.closed = True
    sock.queue.clear()
    stack(m).sockets.pop(s, None)
    m.handles.pop(s, None)
    return 0


@api(W, 5, ordinal=21)
def setsockopt(m, s, level, opt, val, n):
    """SOL_SOCKET SO_BROADCAST sets the broadcast permission (sendto to 255.255.255.255 needs it); any other option is
    kept as given and has no effect."""
    sock = _sock(m, s)
    if sock is None:
        return SOCKET_ERROR
    if not val and n:
        set_error(m, WSAEFAULT)
        return SOCKET_ERROR
    raw = m.read(val, n) if val and n else b""
    sock.options[(level, opt)] = raw
    if level == SOL_SOCKET and opt == SO_BROADCAST:
        sock.broadcast = bool(raw) and any(raw)
    return 0


@api(W, 5, ordinal=7)
def getsockopt(m, s, level, opt, val, lenp):
    """The value setsockopt stored (SO_BROADCAST as a DWORD; others as given, zero DWORD if never set)."""
    sock = _sock(m, s)
    if sock is None:
        return SOCKET_ERROR
    if level == SOL_SOCKET and opt == SO_BROADCAST:
        raw = struct.pack("<I", int(sock.broadcast))
    else:
        raw = sock.options.get((level, opt), bytes(4))
    n = min(m.rs32(lenp), len(raw)) if lenp else 0
    m.write(val, raw[:n])
    if lenp:
        m.w32(lenp, n)
    return 0


@api(W, 6, ordinal=20)
def sendto(m, s, buf, n, flags, to, tolen):
    """Sends one datagram at the machine's virtual time (lan.NetStack.send: copies for this host are delivered at
    once, the rest leaves at the next synchronization); returns the length. A broadcast needs SO_BROADCAST
    (WSAEACCES); an unbound socket gets an ephemeral port."""
    sock = _sock(m, s)
    if sock is None:
        return SOCKET_ERROR
    a = _read_sockaddr(m, to, tolen)
    if a is None:
        return SOCKET_ERROR
    try:
        stack(m).send(sock, m.read(buf, n) if n else b"", a[0], a[1], m.clock_us)
    except lan.NetError as e:
        set_error(m, e.code)
        return SOCKET_ERROR
    return n


@api(W, 4, ordinal=19)
def send(m, s, buf, n, flags):
    """The datagram sockets are never connected (there is no connect): WSAENOTCONN."""
    set_error(m, WSAENOTCONN)
    return SOCKET_ERROR


def _receive(m, sock, buf, n, frm, frmlen):
    d = sock.queue.pop(0)
    k = min(n, len(d.data))
    if k:
        m.write(buf, d.data[:k])
    _write_sockaddr(m, frm, frmlen, d.src_ip, d.src_port)
    if k < len(d.data):
        set_error(m, WSAEMSGSIZE)
        return SOCKET_ERROR
    return k


@api(W, 6, ordinal=17)
def recvfrom(m, s, buf, n, flags, frm, frmlen):
    """The first datagram whose arrival time has come (by arrival, then delivery order), with its sender's address.
    A blocking socket waits cooperatively (other threads and, idle, the clock run on); a non-blocking one returns
    WSAEWOULDBLOCK. A longer datagram fills the buffer and fails with WSAEMSGSIZE (it is consumed). A close while
    waiting: WSAEINTR. An address buffer without room for a sockaddr_in: WSAEFAULT, nothing consumed."""
    sock = _sock(m, s)
    if sock is None or not _sockaddr_room(m, frm, frmlen):
        return SOCKET_ERROR
    if sock.first_ready(m.clock_us) is None:
        if sock.nonblocking:
            set_error(m, WSAEWOULDBLOCK)
            return SOCKET_ERROR
        yield Wait(predicate=lambda: sock.closed or sock.first_ready(m.clock_us) is not None)
        if sock.closed:
            set_error(m, WSAEINTR)
            return SOCKET_ERROR
    return _receive(m, sock, buf, n, frm, frmlen)


@api(W, 4, ordinal=16)
def recv(m, s, buf, n, flags):
    """recvfrom without the sender's address."""
    return (yield from recvfrom(m, s, buf, n, flags, 0, 0))


def _fd_set(m, p) -> list[int]:
    if not p:
        return []
    count = min(m.r32(p), FD_SETSIZE)
    return list(struct.unpack(f"<{count}I", m.read(p + 4, 4 * count))) if count else []


def _write_fd_set(m, p, handles: list[int]) -> None:
    if p:
        m.w32(p, len(handles))
        if handles:
            m.write(p + 4, struct.pack(f"<{len(handles)}I", *handles))


@api(W, 5, ordinal=18)
def select(m, nfds, readfds, writefds, exceptfds, timeout):
    """Readable: a datagram has arrived (or the socket was closed); writable: always (UDP); exceptions: never. A null
    timeout waits until one is ready, a zero one polls; the wait is cooperative on the virtual clock. Returns the
    number of ready handles and leaves only them in the sets."""
    st = stack(m)
    if st is None or not st.started:
        set_error(m, WSANOTINITIALISED)
        return SOCKET_ERROR
    reads, writes = _fd_set(m, readfds), _fd_set(m, writefds)
    for h in reads + writes + _fd_set(m, exceptfds):
        if h not in st.sockets:
            set_error(m, WSAENOTSOCK)
            return SOCKET_ERROR

    def ready_reads():
        return [h for h in reads if st.sockets.get(h) is None or st.sockets[h].closed
                or st.sockets[h].first_ready(m.clock_us) is not None]

    if not ready_reads() and not writes:
        ms = INFINITE
        if timeout:
            sec, usec = struct.unpack("<ii", m.read(timeout, 8))
            ms = max(0, sec * 1000 + usec // 1000)
        if ms != 0:
            yield Wait(timeout_ms=ms, predicate=lambda: bool(ready_reads()))
    r = ready_reads()
    _write_fd_set(m, readfds, r)
    _write_fd_set(m, writefds, writes)
    _write_fd_set(m, exceptfds, [])
    return len(r) + len(writes)


@api(W, 3, ordinal=10)
def ioctlsocket(m, s, cmd, argp):
    """FIONBIO sets or clears non-blocking mode; FIONREAD gives the size of the first arrived datagram (0 if none)."""
    sock = _sock(m, s)
    if sock is None:
        return SOCKET_ERROR
    if cmd == FIONBIO:
        sock.nonblocking = bool(m.r32(argp))
        return 0
    if cmd == FIONREAD:
        d = sock.first_ready(m.clock_us)
        m.w32(argp, len(d.data) if d else 0)
        return 0
    set_error(m, WSAEINVAL)
    return SOCKET_ERROR


@api(W, 9)
def WSAIoctl(m, s, code, inbuf, inlen, outbuf, outlen, retp, overlapped, completion):
    """SIO_UDP_CONNRESET (standard.snp turns ICMP port-unreachable resets off on both sockets): accepted, no effect,
    0 bytes returned. Other codes: WSAEOPNOTSUPP."""
    sock = _sock(m, s)
    if sock is None:
        return SOCKET_ERROR
    if code == SIO_UDP_CONNRESET:
        if retp:
            m.w32(retp, 0)
        return 0
    set_error(m, WSAEOPNOTSUPP)
    return SOCKET_ERROR


@api(W, 3, ordinal=6)
def getsockname(m, s, addr, lenp):
    """The bound address (INADDR_ANY stays 0.0.0.0, as on Windows); unbound: WSAEINVAL."""
    sock = _sock(m, s)
    if sock is None:
        return SOCKET_ERROR
    if sock.addr is None:
        set_error(m, WSAEINVAL)
        return SOCKET_ERROR
    if not addr or not _sockaddr_room(m, addr, lenp):
        set_error(m, WSAEFAULT)
        return SOCKET_ERROR
    _write_sockaddr(m, addr, lenp, *sock.addr)
    return 0


@api(W, 1, ordinal=9)
def htons(m, v):
    """Byte swap of the low 16 bits."""
    return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF)


@api(W, 1, ordinal=15)
def ntohs(m, v):
    """Byte swap of the low 16 bits."""
    return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF)


@api(W, 1, ordinal=8)
def htonl(m, v):
    """32-bit byte swap."""
    return struct.unpack(">I", struct.pack("<I", v & 0xFFFFFFFF))[0]


@api(W, 1, ordinal=14)
def ntohl(m, v):
    """32-bit byte swap."""
    return struct.unpack(">I", struct.pack("<I", v & 0xFFFFFFFF))[0]


@api(W, 1, ordinal=11)
def inet_addr(m, p):
    """Dotted decimal to a network-order address (in memory order); anything else INADDR_NONE."""
    text = m.cstr(p).decode("latin-1").strip()
    parts = text.split(".")
    if len(parts) != 4 or not all(x.isdigit() and int(x) < 256 for x in parts):
        return INADDR_NONE
    return struct.unpack("<I", bytes(int(x) for x in parts))[0]


def _static(m, key: str, size: int) -> int:
    """A per-machine static buffer (Winsock's per-thread ones; the callers copy before the next call)."""
    statics = m.services.setdefault("ws2_static", {})
    if key not in statics:
        statics[key] = m.alloc(size)
    return statics[key]


@api(W, 1, ordinal=12)
def inet_ntoa(m, addr):
    """The address (an in_addr passed by value, memory order) as dotted decimal in a static buffer."""
    text = ".".join(str(b) for b in struct.pack("<I", addr)).encode()
    p = _static(m, "ntoa", 16)
    m.write(p, text + b"\0")
    return p


@api(W, 2, ordinal=57)
def gethostname(m, buf, n):
    """The host's name (lan.NetStack.hostname); a short buffer: WSAEFAULT."""
    st = stack(m)
    if st is None or not st.started:
        set_error(m, WSANOTINITIALISED)
        return SOCKET_ERROR
    name = st.hostname.encode()
    if n <= len(name):
        set_error(m, WSAEFAULT)
        return SOCKET_ERROR
    m.write(buf, name + b"\0")
    return 0


@api(W, 1, ordinal=52)
def gethostbyname(m, p):
    """The host's own name (or "localhost") resolves to its virtual address (or the loopback); a dotted address to
    itself; other names fail (WSAHOST_NOT_FOUND 11001). The HOSTENT is a static buffer."""
    st = stack(m)
    if st is None or not st.started:
        set_error(m, WSANOTINITIALISED)
        return 0
    name = m.cstr(p).decode("latin-1")
    if name.lower() == st.hostname.lower() or name == "":
        ip = st.ip
    elif name.lower() == "localhost":
        ip = lan.INADDR_LOOPBACK
    else:
        try:
            ip = lan.ip_int(name)
        except OSError:
            set_error(m, 11001)
            return 0
    base = _static(m, "hostent", 0x80)
    # HOSTENT {h_name, h_aliases, short h_addrtype, short h_length, h_addr_list}, then the name, the lists, the address
    name_p, aliases, addrs, addr = base + 0x10, base + 0x50, base + 0x58, base + 0x60
    m.write(name_p, name.encode()[:0x3F] + b"\0")
    m.w32(aliases, 0)
    m.write(addrs, struct.pack("<II", addr, 0))
    m.write(addr, struct.pack(">I", ip))
    m.write(base, struct.pack("<IIHHI", name_p, aliases, lan.AF_INET, 4, addrs))
    return base
