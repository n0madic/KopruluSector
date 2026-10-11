# Native 1.17 research and upgrade transitions

Research/upgrade startup, progress bits, state writers, refunds and timer boundaries are recovered from the reviewed StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. See [static witnesses](evidence/native-research-upgrade-static.json), [202 isolated original-code checks](evidence/native-research-upgrade-probes.json), and the [inspector/probe](../../tools/research/inspect_native_research_upgrade_117.py).

All **202 cases passed without helper stubs**. Complete start/refund/bit/state helpers and complete waiting-order paths execute actual reviewed callees in private Unicorn memory. Consumer success and timer completion cases have explicitly recorded prefix checkpoints. No WinMain, full game, Windows API or host file/network operation executes. Fixture sprites contain no images, so the actual sprite-animation loop takes its empty-list return; image/iscript execution is not represented by a successful-return stub. These checks establish the declared transitions, not an entire production lifecycle or campaign result.

## Caller legality and payment

AI research consumer `0x485ca0(ECX=player, EDX=producer)` reads the top spending ID and invokes requirements wrapper `0x407dc0`. Upgrade consumer `0x485d30` invokes wrapper `0x408140`. Both require return value exactly1. The [general requirements report](native-dat-requirements.md) establishes ownership, completion, disabled state, availability/progress, current level/maximum and the actual requirement bytecode. The [training report](native-training.md) establishes current stock caches, AI available-budget gates and spending cost-removal helpers.

After requirements succeed, research calls resource gate `0x482720`, then start helper `0x43af40(producer, technology_ID)`. Upgrade calls gate `0x482770`, then `0x43afb0(producer, upgrade_ID)`. A resource/budget failure retains the request and demand in these consumers; this differs from ordinary unit training's noncampaign failure removal.

On requirements errors2/23/21/8/7/13, these consumers call failed removal `0x482c70`, then tail-jump to retry dispatcher `0x487110` with the producer. Other requirements errors retain the request. Native complete failure cases cover stock shortage, insufficient available budget, disabled producer and producer mismatch. They observe unchanged request count/demand and no start timer. Error-removal/retry branches are statically recovered here but the dispatcher/callees are a separate execution contract.

After the start helper returns1, the consumer issues primary order0x4b for Research or0x4c for Upgrade through `0x42a2c0`, then tail-jumps to successful removal `0x482bc0`. The caller does not test a return from the order issuer. Two native success **prefixes**, retaining unchanged original ID0 recipes, execute actual requirements/resource/start functions and stop before primary-order issue. They observe payment, timer, active ID and progress bit while demand and spending count are still present. Primary-order allocation/activation and the subsequent successful pop are not counted as executed by these prefixes.

## Research startup

`0x43af40(ECX=producer, stack=[technology_ID])` reads only the low byte of its argument and rejects values>=44 with return0. It uses the producer-owner byte+0x4c for its actual stock check `0x408670(owner, ID, feedback=1)`. A failed check returns0 without changing active ID/timer/progress or deducting stocks.

On acceptance it performs the following operations in native order:

1. Store technology ID at unit+0xc8.
2. Store u16 research-time DAT value `word[0x51f810 + ID*2]` at unit+0xc6.
3. Deduct cached current mineral/gas costs through `0x408650`.
4. Set the player's technology in-progress bit through `0x4a05c0`.
5. Dispatch sprite animation0x13 through `0x417220(sprite, 0x13, 1)` and return1.

This helper does not independently check ownership against an external player, completion, disabled status, technology availability, researched state, progress, prerequisites, AI demand or available budget. Those checks belong to its callers. Native fixtures with incomplete/disabled/already-researched state still return1 from this helper when stocks suffice. Stand-alone argument256 becomes technology0 because of byte parsing; the higher-level wrapper's own bounds must remain separate.

## Upgrade startup and integer widths

