# Native protocol-47 play state

`mc::Game` connects the transport, world storage and 20 Hz player simulation.
The supplied `NetHandlerPlayClient`, `EntityPlayerSP`, `DataWatcher`,
`ModifiableAttributeInstance`, and protocol-47 packet serializers are the
reference. This is an incremental client port, not a completed port of all
Minecraft gameplay.

## State and movement

- Join Game initializes entity ID, dimension, game mode, difficulty and world
  flags; sends Client Settings and the explicit `NativeVoxel` brand.
- No player movement simulation or regular movement packets occur before the
  first player-position packet or while the current chunk is missing.
- Player Position and Look applies relative XYZ/yaw/pitch flags, clears velocity
  only on absolute axes, and immediately sends C06 using feet/bounding-box minY
  and **onGround=false**, as in the source.
- S08 does not reset `lastReportedPos*`, `lastReportedYaw/Pitch`, or the position
  refresh counter. These fields start at zero on player construction/respawn and
  are updated by the regular walking-packet algorithm only.
- Walking packets follow `EntityPlayerSP.onUpdateWalkingPlayer`: movement
  threshold squared `9.0E-4`, position refresh when the existing counter reaches
  20, rotation based on float differences, then C03/C04/C05/C06 selection.
  Sprint and sneak changes are sent before the movement packet in source order.
- Flight-toggle notifications retain every event from `Player::tick`, including
  more than one toggle in a single tick. They are encoded as C13 ability packets.
- S12 velocity replaces motion using signed short / 8000. S27 explosion adds its
  float motion values and removes listed blocks; block origin uses Java's cast
  toward zero. Arbitrary server knockback is not discarded.
- Movement attributes apply additions, base multipliers, then total multipliers.
  The local sprint UUID `662a6b8d-da3e-4c1c-8813-96ea6097278d` is excluded from
  the stored nonsprinting movement speed because `Player` applies it itself.
  Server ability walk speed is a separate capability, not a replacement for the
  movement attribute. Raw attribute/modifier data is retained for inspection.
- Jump Boost, Blindness and armor Depth Strider affect supported movement paths.
  Speed/Slowness are represented by the server-synchronized movement attribute,
  avoiding duplicate potion multipliers. Local metadata updates health and the
  sprint flag.
- S44 world-border actions update center, diameter, transition and warnings.
  Real elapsed time interpolates the diameter before physics; `World` applies
  source-based virtual stone collisions and `Player` preserves outside-border
  hysteresis. A replaced dimension starts with the default border.

`positionCorrections` counts S08 packets after initial spawn. It includes normal
server teleports and dimension/server-plugin repositioning; it is **not** an
anti-cheat violation count. Neither zero corrections nor a successful connection
proves anti-cheat compatibility.

## Packet coverage

Every clientbound protocol-47 ID from 0x00 through 0x49 has a bounded wire-shape
decoder. Parsing a packet does not imply that all of its gameplay or visual
effects have been ported. IDs with incomplete handling are counted in
`Game::unhandledPackets`; presentation and physics limits also remain for some
packets whose state is retained.

| IDs | Implemented state/behavior | Remaining scope |
| --- | --- | --- |
| 00 | Keep Alive in transport | None for packet echo |
| 01, 07, 08 | Join, respawn/dimension, position/rotation and acknowledgement | Full integrated-server lifecycle |
| 02, 03, 05, 06, 09, 1F | Chat, time, spawn, health/food, hotbar, experience | Complete GUI effects and client world-time/weather evolution |
| 04 | Remote equipment slots | Complete armor/item rendering |
| 0A, 0B, 1A | Bed/animation fields, item-use completion signal and reduced-debug status | Sleep behavior, hurt/death/status animations and sounds |
| 0C–19 | Spawn/player list association, metadata, fixed-point positions, relative movement, teleport, head look, velocity, destruction, collect removal | Actual mob/item/projectile models, entity classes/AI, pickup FX, full remote-entity simulation |
| 1B | Vehicle/leash references; ridden view/input packets | Vehicle physics, seat placement, mount/dismount simulation |
| 1C–1E | Metadata and local potion effects/removal | All entity metadata-driven behavior and potion visuals |
| 20 | Attribute/modifier parsing; movement/max health; retained raw attributes | Complete attribute effects for every entity and all equipment |
| 21–23, 26 | Chunk/bulk data, unload, multi/single block state changes | World lighting propagation and all client block logic |
| 24 | Bounded FIFO `Game::blockActions` with position, unsigned action/parameter, block ID and receipt tick | Tile-specific action simulation and rendering consume this view separately |
| 25 | Break-animation wire validation | Remote crack overlay |
| 27 | Explosion block deletion and exact received player motion | Explosion particles, fire and complete local effects |
| 28 | World effect wire validation | Auxiliary sound/particle effect mapping |
| 29 | Bounded named sound event queue, /8 coordinates and pitch/63.0f | Mixer/resource support is provided by the audio module |
| 2A | Particle arguments and fields validated | Particle system |
| 2B | Game-mode/rain/thunder state | Credits/demo overlays, elder guardian and other effects |
| 2C | Global-entity position/type | Lightning entity simulation/rendering |
| 2D–32 | Container state, slot/cursor revisions, transaction confirmation; native ordinary crafting and standard container prediction in Controller | Dynamic special recipes and integrated-server processing |
| 33 | Sign coordinates, four plain-text lines and original JSON in `Game::signs` | Sign editing and complete styled text behavior |
| 34, 36, 37 | Bounded map/statistics wire validation and sign-editor request validation | Map simulation, sign editor, achievements/statistics GUI |
| 35 | Complete tile NBT/action retention in `Game::tileEntities`, with count/byte caps and replacement/chunk cleanup | Tile-specific simulation; render modules interpret the retained data |
| 38 | Player-list add/update/remove, retained texture property/signature | Skin cache/downloads and rich tab display use separate modules |
| 39 | Player ability flags, fly/walk capability speeds | All game-mode permissions/UI |
| 3A | Tab completion fields validated | Completion UI |
| 3B–3E | Objectives, scores, display slots, teams and membership | Full scoreboard layout and team-specific visual behavior |
| 3F | Server brand and `MC|TrList` merchant offers; bounded unknown channels | Remaining plugin/Forge/custom channel handlers |
| 40, 41 | Disconnect reason and server difficulty | Difficulty-lock state from newer protocols is outside protocol 47 |
| 42, 43 | Combat-death state and camera ID | Full combat tracking and spectator camera rendering |
| 44 | Border state, elapsed-time interpolation and collision | Border visuals/warnings |
| 45, 47 | Title/subtitle/timing and player-list header/footer state | Complete presentation/animation |
| 46 | Compression changes in transport before next frame | None for framing state |
| 48 | Resource-pack offer retained; sends DECLINED | Download, hash verification, activation and reload of server packs |
| 49 | Entity NBT validated | Entity-NBT behavior |

