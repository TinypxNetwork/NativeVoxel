# NativeVoxel

**English** · [简体中文](README.zh-CN.md)

Minecraft 1.8.9, rebuilt as a native C++ client for Windows. NativeVoxel uses OpenGL 3.3 and speaks the original protocol 47. The game runs without a JVM.

The aim is to bring the feel and behavior of 1.8.9 to a native client, one system at a time. You can explore local worlds, build, use inventories and containers, and connect to servers. It is still a work in progress: multiplayer compatibility varies, and singleplayer is not yet a complete survival game.

If you'd like to follow the project, give it a ⭐ [Star on GitHub](https://github.com/TinypxNetwork/NativeVoxel). It helps more people find NativeVoxel.

## Source layout

- `src/` and `include/mc/`: the C++ client and its tables.
- `third_party/`: the Vorbis decoder, its provenance, and license notices.

This repository contains the C++ source needed to compile the client. Download the prebuilt client and its resource setup files from [Releases](https://github.com/TinypxNetwork/NativeVoxel/releases).

## Compile

You need Windows x64 and a MinGW-w64 C++17 compiler with zlib and OpenSSL development libraries. The existing client has been built with GCC 8.3. Compilation does not require Python, Java, MCP or downloaded game assets.

For a new compiler installation, [MSYS2](https://www.msys2.org/) provides these packages in its **UCRT64** terminal:

~~~sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-openssl
~~~

Then open PowerShell with the compiler available on `PATH`. For a default MSYS2 installation, add `C:\msys64\ucrt64\bin` to that session's `PATH`.

~~~powershell
git clone https://github.com/TinypxNetwork/NativeVoxel.git
cd NativeVoxel
$sources = (Get-ChildItem src -Filter *.cpp).FullName
g++ -std=c++17 -O3 -DNDEBUG -ffp-contract=off -fno-fast-math `
    -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I include $sources `
    -o NativeVoxel.exe -lopengl32 -lgdi32 -luser32 -lshell32 -lwinmm `
    -lws2_32 -lwinhttp -ldnsapi -lcrypto -lz -lole32 -lwindowscodecs -luuid `
    -lstdc++fs -static-libgcc -static-libstdc++

.\NativeVoxel.exe --help
~~~

Keep the floating-point flags shown above, since changing them can affect movement and world generation.

## Run

The game needs an OpenGL 3.3 capable GPU and prepared resources. The Windows ZIP in [Releases](https://github.com/TinypxNetwork/NativeVoxel/releases) includes a launcher that downloads about **118 MiB** of original resources from Mojang, checks their hashes and converts them on first launch. That setup step needs Python 3.10+, Pillow and NumPy; follow the README inside the ZIP. The game itself needs neither Python nor Java afterward.

To run your own build, prepare the release package once, then place your compiled EXE in that folder with its DLLs and `assets/`. Alternatively, keep the runtime DLL directory on `PATH` and pass `--assets D:/NativeVoxel/assets` to use that resource directory. Resource packs can be selected in game and should target 1.8.9 (`pack_format: 1`).

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
