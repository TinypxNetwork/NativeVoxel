# Native player controller

`mc::Controller` implements interaction state between UI input and the native
world/game. It is based on the supplied `Minecraft.clickMouse`,
`rightClickMouse`, `sendClickBlockToController`, `PlayerControllerMP`,
`EntityPlayer`, `ItemStack`, `InventoryPlayer`, the concrete container classes,
`CraftingManager`, `MerchantRecipeList` and anvil/enchantment source.

Call `Controller::tick(game, input)` once per 50 ms game tick, before
`Game::tick`. Transfer `controller.usingItem()` into `Input::usingItem` for the
same physics tick. Mouse press edges must be retained until a tick consumes
them. `ControllerTarget` carries the ray-hit block/face and local hit offsets,
or an entity ID and entity-relative hit vector. Ray casting, screen input and
reach selection remain application responsibilities.

Implemented interaction behavior:

- Float mining-progress accumulation from actual registered block hardness and
  tool strength/harvest predicates, including the harvest penalty, Efficiency,
  Haste, Mining Fatigue, eye-level water/Aqua Affinity, and airborne penalties.
  Bedrock/unbreakable blocks produce zero damage; zero-hardness blocks follow
  Java's floating-point immediate-break behavior.
- START/ABORT/STOP digging packets; item/target changes; source five-tick hit
  delay; creative START-only breaking; creative sword restriction; fire
  extinguishing; common paired-block removal; block breaking/step sounds and
  ordinary tool durability.
- Source mouse-click/held-mining order, miss cooldown, arm swings, and four-tick
  held-use cadence. ClientSettings/hotbar/abilities/movement remain in `Game`.
- Right-click activation for common storage/UI blocks, doors/trapdoors, gates,
  buttons/levers, cake and jukebox cases. Common ItemBlock placement uses block
  replacement, local player collision and world-border checks; slab merging,
  selected directional metadata and stack consumption are predicted.
- Item-use duration/slowdown for swords, food, drinkable potions, milk and bows.
  The remote client's item-use flag continues after its countdown reaches zero;
  it waits for the server's Entity Status 9 or a changed held-item reference.
  The controller does not invent a local food completion merely after 32 ticks.
  Server slot revisions prevent double consumption after authoritative updates.
- Source arrow/creative prerequisites, bow release charge threshold, local
  arrow/durability consumption, food values including metadata-specific fish,
  soup bowl/potion bottle/milk bucket outcomes, ordinary armor equip, and common
  thrown consumables.
- Player-versus-player local sprint cancellation and motion scaling by 0.6,
  knockback-enchantment contribution and tool damage. The implementation honors
  the distinction between `EntityOtherPlayerMP.attackEntityFrom` returning true
  and ordinary remote-world `EntityLivingBase` mob attacks returning false.
  It does not apply PvP slowdown indiscriminately to every entity.
- Player inventory and generic storage-container left/right click, shift
  transfer, number-key exchange, creative clone, dropping, drag distribution,
  and double-click collection. Armor constraints, stack limits, metadata and
  semantic compound-NBT comparison apply. Each C0E carries the original
  `slotClick` return stack and a wrapping per-container transaction ID.
  Number-key displacement first merges in vanilla hotbar-first inventory order.
  `Game` processes rejected transaction confirmations and server resynchronization.
- Native 2x2 and 3x3 crafting from 365 exported shaped/shapeless recipes, plus
  ordinary tool repair. Matching includes mirrored/offset patterns and metadata
  wildcard 32767. Vanilla ordinary recipes ignore ingredient NBT; output-NBT
  copying and container-item remainders follow their actual recipe rules.
  Result pickup consumes ingredients, and shift crafting repeats until input or
  inventory capacity stops it. Milk buckets return their empty buckets.
- Slot validity, stack limits and shift-click routes for workbench, furnace,
  brewing stand, enchanting table, beacon, horse/donkey, anvil and merchant
  containers. Furnace recipes/fuel and potion ingredient predicates come from
  actual Java getters. The client waits for server-driven processing and
  enchanting outcomes, as the remote Java client does.
- Anvil material repair, durability combination, enchantment compatibility and
  level/cost calculation, renaming, prior-work penalties and XP-gated pickup.
  Native NBT serialization preserves typed values and Java modified UTF-8.
  The GUI normalizes unchanged default display names before `renameItem`.
