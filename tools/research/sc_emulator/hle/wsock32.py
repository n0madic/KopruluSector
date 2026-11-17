"""WSOCK32 HLE: the WinSock 1.1 exports, the same implementations as ws2_32.py under wsock32.dll's own ordinals.

standard.snp's UDP provider takes its socket functions from wsock32.dll by ordinal (Udp.cpp, 0x1d0084a0: 2, 3, 9, 17,
20, 21, 23, 111, 115, 116) and only WSAIoctl from ws2_32.dll. The two DLLs number their first 23 exports alike except
inet_addr, inet_ntoa and ioctlsocket (wsock32: 10, 11, 12; ws2_32: 11, 12, 10).
"""
from __future__ import annotations

from ..machine import ApiSpec
from . import REGISTRY

D = "wsock32.dll"
ORDINALS = {
    "bind": 2, "closesocket": 3, "getsockname": 6, "getsockopt": 7, "htonl": 8, "htons": 9, "inet_addr": 10,
    "inet_ntoa": 11, "ioctlsocket": 12, "ntohl": 14, "ntohs": 15, "recv": 16, "recvfrom": 17, "select": 18,
    "send": 19, "sendto": 20, "setsockopt": 21, "socket": 23, "gethostbyname": 52, "gethostname": 57,
    "WSAGetLastError": 111, "WSASetLastError": 112, "WSAStartup": 115, "WSACleanup": 116,
}


def register() -> None:
    for name, ordinal in ORDINALS.items():
        src = REGISTRY[("ws2_32.dll", name)]
        spec = ApiSpec(D, name, src.fn, src.nargs, src.cdecl, f"As WS2_32's {name} (WinSock 1.1 ordinal {ordinal}).")
        REGISTRY[(D, name)] = spec
        REGISTRY[(D, ordinal)] = spec


register()
