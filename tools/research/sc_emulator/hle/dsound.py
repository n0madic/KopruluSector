"""DirectSound HLE: by default there is no sound device.

With ``machine.services["dsound_device"]`` false, ``DirectSoundCreate`` returns
DSERR_NODRIVER and the game runs its "no sound device" paths. Otherwise (the default
configuration) recording COM objects are created: buffers hold real guest memory, play
cursors advance with the virtual clock at the buffer format's byte rate (so status and
position queries behave like a sound card that plays in real time), and
Play/Stop/SetVolume/SetPan are logged. No audio is mixed.
"""
from __future__ import annotations

import logging
import struct

from . import api
from .ddraw import method, new_com, this

log = logging.getLogger(__name__)
DS = "dsound.dll"
DS_OK = 0
DSERR_NODRIVER = 0x88780078

DS_METHODS = ["QueryInterface", "AddRef", "Release", "CreateSoundBuffer", "GetCaps", "DuplicateSoundBuffer",
              "SetCooperativeLevel", "Compact", "GetSpeakerConfig", "SetSpeakerConfig", "Initialize"]
BUF_METHODS = ["QueryInterface", "AddRef", "Release", "GetCaps", "GetCurrentPosition", "GetFormat", "GetVolume",
               "GetPan", "GetFrequency", "GetStatus", "Initialize", "Lock", "Play", "SetCurrentPosition", "SetFormat",
               "SetVolume", "SetPan", "SetFrequency", "Stop", "Unlock", "Restore"]

for _iface in ("IDirectSound", "IDirectSoundBuffer"):
    method(DS, _iface, "AddRef", 0)(lambda m, p: 1)
    method(DS, _iface, "Release", 0)(lambda m, p: 0)
    method(DS, _iface, "QueryInterface", 2)(lambda m, p, iid, out: (m.w32(out, 0), 0x80004002)[1])


