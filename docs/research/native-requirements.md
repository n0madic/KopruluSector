# Native 1.17 unit-production requirements

The local unit-production gate `0x4b32a0` and its actual requirement evaluator are recovered from StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. Evidence: [static code and all 91 shipped unit recipes](evidence/native-requirements-static.json), [182 isolated original-code checks](evidence/native-requirements-probes.json), [inspector/probe](../../tools/research/inspect_native_requirements_117.py).

All checks passed with **no helper stubs**. They execute the full gate and its reviewed leaf callees in private Unicorn x86 memory. The emulator does not execute WinMain, a full game, Windows APIs or host file/network operations. Original recipe tests retain the shipped requirement bytecode; synthetic tests replace data words to discriminate individual tokens. Fixture DAT/count/technology state is explicit and is not a campaign-state or gameplay-parity claim.

## Gate before bytecode

The function takes `CX=requested_unit_ID`, `EDX=player`, stack `[producer_unit_pointer]`. It clears global error dword `0x6be654`, then checks these conditions in order:

| Condition | Return | Error |
|---|---:|---:|
| Unsigned low-word requested ID>=228 | 0 | 16 |
| Producer owner byte +0x4c differs from player | 0 | 1 |
| Producer status bit 0x1 at +0xdc clear | 0 | 20 |
| Disabled predicate below succeeds | 0 | 10 |
| Availability byte `0x52655c + player*228 + ID` is zero | 0 | 2 |
| Queue word at relative position 4 is <=105 | 0 | 3 |
| Producer status bit `0x40000000` set | 0 | 22 |
| Requirement offset resolves to zero | 0 | 23 |

Disabled helper `0x407b50` returns one if status bit `0x400` at +0xdc is set or any of bytes +0x117/+0x119/+0x124 is nonzero. These offsets correspond to disable-effect timers in reference unit layouts; the local tests are raw predicates rather than an assumed list of semantic states.

Queue lookup `0x41b300` uses `(head_byte+relative)%5` over words at +0x98. The gate requests relative position **4**, not the head and not a queue-length calculation. ID 105 rejects while 106 and empty ID 228 pass this check. Fifteen isolated cases cover the threshold at each circular head position. Calling this simply “queue full” loses the actual ID comparison.

Native callers compare the gate result to **exactly 1**. Other returns are 0 or -1 (`0xffffffff`), accompanied by the shared error code. Those values are preserved below rather than collapsed into a boolean.

## Shipped unit recipe table and lazy offsets

The packed original table begins at `0x50da88` and ends after the sentinel at `0x50deca`. It contains a leading zero word, then records of `unit_ID`, requirement words terminated by `0xffff`, and a final `0xffff` record-ID sentinel. There are **91 recipes**. Units without a recipe are not assigned a synthetic prerequisite list.

The u16 offset array is `0x523a78`, 228 entries. If the requested entry is `0xffff`, the gate invokes lazy initializer `0x4b3230`. It walks the original packed records and writes each record's first requirement **word index** into that unit's array entry. It then replaces remaining `0xffff` entries with zero. One isolated execution checks the entire 228-entry result against the independently decoded table.

The decoder records every recipe ID, word offset, raw words, operand boundaries and token addresses. All 91 original recipes were also evaluated by the native gate with their first encoded producer, nonzero completed/raw counts, researched technologies, available units and suitably initialized producer fields/addons. Each returned 1. This verifies the parser/offset mapping and the evaluator over the shipped unit-recipe corpus under permissive fixtures; it does not assert that all recipes are currently legal in every mission.

The shipped corpus uses 14 high-word token types: `ff01`, `ff02`, `ff03`, `ff04`, `ff05`, `ff07`, `ff08`, `ff0b`, `ff0c`, `ff0e`, `ff10`, `ff11`, `ff24`, `ff25`. Ordinary lower words are completed-unit prerequisites. The local unit gate's switch covers `ff02..ff26`, with 18 dedicated targets and one fallback. It is a **specialized production evaluator**. The general DAT/order evaluator located at `0x407570` has a different switch and is a separate contract.

## Token semantics of this specialized gate

The switch table at `0x4b37dc` and index bytes at `0x4b3828` are decoded in the evidence. Dedicated behavior is:

| Token | Native behavior and failure |
|---|---|
| `ff02 ID` | Match current producer type. Completed count for that ID also affects missing-prerequisite bookkeeping. A wrong producer can leave a mismatch flag; terminal mismatch returns -1/error25. |
| `ff03 ID` | Contribute `completed_count + all_unit_count` with u32 wrap. A zero group contribution can return -1/error8. |
| `ff04 ID` | Require nonnull addon pointer +0xc0 whose type word equals ID; missing/wrong returns -1/error4. Cheat flag0x2000 bypasses the pointer/type test. |
| `ff05` | Require status bit0x2 at +0xdc. Failure returns0/error7. |
| `ff07` | Require busy helper0x407b10==0, technology byte +0xc8==44 and upgrade byte +0xc9==61. Failure returns0/error5. |
| `ff08` | Require both construction-target predicates0x435990 and0x42f240==0. Failure returns0/error5. |
| `ff09` | Require technology byte +0xc8==44; failure0/error5. |
| `ff0a` | Require upgrade byte +0xc9==61; failure0/error5. |
| `ff0b` | Reject only producer type7 with primary order0x21; failure0/error5. |
| `ff0c` | Require addon pointer +0xc0==0; failure0/error4 unless previous mismatch bookkeeping preserves another error. |
| `ff0e` | Require existing plus queued fighters below native capacity; failure0/error3. |
| `ff10` | Require dword +0xd0==0; failure0/error3. |
| `ff11` | Require status bit0x10 clear; failure0/error5. |
| `ff22` | Unconditional return-1/error21. |
| `ff23` | Unconditional return0/error23. |
| `ff24` | Require expansion byte0x536720 nonzero; failure0/error26. |
| `ff25 technology_ID` | Require researched byte through0x49a420, or cheat flag0x1000; failure-1/error9. |
| `ff26` | Require status bit0x10 set; failure-1/error5. |

