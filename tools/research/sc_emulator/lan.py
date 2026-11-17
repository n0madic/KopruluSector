"""The virtual LAN of the network oracle (phase N, stage N1): several emulated originals on one UDP/IP network.

Each emulator instance is a host with a virtual IPv4 address. The guest's Winsock calls (hle/ws2_32.py) work on the
host's own stack (`NetStack`): its sockets, their bound ports, the datagrams that arrived and what it sent since the
last synchronization. The instances run in processes of their own (net.py); a coordinator in the parent process
(`Coordinator`) carries the sent datagrams between them through a switch (`VirtualLan`) and keeps their virtual
clocks together.

Time. Every machine has its own virtual clock (machine.py), all starting at 0, so the clocks are one time line. A
datagram sent at time t arrives at t + delay + jitter (the switch's seeded draws), never earlier than t + delay: the
coordinator's conservative rounds use that minimum delay as their lookahead. In a round each instance may run until
its clock reaches its horizon, the smallest clock of the others plus the lookahead; whatever another instance sends in
that round arrives at or after the horizon, so no instance runs past a datagram it should have seen. The instances
of a round run at the same time, each in its process; the coordinator waits for all of them, routes their datagrams
in a fixed order (instance index, then send order) and starts the next round. Everything an instance sees depends
only on its own run and on what the rounds delivered, so a game is reproducible by its seed. A clock charge can take
an instance a little past its horizon; a datagram due in that overshoot is seen at the next round, a little late,
and still deterministically.

A host's own datagrams (to its address, the loopback or a broadcast, which Windows also loops back to the sender's
sockets) are delivered at once inside the host; a broadcast also goes out to every other host.

`VirtualLan` also writes the records: every sent datagram as an IPv4/UDP packet in a pcap file (LINKTYPE_IPV4,
time = the virtual send time from 2000-01-01, the emulator's epoch) and one JSON line per datagram with its
deliveries, delays and losses (`lan.jsonl`).

`LoopbackBridge` puts real UDP peers on the virtual LAN (live checks of Koprulu against the emulated original): each
emulated host's Storm ports 6111 and 6112 are real sockets on 127.0.0.1 (base + 2 * host index + 0 or 1), and an
external peer is a virtual IP whose two Storm ports are real loopback ports. Bridged traffic is not deterministic.

Nothing here needs the emulator (no unicorn): tools/tests/test_emulator_net.py runs it alone.
"""
from __future__ import annotations

import json
import logging
import random
import socket
import struct
import time
from dataclasses import dataclass, field
from pathlib import Path

log = logging.getLogger(__name__)

AF_INET, SOCK_DGRAM, IPPROTO_UDP = 2, 2, 17
INADDR_ANY, INADDR_BROADCAST, INADDR_LOOPBACK = 0, 0xFFFFFFFF, 0x7F000001
STORM_PORTS = (6111, 6112)            # standard.snp's UDP provider: 6111 games and their queries, 6112 game traffic
EPHEMERAL_FIRST = 49152
MAX_UDP_PAYLOAD = 65507
EPOCH_UNIX = 946_684_800              # the emulator's virtual time 0 (hle/kernel32.py)
PCAP_LINKTYPE_IPV4 = 228
DEFAULT_DELAY_US = 2000               # a datagram's minimum delay: also the coordinator's lookahead
DEFAULT_JITTER_US = 1000
SOLO_QUANTUM_US = 50_000              # a round's length when one instance is left


def ip_str(ip: int) -> str:
    return socket.inet_ntoa(struct.pack(">I", ip))


def ip_int(text: str) -> int:
    return struct.unpack(">I", socket.inet_aton(text))[0]


