# NativeVoxel

[English](README.md) · **简体中文**

用 C++ 重新实现 Minecraft 1.8.9 的 Windows 原生客户端。NativeVoxel 使用 OpenGL 3.3，支持原版协议 47，游戏运行时不需要 JVM。

这个项目想做的事很直接：把 1.8.9 的手感和行为，一部分一部分地搬到原生客户端上。目前可以探索本地世界、建造、使用背包和容器，也可以连接服务器。项目还在开发中，联机兼容性需要按服务器验证，单人生存也还没有做完整。

## 构建与运行

需要 Windows x64、支持 OpenGL 3.3 的显卡、Python 3.10+，以及支持 C++17 的 MinGW-w64 编译器和 zlib、OpenSSL 开发库。请确保能找到 `g++`、Python 和编译器附带的 `objdump`。现有构建使用 GCC 8.3 验证，MSVC 暂不在已验证的构建流程内。

如果还没有编译环境，可以安装 [MSYS2](https://www.msys2.org/)，在它的 **UCRT64** 终端安装这些包：

~~~sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-openssl
~~~

然后打开 PowerShell。如果 MSYS2 安装在默认位置，先为当前终端添加编译器路径：

~~~powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
git clone https://github.com/TinypxNetwork/NativeVoxel.git
cd NativeVoxel
python -m pip install -r requirements-dev.txt
powershell -ExecutionPolicy Bypass -File build.ps1
.\run.cmd
~~~

首次构建会从 Mojang 下载原版 1.8.9 客户端资源和 Windows 音频库，检查 SHA-1 与文件大小，再转换成原生渲染器使用的资源。下载量约 **118 MiB**，转换后的资源还会占用额外空间。正常构建不需要另装 Minecraft，也不需要准备 MCP 源码。

下载缓存放在 `.cache/minecraft/`，处理后的资源放在 `assets/`。通过校验的文件会直接复用；如果下载中断，重新执行同一条命令即可补齐缺失文件。Python 只用于准备资源和开发，准备完成后，游戏 EXE 只需要配套 DLL 和 `assets/`，不需要 Python 或 Java。通过 `run.cmd` 启动时，如果缺少处理后的资源，也会自动调用资源准备流程。

~~~powershell
# 已经准备好资源，只重新编译。
powershell -ExecutionPolicy Bypass -File build.ps1 -SkipAssets

# 单独下载并准备全部资源，不编译游戏。
python tools/setup_assets.py

# 只使用已下载且通过校验的缓存，全程不联网。
python tools/setup_assets.py --offline
~~~

用 `--cache D:/MinecraftCache` 指定下载缓存，或用 `--mcp D:/MCP-919-main` 导入已有的本地 MCP 目录。`build.ps1` 对应提供 `-AssetCache`、`-McpRoot` 和 `-OfflineAssets`。资源包可以通过 `python tools/setup_assets.py --pack D:/packs/example.zip` 导入，也可以在游戏里选择，格式应为 1.8.9 的 `pack_format: 1`。细节见[资源说明](docs/ASSETS.md)。

## 开始玩

双击 `run.cmd`，或者在项目根目录执行：

~~~powershell
# 创建本地世界，默认创造模式。
.\bin\NativeVoxel.exe --vanilla --seed 189

# 打开已有的 Anvil 存档。
.\bin\NativeVoxel.exe --world "D:\Worlds\MyWorld"

# 连接服务器。
.\bin\NativeVoxel.exe --connect mc.tinypx.net --username YourName

# 渲染和物理演示场景。
.\bin\NativeVoxel.exe --demo
~~~

WASD 移动，空格跳跃，Shift 潜行，Ctrl 疾跑，E 背包，T 聊天。鼠标左右键交互，数字键或滚轮切换物品，Q 丢出物品。F5 切换视角，F2 截图，F3 调试信息，F11 全屏。按键和声音可以在选项里调整。

正版验证服务器需要有效的 Minecraft 会话。客户端从本地进程环境读取 `MC_ACCESS_TOKEN`、`MC_UUID`，并使用匹配的玩家名；目前没有内置微软账号登录界面。服务器自己的注册、登录功能由服务器处理。

## 目前做到哪了

- **渲染：**区块网格、光照和环境光遮蔽、动态纹理、第一与第三人称、物品和实体模型、装备、粒子，以及资源包重载。
- **联机：**协议 47 登录、加密与压缩、世界更新、实体、聊天、背包和容器。插件与反作弊兼容性仍需要逐服验证。
- **本地世界：**主世界、下界和末地地形生成，Anvil 读取，移动与碰撞，20 Hz 模拟，合成、储物、熔炉、酿造、掉落物和部分投射物。
- **界面与声音：**菜单、可配置按键、Unicode 字体、格式化聊天、物品提示、空间音效和音乐。

比较大的缺口包括生物生成与 AI、红石和流体传播、完整地形装饰与结构在游戏中的接入、传送门，以及剩余的生存交互。一些渲染细节也和原版不同。注册表里有全部方块和物品 ID，不代表它们的行为已经全部实现。

**存档采用新目录快照。** NativeVoxel 会在 `saves/` 下写入新目录，导入的原始世界保持不变。快照只包含已加载区块，并不是整个原存档的备份。部分模拟状态保存在标准 NBT 之外的 `native_simulation.dat` 中。需要交换存档时，先看[世界读写](docs/WORLD_IO.md)和[本地会话](docs/LOCAL_SESSION.md)里的说明。

## 开发与测试

代码在 `src/` 和 `include/mc/`，资源处理和对照数据导出工具在 `tools/`，原生测试和 Java 对照程序在 `tests/`。仓库保留构建需要的小型生成表和数值测试数据，因此普通构建不依赖 MCP。下载的游戏文件、纹理、声音、本地设置、账号凭据、存档、日志和编译产物都不会提交到 Git。

~~~powershell
# 下载器测试，使用本地模拟数据，不需要联网或游戏资源。
python tests/asset_download_tests.py

# 构建完成后，运行原生、资源和协议检查。
powershell -ExecutionPolicy Bypass -File test.ps1

# 加上实际 GPU 渲染检查和全部音频解码。
powershell -ExecutionPolicy Bypass -File test.ps1 -Graphics -AllAudio
~~~

`test.ps1 -Reference` 还会和原 Java 行为进行比较，需要本地 MCP 源码与 JDK。旗帜像素对照文件包含原版纹理像素，因此在本地生成；普通测试缺少这份数据时，会明确显示跳过该项比较。请保留 `-ffp-contract=off -fno-fast-math`，浮点运算方式的变化可能影响移动和世界生成。

更多实现说明：[协议](docs/PROTOCOL.md)、[世界生成](docs/WORLDGEN.md)、[渲染资源](docs/RENDER_ASSETS.md)、[音频](docs/AUDIO.md)和[性能](docs/PERFORMANCE.md)。这些文档会记录已实现内容和限制，不代表已经完整复刻原版。

Minecraft 及其原始资源的权利属于 Mojang、Microsoft 和相应作者。NativeVoxel 是独立项目，并非 Minecraft 官方产品。原版游戏资源在准备阶段下载，不随仓库分发。第三方组件说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 顺便打个广告

**广告 · [TinyPx API](https://ai.tinypx.top)**，一个 AI API 中转站。如果你正在做 AI 应用，或者想给自己的小项目接个 AI 接口，可以来看看：**https://ai.tinypx.top**。
