# Storm's network packets (SNet over UDP)

The packets StarCraft 1.17.0.1 exchanges in a "Local Area Network (UDP/IP)" game: `storm.dll`'s network layer (SNet) on top of `standard.snp`'s UDP provider (`'UDPN'`). Koprulu's `kp_snet` must produce and accept these bytes to play with the original ([network phase](../phase-network.md)). The source is the atlas: [S02](../research/atlas/s02.md) for SNet's internals, [S01](../research/atlas/s01.md) for the exports StarCraft calls and [U01](../research/atlas/u01.md) for the provider; addresses are `storm.dll`'s at base `0x15000000`, except in the last section, which uses `standard.snp`'s at `0x1d000000`.

All integers are little-endian, except inside a provider address (command 6's address field and the UDP provider's addressing below), which is a Winsock `sockaddr_in` with the port and the IP address in network order. "L" is the provider's latency from its caps; the UDP provider reports 50 ms.

## The packet

One SNet packet is one provider datagram. There is no compression, no encryption and no fragmentation.

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | checksum |
| 2 | u16 | length of the whole packet: 12 + payload |
| 4 | u16 | sequence: per destination and class, wrapping |
| 6 | u16 | ack: the next sequence the sender expects from the destination in this class |
| 8 | u8 | class, 0–3 |
| 9 | u8 | command (class 0); 0 in the other classes and in requests and acks |
| 10 | u8 | sender's player id; `0xFF` while it has none |
| 11 | u8 | flags: `1` ack only, `2` resend request, `4` relayed |
| 12 | … | payload |

### Checksum

Computed over bytes 2 to the end, walked from the last byte to the first:

```
a = 0; b = 0
for each byte from the last to offset 2:
    a = a + byte;  if a > 254: a = a - 255
    b = b + a                       (32-bit)
lo = b mod 255;  hi = a
byte[1] = 0xFF - (lo + hi) mod 255
byte[0] = 0xFF - (byte[1] + hi) mod 255
```

A receiver runs the same sum over the whole packet, bytes 0 to the end, and accepts the packet only when both results (the final `a` and `b mod 255`) are 0.

### Acceptance

A datagram is dropped when: it is shorter than 12 bytes; its length field is below 12 or above the datagram's size; its class is above 3; the checksum fails; or, outside a join in progress and unless it is relayed, its sender byte is not `0xFF` and is not the id the receiver holds for the address the datagram came from. With the original's locked send buffer, a packet above `0x1F00` bytes is never sent; StarCraft keeps its turns within min(maxmsg, 512) bytes, where maxmsg is the provider's caps value minus 16 (488 for UDP).

## Classes

| Class | Contents | Delivery | Acknowledged | Recovery |
|---|---|---|---|---|
| 0 | control commands (below) | in sequence | yes, ack delay max(L/2, 25) ms | the sender retransmits the oldest unacknowledged packet |
| 1 | messages (`SNetSendMessage`: lobby and chat) | in sequence per sender | yes, ack delay max(L/2, 25) ms | as class 0 |
| 2 | turns (`SNetSendTurn`) | in sequence per sender; a turn is handed to the game only when every present player's turn of that number is there | collectively, ack delay max(5L, 250) ms | receivers ask; any seat can relay |
| 3 | datagrams (`SNetSendDatagram`) | by sequence per sender, without waiting for gaps | no | none; StarCraft 1.17 never sends this class |

Every packet a seat sends to a player carries the cumulative ack that seat owes the player in that class, so acks normally travel with traffic. A standalone ack is the 12-byte header with flags `1`, sequence = ack = the cumulative ack, command 0.

## Control commands (class 0)

| Cmd | Name | Payload |
|---|---|---|
| 1 | connect request | u32 1. Always sequence 0; the sender's control counter does not advance. The receiver acts on it at once and never files or acknowledges it |
| 2 | connect accept | u32 1 |
| 3 | connect ack | u32 1 |
| 4 | ping | none |
| 5 | pong | the ping's payload, unchanged |
| 6 | player info | see below |
| 7 | join request | name, description, password: three NUL-terminated strings |
| 8 | game info | see below |
| 9 | template data | the host's template bytes (StarCraft: its 32-byte template record) |
| 10 | join rejected | none |
| 11 (`0x0B`) | leave | u32 turn (turns the leaver has sent to this destination), u32 reason |
| 12 (`0x0C`) | drop | u32 id, u32 turn (low 16 bits used), u32 reason |
| 13 (`0x0D`) | new host | u32 id |
| 14 (`0x0E`) | game mode | u32 mode: 1 password, 2 full, 4 advertised, 8 closed. Without the password bit the receiver empties its stored password |
| 15 (`0x0F`) | ladder type | u32 |

