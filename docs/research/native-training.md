# Native 1.17 training, supply, payment and request removal

This report follows production requests into the actual queue/resource helpers and ordinary training consumers in the reviewed StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. See the [static witnesses](evidence/native-training-static.json), [172 isolated original-code checks](evidence/native-training-probes.json), and [inspector/probe](../../tools/research/inspect_native_training_117.py).

All **172 checks passed without helper stubs**. Original x86 instructions execute in private Unicorn memory, including their actual reviewed callees. The probes execute neither WinMain nor a complete game, Windows API or host file/network operation. Fixture data initializes DAT values, producer state, stocks, supply and AI spending entries. Complete reviewed fixture paths are distinguished from the unexecuted special-morph, merge and retry branches. These are production contracts, not proof of a campaign outcome or completion animation.

## Three accounts and the temporary cost cache

The production path maintains three separate accounts:

| Account | Native location | Update covered here |
|---|---|---|
| Current mineral/gas stocks | `0x5263d0 + player*4`, `0x526400 + player*4` | Queue insertion can deduct the current unit price. |
| Accumulated AI mineral/gas/supply demand | First three dwords of PlayerAi | Request removal subtracts demand with saturation. |
| AI available spending budget | PlayerAi+`0xc`/`0x10`/`0x14` | Successful request removal subtracts its cost with u32 wrap; failed removal leaves this budget. |

PlayerAi is `0x67d068 + player*0x4e8`. Its spending heap starts at +0x18; top kind is byte +0x19, ID word +0x1a, context dword +0x1c; count is byte +0x210. The [request-generation report](native-production.md) establishes demand accumulation; the [economy report](native-economy.md) establishes heap insertion/pop and budget preparation. Neither accumulated demand nor available budget is itself the player's current stock.

Cost checks write per-player temporary dwords at `0x53dad0` mineral cost, `0x53db00` gas cost and `0x53db30` supply cost. Unit check `0x4086b0(ECX=player, EDX=unit_ID, stack=[check_supply, feedback])` first caches u16 mineral/gas DAT costs from `0x523520`/`0x5236e8`. It calls supply check `0x4084d0` only when the first stack argument is nonzero. It then checks stocks through `0x4085e0` using **signed** stock>=cached-cost comparisons. Synthetic mineral stock `ffffffff` consequently fails a positive mineral cost.

Technology check `0x408670` caches u16 prices at `0x51f760`/`0x51f7b8`, and sets cached supply to zero. Upgrade check `0x408710` calls the actual base+current-level×factor helpers `0x43b2a0`/`0x43b270`, explicitly truncates each result to AX, and also sets cached supply to zero. A native width case with base65535, level2 and factors3/7 caches prices5/13. Prices and current-level arrays are detailed in the production report.

AI gates `0x4826d0` (unit), `0x482720` (technology) and `0x482770` (upgrade) perform these stock checks, then additionally require each available AI budget dword to be **unsigned >=** the corresponding cached cost. The unit gate enables supply and feedback; technology/upgrade gates disable feedback. Five stock/budget fixtures for each latter gate distinguish current stock failure from insufficient available AI budget.

Actual debit helper `0x408650(ECX=player)` subtracts cached minerals and gas from current stocks using unchecked u32 arithmetic. It does not change supply, demand or AI budget. The debit therefore depends on the immediately prepared cache; replacing this cache with an unrelated calculated price would change the native call contract.

Feedback helper `0x4461f0` takes an early return when player differs from current viewer `0x508d50`, or replay state `0x67cc90` is nonzero. Failure probes use player7/viewer0 and execute this actual early return. Local-viewer UI/sound callees `0x49e780`, `0x4a7d70` and `0x46ae50` are not exercised or replaced by stubs.

## Supply calculation and widths

Supply check `0x4084d0` caches the unit's supply byte at `0x523e10 + ID` as a dword. DAT property `0x400` doubles the **dword**, without byte wrap. Supply0 or property `0x1000000` returns success before the stock/capacity calculation. For requested ID103, the function subtracts the supply byte of ID38 at `0x523e36`, with u32 wrap, before testing remaining supply. Reference names correlate these IDs with Lurker and Hydralisk; the subtraction is native evidence.

The live supply check differs from the demand-cost helper `0x4087c0`, which doubles a byte and can wrap. Native checks with synthetic raw supply127/128/255 cache254/256/510 here. The earlier demand probe with raw supply128/property0x400 wraps to zero. A faithful implementation must retain these different widths in their different callers.

Race/group byte `0x523c40 + ID` selects the supply arrays. Conventional groups1/2/4 use the following dword bases, each indexed by player*4:

