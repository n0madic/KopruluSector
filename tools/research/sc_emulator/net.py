"""Network games of the original between emulated instances (phase N, stage N1: the network oracle).

Each player is a whole emulated StarCraft 1.17 (StarCraft.exe, storm.dll and standard.snp as x86 code) in a process of
its own, with its own virtual IP on the virtual LAN (lan.py) and its own inputs. Its Winsock calls go to the HLE of
hle/ws2_32.py; its background threads (Storm's network thread 0x15022780, the UDP provider's receive threads) take
their turns by preemption (Machine.preempt_us); its clock is held to the others' by the coordinator's rounds
(lan.Coordinator, Machine.clock_limit/on_limit).

The players go through the original's own screens by input, as a person does: the main menu's Multiplayer (M), the
game mode (E/O), the connection screen gluConn (glue state 2: the provider list's row "Local Area Network (UDP/IP)",
then Ok), a new ID (gluPEdit) and the login's Ok, then the join screen gluJoin (state 10). The host presses Create Game
(G), sets the game type and subtype on gluCreat (state 11; the map is the preselected `!` file, as in custom.py) and
Ok; the others wait until the host's game is in gluJoin's list, select its row and press Ok (Join). In the chat room
gluChat (state 3) each player may set its race, chat, and the host presses Ok (Start Game) once every player is in.
From the game's first frame on a player gives only its scripted actions.

Written to <out>/ (koprulu-private/emu/net/<game>/, never committed):
- lan.pcap, lan.jsonl: every datagram (the coordinator, lan.VirtualLan);
- net.json: the game, each player's result, the rounds, the comparison of the players' frame digests and state dumps;
- <player>/: trace.jsonl (frame digests), snet.jsonl (the exe's calls of Storm's SNet functions: arguments, results,
  outputs, SNetInitializeProvider's program data, SNetCreateLadderGame's template; the events Storm gives the exe's
  handlers; each pass of Storm's network thread and of SNetJoinGame with the datagrams it took, the network thread's
  with its signal and its next wait; each record's order of entry and depth of nesting), turns.jsonl (the turns sent
  and received with their commands; 0x05 keep-alive and 0x37 sync split out), screens/ (each glue screen 1.5 s after
  it settled, uiscreen.install; lobby-*.cap at the driver's moments), states/ (state.install's dumps), the oracle's
  logs (scenario.install_logs: commands, ui, triggers, ai, input), setup.json, player.json, run.log.
"""
from __future__ import annotations

import json
import logging
import multiprocessing as mp
import struct
import time
import traceback
from dataclasses import asdict, dataclass, field
from pathlib import Path

from . import lan

log = logging.getLogger(__name__)

OUT_ROOT = Path(__file__).resolve().parents[3] / "koprulu-private/emu/net"
STOCK_MAPS = Path(__file__).resolve().parents[3] / "koprulu-private/full/app/maps"
PREEMPT_US = 1000               # background threads' turn: every virtual millisecond
PROCESS_EXIT_S = 60             # a player's process may take this long to exit after its last message
PROVIDER_UDP = "Local Area Network (UDP/IP)"
DIALOG_LIST = 0x6CE6D8
PLAYERS = 0x6726C8              # 12 player records of 36 bytes (custom.py)
MULTIPLAYER = 0x672664
LOBBY_ROWS, LOBBY_ROW_SIZE = 0x6681B0, 0xB0   # the chat room's 12 rows: +0 kind (5 the local human), +0x50 Storm ID
ROW_REMOTE_HUMAN, ROW_LOCAL_HUMAN = 4, 5
INITIAL_SEED = 0x673934         # SetInitialSeed 0x4781f0: the game's seed (a network game's from the host's 0x48)
STORM_STATUS = 0x672670         # 8 Storm status words: bit 0x10000 present, 0x40000 in sync (ResolveDesync)
RESOLVE_DESYNC = 0x47A630       # drops the minority after a sync mismatch or the timeout dialog's Drop Players
TIMEOUT_DIALOG = 0x675010       # rez\timeout.bin while the game waits for lagging players (0 otherwise)
TIMEOUT_SECONDS = 0x675020      # its countdown: Drop Players (control 2) is disabled until it reached 0
TIMEOUT_DROP = 2
GLUE_MAIN, GLUE_CONN, GLUE_LOGIN, GLUE_JOIN, GLUE_CREATE, GLUE_CHAT = 0, 2, 5, 10, 11, 3
GLUE_READY_ROOMS = (7, 8, 9)    # a Use Map Settings game's briefing (custom.py)
CONN_LIST, CONN_OK = 5, 9
JOIN_LIST, JOIN_OK = 5, 13
CREATE_LIST, CREATE_OK = 5, 12   # gluCreat's maps and saved games, its Ok
CHAT_START = 7
POPUP_OK, POPUP_TEXT = 1, 2     # the glue popups' OK and text (atlas P21)
POPUPS_OK = ("rez\\glupok.bin", "rez\\glupoksplit.bin")
ABORT_QUIT_MISSION, QUIT_MISSION_OK = 2, -2  # abrtmenu's Quit Mission, quit2mnu's quit (atlas P27)
CTRL_GAME_TYPE, CTRL_SUBTYPE = 17, 18
SLOT_TYPE, SLOT_RACE = 31, 32   # row i's type and race drop-downs: control 31 + 4i and 32 + 4i (atlas P18)
# What each instance keeps for its own viewer, not the shared simulation, so the players' dumps differ there by
# design (stage N1's 1v1 at every dump): the megatiles displayed through the viewer's fog and their creep edges
# (0x54323c; RefreshRememberedTerrainOffscreen 0x40af30), the tile flag 0x40000000 that marks a displayed creep
# tile, and the images' world x/y (+0x1c), which the draw recomputes (RecomputeAllSpriteBounds 0x415700 ->
# UpdateImageBounds 0x40ee70; seen on the neutral geysers' smoke overlays). Sprite and unit positions stay compared.
VIEWER_FIELDS = {"terrain": ("display", "border"), "image": ("x", "y")}
VIEWER_TILE_FLAG = 0x40000000
# A game's first dump also has what the menus left: the turn counter 0x672658 and the speed byte 0x6D3130.
MENU_FIELDS = {"step": ("turn",), "triggers": ("speed",)}
VK_UP, VK_DOWN = 0x26, 0x28
CLICK_WAIT_STEPS = 50        # a "click" action waits up to 50 x 100 ms for its control to appear
KEY_NAMES = {"ESC": 0x1B, "ENTER": 0x0D, "TAB": 0x09, "SPACE": 0x20, **{f"F{k}": 0x6F + k for k in range(1, 13)}}