Inventory item NBT is preserved byte-for-byte, including its root tag. The
reader bounds depth, node counts, arrays and packet lengths; it accepts 1.8.9 tag
types 0–11. Missing NBT remains TAG_End. Other tile/entity NBT is currently parsed
for stream correctness but is not installed into simulated tile/entity classes.

Chat retains original JSON and offers a plain-text presentation. The parser
handles arrays, `extra`, Unicode/surrogate pairs and common chat translation
patterns. It does not implement every language translation, font style, click
event, hover event, selector or dynamic score component.

The `digBlock`, `placeBlock`, `swingArm`, `attackEntity`, `useHeldItem` and
`setCreativeSlot` methods are packet primitives for an application/controller.
They do not by themselves reproduce all local `PlayerControllerMP` and
`ItemStack` behavior. In particular, attack success, local sprint cancellation,
attack motion scaling, target knockback, item-use prediction and inventory
transactions need corresponding simulation. Do not use transport availability
as a claim of PvP, crafting or anti-cheat parity.

`Controller` supplies source-based mining, common item use, local PvP motion,
ordinary 2x2/3x3 crafting, standard container shift/click rules, anvil calculation,
merchant matching/consumption and enchanting requests. The creative inventory
GUI uses its optional local-only prediction path before C10 updates, without
consuming C0E transaction IDs. Covered algorithms and remaining dynamic recipe,
item/entity/placement limits are documented in `CONTROLLER.md`.

## Tests and interpretation

`tests/game_tests.cpp` currently has 212 replay/packet checks. It validates
settings/brand, S08 relative flags/velocity/acknowledgement, unloaded-chunk
behavior, movement-packet selection and cadence, action ordering, chunk/bulk
decoding, dimension reset, velocity/explosion, movement attributes, abilities,
metadata/effects, item NBT/enchantments, container synchronization, transaction
rejection, entity interpolation, scoreboard/chat, interactions, sound scaling,
world-border state and truncated bodies.

The world module separately compares its covered physics cases with actual
compiled MCP Java classes. See the world/physics validation documentation for
the tested fixtures and known limits. Network tests independently exercise TCP,
zlib, RSA/AES and signed session hashes on loopback; see `PROTOCOL.md`.

The final controller/recipe oracle compares 14,937 Java/native rows with zero
mismatches, including 16 typed-NBT anvil cases and 21 advanced state snapshots.
These development checks use the supplied Java implementation as an oracle;
the native client itself does not start a JVM.

Neither test suite covers the full original game, all plugin behavior, every
custom anti-cheat, or every terrain/entity interaction. Real-server observations
must identify their duration, input, authentication mode and actual result.

Example standalone test build:

```powershell
$env:PATH = 'D:\Strawberry\c\bin;' + $env:PATH
& D:\Strawberry\c\bin\g++.exe -std=c++17 -O2 -ffp-contract=off -I include src/protocol.cpp src/world.cpp src/game.cpp src/crafting.cpp tests/game_tests.cpp -o tests/game_tests.exe -lcrypto -lz -lws2_32 -lwinhttp -ldnsapi
& .\tests\game_tests.exe
```

Compile movement code without `-ffast-math`; the relevant Java float/double
rounding and expression order must remain intact. `Game::tick` represents one
50 ms game tick and must not be called once per rendered frame.
