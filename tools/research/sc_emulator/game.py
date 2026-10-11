"""StarCraft 1.17.0.1 game knowledge used by the observers (addresses from the atlas).

Nothing here changes guest state: observers only read memory at hooked addresses.
"""
from __future__ import annotations

import hashlib
import struct

from unicorn import x86_const as X

# --- globals (atlas package in parentheses)
FRAME_COUNTER = 0x52651C        # u32, incremented before each object step (P15)
RNG_STATE = 0x67393C            # u32 game RNG state (P26 0x4781d0)
RNG_INITIAL_SEED = 0x673934     # u32 (P26 0x4781e0)
GAME_MODE = 0x6662B8            # u16 game/glue mode: 3 game, 4 glue (P22)
GLUE_STATE = 0x66A274           # glue state (P19)
GAME_RUNNING = 0x6663ED         # u8 (P15)
GAME_EXIT_CODE = 0x666420       # u16 (P15)
SPEED_INDEX = 0x6D3130          # u32 game speed (P15/P45)
MISSION_INDEX = 0x526524        # u16 current campaign mission (P41)
MAP_NAME = 0x527120             # current map title (P41 0x4bb6c0)
READY_ROOM_DIALOG = 0x66B40C    # ready-room dialog (P21)
UNIT_POOL = 0x5DA3A0            # CUnit index 1 (P08); 1700 records of 0x150 bytes
UNIT_SIZE = 0x150
UNIT_COUNT = 1700
UNIT_ACTIVE_HEAD = 0x665AE0     # first active (visible) unit (P08)
UNIT_HIDDEN_HEAD = 0x665AF8     # first hidden unit (P08)
MINERALS = 0x5263D0             # u32[12] (P02)
GAS = 0x526400                  # u32[12] (P02)
COUNTDOWN = 0x5349D4            # trigger countdown seconds (P40/P41)
ELAPSED = 0x5349D8              # trigger elapsed seconds (P41)
TRIGGER_WAIT_ACTIVE = 0x6CA760  # u8[8] (P41)
TRIGGER_WAIT_MS = 0x6CA768      # u32[8] (P41)
TRIGGER_RESULTS = 0x5349E0      # u8[8] committed results (P41)
SWITCHES = 0x534F20             # u32[8] (P41)

# --- code addresses
FRAME_END_PC = 0x444F76         # in RunPendingFrames 0x444e30, right after the trigger scheduler call (P15)
STEP_OBJECTS = 0x445070         # one simulation frame (P15)
RUN_GAME = 0x445310             # RunGame (P15)
GLUE_LOOP = 0x457510            # glue loop entry (P19)
SET_PENDING_MISSION = 0x457e10  # ECX mission, EDX race, stack expansion; ret 4 (P19)
SOUND_HOOKS = {                 # (P35, P24)
    0x49E4E0: "PlaySoundInternal",
    0x49E740: "PlaySound",
    0x49E780: "PlaySoundAtVolume",
    0x49E810: "PlayStreamedSoundFile",
    0x46EF10: "PlayMusicTrack",
    0x46F1B0: "StartMusicTrack",
}

# CUnit fields (BWAPI 1.16.1 layout, confirmed by the atlas)
U_NEXT = 0x04
U_HP = 0x08
U_POS = 0x28
U_OWNER = 0x4C
U_ORDER = 0x4D
U_SHIELDS = 0x60
U_TYPE = 0x64


def _walk(pool: bytes, head: int, limit=UNIT_COUNT + 1):
    p = head
    n = 0
    while p and n < limit:
        off = p - UNIT_POOL
        if not (0 <= off < len(pool)):
            break
        yield off
        p = struct.unpack_from("<I", pool, off + U_NEXT)[0]
        n += 1


def digest(m) -> dict:
    """Compact per-frame state digest."""
    pool = m.read(UNIT_POOL, UNIT_SIZE * UNIT_COUNT)
    rows = []
    for head in (UNIT_ACTIVE_HEAD, UNIT_HIDDEN_HEAD):
        for off in _walk(pool, m.r32(head)):
            rows.append(struct.pack("<HBHHiBH", struct.unpack_from("<H", pool, off + U_TYPE)[0], pool[off + U_OWNER],
                                    *struct.unpack_from("<HH", pool, off + U_POS),
                                    struct.unpack_from("<i", pool, off + U_HP)[0], pool[off + U_ORDER],
                                    (off // UNIT_SIZE) + 1))
    active = sum(1 for _ in _walk(pool, m.r32(UNIT_ACTIVE_HEAD)))
    h = hashlib.sha1(b"".join(rows)).hexdigest()[:16]
    waits = m.read(TRIGGER_WAIT_ACTIVE, 8)
    wait_ms = struct.unpack("<8I", m.read(TRIGGER_WAIT_MS, 32))
    return {
        "f": m.r32(FRAME_COUNTER),
        "rng": m.r32(RNG_STATE),
        "u": active,
        "uh": len(rows) - active,
        "h": h,
        "cd": m.r32(COUNTDOWN),
        "el": m.r32(ELAPSED),
        "w": [wait_ms[i] if waits[i] else -1 for i in range(8)],
        "min": list(struct.unpack("<8I", m.read(MINERALS, 32))),
        "gas": list(struct.unpack("<8I", m.read(GAS, 32))),
        "res": list(m.read(TRIGGER_RESULTS, 8)),
        "t": m.clock_us // 1000,
    }


def sound_event(m, name: str) -> dict:
    ecx, edx, esp = m.reg(X.UC_X86_REG_ECX), m.reg(X.UC_X86_REG_EDX), m.esp
    ev = {"f": m.r32(FRAME_COUNTER), "t": m.clock_us // 1000, "fn": name}
    if name in ("PlaySoundInternal", "PlaySoundAtVolume"):
        pan = struct.unpack("<i", m.read(esp + 4, 4))[0]
        unit = m.r32(esp + 8)
        ev.update(id=ecx, vol=struct.unpack("<i", struct.pack("<I", edx))[0], pan=pan,
                  unit=((unit - UNIT_POOL) // UNIT_SIZE + 1) if unit else 0,
                  unit_type=m.r16(unit + U_TYPE) if unit else None)
    elif name == "PlaySound":
        ev.update(id=ecx)
    elif name == "PlayStreamedSoundFile":
        ev.update(path=m.cstr(ecx).decode("latin-1"))
    else:
        ev.update(track=ecx)
    return ev
