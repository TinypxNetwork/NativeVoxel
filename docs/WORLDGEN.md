# Native 1.8.9 generation

The generator is a native C++ port of the supplied MCP sources. It does not call
the Java runtime. The original Java classes are used by development-only
differential tests. `World::generatePreview` remains a separate rendering fixture
and is not used as a fallback for unsupported generation stages.

## Available stages

`mc::worldgen::Generator` supports signed 64-bit seeds, Default, Large Biomes and
Amplified world types. Generation stages are explicit:

| Stage | Overworld | Nether | End |
| --- | --- | --- | --- |
| Terrain density and interpolation | Implemented | Implemented | Implemented |
| Biome surface/bedrock | Implemented, all 61 biomes | Implemented | Original 1.8.9 surface pass |
| Caves/ravines | Implemented | Nether caves implemented | No carvers in the original generator |
| Structures | Pending | Fortress pending | No generation-time structures in 1.8.9 |
| Population | Lakes, dungeons and all biome decorators ported; full orchestration/runtime effects pending | Features ported; provider orchestration/fortress pending | Block decoration and entity creation records implemented |

`generateChunk` replaces only the target column. Unavailable structures return an
error before world mutation. `Structures` with `mapFeatures=false` intentionally
produces the completed carved stage. The End is the central island from 1.8.9;
later-version outer islands and End cities are not substituted.

The default settings select `Carved`. A generated column preserves block states,
biome IDs and initial client-visible vertical skylight. This is not a complete
integrated-server simulation or its runtime light-propagation system.

## Population and entity ownership

The explicit `populate(world, x, z, worldRandom, output, error)` overload currently
implements End decoration. Supply the target column and its three positive-axis
neighbors. Supply the running world's `JavaRandom`; the original End provider
uses `World.rand` without reseeding it from the world or chunk seed. Generation
order and earlier world-random consumption can therefore change decoration.

The original End decorator consumes the ordinary ore pass even when no stone is
available, attempts a random obsidian spike, and requests the initial dragon in
chunk `(0, 0)`. `PopulationOutput::entities` records the original entity IDs,
positions and rotations. The owning server must construct and simulate these
entities, including each entity's separate RNG, health and data watcher. These
records are not full saved-entity NBT. No entity request is silently discarded.

The overload without a world RNG and output returns an explicit error. Overworld
and Nether population currently return an explicit error as well. Complete
provider orchestration, structures, mob population and runtime side effects must
be connected before either dimension's full population can be enabled.

`decorateBiome` in `mc/worldgen_decoration.hpp` ports all 61 registered biome
decoration methods and mutation delegation. It accepts the original light query
and immediate spring-tick callback. `springs=false` corresponds to disabling the
original decorator's `generateLakes` field; enabling springs without a callback
returns an error before editing the world. Tests capture the exact callback
positions and Random state; they do not substitute a fake fluid simulation.

The lake generator accepts current sky-light, biome-top and freezing queries.
It reproduces the full shape, rejection checks, grass/mycelium restoration and
lava stone shell. Its supplied world-query interface is kept separate from the
server's light propagation and fluid tick queues.

Dungeon generation includes chest-facing corrections and complete chest/spawner
NBT. `PopulationOutput::blockEntities` is an ordered update stream; a TAG_End
payload removes an older tile at that position. Apply the full compounds to the
owning world/save, including zero-count stacks retained by original NBT rules.
`worldio::SaveInfo::blockEntityUpdates` accepts this stream for Anvil export.
Set its optional `generation` metadata explicitly for native terrain; this keeps
the selected vanilla world type and marks incomplete population correctly.
Live inventory-drop entities produced when an old chest is destroyed belong to
the server runtime and are outside the geometry/NBT kernel's validation scope.
The dungeon oracle uses real loaded Java chunks and server chest callbacks.

`makeEnchantedBook`, `makeDungeonChest` and `makeDungeonSpawner` are independently
verified NBT producers. Book enchanting uses the existing crafting registry's
weights/compatibility plus exported original enchantability bounds. It preserves
Java 8+ HashMap bucket/insertion ordering when selecting weighted enchantments.
The fixed level-30 book is generated before the chest's eight rolls, even when no
book is ultimately selected; later rolls can overwrite earlier inventory slots.

Independent feature ports live in `src/worldgen_features.*`: default ore veins,
End spikes, flowers, tall grass, dead bushes, double plants, cactus, reeds,
pumpkins, melons, water lilies, mushrooms, fire, both original glowstone variants,
clay/sand/gravel sediment, mossy boulders, ice spikes, ice paths, desert wells and
vines. The mushroom generator accepts an
exact light callback; callers must supply current light when using a changing
server world. These helpers do not advance fluid, entity or lighting queues.

`src/worldgen_trees.cpp` implements the original oak, big oak, birch (including
tall birch), pine, spruce, swamp, shrub, acacia, dark oak, mega jungle, mega pine,
mega spruce and giant mushroom generators. Mega pine/spruce post-generation
podzol patches are separate `decorateTreeBase` calls, matching the original tree
hook. Big-oak height limits and default giant-mushroom type choices persist on
their original generator instances, even after failed attempts; keep the supplied
state objects per original generator instance when integrating a biome decorator.

## Spawn selection

`findSpawn()` runs the original WorldServer selection algorithm against columns
generated through the configured stage. Its Y value is the stored world-spawn
field, not the eventual player's feet position. It does not populate neighbors.
Full server spawning should use the predicate overload and provide the actual
provider's `canCoordinateBeSpawn` query, including loaded/populated terrain.

