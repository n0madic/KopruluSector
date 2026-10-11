# Native AIScript opcode contracts in StarCraft 1.17

This batch reviews **all 113 native dispatcher ordinals**, including all **64 opcode types in the conservative campaign control-flow inventory**. It recovers the interpreter-facing contract: operand reads, PC/wait changes, direct state writes, register/stack arguments, and conditional continuations. It also follows selected leaf helpers, the BIN loader, script allocation, list insertion, termination, and serialization. A recovered wrapper is **not** a recovered construction, military, pathfinding, or spell algorithm.

The reference is the exact local StarCraft.exe, numerical PE version **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. The static inspector reads PE bytes without executing the program. A separate, explicitly authorized mode also executed selected original instructions in private Unicorn memory: **89 isolated checks passed**, with no helper stubs. The complete game, WinMain, Windows APIs, host file access, and host networking were not executed. Preferred VAs assume ImageBase `0x400000`; the static evidence also records RVA, file offset, instruction bytes, and decoded instructions. [Structured static contracts](evidence/native-ai-opcodes-static.json), [isolated original-code results](evidence/native-ai-opcode-probes.json), [reproducible inspector](../../tools/research/inspect_ai_opcodes_117.py).

The campaign set comes from the existing static inventory, whose branches include disabled launch actions and impossible trigger conditions. Its 64 types are an upper bound, not a runtime trace. The evidence retains its input hash and the AI/BW BIN hashes. The full dispatch table has 113 entries, `0x00..0x70`; PyMS's extra `0x71 if_towns` is rejected by the native range check. Opcode names and format types are correlated with the pinned [PyMS schema](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L163). AISE's extensions remain separate.

## Reading the contracts

The script layout is `next+0`, `prev+4`, `PC+8`, `wait+0xc`, `player+0x10`, four signed 32-bit rectangle coordinates at `+0x14..+0x20`, center at `+0x24/+0x28`, town at `+0x2c`, and flags at `+0x30`. Native accesses corroborate the layout published by [AISE](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L809).

The dispatcher increments PC once for the opcode. Readers `0x4932f0` and `0x493310` then consume unsigned byte and little-endian unsigned word operands, advancing PC by one and two. `set_randomseed` has an inline unsigned 32-bit read. Ordinarily, a handler continues dispatch immediately. Only an explicit wait, a failed wait/request predicate, or termination returns to the scheduler. There is no per-opcode yield in the ordinary continuation path.

Each JSON entry distinguishes `native_operands`, `pc_control`, `wait_effect`, `state_writes`, `reviewed_helper_arguments`, and `unresolved_dependencies`. `wrapper_status=reviewed_static` describes this scope only. `gameplay_status=partial_helpers` prevents a call-through wrapper from becoming a complete gameplay claim. Twenty-one entries have no unresolved callee in their local operation; even those depend on valid script state and correctly maintained game tables.

Within-dispatch CFG extraction stops at the next main-loop visit or a return; calls are not followed automatically. Shared handler alternatives are conservatively retained. The reviewed annotation selects the appropriate branch for the actual ordinal. Independent witnesses cover explicitly followed helpers. This separates instruction extraction from human interpretation and avoids silently treating every reachable machine-code branch as taken by each opcode.

## BIN loading and script launch

The loader at `0x493800` passes the literal `scripts\aiscript.bin` at `0x4efa5c` to file-loading helper `0x444890` and writes its result to `0x6a2e74`. It then loads `scripts\bwscript.bin` at `0x4efa74` and writes `0x6a2e78`. These globals are now directly identified, rather than inferred only from a flag-selecting read. File loading itself, MPQ precedence, and error handling belong to separate contracts.

ID resolver `0x494d20` reads the first dword as the table offset. It searches AIScript records in **16-byte strides**, comparing the first dword with the requested four-byte ID and returning the offset at record+4 if nonzero. A missing ID or a matching zero offset falls through to BWScript lookup in **8-byte strides**. A nonzero BW offset is returned with an output BIN-selector value of 1. Failure returns zero. Four isolated original-code checks confirm AI precedence, zero-offset redirection, BW-only fallback, and complete lookup failure using explicit synthetic tables. This resolver does not read AIScript record string IDs or flags; no runtime-launch meaning for those fields should be invented from this function. [PyMS parser corroboration](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L503).