@dataclass
class Datagram:
    """One UDP datagram. Addresses in host order; times in virtual microseconds."""
    src_ip: int
    src_port: int
    dst_ip: int
    dst_port: int
    data: bytes
    sent_us: int
    arrival_us: int = 0
    order: int = 0          # the receiving stack's insertion number: ties of arrival keep it
    src_host: str = ""

    def wire(self) -> tuple:
        return (self.src_ip, self.src_port, self.dst_ip, self.dst_port, self.data, self.sent_us, self.arrival_us,
                self.src_host)

    @classmethod
    def from_wire(cls, t: tuple) -> "Datagram":
        return cls(*t[:7], src_host=t[7])


@dataclass
class UdpSocket:
    handle: int
    addr: tuple[int, int] | None = None     # bound (ip, port)
    broadcast: bool = False
    nonblocking: bool = False
    options: dict = field(default_factory=dict)
    queue: list[Datagram] = field(default_factory=list)   # sorted by (arrival, order)
    closed: bool = False

    def first_ready(self, now: int) -> Datagram | None:
        return self.queue[0] if self.queue and self.queue[0].arrival_us <= now else None


class NetError(Exception):
    """A Winsock error code for the calling thread (hle/ws2_32.py turns it into SOCKET_ERROR)."""

    def __init__(self, code: int):
        super().__init__(code)
        self.code = code


WSAEACCES, WSAEINVAL, WSAEMSGSIZE, WSAEADDRINUSE, WSAEADDRNOTAVAIL = 10013, 10022, 10040, 10048, 10049


class NetStack:
    """One host's UDP/IP: its address, its sockets and the datagrams it sent since the last synchronization."""

    def __init__(self, ip: int, hostname: str = "koprulu"):
        self.ip = ip
        self.hostname = hostname
        self.sockets: dict[int, UdpSocket] = {}
        self.outbox: list[Datagram] = []
        self.started = 0                  # WSAStartup calls not yet undone by WSACleanup
        self.next_ephemeral = EPHEMERAL_FIRST
        self.received = 0                 # insertion counter (UdpSocket.queue order)
        self.sent = 0
        self.dropped_local = 0            # datagrams for a port no socket here is bound to

    def ours(self, ip: int) -> bool:
        return ip in (self.ip, INADDR_LOOPBACK)

    def bind(self, sock: UdpSocket, ip: int, port: int) -> None:
        if sock.addr is not None:
            raise NetError(WSAEINVAL)
        if ip not in (INADDR_ANY, self.ip, INADDR_LOOPBACK):
            raise NetError(WSAEADDRNOTAVAIL)
        if port == 0:
            port = self._ephemeral()
        elif any(s.addr is not None and s.addr[1] == port and not s.closed for s in self.sockets.values()):
            raise NetError(WSAEADDRINUSE)
        sock.addr = (ip, port)

    def _ephemeral(self) -> int:
        used = {s.addr[1] for s in self.sockets.values() if s.addr is not None}
        while self.next_ephemeral in used:
            self.next_ephemeral += 1
        port = self.next_ephemeral
        self.next_ephemeral += 1
        return port

    def send(self, sock: UdpSocket, data: bytes, dst_ip: int, dst_port: int, now: int) -> None:
        """A datagram leaves: local copies are delivered now, the rest waits in the outbox for the coordinator."""
        if len(data) > MAX_UDP_PAYLOAD:
            raise NetError(WSAEMSGSIZE)
        if dst_ip == INADDR_BROADCAST and not sock.broadcast:
            raise NetError(WSAEACCES)
        if sock.addr is None:
            sock.addr = (INADDR_ANY, self._ephemeral())
        src_ip = sock.addr[0] if sock.addr[0] not in (INADDR_ANY, INADDR_LOOPBACK) else self.ip
        self.sent += 1
        d = Datagram(src_ip, sock.addr[1], dst_ip, dst_port, bytes(data), now)
        if dst_ip == INADDR_BROADCAST or self.ours(dst_ip):
            self.deliver(Datagram(src_ip, d.src_port, dst_ip, dst_port, d.data, now, now))
        self.outbox.append(d)  # every datagram: the coordinator records the local ones and routes the rest

    def deliver(self, d: Datagram) -> bool:
        """Queues an arriving datagram at the socket bound to its port (any address, or ours); False: no such socket."""
        for s in self.sockets.values():
            if s.closed or s.addr is None or s.addr[1] != d.dst_port:
                continue
            if s.addr[0] not in (INADDR_ANY, self.ip) and not (s.addr[0] == INADDR_LOOPBACK and self.ours(d.dst_ip)):
                continue
            self.received += 1
            d.order = self.received
            q = s.queue
            k = len(q)
            while k and (q[k - 1].arrival_us, q[k - 1].order) > (d.arrival_us, d.order):
                k -= 1
            q.insert(k, d)
            return True
        self.dropped_local += 1
        return False

    def next_due_us(self, now: int) -> int | None:
        """The earliest arrival still in the future (the machine's idle clock jumps to it)."""
        future = [next((d.arrival_us for d in s.queue if d.arrival_us > now), None)
                  for s in self.sockets.values() if not s.closed]   # each queue is sorted by arrival
        future = [t for t in future if t is not None]
        return min(future) if future else None

    def take_outbox(self) -> list[Datagram]:
        out, self.outbox = self.outbox, []
        return out


