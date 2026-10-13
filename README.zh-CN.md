# NativeVoxel

[English](README.md) · **简体中文**

用 C++ 重新实现 Minecraft 1.8.9 的 Windows 原生客户端。NativeVoxel 使用 OpenGL 3.3，支持原版协议 47，游戏运行时不需要 JVM。

这个项目想做的事很直接：把 1.8.9 的手感和行为，一部分一部分地搬到原生客户端上。目前可以探索本地世界、建造、使用背包和容器，也可以连接服务器。项目还在开发中，联机兼容性需要按服务器验证，单人生存也还没有做完整。

## 源码结构

- `src/` 和 `include/mc/`：C++ 客户端及其数据表。
- `tools/`：原版资源下载和转换的 Python 源码。
- `tools/reference/`：资源转换需要的文本数据表。
- `third_party/`：Vorbis 解码器源码、来源记录和许可证。

仓库只保留源码及其必要数据。构建辅助脚本、测试工具、开发报告、游戏资源和编译产物不放进公开目录。

## 准备资源

需要 Windows x64、支持 OpenGL 3.3 的显卡、Python 3.10+，以及支持 C++17 的 MinGW-w64 编译器和 zlib、OpenSSL 开发库。现有客户端已使用 GCC 8.3 构建。

如果还没有编译环境，可以安装 [MSYS2](https://www.msys2.org/)，在它的 **UCRT64** 终端安装这些包：

~~~sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-openssl
~~~

然后打开 PowerShell，确保 Python 和编译器已加入 `PATH`。MSYS2 默认安装对应的编译器目录为 `C:\msys64\ucrt64\bin`，可将它添加到当前终端的 `PATH`。

~~~powershell
git clone https://github.com/TinypxNetwork/NativeVoxel.git
cd NativeVoxel
python -m pip install "Pillow>=10" "numpy>=1.24"
python tools/setup_assets.py
~~~

资源准备命令会自动从 Mojang 下载约 **118 MiB** 的原版 1.8.9 资源和 Windows 音频库，检查 SHA-1 与文件大小，再转换成原生渲染器使用的格式。下载缓存放在 `.cache/minecraft/`，游戏读取的资源放在 `assets/`。不需要另装 Minecraft，也不需要准备 MCP 源码。

通过校验的缓存会直接复用，下载失败后重新执行命令即可补齐。使用 `--offline` 可以完全离线处理已验证的缓存；`--cache D:/MinecraftCache` 指定缓存目录；`--mcp D:/MCP-919-main` 则导入已有的本地 MCP 目录。

资源包可以用 `python tools/setup_assets.py --pack D:/packs/example.zip` 导入，也可以在游戏里选择。格式应为 1.8.9 的 `pack_format: 1`。

## 编译与运行

在项目根目录打开 PowerShell，直接编译源码：

~~~powershell
$sources = (Get-ChildItem src -Filter *.cpp).FullName
g++ -std=c++17 -O3 -DNDEBUG -ffp-contract=off -fno-fast-math `
    -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I include $sources `
    -o NativeVoxel.exe -lopengl32 -lgdi32 -luser32 -lshell32 -lwinmm `
    -lws2_32 -lwinhttp -ldnsapi -lcrypto -lz -lole32 -lwindowscodecs -luuid `
    -lstdc++fs -static-libgcc -static-libstdc++

.\NativeVoxel.exe
~~~

运行时，编译器配套 DLL 所在目录需要在 `PATH` 中，也可以把所需 DLL 放到 EXE 旁边。请从项目根目录启动，以便客户端找到 `assets/`。Python 只用于准备资源，游戏本身不需要 Python 或 Java。请保留上面的浮点编译参数，修改它们可能影响移动和世界生成。

~~~powershell
# 创建本地世界，默认创造模式。
.\NativeVoxel.exe --vanilla --seed 189

# 打开已有的 Anvil 存档。
.\NativeVoxel.exe --world "D:\Worlds\MyWorld"

# 连接服务器。
.\NativeVoxel.exe --connect mc.tinypx.net --username YourName

# 渲染和物理演示场景。
.\NativeVoxel.exe --demo
~~~

WASD 移动，空格跳跃，Shift 潜行，Ctrl 疾跑，E 背包，T 聊天。鼠标左右键交互，数字键或滚轮切换物品，Q 丢出物品。F5 切换视角，F2 截图，F3 调试信息，F11 全屏。按键和声音可以在选项里调整。

正版验证服务器需要有效的 Minecraft 会话。客户端从本地进程环境读取 `MC_ACCESS_TOKEN`、`MC_UUID`，并使用匹配的玩家名；目前没有内置微软账号登录界面。服务器自己的注册、登录功能由服务器处理。

## 目前做到哪了

- **渲染：**区块网格、光照和环境光遮蔽、动态纹理、第一与第三人称、物品和实体模型、装备、粒子，以及资源包重载。
- **联机：**协议 47 登录、加密与压缩、世界更新、实体、聊天、背包和容器。插件与反作弊兼容性仍需要逐服验证。
- **本地世界：**主世界、下界和末地地形生成，Anvil 读取，移动与碰撞，20 Hz 模拟，合成、储物、熔炉、酿造、掉落物和部分投射物。
- **界面与声音：**菜单、可配置按键、Unicode 字体、格式化聊天、物品提示、空间音效和音乐。

比较大的缺口包括生物生成与 AI、红石和流体传播、完整地形装饰与结构在游戏中的接入、传送门，以及剩余的生存交互。一些渲染细节也和原版不同。注册表里有全部方块和物品 ID，不代表它们的行为已经全部实现。

**存档采用新目录快照。** NativeVoxel 会在 `saves/` 下写入新目录，导入的原始世界保持不变。快照只包含已加载区块，并不是整个原存档的备份。部分模拟状态保存在标准 NBT 之外的 `native_simulation.dat` 中，存档互通还不完整。

Minecraft 及其原始资源的权利属于 Mojang、Microsoft 和相应作者。NativeVoxel 是独立项目，并非 Minecraft 官方产品。原版游戏资源在准备阶段下载，不随仓库分发。第三方组件说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 顺便打个广告

**广告 · [TinyPx API](https://ai.tinypx.top)**，一个 AI API 中转站。如果你正在做 AI 应用，或者想给自己的小项目接个 AI 接口，可以来看看：**https://ai.tinypx.top**。