`0x43afb0` similarly reads the low stack byte, rejects values>=61, and checks current stocks through `0x408710(owner, ID, feedback=1)`. The actual price uses current researched level and truncates mineral/gas results to u16, as already documented in the production/training reports.

On acceptance it stores upgrade ID at unit+0xc9 and computes:

```text
unit.timer_u16 = (time_base_u16 + current_level_u8*time_factor_u16) mod 65536
unit.next_level_u8 = (current_level_u8 + 1) mod 256
```

Time base/factor arrays are `0x524ad0 + ID*2` and `0x524b50 + ID*2`. The code calls current-level getter `0x49a470` separately for the timer and next-level byte. It then deducts cached current stocks, sets the upgrade in-progress bit through `0x4a05f0`, dispatches animation0x13 and returns1.

Native starts cover IDs on both sides of the technology24/upgrade46 state-array splits. Upgrade fixtures at current levels0/2/255, base time65535 and factor3 confirm timer wrap and next-level255→0. Incomplete/disabled/maxed-level fixtures prove that this helper relies on caller legality rather than duplicating the wrapper. These synthetic width tests do not assert level255 occurs in a normal campaign.

## Progress bits and researched/level writers

In-progress state uses a bit per ID:

| Domain | Selected byte |
|---|---|
| Research | `0x536510 + player*6 + (ID>>3)` |
| Upgrade | `0x5366c0 + player*8 + (ID>>3)` |

Setters `0x4a05c0`/`0x4a05f0` OR the selected bit `1<<(ID&7)`; clearers `0x4a0500`/`0x4a0530` AND its complement. They take ECX=ID low byte, EDX=player. Ninety-six complete native set/clear cases use players0/7, bit/byte boundaries, expansion IDs and initial bytes0x55/0xaa. All other bits are preserved. These are flags, not reference counts for multiple simultaneous producers.

Research state writer `0x49a300(ECX=player, EDX=ID, stack=[value])` stores the low value byte. IDs<24 address `0x534224 + player*24 + ID`; higher IDs address `0x536408 + player*20 + ID` **only if expansion byte `0x536720` is nonzero**. Upgrade level writer `0x49a370` uses `0x534590 + player*46 + ID` below46 and `0x5365de + player*15 + ID` at/above46, with the same expansion gate. The adjusted higher-ID bases retain the original ID in their formulas.

Thirty-two native state-writer cases distinguish players0/7, each side of both splits, expansion enabled/disabled and argument257→stored1. These writers do not enforce nominal technology/upgrade capacities themselves; fixtures stay within valid IDs. The general requirement getters' capacity and availability checks remain separate.

## Two refund contexts

The EXE contains distinct full-refund and three-quarter-refund helpers. This report uses those observable names rather than declaring every caller a user cancellation or destruction path.

| Domain | Full refund | Three-quarter refund |
|---|---|---|
| Research | `0x43ab80` | `0x43adf0` |
| Upgrade | `0x43abe0` | `0x43ae60` |

Research helpers return immediately for active-technology sentinel44. Otherwise they read current u16 mineral/gas DAT costs and add either the full price or `floor(3*price/4)` into current stocks, with u32 wrap. Both clear the technology in-progress bit, store active technology44 and timer0. The full helper also dispatches animation0x14; the three-quarter helper does not call the sprite-animation helper. Neither helper issues a primary order or updates AI demand/budget here.

Upgrade helpers return for upgrade sentinel61. Full refund additionally requires the **producer type's DAT building property bit0x1**; it does not test grounded status bit0x2. Its cost helper `0x43b210` recomputes `(base + current_level*factor) mod65536`, separately for mineral/gas, and refunds that full result. Three-quarter helper `0x43b2d0` computes the same truncated price, then refunds floor(3*price/4). The three-quarter caller has no corresponding DAT building-property guard.

After a valid upgrade refund, clear the progress bit, store upgrade61, timer0 and next-level0. Full refund dispatches animation0x14; three-quarter refund does not. Native cases confirm the building-property distinction, sentinel no-op, floor rounding, u16 price wrap and u32 stock wrap.