@dataclass
class HostInfo:
    name: str
    ip: int
    index: int


class VirtualLan:
    """The switch: who gets a datagram, when, or that it is lost (seeded); the cut hosts; the records."""

    def __init__(self, seed: int = 1, delay_us: int = DEFAULT_DELAY_US, jitter_us: int = DEFAULT_JITTER_US,
                 loss: float = 0.0, out: Path | None = None):
        if delay_us <= 0:
            raise ValueError("the minimum delay must be positive (it is the rounds' lookahead)")
        self.seed, self.delay_us, self.jitter_us, self.loss = seed, delay_us, jitter_us, loss
        self.rng = random.Random(seed)
        self.hosts: list[HostInfo] = []
        self.cuts: dict[str, int] = {}          # host name -> virtual time from which nothing reaches or leaves it
        self.count = 0
        self.stats = {"sent": 0, "delivered": 0, "lost": 0, "cut": 0, "unreachable": 0}
        self._pcap = self._jsonl = None
        if out is not None:
            out.mkdir(parents=True, exist_ok=True)
            self._pcap = open(out / "lan.pcap", "wb")
            self._pcap.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, PCAP_LINKTYPE_IPV4))
            self._jsonl = open(out / "lan.jsonl", "w")

    def add_host(self, name: str, ip: int) -> HostInfo:
        if any(h.ip == ip or h.name == name for h in self.hosts):
            raise ValueError(f"host {name} {ip_str(ip)} is already on the LAN")
        h = HostInfo(name, ip, len(self.hosts))
        self.hosts.append(h)
        return h

    def host(self, name: str) -> HostInfo:
        return next(h for h in self.hosts if h.name == name)

    def cut(self, name: str, at_us: int) -> None:
        """From `at_us` on nothing reaches the host or leaves it (its cable is pulled)."""
        self.cuts[name] = at_us

    def _is_cut(self, name: str, t: int) -> bool:
        return name in self.cuts and t >= self.cuts[name]

    def route(self, d: Datagram, src: str) -> list[tuple[str, Datagram]]:
        """The copies of a datagram the other hosts get: [(host name, datagram with its arrival)]. A broadcast goes to
        every host but the sender (which delivered its own copy); a unicast to the host with that address. The draws
        (loss, then jitter) are made per copy in host order, so the seed fixes the outcome."""
        self.count += 1
        self.stats["sent"] += 1
        targets = [h for h in self.hosts if h.name != src] if d.dst_ip == INADDR_BROADCAST else \
            [h for h in self.hosts if h.ip == d.dst_ip and h.name != src]
        rec = {"n": self.count, "t": d.sent_us, "src": f"{ip_str(d.src_ip)}:{d.src_port}",
               "dst": f"{ip_str(d.dst_ip)}:{d.dst_port}", "len": len(d.data), "from": src, "to": []}
        out = []
        if not targets:
            self.stats["unreachable"] += 1
        for h in targets:
            if self._is_cut(src, d.sent_us) or self._is_cut(h.name, d.sent_us):
                self.stats["cut"] += 1
                rec["to"].append({"host": h.name, "cut": True})
                continue
            lost = self.rng.random() < self.loss if self.loss > 0 else False
            jitter = self.rng.randrange(self.jitter_us + 1) if self.jitter_us > 0 else 0
            if lost:
                self.stats["lost"] += 1
                rec["to"].append({"host": h.name, "lost": True})
                continue
            arrival = d.sent_us + self.delay_us + jitter
            self.stats["delivered"] += 1
            rec["to"].append({"host": h.name, "at": arrival})
            out.append((h.name, Datagram(d.src_ip, d.src_port, d.dst_ip, d.dst_port, d.data, d.sent_us, arrival,
                                         src_host=src)))
        self._record(d, rec)
        return out

    def record_local(self, d: Datagram, src: str) -> None:
        """A datagram that stayed inside its host (to itself or the loopback): recorded, never routed."""
        self.count += 1
        self._record(d, {"n": self.count, "t": d.sent_us, "src": f"{ip_str(d.src_ip)}:{d.src_port}",
                         "dst": f"{ip_str(d.dst_ip)}:{d.dst_port}", "len": len(d.data), "from": src, "local": True})

    def _record(self, d: Datagram, rec: dict) -> None:
        if self._pcap is not None:
            self._pcap.write(pcap_record(d, self.count))
        if self._jsonl is not None:
            self._jsonl.write(json.dumps(rec, separators=(",", ":")) + "\n")

    def close(self) -> None:
        for f in (self._pcap, self._jsonl):
            if f is not None:
                f.close()
        self._pcap = self._jsonl = None