# Storm's SNet exports the executable imports (atlas S01): ordinal -> (name, stdcall arguments)
SNET = {
    101: ("SNetCreateGame", 10), 102: ("SNetDestroy", 0), 103: ("SNetEnumDevices", 1), 105: ("SNetEnumProviders", 2),
    106: ("SNetDropPlayer", 2), 107: ("SNetGetGameInfo", 4), 109: ("SNetGetNumPlayers", 3),
    112: ("SNetGetPlayerCaps", 2), 113: ("SNetGetPlayerName", 3), 114: ("SNetGetProviderCaps", 1),
    115: ("SNetGetTurnsInTransit", 1), 116: ("SNetInitializeDevice", 5), 117: ("SNetInitializeProvider", 5),
    118: ("SNetJoinGame", 6), 119: ("SNetLeaveGame", 1), 121: ("SNetReceiveMessage", 3),
    122: ("SNetReceiveTurns", 5), 123: ("SNetRegisterEventHandler", 2), 125: ("SNetSelectGame", 6),
    127: ("SNetSendMessage", 3), 128: ("SNetSendTurn", 2), 130: ("SNetSetGameMode", 2), 133: ("SNetEnumGamesEx", 4),
    137: ("SNetDisconnectAll", 1), 138: ("SNetCreateLadderGame", 12), 139: ("SNetReportGameResult", 5),
    144: ("SNetGetPlayerNames", 1),
}
# polled every loop: logged only when they succeed and their record changed
SNET_POLLED = {107, 109, 112, 113, 114, 115, 121, 122, 144}
# what differs between two records of one unchanged poll: its time, frame and place in the call order
VOLATILE = ("t", "t_us", "seq", "depth", "end", "frame")
# polled, and logged at every call (a failure as a short record): each success consumes a turn or a message, and
# their failures still run Storm's drops and events and SNetReceiveTurns's resend requests and pings, which kp_snet's
# replay of a recording (stage N2) repeats; a failed SNetReceiveTurns keeps its arguments and the status words Storm
# wrote (StarCraft reads them after a failure: who is waited for)
SNET_FAILURES = {121, 122}
MAX_TURN_SLOTS = 256  # a failed SNetReceiveTurns's status words read, at most
# Storm's network thread inside its lock, and where ReceivePackets has a datagram from the provider (install_snet_log)
PUMP_PASS, PUMP_TIMERS, PUMP_PASS_END = 0x150227CD, 0x150227DB, 0x150227E0
JOIN_PASS, JOIN_TIMERS, JOIN_PASS_END = 0x15023F71, 0x15023F7D, 0x15023F82
TAKEN = (0x15021FD1, 0x15022769)
CLOCK_READS = {"GetTickCount", "timeGetTime", "QueryPerformanceCounter"}
STRING_ARGS = {101: (0, 1, 2, 7, 8), 138: (0, 1, 2, 9, 10), 118: (1, 2, 3, 4)}
# Turn command lengths with the ID byte where the turn walker 0x475d30 differs from the table 0x4ef048 (0x4720c0):
# the sync command 0x37 is 7 bytes in a turn; save and load (6, 7) are 5 bytes and a zero-terminated name
SYNC, KEEP_ALIVE = 0x37, 0x05


# ---------------------------------------------------------------------------------------------- specification
@dataclass
class Action:
    """A player's scripted input. `at`: "lobby" (in the chat room, once every player is in, before the start) or a
    game frame. Kinds: chat (Enter in a game, the text, Enter: also a whisper "/w NAME text"), keys (whitespace-
    separated keys, a letter or a name of KEY_NAMES, 700 ms apart: the game menu's paths, e.g. "F10 O N E O" for
    extra high latency), text (typed without Enter), slot ("ROW:ITEM", the host's type drop-down of a chat room row,
    ROW a number or "remote" for the first remote human's: Open kicks, "Ban Player!" bans, both after the OK of
    the "will boot" popup), race (the local row's race drop-down), click ("[DIALOG:]ID", a control of the topmost
    dialog, waited for with that dialog's BIN name on top: a popup's OK, a menu's button), wait (milliseconds), stop
    (the driver ends there: a lobby that never starts), poke ("ADDR:HEX" or "*POINTER+OFFSET:HEX", a memory write at
    the frame's end: the desync test). A cut (value: virtual microseconds) pulls the player's cable on the LAN at that
    time; its `at` is not used."""
    at: str | int
    kind: str
    value: str = ""


@dataclass
class Player:
    name: str                         # the ID (profile) name, also the host name
    ip: str
    role: str = "join"                # host | join | external (a live peer the emulator does not run, over --bridge:
                                      # Koprulu; an external player hosts when no player is the host)
    has_map: bool = True              # the map is in its overlay (False: the host must send it)
    auto_drop: bool = False           # presses Drop Players once the timeout dialog's countdown ended
    actions: list[Action] = field(default_factory=list)
    preload: list[list[str]] = field(default_factory=list)   # [guest path, host file]: e.g. a saved game to load


@dataclass
class GameSpec:
    name: str
    map_path: str
    players: list[Player]
    expansion: bool = True
    game_type: str = ""               # gluCreat's game type item ("" keeps the default)
    subtype: str = ""
    load_save: str = ""               # the host creates the game from this saved game of gluCreat's list instead
    frames: int = 2001                # every player runs until its frame counter reached this
    state_steps: tuple[int, ...] = ()
    seed: int = 1
    delay_us: int = lan.DEFAULT_DELAY_US
    jitter_us: int = lan.DEFAULT_JITTER_US
    loss: float = 0.0
    wall_limit_s: float = 4 * 3600
    lobby_timeout_ms: int = 120_000   # virtual time a lobby step may take
    bridge_base: int = 0              # lan.LoopbackBridge's first real port (0: no bridge)
    realtime: bool = False
    wait_players: int = 0             # the host starts with this many in the chat room (live peers), at least these

    @property
    def map_name(self) -> str:
        return Path(self.map_path).name

    def to_json(self) -> dict:
        d = asdict(self)
        d["state_steps"] = list(self.state_steps)
        return d

    @classmethod
    def from_json(cls, d: dict) -> "GameSpec":
        d = dict(d)
        d["players"] = [Player(**{**p, "actions": [Action(**a) for a in p["actions"]]}) for p in d["players"]]
        d["state_steps"] = tuple(d["state_steps"])
        return cls(**d)


# ---------------------------------------------------------------------------------------------- turn commands
def turn_commands(data: bytes) -> tuple[list[bytes], bytes]:
    """A turn's commands by the walker 0x475d30's lengths; the bytes after the first unknown ID are returned apart."""
    from scdata.formats import rep
    out, pos = [], 0
    while pos < len(data):
        cid = data[pos]
        if cid == SYNC:
            n = 7
        elif cid in (6, 7):
            end = data.find(b"\0", pos + 5)
            n = (end + 1 - pos) if end >= 0 else -1
        else:
            n = rep.command_length(data, pos)
        if n <= 0 or pos + n > len(data):
            return out, data[pos:]
        out.append(data[pos:pos + n])
        pos += n
    return out, b""


def describe_turn(data: bytes) -> dict:
    cmds, rest = turn_commands(data)
    d = {"size": len(data), "keepalive": sum(1 for c in cmds if c[0] == KEEP_ALIVE),
         "sync": [c[1:].hex() for c in cmds if c[0] == SYNC],
         "commands": [c.hex() for c in cmds if c[0] not in (KEEP_ALIVE, SYNC)]}
    if rest:
        d["rest"] = rest.hex()
    return d


# ---------------------------------------------------------------------------------------------- the SNet log
def snet_return_sites(exe) -> list[int]:
    """Every return address of a call into Storm's SNet imports: after `call [iat]` (FF 15) and after `call thunk`
    where the thunk is `jmp [iat]` (FF 25). A stray match in data is never executed, so it costs nothing."""
    pe = exe.pe
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    iats = []
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        if entry.dll.decode().lower() == "storm.dll":
            iats += [imp.address for imp in entry.imports if imp.ordinal in SNET]
    sites, thunks = set(), set()
    for iat in iats:
        key = struct.pack("<I", iat)
        for op, into in ((b"\xff\x15", sites), (b"\xff\x25", thunks)):
            k = image.find(op + key)
            while k >= 0:
                into.add(base + k + 6 if op == b"\xff\x15" else base + k)
                k = image.find(op + key, k + 1)
    if thunks:
        k = image.find(b"\xe8")
        while k >= 0 and k + 5 <= len(image):
            target = (base + k + 5 + struct.unpack_from("<i", image, k + 1)[0]) & 0xFFFFFFFF
            if target in thunks:
                sites.add(base + k + 5)
            k = image.find(b"\xe8", k + 1)
    return sorted(sites)


