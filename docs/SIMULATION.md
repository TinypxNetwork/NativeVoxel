# Native singleplayer simulation

`mc::Simulation` is a separate offline authority. It never opens a connection and
rejects an online `Game`. It currently implements real food-state transitions,
time/weather updates, furnace processing, brewing, basic block harvesting and
dropped item/XP entities; this is not an integrated-server completion claim.

Call `start(game, seed)` after creating valid terrain and positioning the player.
Then call `Simulation::tick(game, input)` once every 50 ms **instead of** the
ordinary `Game::tick` for that step. Simulation calls Game's existing player
physics exactly once, accounts for movement/jump exhaustion, updates survival
state, and advances world time and loaded furnaces/brewing stands. Pausing the singleplayer game
means pausing these ticks.

The application now installs this authority through [LocalSession](LOCAL_SESSION.md).
The current stage includes:

- The actual 1.8.9 `FoodStats` exhaustion threshold/cap, saturation consumption,
  hunger, 80-tick natural regeneration and difficulty-dependent starvation.
  Creative invulnerability is respected. Peaceful automatic food/health recovery
  is handled at its original tick intervals.
- World total time, the daylight-cycle rule, clear-weather/rain/thunder timers,
  original Java random algorithm and exact float rain/thunder transitions.
  Nether and End providers skip weather. `settings.weatherCycle=false` is a
  native pause option, not a vanilla 1.8.9 gamerule.
- Original furnace ignition, fuel-container handling, cooking/cooling, blocked
  output, one-item smelting, wet-sponge bucket conversion and lit-block changes.
  Recipe/fuel values come from the exported Java registry. Input changes reset
  cooking only when item/metadata/NBT change, not merely when stack count changes.
- Brewing ingredient expressions, potion-effect comparisons, 400-tick brew
  cycles, ingredient cancellation and consumption, and bottle occupancy metadata.
  All 15 ingredient formulas and all 32,768 potion metadata values are derived
  from the original registry. A loaded positive brew timer follows Java's missing
  ingredient-cache behavior, including completion when the saved counter is one.
- Actual local furnace/workbench/brewing windows through `interactBlock`. Bind
  `Game::offlineBlockInteraction` to this method and
  `Game::beforeOfflineContainerClose` to `beforeContainerClose`; the caller additionally
  gates these hooks on an active simulation. Controller preserves vanilla sneak
  activation rules, and online paths never call these hooks. Commit edited machine
  slots before saving or switching windows. Inventory and progress synchronize on
  each simulation tick.
- Native compressed NBT save/reload of player inventory/health/food, counters,
  world time/weather, independent random streams, furnace/brewing contents/timers,
  and dropped item/XP state. Save installation
  replaces the target only after successful writing. Invalid loads leave the
  current state intact. The simulation file accompanies terrain persistence;
  it does not silently modify a Java save or pretend to serialize unsupported
  subsystems. Its player inventory slots use the native 45-slot layout.
- Authoritative `breakBlock` with tool eligibility captured before durability,
  enchantments read after durability, ordinary/special drop rules and XP orb
  splitting. Bind `Game::offlineBlockBreak` only while simulation is active;
  **false means rejected**, with no Controller fallback. Complex paired/tile
  callbacks and nonempty machine inventories are explicitly rejected.
- Item and XP motion, collision, water acceleration, lava/fire/cactus handling,
  expiry, pickup delay, item merging, inventory insertion and XP pickup/leveling.
  A two-block spatial grid limits merge candidate searches. Native entities
  publish Object type 2 plus Slot metadata 10, or ExperienceOrb plus `xpValue`,
  to Game's render view. See [DROPS.md](DROPS.md) for exact coverage and limits.
- Player throwing, Q/Ctrl-Q, normal container outside clicks, slot drop mode and
  crafting-container overflow. Bind `offlineHeldItemDrop` to `dropHeldItem`,
  `offlineItemDrop` to `dropFromPlayer >= 0`, and `offlineItemDrops` to
  `dropFromPlayerBatch`. GUI drop failure restores cursor, slots, container and
  XP; a batch is required when one click produces multiple drops. Closing
  containers drops the cursor and applicable crafting/input slots while retaining
  furnace/brewing contents. Call `Game::dropHeldItem` from gameplay input; the
  online branch emits C07 without a local inventory decrement.

