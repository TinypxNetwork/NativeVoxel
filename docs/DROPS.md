# Native block loot and dropped entities

`mc::drops` ports the block/item kernels. `mc::drop_entities` owns item and XP
motion, merging and collection. `Simulation` connects them to terrain, inventory,
the render view and native NBT persistence. Production code runs entirely in C++.

## Block rules and ordering

The generated `drops_data.hpp` contains original getters for 3,039 accepted
legacy block/metadata inputs. Invalid metadata is not guessed. Banner/skull,
moving-piston and silverfish normal-drop paths remain explicitly unsupported
(76 metadata inputs). Tile inventories and paired-block callbacks require more
than a drop table and are separately gated by Simulation.

Implemented kernels include stone/variant metadata, ordinary blocks, ore Fortune
multipliers, Silk Touch, lapis/redstone, gravel, glowstone, sea lanterns, melon,
huge mushrooms, crop maturity/extra seeds, poisonous potatoes, cocoa, nether wart,
stems, saplings/apples, shears, snow layers and ice replacement. These preserve
the source's random draw order, including chance draws at chance one and the
three block-offset draws for each spawned entity. `doTileDrops=false` skips item
spawn offsets but leaves the original ore XP behavior intact.

Harvest eligibility uses the tool before damage. The item then receives damage,
including Unbreaking and break-particle random draws. Harvest reads the remaining
tool afterward, so a tool broken on its last use loses its Silk/Fortune effects
for that drop. Shears-specific harvest and snow use their original exhaustion
rules. The source's snow-layer `LAYERS + 1` behavior is retained.

World loot/weather, player durability, Math-style construction motion and entity
seed allocation use separate random streams. Each dropped entity has its own
Java-style RNG. Original Java entity construction seeds are not determined by
the world seed alone; native explicit seeds provide reproducible native saves.
The oracle compares explicit initial entity state, not Java's process-global
time-seeded Math.random values.

## Motion, collection and persistence

Item/XP kernels retain original gravity and float/double conversions, Y/X/Z
collision order, small bounding boxes, block escape, web and soul-sand motion,
slime landing, water acceleration, fire/lava/cactus damage, age and pickup-delay
sentinels. XP orbs retain target-refresh timing, attraction, cooldown and level
capacity formulas. Items merge at the original point before drag/age advancement,
using item identity, metadata and typed NBT, stack limits, minimum age and maximum
pickup delay. The larger stack survives, with original equal-size behavior.

Inventory insertion follows hotbar-first Java inventory order. One non-obvious
source behavior is preserved: a partially inserted stack can shrink while the
final insertion result is false because no further slot has room. Pickup owner
protection expires during the last 200 ticks of item life.

Player throwing uses original eye position, 40-tick pickup delay, forward/scatter
motion, thrower identity and player RNG. `Game::dropHeldItem` sends C07 action 4
for Q and action 3 for Ctrl-Q online, with origin/down fields and no premature
inventory decrement; offline it calls `offlineHeldItemDrop`. The GUI's
`offlineItemDrop`/`offlineItemDrops` hooks stage normal outside clicks, slot drop
mode and multiple crafting-container remainders. A rejected batch cannot partly
spawn items and then leave the original stacks available. The click restores its
inventory, cursor, container and XP on rejection. Close callbacks retain machine
inventories and throw cursor/crafting input items in original order.

The UI binds these only for an active native simulation:

```cpp
game.offlineBlockBreak = [&](int x,int y,int z,int face) {
    return simulation.breakBlock(game,x,y,z,face);
};
game.offlineHeldItemDrop = [&](bool all) {
    return simulation.dropHeldItem(game,all);
};
game.offlineItemDrop = [&](const mc::ItemStack& stack,bool trace) {
    return simulation.dropFromPlayer(game,stack,trace)>=0;
};
game.offlineItemDrops = [&](const std::vector<mc::ItemStack>& stacks,bool trace) {
    return simulation.dropFromPlayerBatch(game,stacks,trace);
};
game.beforeOfflineContainerClose = [&] { simulation.beforeContainerClose(game); };
```

Remove these bindings before leaving native authority. The normal online paths
also explicitly skip them, preserving the existing client packet/prediction flow.

Simulation publishes item stacks using `Game::Entity.metadata[10]` (type 5,
complete Slot bytes) and XP through `Entity::xpValue`. The S11 network decoder also
preserves that explicit XP field. The legacy `Entity::type` value is retained.
Entity position/motion/bounds, counters, stack/NBT, owner, RNG and XP target state
persist in the optional `DroppedEntities` list of native simulation save version
1; older native saves without it remain valid. This is native continuation data,
not a claim to import all original Java tile/entity saves.

## Verification

| Suite | Result |
| --- | --- |
| Drop kernel unit tests | 30 checks |
| Dropped entity unit tests | 15 checks |
| Simulation integration/persistence | 75 checks |
| Original block/item methods | 34,098 exact rows |
| Original item/XP motion | 3,446 exact ticks |
| Original inventory insertion | 800 exact cases |
| Original XP arithmetic | 1,152 exact cases |
| Original merge, including typed NBT | 384 exact cases |
| Original player throw motion/RNG | 11,520 exact cases |
| Original container closing | 72 exact cases |

`tools/export_drops.py` recompiles original block sources and audits overridden
drop/harvest methods before generating static data. `tools/verify_drops.py` checks
spawned stacks, metadata, position bits, XP split sequence and world/player RNG
continuation. `tools/verify_drop_entities.py` calls original EntityItem/EntityXPOrb
updates, movement/collision, InventoryPlayer insertion, XP arithmetic and private
item-combine methods. It compares position, motion, collision boxes, counters,
flags and internal entity RNG state exactly. Java is only a development oracle.

Reports, source hashes and traces live in `build/drops_reference` and
`build/drop_entity_reference`. The original 14,937-row Controller oracle remains
passing after the authority hooks, and Game has 212 packet/state checks.

## Remaining boundaries

- `Game::offlineBlockBreak` is authoritative when installed: false rejects;
  Controller never silently falls back. Install only for an active Simulation
  and clear it before online/replay sessions. Online packet behavior is unchanged.
- Nonempty furnaces/brewing stands are rejected until `InventoryHelper` container
  spill behavior is ported. Beds, doors, double plants, other tile inventories,
  skull/banner NBT, moving pistons and silverfish lifecycle remain gated.
- Log/leaf removal marks nearby leaves for decay. Scheduled/random decay,
  fluid propagation, general neighbor notifications, redstone and falling blocks
  are separate unfinished server systems. Ice replacement creates flowing water
  without claiming that fluid propagation is complete.
- Portal transfer, pressure/tripwire callbacks and moving-piston interactions
  report explicit entity warning bits. Rain exposure currently uses terrain
  height/base biome values; high-altitude snow boundaries need further work.
- Primitive merge and representative compound-shape fixtures do not prove every
  cross-chunk entity traversal order or every history of shared mutable Java block
  bounds. Mob/vehicle collision, furnace-output XP, creative C10 GUI drop integration, full combat,
  enchantment XP seeds/statistics, achievements and all particle/audio effects
  are not established by these tests.

These results establish the tested kernels and bridge. They do not establish a
complete integrated server or an anti-cheat compatibility guarantee.
