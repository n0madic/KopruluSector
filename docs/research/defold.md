# Implementing StarCraft algorithms in Defold

Defold can provide rendering, input, resource packaging, audio, and UI. AI and combat fidelity depend on a separate simulation implemented as Lua modules with explicit state. This is an architectural conclusion from the researched dependencies, not a KopruluSector performance measurement.

## Simulation boundary

Store units/orders, projectiles, movement, visibility, effect timers, player resources/upgrades/tech, Trigger VM, AIScript/BWScript VM, IScript, towns/guards/requests/regions, RNG, and command queues in one world state. Presentation reads that world and its events; visual animation time does not determine gameplay impacts.

Proposed responsibilities:

```text
sim/world.lua       creation, stepping, saving
sim/orders/         unit order state machines
sim/combat/         targeting, weapons, projectiles, damage, spells
sim/movement/       paths, collision, locomotion
sim/economy/        mining, construction, production, requirements
sim/vision/         fog, detection, shared vision
vm/iscript.lua      script state and gameplay effects
vm/triggers.lua     conditions, actions, waits, scheduling
ai/vm.lua           AIScript/BWScript, PC/wait/flags, threads
ai/towns.lua        workers, economy, building placement
ai/spending.lua     resource and production requests
ai/military.lua     regions, guards, attack/defense groups
ai/unit.lua         AI decisions, special orders, spells
```

These are proposed boundaries, not implemented modules or a copy of original memory layout. AI, player commands, and triggers should use the same gameplay action handlers.

## Clocks, ordering, and saves

Use a fixed logical step measured in BW frames; rendering FPS does not set AI cadence. A mission-manager accumulator can handle speed changes, bounded catch-up, and independent visual interpolation. Defold's `engine.fixed_update_frequency` configures fixed_update Hz; `display.update_frequency` concerns the engine loop. Neither is automatically the original game speed or script cadence. [Project settings](https://defold.com/manuals/project-settings/).

Use one simulation owner to traverse the explicit native-compatible entity lists. Defold documents unspecified ordering among individual objects' `update`, `fixed_update` and `late_update` callbacks; distributing authoritative unit/AI steps across those callbacks would not preserve original traversal or shared-state ordering. Fixed updates can occur zero or several times per rendered frame. [Application lifecycle](https://defold.com/manuals/application-lifecycle/) (checked2026-10-09).

The [native resource/Town probes](native-resource-areas.md) provide a concrete reason: active-list order controls selected minerals and retained gas candidates, while automatic spending requests change the next worker allocation percentage. Preserve these lists and pass boundaries inside the Lua world, independently of presentation-object order.

The local 1.17 speed table at `0x503360` statically contains 167/111/83/67/56/48/42 ms, so 42 ms Fastest is established from the executable ([native frame](native-frame.md)); wall-clock pacing, pauses, turn delays and catch-up remain unmeasured. The value does not imply that every AI subsystem runs every 42 ms. VM, workers, military decisions, visibility, and triggers need separate scheduling evidence.

Persist PC, wait, player/town, flags, and the proven call/return data explicitly. The native 1.17 handlers use a global return PC; introducing a conventional per-thread call stack changes this behavior. [Native AI](native-ai.md). Lua coroutines do not provide an automatic compatible save format. Persist reference generations and entity traversal order so load does not change targeting or tie-breaking.

## Arithmetic and performance

Do not use math.random for simulation decisions. The custom RNG must reproduce exact uint32 arithmetic; direct large double multiplication fails. [Native RNG](native-prng.md).

Prefer dense arrays/SoA, reused tables, and spatial indexing for hot data. Minimize per-unit script components and message traffic. Measure GC and engine API overhead before choosing an optimization. [Defold runtime performance](https://defold.com/manuals/optimization-speed/).

Use Lua 5.1 features for portability. Defold documents Lua 5.1.4 on HTML5 and disabled JIT on iOS; desktop LuaJIT performance does not establish a mobile budget. [Lua manual](https://defold.com/manuals/lua/).

Native extensions for pathfinding, spatial queries, or visibility are an option after profiling real missions. Defold supports C/C++ extensions, but this does not require putting the entire original engine in an extension. [Native extensions](https://defold.com/manuals/extensions/).

## Acceptance

Start with narrow original comparisons: RNG, one attack, one harvest cycle, one build request, one wave, and one trigger wait. Continue with a real mission loop, player interference, and save/load. Matching army composition and first-attack time alone can hide incorrect guards, targeting, recovery, or spellcasting.

Defold validation separately requires a Bob build, clean runtime, correct timing/rendering, a release profile, archive resource inspection, and target-platform runs. No Defold runtime exists in the current project, so these checks were not performed in this research.

The [64 supported-map resource compositions](native-campaign-resource-areas.md) connect actual resource UNIT properties to original terrain heights/graphs and generated areas. The importer must retain preplaced racial gas buildings as resource containers, preserve pixel coordinates/amount words and reproduce height/group/connected-path filters. That evidence uses declared runtime-controller/visibility inputs and reconstructed original graph outputs; complete mission startup and dynamic obstructions require their own integration.