Local single/double/trapped/ender chest storage and dispenser/dropper/hopper
storage windows are also integrated, with vanilla slot ordering, basic lock
checks and chest lid actions. Storage edits persist in Anvil tile NBT; personal
EnderItems also persist in the player and native simulation state. Machine
contents and supported timers are imported/exported through standard tile NBT.

`saveState`/`loadState` expose the same structured NBT to the world-save layer.
`loadStateFile` starts the loaded simulation; do not call `start` afterward and
reset its counters. `coverage()` reports implemented and untouched subsystems.

## Validation

The native suite has 75 checks, including local machine windows, close commit
hooks, exactly one player tick per step, break/drop/pickup integration, rejected
authority callbacks, online isolation and exact entity save continuation.
`tools/verify_simulation.py` compares **506,185 rows exactly** with
original recompiled Java methods:

- 8,240 food ticks, using original FoodStats and actual EntityPlayer healing and
  damage for the fixture's unarmored, potion-free player.
- 2,400 original World weather ticks, including seeded transitions, clear
  weather and no-sky dimensions. WorldServer's time increments are reproduced in
  the headless adapter; the rest of WorldServer.tick is not exercised by it.
- 1,931 actual TileEntityFurnace ticks, including ignition, cooling, refueling,
  blocked output and wet sponge.
- 2,022 actual TileEntityBrewingStand ticks, including simultaneous bottle
  conversion, ingredient replacement, blocked inputs and countdown boundaries.
- 491,520 exhaustive PotionHelper cases: 15 ingredients × 32,768 metadata values.
- 72 original player/workbench/furnace/brewing close cases, including slot
  conservation, ordered item drops, exact throwing motion and player RNG.

The report and traces are in `build/simulation_reference`. Java is a development
oracle only. A native test build with the local toolchain is:

```powershell
$env:PATH = 'D:\Strawberry\c\bin;' + $env:PATH
& D:\Strawberry\c\bin\g++.exe -std=c++17 -O2 -ffp-contract=off -I include src/protocol.cpp src/world.cpp src/worldgen_noise.cpp src/game.cpp src/crafting.cpp src/controller.cpp src/simulation.cpp src/simulation_entities.cpp src/simulation_containers.cpp src/brewing.cpp src/drops.cpp src/drop_entities.cpp tests/simulation_tests.cpp -o tests/simulation_tests.exe -lcrypto -lz -lws2_32 -lwinhttp -ldnsapi -lstdc++fs
& .\tests\simulation_tests.exe
python tools/verify_simulation.py
```

## Untouched systems

Complete tile/container/paired-block destruction callbacks, scheduled block updates, fluid propagation, falling blocks,
redstone, mob AI/spawning,
combat/armor/potion damage, sleep and full server save/import behavior are not yet
ported in this stage. Existing client interaction prediction is not a substitute
for these server systems. The weather fixture isolates its random stream; runtime
weather and block loot now share a stream, but the
entire Java world's random-event ordering is not reproduced while those other
systems are absent. The tested food fixture does not establish full combat or
absorption/potion behavior.

`coverage().simpleBlockDrops`, `.itemEntities` and `.experience` describe the new
kernels. `.blockDrops` remains false because complete destruction callbacks are
not implemented. Item portal transfer and pressure/tripwire activation report
entity warning bits. Rain exposure in the integration uses terrain height and
base biome values; altitude/noise-dependent snow boundaries are not yet exact.
Single-entity motion and primitive merge tests do not establish every cross-chunk
entity iteration order or every possible shared mutable Java block-bounds history.

Terrain generation is supplied by the separate world-generation module.
Simulation must be integrated deliberately, with its missing systems visible
in development coverage, before the program can claim complete native survival
singleplayer.