Player info (command 6):

| Offset | Size | Field |
|---|---|---|
| 0 | u32 | total payload size: 0x24 + both strings with their NULs |
| 4 | u32 | player id |
| 8 | u32 | host flag (0 or 1) |
| 12 | u32 | player flags: 4 joining, 8 dropping (as the host holds them) |
| 16 | u32 | start turn: the receiver expects this player's turns from this number on. In the packets of a join every command 6 carries the joiner's start turn, also those about the host and the other players |
| 20 | 16 | the player's provider address. When the player id equals the packet's sender byte, the receiver uses the datagram's source address instead; the host's command 6 about itself has zeros here |
| 36 | … | name NUL, description NUL |

Game info (command 8, host to joiner):

| Offset | Size | Field |
|---|---|---|
| 0 | u32 | the joiner's id |
| 4 | u32 | maximum players |
| 8 | u32 | the joiner's start turn |
| 12 | u32 | mode bits |
| 16 | u32 | seconds since the game was created |
| 20 | … | game name NUL, stat string NUL, password NUL |

Turns (class 2) and messages (class 1) carry the game's bytes as the payload, unchanged.

## Resend requests and relays

A resend request is a header with flags `2`, command 0, and the requested sequence:

- 12 bytes, or byte 12 equal to the receiver's own id: "send me your packet of this class and sequence". The receiver searches what it sent to the requester in that class (for class 2 also its own turn queue and history) and sends the match again.
- Any other length, normally 13 bytes, class 2 only (such a request in another class is ignored): byte 12 is the id of the player whose turn is missing. The receiver searches that player's turns it holds (queued or delivered and not yet acknowledged by everybody). A match is resent with flags `4` added (relayed) and a new checksum; the sequence and sender byte stay the original player's. If the receiver has already removed that player, it also sends the requester command 0x0C with its own drop turn and reason for him.

A relayed packet is attributed to the player named in its sender byte, not to the address it came from; its ack field is ignored.

When resent, a stored packet gets the current cumulative ack for its destination and a new checksum; nothing else changes.

## Reliability rules