- Merchant offers received through `MC|TrList`, metadata and compound-NBT subset
  price matching, selected trades, swapped inputs, usage limits and repeated
  shift trading. Trade selection and renaming use the original plugin channels;
  enchanting choices use C11 after the original client prerequisites.

`clickSlot(game, slot, button, mode, notifyServer)` defaults to
`notifyServer=true`, preserving the existing C0E transaction behavior. Passing
false applies the same local prediction and skips both C0E and transaction-ID
allocation. The creative inventory GUI uses this before sending its C10 slot
updates; the controller does not send those creative updates automatically.

Registry values are generated by `tools/export_registry.py`, which recompiles
the original Java item/block sources and calls their getters. The checked-in
native header preserves 198 registered blocks, 337 registered items, deduplicated
tool rules and metadata-dependent food values. The runtime does not invoke Java.
`tools/export_crafting.py` separately exports the ordinary recipes, 26 smelting
rules, item remainders, repair materials and enchantment applicability/compatibility.
Generated data is checked into native headers.

## Validation

`tests/controller_tests.cpp` has 63 focused packet/state checks for mining timer
boundaries, movement-related digging modifiers, item use, attack slowdown,
placement/slab merging and inventory transactions, including the local-prediction
overload. `tests/crafting_tests.cpp` adds 20 recipe and result-consumption checks.

`tools/verify_controller.py` executes original recompiled MCP methods as an
independent development oracle. Its final checkpoint compares **14,937 rows
exactly**:

- 10,835 digging-hardness cases over all 197 non-air block IDs, eleven hand/tool
  choices and five enchantment/potion/water/ground combinations. Comparisons use
  exact float bit patterns.
- 2,308 recipe cases covering ordinary recipes, offsets, mirrors and remainders.
- 1,757 inventory clicks over player, storage, workbench, furnace, brewing,
  enchanting, beacon, horse/donkey, anvil and merchant containers. Every return
  stack, cursor and container slot is compared after each action.
- 16 anvil cases compare costs, material use and complete typed NBT, including
  enchantment combination/conflict, renaming, Unicode and prior-work penalties.
- 21 additional state snapshots compare anvil pickup/XP, merchant consumption
  and enchanting prerequisites. They include the original anvil material-cost
  field persisting after its first input becomes empty.

The report and both traces are written under `build/controller_reference`.
The Java world/player fixture from `tools/verify_physics.py` is a development
prerequisite; neither it nor a JDK is needed to run the native game.
Run `tools/export_crafting.py` first when its development JSON cache under
`build/crafting_export` is absent. Normal native builds use the checked-in header.

```powershell
$env:PATH = 'D:\Strawberry\c\bin;' + $env:PATH
& D:\Strawberry\c\bin\g++.exe -std=c++17 -O2 -ffp-contract=off -I include src/protocol.cpp src/world.cpp src/game.cpp src/crafting.cpp src/controller.cpp tests/controller_tests.cpp -o tests/controller_tests.exe -lcrypto -lz -lws2_32 -lwinhttp -ldnsapi
& .\tests\controller_tests.exe
python tools/verify_controller.py
```

## Remaining behavior

The controller is not a complete `PlayerControllerMP`/item/block port. Seven
dynamic recipe classes remain unimplemented: armor dyeing, fireworks, banner
pattern addition/duplication, written-book cloning, map cloning and map
extension. These are separate from the working ordinary crafting registry.
Controller itself supplies no integrated server or offline enchantment generator.
The separate [Simulation](SIMULATION.md) now implements furnace/brewing processing
and a validated authority hook for basic offline block harvesting. An offline
enchantment generator is not supplied by the client container implementation.

Special block placement hooks, all support/adjacency rules, entity collisions
during placement, BlockEntityTag installation, special tools/items, combat
damage/effects for every entity, and entity-specific right-click outcomes remain
incomplete. Common placement prediction is not covered by the exhaustive mining
or inventory oracle. Some unsupported right-click cases still send the standard
server request but cannot fully predict its local result.

Durability uses the Java 48-bit random algorithm for covered Unbreaking paths,
but the entire original entity/world random-event sequence is not reproduced.
Complete client-side block neighbor callbacks, redstone and scheduled updates
are outside this module. Sounds are emitted as native audio events; exact
cosmetic random pitch/particle sequences are not reproduced everywhere.

`unsupportedInteractions` and `status` expose unsupported controller actions.
The verified cases substantiate those specific algorithms; they do not establish
complete PvP, crafting, item, world, anti-cheat or server-plugin parity.
