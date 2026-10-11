# Original local-path heap identity and priorities

**452 complete cases / 2,576 original entry returns pass.** This report measures the local-search heap's object order, signed priority changes and tie handling independently of map geometry. The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, image base `0x400000`.

[Static evidence](evidence/native-local-heaps-static.json) contains three unchanged function ranges and 167 decoded instructions. [Probe evidence](evidence/native-local-heaps-probes.json) records every complete return and exact object-ID order. The [probe](../../tools/research/probe_native_local_heaps_117.py) executes no helper replacements, imports, full world or game. Its instruction limit is 10,000 per invocation; the maximum observed is 949, with 180,420 instructions across the recorded entry invocations. Static-only regeneration is byte-identical.

## Layout and caller contracts

| Function | Native entry | Inputs / cleanup |
|---|---|---|
| Insert | `0x492a30` | ECX=heap; stack=node pointer; pops4 |
| Sift down | `0x4929b0` | ECX=heap; stack=one-based heap index; pops4 |
| Change priority | `0x492900` | ECX=heap; stack=node pointer, new signed priority; pops8 |

The heap stores node pointers at `heap + index*4`, beginning with index1. Count is DWORD `+0x25c`; sentinel pointer is DWORD `+0x260`. Index0 is a sentinel slot. Each 32-byte node stores its signed priority at `+0x1c`. Native search uses the physical heap at `0x69d6c8` and its separate node arena; [local routes](native-local-paths.md) connects these functions to actual search.

The private fixtures use heap lengths1,2,3,4,7,8,15,16,31,32,63,64,149,150. Their sentinel priority is explicitly `INT32_MIN`, lower than every tested node priority. This fixture establishes the helpers under that precondition; it does not claim the supplied sentinel was copied from a running campaign. Return EAX is recorded without assigning a success meaning to it.

## Identity matters when costs tie

Insertion increments count, stores the new pointer and bubbles it upward while the parent's signed priority is **greater than or equal to** the new priority. Equal-cost nodes therefore move ahead of older parents. The new object can reach the root. A generic stable priority queue would not reproduce this identity order.

Sift-down chooses the **left child when child priorities tie**. It stops when the current node's priority is less than or equal to the chosen child's priority. Equal-cost parent/child pairs remain in place. These rules differ from insertion's equality rule and must be preserved separately.

The 56 insertion-chain cases cover ascending, descending, all-equal and alternating priorities at every listed length. Each chain checks the entire exact pointer order after every insertion. The 59 sift-down cases raise the selected node's priority and exercise root, interior and leaf indices. Guard bytes around the heap remain unchanged.

## Priority mutation and absent objects

Change-priority compares the node's old and new signed costs first. Exact equality returns without searching or reordering. Otherwise it writes the new cost **before checking heap membership**, then searches backward from the current count for the same node pointer.

A found decrease bubbles upward using the same equal-parent rule as insertion. A found increase delegates to the unchanged sift-down function. The 295 mutation cases include negative new priorities, decreases to zero, equality, small increases and a large increase, checking exact object order and every node cost.

An absent node still receives its new priority. The heap's count and pointer order remain unchanged. Forty-two complete calls distinguish absent decreases, equality and increases. This function is not an implicit insertion API.

## Reproduction and limits

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_local_heaps_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --static-out docs/research/evidence/native-local-heaps-static.json \
  --out docs/research/evidence/native-local-heaps-probes.json \
  --execute-original-code
```

Omitting the execution flag only validates identity and disassembles. Runtime: x86_64 Python3.9.6 / Rosetta, Unicorn2.1.4, Capstone5.0.6 and pefile2024.8.26. Script SHA256: `90ad1e26c5c709d1415a6ae38211164f995b85a13b28631a126391e44057f9f9`; dependency hashes are in the generated report.

These helpers do not enforce a standalone allocation limit. The native search's capacity gates, 150/200-node fallback decisions, 250-node arena exhaustion, route reconstruction and physical collision response are caller contracts. This batch does not execute insertion beyond150, invalid heap indices, a sentinel equal to a node cost, integer-cost overflow or duplicate node pointers. Those malformed fixtures are distinct from normal local-search reachability. Full campaign routing and movement remain broader obligations.