def log_event(m, kind, **kw):
    m.services.setdefault("dsound_log", []).append({"clock_ms": m.clock_us // 1000, "event": kind, **kw})


@api(DS, 3)
def DirectSoundCreate(m, guid, out, outer):
    """DSERR_NODRIVER unless a recording device is configured."""
    if not m.services.get("dsound_device"):
        if out:
            m.w32(out, 0)
        return DSERR_NODRIVER
    o = new_com(m, DS, "IDirectSound", DS_METHODS)
    m.w32(out, o.addr)
    return DS_OK


@api(DS, 2)
def DirectSoundEnumerateA(m, cb, ctx):
    """Enumerates nothing."""
    return DS_OK


@method(DS, "IDirectSound", "SetCooperativeLevel", 2)
def ds_SetCooperativeLevel(m, p, hwnd, level):
    """DS_OK."""
    return DS_OK


@method(DS, "IDirectSound", "GetCaps", 1)
def ds_GetCaps(m, p, caps):
    """Zeroed caps."""
    n = m.r32(caps)
    m.write(caps + 4, bytes(max(n - 4, 0)))
    return DS_OK


@method(DS, "IDirectSound", "CreateSoundBuffer", 3)
def ds_CreateSoundBuffer(m, p, desc, out, outer):
    """Buffer backed by guest memory of the requested size (primary buffers get 64 KiB)."""
    size, flags, nbytes, res, fmt = struct.unpack("<5I", m.read(desc, 20))
    nbytes = nbytes or 0x10000
    wfx = m.read(fmt, 18) if fmt else struct.pack("<HHIIHHH", 1, 2, 22050, 88200, 4, 16, 0)
    o = new_com(m, DS, "IDirectSoundBuffer", BUF_METHODS, size=nbytes, bits=m.alloc(nbytes), flags=flags,
                playing=False, volume=0, pan=0, wfx=wfx, freq=struct.unpack_from("<I", wfx, 4)[0],
                rate=max(struct.unpack_from("<I", wfx, 8)[0], 1), pos=0, start_us=0, start_pos=0, loop=False)
    m.w32(out, o.addr)
    log_event(m, "create", buffer=o.addr, bytes=nbytes, flags=flags)
    return DS_OK


@method(DS, "IDirectSound", "DuplicateSoundBuffer", 2)
def ds_DuplicateSoundBuffer(m, p, src, out):
    """Shares the source buffer's memory."""
    s = this(m, src).data
    o = new_com(m, DS, "IDirectSoundBuffer", BUF_METHODS, **dict(s, playing=False, pos=0))
    m.w32(out, o.addr)
    return DS_OK


@method(DS, "IDirectSoundBuffer", "Lock", 7)
def buf_Lock(m, p, off, n, p1, n1, p2, n2, flags):
    """Returns the region (wrapping at the end)."""
    d = this(m, p).data
    if flags & 2:
        off, n = 0, d["size"]
    off %= d["size"]
    first = min(n, d["size"] - off)
    m.w32(p1, d["bits"] + off)
    m.w32(n1, first)
    if p2:
        m.w32(p2, d["bits"] if n > first else 0)
    if n2:
        m.w32(n2, n - first)
    return DS_OK


@method(DS, "IDirectSoundBuffer", "Unlock", 4)
def buf_Unlock(m, p, p1, n1, p2, n2):
    """DS_OK."""
    return DS_OK


def position(m, d) -> int:
    """Play cursor on the virtual clock: start position + elapsed time x average bytes per
    second (WAVEFORMATEX.nAvgBytesPerSec). A looping buffer wraps; a one-shot buffer stops
    at its end (it then reports "not playing")."""
    if not d["playing"]:
        return d["pos"]
    pos = d["start_pos"] + (m.clock_us - d["start_us"]) * d["rate"] // 1_000_000
    if d["loop"]:
        return pos % d["size"]
    if pos >= d["size"]:
        d["playing"] = False
        d["pos"] = 0
        return 0
    return pos


@method(DS, "IDirectSoundBuffer", "Play", 3)
def buf_Play(m, p, r1, r2, flags):
    """Starts the virtual play cursor (DSBPLAY_LOOPING = 1 makes it wrap) and logs the play."""
    d = this(m, p).data
    if not d["playing"]:
        d["start_pos"], d["start_us"] = d["pos"], m.clock_us
    d["playing"] = True
    d["loop"] = bool(flags & 1)
    log_event(m, "play", buffer=p, flags=flags, volume=d["volume"], pan=d["pan"], bytes=d["size"])
    return DS_OK


@method(DS, "IDirectSoundBuffer", "Stop", 0)
def buf_Stop(m, p):
    """Freezes the play cursor; logged."""
    d = this(m, p).data
    d["pos"] = position(m, d)
    d["playing"] = False
    log_event(m, "stop", buffer=p)
    return DS_OK


@method(DS, "IDirectSoundBuffer", "SetVolume", 1)
def buf_SetVolume(m, p, v):
    """Stored."""
    this(m, p).data["volume"] = struct.unpack("<i", struct.pack("<I", v))[0]
    return DS_OK


@method(DS, "IDirectSoundBuffer", "SetPan", 1)
def buf_SetPan(m, p, v):
    """Stored."""
    this(m, p).data["pan"] = struct.unpack("<i", struct.pack("<I", v))[0]
    return DS_OK


@method(DS, "IDirectSoundBuffer", "SetFrequency", 1)
def buf_SetFrequency(m, p, v):
    """DS_OK."""
    return DS_OK


@method(DS, "IDirectSoundBuffer", "GetStatus", 1)
def buf_GetStatus(m, p, out):
    """DSBSTATUS_PLAYING (| DSBSTATUS_LOOPING) while the virtual play cursor runs; a one-shot
    buffer stops when the cursor reaches its end."""
    d = this(m, p).data
    position(m, d)
    m.w32(out, (1 | (4 if d["loop"] else 0)) if d["playing"] else 0)
    return DS_OK


@method(DS, "IDirectSoundBuffer", "GetCurrentPosition", 2)
def buf_GetCurrentPosition(m, p, play, write):
    """Play cursor from the virtual clock; the write cursor leads it by 10 ms of data."""
    d = this(m, p).data
    pos = position(m, d)
    lead = (d["rate"] // 100 + 3) & ~3 if d["playing"] else 0
    if play:
        m.w32(play, pos)
    if write:
        m.w32(write, (pos + lead) % d["size"])
    return DS_OK


@method(DS, "IDirectSoundBuffer", "SetCurrentPosition", 1)
def buf_SetCurrentPosition(m, p, pos):
    """Moves the play cursor."""
    d = this(m, p).data
    d["pos"] = pos % d["size"]
    d["start_pos"], d["start_us"] = d["pos"], m.clock_us
    return DS_OK


@method(DS, "IDirectSoundBuffer", "SetFormat", 1)
def buf_SetFormat(m, p, fmt):
    """DS_OK."""
    return DS_OK


@method(DS, "IDirectSoundBuffer", "GetVolume", 1)
def buf_GetVolume(m, p, out):
    """Stored volume."""
    m.w32(out, this(m, p).data["volume"])
    return DS_OK


@method(DS, "IDirectSoundBuffer", "GetFormat", 3)
def buf_GetFormat(m, p, wfx, size, written):
    """The WAVEFORMATEX given at creation."""
    data = this(m, p).data["wfx"]
    if wfx:
        m.write(wfx, data[:size])
    if written:
        m.w32(written, min(len(data), size) if wfx else len(data))
    return DS_OK


@method(DS, "IDirectSoundBuffer", "GetPan", 1)
def buf_GetPan(m, p, out):
    """Stored pan."""
    m.w32(out, this(m, p).data["pan"])
    return DS_OK


@method(DS, "IDirectSoundBuffer", "GetFrequency", 1)
def buf_GetFrequency(m, p, out):
    """Sample rate of the buffer format."""
    m.w32(out, this(m, p).data["freq"])
    return DS_OK


@method(DS, "IDirectSoundBuffer", "GetCaps", 1)
def buf_GetCaps(m, p, caps):
    """DSBCAPS: flags and buffer size."""
    d = this(m, p).data
    m.write(caps, struct.pack("<5I", 20, d["flags"], d["size"], 0, 0))
    return DS_OK


@method(DS, "IDirectSoundBuffer", "Restore", 0)
def buf_Restore(m, p):
    """DS_OK."""
    return DS_OK