| Group | Live provided capacity | Used | Global cap |
|---|---|---|---|
| 1 | `0x529424` | `0x529454` | `0x529484` |
| 2 | `0x5294b4` | `0x5294e4` | `0x529514` |
| 4 | `0x529544` | `0x529574` | `0x5295a4` |

Helper `0x414180(unit_ID, player)` returns `min(provided, cap)-used`, with an unsigned minimum and wrapping dword subtraction. Cheat bit `0x20000` selects `cap-used`, bypassing live provided capacity. Native checks cover all three conventional races, both minimum branches, full capacity and underflow. These quantities use the game's internal supply scale; this batch does not substitute displayed supply numbers. The frozen historical runner calls the first and third arrays `maximum` and `total`; those are fixture-variable labels. Original completed-unit accounting `0x4145a0` adds DAT supply-provided bytes to the **first** array, while live-unit accounting `0x414760` adds supply-required bytes to the middle array. The third array is the global cap. [Original ownership and accounting witnesses](native-trigger-ownership.md) document the corresponding counter producers. The previous table labels had these capacity meanings reversed; the recorded arithmetic probes remain unchanged.

The check separately requires signed `(used+cached_supply)<=cap` and signed `(remaining-cached_supply)>=0`. It never increments used supply here. A synthetic Lurker cost below Hydralisk cost wraps the cached difference to `fffffffe`; the original signed conditions accept that particular fixture. This establishes arithmetic width, not a claim that retail DAT defines such costs.

Supply-race helper `0x414550` tests group bits in precedence1, then2, then4; no matching bit produces low-byte value4. The training race-match predicate `0x4860a0` uses **different precedence**: requested group bit2 first, then1, otherwise test the producer's bit4. It sets error25 on failure. All64 requested/producer group combinations0..7 are executed, including requested group0's fallback. A single common race-selection helper would not preserve both native contracts.

## Queue insertion, payment and AI metadata

Unit queue helper `0x41b4b0(ECX=producer, stack=[requested_ID])` searches at most five positions from head byte `producer+0xa4`, selecting the first word228 at `producer+0x98`. Each subsequent increment wraps at five. A full queue returns0 without checking resources or deducting stocks.

For the selected position it calls the actual unit resource check with `check_supply=(selected_slot==head)` and feedback1. Therefore a unit appended behind the current head skips the supply test. A native fixture with used supply equal to total rejects insertion at the head but accepts an appended item. In the latter path, the existing cached supply dword is left unchanged; it is not recomputed or reset.

On success the function writes the requested ID. If that **requested unit's** DAT property bit0x1 is clear, it deducts cached current minerals/gas through `0x408650`; if set, it omits that debit. It does not update used supply or the AI accounts. The building-property omission is tested explicitly rather than interpreted as a general cost exemption elsewhere.

AI wrapper `0x41b550(producer, stack=[ID, tag, context])` locates the first empty slot, calls the actual queue helper, then writes training metadata through `0x4844a0`: tag byte at `AI+9+slot` and context dword at `AI+0x18+slot*4`, where producer+0x134 supplies the AI pointer. This metadata writer has no null/type guard. Probes supply a valid AI allocation and cover all five queue heads and occupancies0..5, observing queue, stocks, tags, contexts and cached supply together.

Secondary-order helper `0x42a1e0(producer, order)` does nothing if byte+0xa6 already equals the requested order. Otherwise it changes +0xa6 and clears dwords+0xe8/+0xec and byte+0xe2. The successful training consumer requests order0x26. A probe starting already at0x26 preserves those fields; another starting at0 clears them. Numeric Train/UnitMorph/merge names can be checked against [pinned Teippi order constants](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h).

## Removing a spending request

Cost-removal function `0x482260(ECX=player, EDX=consume_available)` reads the top request ID and kind. It obtains costs through `0x481ff0`, which uses the already recovered unit/technology/upgrade cost helpers. It subtracts each demand total with saturation at zero. When EDX is nonzero, it additionally subtracts each available-budget dword with unchecked u32 wrap. Current stocks are not changed by this function.

Before examining request kind, it skips **all cost removal** for top IDs `{44,62,63,68,103}`. This ID-only exception is executed for every listed value, with preexisting nonzero demand/budget. The following heap pop still occurs. Reference schemas correlate these IDs with Guardian, Devourer, Dark Archon, Archon and Lurker; interpreting why the original skips their demand costs is separate from proving the exception.

Successful removal `0x482bc0` sets consume_available1, removes costs, then calls actual heap pop `0x482310`. Failed removal `0x482c70` sets consume_available0, removes demand, then pops. Four saturation/underflow cases distinguish demand saturation from available-budget wrapping, on both success/failure paths. These helpers assume a valid nonempty spending heap; the fixtures do not certify malformed empty-heap input.

