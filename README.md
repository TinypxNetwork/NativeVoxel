# NativeVoxel

**English** · [简体中文](README.zh-CN.md)

Minecraft 1.8.9, rebuilt as a native C++ client for Windows. NativeVoxel uses OpenGL 3.3 and speaks the original protocol 47. The game runs without a JVM.

The aim is to bring the feel and behavior of 1.8.9 to a native client, one system at a time. You can explore local worlds, build, use inventories and containers, and connect to servers. It is still a work in progress: multiplayer compatibility varies, and singleplayer is not yet a complete survival game.

## Source layout

- `src/` and `include/mc/`: the C++ client and its tables.
- `tools/`: Python source for downloading and converting vanilla resources.
- `tools/reference/`: the text tables needed for resource conversion.
- `third_party/`: the Vorbis decoder, its provenance, and license notices.

This repository contains the source and its required data. Build helpers, test harnesses, development reports, game assets and compiled binaries are kept out of the published tree.

## Prepare the resources

You need Windows x64, an OpenGL 3.3 capable GPU, Python 3.10+, and a MinGW-w64 C++17 compiler with zlib and OpenSSL development libraries. The existing client has been built with GCC 8.3.

For a new compiler installation, [MSYS2](https://www.msys2.org/) provides these packages in its **UCRT64** terminal:

~~~sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-openssl
~~~

Then open PowerShell, with Python and your compiler available on `PATH`. For a default MSYS2 installation, add `C:\msys64\ucrt64\bin` to that session's `PATH`.

~~~powershell
git clone https://github.com/TinypxNetwork/NativeVoxel.git
cd NativeVoxel
python -m pip install "Pillow>=10" "numpy>=1.24"
python tools/setup_assets.py
~~~

The setup command automatically downloads about **118 MiB** of original 1.8.9 resources and the Windows audio library from Mojang, verifies their SHA-1 hashes and sizes, and converts them for the native renderer. Downloads are cached in `.cache/minecraft/`; the game reads the prepared files in `assets/`. Neither an existing Minecraft installation nor an MCP checkout is required.

Valid cached files are reused. If a download fails, rerun the command to finish the missing files. Use `--offline` to prepare resources from a verified cache without network requests, `--cache D:/MinecraftCache` to choose the cache directory, or `--mcp D:/MCP-919-main` to import an existing local MCP tree.

For a resource pack, run `python tools/setup_assets.py --pack D:/packs/example.zip`, or select it in game. Packs should target 1.8.9 (`pack_format: 1`).

## Compile and run

Compile directly from PowerShell in the repository root:

~~~powershell
$sources = (Get-ChildItem src -Filter *.cpp).FullName
g++ -std=c++17 -O3 -DNDEBUG -ffp-contract=off -fno-fast-math `
    -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I include $sources `
    -o NativeVoxel.exe -lopengl32 -lgdi32 -luser32 -lshell32 -lwinmm `
    -lws2_32 -lwinhttp -ldnsapi -lcrypto -lz -lole32 -lwindowscodecs -luuid `
    -lstdc++fs -static-libgcc -static-libstdc++

.\NativeVoxel.exe
~~~

Keep your toolchain's runtime DLL directory on `PATH` when launching, or place the required DLLs beside the executable. Run from the repository root so the client can find `assets/`. Python is only needed to prepare resources; the game itself needs neither Python nor Java. Keep the floating-point flags shown above, since changing them can affect movement and world generation.

~~~powershell
# Generate a local world. Creative mode is the default.
.\NativeVoxel.exe --vanilla --seed 189

# Open an existing Anvil world.
.\NativeVoxel.exe --world "D:\Worlds\MyWorld"

# Connect to a server.
.\NativeVoxel.exe --connect mc.tinypx.net --username YourName

# Try the rendering and physics demo.
.\NativeVoxel.exe --demo
~~~

WASD moves, Space jumps, Shift sneaks, Ctrl sprints, E opens the inventory, and T opens chat. Use the mouse buttons to interact, number keys or the wheel to change slots, and Q to drop an item. F5 changes perspective, F2 takes a screenshot, F3 shows debug information, and F11 toggles fullscreen. Controls and sound settings can be changed in Options.

Online-mode servers require a valid Minecraft session. The client reads `MC_ACCESS_TOKEN` and `MC_UUID` from the local process environment, together with the matching player name; it does not include a Microsoft sign-in screen. Server-side registration or login is handled by the server.

## What works so far

- **Rendering:** chunk meshes, lighting and ambient occlusion, animated textures, first- and third-person views, item and entity models, equipment, particles, and resource pack reloads.
- **Multiplayer:** protocol 47 login, encryption and compression, world updates, entities, chat, inventories and containers. Plugin and anti-cheat compatibility still needs testing on individual servers.
- **Local worlds:** Overworld, Nether and End terrain generation, Anvil loading, movement and collision, a 20 Hz simulation, crafting, storage, furnaces, brewing, dropped items and several projectile types.
- **Interface and audio:** menus, configurable controls, Unicode fonts, formatted chat, tooltips, positional sound and music.

The larger gaps are mob spawning and AI, redstone and fluid propagation, complete world decoration and structures in playable worlds, portals, and the remaining survival interactions. Some rendering details also differ from vanilla. Having all block and item IDs in the registry does not mean every behavior is implemented.

**Saves are snapshots.** NativeVoxel writes a new directory under `saves/` and leaves an imported world unchanged. A snapshot includes loaded chunks, not a backup of the entire imported world. Some simulation state uses `native_simulation.dat` alongside standard NBT, so save interchange is not yet complete.

Minecraft and its original assets belong to Mojang/Microsoft and their respective authors. NativeVoxel is an independent project, not an official Minecraft product. Original game resources are downloaded during setup rather than shipped in this repository. See [third-party notices](THIRD_PARTY_NOTICES.md).

## A small plug

**Ad · [TinyPx API](https://ai.tinypx.top)** — an AI API relay service. If you're building an AI app or just need an API for a side project, take a look: **https://ai.tinypx.top**.
