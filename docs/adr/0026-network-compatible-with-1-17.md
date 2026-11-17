# ADR 0026: Network play compatible with 1.17

Status: accepted (2026-11-17). The user approved phase N's design inline on 2026-11-17 ([phase N](../phase-network.md)). Stage N0 (the research of Storm's network layer and the UDP provider) is done, and so is stage N2 ([its additions](#addition-stage-n2-the-state-machines-and-their-transports)); the later stages add their decisions here as they are built.

## Context

Phase M ([ADR 0025](0025-seats-and-network-branches.md)) left the original's network branches in `kp_core` under the multiplayer byte and several seats of one game in one process over a turn hub, with no network under them. The executable has no transport: `StarCraft.exe` calls Storm's network API (SNet, in `storm.dll`), and Storm sends its packets through a provider loaded from a `.snp` file. Until phase N the atlas specified only the executable's side ([replay and network notes](../research/replay-network-notes.md#network-play)).

Network play could follow the original's protocol or a protocol of the remake's own. A protocol of its own would play only with Koprulu. The web cannot send UDP and cannot listen, and phones sit behind NATs, so any design needs some relay for them.

## Decision

1. **Compatible with the original.** Koprulu implements Storm's network layer and `standard.snp`'s "Local Area Network (UDP/IP)" provider (`'UDPN'`) byte for byte, so a Koprulu seat and an original 1.17 seat play one game. Battle.net, IPX, the cable and modem providers and version 1.16.1 stay out, and so does resuming a saved game of a lobby that mixes Koprulu and the original, whose saves differ (Koprulu answers "Save Game File Not Found", as the original does without the file).
2. **The specification is the atlas.** Storm's network layer is package [S02](../research/atlas/s02.md), the UDP provider [U01](../research/atlas/u01.md), and the wire format [storm-udp.md](../formats/storm-udp.md). Each entry was reviewed independently from the disassembly. The wire format was then checked on the original's own traffic (decision 3): every one of 26,923 datagrams of 20 recorded games had a valid checksum and length.
3. **The oracle is the original over an emulated Winsock.** The [full-game emulator](../research/full-game-emulation.md) runs the real `storm.dll` and `standard.snp`, and replaces only `ws2_32` and `wsock32` by a virtual LAN between emulator instances, with a bridge to real UDP on the loopback. Its recordings (the datagrams, Storm's calls, the turns, the lobby's screens, every instance's states) stay in the private workspace. Phase M's "no SNet HLE" is superseded: the network's branches can now be observed.
4. **The layers follow the original's split:**
   - `kp_core`: the simulation and its network branches, and the sync command `0x37`'s inputs byte for byte (`ComputeSyncValue` `0x47a360`, the sprite visibility checksum `0x417540`–`0x417610`). The original computes the checksum in its draw path; Koprulu takes it inside the step, right after the first frame past a turn boundary, with the camera's row from the seat's interface.
   - `kp_core/src/ui`: the original's Multiplayer screens.
   - `kp_app`: the game's network code (turn manager, lobby protocol, map transfer, chat, leaves).
   - `kp_app/net`: `kp_snet` (Storm's SNet, with the API the executable calls) over providers (UDP, the relay, an in-process loop for tests).
5. **Storm and its providers are pure state machines.** Datagrams and the time go in; datagrams and events come out; there is no I/O inside. Tests drive them from recorded traffic and with simulated time.
   - **Sockets:** a thin driver per platform puts them on sockets.
   - **Threads:** on the native platforms a network thread runs the state machines (acknowledgements, resends, pings), so that a long mission load does not miss Storm's timeouts. On the web the main loop runs them.
   - **Determinism:** the time changes only when a turn arrives, never what it holds.
6. **The web and the internet go through `koprulu-relay`.** It is a virtual LAN over TCP and WebSocket that carries Storm's UDP datagrams unchanged, broadcasts within a room, and does not understand the game. TLS is a proxy's (Caddy or nginx) in front of it; the native clients connect over plain TCP. Its framing gets its own format document with stage N5.
7. **Nothing of Koprulu's own goes on the wire.** An original peer would not understand it. Desyncs are caught by the original's sync command and resolved by its `ResolveDesync` `0x47a630`, whose result in a split 1v1 (each side drops the other and wins) is kept. Between Koprulu seats in tests and in `--seats`, the shared state hash of ADR 0025 is compared besides.

## Consequences

- Koprulu's network peers interoperate with 1.17. Data identity needs no check of its own: Koprulu's rule files equal 1.17's ([replay and network notes](../research/replay-network-notes.md#the-rule-files-of-117-and-remastered-2026-11-14)), and Storm's checks at join (program ID, version `0xD5`, the lobby's version 5) are the original's.
- The original's provider has weaknesses a peer must never trigger: a receive error (a datagram over 504 bytes among them) ends that receiving thread for good, and an advertisement whose strings run past its length makes the receiver fail fatally. Koprulu's sender never produces either, and its receiver rejects both instead of failing.
- Two instances cannot share one machine's port 6112 (the original sets no `SO_REUSEADDR`). Local tests use the in-process loop provider or the emulator's virtual LAN.
- On iOS, broadcasting needs Apple's multicast entitlement. Without it an iOS seat plays through the relay.
- The sync command makes Koprulu's simulation answerable to the original's every turn: any divergence from 1.17 in a mixed game drops a seat, which is the strongest check the remake has had.

## Addition: stage N2, the state machines and their transports

Built on 2026-11-17 ([phase N](../phase-network.md#n2-kp_snet-and-the-udp-provider)).

1. **Two state machines, no third provider.** `kp_snp_udp` is the provider and `kp_snet` is SNet over it. The in-process provider of decision 4 (`kp_snp_loop`) is not built: the tests and the fuzz targets run the UDP provider itself over an in-process LAN (`kp_net_lan`), so Koprulu↔Koprulu checks carry the bytes a 1.17 peer gets. The relay of stage N5 will be another transport under the same provider.
2. **The pump belongs to the driver.** `kp_snet_pump` is one pass of Storm's network thread: the driver runs it when a datagram arrived (signalled) or when the wait the last pass returned has run out. `kp_net_sock` waits on the sockets and starts no thread. The network thread of decision 5 belongs to the game's network code (stage N4), which owns the driver. `kp_snet_join_game` alone pumps for itself, as `SNetJoinGame` does, and waits through the host's sleep.
3. **Several programs on one machine share a port map.** The original binds ports 6111 and 6112 without `SO_REUSEADDR`. `kp_net_sock` therefore maps virtual hosts 10.0.0.k to the loopback ports BASE+2k and BASE+2k+1, which is the network oracle's bridge. A program outside the map binds two adjacent ports and is 10.0.1.n to the bridge, which is how Koprulu joins an emulated 1.17 game.
4. **The acceptance is the original's recorded traffic.** `test_net_trace` replays each recorded player's inputs on the emulator's clock: the datagrams that arrived, StarCraft's calls of SNet, and Storm's passes, with each pass's preemptions. Every datagram Koprulu sends must be the original's, byte for byte and in order, each within 50 ms of the original's time; every result, wait and event must equal the original's.
5. **Departures from the original are listed in the headers.** They cover only inputs the original mishandles (a datagram over 504 bytes, an advertisement whose strings overrun, a short control command, a handler that deletes the record being dispatched) and bounds on the memory that forged addresses grow (remote records, filed packets, queued datagrams, listed games). None of them changes a datagram that a 1.17 peer sends or accepts.

