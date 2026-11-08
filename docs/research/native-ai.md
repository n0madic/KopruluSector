# Native AI interpreter in StarCraft 1.17

The AIScript dispatcher was located by static inspection of local StarCraft.exe 1.17.0.1, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. This initial report records static evidence. A subsequent [opcode-contract batch](native-ai-opcodes.md) executed selected original functions in isolation; the complete game has not been launched. Addresses below are preferred VAs at ImageBase 0x400000; evidence also records RVA, file offsets, and instructions. [Machine-code witnesses](evidence/native-ai-static.json).

The search used Samase_scarf's structural signature: byte opcode read, comparison with 0x70, default branch, and a switch. Independent corroboration includes PC increments, two program bases selected by a flag, operand widths matching goto/wait/build, and several matching handler ordinals. Function names here are analytical labels, not original symbols recovered from the EXE. [Samase_scarf switch analysis](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/ai.rs#L166), [PyMS opcode schema](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L163).

## Dispatcher and script state

Function entry: VA 0x493a60 / RVA 0x93a60. Main dispatch loop: 0x493a70.

1. Read script+0x30 and test bit 0.
2. Select a program base from globals 0x6a2e74 or 0x6a2e78.
3. Read one byte at program_base+PC; increment PC at script+8 by one.
4. `cmp eax,0x70; ja default` rejects ordinals above 0x70.
5. `jmp [eax*4+0x494a98]` selects one of 113 handlers.

The local 1.17 dispatcher therefore accepts 0x00..0x70. PyMS lists 114 names including unused 0x71 if_towns; that ordinal **does not pass this native bound check**. AISE extensions using 0x71 are not automatically original 1.17 opcodes.

Examined handlers corroborate PC at +0x08, wait at +0x0c, player at +0x10, area at +0x14, center at +0x24/+0x28, town at +0x2c, and flags at +0x30. Subsequent loader recovery directly identifies `0x6a2e74` as AIScript and `0x6a2e78` as BWScript; creation/termination and low flag bits are detailed in the [expanded native contracts](native-ai-opcodes.md).

Readers at 0x4932f0 and 0x493310 read an unsigned byte and little-endian unsigned word, advancing PC by one and two respectively.

## Scheduler countdown

The immediate scheduling function at 0x4938e0 traverses a linked list headed by global 0x6a2e70. It caches the next script before processing the current one. For each script it loads the old wait value, writes wait-1, and skips interpretation when the **old value was nonzero**. With old wait=0 it either removes a script marked by flag bit 2 or calls the dispatcher. This is a post-decrement test, not a run-as-soon-as-the-new-value-is-zero test.

Consequently, absent other writes, a handler setting wait=30 skips 30 subsequent scheduler visits and becomes eligible on the 31st. The [outer caller](native-frame.md) now establishes one scheduler visit per accepted unpaused object step. The [expanded opcode contracts](native-ai-opcodes.md) recover head insertion during multirun and shared-return-PC interactions, including selected isolated native execution cases.

## Examined handlers

| Opcode | PyMS name | Preferred VA | Machine-code behavior |
|---|---|---|---|
| 0x00 | goto | 0x493c5c | Read word, assign PC, continue dispatch |
| 0x02 | wait | 0x494a1c | Read word, assign wait, return from interpreter |
| 0x06 | build | 0x493f54 | Read byte/word/byte; pass operands and town to helper 0x487550; continue |
| 0x09/0x0a | wait_build/start | 0x493abe | Read count/unit, distinguish opcode, compare helper result; if insufficient: PC-=4, wait=30, return |
| 0x0c | attack_add | 0x493ee6 | Read count byte/unit word; pass with player to helper 0x4839f0 |
| 0x0d | attack_prepare | 0x493f2e | Call helper 0x4af300 with player/area center, then continue |
| 0x0e | attack_do | 0x493be6 | Call same helper; nonzero result continues, zero rewinds PC by one, sets wait=30, returns |
| 0x30 | random_jump | 0x493cc2 | Read byte threshold/word target; raw RNG with source 0x34; jump when (result&255)<=threshold |
| 0x40 | call | 0x4942ff | Save following PC to global 0x6a2e7c; PC=target word |
| 0x41 | return | 0x49431b | PC=global 0x6a2e7c; clear global; continue |
| 0x48 | multirun | 0x494349 | Pass target/player/area/BIN bit to creation helper; copy town to successfully created script |
| 0x4c | train | 0x493b73 | Read count/unit; if helper count insufficient, invoke request helper and retry with PC-=4, wait=30 |

wait_build includes an additional path that reduces the expected count before comparison. Those helpers are not completely understood, so a rule that it always waits for exactly the authored number would exceed the evidence.

**30 skips 30 following object steps; interpretation resumes on step 31.** Its measured wall-clock time can vary with speed, pauses, and turn readiness. This retry branch exists in native 1.17 independently of similar AISE values; equal delay values do not establish equal modified-handler semantics.

## Two easily lost compatibility details

random_jump compares a threshold with the low byte of raw RNG and includes equality. A percentage comparison such as math.random(100)<n is incompatible. Threshold zero still permits a jump when the RNG low byte is zero. The probability estimate (n+1)/256 assumes a uniform byte distribution; the exact predicate is established without that assumption. A direct call to 0x478200 connects this handler to the locally inspected RNG. [RNG research](native-prng.md).

call/return use **one global return PC** in these handlers. A per-script call stack was not found. Introducing a conventional full stack changes this mechanism. Nested calls, script interleaving, and save/load require further examination of global accesses and scheduler order. The conservative inventory follows target and continuation without pretending to emulate a call stack.

## Remaining recovery

All 113 wrapper contracts are now reviewed in the [expanded native report](native-ai-opcodes.md); this does not mean 113 complete gameplay algorithms have been recovered. Highest-priority remaining dependencies are production/request servicing, complete town/resource-area behavior, attack preparation, regional strength/states, and guard/spell decisions. [Economy](native-economy.md) and [combat AI](native-combat-ai.md) close selected lower layers; the [completion ledger](completion-ledger.md) retains the rest.

For each helper, trace operands through state/tables, side effects, and resulting unit orders; then compare controlled input variations using isolated native execution or original gameplay as appropriate. A full game runtime is unnecessary for leaf probes. **Complete original-game runtime execution has not been performed.**