def _checksum(header: bytes) -> int:
    s = sum(struct.unpack(f">{len(header) // 2}H", header))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return ~s & 0xFFFF


def pcap_record(d: Datagram, ident: int) -> bytes:
    """One pcap record: an IPv4 header (identification = the datagram's number), a UDP header (no checksum, which
    IPv4 allows) and the payload, stamped with the virtual send time."""
    udp_len = 8 + len(d.data)
    ip = bytearray(struct.pack(">BBHHHBBHII", 0x45, 0, 20 + udp_len, ident & 0xFFFF, 0, 128, IPPROTO_UDP, 0,
                               d.src_ip, d.dst_ip))
    struct.pack_into(">H", ip, 10, _checksum(bytes(ip)))
    packet = bytes(ip) + struct.pack(">HHHH", d.src_port, d.dst_port, udp_len, 0) + d.data
    sec, usec = divmod(d.sent_us, 1_000_000)
    return struct.pack("<IIII", EPOCH_UNIX + sec, usec, len(packet), len(packet)) + packet


def read_pcap(path: Path) -> list[dict]:
    """The records of a pcap written by VirtualLan (tests and summaries)."""
    raw = Path(path).read_bytes()
    magic, _, _, _, _, _, link = struct.unpack_from("<IHHiIII", raw, 0)
    if magic != 0xA1B2C3D4 or link != PCAP_LINKTYPE_IPV4:
        raise ValueError("not a VirtualLan pcap")
    out, pos = [], 24
    while pos < len(raw):
        sec, usec, incl, _ = struct.unpack_from("<IIII", raw, pos)
        pkt = raw[pos + 16:pos + 16 + incl]
        pos += 16 + incl
        ident, src, dst = struct.unpack_from(">H", pkt, 4)[0], *struct.unpack_from(">II", pkt, 12)
        sport, dport, ulen = struct.unpack_from(">HHH", pkt, 20)
        out.append({"t": (sec - EPOCH_UNIX) * 1_000_000 + usec, "id": ident, "src": (src, sport), "dst": (dst, dport),
                    "data": pkt[28:20 + ulen], "ip_ok": _checksum(pkt[:20]) == 0})
    return out