For an ordinary unit-ID word, the evaluator adds the completed-table dword at `0x52c0c4 + 4*(ID*12+player)` to its group accumulator. Cheat flag `0x2000` contributes an additional one. `ff03` adds both the completed and raw all-unit tables; it does not substitute the raw count alone. Its sum can wrap to zero in synthetic width probes.

Within this gate, `ff06`, `ff0d`, `ff0f`, `ff12..ff21` and other tokens without dedicated targets take the **ordinary completed-unit-count fallback using that numeric word**. They are not implementations of the generic lift, movement, transport, hallucination or upgrade-level tokens. The shipped unit corpus avoids those unsupported values. No claim is made that an arbitrary malformed/modded token stream is safe: the fallback has no unit-ID bounds check and can read outside the nominal count table.

AISE's `DatReq` names and generic parser are useful reference labels, but cannot establish which tokens this local gate recognizes. In particular, AISE treats `ff0f` as a technology predicate; the local specialized unit gate recognizes **ff25** for its shipped Lurker technology prerequisite. [AISE generic requirement reader/evaluator, pinned revision](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/datreq.rs#L8).

### Grouping and errors

`ff01` continues the current group as OR: the contribution accumulator is preserved while the next token is evaluated. Ordinary groups are then combined sequentially as AND. Zero contribution with the missing-prerequisite flag returns -1/error8. Current-producer comparisons also maintain a mismatch flag: a later matching producer alternative clears it. Terminal producer mismatch returns -1/error25. Tests distinguish a wrong-but-owned producer from a wrong producer whose required type has no completed count.

Some dedicated predicates return immediately on failure, before the group separator is processed. Thus a failing `ff05` followed by `ff01` and an existing completed-unit alternative still returns0/error7. A replacement evaluator must preserve this actual short-circuit behavior and error ordering; expressing every token as a freely composable boolean OR changes the gate.

Successful termination at `ffff` returns1. These return/error distinctions feed production consumers' request-pop/retry decisions; those consumers must be recovered separately. The [production report](native-production.md) establishes request regeneration and costs, not that transition policy.

## Busy, construction and fighter callees

Busy helper `0x407b10` checks queue head through `0x41b300(unit,0)`. A nonempty word returns1. With empty ID228, it checks addon-construction predicate `0x435990`, then Protoss-construction predicate `0x42f240`; otherwise returns0. Both actual predicate bodies and calls are included in this probe. Their status/target tests are documented in [queued production](native-production.md).

For `ff0e`, type predicates identify Carrier types72/82 and Reaver types83/81. Carrier count adds bytes +0xc8 and +0xc9; Reaver count uses +0xc8. It adds queued fighters via `0x41cc50`, which counts **both** IDs73 and85 in all five physical queue words regardless of producer type. The resulting sum uses byte arithmetic. Unsupported producer types leave the count byte255, which fails against capacity zero.

Capacity `0x41cba0` is 4/8 for Carrier types depending on upgrade43, and 5/10 for Reaver types depending on upgrade36. DAT property0x40 selects hero capacities8/10 without the upgrade test. The predicate accepts only `amount < capacity`; equality rejects. Twelve boundary cases and six queued-fighter cases execute the actual capacity/count helpers. Cross-type queued fighters are synthetic fixtures distinguishing the raw helper's behavior, not claims about reachable authentic queues.

## Reproduction and dependencies

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_requirements_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-requirements-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-requirements-probes.json
```

Without the explicit execution flag, the inspector only reads/disassembles the EXE. It gates the exact hash before parsing. The probe records script SHA-256, Python/Unicorn/Capstone/pefile versions, architecture, no-stub status, instruction counts and the bounded native allowlist. All **182 cases passed**, including all91 unchanged shipped recipes and full offset initialization.

Samase's recovery method independently identifies unit requirements from DAT offset reads and error0x17, and the general evaluator from a technology-requirement-offset call. Those are version-independent structural clues; its addresses are not transplanted into local1.17. [Samase requirement recovery source](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/requirements.rs#L16).

The general DAT interpreter at `0x407570` and its technology/upgrade/order wrappers and tables are now separately recovered in [general DAT requirements](native-dat-requirements.md), with 482 no-stub native cases. The remaining production dependency is the actual spending consumer at `0x487e..` that invokes this unit gate, checks resources/limits, pops requests and issues train/morph orders, plus the unit-order lifecycle. This report closes the specialized unit-production gate and its shipped recipe format; a passing legality gate does not establish complete production execution.
