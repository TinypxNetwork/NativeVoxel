# NativeVoxel

**English** · [简体中文](README.zh-CN.md)

Minecraft 1.8.9, rebuilt as a native C++ client for Windows. NativeVoxel uses OpenGL 3.3 and speaks the original protocol 47. The game runs without a JVM.

The aim is to bring the feel and behavior of 1.8.9 to a native client, one system at a time. You can explore local worlds, build, use inventories and containers, and connect to servers. It is still a work in progress: multiplayer compatibility varies, and singleplayer is not yet a complete survival game.

## Build and run

You need Windows x64, an OpenGL 3.3 capable GPU, Python 3.10+, and a MinGW-w64 C++17 compiler with zlib and OpenSSL development libraries. `g++`, Python and the compiler's `objdump` must be available. The existing build has been tested with GCC 8.3; MSVC is not part of the verified build workflow.

For a new toolchain, [MSYS2](https://www.msys2.org/) provides the packages. In its **UCRT64** terminal:

~~~sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-openssl
~~~

Then open PowerShell. If you used the default MSYS2 location, add the compiler to that session's path:

~~~powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
git clone https://github.com/TinypxNetwork/NativeVoxel.git
cd NativeVoxel
python -m pip install -r requirements-dev.txt
powershell -ExecutionPolicy Bypass -File build.ps1
.\run.cmd
~~~

The first build downloads the original 1.8.9 client resources and Windows audio library from Mojang, checks their SHA-1 hashes and sizes, and converts the resources for the native renderer. Expect about **118 MiB of downloads**, plus space for generated assets. A separate Minecraft installation or MCP checkout is not needed for a normal build.

Downloads live in `.cache/minecraft/`; prepared resources go in `assets/`. Valid cached files are reused. If the connection drops, rerun the same command to finish the missing files. Python is used for setup and development; after setup, the game executable needs its DLLs and `assets/`, but no Python or Java. `run.cmd` also starts setup if the prepared assets are missing.

~~~powershell
# Recompile using assets you already prepared.
powershell -ExecutionPolicy Bypass -File build.ps1 -SkipAssets

# Prepare all assets separately, without compiling the game.
python tools/setup_assets.py

# Rebuild assets from the verified download cache, without network requests.
python tools/setup_assets.py --offline
~~~

Use `--cache D:/MinecraftCache` to choose a download directory, or `--mcp D:/MCP-919-main` to import an existing local MCP tree. `build.ps1` accepts the corresponding `-AssetCache`, `-McpRoot` and `-OfflineAssets` options. Resource packs can be added with `python tools/setup_assets.py --pack D:/packs/example.zip`, or selected in game. Packs should target 1.8.9 (`pack_format: 1`). See [asset setup](docs/ASSETS.md).

## Playing

Start with `run.cmd`, or launch from the repository root:

~~~powershell
# Generate a local world. Creative mode is the default.
.\bin\NativeVoxel.exe --vanilla --seed 189

# Open an existing Anvil world.
.\bin\NativeVoxel.exe --world "D:\Worlds\MyWorld"

# Connect to a server.
.\bin\NativeVoxel.exe --connect mc.tinypx.net --username YourName

# Try the rendering and physics demo.
.\bin\NativeVoxel.exe --demo
~~~

WASD moves, Space jumps, Shift sneaks, Ctrl sprints, E opens the inventory, and T opens chat. Use the mouse buttons to interact, number keys or the wheel to change slots, and Q to drop an item. F5 changes perspective, F2 takes a screenshot, F3 shows debug information, and F11 toggles fullscreen. Controls and sound settings can be changed in Options.

Online-mode servers require a valid Minecraft session. The client reads `MC_ACCESS_TOKEN` and `MC_UUID` from the local process environment, together with the matching player name; it does not include a Microsoft sign-in screen. Server-side registration or login is handled by the server.

## What works so far

- **Rendering:** chunk meshes, lighting and ambient occlusion, animated textures, first- and third-person views, item and entity models, equipment, particles, and resource pack reloads.
- **Multiplayer:** protocol 47 login, encryption and compression, world updates, entities, chat, inventories and containers. Plugin and anti-cheat compatibility still needs testing on individual servers.
- **Local worlds:** Overworld, Nether and End terrain generation, Anvil loading, movement and collision, a 20 Hz simulation, crafting, storage, furnaces, brewing, dropped items and several projectile types.
- **Interface and audio:** menus, configurable controls, Unicode fonts, formatted chat, tooltips, positional sound and music.

The larger gaps are mob spawning and AI, redstone and fluid propagation, complete world decoration and structures in playable worlds, portals, and the remaining survival interactions. Some rendering details also differ from vanilla. Having all block and item IDs in the registry does not mean every behavior is implemented.

**Saves are snapshots.** NativeVoxel writes a new directory under `saves/` and leaves an imported world unchanged. A snapshot includes loaded chunks, not a backup of the entire imported world. Some simulation state uses `native_simulation.dat` alongside standard NBT. See [world I/O](docs/WORLD_IO.md) and [local sessions](docs/LOCAL_SESSION.md) before relying on save interchange.

## Development

The code is in `src/` and `include/mc/`. `tools/` contains asset preparation and reference exporters; `tests/` contains native tests and Java comparison harnesses. Small generated tables and numerical test fixtures are checked in so ordinary builds work without MCP. Downloaded game files, textures, sound files, local settings, credentials, saves, logs and build outputs are excluded from Git.

~~~powershell
# Self-contained downloader tests; no network or game assets needed.
python tests/asset_download_tests.py

# Native, asset and protocol checks, after building.
powershell -ExecutionPolicy Bypass -File test.ps1

# Also exercise GPU rendering and decode every sound.
powershell -ExecutionPolicy Bypass -File test.ps1 -Graphics -AllAudio
~~~

`test.ps1 -Reference` additionally compares against original Java behavior and requires a local MCP tree and JDK. The banner pixel reference is generated locally because it contains original texture pixels; ordinary tests report that comparison as skipped when the fixture is absent. Keep `-ffp-contract=off -fno-fast-math`: changing floating-point behavior can change movement and world generation.

More detail: [protocol](docs/PROTOCOL.md), [world generation](docs/WORLDGEN.md), [rendering assets](docs/RENDER_ASSETS.md), [audio](docs/AUDIO.md), and [performance](docs/PERFORMANCE.md). These documents include implementation notes and known limits, not a promise of full vanilla parity.

Minecraft and its original assets belong to Mojang/Microsoft and their respective authors. NativeVoxel is an independent project, not an official Minecraft product. Original game resources are downloaded during setup rather than shipped in this repository. See [third-party notices](THIRD_PARTY_NOTICES.md).

## A small plug

**Ad · [TinyPx API](https://ai.tinypx.top)** — an AI API relay service. If you're building an AI app or just need an API for a side project, take a look: **https://ai.tinypx.top**.