## Ordinary training consumer

The consumer at `0x487de0` takes ECX=player, EDX=producer and two stack dwords; the first is unused in the reviewed body, and the second controls the retry call. For an ordinary requested ID it uses the supplied producer, checks native race matching, then requires the specialized [unit-production gate](native-requirements.md) to return exactly1. It checks current resources, supply and available AI budget, then the native [unit-limit predicate](native-economy.md).

If producer remains the supplied unit, it enqueues through `0x41b550` with the top request's kind and context as training metadata. Failure to enqueue returns0 and retains the spending request. Success removes the request through `0x482bc0`. Producer types35/43/38 then issue primary order0x2a through `0x42a2c0`; other types invoke the recovered secondary-order0x26 helper and return1.

Three full ordinary success paths execute this consumer and every called helper: request kinds1/2, plus secondary Train already active. They observe the emitted ID/tag/context, actual stock debit, demand removal, available-budget debit, heap pop, secondary order and state clears. They use requested ID0 and producer111, correlated with Marine/Barracks. They stop by normal return, not a prefix checkpoint.

Failure handling is not a generic “retry later” rule:

| Failure | Native action |
|---|---|
| Resource/budget/supply failure in campaign mode (PlayerAi flag0x20) | Return0, retain top request and demand. |
| Same failure outside campaign mode | Failed removal; optionally call retry helper. |
| Unit-limit failure | Failed removal, return0. |
| Race/requirements failure with error2/23/21/8/7/13 | Failed removal; optionally call retry helper. |
| Other race/requirements error | Return0 and retain request. |

The actual error getter `0x4b38a0` reads global `0x6be654`. If the second stack dword is zero, selected failed-removal paths call `0x487110` with the original producer. This batch uses value1 and does not execute that retry helper. Native failure cases distinguish race error25, unavailable unit error2, wrong producer error25, unit limit255, and campaign/melee resource failure. Calling all requirement failures permanent or all errors retriable would alter this policy.

### Town worker training

Consumer `0x487d30(ECX=player, EDX=producer, stack=[worker_AI])` first requires the top request's context to equal worker_AI+0x14. It then checks race, specialized requirements, resources/budget and unit limit. A limit failure removes the request through the failure helper; earlier failed checks retain it. On acceptance it uses plain queue helper `0x41b4b0`, which does **not** write the AI training tag/context; producer35 takes the primary morph branch, other types use secondary order0x26, then successful request removal. No general return-value contract is assigned to this function's leftover EAX.

Six complete native paths use requested SCV ID7/Command Center producer106: success, town mismatch, race mismatch, unavailable unit, resource shortage and unit limit. The success observes payment and both AI-account changes, while seeded training tags remain unchanged. The broader worker selector and reassignment service are covered by the [worker report](native-workers.md).

## Special branches and remaining dependencies

The ordinary consumer's static switch also routes requested IDs44/62 to parent selector `0x4b03c0`, ID103 to `0x4b0350`, ID68 to paired-unit selector `0x4b0430`, and ID63 to `0x4b0250`. Pair paths issue orders0x69/0xb7 to each selected unit through `0x42a380`, and return0 after successful request removal; ordinary/morph paths return1. Failure to select the relevant parent/pair removes the request through the failure helper.

When a morph parent differs from the supplied producer, the consumer calls plain queue insertion, removes the parent's existing AI via `0x48dbc0`, then installs guard AI through `0x4b25b0` for top kind2 or military AI through `0x4ae760` otherwise. These selectors, merge order execution, AI migration and primary-order helper are not declared recovered merely because the switch/call arguments are visible. Teippi's modified reconstruction corroborates this dependency structure but is not the local behavioral oracle. [Pinned Teippi training implementation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L1013).

The batch closes the temporary costs, supply tests, stock/budget gates, circular enqueue/payment, metadata writes, request cost removal, complete ordinary nonmorph success/failure paths and the reviewed town-worker branches. Remaining concrete dependencies are parent/pair selection, retry `0x487110`, primary-order allocation/issue, AI migration and the subsequent Train/UnitMorph/Research/Upgrade execution and completion/cancellation state machines. Queue acceptance alone does not prove a finished produced unit.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_training_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-training-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-training-probes.json
```

The inspector is static by default and rejects a different EXE hash before decoding. Native execution requires the explicit flag and reviewed x86_64 runtime. The probe imports only the unit-requirements inspector's hash and reviewed witness ranges; output records both script hashes, the exact target, runtime versions/architecture, instruction counts, native call traces and absence of stubs. Its instruction allowlist rejects unexpected execution. All172 cases matched their explicit fixture contracts.
