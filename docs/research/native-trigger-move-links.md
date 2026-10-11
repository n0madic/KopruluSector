# Trigger Move: paths, AI destination guards and linked turrets

**183 complete original-function cases passed without stubs or imports.** This
batch closes existing-path disposal during Move, the destination guard reached by
nonnull AI pointers, and linked Goliath/Tank turret repositioning. It extends
[ordinary Move](native-trigger-move.md), [grounded Move](native-trigger-grounded-move.md)
and [populated-image Move](native-trigger-move-images.md). Creep remains separately
bounded in [native-trigger-creep.md](native-trigger-creep.md).

The target is StarCraft.exe **1.17.0.1**, SHA256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred
ImageBase `0x400000`. All addresses below are preferred VAs. The
[probe](../../tools/research/probe_native_trigger_move_links_117.py) SHA256 is
`064eae4aed2f61add61784dff28111dcf9b45484fc160a4a86bc7687d0e0aa69`.
[Native results](evidence/native-trigger-move-links-probes.json) record
**3,460 distinct executed instruction addresses**;
[static witnesses](evidence/native-trigger-move-links-static.json) contain
**261 reviewed blocks / 12,154 decoded instructions**. Source dependencies,
retail DAT hashes, architecture, Python and emulator versions are recorded.
All 183 cases return through original instructions and satisfy every assertion.

Original code runs unchanged in private Unicorn x86 memory under Rosetta/x86_64
Python, bounded to 300,000 instructions per call. No helper stubs, imports,
Windows process, WinMain, game loop or guest file/network operations execute.
The memory-read observer measures reads without replacing code or values. Unit,
path, AI, terrain, sprite/list and density-grid state are explicit fixtures;
these are not played-campaign traces.

## Complete coverage

| Family | Cases |
|---|---:|
| Path release into index-encoded free list | 16 |
| Path timer polling | 10 |
| Path timer low-byte assignment | 12 |
| Connected Move releases old path, accepted and rollback | 32 |
| Nonnull AI destination helper: seven early guards | 7 |
| Connected Move with nonnull AI, four orders, CanMove on/off | 48 |
| Goliath/Tank with unindexed turret, completion/rollback/path variants | 16 |
| Non-subunit linked-object guard | 2 |
| Fixed-point turret relocation and sprite row lists | 40 |
| **Total complete original paths** | **183** |

Units/flingy/orders columns use the hash-verified stock files in the
[retail manifest](evidence/retail-manifest.json). No new authored campaign
parameters are added: the previous Move reports separately cover stored campaign
records. The synthetic state variations establish local consumer behavior.

## Path release precedes placement and survives rollback

`0x42c8a0` reads CUnit path pointer `+0x100`. A nonzero pointer is passed to
`0x48e910`, then the CUnit pointer is cleared. The path free-head is
`0x69b570`; the pool base is `0x69b574`, with **128-byte records**. Link encoding
inside a free record is an index rather than a native pointer:

```text
path[+4] = ((path_address - pool_base) arithmetic_shift_right 7) + 1
path[+0] = old_free_head == 0 ? 0 :
           ((old_free_head - pool_base) arithmetic_shift_right 7) + 1
free_head = path_address
unit[+0x100] = 0
```

The probes compare four slot indices and four previous heads. One deliberate
alias input places the released record at the existing head; its measured link
arithmetic is not claimed to be a legal allocation state or a double-free guard.
The leaf contains no pool-bound or alias validation.

Move's hide path calls `0x430390 → 0x42c7e0 → 0x42c8a0` **before** destination
placement. The 32 connected cases use Marine, SCV, Civilian and Wraith, Complete
on/off, clear/blocked target and empty/nonempty previous free head. Accepted Move
and rejected ground Move both clear the path and retain its free-list insertion.
Rollback restores position `[64,64]` without recovering the original path. This
matches the reconstruction's distinct unit-path clear and push-front operations;
the native record encoding is verified directly.
[OpenBW free_path](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L10763-L10771).

Timer helper `0x48e950` returns 1 only for a **present path whose BYTE `+0x19` is
already zero**. A positive timer decrements and returns 0; an absent path returns
0. Thus timer 1 becomes 0 on a call that still returns 0. Setter `0x48e970`
stores DL at path `+0x19` when present. Inputs above 255 test low-byte truncation.
These contracts do not cover path construction, waypoint selection or search.

## Physical relocation keeps the strategic AI branch unreachable here

The immediate native ordering matters:

```text
43a270 relocation
  → 40cd80 updates actual unit position, fixed coordinates and Sprite
  → 43a0d0 requests the same point as movement target
       → if unit AI pointer +134 is nonzero: 437540 destination guard
  → 40cbd0 installs the next waypoint
```

`0x437540` first returns 1 for primary order 96. Otherwise it returns 0 for
Airborne status4 or stock Subunit property `0x10`. For a normal ground unit it
next compares the destination with the **current Sprite position**, returning 0
when equal. Since `0x43a270` already moved that sprite, this equality stops the
later region/strategic response. Its other early guards include absent **CanMove
status `0x20000`**, an SCV constructing a flying target, and an unusable target
region. `0x20000` is a movement capability bit, not an AI-class flag.

The seven direct helper cases execute those guards with real callees, including
`0x481b10/0x481bb0` for the blocked-region case. The 48 connected action cases use
Marine, SCV and Wraith, initial orders 3/6/34/96, CanMove clear/set, and accepted
or blocked ground placement. CanMove cases provide a real density-grid fixture
with the old cell initialized and an existing high-nibble density state; original
remove/add density consumers run, without forcing another source-37 draw.

For **every recorded `0x437540` entry in these actions**, the native Sprite point
already equals its destination stack arguments. A memory-read hook records **zero
reads** from the opaque 64-byte private AI record. The pointer and bytes remain
unchanged. No `0x4b0580`, `0x4ab450` or `0x437f80` strategic helper executes.
The opaque bytes therefore do not substitute for a decoded Worker/Building/
Military AI structure; the guard does not consume such a structure in this path.

If primary order 96 makes the guard return 1, `0x43a0d0` returns 0 early. Relocation
still continues to its unconditional next-waypoint call; its caller does not
branch on that target-helper return. The general strategic destination algorithm
can run for other callers that request a point different from the current sprite.
That is a separate contract. It should not be added as an immediate Trigger Move
side effect. The independently pinned `move_unit` reconstruction also writes
actual position/Sprite before the target request; its source is corroboration,
while native calls and state provide this batch's proof.
[OpenBW move_unit](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L16696-L16709).

## Linked turret position, lists and path cleanup

The connected fixtures use stock Goliath body 3/turret 4 and Tank body 5/turret 6.
The two `+0x70` links point to each other. They follow the native allocation list
invariants documented in [image initialization](native-image-initialization.md):
owner list starts at the turret then body, the active parent list contains only
the body, both sprites have actual row-list membership, and the turret carries
Sprite bit `0x10`. The turret is excluded from sorted spatial borders; the body
has the only two X/Y border entries. Existing images are empty animation guards
in these fixtures, so this batch does not duplicate populated GRP/IScript proof.

Hide clears references through original `0x4269b0`, including recursive
`0x426d30` for a linked subunit. Reattachment `0x430430` recognizes the stock
Subunit property and passes the parent's **fixed-point** `+0x2c/+0x30` coordinates
to `0x40cc00`. The child receives those fixed coordinates, the arithmetic `>>8`
integer position, and an actual Sprite relocation through `0x417330`. At the end,
its movement initializer runs and releases a nonzero old child path through the
same original free-list leaf.

All 16 action cases vary Complete, clear/blocked target and an old child path.
The final body and turret centers match after either accepted Move or rollback.
Both links, canonical owner-list order, parent-only active list and two-entry
spatial count remain intact. Child paths are cleared on reattachment for both
outcomes. Two additional synthetic links to a Civilian discriminate the type
guard: a non-subunit linked object receives no final fixed-position copy.

Forty direct `0x40cc00` cases use both turret types, five map positions and four
fraction profiles. Fractions remain in the fixed fields; integer unit/Sprite
positions use arithmetic shift. Actual old/new sprite row heads/tails and
backlinks are checked. These cases establish coordinate/list operations, not
stock turret LO/image offsets, rendered movement, normal creation or allocation.

## Reproduction and remaining scope

Prepare local stock inputs using [reproduction.md](reproduction.md), then run:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trigger_move_links_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/move-links-probes.json \
  --static-out /private/tmp/move-links-static.json \
  --execute-original-code
```

Paths are explicit CLI arguments; target and stable dependency hashes gate
execution. Omitting the execution flag produces only static extraction/plan data.
Retail bytes stay in private scratch. General path allocator/search, general AI
movement response, positive grounded construction/addon cancellation, full creep
visual-context acquisition and played-campaign validation remain separate. The
immediate ordinary-Move path/AI-pointer/turret consumers above are complete for
the stated original fixture profiles.