def install_snet_log(s, out: Path, frame_of) -> dict:
    """snet.jsonl and turns.jsonl (the module's docstring): hooks at Storm's SNet exports (called from the exe) and at
    every return site in the exe."""
    m = s.m
    storm, exe = m.images["storm.dll"], m.exe
    f_snet, f_turns = open(out / "snet.jsonl", "w", buffering=1), open(out / "turns.jsonl", "w", buffering=1)
    pending: dict[int, list] = {}
    last: dict[int, str] = {}
    handlers: set[int] = set()
    counter = [0]

    def next_seq() -> int:
        counter[0] += 1
        return counter[0]
    st = {"calls": 0, "turns_received": 0, "turns_sent": 0, "files": (f_snet, f_turns)}

    def cstr(p):
        return m.cstr(p, 256).decode("latin-1") if p else None

    def entry(ordinal):
        name, nargs = SNET[ordinal]

        def on_entry():
            esp = m.esp
            ret = m.r32(esp)
            if not exe.contains(ret):
                return
            args = list(struct.unpack(f"<{nargs}I", m.read(esp + 4, 4 * nargs))) if nargs else []
            # seq: the entry order (a call's record is written when it returns); depth: the calls it is inside (an
            # event handler of the executable calls SNet inside the call that dispatched the event)
            depth = sum(1 for e in pending.get(m.current.tid, []) if e[0] > esp)
            rec = {"t": m.clock_us // 1000, "t_us": m.clock_us, "seq": next_seq(), "depth": depth, "frame": frame_of(),
                   "fn": name, "args": [f"{a:#x}" for a in args]}
            for k in STRING_ARGS.get(ordinal, ()):
                rec.setdefault("strings", {})[str(k)] = cstr(args[k])
            if ordinal == 138:
                rec["template"] = m.read(args[6], args[7]).hex() if args[6] and args[7] else ""
            elif ordinal == 117 and args[1]:
                # SNETPROGRAMDATA: its size first, then names, the program id (+0xC), version (+0x10), maxplayers
                rec["program"] = m.read(args[1], min(m.r32(args[1]), 0x80)).hex()
            elif ordinal == 123 and args[1] and args[1] not in handlers:
                handlers.add(args[1])
                s.hook(args[1], on_event)
            if ordinal == 128:
                data = m.read(args[0], args[1]) if args[1] else b""
                rec["data"] = data.hex()
                st["turns_sent"] += 1
                f_turns.write(json.dumps({"t": rec["t"], "frame": rec["frame"], "sent": describe_turn(data)}) + "\n")
            elif ordinal == 127:
                rec["data"] = m.read(args[1], args[2]).hex() if args[2] else ""
            pending.setdefault(m.current.tid, []).append((esp, ordinal, args, rec))
        return on_entry

    def on_event():
        """A handler the executable registered (SNetRegisterEventHandler), at its entry: the event Storm dispatches,
        SNETEVENT {id, player, data, size}."""
        ev = m.r32(m.esp + 4)
        eid, player, data, size = struct.unpack("<IiII", m.read(ev, 16))
        depth = sum(1 for e in pending.get(m.current.tid, []) if e[0] > m.esp)
        st["calls"] += 1
        f_snet.write(json.dumps({"t": m.clock_us // 1000, "t_us": m.clock_us, "seq": next_seq(), "depth": depth,
                                 "frame": frame_of(), "fn": "event", "id": eid, "player": player,
                                 "data": m.read(data, size).hex() if data and size else ""}) + "\n")

    # Storm's network thread 0x15022780, each pass: WaitForSingleObject(event, wait), EnterCriticalSection, then (with a
    # provider) 0x150227cd with esi = whether the event was signalled, ReceivePackets 0x15021fa0 if it was, RunNetTimers
    # 0x15020b80 (called at 0x150227db), whose result (back at 0x150227e0) is the next wait. SNetJoinGame's own passes
    # (PumpUntilControl 0x15023f40, in the executable's thread): each at 0x15023f71 (ebx 1 for the first, which follows
    # no Sleep(10)), RunNetTimers called at 0x15023f7d, back at 0x15023f82. The datagrams Storm takes from the provider
    # (its slot +0x2C, returning at 0x15021fd1 and 0x15022769 with the address, data and size pointers' targets at
    # [ebp-0x14], [ebp-0x10], [ebp-0xc]) go with the pass that took them.
    # the emulator's own imports: the module's pure parts run without unicorn
    from unicorn import x86_const as X

    from .hle.kernel32 import TIME_QUERY_COST_US
    passes: dict[int, dict] = {}  # each thread's pass in progress

    def pass_start(fn: str, **extra):
        passes[m.current.tid] = {"fn": fn, "t_us": m.clock_us, "seq": next_seq(), "frame": frame_of(), **extra,
                                 "timers_us": None, "received": [], "clock": []}

    def on_api(spec, _args):
        """The clock's reads inside a pass, [caller, the virtual microsecond read]: each charges the emulator's
        TIME_QUERY_COST_US first (hle/kernel32.py), so where Storm reads it decides the milliseconds it gets."""
        p = passes.get(m.current.tid)
        if p is not None and spec.name in CLOCK_READS:
            p["clock"].append([f"{m.r32(m.esp):#x}", m.clock_us + TIME_QUERY_COST_US])

    def on_timers():
        if m.current.tid in passes:
            passes[m.current.tid]["timers_us"] = m.clock_us

    def pass_end(**extra):
        p = passes.pop(m.current.tid, None)
        if p is None:
            return
        st["calls"] += 1
        f_snet.write(json.dumps({"t": p["t_us"] // 1000, **p, **extra, "end_us": m.clock_us}) + "\n")

    def on_pass():
        pass_start("pump", signalled=int(bool(m.reg(X.UC_X86_REG_ESI))))

    def on_pass_end():
        pass_end(wait=m.reg(X.UC_X86_REG_EAX))

    def on_join_pass():
        pass_start("join_pass", first=int(bool(m.reg(X.UC_X86_REG_EBX))))

    def on_join_pass_end():
        pass_end()

    def on_taken():
        if not m.reg(X.UC_X86_REG_EAX):
            return
        ebp = m.reg(X.UC_X86_REG_EBP)
        addr, data, size = m.r32(ebp - 0x14), m.r32(ebp - 0x10), m.r32(ebp - 0xC)
        if not addr or not data:
            return
        port, ip = struct.unpack(">HI", m.read(addr + 2, 6))
        d = {"from": f"{ip >> 24}.{ip >> 16 & 255}.{ip >> 8 & 255}.{ip & 255}:{port}", "data": m.read(data, size).hex()}
        if m.current.tid in passes:
            passes[m.current.tid]["received"].append(d)
            return
        st["calls"] += 1
        f_snet.write(json.dumps({"t": m.clock_us // 1000, "t_us": m.clock_us, "seq": next_seq(), "frame": frame_of(),
                                 "fn": "taken", **d}) + "\n")

    def outputs(ordinal, args, eax) -> dict:
        o = {}
        if not eax:
            return o
        if ordinal == 122:
            first, count, datap, sizes, status = args
            players = []
            for i in range(count):
                ptr, size, word = m.r32(datap + 4 * i), m.r32(sizes + 4 * i), m.r32(status + 4 * i)
                p = {"id": first + i, "status": f"{word:#x}", "size": size}
                if ptr and size:
                    p["data"] = m.read(ptr, size).hex()
                players.append(p)
            o["players"] = players
        elif ordinal == 121:
            sender, datap, sizep = args
            size = m.r32(sizep)
            o["sender"] = m.rs32(sender)
            o["data"] = m.read(m.r32(datap), size).hex() if size else ""
        elif ordinal == 107:
            n = m.r32(args[3]) if args[3] else args[2]
            o["info"] = m.read(args[1], min(n, args[2])).hex() if args[1] and n else ""
        elif ordinal == 109:
            o["num"] = [m.rs32(a) if a else None for a in args]
        elif ordinal == 113:
            o["name"] = cstr(args[1])
        elif ordinal == 115:
            o["in_transit"] = m.r32(args[0]) if args[0] else None
        elif ordinal == 114:
            o["caps"] = m.read(args[0], 0x24).hex() if args[0] else ""
        elif ordinal == 118:
            o["player_id"] = m.rs32(args[5]) if args[5] else None
        elif ordinal in (101, 138):
            o["player_id"] = m.rs32(args[-1]) if args[-1] else None
        return o

    def on_return():
        stack = pending.get(m.current.tid)
        if not stack:
            return
        esp = m.esp
        eax = m.reg(X.UC_X86_REG_EAX)
        while stack and stack[-1][0] < esp:
            _, ordinal, args, rec = stack.pop()
            rec["end"] = next_seq()  # what has a seq between the call's and this one happened inside the call
            rec["result"] = eax
            rec.update(outputs(ordinal, args, eax))
            if ordinal in SNET_POLLED:
                if not eax:
                    if ordinal in SNET_FAILURES:
                        st["calls"] += 1
                        short = {k: rec[k] for k in ("t", "t_us", "seq", "depth", "end", "frame", "fn", "result")}
                        if ordinal == 122:
                            _, count, _, _, status = args
                            short["args"] = rec["args"]
                            if status and count <= MAX_TURN_SLOTS:
                                short["status"] = [f"{m.r32(status + 4 * i):#x}" for i in range(count)]
                        f_snet.write(json.dumps(short) + "\n")
                    continue
                key = json.dumps({k: v for k, v in rec.items() if k not in VOLATILE}, sort_keys=True)
                if ordinal not in SNET_FAILURES and last.get(ordinal) == key:
                    continue
                last[ordinal] = key
            if ordinal == 122:
                st["turns_received"] += 1
                f_turns.write(json.dumps({"t": rec["t"], "frame": rec["frame"], "received": [
                    {"id": p["id"], "status": p["status"], **describe_turn(bytes.fromhex(p.get("data", "")))}
                    for p in rec["players"]]}) + "\n")
            st["calls"] += 1
            f_snet.write(json.dumps(rec) + "\n")

    for ordinal in SNET:
        addr = storm.export(ordinal)
        if addr is not None:
            s.hook(addr, entry(ordinal))
    m.on_api.append(on_api)
    s.hook(PUMP_PASS, on_pass)
    s.hook(PUMP_TIMERS, on_timers)
    s.hook(PUMP_PASS_END, on_pass_end)
    s.hook(JOIN_PASS, on_join_pass)
    s.hook(JOIN_TIMERS, on_timers)
    s.hook(JOIN_PASS_END, on_join_pass_end)
    for site in TAKEN:
        s.hook(site, on_taken)
    sites = snet_return_sites(exe)
    for site in sites:
        s.hook(site, on_return)
    st["return_sites"] = len(sites)
    return st


# ---------------------------------------------------------------------------------------------- the instance
class ScriptStop(Exception):
    """A player's script ends its run here (an action "stop")."""


class LanStopped(Exception):
    """The coordinator stopped the game (another player exited, the wall-time limit, every driver finished)."""


class Link:
    """This instance's side of the coordinator's rounds (lan.Coordinator's protocol)."""

    def __init__(self, conn, m, stack: lan.NetStack):
        self.conn, self.m, self.stack = conn, m, stack
        self.state = "run"
        self.stopped: str | None = None
        self.syncs = 0

    def sync(self, _machine=None):
        """Machine.on_limit (which passes the machine, this link's own) and the first round's call."""
        m = self.m
        if self.stopped is not None:  # after the stop every run returns at once
            m.stop_requested = True
            return
        self.conn.send(("sync", m.clock_us, [d.wire() for d in self.stack.take_outbox()], self.state))
        msg = self.conn.recv()
        self.syncs += 1
        if msg[0] == "go":
            for t in msg[2]:
                self.stack.deliver(lan.Datagram.from_wire(t))
            m.clock_limit = msg[1]
        else:
            self.stopped = msg[1]
            m.clock_limit = m.clock_us  # every later run() hands over at once and returns
            m.stop_requested = True


class Driver:
    """One player's run: the machine, its inputs and records."""

    def __init__(self, spec: GameSpec, index: int, out: Path, link_conn):
        from . import process, scenario, state, uiscreen, game
        from .custom import Controls
        from .inputs import Input
        from .session import Session
        self.spec, self.index, self.player = spec, index, spec.players[index]
        self.out = out
        self.s = Session(process.Config(cd="expansion" if spec.expansion else "original"))
        self.m = m = self.s.m
        self.game = game
        self.stack = lan.NetStack(lan.ip_int(self.player.ip), self.player.name)
        m.services["net"] = self.stack
        m.preempt_us = PREEMPT_US
        self.link = Link(link_conn, m, self.stack)
        m.on_limit = self.link.sync
        self.inp = Input(self.s)
        self.ctl = Controls(m)
        self.events: list[dict] = []
        self.visited: list[list] = []
        self.st = {"last_frame": -1, "started_at": None}
        self.lobby_shots = 0
        if self.player.has_map:
            from .custom import MAPS_DIR
            folder = MAPS_DIR + ("\\BroodWar" if spec.expansion else "")
            m.services["fs"].preload(f"{folder}\\!{spec.map_name}", Path(spec.map_path).read_bytes())
        for guest, host in self.player.preload:
            m.services["fs"].preload(guest, Path(host).read_bytes())
        self.preloaded = {m.services["fs"]._key(k) for k in m.services["fs"].overlay}
        if spec.state_steps:
            state.install(self.s, spec.state_steps, out / "states")
        self.logs = scenario.install_logs(self.s, out)
        self.screens = uiscreen.install(self.s, out / "screens", glue_ms=(1500,),
                                        names=self.logs["uilog"]["dialog_ref"])
        self.snet = install_snet_log(self.s, out, lambda: m.r32(game.FRAME_COUNTER))
        self.trace = open(out / "trace.jsonl", "w", buffering=1)
        self.s.hook(game.FRAME_END_PC, self._on_frame_end)
        self.setup: dict = {}
        self.s.hook(0x443F40, self._on_start_game)  # StartGame
        self.s.hook(RESOLVE_DESYNC, self._on_resolve_desync)

    # ------------------------------------------------------------------ records
    def ev(self, kind, **kw):
        e = {"t": self.m.clock_us // 1000, "kind": kind, "frame": self.m.r32(self.game.FRAME_COUNTER),
             "glue": self.m.r32(self.game.GLUE_STATE), **kw}
        self.events.append(e)
        log.info("%s %s", self.player.name, e)

    def _on_frame_end(self):
        m, game = self.m, self.game
        f = m.r32(game.FRAME_COUNTER)
        if f == self.st["last_frame"]:
            return
        self.st["last_frame"] = f
        if f <= self.spec.frames:
            self.trace.write(json.dumps(game.digest(m), separators=(",", ":")) + "\n")
        for a in self.player.actions:
            if a.at == f and a.kind == "poke":
                self._act(a)

    def _on_resolve_desync(self):
        words = struct.unpack("<8I", self.m.read(STORM_STATUS, 32))
        self.ev("resolve_desync", status=[f"{w:#x}" for w in words])

    def _on_start_game(self):
        from .custom import _setup_snapshot
        if "start_game" not in self.setup:
            self.setup["start_game"] = _setup_snapshot(self.m)
            self.setup["start_game"]["seed"] = self.m.r32(INITIAL_SEED)
            self.setup["multiplayer"] = self.m.r8(MULTIPLAYER)

    def capture(self, label: str):
        from . import uiscreen
        path = self.out / "screens" / f"lobby-{self.lobby_shots:02d}-{label}.cap"
        uiscreen.capture_screen(self.m, self.screens, path, {"label": label}, names=self.logs["uilog"]["dialog_ref"])
        self.lobby_shots += 1

    # ------------------------------------------------------------------ running
    def check(self):
        if self.link.stopped is not None:
            raise LanStopped(self.link.stopped)

    def wait(self, ms: int):
        end = self.m.clock_us + ms * 1000
        self.run_until(lambda: self.m.clock_us >= end, ms + 1000)

    def run_until(self, pred, limit_ms: int) -> bool:
        """Runs until pred() or `limit_ms` of virtual time passed; False on the limit."""
        end = self.m.clock_us + limit_ms * 1000
        self.check()
        self.m.run(stop=lambda: pred() or self.m.clock_us >= end or self.link.stopped is not None)
        self.check()
        return pred()

    def top_bin(self):
        ref = self.logs["uilog"]["dialog_ref"](self.m.r32(DIALOG_LIST))
        return ref[0].lower() if isinstance(ref, list) else None

    def settle(self, want_state: int, want_bin: str, settle_ms: int = 1500) -> None:
        m, game = self.m, self.game
        last, since = None, m.clock_us
        end = m.clock_us + self.spec.lobby_timeout_ms * 1000
        while m.clock_us < end:
            self.wait(100)
            cur = (m.r16(game.GAME_MODE), m.r32(game.GLUE_STATE), m.r32(DIALOG_LIST))
            if cur != last:
                last, since = cur, m.clock_us
                self.visited.append([m.clock_us // 1000, cur[0], cur[1], self.top_bin()])
                continue
            if m.clock_us - since >= settle_ms * 1000 and cur[0] == 4 and cur[1] == want_state \
                    and self.top_bin() == want_bin.lower():
                return
        raise RuntimeError(f"the screen {want_bin} (glue state {want_state}) did not come "
                           f"(mode {m.r16(game.GAME_MODE)}, glue {m.r32(game.GLUE_STATE)}, top {self.top_bin()})")

    def press(self, keys: str):
        for k, ch in enumerate(keys):
            if k:
                self.wait(500)
            self.inp.key(ord(ch.upper()))

    def click_control(self, ident: int):
        c = self.ctl.find(ident)
        if not c:
            raise RuntimeError(f"control {ident} not found on {self.top_bin()}")
        left, top, right, bottom = self.ctl.rect(c)
        self.inp.click((left + right) // 2, (top + bottom) // 2)
        self.wait(300)

    def control_text(self, ident: int) -> str:
        c = self.ctl.find(ident)
        ptr = self.m.r32(c + 0x14) if c else 0  # a control's text (atlas E01)
        return self.m.cstr(ptr).decode("latin-1") if ptr else ""

    def list_items(self, ident: int) -> list[str]:
        c = self.ctl.find(ident)
        return self.ctl.items(c) if c else []

    def select_list_row(self, ident: int, match) -> str:
        """Clicks the first row whose text satisfies match(text) (ListBoxProc: the press sets the hover row, the
        release selects it)."""
        m = self.m
        c = self.ctl.find(ident)
        items = self.ctl.items(c)
        k = next((i for i, t in enumerate(items) if match(t)), None)
        if k is None:
            raise RuntimeError(f"list {ident} has no matching row: {items}")
        top_index, row = m.read(c + 0x4D, 1)[0], m.read(c + 0x49, 1)[0]
        left, top, right, bottom = self.ctl.rect(c)
        y = top + (k - top_index) * row + row // 2
        if top <= y < bottom:
            self.inp.click(left + 20, y)
        else:  # scrolled out of view: a visible row's click, then the arrow keys (the list's keys 0x4c6bb0, P43)
            self.inp.click(left + 20, top + row // 2)
            self.wait(300)
            cur = self.ctl.selected(c)
            for _ in range(abs(k - cur)):
                self.inp.key(VK_DOWN if k > cur else VK_UP)
                self.wait(100)
        self.wait(300)
        got = self.ctl.selected(c)
        if got != k:
            raise RuntimeError(f"list {ident}: selected {got} instead of {k} ({items[k]!r})")
        return items[k]

    def lobby_players(self) -> int:
        """Human slots taken in the chat room (the player records' type 2 with a Storm ID)."""
        n = 0
        for p in range(8):
            b = self.m.read(PLAYERS + 36 * p, 12)
            storm, kind = struct.unpack_from("<i", b, 4)[0], b[8]
            if kind == 2 and storm >= 0:
                n += 1
        return n

    def row_of_kind(self, kind: int) -> int | None:
        for i in range(12):
            if self.m.r32(LOBBY_ROWS + LOBBY_ROW_SIZE * i) == kind:
                return i
        return None

    def keys(self, text: str):
        for k, tok in enumerate(text.split()):
            if k:
                self.wait(700)
            self.inp.key(KEY_NAMES[tok.upper()] if tok.upper() in KEY_NAMES else ord(tok.upper()))

    def _act(self, a: Action):
        from .custom import ComboError, select_combo
        self.ev("action", action=asdict(a))
        in_game = self.m.r16(self.game.GAME_MODE) == 3
        if a.kind == "chat":
            if in_game:
                self.inp.key(0x0D)
                self.wait(300)
            self.inp.text(a.value)
            self.wait(300)
            self.inp.key(0x0D)
        elif a.kind == "keys":
            self.keys(a.value)
        elif a.kind == "text":
            self.inp.text(a.value)
        elif a.kind == "slot":
            row, _, item = a.value.partition(":")
            row = self.row_of_kind(ROW_REMOTE_HUMAN) if row == "remote" else int(row)
            if row is None:
                raise RuntimeError("no remote player's row in the chat room")
            try:
                self.events[-1]["combo"] = select_combo(self.inp, self.ctl, SLOT_TYPE + 4 * row, item)
            except ComboError as e:
                # the type drop-down of a human row is a menu of actions (BuildSlotTypeMenu 0x4532b0): the choice is
                # queued as the row's pending change (applied some turns later) and the drop-down shows the occupant
                # again, so a selection that did not stay is no failure here
                if "selected" not in str(e):
                    raise
                self.events[-1]["combo"] = {"control": SLOT_TYPE + 4 * row, "item": item, "note": str(e)}
        elif a.kind == "race":
            row = self.row_of_kind(ROW_LOCAL_HUMAN)
            if row is None:
                raise RuntimeError("no local row in the chat room")
            self.events[-1]["combo"] = select_combo(self.inp, self.ctl, SLOT_RACE + 4 * row, a.value)
        elif a.kind == "poke":
            target, data = a.value.split(":")
            if target.startswith("*"):  # *POINTER+OFFSET: through a pointer (e.g. the first active unit)
                ptr, _, off = target[1:].partition("+")
                addr = self.m.r32(int(ptr, 16)) + int(off or "0", 16)
            else:
                addr = int(target, 16)
            self.events[-1]["poked"] = {"addr": f"{addr:#x}", "old": self.m.read(addr, len(data) // 2).hex()}
            self.m.write(addr, bytes.fromhex(data))
        elif a.kind == "click":  # [DIALOG:]ID: a control of the topmost dialog (a popup's OK, a menu's button)
            dialog, _, ident = a.value.rpartition(":")
            ident = int(ident)
            want = f"rez\\{dialog.lower()}.bin" if dialog else None
            for _ in range(CLICK_WAIT_STEPS):  # once that dialog is on top (menus open a frame or more later)
                if (want is None or self.top_bin() == want) and self.ctl.find(ident):
                    break
                self.wait(100)
            self.events[-1]["dialog"] = self.top_bin()
            if want and self.top_bin() != want:
                raise RuntimeError(f"click {a.value}: {self.top_bin()} is on top")
            self.click_control(ident)
        elif a.kind == "wait":
            self.wait(int(a.value))
        elif a.kind == "stop":
            raise ScriptStop(f"stopped by its script at {a.at}")
        elif a.kind != "cut":
            raise ValueError(f"unknown action {a.kind}")

    # ------------------------------------------------------------------ the screens
    def to_join_screen(self):
        spec, p = self.spec, self.player
        self.settle(GLUE_MAIN, "rez\\gluMain.bin")
        self.ev("press", keys="M")
        self.press("M")
        self.settle(GLUE_MAIN, "rez\\gluGameMode.bin")
        self.press("E" if spec.expansion else "O")
        self.settle(GLUE_CONN, "rez\\gluConn.bin")
        providers = self.list_items(CONN_LIST)
        self.ev("providers", items=providers)
        self.select_list_row(CONN_LIST, lambda t: t == PROVIDER_UDP)
        self.click_control(CONN_OK)
        self.settle(GLUE_LOGIN, "rez\\gluPEdit.bin")
        self.inp.text(p.name)
        self.inp.key(0x0D)
        self.settle(GLUE_LOGIN, "rez\\gluLogin.bin")
        self.press("O")
        self.settle(GLUE_JOIN, "rez\\gluJoin.bin")

    def host(self):
        from .custom import ComboError, select_combo
        spec = self.spec
        self.to_join_screen()
        self.ev("create")
        self.press("G")
        self.settle(GLUE_CREATE, "rez\\gluCreat.bin")
        combos = []
        if spec.load_save:  # a saved game's settings are its own: only its row is selected
            self.setup["load_save"] = self.select_list_row(CREATE_LIST, lambda t: t.lower().startswith(
                spec.load_save.lower()))
            self.wait(1500)
        try:
            if spec.game_type and not spec.load_save:
                combos.append(select_combo(self.inp, self.ctl, CTRL_GAME_TYPE, spec.game_type))
                self.wait(1500)
            if spec.subtype and not spec.load_save:
                combos.append(select_combo(self.inp, self.ctl, CTRL_SUBTYPE, spec.subtype))
                self.wait(500)
        except ComboError as e:
            raise RuntimeError(f"combo: {e}") from e
        self.setup["combos"] = combos
        self.capture("create")
        self.click_control(CREATE_OK)
        self.settle(GLUE_CHAT, "rez\\gluChat.bin")
        self.lobby()
        n = max(len(spec.players), spec.wait_players)
        if not self.run_until(lambda: self.lobby_players() >= n, spec.lobby_timeout_ms):
            raise RuntimeError(f"{self.lobby_players()} of {n} players in the chat room")
        self.ev("lobby_full", players=self.lobby_players())
        self.wait(2000)
        self.lobby_actions()
        self.capture("start")
        self.ev("start")
        self.click_control(CHAT_START)
        self.wait_game()

    def join(self):
        spec = self.spec
        self.to_join_screen()
        host = next((p for p in spec.players if p.role == "host"), None) or \
            next(p for p in spec.players if p.role == "external")

        def listed():
            return any(t.startswith(host.name) for t in self.list_items(JOIN_LIST))
        if not self.run_until(listed, spec.lobby_timeout_ms):
            raise RuntimeError(f"the host's game is not listed: {self.list_items(JOIN_LIST)}")
        self.wait(1500)
        game = self.select_list_row(JOIN_LIST, lambda t: t.startswith(host.name))
        self.ev("join", game=game)
        self.capture("join")
        self.click_control(JOIN_OK)
        self.settle(GLUE_CHAT, "rez\\gluChat.bin")
        self.lobby()
        n = len(spec.players)
        self.run_until(lambda: self.lobby_players() >= n, spec.lobby_timeout_ms)
        self.wait(1000)
        self.lobby_actions()
        self.wait_game()

    def lobby(self):
        self.capture("chat")

    def lobby_actions(self):
        for a in self.player.actions:
            if a.at == "lobby":
                self._act(a)
                self.wait(1000)

    def in_game(self) -> bool:
        return self.m.r16(self.game.GAME_MODE) == 3 and self.m.r32(self.game.FRAME_COUNTER) > 0

    def wait_game(self):
        """From the chat room into the game: a Use Map Settings game's ready room is started with S by every player;
        a player kicked or banned is back on gluJoin (the run ends there)."""
        m, game = self.m, self.game
        end = m.clock_us + self.spec.lobby_timeout_ms * 1000
        ready_pressed = False
        while not self.in_game():
            if m.clock_us >= end:
                raise RuntimeError(f"no game (mode {m.r16(game.GAME_MODE)}, glue {m.r32(game.GLUE_STATE)})")
            self.wait(250)
            glue = m.r32(game.GLUE_STATE)
            top = self.top_bin()
            if top in POPUPS_OK:  # why the room closed (SetLobbyLeaveReason 0x4506b0: "You have been banned!"...)
                self.capture("popup")
                self.ev("popup", dialog=top, text=self.control_text(POPUP_TEXT))
                self.click_control(POPUP_OK)
                continue
            if m.r16(game.GAME_MODE) == 4 and glue == GLUE_JOIN:
                self.ev("left_chat_room")
                raise ScriptStop("back on gluJoin from the chat room (kicked, banned or the game was closed)")
            if not ready_pressed and glue in GLUE_READY_ROOMS and m.r32(game.READY_ROOM_DIALOG):
                self.wait(2000)
                self.capture("ready_room")
                self.ev("ready_room_start")
                self.inp.key(ord("S"))
                ready_pressed = True
        self.st["started_at"] = m.clock_us // 1000
        self.ev("game")
        pending = sorted((a for a in self.player.actions if isinstance(a.at, int) and a.kind not in ("poke", "cut")),
                         key=lambda a: a.at)
        while self.st["last_frame"] < self.spec.frames and m.r16(game.GAME_MODE) == 3:
            self.run_until(lambda: self.st["last_frame"] >= self.spec.frames or m.r16(game.GAME_MODE) != 3
                           or (pending and self.st["last_frame"] >= pending[0].at), 500)
            while pending and self.st["last_frame"] >= pending[0].at and m.r16(game.GAME_MODE) == 3:
                self._act(pending.pop(0))
            self.check_timeout_dialog()

    def check_timeout_dialog(self):
        """The timeout dialog (waiting for lagging players): captured once per opening; with auto_drop, Drop
        Players is pressed once its countdown ended."""
        m = self.m
        dialog = m.r32(TIMEOUT_DIALOG)
        if not dialog:
            self.st.pop("timeout_seen", None)
            return
        if self.st.get("timeout_seen") != dialog:
            self.st["timeout_seen"] = dialog
            self.ev("timeout_dialog", seconds=m.r32(TIMEOUT_SECONDS))
            self.capture("timeout")
        c = self.ctl.find(TIMEOUT_DROP, dialog)
        if self.player.auto_drop and c and self.ctl.usable(c) and not m.r32(TIMEOUT_SECONDS):
            left, top, right, bottom = self.ctl.rect(c, dialog)
            self.ev("drop_players")
            self.inp.click((left + right) // 2, (top + bottom) // 2)
            self.wait(500)

    def finish(self) -> str:
        m, game = self.m, self.game
        if self.st["last_frame"] >= self.spec.frames:
            return "ok"
        return f"game ended at frame {self.st['last_frame']} (mode {m.r16(game.GAME_MODE)}, " \
               f"exit code {m.r16(game.GAME_EXIT_CODE)})"

    def park(self):
        """The driver is done; the machine runs on (without input) for the others until the coordinator stops."""
        self.link.state = "park"
        while self.link.stopped is None:
            self.m.run(stop=lambda: self.link.stopped is not None)

    def close(self, finished: str, t0: float) -> dict:
        from .session import write_json
        self.trace.close()
        for f in self.snet["files"]:
            f.close()
        write_json(self.out / "setup.json", self.setup)
        fs = self.m.services["fs"]
        written = {}
        for key, data in fs.overlay.items():  # what the guest wrote (saves, a downloaded map, profiles), kept
            name = fs.overlay_names.get(key, key)
            written[name] = len(data)
            if key not in self.preloaded and name.lower().rsplit(".", 1)[-1] in ("mlt", "mlx", "scm", "scx", "rep"):
                (self.out / "files").mkdir(exist_ok=True)
                (self.out / "files" / name.rsplit("\\", 1)[-1]).write_bytes(bytes(data))
        r = {"written": written, "player": self.player.name, "ip": self.player.ip, "role": self.player.role,
             "finished": finished, "last_frame": self.st["last_frame"], "started_at_ms": self.st["started_at"],
             "virtual_ms": self.m.clock_us // 1000, "wall_seconds": round(time.time() - t0, 1),
             "events": self.events, "visited": self.visited, "input_log": self.inp.log, "syncs": self.link.syncs,
             "stopped": self.link.stopped, "snet_calls": self.snet["calls"], "turns_sent": self.snet["turns_sent"],
             "turns_received": self.snet["turns_received"], "snet_return_sites": self.snet["return_sites"],
             "net": {"sent": self.stack.sent, "received": self.stack.received,
                     "dropped_no_socket": self.stack.dropped_local},
             "threads": [[f"{t.tid:#x}", f"{t.start:#x}", t.state] for t in self.m.threads],
             "missing_procs": sorted(self.m.services.get("missing_procs", ()))}
        write_json(self.out / "player.json", r)
        return r


def instance_main(conn, spec_json: dict, index: int, out: str) -> None:
    """A player's process (spawned): build, synchronize, play its role, park, report."""
    import os
    spec = GameSpec.from_json(spec_json)
    out_dir = Path(out) / spec.players[index].name
    out_dir.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, filename=out_dir / "run.log", filemode="w",
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    try:
        os.nice(10)
    except OSError:
        pass
    t0 = time.time()
    d = None
    finished = "?"
    try:
        d = Driver(spec, index, out_dir, conn)
        d.link.sync()                       # the first round
        d.s.start()
        try:
            d.host() if d.player.role == "host" else d.join()
            finished = d.finish()
        except LanStopped as e:
            finished = f"stopped: {e}"
        except ScriptStop as e:
            finished = f"script: {e}"
        except Exception as e:  # noqa: BLE001 - the run's fault is its result
            log.exception("player %s failed", spec.players[index].name)
            finished = f"fault: {str(e)[:1500]}"
            try:
                d.capture("fault")
            except Exception:  # noqa: BLE001
                pass
        if d.link.stopped is None and not finished.startswith("fault"):
            d.park()
        r = d.close(finished, t0)
    except Exception as e:  # noqa: BLE001
        log.exception("player %s: set-up failed", spec.players[index].name)
        r = {"player": spec.players[index].name, "finished": f"fault: {e}", "traceback": traceback.format_exc()}
    conn.send(("exit", r))
    conn.close()


class PipeEndpoint(lan.Endpoint):
    def __init__(self, conn, proc):
        self.conn, self.proc = conn, proc

    def send(self, msg):
        try:
            self.conn.send(msg)
        except (BrokenPipeError, OSError):
            pass

    def recv(self):
        try:
            return self.conn.recv()
        except (EOFError, OSError):
            return ("exit", {"finished": f"process died (exit code {self.proc.exitcode})"})


# ---------------------------------------------------------------------------------------------- the game
def run_game(spec: GameSpec, out: Path) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    from .session import write_json
    write_json(out / "spec.json", spec.to_json())
    t0 = time.time()
    lan_ = lan.VirtualLan(spec.seed, spec.delay_us, spec.jitter_us, spec.loss, out)
    events: list = []
    for p in spec.players:
        if p.role == "external":  # the bridge adds its peer when it first sends
            continue
        lan_.add_host(p.name, lan.ip_int(p.ip))
        for a in p.actions:
            if a.kind == "cut":  # exact: the switch compares each datagram's send time with it
                lan_.cut(p.name, int(a.value))
                events.append({"t": int(a.value), "member": p.name, "cut": True})
    ctx = mp.get_context("spawn")
    members, procs = [], []
    bridge = None
    try:
        bridge = lan.LoopbackBridge(lan_, spec.bridge_base) if spec.bridge_base else None
        for i, p in enumerate(spec.players):
            if p.role == "external":
                continue
            parent, child = ctx.Pipe()
            proc = ctx.Process(target=instance_main, args=(child, spec.to_json(), i, str(out)), name=f"sc-{p.name}")
            proc.start()
            child.close()
            procs.append(proc)
            members.append(lan.Member(p.name, PipeEndpoint(parent, proc)))
        coord = lan.Coordinator(lan_, members, wall_limit_s=spec.wall_limit_s, bridge=bridge,
                                realtime=spec.realtime, events=events)
        reason = coord.run()
        for proc in procs:
            proc.join(PROCESS_EXIT_S)
    finally:
        for x in members:  # a player still blocked in its pipe sees EOF
            x.endpoint.conn.close()
        for proc in procs:
            if proc.is_alive():
                log.warning("player process %s did not exit; terminating it", proc.name)
                proc.terminate()
                proc.join(PROCESS_EXIT_S)
        if bridge is not None:
            bridge.close()
        lan_.close()
    result = {"game": spec.name, "reason": reason, "rounds": coord.rounds, "wall_seconds": round(time.time() - t0, 1),
              "lan": lan_.stats, "events": events,
              "players": {x.name: x.result for x in members},
              "compare": compare(out, spec)}
    write_json(out / "net.json", result)
    return result


def compare(out: Path, spec: GameSpec) -> dict:
    """The players' frame digests (trace.jsonl) frame by frame and their state dumps by the decoder's categories."""
    traces = {}
    names = [p.name for p in spec.players if p.role != "external"]
    for name in names:
        path = out / name / "trace.jsonl"
        rows = {}
        if path.exists():
            for line in path.read_text().splitlines():
                r = json.loads(line)
                r.pop("t", None)
                rows[r["f"]] = r
        traces[name] = rows
    common = sorted(set.intersection(*(set(t) for t in traces.values()))) if traces else []
    first_diff = next((f for f in common if any(traces[n][f] != traces[names[0]][f] for n in names[1:])), None)
    res = {"frames_compared": len(common), "last_common_frame": common[-1] if common else None,
           "first_digest_difference": first_diff}
    if first_diff is not None:
        res["digests_at_difference"] = {n: traces[n][first_diff] for n in names}
    res["states"] = compare_states(out, names)
    return res


def compare_states(out: Path, names: list[str]) -> dict:
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    try:
        from native_state import Decoder
        from .state import State
    except ImportError as e:  # pragma: no cover - the decoder lives in tools/
        return {"error": str(e)}
    steps = sorted(set.intersection(*({p.name for p in (out / n / "states").glob("s*.state")} for n in names))) \
        if all((out / n / "states").exists() for n in names) else []
    report = {"dumps": len(steps), "equal": 0, "differ": [], "viewer_only": []}
    for step in steps:
        models = [Decoder(State(out / n / "states" / step)).model() for n in names]
        raw = differing(models)
        bad = differing([shared_model(mm, first=step == steps[0]) for mm in models])
        if bad:
            report["differ"].append({"dump": step, "categories": bad})
        else:
            report["equal"] += 1
            if raw:
                report["viewer_only"].append({"dump": step, "categories": raw})
    return report


def differing(models: list[dict]) -> list[str]:
    return sorted(c for c in models[0] if any(mm.get(c) != models[0][c] for mm in models[1:]))


def shared_model(model: dict, first: bool = False) -> dict:
    """A dump's model (native_state.Decoder) without what each instance keeps for its own viewer (VIEWER_FIELDS,
    VIEWER_TILE_FLAG) and, in a game's first dump, without what the menus left (MENU_FIELDS)."""
    out = dict(model)
    masks = {**VIEWER_FIELDS, **(MENU_FIELDS if first else {})}
    for cat, fields in masks.items():
        if cat in out:
            out[cat] = [(k, {f: v for f, v in row.items() if f not in fields}) for k, row in out[cat]]
    if "tiles" in out:
        out["tiles"] = [(k, {**row, "flags": "".join(f"{int(row['flags'][i:i + 8], 16) & ~VIEWER_TILE_FLAG:08x}"
                                                      for i in range(0, len(row["flags"]), 8))})
                        for k, row in out["tiles"]]
    return out


# ---------------------------------------------------------------------------------------------- the suite
def _players(n: int, **extra) -> list[Player]:
    names = ("Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta")
    return [Player(names[k], f"10.0.0.{k + 1}", "host" if k == 0 else "join", **extra) for k in range(n)]


def _with(players: list[Player], name: str, *actions: Action, **fields) -> list[Player]:
    p = next(x for x in players if x.name == name)
    p.actions += actions
    for k, v in fields.items():
        setattr(p, k, v)
    return players


def suite() -> dict[str, GameSpec]:
    """The network games of stage N1 beyond the 1v1 melee (`python -m sc_emulator net --suite NAME,...`). Each is
    small (a few hundred frames): the point is the path through the original's screens and network code, recorded."""
    from .custom import MAPS_DIR  # the emulator's package (unicorn); suite() runs only with it
    m = lambda name: str(STOCK_MAPS / name)  # noqa: E731
    # the "save" game's saved game (run it first), offered to every player of "load" in the maps folder
    saves = OUT_ROOT / "save"
    saved = next(iter(sorted((saves / "Alpha" / "files").glob("*.ml[tx]"))), saves / "Alpha" / "files" / "netsave.mlt")
    two = "(2)Challenger.scm"

    def boot(item: str) -> tuple[Action, ...]:
        """The host's kick or ban: the row's type menu item, the "will boot" popup's OK (ConfirmLobbySlotAction
        0x451640), then the chat room for a while (the booted player is back on gluJoin) and the end."""
        return (Action("lobby", "slot", item), Action("lobby", "click", f"gluPOkCancel:{POPUP_OK}"),
                Action("lobby", "wait", "8000"), Action("lobby", "stop"))

    games = [
        GameSpec("bw-1v1", m(two), _players(2), expansion=True, game_type="Melee", frames=150),
        GameSpec("ffa4", m("(4)Lost Civilization.scm"), _players(4), expansion=False, game_type="Free For All",
                 frames=400),
        GameSpec("team-melee", m("(4)Blood Bath.scm"), _players(4), expansion=False, game_type="Team Melee",
                 subtype="2", frames=400),
        GameSpec("top-vs-bottom", m(two), _players(2), expansion=False, game_type="Top vs Bottom", subtype="1 vs 1",
                 frames=400),
        GameSpec("ums-ready-room", m("scenario/(4)Zoo Keeper.scm"), _players(2), expansion=False,
                 game_type="Use Map Settings", frames=300),
        GameSpec("ctf", m(two), _players(2), expansion=False, game_type="Capture The Flag", frames=300),
        GameSpec("map-transfer", m(two), _with(_players(2), "Beta", has_map=False), expansion=False,
                 game_type="Melee", frames=100),
        GameSpec("chat", m(two), _with(_with(_players(2), "Beta", Action("lobby", "chat", "hello from Beta"),
                                             Action(60, "chat", "/w Alpha psst")),
                                       "Alpha", Action("lobby", "chat", "/w Beta welcome"),
                                       Action(40, "chat", "gl hf")),
                 expansion=False, game_type="Melee", frames=150),
        GameSpec("kick", m(two), _with(_players(2), "Alpha", *boot("remote:Open")), expansion=False,
                 game_type="Melee", frames=100),
        GameSpec("ban", m(two), _with(_players(2), "Alpha", *boot("remote:Ban Player!")), expansion=False,
                 game_type="Melee", frames=100),
        GameSpec("latency", m(two), _with(_players(2), "Alpha", Action(100, "keys", "F10 O N E O")),
                 expansion=False, game_type="Melee", frames=300),
        # the save dialog takes the name only once it is open (typed at once, the first letters were hotkeys)
        GameSpec("save", m(two), _with(_players(2), "Alpha", Action(100, "keys", "F10 S"), Action(100, "wait", "1500"),
                                       Action(100, "text", "netsave"), Action(100, "keys", "ENTER")),
                 expansion=False, game_type="Melee", frames=250),
        # each player its own copy (a joiner's request names its record in the save it has: OnLobbyJoinRequest
        # 0x470af0 rejects with reason 2 when that record is not free, as with the host's copy)
        GameSpec("load", m(two), [Player(x.name, x.ip, x.role, preload=[[f"{MAPS_DIR}\\{saved.name}",
                                                                          str(saves / x.name / "files" / saved.name)]])
                                  for x in _players(2)], expansion=False, load_save=saved.stem, frames=350),
        GameSpec("leave", m(two), _with(_players(2), "Beta", Action(150, "keys", "F10 E"),
                                        Action(150, "click", f"abrtmenu:{ABORT_QUIT_MISSION}"),
                                        Action(150, "click", f"quit2mnu:{QUIT_MISSION_OK}")),
                 expansion=False, game_type="Melee", frames=300),
        GameSpec("drop", m(two), _with(_players(2, auto_drop=True), "Beta", Action("net", "cut", "55000000")),
                 expansion=False, game_type="Melee", frames=600),
        # the desync: the first active unit's hit points on one player only (the 0x37 sync hashes the units)
        GameSpec("desync", m(two), _with(_players(2, auto_drop=True), "Beta",
                                         Action(200, "poke", "*665AE0+8:00010000")),
                 expansion=False, game_type="Melee", frames=500),
    ]
    return {g.name: g for g in games}
