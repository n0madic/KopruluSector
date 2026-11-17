"""The network oracle's virtual LAN (tools/research/sc_emulator/lan.py) and its Winsock HLE (hle/ws2_32.py,
hle/wsock32.py).

lan.py is pure Python and is loaded from its file, so its tests (the stacks, the switch's seeded routing, the pcap
records, the coordinator's rounds over in-process stand-ins of the instances, the loopback bridge's real sockets)
need neither the emulator nor Unicorn. The HLE's tests run the Winsock functions on a bare Machine (no game loaded),
which needs unicorn (the research venv); they skip without.
"""
from __future__ import annotations

import importlib.util
import json
import socket
import struct
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESEARCH = ROOT / "tools" / "research"
HAS_UNICORN = importlib.util.find_spec("unicorn") is not None


def _load_lan():
    spec = importlib.util.spec_from_file_location("sc_emulator_lan", RESEARCH / "sc_emulator" / "lan.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod  # dataclasses look their module up
    spec.loader.exec_module(mod)
    return mod


lan = _load_lan()
A, B, C = lan.ip_int("10.0.0.1"), lan.ip_int("10.0.0.2"), lan.ip_int("10.0.0.3")


def _bound(stack, port, broadcast=True):
    s = lan.UdpSocket(len(stack.sockets) + 1, broadcast=broadcast)
    stack.sockets[s.handle] = s
    stack.bind(s, lan.INADDR_ANY, port)
    return s


class NetStackTest(unittest.TestCase):
    def test_bind(self):
        st = lan.NetStack(A)
        s = _bound(st, 6111)
        self.assertEqual(s.addr, (lan.INADDR_ANY, 6111))
        with self.assertRaises(lan.NetError) as e:
            _bound(st, 6111)
        self.assertEqual(e.exception.code, lan.WSAEADDRINUSE)
        self.assertEqual(_bound(st, 0).addr[1], lan.EPHEMERAL_FIRST)
        other = lan.UdpSocket(99)
        with self.assertRaises(lan.NetError) as e:
            st.bind(other, B, 6112)
        self.assertEqual(e.exception.code, lan.WSAEADDRNOTAVAIL)

    def test_broadcast_loops_back_and_goes_out(self):
        st = lan.NetStack(A)
        s = _bound(st, 6111)
        st.send(s, b"query", lan.INADDR_BROADCAST, 6111, now=1000)
        d = s.first_ready(1000)
        self.assertIsNotNone(d)
        self.assertEqual((d.src_ip, d.src_port, d.data, d.arrival_us), (A, 6111, b"query", 1000))
        out = st.take_outbox()
        self.assertEqual([(x.dst_ip, x.dst_port, x.sent_us) for x in out], [(lan.INADDR_BROADCAST, 6111, 1000)])
        self.assertEqual(st.take_outbox(), [])
        quiet = _bound(st, 6112, broadcast=False)
        with self.assertRaises(lan.NetError) as e:
            st.send(quiet, b"x", lan.INADDR_BROADCAST, 6111, now=1000)
        self.assertEqual(e.exception.code, lan.WSAEACCES)

    def test_arrival_order(self):
        st = lan.NetStack(A)
        s = _bound(st, 6112)
        for arrival, data in ((300, b"late"), (100, b"first"), (100, b"second")):
            st.deliver(lan.Datagram(B, 6112, A, 6112, data, 0, arrival))
        self.assertIsNone(s.first_ready(99))
        self.assertEqual(st.next_due_us(0), 100)
        self.assertEqual(st.next_due_us(100), 300)
        self.assertEqual([d.data for d in s.queue], [b"first", b"second", b"late"])
        self.assertFalse(st.deliver(lan.Datagram(B, 6112, A, 7000, b"nobody", 0, 0)))
        self.assertEqual(st.dropped_local, 1)


class VirtualLanTest(unittest.TestCase):
    def _lan(self, seed=7, loss=0.0, out=None):
        v = lan.VirtualLan(seed, delay_us=2000, jitter_us=1000, loss=loss, out=out)
        for name, ip in (("a", A), ("b", B), ("c", C)):
            v.add_host(name, ip)
        return v

    def _routes(self, v, n=60):
        out = []
        for k in range(n):
            d = lan.Datagram(A, 6111, lan.INADDR_BROADCAST if k % 2 else B, 6111, bytes([k]), 1000 * k)
            out.append([(name, x.arrival_us) for name, x in v.route(d, "a")])
        return out

    def test_seeded_routing(self):
        first, again = self._routes(self._lan(loss=0.3)), self._routes(self._lan(loss=0.3))
        self.assertEqual(first, again)
        self.assertNotEqual(first, self._routes(self._lan(seed=8, loss=0.3)))
        for k, copies in enumerate(first):
            self.assertNotIn("a", [name for name, _ in copies])
            if k % 2 == 0:
                self.assertTrue(set(name for name, _ in copies) <= {"b"})
            for _, arrival in copies:
                self.assertTrue(1000 * k + 2000 <= arrival <= 1000 * k + 3000)
        lossless = self._routes(self._lan())
        self.assertEqual(sum(len(c) for c in lossless), 30 * 2 + 30)

    def test_cut(self):
        v = self._lan()
        v.cut("b", 5000)
        before = v.route(lan.Datagram(A, 6112, B, 6112, b"x", 4999), "a")
        after = v.route(lan.Datagram(A, 6112, lan.INADDR_BROADCAST, 6112, b"x", 5000), "a")
        self.assertEqual([n for n, _ in before], ["b"])
        self.assertEqual([n for n, _ in after], ["c"])
        self.assertEqual(v.stats["cut"], 1)

    def test_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            v = self._lan(out=Path(tmp))
            v.route(lan.Datagram(A, 6111, lan.INADDR_BROADCAST, 6111, b"hello", 1_500_000), "a")
            v.record_local(lan.Datagram(A, 6112, A, 6112, b"self", 1_600_000), "a")
            v.close()
            recs = lan.read_pcap(Path(tmp) / "lan.pcap")
            lines = (Path(tmp) / "lan.jsonl").read_text().splitlines()
        self.assertEqual(len(recs), 2)
        r = recs[0]
        self.assertEqual((r["t"], r["src"], r["dst"], r["data"], r["id"]),
                         (1_500_000, (A, 6111), (lan.INADDR_BROADCAST, 6111), b"hello", 1))
        self.assertTrue(all(x["ip_ok"] for x in recs))
        self.assertEqual(len(lines), 2)
        self.assertIn('"local":true', lines[1])


class FakeHost(lan.Endpoint):
    """An instance's stand-in: one socket on 6112; each round it runs to the horizon in 250 µs steps, sending its
    schedule's datagrams at their times and echoing every datagram it receives that is not an echo. It notes a
    datagram delivered after its clock passed the arrival (a late one) and parks once its clock reaches `until`."""

    STEP = 250

    def __init__(self, name, ip, sends, until):
        self.name, self.until = name, until
        self.stack = lan.NetStack(ip, name)
        self.sock = _bound(self.stack, 6112)
        self.clock = 0
        self.sends = sorted(sends)
        self.got: list[tuple] = []
        self.late: list[tuple] = []
        self.answers = [("sync", 0, [], "run")]

    def send(self, msg):
        if msg[0] == "stop":
            self.answers.append(("exit", {"finished": "stopped", "got": len(self.got)}))
            return
        horizon, arriving = msg[1], msg[2]
        for t in arriving:
            d = lan.Datagram.from_wire(t)
            if d.arrival_us < self.clock:
                self.late.append((self.clock, d.arrival_us))
            self.stack.deliver(d)
        while self.clock < horizon:
            self.clock = min(horizon, self.clock + self.STEP)
            while self.sends and self.sends[0][0] <= self.clock:
                t, dst, data = self.sends.pop(0)
                self.stack.send(self.sock, data, dst, 6112, t)
            while (d := self.sock.first_ready(self.clock)) is not None:
                self.sock.queue.pop(0)
                self.got.append((d.arrival_us, d.src_ip, d.data))
                if not d.data.startswith(b"echo") and d.src_ip != self.stack.ip:
                    self.stack.send(self.sock, b"echo " + d.data, d.src_ip, 6112, self.clock)
        state = "park" if self.clock >= self.until else "run"
        self.answers.append(("sync", self.clock, [d.wire() for d in self.stack.take_outbox()], state))

    def recv(self):
        return self.answers.pop(0)


class CoordinatorTest(unittest.TestCase):
    def _game(self, seed=3, out=None):
        v = lan.VirtualLan(seed, delay_us=2000, jitter_us=1500, loss=0.1, out=out)
        hosts = [FakeHost("a", A, [(1000 * k, lan.INADDR_BROADCAST, b"a%d" % k) for k in range(1, 40)], 60_000),
                 FakeHost("b", B, [(1700 * k, A, b"b%d" % k) for k in range(1, 20)], 45_000),
                 FakeHost("c", C, [], 30_000)]
        for h in hosts:
            v.add_host(h.name, h.stack.ip)
        coord = lan.Coordinator(v, [lan.Member(h.name, h) for h in hosts])
        reason = coord.run()
        return reason, hosts, coord

    def test_rounds(self):
        reason, hosts, coord = self._game()
        self.assertEqual(reason, "every driver finished")
        for h in hosts:
            self.assertEqual(h.late, [], h.name)
            self.assertGreaterEqual(h.clock, h.until)
        a, b, c = hosts
        self.assertTrue(any(data.startswith(b"echo a") for _, _, data in a.got))   # b's and c's echoes came back
        self.assertTrue(any(data.startswith(b"b") for _, _, data in a.got))
        self.assertTrue(any(data.startswith(b"echo b") for _, _, data in b.got))
        self.assertGreater(coord.rounds, 10)

    def test_reproducible(self):
        with tempfile.TemporaryDirectory() as t1, tempfile.TemporaryDirectory() as t2:
            _, first, _ = self._game(out=Path(t1))
            _, again, _ = self._game(out=Path(t2))
            self.assertEqual((Path(t1) / "lan.pcap").read_bytes(), (Path(t2) / "lan.pcap").read_bytes())
        self.assertEqual([h.got for h in first], [h.got for h in again])

    def test_exit_stops_the_others(self):
        class Quitter(FakeHost):
            def send(self, msg):
                super().send(msg)
                if msg[0] == "go" and self.clock >= 5000:
                    self.answers[-1] = ("exit", {"finished": "fault: test"})

        v = lan.VirtualLan(1)
        hosts = [Quitter("a", A, [], 10**9), FakeHost("b", B, [], 10**9)]
        for h in hosts:
            v.add_host(h.name, h.stack.ip)
        reason = lan.Coordinator(v, [lan.Member(h.name, h) for h in hosts]).run()
        self.assertTrue(reason.startswith("a member exited: a (fault: test)"), reason)
        self.assertEqual(hosts[1].answers, [])


class LoopbackBridgeTest(unittest.TestCase):
    @staticmethod
    def _free_base() -> int:
        for _ in range(20):
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            probe.bind(("127.0.0.1", 0))
            base = probe.getsockname()[1]
            probe.close()
            other = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                other.bind(("127.0.0.1", base + 1))
                return base
            except OSError:
                continue
            finally:
                other.close()
        raise unittest.SkipTest("no free pair of loopback ports")

    def test_both_ways(self):
        v = lan.VirtualLan(1)
        v.add_host("a", A)
        base = self._free_base()
        bridge = lan.LoopbackBridge(v, base)
        ext = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ext.bind(("127.0.0.1", 0))
        ext.settimeout(2)
        try:
            ext.sendto(b"from outside", ("127.0.0.1", base))          # to a's virtual 6111
            got, deadline = [], time.time() + 2
            while not got and time.time() < deadline:
                got = bridge.poll(5000)
                time.sleep(0.01)
            self.assertEqual(len(got), 1)
            host, d = got[0]
            self.assertEqual((host, d.dst_ip, d.dst_port, d.data, d.arrival_us), ("a", A, 6111, b"from outside", 7000))
            self.assertTrue(bridge.is_external(d.src_host))
            reply = lan.Datagram(A, 6112, d.src_ip, d.src_port, b"from inside", 8000, src_host="a")
            bridge.forward(reply, d.src_host)
            data, (_, port) = ext.recvfrom(100)
            self.assertEqual((data, port), (b"from inside", base + 1))   # sent from a's real 6112
        finally:
            ext.close()
            bridge.close()

    def test_two_socket_peer(self):
        """A peer with the original's two sockets (6112's next above 6111's) is one virtual host: each socket is
        its own Storm port, and a datagram to the peer's 6112 reaches its 6112 socket."""
        v = lan.VirtualLan(1)
        v.add_host("a", A)
        base = self._free_base()
        bridge = lan.LoopbackBridge(v, base)
        peer_base = self._free_base()
        games, packets = (socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2))
        try:
            games.bind(("127.0.0.1", peer_base))
            packets.bind(("127.0.0.1", peer_base + 1))
            packets.settimeout(2)
            games.sendto(b"advert", ("127.0.0.1", base))           # to a's virtual 6111
            packets.sendto(b"connect", ("127.0.0.1", base + 1))    # to a's virtual 6112
            got, deadline = [], time.time() + 2
            while len(got) < 2 and time.time() < deadline:
                got += bridge.poll(5000)
                time.sleep(0.01)
            self.assertEqual(sorted((d.data, d.src_ip, d.src_port, d.dst_port) for _, d in got), [
                (b"advert", got[0][1].src_ip, 6111, 6111), (b"connect", got[0][1].src_ip, 6112, 6112)])
            self.assertEqual(len(bridge.peers), 1)
            peer = got[0][1].src_host
            bridge.forward(lan.Datagram(A, 6112, got[0][1].src_ip, 6112, b"accept", 8000, src_host="a"), peer)
            data, (_, port) = packets.recvfrom(100)
            self.assertEqual((data, port), (b"accept", base + 1))
        finally:
            games.close()
            packets.close()
            bridge.close()