- **Retransmission (classes 0 and 1).** Only the oldest unacknowledged packet per destination and class is retransmitted, when `(maxmsg + 128 + length)·1000 / bytesPerSec + 2·(max(L,50) + 100)` ms have passed since it was sent, last resent, or became the oldest (integer division; 306–311 ms with UDP's 100,000 bytes a second). Class 2 is never retransmitted on a timer.
- **Gap requests (classes 0–2).** When a later packet has arrived but an earlier sequence is missing, the receiver sends a 12-byte request for the first missing sequence max(L/2, 25) ms after the later packet arrived, then every max(L,50) ms.
- **Missing turns.** While a seat waits for player P's turn T, it sends, at most every max(L,50) ms, a class-2 request for P's next turn (normally T) to every player with an id: 12 bytes to P, 13 bytes (byte 12 = P) to the others. On the same calls it pings (command 4) every present player whose control queue is empty and who was neither heard from nor pinged for max(4L,1000)/2 ms. These calls do not wake the network thread, so when no timer is pending such a ping is not retransmitted until other traffic wakes it.
- **Old and duplicate packets.** A packet whose sequence is older than the next expected one is dropped; in classes 0 and 1 the receiver then sends its ack again. A duplicate of a packet already queued is dropped.
- **Collective turn ack.** A seat acknowledges turn u in class 2 to all players at once, with the same value: the first turn number that some player (present, or joining at or before that turn) has not yet delivered to it. A turn therefore counts as acknowledged only when every seat holds it from every player, and seats keep delivered turns until then, for relays.
- **Pacing.** The sender models the provider's bandwidth: each send advances a clock by `(length + 64)·addresses·1000 / bytesPerSec` ms, and a new packet's retransmit interval counts from that clock.

## Joining

Joiner J, host H. Each of J's waits lasts max(12L, 5000) ms (5000 ms with UDP).

1. J reads the advertised game from the provider and requires the same program id and version as its own (StarCraft 1.17: version `0xD5`).
2. J sends command 1 three times in a row (sequence 0, ack 1, never retransmitted): J's record for H starts with control sequences 1. H ignores a repeat that arrives within max(4L, 1000) ms of the first; otherwise it forgets anything it held for J's address, expects J's control sequence 1 next, and answers command 2 (its control sequence 1).
3. J answers command 2 with command 3 (control sequence 1), and sends command 7 with its name, description and the password. Messages (class 1) start at sequence 0 in both directions.
4. H answers command 10 when the game is full, closed (mode bit 8), the password differs (case-insensitive; an empty game password accepts any) or no id below the maximum is free. Otherwise J gets the lowest free id and, as start turn, the number of turns H has sent. H sends J, in this order: command 8, command 6 for H, command 6 for every other player, command 15, command 9, and then its own turns not yet delivered. They precede J's start turn, and J drops them when it reads them after commands 8 and 6. When J reads them in the same batch as those commands, it files them first; it then asks H for one turn every max(L,50) ms (in the recordings the turn just below its start turn), and H answers each request, until J's first `SNetReceiveTurns` trims the stale turns. H sends command 6 about J to every other player. Full games get mode bit 2 and are advertised again.
5. A player whose start turn is ahead of the turn a seat is delivering is "joining" until that seat reaches it. Each seat that learns of a player through command 6 resends him its own turns from his start turn on.
6. J is in the game when command 9 has arrived; J's game then raises event 1 with the template.

## Leaving and dropping

- **Leave.** The leaving seat sends command 0x0B {turns it has sent, reason} three times to every player and stops. Each receiver removes the leaver once it has delivered all of his turns below that number, so every seat removes him before handing out the same turn.
- **Drop.** A seat dropping player P sends command 0x0C {P, P's next turn at that seat, reason} to the other players; they adopt that turn and reason. Seats that have already delivered that turn remove P at once.
- **Removal** raises event 3 with the u32 reason. A removed player's turns stay available for relays; his record goes after 50 s without traffic from him.
- **Host migration.** When a seat that is not host removes a player and no remaining player is host, the seat with the lowest id among those remaining makes itself host and sends command 0x0D {its id} to the others.
- A silent player is never removed by Storm itself; the game sees him as not responding (heard within max(4L, 1000) ms or not) and drops him.

## Timers

| Value | UDP (ms) | Use |
|---|---:|---|
| max(L/2, 25) | 25 | ack delay, classes 0 and 1; first gap request |
| max(L, 50) | 50 | gap and missing-turn request interval |
| max(5L, 250) | 250 | ack delay, class 2 |
| max(4L, 1000) | 1000 | "heard recently"; repeat window for command 1; half of it is the silence after which `SNetReceiveTurns` pings a player |
| max(12L, 5000) | 5000 | each wait of the join |
| 20000 | 20000 | idle ping (only when the network thread wakes for other traffic) |
| 50000 | 50000 | forgetting an address without a player id |

## UDP provider

`standard.snp`'s provider 4, `'UDPN'` ([U01](../research/atlas/u01.md)). It carries each SNet packet as one UDP datagram and keeps the list of advertised games with its own datagrams on a second port. Nothing else of Storm's packet is changed or wrapped.

### Caps

Storm takes the caps from `caps.dat` inside `standard.snp`, not from `SnpQuery` ([S02](../research/atlas/s02.md) `ScanProviders`). The two agree except in the flags: `SnpQuery` reports `0x20000003`, `caps.dat` `0x00000003`, and Storm (and so `SNetGetProviderCaps`) uses the latter.

| Field | Value |
|---|---:|
| flags | 3 (bit 0: Storm's locked send buffer) |
| max message size | 504 (Storm keeps 488) |
| max queue | 16 |
| max players | 256 |
| bytes a second | 100,000 |
| latency | 50 ms |
| turns a second | 8 |
| turns in transit | 2 |

### Sockets and ports

Winsock 1.1 from `wsock32.dll` (`WSAStartup` version 0x101); `ws2_32.dll` only for `WSAIoctl`. Two blocking UDP sockets (`AF_INET`, `SOCK_DGRAM`, `IPPROTO_UDP`), each read by its own threads:

| Socket | Local address | Options | Traffic |
|---|---|---|---|
| A | `0.0.0.0:6111` | `SIO_UDP_CONNRESET` = FALSE (when `WSAIoctl` exists), `SO_BROADCAST` = 1 | the game list: requests, advertisements, stops |
| B | `0.0.0.0:6112` | the same | SNet packets in both directions |

Socket B is bound before socket A. No other option is set: default buffer sizes, no `SO_REUSEADDR`, so a second instance on the same machine fails at the bind of 6112 (error `0x4C6`, which StarCraft shows as network.tbl `0x53`). All broadcasts go to the limited broadcast address `255.255.255.255`. The provider never looks up its own address; peers know each other only by the source addresses of datagrams.

### Addresses

A player's 16-byte provider address (Storm's player record, command 6) is the peer's `sockaddr_in` as `recvfrom` reported it:

| Offset | Size | Field |
|---|---|---|
| 0 | u16 little-endian | 2 (`AF_INET`) |
| 2 | u16 big-endian | UDP port |
| 4 | 4 bytes | IPv4 address, network order |
| 8 | 8 bytes | zero |

Bytes 8–15 are always zeroed on receipt, so equal addresses compare equal byte for byte. Storm's packets to a player go to this address as is. The address a joiner connects to is different: it is the advertiser's IP with port 6112, whatever port the advertisement came from.

### Game-list datagrams (port 6111)

Header, little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | checksum, as Storm's ([Checksum](#checksum)) over bytes 2 to the end |
| 2 | u16 | length of the whole datagram |
| 4 | u16 | type: 0 advertisement, 1 stop, 2 request |
| 6 | u16 | 0 (not checked) |
| 8 | u32 | program id: `'STAR'` `0x53544152` or `'SEXP'` `0x53455850` |
| 12 | u32 | program version: `0xD5` for 1.17 |
| 16 | u32 | game mode bits (types 0 and 1); 0 in a request |

| Type | Size | Body after the header | Sent |
|---|---|---|---|
| 0 advertisement | 22 + both strings + 12 | game name NUL, stat string NUL (each at most 127 characters), then Storm's 12-byte player record {u32 12, u32 players, u32 max players} | broadcast each time Storm advertises (create, join, removal, mode change); unicast to the source of every request while the game is advertised |
| 1 stop | as the advertisement | the last advertisement, type changed to 1, checksum recomputed | broadcast once when the host stops advertising (Storm's leave as host) |
| 2 request | 20 | none | broadcast once per Initialize and then at most every 400 ms (strictly more) as Storm reads the game list; StarCraft reads it once a second, so about one request a second while the join screen is open |

Storm's first advertisement of a new game is sent before it sets mode bit 4, so it carries the creator's mode without that bit; StarCraft's following `SNetSetGameMode`(4) finds bit 4 already set and does not advertise again. Until the first join re-advertises the game, requests are answered with that first advertisement (mode 0 in the recordings).

A receiver accepts a datagram only when it is at least 20 bytes, its length field equals its size and its checksum holds; otherwise, and for other types, it is ignored. A datagram above 532 bytes is lost (`recvfrom` fails, the thread goes on). Every request is answered, whatever its program id or version.

**Filing.** For an advertisement or a stop, the receiver first removes every listed game whose address equals the sender's IP with port 6112. An advertisement whose program id equals the receiver's own is then added at the head of the list with:

- the next local id (ids are per receiver; one that replaces an entry keeps its id);
- the mode bits, the name and stat string, the bytes after them (the player record) and the program id and version;
- the time of receipt.

The version is compared only when the player joins: a mismatch fails the join with `0x85100079`. Entries not refreshed for 10,000 ms are dropped when Storm next reads the list. The receiver does not filter by mode; Storm hides games with mode bits `0xB`. The game type of a UDP game is always 0, and password, uptime and ladder type are never transmitted.

**Malformed advertisements.** The two strings must end within the datagram: when they run past its length the receiver allocates a negative size, which Storm's allocator treats as a fatal error.

### Storm's datagrams (port 6112)

Each SNet packet is one datagram from socket B to the player's address; a send to several players repeats the `sendto`, last address first. Received datagrams are queued unchanged with their source address, in arrival order and without limit, and each one signals Storm's pump event; Storm validates them ([Acceptance](#acceptance)).

- At most 504 bytes are read per datagram. A larger one fails `recvfrom`, and a failing `recvfrom` ends the receiving thread. The provider runs two such threads (one on Windows 9x), so two oversized datagrams leave it deaf until it is initialised again. Peers must keep datagrams within 504 bytes. Storm does not check the size; StarCraft keeps its turns within 488 bytes, 500 with Storm's header.
- `SIO_UDP_CONNRESET` is disabled so that an ICMP "port unreachable" from a vanished peer does not fail the next `recvfrom`.

### Threads and timers

| Item | Value |
|---|---|
| port-6111 thread | 1, `THREAD_PRIORITY_ABOVE_NORMAL`; sends one request when it starts |
| port-6112 threads | 2 (1 on Windows 9x), `THREAD_PRIORITY_HIGHEST` |
| request spacing | more than 400 ms, measured from the last request sent while Storm reads the list |
| `SNetEnumGamesEx`'s `*out` | 500 |
| game-list entry lifetime | 10,000 ms |
| shutdown | up to 5 rounds of 500 ms; each round sends the wake datagram `08 EF 08 00 00 00 00 00` to `255.255.255.255:6111` once and to `255.255.255.255:6112` twice |

One lock guards the advertisement, the game list and the receive queue. The wake datagrams of a shutdown reach every machine on the segment: port 6111 ignores them (too short), and on port 6112 Storm drops them (shorter than its 12-byte header). A shutdown closes socket A before it sends the stop, so that stop is not transmitted; a host that leaves normally has already sent it, and otherwise its entry ages out after 10 s.

### Koprulu's addition: a host by its address (phase N, stage N5)

Not the original's. Koprulu's provider can be given one host's address, its socket A (`kp_snp_udp_direct`, the join screen's Address, `--lan-host`). It sends nothing new on the wire:

- each game-list request it broadcasts also goes to that address, and one more goes there as soon as the address is given: the original's type 2 datagram, which any 1.17 provider answers with its advertisement to the sender;
- an advertisement that comes from exactly that address is filed with the game's address at that port + 1 (its socket B, as Koprulu's `--lan-port` binds it); every other advertisement keeps the original's port 6112. For the default port 6111 the two rules agree, so a 1.17 host is joined as the original would join it.

The original's joiner has no such address: it lists only what the broadcast reaches.

### Koprulu's receiver: who may send what (the review of 2026-11-21)

Not the original's, and nothing on the wire changes: an honest 1.17 or Koprulu peer sends none of what Koprulu ignores. The original takes the control commands and the relays above from any address, which since stage N5 means from anywhere on the internet ([ADR 0026](../adr/0026-network-compatible-with-1-17.md#addition-the-review-of-the-network-play), the list in `kp_snet.h`):

- during a join, commands 2, 6, 8, 9 and 10 count only from the joined game's address; outside a join 2, 8, 9 and 10 are ignored, and 6 counts only from a record with a player id (a stranger's command 8 reset the seat's id and turn counters, a stranger's 9 or 10 ended a join);
- commands 12 to 15 count only from a record with a player id;
- a relayed packet counts only in class 2 and only from the address of a record with a player id (the original attributes it to the player its sender byte names: one datagram ran as the host's turn);
- a record files at most 1024 packets, 256 without a player id, and records without an id together file only while fewer than half of the 16,384 packets kept are in use; class 3 is not filed.
- a datagram that fails the header's checks above (its length, class or checksum) makes no record (the original makes the sender's record first); with 1024 records kept, a new address takes the place of the stranger heard least recently (a record with no player id and no dropped player's old id, not the join's host, one that has not started a connection first), so datagrams from forged addresses cannot keep a joiner out; only when every record is a player's is a new address's datagram dropped.
- the limit of that eviction: a joiner's record is a stranger from its connect (command 1) until the host reads its join request (command 7), a round trip and the host's next pump. More than 1023 forged connects in that time, tens of thousands of datagrams a second held for as long as joins should fail, evict it, and the join fails; the bound alone took 1024 datagrams every 50 s (a record without an id is forgotten after 50 s of silence), and the original grows a record for every forged connect. Evicting the newest stranger, refusing a new address when the records are full or a bound per address prefix (a forger picks any prefix) would each make that cheaper. The 1.17 wire does not authenticate a sender, so whoever forges a player's address can as well send that player's packets; no departure that keeps the wire closes it.

## Runtime confirmation (2026-11-17)

The full-game emulator's network recordings were checked against this page: 20 LAN games of two to four emulated StarCraft 1.17 seats on a virtual segment (two-player, free-for-all, team, capture-the-flag, top-versus-bottom and UMS games, a Brood War game, chat, a map transfer, save and load, leave, kick, ban, a cut link, a desync and a latency run), each with the segment's packet capture and every seat's SNet call log. No recorded datagram contradicts the page.

| What | Recorded |
|---|---|
| datagrams | 26,923: 26,416 Storm packets (port 6112 to 6112), 507 game-list datagrams (6111 to 6111) |
| checksums and lengths | all valid, by the sender's formula and by the receiver's sum; the length field equals the datagram size in every packet |
| classes | 655 class 0, 1,122 class 1, 24,639 class 2, no class 3 |
| class-0 commands | 1: 72, 2: 24, 3: 24, 4: 123, 5: 121, 6: 36, 7: 24, 8: 24, 9: 24, 11: 9, 14: 28, 15: 24 |
| standalone acks | 200 (class 0: 122, class 1: 75, class 2: 3), all 12 bytes with sequence = ack and command 0 |
| resend requests | 2,098, all 12-byte class 2 |
| largest Storm packet | 152 bytes (class 1, map transfer) |
| game list | 346 requests (20 bytes, broadcast); 161 advertisements, 71 broadcast and 90 sent to a requester's port 6111, all with the 12-byte player record; modes 0 (92), 4 (27), 6 (19), `0xC` (7), `0xE` (16); `'STAR'` 487 and `'SEXP'` 20, version `0xD5`; offset 6 always 0 |

Timing, with UDP's L = 50:

- Each seat broadcast one game-list request when its provider started, then one about every 1.001 s while the join screen was open.
- Idle pings came 20.00–20.04 s apart; pings and pongs had empty payloads.
- Standalone acks left 28–32 ms (class 0), 20–31 ms (class 1) and about 255 ms (class 2) after the packet they acknowledge.
- Command 1 went out three times, 50 ms apart; all 24 joins were answered with 8, 6 (one per player), 15, 9 in that order, then command 6 about the joiner to the others.
- A gap request left about 28.5 ms after the later packet arrived; the resend followed about 2 ms after the request arrived and carried the resender's current ack (a turn first sent with ack 121 came back with ack 122).
- The missing-turn request of `SNetReceiveTurns` and the timer's gap request are not coordinated: the same sequence was requested twice 4.5 ms apart, and both requests were answered.
- Leave: command 0x0B three times about 50 µs apart, with the turn field one above the leaver's last class-2 sequence to that player and reason `0x40000001`.

Behavior seen:

- In 16 of the 20 games the first joiner filed the host's early turns before commands 8 and 6 and sent 9 requests for the turn below its start turn, about 50.8 ms apart, until its first `SNetReceiveTurns` ([Joining](#joining) step 4).
- In a free-for-all lobby the advertised mode was 4 at two and three of four players, 6 when full, 4 again after the game cleared the full bit, then 6 and `0xE` at the start. Kicks and bans are game messages (class 1) followed by the removed seat's own leave; no command 10 was sent, and the host advertised the game again with mode 4.
- Cut link: from the cut on, each side requested the other's missing turn every 50 ms and pinged him once, after about 500 ms of silence. The ping was never retransmitted and no 20-second ping followed, because nothing woke the network thread ([Missing turns](#reliability-rules)). Storm did not remove the silent player; about 48 s later the game dropped him. With two players no command 0x0C was sent, and both seats then advertised the game with mode `0xC` and one of two players, the former non-host as the new host. The desync game ended the same way.

Not exercised by the recordings: the stop advertisement (no host left through `SNetLeaveGame`), the shutdown's wake datagrams, commands 10, 12 and 13, 13-byte requests and relays, class 3, timer retransmission of classes 0 and 1 (all 9 losses in the recordings hit class-2 packets), the pacing clock, datagrams above 504 bytes, and the single receive thread of Windows 9x.