# ---------------------------------------------------------------------------------------------- rounds
class Endpoint:
    """The coordinator's link to one instance: send() a message, recv() its answer (a pipe to its process, or an
    in-process stand-in in the tests)."""

    def send(self, msg: tuple) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def recv(self) -> tuple:  # pragma: no cover - interface
        raise NotImplementedError


@dataclass
class Member:
    name: str
    endpoint: Endpoint
    clock: int = 0
    state: str = "run"         # run | park (its driver finished; it runs on for the others) | exit
    inbox: list = field(default_factory=list)
    result: dict | None = None
    rounds: int = 0


class Coordinator:
    """Conservative rounds over the members (the module's docstring). Protocol:
    member -> coordinator: ("sync", clock_us, [sent datagrams' wire tuples], state "run" | "park") or ("exit", result);
    coordinator -> member: ("go", horizon_us, [arriving datagrams' wire tuples]) or ("stop", reason).
    A member's first message is its sync at the start; after "stop" its next message is its "exit"."""

    def __init__(self, lan: VirtualLan, members: list[Member], *, wall_limit_s: float = 7200,
                 stop_on_exit: bool = True, bridge: "LoopbackBridge | None" = None, realtime: bool = False,
                 events: list | None = None):
        self.lan, self.members = lan, members
        self.wall_limit_s, self.stop_on_exit, self.bridge, self.realtime = wall_limit_s, stop_on_exit, bridge, realtime
        self.events = events if events is not None else []
        self.rounds = 0
        self.reason = ""

    def member(self, name: str) -> Member:
        return next(x for x in self.members if x.name == name)

    def _receive(self, x: Member) -> None:
        msg = x.endpoint.recv()
        if msg[0] == "sync":
            _, clock, sent, state = msg
            x.clock, x.state = clock, state
            for t in sent:
                d = Datagram.from_wire(t)
                if d.dst_ip == INADDR_LOOPBACK or d.dst_ip == self.lan.host(x.name).ip:
                    self.lan.record_local(d, x.name)
                    continue
                for name, copy in self.lan.route(d, x.name):
                    if self.bridge is not None and self.bridge.is_external(name):
                        self.bridge.forward(copy, name)
                    else:
                        self.member(name).inbox.append(copy.wire())
        elif msg[0] == "exit":
            x.state, x.result = "exit", msg[1]
            self.events.append({"t": x.clock, "member": x.name, "exit": msg[1].get("finished", "")})
        else:
            raise ValueError(f"{x.name}: unexpected message {msg[0]!r}")

    def _stop(self, live: list[Member], reason: str) -> None:
        self.reason = reason
        for x in live:
            x.endpoint.send(("stop", reason))
        for x in live:
            while x.state != "exit":
                self._receive(x)

    def run(self) -> str:
        t0 = time.time()
        for x in self.members:                  # each member's first sync
            self._receive(x)
        while True:
            live = [x for x in self.members if x.state != "exit"]
            if not live:
                self.reason = self.reason or "every member exited"
                break
            if self.stop_on_exit and len(live) < len(self.members):
                self._stop(live, "a member exited: " + ", ".join(
                    f"{x.name} ({(x.result or {}).get('finished', '')})" for x in self.members if x.state == "exit"))
                break
            if all(x.state == "park" for x in live):
                self._stop(live, "every driver finished")
                break
            if time.time() - t0 > self.wall_limit_s:
                self._stop(live, "wall-time limit")
                break
            granted = []
            for x in live:
                others = [y.clock for y in live if y is not x]
                horizon = (min(others) + self.lan.delay_us) if others else x.clock + SOLO_QUANTUM_US
                if x.clock < horizon:
                    granted.append((x, horizon))
            if self.realtime:
                lag = min(x.clock for x in live) / 1e6 - (time.time() - t0)
                if lag > 0:
                    time.sleep(lag)
            for x, horizon in granted:
                x.endpoint.send(("go", horizon, x.inbox))
                x.inbox = []
                x.rounds += 1
            for x, _ in granted:
                self._receive(x)
            clocks = [x.clock for x in self.members if x.state != "exit"]
            if self.bridge is not None and clocks:
                for name, d in self.bridge.poll(min(clocks)):
                    self.member(name).inbox.append(d.wire())
            self.rounds += 1
        self.lan.close()
        return self.reason


