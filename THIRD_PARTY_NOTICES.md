# Third-party notices

- **Minecraft 1.8.9 / MCP references.** Development uses the locally supplied MCP 9.19 source as a behavior reference. Minecraft code and assets remain the property of Mojang, Microsoft and their respective authors. Source-derived tables record their provenance where available. The original MCP tree, client JAR, sounds and textures are not included in this repository. Asset setup retrieves metadata, client resources and indexed objects from Mojang's official HTTPS endpoints and verifies their published SHA-1 hashes and sizes.
- **OpenAL Soft.** Asset setup retrieves the original LWJGL Windows natives archive from Mojang and extracts `OpenAL64.dll` for native audio. The bundled version identifies itself as OpenAL Soft 1.15.1. License: LGPL; see [OpenAL-Soft-COPYING](third_party/OpenAL-Soft-COPYING).
- **stb_vorbis.** The Vorbis decoder is vendored at a fixed upstream revision. Its license is included in [stb_vorbis.c](third_party/stb_vorbis.c); source and revision details are in [the provenance record](third_party/stb_vorbis.provenance.json).
- **Compiler and libraries.** The client uses the installed GCC/MinGW runtime, zlib and OpenSSL. Their respective upstream licenses apply.
- **Windows APIs.** Rendering, input, networking and image decoding use OpenGL, Win32, Winsock, WinHTTP and Windows Imaging Component.
- **Resource conversion.** Python, Pillow and NumPy prepare assets. They are not dependencies of the game executable once its assets are prepared.

NativeVoxel is not an official Minecraft product and is not affiliated with or endorsed by Mojang or Microsoft. Downloading assets for local use does not grant permission to redistribute them. This repository contains no compiled client or prepared game resources.
