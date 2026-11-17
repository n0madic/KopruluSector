# Network play compatible with 1.17 (phase N)

Phase M ([phase-multiplayer.md](phase-multiplayer.md)) left several seats of one game in one process over a turn hub (`kp_hub.{h,c}`), the original's network branches under the multiplayer byte `0x672664` (fixture-verified), and no network: no transport, lobby, leave, drop or chat. Phase N ([ADR 0026](adr/0026-network-compatible-with-1-17.md)) adds network play that is compatible with StarCraft 1.17.0.1 itself: Koprulu speaks the original's "Local Area Network (UDP/IP)" provider byte for byte, so it plays with Koprulu and with the original game.

## Decisions (the user, 2026-11-17)

- **Compatibility with the original.** The transport is the original's: `storm.dll`'s network layer (SNet) over `standard.snp`'s UDP provider (`'UDPN'`). Out of scope: Battle.net (`battle.snp`), IPX, the modem and the cable providers, version 1.16.1, and resuming a saved game of a mixed lobby (Koprulu with the original: their saves differ; Koprulu answers "Save Game File Not Found" as the original does without the file).
- **The lobby is the original's screens:** gluConn, gluLogin, gluJoin, gluCreat, gluChat, the countdown, `timeout.bin` and `netdlg.bin`, with the lobby's messages in the turn stream as the [atlas](research/atlas/README.md) specifies them ([replay and network notes](research/replay-network-notes.md#network-play)).
- **The oracle is the original over an emulated Winsock.** The [full-game emulator](research/full-game-emulation.md) runs the real `storm.dll` and `standard.snp`; only `ws2_32` and `wsock32` are replaced, by a virtual LAN between emulator instances with a bridge to real UDP on the loopback. The packets themselves become goldens, and Koprulu is checked live against the emulated 1.17. This replaces phase M's "no SNet HLE".
- **The web and the internet go through `koprulu-relay`**, a virtual LAN over TCP and WebSocket that carries Storm's UDP datagrams unchanged (the last stage). TLS is a proxy's (Caddy or nginx) in front of the relay; the native clients use plain TCP.

## Layers

The remake keeps the original's split: `StarCraft.exe` → `storm.dll` (SNet) → a `.snp` provider → sockets.

1. `kp_core`: the simulation and its network branches; new, the sync command `0x37`'s inputs byte for byte (`ComputeSyncValue` `0x47a360`, the sprite visibility checksum `0x417540`–`0x417610`, taken inside the step after the first frame past a turn boundary) and the network players' state (`0x673818`, the drop flags `0x529042`).
2. `kp_core/src/ui`: the screens of [P17](research/atlas/p17.md)–[P20](research/atlas/p20.md), [P24](research/atlas/p24.md) and [P27](research/atlas/p27.md), reading the network's state through host hooks.
3. `kp_app`: the game's network code of P24–P27 (turn manager, `0x05`/`0x37`, the lobby protocol, map transfer, chat, leaves).
4. `kp_app` (network): `kp_snet` (Storm's SNet, the API the executable calls) over the UDP provider `kp_snp_udp`, both pure state machines (datagrams and time in, datagrams and events out), over a transport: the system's sockets (`kp_net_sock`), the relay (stage N5) or, in the tests, an in-process LAN (`kp_net_lan`; [N2](#n2-kp_snet-and-the-udp-provider)). The game's network code runs them on a network thread on the natives and from the main loop on the web.
5. `tools/relay`: `koprulu-relay`.

## Stages

| Stage | Content | Status |
|---|---|---|
| N0 | `storm.dll`'s SNet and `standard.snp`'s UDP provider in the atlas (packages S02, U01) | done 2026-11-17 |
| N1 | the network oracle in the emulator: Winsock HLE, virtual LAN, the Multiplayer menus' driver, recordings | done 2026-11-17 |
| N2 | `kp_snet` and the UDP provider, the socket drivers and the in-process LAN | done 2026-11-17 |
| N3 | the lobby: screens, protocol, map transfer, network saves | |
| N4 | the game over the network: `0x05`/`0x37`, `ResolveDesync`, leaves and drops, chat, the network menus | |
| N5 | `koprulu-relay`, the web, the phones | |
| N6 | acceptance | |

## N0: Storm's network and the UDP provider (done 2026-11-17)

The atlas specified the executable's side of network play in its second pass (2026-11-14). Storm's own network layer and the providers were left out (S01's six network imports the specified code calls, and Storm's internals). N0 specifies them from the disassembly, with an independent review of every entry, as the atlas did for the executable.

- **Inputs.** `storm.dll`'s Ghidra export (as for [S01](research/atlas/s01.md)) and a new one of the 1.17 install's `standard.snp` (439 functions; Ghidra 12.2, kept in the private workspace with the other analyses).
- **`standard.snp`'s providers** (`SnpQuery`, `SnpBind`): 0 IPX, 1 Direct Cable, 2 the single-player provider (identifier 0, no name, 1 player, 30 turns a second), 3 Modem, 4 Local Area Network (UDP/IP). The UDP provider's identifier is `'UDPN'` (`0x5544504e`) and its caps are: flags `0x00000003` (`SnpQuery` reports `0x20000003`, but Storm builds its record from `caps.dat`; it tests only bit 0 and its listing filter), messages of at most 504 bytes, a queue of 16, 256 players, 100,000 bytes a second, a latency of 50 ms, 8 turns a second and 2 turns in transit. The last two are the values phase M's turn hub chose. Its function table at `0x1d01b2e8` (0x70 bytes) holds 19 functions, four of them shared stubs Ghidra made no function of (`0x1d005d10`, `0x1d0036a0`, `0x1d006940`, `0x1d007990`).
- **Package S02** (`storm.dll`'s SNet module, `0x15020040`–`0x15028500`): the module's 112 functions, 36 of them S01's entries (revisited where S01 excluded them), 76 new.
  Written 2026-11-17 ([S02](research/atlas/s02.md), wire format in [storm-udp.md](formats/storm-udp.md)): 59 specified, 14 excluded (Battle.net, leagues, ladders, modems), 3 covered (`SReg` helpers). In S01, seven network imports went from excluded to specified (103, 105, 106, 112, 118, 133, 137) and seven gained detail; each changed entry waits for the review. What it found, from the static disassembly (no network path has run in the emulator yet):
  - **One provider datagram per Storm packet**, no compression, encryption or fragmentation, behind a 12-byte header: a checksum (mod-255, Fletcher style), the length, a sequence and a cumulative acknowledgement (per destination and class), the class, the command, the sender's ID (`0xff` before it has one) and flags (acknowledgement only, resend request, relayed).
  - **Four classes:** 0 control (connect, accept, ack, ping and pong, player info, join request, game info, the template that raises event 1, reject, leave `0x0B`, drop `0x0C`, new host, mode, ladder type), 1 messages and 2 turns (both in order per sender), 3 unordered datagrams (1.17 sends none).
  - **Reliability:** classes 0 and 1 resend their oldest unacknowledged packet per destination after a timer from the caps (306–311 ms over UDP). Turns are never resent by a timer: a seat missing a turn asks its sender and every other seat, and any holder relays it.
  - **Joining:** connect three times, accept, join request (name, description, password), then game info, the players, the ladder type, the template and the host's turns; the joiner gets the lowest free ID and starts at the host's count of sent turns.
  - **A leave is deterministic:** `0x0B` carries the leaver's count of sent turns, and every seat removes the leaver only after running all of his turns below it. So the game's `ProcessLeaveReasons` (`0x477da0`, after a received turn) handles it at the same turn everywhere, and replay command `0x57` is the same on every seat. A drop (`0x0C`) is each seat's own view, so it is not. When the host leaves, the remaining seat with the lowest ID becomes the host (`0x0D`).
  - **Threads:** the pump thread `0x15022780` runs at the highest priority on an auto-reset event; events reach the game outside Storm's lock.
- **Package U01** (`standard.snp`): `SnpQuery`, `SnpBind`, the UDP provider's code and the shared code it reaches (39 functions and the four stubs), and the 20 library functions it calls directly (Storm's imports, Winsock's, the static C runtime's). The IPX, cable and modem providers stay outside the atlas, like Battle.net.

  Written 2026-11-17 ([U01](research/atlas/u01.md), the "UDP provider" section of [storm-udp.md](formats/storm-udp.md)): 33 specified, 10 excluded (the provider's own game dialog, the address comparison Storm never calls, the performance counters StarCraft does not import, three stubs), 20 library. What it found, from the static disassembly and the install's `caps.dat`:
  - **Winsock 1.1** from `wsock32.dll` (ten functions by ordinal); `ws2_32.dll` only to turn off `SIO_UDP_CONNRESET`.
  - **Two blocking UDP sockets with `SO_BROADCAST`:** port 6111 for the game list, port 6112 for Storm's packets (one packet per datagram, as it is). No `SO_REUSEADDR`, so a second instance on one machine cannot bind 6112. A peer's address is a 16-byte `sockaddr_in` (port and IP in network order); the provider never learns its own IP.
  - **The game list:** datagrams to `255.255.255.255:6111` behind a 20-byte header (Storm's checksum, length, type, program ID, version, mode): 0 an advertisement (name, stat string, the 12-byte player record), 1 its withdrawal, 2 a request. An advertisement is broadcast when Storm advertises and sent to whoever asks; requests go out at most every 400 ms; a listed game expires after 10 s; a game of another program ID is not listed, one of another version is refused at the join (`0x85100079`).
  - **Threads:** one receiving thread on 6111, two on 6112 at the highest priority, each queueing datagrams of up to 504 bytes for Storm's pump.
  - **Risks for the remake's peers:** any receive error, a datagram over 504 bytes among them, ends that receiving thread for good; an advertisement whose strings run past its length makes the receiver ask for a negative allocation, which is fatal. A Koprulu peer must never send either.
  - **S02 confirmed:** the device list is empty (so `SNetInitializeDevice` stays excluded), no external messages (no event 4 over UDP), the latency of 50 ms; the game type over UDP is always 0.

**The review** (2026-11-17): an independent reviewer re-derived every entry of S02, U01 and the 14 changed entries of S01 from the disassembly, and checked the wire format on stage N1's recordings of the original's own traffic (20 games of two to four seats over the emulator's virtual LAN). 140 entries were confirmed and 13 corrected (S02 9, U01 1, S01 3); the atlas now has 6221 entries, all reviewed (`verify_atlas.py`: 0 errors).
- **Corrections that change the wire:**
  - a connect (control command 1) is never filed or acknowledged;
  - a turn request addressed to the receiver itself is resent with the current acknowledgement and a new checksum; only class 2 is relayed;
  - a missing turn's request carries the player's next turn as its sequence;
  - the ping of a silent peer runs for every present peer and is not resent while the pump thread waits without a timer;
  - control sequences start at 1 (messages at 0);
  - `SNetCreateLadderGame` (138) serves every provider, and its first advertisement goes out before mode bit 4, so list requests are answered with mode 0 until the first join.
- **Runtime confirmation** ([storm-udp.md](formats/storm-udp.md)): all 26,923 datagrams had a valid checksum and a length equal to their size (26,416 Storm packets on 6112, 507 list datagrams on 6111); every one of the 24 joins ran the documented handshake; a leave sent `0x0B` three times with the leaver's next turn; after a cut link each side asked for the missing turn every 50 ms and pinged once, and the game dropped the player about 48 s later, one survivor advertising itself as the new host. Idle pings came every 20 s; standalone acknowledgements left about 30 ms (classes 0 and 1) and 255 ms (class 2) after their packet.
- **Not exercised by the recordings:** the withdrawal of an advertisement and the shutdown's wake datagrams, the reject, drop and new-host commands (10, 12, 13), 13-byte turn requests and relays, class 3, the timer resends of classes 0 and 1 (every loss hit class 2), datagrams over 504 bytes. Their entries stay specified from the disassembly.
- **Open:** the join's loop of nine turn requests (16 of 20 games) is tied to the host's early turns arriving before the game info, by the packets' timing and the call log; the exact gap test was not traced instruction by instruction.

## N1: the network oracle in the emulator (done 2026-11-17)

The [full-game emulator](research/full-game-emulation.md#network-games-phase-n-stage-n1) runs two to four whole originals, each in a process of its own, over a virtual LAN: `hle/ws2_32.py` and `hle/wsock32.py` replace Winsock, `lan.py`'s coordinator keeps the clocks together by conservative rounds and routes the datagrams through a seeded switch (delay, jitter, loss, a cut cable), and `net.py` drives every player through the original's Multiplayer screens with its own inputs. `--bridge PORT` maps each emulated host's ports 6111 and 6112 to real UDP on the loopback, for the live checks of N2–N4. The recordings (`lan.pcap`, Storm's calls, the turns, the lobby's screens, the state dumps) stay in the private workspace; `tools/tests/test_emulator_net.py` checks the LAN and the driver.

- **The suite** ([results](research/full-game-emulation.md#network-game-results-stage-n1)): all 16 games were played: melee 1v1 to frame 2,001 (equal digests at every frame, the 9 state dumps equal in the shared simulation), Brood War 1v1, FFA of four, Team Melee, Top vs Bottom, the Use Map Settings ready room, Capture The Flag, map transfer, chat and whispers, kick, ban, the latency command, a network save and its load, a leave, a drop after a cut cable and a desync by a memory write. Two runs with one seed and 2 % loss gave byte-identical records.
- **What differs between two players' dumps** is only each instance's viewer data, which the atlas specifies (the megatiles displayed through its fog and their creep edges, tile flag `0x40000000`, the images' world x/y that the draw recomputes); the shared-state comparison leaves it out.
- **For N4, the sync command `0x37`:** bytes 2–4 (turn and schedule, the hash WORD) are equal on every player; the last three (`rec+3`, the low byte of `rec+8`, `rec+5`) are constant per player from the fourth turn on and differ between players (`98 6c 12` for the host, `00 00 0e` for the joiner in the 1v1). A byte-exact `0x37` needs each player's own values.
- **For N3, network saves:** each player loads its own copy of the save (with the host's copy the joiner is rejected with reason 2); a loaded game resumes one frame after the save but is not its continuation frame for frame (another RNG state at its first frame).
- **For N4, drops and desyncs:** a cut cable opens the timeout dialog on both sides, and its Drop Players runs `ResolveDesync` `0x47a630` and `SNetDropPlayer`(the other, `0x40000006`); a desync does the same without the dialog, two frames after the sync commands' hashes differ. In a 1v1 each side drops the other and plays on alone.
- **Open:** the meaning of `0x37`'s last three bytes and the cause of the loaded game's new RNG state; `--bridge` checked then only by a unit test (Koprulu joined through it in N2); a Brood War save (`.mlx`) and a banned player's second join not played; the drop game's idle timeout dialog runs about 40 s of wall time per virtual second (36 min for the game).

## N2: `kp_snet` and the UDP provider (done 2026-11-17)

Storm's network layer and `standard.snp`'s UDP provider, ported from the atlas's S02 and U01 entries as pure state machines, with their transports.

- **The layers** (`native/kp_app`, in `kp_app_core`):
  - `kp_net.{h,c}`: what they share. A provider address as Storm keeps it (Winsock's `sockaddr_in`, a player record's `+0x108`), Storm's checksum (`FletcherSum` `0x15021c50`, `ComputePacketChecksum` `0x15021ca0`), and `KpNetIo`, a provider's I/O (a send from one of its sockets, the millisecond clock).
  - `kp_snp_udp.{h,c}`: the provider `'UDPN'`. Socket A (port 6111) carries the game list: advertisements, list requests, stops, the list's ten-second expiry and the 400 ms between requests. Socket B (port 6112) carries Storm's datagrams. The functions are the slots of `SnpBind`'s table.
  - `kp_snet.{h,c}`: SNet over the provider. It has the exports StarCraft calls, with their arguments, results and Storm's error codes, and the pump thread's pass `kp_snet_pump`: it files what the provider received and sends acknowledgements, resends, turn requests and pings, and returns its next wait. `kp_snet_join_game` pumps the provider itself, as `SNetJoinGame`'s `PumpUntilControl` does, and waits through the host's sleep. Events go to the host's handler from the calls Storm dispatches them in.
  - The transports:
    - `kp_net_sock.{h,c}`: the system's UDP over POSIX sockets and Winsock. On the web it reports that there are no sockets (the relay is stage N5's).
    - `kp_net_lan.{h,c}`: the in-process LAN of the tests and the fuzz targets. It has hosts, a seeded delay, jitter and loss, and cut cables. A host's own broadcasts come back to it at once, as on Windows.
- **No loop provider.** The plan had a third provider for the tests (`kp_snp_loop`). Instead the UDP provider itself runs over the in-process LAN, so a Koprulu↔Koprulu test carries the bytes a 1.17 peer would get.
- **Several programs on one machine** (`kp_net_sock`'s map). Two programs cannot both bind port 6112, so the map gives each virtual host 10.0.0.k two real loopback ports, BASE+2k and BASE+2k+1, as the network oracle's bridge does. Koprulu's own games on one machine use the map too. A program the map does not hold, such as Koprulu against the bridge, binds two adjacent ports of its own; the bridge names it 10.0.1.n.
- **Where Koprulu differs from the original.** Only for inputs the original mishandles, and to bound the memory that forged addresses would grow; the headers list each case:
  - a datagram over 504 bytes is dropped (the original's receiving thread ends);
  - an advertisement whose strings run past its end is ignored (fatal in the original);
  - a short control command reads zeros past its end;
  - a handler that deletes the record being dispatched ends the dispatch;
  - `SNetReceiveTurns`'s slots outside 0–254 have no record (the original compares an id's low byte, so slot 256 + k takes player k's turn a second time);
  - at most 1024 remote records (`KP_SNET_RECORDS`), 16,384 filed packets (`KP_SNET_NODES`), 4096 queued datagrams (`KP_SNP_QUEUE`) and 256 listed games (`KP_SNP_GAMES`; an advertiser beyond them still takes its id, so the listed games keep the original's ids).

  Not ported: class 3 (`SNetSendDatagram`, `SNetReceiveDatagram`, which StarCraft never calls), Battle.net's name table and the performance counters.
- **Tests:**
  - `test_snet` (16 tests):
    - checksum vectors; the game list following the advertisements, within its bound;
    - on the in-process LAN: two seats in lockstep; four seats with 10 % loss, whose two runs of one seed are equal; a joiner entering a running game; messages in their order; a leave seen at the same turn by every seat; the host's leave, after which the lowest ID hosts; refused joins; a silent seat dropped; Storm's resend timers; garbage on port 6112 ignored;
    - on real sockets over 127.0.0.1: three seats (two of the map's hosts and a program outside it) list and join the game, play in lockstep and see a leave at one turn;
    - the live join (below), and the exports' errors.
  - `test_net_trace`: the replay of stage N1's recordings (below); `tools/tests/test_net_check.py` checks their converter on synthetic recordings.
  - The fuzz targets `udp` and `snet` ([ADR 0022's addition](adr/0022-turn-source-and-fuzzing.md#addition-the-networks-targets-phase-n-stage-n2)).
- **The replay of the oracle's recordings.** Stage N1's games were recorded again into `koprulu-private/emu/net-n2`: the suite's 16, a melee 1v1 of 120 frames and a 1v1 with 5 % loss (seed 7, 300 frames). The SNet log was extended for this:
  - each record has its order of entry (`seq`), and a call also its order at return (`end`);
  - Storm's passes are logged (the network thread's `pump`, SNetJoinGame's `join_pass`), with what each took from the provider and its clock reads;
  - a failed SNetReceiveTurns keeps its arguments and status words.

  `tools/net_check.py` turns each player into a golden (version 5, [golden-oracle.md](golden-oracle.md)). `test_net_trace` gives Koprulu's SNet and provider that player's inputs on the emulator's clock: the datagrams that arrived, StarCraft's calls with what ran inside them, and Storm's passes. **All 40 players of the 18 games replay with no difference** (on the host, under ASan and UBSan, and in wasm):
  - 22,893 datagrams sent, byte for byte and in order, each within 50 ms of the original's time;
  - every result and output of 1,150,955 recorded calls and passes, every wait a pass returned, and the 63 events.

  The games include the joins' handshakes, map transfer, chat, kick and ban, the latency command, a save and its load, a leave (`0x0B`), a drop after a cut cable (the turn requests, the pings, `SNetDropPlayer`), a desync and the lossy game's resends. Getting there showed four things the replay must model:
  - **The emulator's clock.** Each GetTickCount charges 50 µs before it reads the time, so a pass's reads advance its clock as they do in the emulator.
  - **The emulator's preemptions.** In a network game the emulator preempts the running thread every virtual millisecond at an API return (its second core for the background threads). A pass can therefore have gaps between its clock reads, in which the provider's threads answered game-list datagrams. The replay passes each gap too, and delivers the arrivals before its end (strictly before).
  - **Storm's command 6** (`0x15023660`, the player info) finds or creates the sender's record before it deletes it, so a record it creates reads the clock. `kp_snet` does the same.
  - **A failed SNetReceiveTurns still sends.** When a present player's turn is missing and its request is older than the gap, the call sends a resend request (flag 2), even though it fails with "no data". Hence golden version 5.
- **The live join** (2026-11-17). The emulated 1.17 hosted a game on the loopback bridge (`sc_emulator net MAP live --players Alpha@10.0.0.1:host --wait-players 2 --bridge BASE --realtime`). `KOPRULU_NET_BRIDGE=BASE kp_test test_snet live_bridge_join` then ran Koprulu on the system's sockets, outside the bridge's map:
  - Koprulu listed "Alpha", joined it through Storm's handshake as player 1 (the host's template arrived), and received 16 of the host's turns, sending one keep-alive turn (`05`) for each turn set, as StarCraft's lobby does;
  - the original saw the join (event 2), sent `'J'` and `'P'`, received 18 turn sets with Koprulu's keep-alive (status `0x70000`), and on Koprulu's leave got event 3 with reason 3.

  StarCraft's lobby protocol on top of this is stage N3's.
- **Fuzzing:**
  - 120 s a target found no crash, sanitizer report, leak or hang: `udp` 2,330,330 inputs, `snet` 1,732,668;
  - the full CI's 60 s found none either: `udp` 1,205,236 inputs, `snet` 929,937.
- **Builds and checks:**
  - `tools/ci.sh` with `KOPRULU_FULL=1` and the product passed in 2189 s: 735 C tests on the host, under ASan, LSan and UBSan and in wasm; 287 Python tests; the seven fuzz targets; the product's scripts;
  - GCC 15.2 in the Linux image (warnings as errors, and with the sanitizers): `test_snet` and `test_fuzz` pass;
  - mingw-w64 GCC 16.2: they pass under Wine, the socket test over Winsock included. mingw's GCC also caught a real fault: two tests read a join's error code in the same call that set it, and C leaves the order of arguments open;
  - the network sources compile for Android (NDK 29) and iOS without a warning;
  - `tools/boundaries.sh` passes.
- **The code review** (2026-11-17), all 16 findings fixed:
  - `kp_snet_receive_turns` gives no record to a slot outside 0–254, and a negative first slot no longer wraps its loop;
  - the provider gives every unknown advertiser its id before it checks the program or the list's bound, as the original does, in one walk of the list;
  - `kp_snet_new` requires the clock (the join's waits ran forever without it); the checksum leaves a packet below 2 bytes alone; the in-process LAN's jitter of `0xFFFFFFFF` no longer divides by zero;
  - the SNet log's dedup of polled calls leaves out every order field again (since `seq`, `t_us`, `depth` and `end` it never matched); filtering `net-n2`'s recordings by it drops 12,924 of 1,151,415 records and every player still replays with no difference. A NULL event handler is not hooked, and `net.py` imports the emulator only where it hooks it, so its pure parts run without unicorn;
  - `tools/net_check.py` keeps an index's other games only when the index is of its version, and `test_net_trace` fails a listed game of another version;
  - the tests read every output after the call that sets it, and `test_net_trace` decodes hex with `kp_hex_decode`.
- **Open:**
  - the network thread that runs the driver on the natives belongs to the game's network code (stage N4); `kp_net_sock` waits and reads, and its caller pumps;
  - the web has no transport until the relay (stage N5): `sockets_carry_a_game` is skipped there;
  - not checked against the original, since its recordings never reach them: the advertisement's withdrawal, commands 10, 12 and 13 (reject, drop, new host; they run between Koprulu seats in `test_snet`'s refused joins, drop and host's leave), the 13-byte turn requests and the relays. They follow the disassembly's entries;
  - the live join is a manual check: it needs the emulator, so CI skips it without `KOPRULU_NET_BRIDGE`.