# ---------------------------------------------------------------------------------------------- bridge
@dataclass
class ExternalPeer:
    name: str
    ip: int
    ports: tuple[int, int]      # the real loopback ports of its virtual ports 6111 and 6112


class LoopbackBridge:
    """Real UDP on 127.0.0.1 for the emulated hosts (live checks): host k's virtual port 6111 + j is the real port
    base + 2k + j. A datagram from a real port of a known peer comes in as that peer's virtual address; an unknown
    real sender becomes a peer of its own (10.0.1.n), the sending port taken as its socket for the 6111 or 6112 it
    was sent to and the adjacent port as its other one (a peer binds 6112's socket next above 6111's, as the
    original's 6111 and 6112 are; a single-socket peer still gets its replies, at the port it sent from). Outgoing
    datagrams to a peer's virtual address (or a broadcast) leave through the sending host's real socket for its
    source port to the peer's real port of the destination's virtual port. Arrivals are stamped with the slowest
    member's clock plus the LAN's delay at the time they are polled."""

    def __init__(self, lan: VirtualLan, base_port: int, peers: list[ExternalPeer] | None = None):
        self.lan, self.base = lan, base_port
        self.peers: dict[str, ExternalPeer] = {}
        self.socks: dict[tuple[str, int], socket.socket] = {}
        for k, h in enumerate(lan.hosts):
            for j, vport in enumerate(STORM_PORTS):
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.bind(("127.0.0.1", base_port + 2 * k + j))
                s.setblocking(False)
                self.socks[(h.name, vport)] = s
        for p in peers or []:
            self.add_peer(p)
        self.forwarded = self.injected = 0

    def add_peer(self, p: ExternalPeer) -> None:
        self.peers[p.name] = p
        self.lan.add_host(p.name, p.ip)

    def is_external(self, name: str) -> bool:
        return name in self.peers

    def _peer_by_real(self, port: int, vport: int) -> ExternalPeer:
        for p in self.peers.values():
            if port in p.ports:
                return p
        # a peer binds its two Storm sockets as the original does, 6112's next above 6111's: the sending socket is
        # the one for the virtual port it sent to, and its other socket is the adjacent port
        ports = (port, port + 1) if vport == STORM_PORTS[0] else (port - 1, port)
        n = len(self.peers) + 1
        p = ExternalPeer(f"peer{n}", ip_int(f"10.0.1.{n}"), ports)
        log.info("bridge: new external peer %s at 127.0.0.1:%d and %d", p.name, *ports)
        self.add_peer(p)
        return p

    def forward(self, d: Datagram, peer_name: str) -> None:
        p = self.peers[peer_name]
        if d.dst_port not in STORM_PORTS or d.src_port not in STORM_PORTS:
            return
        s = self.socks.get((d.src_host, d.src_port))
        if s is not None:
            s.sendto(d.data, ("127.0.0.1", p.ports[STORM_PORTS.index(d.dst_port)]))
            self.forwarded += 1

    def poll(self, now: int) -> list[tuple[str, Datagram]]:
        out = []
        for (host, vport), s in self.socks.items():
            while True:
                try:
                    data, (_, port) = s.recvfrom(65536)
                except BlockingIOError:
                    break
                p = self._peer_by_real(port, vport)
                src_vport = STORM_PORTS[p.ports.index(port)]
                ip = self.lan.host(host).ip
                out.append((host, Datagram(p.ip, src_vport, ip, vport, data, now, now + self.lan.delay_us,
                                           src_host=p.name)))
                self.injected += 1
        return out

    def close(self) -> None:
        for s in self.socks.values():
            s.close()