Refund is recomputed from the **current global level**, rather than a saved amount paid at startup. Two native sequences start level0, explicitly change the fixture level to2, then refund: with base prices50/25 and factors3/7, full refund leaves stocks306/114 after starting from300/100; three-quarter refund leaves292/104. The inter-call level change is controlled fixture input, not proof that normal simultaneous upgrades create that condition. It demonstrates the exact price dependency.

Pinned GPTP distinguishes command cancellation helpers from unit-death cleanup and reconstructs the Research/Upgrade order code. Its version1.16.1 addresses are never transferred into local1.17. The source corroborates roles while local instructions establish widths and transitions. [GPTP cancellation callers](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/recv_commands/CMDRECV_Cancel.cpp#L88), [GPTP order reconstruction](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/research_upgrade_orders.cpp#L19).

## Order timer and completion boundary

Research handler `0x43ac40` tests active-technology!=44. If active, it writes `timer=(old_timer-1) mod65536`, then enters completion when **old_timer==0**, the technology is already researched, or cheat bit0x2 is set. Otherwise it returns and keeps waiting. Thus old1 becomes0 without completing under ordinary unresearched/no-cheat state; the next invocation at old0 writes65535 and enters completion. Old65535 becomes65534 and continues waiting. These are handler invocations, not an inferred wall-clock duration.

Upgrade handler `0x43ace0` first requires unit status bit0x2 and active-upgrade!=61 to enter the decrement/wait section. It writes the same wrapping timer decrement and completes when old0, current level>=stored next level, or cheat bit0x2. Otherwise it returns waiting. If that initial status/active test fails, static control flow proceeds to cleanup instead of decrementing. This grounded-state gate is distinct from the full-refund helper's DAT building-property gate.

Twelve native cases execute complete waiting paths or stop at the exact completion branch (`0x43ac7d`/`0x43ad36`). They cover old timers2/1/0/65535, already-known state and cheat0x2. Completion prefixes **do not** execute or replace sound, UI, upgrade effects or final primary orders.

The static completion bodies establish these call/state contracts:

- Research calls completion-sound helper `0x446b70`, writes researched1 through actual setter `0x49a300`, calls UI refresh `0x4a7ed0`, dispatches animation0x14, clears the progress bit, and sets active technology44. With AI it issues ComputerAI order0x9c through `0x42a2c0`; without AI it tail-jumps to next-order helper `0x42a5a0`.
- Upgrade calls sound `0x447060`; if current level is below stored next level and also below allowed maximum, it writes that next level through `0x49a370` and calls upgrade-effect helper `0x43b0b0`. It refreshes UI, dispatches animation0x14, clears progress, stores upgrade61/next-level0, then uses the same AI/non-AI order split.

Those completion bodies do not reset timer to0. Their sound/UI/effect/order dependencies remain explicitly outside this probe's completion execution. The state writers are independently executed leaf evidence; that is not a claim that the whole completion chain has executed.

## Reproduction and closed scope

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_research_upgrade_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-research-upgrade-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-research-upgrade-probes.json
```

Default inspection is static and gates the exact EXE hash. Explicit execution requires the reviewed x86_64 runtime. Evidence includes target/script/dependency hashes, architecture/runtime versions, reviewed VA ranges, instruction counts, call traces, fixture inputs and each complete/prefix boundary. All202 cases matched.

Closed contracts are startup/payment/timer fields, byte/u16 widths, set/clear progress bits, expansion-gated state writes, full/three-quarter refunds, complete waiting-handler decisions, selected AI consumer failure paths and success prefixes. Remaining concrete dependencies are retry `0x487110`, primary-order allocation/activation, image/iscript animation, complete local-viewer sound/UI refresh, applied upgrade effects, and full completion/order cleanup chains. [Simulation scope](simulation.md) tracks the broader unit/order loop; these partial chains do not certify campaign or Defold parity.
