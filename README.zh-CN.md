<div align="center">

<img src=".github/assets/grass-block.png" alt="草方块" width="160">

<h1>NativeVoxel</h1>

<h3>用 C++，重新打开 Minecraft 1.8.9。</h3>

<p>
  <a href="https://github.com/TinypxNetwork/NativeVoxel/releases/tag/v0.1.0"><img src="https://img.shields.io/badge/version-v0.1.0_preview-a78bfa?style=flat-square" alt="v0.1.0 预览版"></a>
  <img src="https://img.shields.io/badge/platform-Windows_x64-7dd3fc?style=flat-square" alt="Windows x64">
  <img src="https://img.shields.io/badge/C%2B%2B-17-60a5fa?style=flat-square" alt="C++17">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL_v3-fda4af?style=flat-square" alt="GNU GPL v3"></a>
  <a href="https://github.com/TinypxNetwork/NativeVoxel"><img src="https://img.shields.io/badge/GitHub-Star-facc15?style=flat-square&logo=github&logoColor=white" alt="给项目点个 Star"></a>
</p>

<p>熟悉的方块世界，这次用原生客户端打开。<br>从一块草方块开始，把 1.8.9 的手感一点点做回来。</p>

<p>
  <a href="https://github.com/TinypxNetwork/NativeVoxel/releases"><b>下载预览版</b></a>
  &nbsp;·&nbsp;
  <a href="#从源码编译">从源码编译</a>
  &nbsp;·&nbsp;
  <a href="https://github.com/TinypxNetwork/NativeVoxel/issues">反馈问题</a>
  &nbsp;·&nbsp;
  <a href="README.md">English</a>
</p>

<sub>如果你对这个项目感兴趣，欢迎点个 ⭐ Star，也让更多人看到 NativeVoxel。</sub>

</div>

---

## 把 1.8.9 搬到原生客户端上

NativeVoxel 是一个面向 Windows 的 C++17 客户端，使用 OpenGL 3.3 和 Minecraft 原版协议 47。目前可以探索本地世界、建造、使用背包和容器，也可以连接服务器。游戏运行时不需要 JVM。

这是一个早期预览版本，已经有不少东西可以玩，但单人生存还没有做完整，联机兼容性也需要按服务器验证。

| 部分 | 目前已接入 |
| :--- | :--- |
| **世界与建造** | 主世界、下界和末地地形，Anvil 读取，移动与碰撞，合成、储物、熔炉和酿造。 |
| **渲染** | 区块网格、光照与环境光遮蔽、动态纹理、第一与第三人称、实体、装备和粒子。 |
| **联机** | 协议 47、加密登录、压缩、世界与背包同步、聊天和容器。 |
| **熟悉的细节** | 资源包重载、可配置按键、Unicode 字体、格式化聊天、物品提示、空间音效和音乐。 |

生物生成与 AI、红石、流体更新、传送门、完整地形装饰和剩余的生存交互还在开发中。一些渲染细节也与原版不同。

## 开始玩

1. 在 [Releases](https://github.com/TinypxNetwork/NativeVoxel/releases) 下载 **Windows x64 ZIP**，完整解压。
2. 首次准备资源需要 **Python 3.10+**，安装后执行：
   `python -m pip install "Pillow>=10" "numpy>=1.24"`
3. 双击解压目录里的 **`run.cmd`**。

启动入口会从 Mojang 下载约 **118 MiB** 的原版资源，检查哈希和大小，转换完成后启动游戏。下载有缓存，中断后重新运行即可复用已完成的文件。

准备完成后，游戏本身不再需要 Python 或 Java。请保留 EXE 旁边的 DLL 和 `assets/`，显卡需要支持 **OpenGL 3.3**。

<details>
<summary><b>常用操作与启动参数</b></summary>

| 按键 | 操作 |
| :--- | :--- |
| WASD · 空格 · Shift · Ctrl | 移动 · 跳跃 · 潜行 · 疾跑 |
| E · T · Q | 背包 · 聊天 · 丢出物品 |
| 数字键 / 鼠标滚轮 | 切换快捷栏 |
| F5 · F2 · F3 · F11 | 切换视角 · 截图 · 调试信息 · 全屏 |

鼠标左右键与世界交互，按键和声音可以在选项里调整。

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

正版验证服务器需要有效的 Minecraft 会话。客户端从本地进程环境读取 `MC_ACCESS_TOKEN`、`MC_UUID`，并使用匹配的玩家名。目前没有内置微软账号登录界面，服务器自己的注册和登录功能由服务器处理。

</details>

**关于存档：** NativeVoxel 会在 `saves/` 下创建新快照，导入的原始世界保持不变。快照只包含已加载区块，不是整个原存档的备份。部分模拟状态保存在标准 NBT 之外的 `native_simulation.dat` 中，存档互通还不完整。

## 从源码编译

仓库保留 C++ 客户端和编译需要的第三方源码。需要 **支持 C++17 的 MinGW-w64、zlib 和 OpenSSL 开发库**；编译本身不需要 Python、Java、MCP 或游戏资源。当前构建已使用 GCC 8.3 验证。

<details>
<summary><b>编译环境与完整命令</b></summary>

可以通过 [MSYS2](https://www.msys2.org/) 安装编译器和依赖，在它的 **UCRT64** 终端执行：

~~~sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-openssl
~~~

然后打开 PowerShell，确保编译器在 `PATH` 中。MSYS2 默认安装对应的目录为 `C:\msys64\ucrt64\bin`，将它加入当前终端的 `PATH` 即可。

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

请保留上面的浮点参数，修改它们可能影响移动和世界生成。

| 目录 | 内容 |
| :--- | :--- |
| `src/` | 客户端实现 |
| `include/mc/` | 头文件与编译所需的数据表 |
| `third_party/` | Vorbis 解码器、来源记录与许可证 |

要运行自己的构建，先完成一次发行包的资源准备，再把新 EXE 放进该目录，与 DLL 和 `assets/` 放在一起。也可以把运行库目录加入 `PATH`，使用 `--assets D:/NativeVoxel/assets` 指定处理后的资源。

资源包可以在游戏里选择，格式应为 1.8.9 的 `pack_format: 1`。

</details>

## 开源协议

NativeVoxel 的原创代码采用 **[GNU GPL v3](LICENSE)**（`GPL-3.0-only`）发布。

第三方代码与图片沿用各自的许可，详见[第三方说明](THIRD_PARTY_NOTICES.md)。Minecraft 及其原始资源的权利属于 Mojang、Microsoft 和相应作者。NativeVoxel 是独立项目，并非 Minecraft 官方产品。

---

<div align="center">

<h3><a href="https://ai.tinypx.top">TinyPx API</a></h3>

<p><b>一个 AI API 中转站。</b><br>做 AI 应用，或者想给自己的小项目接个 AI 接口，可以来看看。</p>

<p><a href="https://ai.tinypx.top"><b>前往 ai.tinypx.top ↗</b></a></p>

<sub>广告</sub>

</div>