Launch gate `0x493970` first searches a zero-terminated ID list starting at `0x508f70`. Otherwise, it accepts player-type byte 1, or type 7 for literal ID `Rscu` (`0x75637352`). It then resolves the ID and invokes creation with player, returned offset, supplied rectangle, and BIN selector. The whitelist has 21 four-byte IDs: `EnBk`, `EnTr`, `ExTr`, `DWHe`, `PB3F`, `+Vi0`…`+Vi7` and `-Vi0`…`-Vi7`; these scripts start for any player type, while every other ID needs controller byte `0x6726d0 + 36*player` = 1, or = 7 for `Rscu` only. Resolved by the function atlas ([P32 tables and `0x493970`](atlas/p32.md)). Type names are source-correlated, not native symbols.

## Pool, insertion order, cancellation, and save records

Pool initialization `0x493330` creates **100 slots of 0x34 bytes**, zeros their payloads, and links the free list. The pool is at `0x6a1a18`; its free head is at pool+`0x1450`. Global `0x6a2e6c` points to the pool and `0x6a2e70` is the active head.

Creation `0x494d90` returns zero if no free slot exists. Otherwise it removes the first free slot and **prepends it to the active list**. It stores PC/player, sets wait=0, selects BIN bit 0, copies the rectangle, computes its center with signed truncation toward zero, and sets town=0. With no rectangle it writes zero area and center. The code changes only BIN bit 0 and then applies `flags &= 7`: old low bits `0x2` and `0x4` are retained. This is an observed write rule. It creates a slot-reuse dependency, not proof that an official mission encounters a stale-flag bug.

`multirun` calls this creator with its own player/rectangle/BIN bit and, on success, copies its town into the child. The scheduler caches `current.next` before dispatch. Consequently, an ordinary new head is absent from the remainder of that traversal and first becomes eligible on the next traversal. The isolated original-code probe confirms this sequence through actual creation and scheduler instructions. A complete-game ordering trace has not been recorded, and helpers that independently remove/reuse pending nodes still require analysis.

`killable` sets `flags |= 0x2`. `kill_thread` traverses the entire active list, including other players and the current thread, setting `flags |= 0x4` on nodes with bit `0x2`. It does **not** unlink them immediately. The scheduler first decrements wait and tests its old value; it checks pending-kill bit `0x4` only when old wait=0. A delayed thread therefore remains linked until its countdown permits the check. The current interpreter also continues after marking itself unless another opcode yields or terminates. [Scheduler evidence](native-ai.md).

`stop` calls `0x493240` to unlink the current node and prepend it to the free list, then returns. Invalid ordinals above `0x70` use an inline equivalent unlink/recycle path. Neither path resets payload fields. `0x493860` similarly removes all active scripts whose town pointer matches its argument. These mechanisms alter head and reciprocal links without heap allocation.

The reviewed save function `0x4936a0` encodes script/town pointers, writes a `0x1454`-byte pool record, restores pointers, and writes an **8-byte** record covering pool/head globals beginning at `0x6a2e6c`. `0x493530` zeroes the payload of free slots during pointer encoding. Pointer indices use zero for null and `(pointer-pool)/0x34+1` for nonnull slots. Load function `0x493610` restores these records and pointers; an input version word of zero also adds `0xb0` to every active PC. The broader save format/version interpretation is not recovered here.

`call` saves one following PC in global `0x6a2e7c`; `return` loads that value and clears it. This state is shared across players and both BINs. A direct absolute-reference scan found only those three accesses, with the scan limitations retained in JSON. The reviewed pool/head save records exclude this address. This is **not** proof that every higher-level save path omits it; indirect/global-block serialization remains to be checked. A conventional per-script stack would already differ from these native handlers.

## Waiting, counting, and training