class NetDriverTest(unittest.TestCase):
    """net.py's pure parts: the turns' commands and the games' specifications (no emulator needed)."""

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(RESEARCH))
        from sc_emulator import net
        cls.net = net

    def test_turn_commands(self):
        keep_alive, sync = bytes([0x05]), bytes.fromhex("37416bf3986c12")
        select = bytes([0x09, 2, 1, 0, 2, 0])
        save = bytes([0x06, 1, 0, 0, 0]) + b"netsave\0"
        d = self.net.describe_turn(keep_alive + sync + select + save + keep_alive)
        self.assertEqual((d["size"], d["keepalive"], d["sync"]), (28, 2, ["416bf3986c12"]))
        self.assertEqual(d["commands"], [select.hex(), save.hex()])
        self.assertNotIn("rest", d)
        cmds, rest = self.net.turn_commands(sync + bytes([0xFF, 1, 2]))
        self.assertEqual((cmds, rest), ([sync], bytes([0xFF, 1, 2])))
        self.assertEqual(self.net.turn_commands(sync[:4]), ([], sync[:4]))   # cut short

    def test_spec_round_trip(self):
        net = self.net
        spec = net.GameSpec("g", "/maps/x.scm", [
            net.Player("Alpha", "10.0.0.1", "host", actions=[net.Action("lobby", "chat", "hi")]),
            net.Player("Beta", "10.0.0.2", has_map=False, auto_drop=True,
                       actions=[net.Action(200, "poke", "*665AE0+8:00010000")], preload=[["C:\\a.mlt", "/x"]])],
            state_steps=(1, 251), frames=300)
        self.assertEqual(net.GameSpec.from_json(json.loads(json.dumps(spec.to_json()))), spec)
        self.assertEqual(spec.map_name, "x.scm")

    def test_shared_model(self):
        """Two players' dumps compare equal when only their viewers' data differ; anything else stays a
        difference (the menus' leftovers only in a game's first dump)."""
        net = self.net

        def model(display="0001", image_x=10, flags=0x0084FFFF, turn=40, hp=100):
            return {"terrain": [("0", {"mtxm": "0001", "backup": "0000", "border": "00", "display": display})],
                    "image": [("7", {"id": 431, "frame": 2, "x": image_x, "y": 5})],
                    "tiles": [("0", {"flags": f"{flags:08x}{0x0001FFFF:08x}"})],
                    "step": [("0", {"turn": turn, "elapsed": 3})],
                    "triggers": [("0", {"speed": 4})],
                    "unit": [("0", {"hp": hp})]}

        a = model()
        b = model(display="0002", image_x=12, flags=0x0084FFFF | net.VIEWER_TILE_FLAG)
        self.assertEqual(net.differing([a, b]), ["image", "terrain", "tiles"])
        self.assertEqual(net.differing([net.shared_model(a), net.shared_model(b)]), [])
        c = model(turn=53)
        self.assertEqual(net.differing([net.shared_model(a), net.shared_model(c)]), ["step"])
        self.assertEqual(net.differing([net.shared_model(a, first=True), net.shared_model(c, first=True)]), [])
        d = model(hp=99, flags=0x0084FFFE)  # a unit and a tile's shared bits: a desync
        self.assertEqual(net.differing([net.shared_model(a), net.shared_model(d)]), ["tiles", "unit"])


