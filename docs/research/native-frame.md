# Native 1.17 frame order and AI cadence

Target: StarCraft.exe 1.17.0.1, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Preferred ImageBase: `0x400000`. This report derives call order and counters from local instructions; it does not claim a complete running-game trace. [Selected instructions and table bytes](evidence/native-economy-frame-static.json).

## Object-step entry and accepted frames

The local object-step function is `0x445070`. It calls `0x4781c0` with ECX=1 on entry and ECX=0 on exit. That leaf stores the previous RNG enable flag in EAX and replaces global `0x673930` with ECX. This matches the independently documented structural signature for object-step detection: enable RNG, progress objects, disable RNG. [Samase_scarf detector](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/game.rs#L76).

Its caller at `0x444f37` increments global `0x52651c` and then invokes the step at `0x444f56`. This path follows a pause test at `0x444f27`; the paused branch goes to a different helper. The surrounding loop also checks turn readiness and foreground/replay state. One accepted unpaused object-step path increments the frame counter once and calls AI once.

The object-step call sequence is:

| Order | Native call / state | Established boundary |
|---|---|---|
| 1 | `0x4781c0(1)` | Enable deterministic RNG |
| 2 | `0x4b4010` | Creep-disappearance scheduling candidate; decrement counter `0x6c46d4`, reload from list-size lookup, tail-call `0x4b4a40` |
| 3 | `0x48dd90` | Native AI substep, expanded below |
| 4 | Counter `0x6663e4` | If zero, load 100; decrement; set byte `0x6663ec` when it reaches zero |
| 5, conditional | `0x40af30`, then `0x46c180` | Periodic creep/fog maintenance roles corroborated by the reference pipeline; complete geometry is separate work |
| 6 | `0x438ef0` | Unit-step entry; includes main/secondary order-timer redistribution counters 150/300 |
| 7 | `0x40a5f0` | Bullet-step candidate, with reference-pipeline corroboration |
| 8 | `0x40c910` | Non-unit flingy-step candidate |
| 9 | `0x418990` | Lone-sprite-step candidate |
| 10 | `0x4781c0(0)` | Disable deterministic RNG |

The **native addresses, order, and counter writes** are established by local code. Detailed roles of every internal call are not established merely by matching their position to another implementation. The periodic 100 counter does not imply that all unit vision changes happen only once per 100 frames. [Reference pipeline](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/game.cpp#L186).

## AI substep and script timing

`0x48dd90` calls these in order:

1. `0x4938e0`: script linked-list scheduler.
2. `0x4b0d70`: regional AI scheduler.
3. `0x496c80`: resource-area update.
4. Tail-call `0x48df50`: slower AI maintenance.

The script scheduler is therefore invoked once on each accepted unpaused object step. Combining this caller with its established old-value countdown closes the previous wait-unit question: `wait=N` skips N following object steps and becomes eligible on step N+1, unless another writer changes the state. `wait=0` still yields until the next scheduler traversal. See [native AI countdown](native-ai.md).

At the slowest through fastest speed indices, the local table at `0x503360` contains **167, 111, 83, 67, 56, 48, 42**. The surrounding frame loop adds the selected value to its next-frame tick. A 30-frame retry becomes eligible on the 31st subsequent logical step; at a steady fastest rate the nominal time is `31*42 = 1302 ms`. Pauses, turn delays, and catch-up mean this is not a measured wall-clock guarantee.

## Slower AI maintenance

`0x48df50` maintains three native counters independently:

- `0x69b568`: old nonzero decrements; old zero reloads 300 and services the longer refresh branch. Thus its normal period is 301 invocations.
- `0x69b56c`: decremented only when the preceding counter expires. Its zero/dirty path reloads 6, clears `0x69b560`, and clears unit status bit `0x80` through the active-unit chain. The dirty flag can shorten this outer refresh schedule.
- `0x69b564`: post-decrement eligibility. Old zero reloads **7** while `0x5349d8 < 4500`, otherwise **37**. It then calls `0x483400`, `0x486c80`, and tail-calls `0x4acdf0`. Normal intervals are therefore **8** or **38** object steps, rather than 7 or 37.

`0x483400` advances a persistent player index at `0x67f7a8`, wrapping 8 to 0. It services only player type 1 with PlayerAi flag `0x2`, updates byte timers, and enters spending/development helpers. `0x486c80` independently advances the player index at `0x685574`, wrapping the same way, and visits that player's town list. Under steady conditions a particular player's slot is revisited every **64** early or **304** late object steps. These slot intervals are derived from the nested counters; actual state changes can be skipped by native eligibility predicates.

The native trigger timer identifies `0x5349d8` as elapsed game time: a separate old-value counter reloads 15 and increments it on the next zero-eligible visit. One such game-time unit therefore corresponds to 16 eligible scheduler calls, independently of the speed-dependent millisecond decrement used by trigger waits. The threshold 4500 uses these game-time units, not the object-step frame counter or an assumed 24-frames-per-second conversion. [Native elapsed timer](native-triggers.md).

## Triggers are outside the object-step function

After the object/pause branch, `0x444f5b` enables RNG, loads the selected speed-table value into ECX, calls `0x4be500` at `0x444f71`, and disables RNG. This proves the outer relative ordering: object simulation and AI precede this trigger scheduler call. The trigger function has its own timer and pause handling; an unconditional call in this outer region does not prove unconditional trigger execution. [Native trigger recovery](native-triggers.md).

## RNG initialization versus live state

The initial-seed getter/setter `0x4781e0`/`0x4781f0` access `0x673934`. The live RNG state is a different dword, `0x67393c`. Reset function `0x478240` clears the total counter `0x673938`, clears 1024 bytes of source counters beginning at `0x673940`, and writes its ECX argument to live state.

Two initialization sequences at `0x444029`→`0x444030` and `0x44409b`→`0x4440a2` call the initial-seed getter, copy EAX to ECX, then call reset. This closes a concrete transfer path between the two seeds. The AI `set_randomseed` opcode calls the initial-seed setter; it does **not** itself reset the live state. Save/load and all initialization branch conditions still require their own trace. [RNG arithmetic](native-prng.md).

## Completion boundary

This closes immediate AI scheduling cadence, post-decrement periods, the top-level object/AI/trigger order, local speed values, and an initial-to-live RNG transfer. Unit/bullet internal phases, command application, save/load restoration, and runtime ordering under every branch are separate requirements. The source corpus describes those candidates; native call order alone is not their completion proof.