The common failed one-byte wait rewinds PC by 1 and sets wait=30; failed count/unit waits rewind by 4. The post-decrement scheduler makes a value 30 skip **30 subsequent scheduler visits**, becoming eligible on visit 31 absent other writes. The separately recovered outer caller invokes it once per accepted unpaused object step, so these visits are logical object steps. `wait=0` still yields until the next traversal. Speed-table conversion, pauses, and turn delays are documented in [native frame scheduling](native-frame.md), rather than inferred from a source guide's wording.

`wait_build` and `wait_buildstart` call town counting helper `0x4872f0` with mode 1 and 0 respectively. Before comparison, if the requested unit equals helper `0x486070(player)` and `0x487b70(town)` returns a unit, the requested count is capped to the **low byte** of `0x496690(unit,player)`. Those three helpers' exact identities/scopes remain dependencies. Their presence rules out a universal “wait for exactly the encoded count” contract.

`train`, `wait_force`, and `do_morph` share a handler. `train` selects count-table mode 0; `wait_force` and `do_morph` select mode 1. Helper `0x4939e0` adds both tank modes, IDs **5 and 30**, whichever is requested. Mode 0 reads dword table `0x529604+4*(unit*12+player)`; mode 1 reads `0x52c0c4` with the same indexing. Names **all units** and **completed units** are corroborated by [Teippi Score](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/game.cpp#L531); native counter maintenance is still required for full meaning.

When the count is insufficient, all three invoke `0x482eb0`, which stores **unit+1**, modulo 65536, in PlayerAi's training word at `+0x220`. `do_morph` then continues without waiting; `train` and `wait_force` rewind and wait 30. `wait_train` checks mode 1 but submits no request. `notowns_jump`/`if_owned` instead read the raw mode-0 table for exactly the encoded ID and do not merge tank modes.

`build` submits count/unit/priority with final helper argument 0; `player_need` submits count/unit with priority **80** and final argument 1. Upgrade and technology wrappers preserve their distinct operand order and helper arguments. Their town insertion and execution contracts are followed in [native economy research](native-economy.md). AISE's fixes are not substituted for those native operations. [AISE overridden handlers](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/samase.rs#L494).

## Attack and defense roster mechanics

PlayerAi's base is `0x67d068 + player*0x4e8`, with field names correlated against [AISE PlayerAiData](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L836).

`attack_add` helper `0x4839f0` rejects `player>=8`, `count>=63`, or `unit>=228`. It scans the roster at PlayerAi+`0x230` for the first zero, then appends **unit+1** words until the requested count is exhausted or index 63 is reached. The array holds **63 usable entries**, followed by a zero terminator; the 128-byte allocation does not establish capacity 64. A count of zero does nothing; an excessive count is rejected rather than truncated to a legal argument. Full-list scanning can read the adjacent terminator before its next capacity test.

Defense insertion helpers accept **count<=10**, including zero, and `unit<228`. Each appends unit+1 to the first zero in its **20-word** list, truncating at capacity. Neither a compact count-per-unit map nor a sorted set preserves this ordering.

| Category | Build offset / helper | Use offset / helper | Clear helper |
|---|---|---|---|
| gg | +0x2b0 / 0x482670 | +0x350 / 0x482e40 | 0x482890 |
| ag | +0x2d8 / 0x482610 | +0x378 / 0x482de0 | 0x482850 |
| ga | +0x300 / 0x4825b0 | +0x3a0 / 0x482d80 | 0x482810 |
| aa | +0x328 / 0x482550 | +0x3c8 / 0x482d20 | 0x4827d0 |

Each clear helper zeroes **both** corresponding build/use arrays. Their downstream interpretation as pathable/unpathable and ground/air defensive requests remains a regional-consumer question; array recovery alone does not prove force assignment.

`attack_clear` zeros the 128-byte roster, grouping-region word, and last-attack timestamp, then visits regions. Qualifying state-8 regions change to state 0 with defense-priority byte 10, clear flag `0x20`, and may receive priority 40 depending on flag `0x40` and helper `0x4831e0`. The full region iterator and condition remain dependencies. `attack_prepare` and `attack_do` call the **same** `0x4af300` helper with the same player/center and constants; only the latter retries when its return is zero.

The grouping-timeout helper `0x483940` returns zero if timestamp=0. Otherwise it computes unsigned elapsed counter minus timestamp, compares with **120** or **180** according to campaign flag `0x20`, and clears the timestamp when the deadline is reached. Conversion of counter `0x5349d8` to time is a separate frame/timing contract.

The previously inconsistent `harass_factor` arithmetic is now recovered at `0x483830`: after target/enemy resolution it sums `max(unit+0x138,unit+0x13a)` for eligible enemy units, computes `ceil(strength/divisor)-1`, caps it at 3, and duplicates the existing roster only when that value exceeds 1. Thus the total multiplier is `max(1,min(3,ceil(strength/divisor)-1))`, with capacity 63. Strength maintenance, enemy resolver `0x48e080`, exclusion predicate `0x41fe40`, overflow inputs, and division by zero on the reached DIV branch remain explicit dependencies. This closes the arithmetic disagreement, not the complete strength/target contract.

## Native quirks and state-setting effects

`harass_location` dispatches straight back to the loop: it consumes only its opcode and is a no-op. `scout_with` reads and discards a unit word. `try_townpoint` jumps when `0x4872d0(player)` is **exactly equal** to its count byte; it does not implement the inequality in PyMS's descriptive text. The helper's complete town interpretation remains separate. These examples demonstrate why community prose cannot replace local instruction evidence. [PyMS command descriptions](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/PyAI/HelpContent.py#L25).

| Command | Recovered PlayerAi write |
|---|---|
| start_campaign | word+0x218 OR 0x20 |
| defaultbuild_off / default_build | word+0x218 set / clear 0x4 |
| transports_off / check_transports | word+0x218 set / clear 0x8 |
| farms_notiming / farms_timing | word+0x218 set / clear 0x10 |
| target_expansion | word+0x218 OR 0x800 |
| default_min | byte+0x222 = operand |
| max_force | word+0x21c = operand |
| nuke_rate | byte+0x212 = operand |
| set_attacks | byte+0x213 = operand |
| panic | word+0x21a = target |
| define_max | byte+0x3f0+unit = limit; no helper-side range check |
| fake_nuke | dword+0x214 = counter[0x5349d8]+1 |

Consumers of these fields still determine gameplay. `give_money` independently adds 2000 to each signed mineral/gas resource dword if it is below **500**; it does not set a fixed balance of 2000. `resources_jump` uses signed >= comparisons against its two unsigned16 thresholds. `if_dif` uses a strict unsigned comparison: comparison byte zero means `<`; any nonzero byte means `>`.

`enemyowns_jump` and `enemyresources_jump` scan the global head `0x665ae0` via unit+4. Hostility helper `0x429110` tests whether alliance byte `0x534914+player*12+owner` equals zero; owner 11 instead supplies sprite+0xa as the comparison owner. Neither wrapper nor that helper tests visibility. The global list's membership/order and alliance updates remain to be recovered before claiming complete knowledge of fog of war.

`enemyresources_jump` stops at the **first** hostile unit, reads its raw owner byte, and checks only that owner's mineral/gas tables. It performs no nearest-distance computation and does not resume the scan when resource tests fail. Even after hostility substitutes the sprite owner for owner 11, resource lookup uses the raw unit owner. Enemy ordering and neutral-owner edge cases therefore need targeted probes.

`sharedvision_on/off` respectively set/clear `1<<script_player` in dword `0x5264cc+4*operand_player`. Visibility consumers determine the user-facing direction; the write itself is fixed. The x86 shift uses the low five shift-count bits.

`set_randomseed` writes **0x673934**, not live RNG state **0x67393c**. It consumes four bytes, calls setter `0x4781f0`, and continues. Startup transfer from the saved-seed getter through RNG reset belongs to [native RNG lifecycle](native-prng.md); an implementation that immediately overwrites live state would change this opcode. `random_jump` retains the inclusive low-byte predicate `(result&255)<=threshold`, and `time_jump` compares the elapsed counter against `minutes*60`.

## Spell, guard, and creation wrappers

Spell-unit selector `0x4932a0` traverses a player's chain from global `0x665b08+4*player` via unit+0x6c. It chooses the **first** unit with matching type, energy high byte at +0xa3 at least the supplied threshold, and current order byte +0x4d in `{0x9c,0xa0,2}`. Eight isolated original-code checks confirm threshold equality, fractional energy immediately below the integer threshold, the three accepted orders, rejected orders/type, and first-eligible ordering. These use a fixture threshold, not proof of loaded technology costs. Teippi correlates the orders as ComputerAI, AiGuard, and Guard, and identifies the ushort energy field at +0xa2. [Order IDs](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h#L165), [unit layout](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.h#L186).

`disruption_web` selects unit60 with threshold word `0x51f89a` and issues order `0xb5` to the rectangle center; `recall_location` selects unit71 with threshold `0x51f892` and order `0x89`. These wrappers use the first eligible caster, not a proximity optimization. Energy-global initialization, list ordering, legality checks inside order execution, and actual spell effects remain dependencies.

Nuke wrappers select unit1 with threshold0, then require helper `0x436f60(ghost,1,ghost)` to supply a linked missile. `nuke_location` tries rectangle target-unit helper `0x432f60`; on success it issues target-unit order `0x7e`, otherwise point order `0x7e` to `(left+right)/2,(top+bottom)/2`, each with signed truncation. `nuke_pos` uses its encoded coordinates. Both write the ghost pointer to +0x80 of the object referenced by missile+0xd0. Missile lookup, target selection, and nuclear state machines are not recovered by this wrapper batch.

`place_guard`'s byte is a **position selector**, not a count. Helper `0x487ac0` selects signed16 town coordinates for selector0, mineral sprite coordinates for selector1, and for any other value queries category3 then category2 through `0x485de0`; on success the wrapper asks `0x4b22a0` to create a guard at the returned unit's sprite position. Exact category meanings, allocation, and replacement remain dependencies.

`guard_all` invokes `0x4b00a0` for every unit in the player's chain. `create_nuke` selects the first Nuclear Silo ID108 whose +0xd4 is zero. Its helper creates unit14, runs initialization, links silo/missile, and tail-calls `0x48d320`; allocation and unit invariants remain unresolved. `create_unit` creates the unit through `0x4261f0(type, x, y, player)`; for a created unit it asks `0x434900` for the unit's cloaking tech (10 for unit IDs 1/16/51/99/100/104, 9 for IDs 8/21, 44 = none) and, when that tech is not 44 and `0x407f90(tech, owner, unit)` returns exactly 1, starts cloaking with `0x41d410`. Corrected after the function atlas ([P32 opcode `0x6c`](atlas/p32.md)): the value compared with `0x2c` is this cloak tech, not an idle order. Diplomacy, transports, bunker entry, Dark Templar movement, patrol, help, and capture wrappers now have exact arguments, but their selection and order algorithms remain explicit JSON dependencies.

## Validation and next discriminating probes

The inspector verifies the exact EXE/schema/BIN hashes, rejects unknown versions before PE decoding, reproduces the switch, and requires all 113 native names plus the entire 64-type campaign set. Its static output is deterministic. Scalar operand widths are checked against the independently parsed PyMS schema; `debug`'s inline text is separately identified because the handler passes a pointer then jumps rather than consuming text through a string reader.

The authorized execution mode maps the known PE into a private x86-32 emulated address space, supplies explicit memory fixtures, and rejects every instruction outside reviewed address ranges. No helper is replaced with a result stub. Instruction limits and a sentinel return bound every call. Execution used **Unicorn 2.1.4 under x86_64 Rosetta Python**; arm64 Unicorn had crashed even on an agent-authored NOP mapping test, so the probe refuses an arm64 host runtime. Static decoding does not require Unicorn.

| Isolated original-code checks | Count | Outcome |
|---|---:|---|
| Attack roster player/count/unit bounds and capacity | 10 | Matched native encoding/truncation/rejection |
| Eight defense insertion helpers, six boundaries each | 48 | Matched count10/11, unit227/228 and capacity20 |
| train / wait_force / do_morph / wait_train, tank counter modes | 12 | Matched request+1 encoding, counter scope, continuation/retry |
| set_randomseed through native VM | 1 | Saved seed changed; live RNG state unchanged |
| Interleaved AI/BW call/return through native VM | 1 | Shared global overwritten across BINs; return followed second caller's PC |
| First hostile owner's resource ordering | 2 | Reversing fixture list changed jump; later wealthy owner did not rescue a failed first check |
| First eligible spell-unit selector | 8 | Matched type, integer/fractional energy, accepted orders and chain ordering |
| BIN ID resolver | 4 | Matched AI precedence, zero-offset/BW fallback, and failure |
| Pool creation, multirun, two scheduler traversals | 1 | Child inserted at head and first executed next traversal |
| Free/recreate flagged slot and scheduler | 1 | Flags6 became7 with BIN1; child removed before dispatch |
| Pending kill in another player's waiting thread | 1 | Wait2 preserved node for two visits; third removed it |

These results are **original-isolated execution with fixture state**, not a live campaign. The slot-reuse edge is now demonstrated in that bounded setting, while its incidence in official missions remains unknown. The probe output retains observed values, instruction counts, dispatch PC traces, inputs, and explicit empty helper-stub lists. Default inspector mode remains static; original execution requires `--execute-isolated-original-code --probe-out PATH` and an x86_64 runtime.

Remaining discriminating probes include nested calls, complete save/load restoration of the shared return PC, exact try_townpoint helper equality, defense consumers, and attack/guard/spell target helpers. Stubbing a helper can establish wrapper argument/control flow, never the helper's original gameplay.

Remaining concrete dependencies are recorded per opcode, with native addresses and arguments rather than generic “AI unknown” labels. The largest are `0x4af300` attack preparation, town counting/caps, region strength/state consumers, construction execution, guard allocation/replacement, transportation, and complete spell/nuclear/unit order state machines. The dispatcher contract is substantially recovered; those dependencies must be completed before claiming faithful opponents or campaign compatibility.

## Opcode index

`C` marks membership in the conservative campaign-reachable set. `B/W/D` mean unsigned byte / little-endian word / little-endian dword. The structured entry supplies exact PC/wait effects, calls, writes, and limitations for each row.
| Opcode | C | Name | Native scalar operands | Entry VA | Helper scope |
|---|---|---|---|---|---|
| 0x0 | C | goto | target:u16le | 0x493c5c | local effect |
| 0x1 |  | notowns_jump | unit:u16le, target:u16le | 0x493d2b | local effect |
| 0x2 | C | wait | delay:u16le | 0x494a1c | local effect |
| 0x3 | C | start_town | none | 0x493e51 | partial |
| 0x4 | C | start_areatown | none | 0x493e51 | partial |
| 0x5 |  | expand | limit:u8, target:u16le | 0x493e07 | partial |
| 0x6 | C | build | count:u8, unit:u16le, priority:u8 | 0x493f54 | partial |
| 0x7 | C | upgrade | level:u8, upgrade:u16le, priority:u8 | 0x493fad | partial |
| 0x8 | C | tech | technology:u16le, priority:u8 | 0x493ff2 | partial |
| 0x9 | C | wait_build | count:u8, unit:u16le | 0x493abe | partial |
| 0xa | C | wait_buildstart | count:u8, unit:u16le | 0x493abe | partial |
| 0xb | C | attack_clear | none | 0x493e92 | partial |
| 0xc | C | attack_add | count:u8, unit:u16le | 0x493ee6 | local effect |
| 0xd | C | attack_prepare | none | 0x493f2e | partial |
| 0xe | C | attack_do | none | 0x493be6 | partial |
| 0xf |  | wait_secure | none | 0x493aa1 | partial |
| 0x10 | C | capt_expand | none | 0x493f45 | partial |
| 0x11 |  | build_bunkers | none | 0x49416a | partial |
| 0x12 |  | wait_bunkers | none | 0x493c2b | partial |
| 0x13 | C | defensebuild_gg | count:u8, unit:u16le | 0x49404a | partial |
| 0x14 | C | defensebuild_ag | count:u8, unit:u16le | 0x49406e | partial |
| 0x15 | C | defensebuild_ga | count:u8, unit:u16le | 0x494092 | partial |
| 0x16 | C | defensebuild_aa | count:u8, unit:u16le | 0x4940b6 | partial |
| 0x17 | C | defenseuse_gg | count:u8, unit:u16le | 0x4940da | partial |
| 0x18 | C | defenseuse_ag | count:u8, unit:u16le | 0x4940fe | partial |
| 0x19 | C | defenseuse_ga | count:u8, unit:u16le | 0x494122 | partial |
| 0x1a | C | defenseuse_aa | count:u8, unit:u16le | 0x494146 | partial |
| 0x1b |  | defenseclear_gg | none | 0x494016 | local effect |
| 0x1c |  | defenseclear_ag | none | 0x494023 | local effect |
| 0x1d |  | defenseclear_ga | none | 0x494030 | local effect |
| 0x1e |  | defenseclear_aa | none | 0x49403d | local effect |
| 0x1f | C | send_suicide | mode:u8 | 0x494184 | partial |
| 0x20 | C | player_enemy | none | 0x4941b6 | partial |
| 0x21 | C | player_ally | none | 0x4941c6 | partial |
| 0x22 | C | default_min | strength:u8 | 0x4941d6 | partial |
| 0x23 | C | defaultbuild_off | none | 0x4941ed | partial |
| 0x24 | C | stop | none | 0x494a30 | local effect |
| 0x25 | C | switch_rescue | none | 0x49421a | partial |
| 0x26 | C | move_dt | none | 0x494207 | partial |
| 0x27 |  | debug | target:u16le | 0x494441 | partial |
| 0x28 |  | fatal_error | none | 0x494a0a | partial |
| 0x29 | C | enter_bunker | none | 0x494227 | partial |
| 0x2a | C | value_area | none | 0x494257 | partial |
| 0x2b |  | transports_off | none | 0x493ea4 | partial |
| 0x2c |  | check_transports | none | 0x493eb3 | partial |
| 0x2d | C | nuke_rate | rate:u8 | 0x494281 | partial |
| 0x2e | C | max_force | strength:u16le | 0x49426a | partial |
| 0x2f | C | clear_combatdata | none | 0x4942a5 | partial |
| 0x30 |  | random_jump | threshold:u8, target:u16le | 0x493cc2 | partial |
| 0x31 |  | time_jump | minutes:u8, target:u16le | 0x493cf9 | local effect |
| 0x32 |  | farms_notiming | none | 0x493ec5 | partial |
| 0x33 |  | farms_timing | none | 0x493ed4 | partial |
| 0x34 |  | build_turrets | none | 0x494177 | partial |
| 0x35 |  | wait_turrets | none | 0x493c38 | partial |
| 0x36 |  | default_build | none | 0x4941fa | partial |
| 0x37 |  | harass_factor | divisor:u16le | 0x493c45 | partial |
| 0x38 | C | start_campaign | none | 0x4942b5 | partial |
| 0x39 |  | race_jump | terran_target:u16le, zerg_target:u16le, protoss_target:u16le | 0x493d8b | partial |
| 0x3a |  | region_size | size:u8, target:u16le | 0x493dd6 | partial |
| 0x3b |  | get_oldpeons | count:u8 | 0x493e3a | partial |
| 0x3c |  | groundmap_jump | target:u16le | 0x493c98 | partial |
| 0x3d | C | place_guard | unit:u16le, position_selector:u8 | 0x4942c2 | partial |
| 0x3e |  | wait_force | count:u8, unit:u16le | 0x493b73 | partial |
| 0x3f |  | guard_resources | unit:u16le | 0x4942e8 | partial |
| 0x40 | C | call | target:u16le | 0x4942ff | local effect |
| 0x41 | C | return | none | 0x49431b | local effect |
| 0x42 |  | eval_harass | target:u16le | 0x493c6e | partial |
| 0x43 |  | creep | mode:u8 | 0x493fdc | partial |
| 0x44 |  | panic | target:u16le | 0x494332 | partial |
| 0x45 |  | player_need | count:u8, unit:u16le | 0x493f85 | partial |
| 0x46 |  | do_morph | count:u8, unit:u16le | 0x493b73 | partial |
| 0x47 |  | wait_upgrades | none | 0x493c1e | partial |
| 0x48 | C | multirun | target:u16le | 0x494349 | partial |
| 0x49 |  | rush | kind:u8, target:u16le | 0x4943aa | partial |
| 0x4a |  | scout_with | unit:u16le | 0x4943e0 | local effect |
| 0x4b | C | define_max | limit:u8, unit:u16le | 0x4943ec | partial |
| 0x4c | C | train | count:u8, unit:u16le | 0x493b73 | partial |
| 0x4d | C | target_expansion | none | 0x494410 | partial |
| 0x4e | C | wait_train | count:u8, unit:u16le | 0x493b51 | partial |
| 0x4f |  | set_attacks | count:u8 | 0x49441d | partial |
| 0x50 |  | set_gencmd | none | 0x494237 | partial |
| 0x51 |  | make_patrol | none | 0x494247 | partial |
| 0x52 |  | give_money | none | 0x494434 | local effect |
| 0x53 |  | prep_down | subtract:u8, minimum:u8, unit:u16le | 0x4944ad | partial |
| 0x54 |  | resources_jump | minerals:u16le, gas:u16le, target:u16le | 0x494468 | local effect |
| 0x55 |  | enter_transport | none | 0x494515 | partial |
| 0x56 |  | exit_transport | none | 0x494525 | partial |
| 0x57 | C | sharedvision_on | other_player:u8 | 0x494535 | partial |
| 0x58 | C | sharedvision_off | other_player:u8 | 0x49454d | partial |
| 0x59 | C | nuke_location | none | 0x494565 | partial |
| 0x5a | C | harass_location | none | 0x493a70 | local effect |
| 0x5b |  | implode | none | 0x4946e4 | partial |
| 0x5c | C | guard_all | none | 0x4946f1 | partial |
| 0x5d | C | enemyowns_jump | unit:u16le, target:u16le | 0x494719 | partial |
| 0x5e | C | enemyresources_jump | minerals:u16le, gas:u16le, target:u16le | 0x49476e | partial |
| 0x5f |  | if_dif | comparison:u8, threshold:u8, target:u16le | 0x4947f7 | local effect |
| 0x60 |  | easy_attack | count:u8, unit:u16le | 0x494846 | local effect |
| 0x61 | C | kill_thread | none | 0x494384 | local effect |
| 0x62 | C | killable | none | 0x494379 | local effect |
| 0x63 |  | wait_finishattack | none | 0x493bfd | partial |
| 0x64 |  | quick_attack | none | 0x493f0a | partial |
| 0x65 | C | junkyard_dog | none | 0x4946ac | partial |
| 0x66 | C | fake_nuke | none | 0x494298 | partial |
| 0x67 | C | disruption_web | none | 0x494610 | partial |
| 0x68 | C | recall_location | none | 0x49465e | partial |
| 0x69 | C | set_randomseed | seed:u32le | 0x49487a | partial |
| 0x6a |  | if_owned | unit:u16le, target:u16le | 0x493d5b | local effect |
| 0x6b | C | create_nuke | none | 0x49489d | partial |
| 0x6c | C | create_unit | unit:u16le, x:u16le, y:u16le | 0x4948d8 | partial |
| 0x6d | C | nuke_pos | x:u16le, y:u16le | 0x49493e | partial |
| 0x6e | C | help_iftrouble | none | 0x494990 | partial |
| 0x6f | C | allies_watch | resource_area:u8, target:u16le | 0x49499d | partial |
| 0x70 | C | try_townpoint | count:u8, target:u16le | 0x4949da | partial |