@unittest.skipIf(not HAS_UNICORN, "unicorn not installed (the research venv has it)")
class ControlsTest(unittest.TestCase):
    """custom.Controls on a fake memory: a control's rectangle is relative to its dialog, so a popup's OK is found
    on the screen at the dialog's origin plus its own; IDs are signed WORDs."""

    class Memory:
        def __init__(self):
            self.mem = {}

        def put(self, addr: int, data: bytes):
            for k, b in enumerate(data):
                self.mem[addr + k] = b

        def read(self, addr: int, n: int) -> bytes:
            return bytes(self.mem.get(addr + k, 0) for k in range(n))

        def r16(self, addr: int) -> int:
            return int.from_bytes(self.read(addr, 2), "little")

        def r32(self, addr: int) -> int:
            return int.from_bytes(self.read(addr, 4), "little")

    def test_rect_and_signed_ids(self):
        sys.path.insert(0, str(RESEARCH))
        from sc_emulator.custom import DIALOG_LIST, Controls
        m = self.Memory()
        dialog, ok, menu_ok = 0x1000, 0x2000, 0x3000
        m.put(DIALOG_LIST, struct.pack("<I", dialog))
        m.put(dialog + 4, struct.pack("<4h", 140, 140, 499, 339))       # the popup's rectangle on the screen
        m.put(dialog + 0x42, struct.pack("<I", ok))                       # its first control
        m.put(ok, struct.pack("<I", menu_ok))                             # next control
        m.put(ok + 4, struct.pack("<4h", 20, 150, 120, 170))
        m.put(ok + 0x20, struct.pack("<H", 1))
        m.put(menu_ok + 4, struct.pack("<4h", 0, 0, 10, 10))
        m.put(menu_ok + 0x20, struct.pack("<h", -2))
        ctl = Controls(m)
        self.assertEqual(ctl.find(1), ok)
        self.assertEqual(ctl.rect(ok), (160, 290, 260, 310))
        self.assertEqual(ctl.find(-2), menu_ok)
        self.assertEqual(ctl.rect(dialog), (140, 140, 499, 339))          # a dialog's own is on the screen
        other = 0x4000
        m.put(other + 4, struct.pack("<4h", 100, 50, 300, 250))
        self.assertEqual(ctl.rect(ok, other), (120, 200, 220, 220))       # relative to a given dialog
        self.assertEqual(ctl.find(1, other), 0)                           # a given dialog's own controls only
        m.put(DIALOG_LIST, struct.pack("<I", other))
        self.assertEqual((ctl.find(1), ctl.find(-2, dialog)), (0, menu_ok))