The selection preserves the original coarse-biome sampling quirk: the counter
increments only when the candidate is replaced. It also preserves the 1,000-move
fallback, Nether's `(0,64,0)` stored spawn and End's `(0,50,0)` stored spawn.
The End portal arrival coordinate `(100,50,0)` is a different provider operation.

## Reference verification

The scripts compile the supplied Java sources and compare their results with
the C++ implementation. Reports are written to `build/worldgen_reference/`.

| Verification | Exact comparisons completed |
| --- | --- |
| Java Random and generation noise | 64,981 rows for generation-used operations |
| Complete GenLayer pipeline | 756 regions / 754,488 biome IDs |
| Overworld Terrain, Surface and Carved | 105 columns / 6,881,280 blocks per stage |
| All biome surface rules, including mutations | 10,675 columns / 2,732,800 blocks; post-call RNG and temperature bits |
| All dimensions Carved | 231 columns / 15,138,816 blocks, plus light and biome bytes |
| Nether Terrain and Surface; End Terrain | 63 columns / 4,128,768 blocks per stage |
| Biome search and spawn selection | 420 biome searches / 119 selections / 28,272 ground queries |
| Default ores and End decoration | 287 operations / 1,148 columns / 75,235,328 blocks; 28 dragon and 22 crystal records; post-call RNG |
| Independent vegetation and feature rules | 1,940 operations / 108,572 changed blocks; return values, entity placement and post-call RNG |
| Tree and giant-mushroom rules | 3,403 operations / 295,675 changed blocks; persistent generator state, return values and post-call RNG |
| All biome decoration overrides | 445 calls / 61 biomes / 937,910 changed blocks; 489 immediate spring-tick boundaries; 8 entity requests |
| Water and lava lakes | 448 attempts / 60,186 changed blocks; light/freezing queries and post-call RNG |
| Books and dungeon tile NBT | 10,566 results: 8,262 books, 2,048 chests, 256 spawners; exact typed values, list/slot order and RNG |
| Dungeon rooms and repeated generation | 664 fixtures / 1,048 attempts / 166,984 changed blocks / 1,721 complete tile snapshots; post-call RNG |
| Nether provider feature pass without fortress starts | 234 passes / 22,400 changed blocks / 1,670 immediate lava-tick boundaries; post-call RNG |

Typical commands:

```text
python tools/verify_worldgen_noise.py
python tools/verify_worldgen_biomes.py
python tools/verify_worldgen_surfaces.py
python tools/verify_worldgen_chunks.py --stage carved --dimension all
python tools/verify_worldgen_spawn.py
python tools/verify_worldgen_population.py
python tools/verify_worldgen_features.py
python tools/verify_worldgen_trees.py
python tools/verify_worldgen_decoration.py
python tools/verify_worldgen_lakes.py
python tools/verify_worldgen_loot.py
python tools/verify_worldgen_dungeons.py
python tools/verify_worldgen_nether_population.py
```

The tested seeds include both signed-64-bit extremes; terrain fixtures include
coordinates near the vanilla world limit. A passing corpus demonstrates the
tested behavior, not untested whole-game parity or an anti-cheat guarantee.

## Compatibility details

- Compile with `-ffp-contract=off -fno-fast-math`; preserve intermediate float
  precision and explicit ordering of RNG calls.
- `JavaRandom::nextGaussian` uses platform `log`/`sqrt`. Optional Gaussian probes
  observed 18 least-bit differences from Java StrictMath. The reference world
  generation package does not call this method; validated generation uses the
  exact non-Gaussian operations.
- Improved noise retains the original 2-D specialization and Y-cell gradient
  cache. Simplex scalar and region paths have different coordinate-offset rules.
- The biome graph preserves GenLayerHills' original secondary-parent seed
  initialization behavior. Large Biomes enlarges biome zooms but keeps four
  river zooms.
- Mesa's per-biome cache is shared like the original singleton biomes. Pillar
  noise initialization uses the previously stored seed; band-offset noise
  samples X twice. The cache is protected by a mutex, but reproducing history
  still requires reproducing generation order.
- Surface local X/Z conventions, cave versus ravine lava IDs, grass-restoration
  metadata differences and Nether roof RNG short-circuit behavior are retained.
- Biome decoration keeps process-wide mutable counters and tree/mushroom state.
  The original Jungle's extra vine attempts start at Y128, where the vine
  generator's Y<128 loop is empty; their coordinate RNG draws are still consumed.
- No-sky dimensions expose zero sky light. The Java fixture normalizes the
  missing-section raw sky-light sentinel to its client-visible value.

Current limitations include customized/superflat/debug world generators,
structure persistence, full Overworld/Nether population, complete server-world
light/fluid/tick semantics and live generated entity/block-entity simulation.

The application now uses the Carved stage for local world creation and missing
native-save columns through [LocalSession](LOCAL_SESSION.md). The feature and
population kernels above are separate from that game entry point.

`populateNetherFeatures` requires the target and three positive-axis population
columns plus west and north neighbors for queries. Enclosed lava springs sample
local X/Z zero and inspect the adjacent block at -1. The reference adapter has
an unbounded procedural world; its native counterpart therefore supplies these
query columns too. Missing neighbors are rejected before changing blocks or RNG.
The oracle checks that query-only columns remain unmodified. Immediate fluid
ticks are recorded at their original call boundaries, not replaced with guessed
fluid propagation.