@unittest.skipIf(not HAS_UNICORN, "unicorn not installed (the research venv has it)")
class WinsockHleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(RESEARCH))
        from sc_emulator import hle
        from sc_emulator import lan as emu_lan
        from sc_emulator.machine import Machine, Wait
        hle.load_all()
        cls.REGISTRY, cls.Machine, cls.Wait, cls.lan = hle.REGISTRY, Machine, Wait, emu_lan

    def setUp(self):
        self.m = self.Machine(ROOT, ROOT)
        self.m.current = self.m.create_thread(0, 0)
        self.m.services["net"] = self.stack = self.lan.NetStack(A, "alpha")

    def call(self, name, *args, dll="ws2_32.dll"):
        r = self.REGISTRY[(dll, name)].fn(self.m, *args)
        if hasattr(r, "send"):
            try:
                w = next(r)
            except StopIteration as e:
                return e.value
            self.fail(f"{name} waited: {w}")
        return r

    def sockaddr(self, ip, port):
        p = self.m.alloc(16)
        self.m.write(p, struct.pack("<H", 2) + struct.pack(">HI", port, ip) + bytes(8))
        return p

    def error(self):
        return self.call("WSAGetLastError")

    def started_socket(self, port):
        data = self.m.alloc(400)
        self.assertEqual(self.call("WSAStartup", 0x0101, data), 0)
        self.assertEqual(struct.unpack("<HH", self.m.read(data, 4)), (0x0101, 0x0202))
        s = self.call("socket", 2, 2, 17)
        one = self.m.alloc(4)
        self.m.w32(one, 1)
        self.assertEqual(self.call("setsockopt", s, 0xFFFF, 0x20, one, 4), 0)
        self.assertEqual(self.call("bind", s, self.sockaddr(0, port), 16), 0)
        return s

    def test_wsock32_ordinals(self):
        r = self.REGISTRY
        for ordinal, name in ((10, "inet_addr"), (11, "inet_ntoa"), (12, "ioctlsocket"), (17, "recvfrom"),
                              (23, "socket"), (115, "WSAStartup")):
            self.assertIs(r[("wsock32.dll", ordinal)].fn, r[("ws2_32.dll", name)].fn, name)
        self.assertIs(r[("ws2_32.dll", 11)].fn, r[("ws2_32.dll", "inet_addr")].fn)
        self.assertIs(r[("ws2_32.dll", 10)].fn, r[("ws2_32.dll", "ioctlsocket")].fn)

    def test_no_network(self):
        del self.m.services["net"]
        self.assertEqual(self.call("WSAStartup", 0x0202, 0), 10091)  # WSASYSNOTREADY

    def test_datagrams(self):
        s = self.started_socket(6111)
        self.assertEqual(self.call("socket", 6, 1, 0), 0xFFFFFFFF)  # IPX
        self.assertEqual(self.error(), 10047)
        name, size = self.m.alloc(16), self.m.alloc(4)
        self.m.w32(size, 16)
        self.assertEqual(self.call("getsockname", s, name, size), 0)
        self.assertEqual(struct.unpack(">H", self.m.read(name + 2, 2))[0], 6111)
        buf = self.m.alloc(64)
        self.m.write(buf, b"ping")
        self.assertEqual(self.call("sendto", s, buf, 4, 0, self.sockaddr(lan.INADDR_BROADCAST, 6111), 16), 4)
        self.assertEqual(len(self.stack.outbox), 1)
        out, frm, frmlen = self.m.alloc(64), self.m.alloc(16), self.m.alloc(4)
        self.m.w32(frmlen, 8)
        self.assertEqual(self.call("recvfrom", s, out, 64, 0, frm, frmlen), 0xFFFFFFFF)   # no room for the sender
        self.assertEqual(self.error(), 10014)  # WSAEFAULT, the datagram still queued
        self.m.w32(frmlen, 16)
        self.assertEqual(self.call("recvfrom", s, out, 64, 0, frm, frmlen), 4)   # the broadcast's own copy
        self.assertEqual(self.m.read(out, 4), b"ping")
        self.assertEqual(struct.unpack(">HI", self.m.read(frm + 2, 6)), (6111, A))
        arg = self.m.alloc(4)
        self.m.w32(arg, 1)
        self.assertEqual(self.call("ioctlsocket", s, 0x8004667E, arg), 0)   # FIONBIO
        self.assertEqual(self.call("recvfrom", s, out, 64, 0, 0, 0), 0xFFFFFFFF)
        self.assertEqual(self.error(), 10035)  # WSAEWOULDBLOCK
        self.assertEqual(self.call("closesocket", s), 0)
        other = self.call("socket", 2, 2, 0)
        self.assertIn(other, self.m.handles)
        self.assertEqual(self.call("WSACleanup"), 0)
        self.assertNotIn(other, self.m.handles)   # the last cleanup closes every socket and frees its handle

    def test_blocking_receive_waits_for_the_arrival(self):
        s = self.started_socket(6112)
        out = self.m.alloc(64)
        gen = self.REGISTRY[("ws2_32.dll", "recvfrom")].fn(self.m, s, out, 64, 0, 0, 0)
        w = next(gen)
        self.assertIsInstance(w, self.Wait)
        self.assertFalse(w.predicate())
        self.stack.deliver(self.lan.Datagram(B, 6112, A, 6112, b"turn", 0, self.m.clock_us + 500))
        self.assertFalse(w.predicate())
        self.m.clock_us += 500
        self.assertTrue(w.predicate())
        with self.assertRaises(StopIteration) as e:
            gen.send(0)
        self.assertEqual(e.exception.value, 4)
        gen = self.REGISTRY[("ws2_32.dll", "recvfrom")].fn(self.m, s, out, 64, 0, 0, 0)
        w = next(gen)
        self.call("closesocket", s)
        self.assertTrue(w.predicate())
        with self.assertRaises(StopIteration) as e:
            gen.send(0)
        self.assertEqual(e.exception.value, 0xFFFFFFFF)
        self.assertEqual(self.error(), 10004)  # WSAEINTR

    def test_names(self):
        self.started_socket(6111)
        text = self.m.alloc(32)
        self.m.write(text, b"10.0.0.2\0")
        addr = self.call("inet_addr", text)
        self.assertEqual(struct.pack("<I", addr), bytes([10, 0, 0, 2]))
        self.assertEqual(self.m.cstr(self.call("inet_ntoa", addr)), b"10.0.0.2")
        name = self.m.alloc(64)
        self.assertEqual(self.call("gethostname", name, 64), 0)
        self.assertEqual(self.m.cstr(name), b"alpha")
        host = self.call("gethostbyname", name)
        first = self.m.r32(self.m.r32(self.m.r32(host + 12)))
        self.assertEqual(first, struct.unpack("<I", bytes([10, 0, 0, 1]))[0])
        self.assertEqual(self.call("htons", 0x1234), 0x3412)


if __name__ == "__main__":
    unittest.main()
