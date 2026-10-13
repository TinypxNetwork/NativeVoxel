# 原生 1.8.9 音频模块

`include/mc/audio.hpp`、`src/audio.cpp` 实现无 Java 的声音事件解析、Ogg Vorbis 解码、OpenAL 空间播放和流式播放。资源来自导入器恢复的原版文件。此模块不依赖游戏世界或网络，服务器音效和本地脚步等触发由应用层传入。

## 接口与构建

```cpp
mc::Audio audio;
std::string error;
if (audio.initialize("assets", error)) {
    // 相机/玩家插值后的眼睛坐标；yaw 和 pitch 使用 Minecraft 的角度。
    audio.updateListener(10, 65.62, 20, 90, 0);
    audio.play("step.stone", 10, 64, 20, 0.15f, 1.0f);
    audio.tick(); // 每帧或至少每秒 20 次
}
```

音频初始化失败会返回错误，游戏可以继续运行。`play` 失败原因可以通过 `lastError()` 查看。`stopAll()` 停止全部声音；析构会释放源、缓冲区、上下文和设备。`setMasterVolume` 与 `setCategoryVolume` 接受 0–1；静音会停止相应声音。

只需要把 `src/audio.cpp` 加入 C++17 编译。它在单独的编译单元内包含 `third_party/stb_vorbis.c` 的实现，不要再次单独编译该 C 文件。OpenAL 使用 `LoadLibraryW` 从资产目录的完整路径加载，没有 OpenAL 导入库依赖。

## sounds.json 规则

`SoundRegistry` 可独立使用，不加载 OpenAL，也不创建音频设备。它读取 `runtime/audio/sounds-layers.txt`，按照原版默认包、用户资源包优先级逐层合并定义。覆盖一个 `sounds.json` 并不会清除整个默认声音表：原 `SoundHandler` 使用 `getAllResources`，随后对每个事件应用规则。

- `replace` 默认为 false：向已有事件追加选项，保留其原类别；true 则替换该事件并更新类别。
- 字符串选项是文件名，默认 volume=1、pitch=1、weight=1、stream=false；文件路径为 `<namespace>:sounds/<name>.ogg`。
- 未限定文件名继承当前声音事件的命名空间；明确指定的文件命名空间也被保留。
- 文件权重为正整数；选择使用 Java `Random` 的 48 位线性同余状态与 `nextInt` 拒绝采样。纯测试包含 `java.util.Random(0).nextInt(10)` 的已知序列。
- 原版 1.8.9 的 `type:event` 权重为目标事件所有选项的权重之和。它忽略别名选项自身的 weight、volume、pitch、stream，而不是后续版本的乘法规则。最终分类来自调用的外层事件。
- 还保留了此 MCP `SoundHandler` 的明确命名空间别名细节：别名目标使用原始 name 作为 path。因此 `name:"minecraft:base"` 在此源码中会构成 `minecraft:minecraft:base`，而 `name:"base"` 是普通的同命名空间引用。这与现代版本规则不同。
- 缺失音频文件从池中移除并记入 `warnings()`；空事件、未知事件、别名循环、深度或权重溢出明确返回错误。

默认提供的 `sounds.json` 有 246 个事件，全部文件均存在。事件循环与异常资源包输入采用有界失败，不模仿 Java 栈溢出或异常崩溃。JSON 限制为 16 MiB、64 层、200,000 个节点；仅接受有限正数音量/音高与正整数权重。

## 音量、方向与距离

对应源码：`SoundManager::playSound`、`getNormalizedVolume`、`getNormalizedPitch` 和 `setListener`。

```text
gain = clamp(调用音量 × 文件音量, 0, 1) × 类别音量 × 主音量
pitch = clamp(调用音高 × 文件音高, 0.5, 2.0)
maxDistance = 16 × max(1, 调用音量)
distanceGain = clamp(1 - 距离 / maxDistance, 0, 1)
```

距离范围在调用音量被限制到 1 之前计算，并且不乘声音选项音量。普通世界音效采用线性衰减。`relative=true` 用于界面等相对声音，不受距离影响；声音位于听者本地原点。类别名称沿用 1.8.9 的 `master/music/record/weather/block/hostile/neutral/player/ambient`，例如 `step.stone` 原本属于 neutral。

听者 yaw=0 朝向 +Z，yaw=90 朝向 −X，正 pitch 朝下；前向量和上向量依据原版公式计算。调用方必须传眼睛坐标，不是脚下坐标。OpenAL 负责设备输出和方向声像；距离音量手动计算，以便立体声资源也按原规则衰减。禁用 Doppler 和 OpenAL 自带距离衰减，避免重复应用。

## 解码、缓冲与流式播放

文件首先校验完整 Ogg 页边界、页序号、单逻辑流和页 CRC，然后由固定版本 stb_vorbis 解码为交错 16 位 PCM。支持单声道和立体声，采样率 1,000–384,000 Hz。压缩文件最大 64 MiB；公开 CPU 解码接口还限制帧数和最高 256 MiB 输出，拒绝截断或损坏文件。

普通音效按资源路径缓存 OpenAL PCM 缓冲区，最多 64 MiB，优先淘汰未被正在播放的声音使用的旧缓冲区。最多 32 个普通源和 4 个流式源；达到上限时替换同类最旧的声音。

`stream:true` 使用独立 Vorbis 状态与四个 4,096 帧 OpenAL 队列缓冲区，在 `tick()` 回收与补充。完整的压缩 Ogg 文件保留在内存中，但不把整首音乐解码成 PCM；不阻塞播放等待完整解码。必须及时调用 `tick()`，长时间停止更新可能造成短暂断续，恢复时会重新启动有数据的源。

## 测试与实际验证范围

所有自动测试使用 `initialize(path,error,true)` 的 `nullOutput`。此模式不会加载 OpenAL、不会创建设备，也不会发出声音，但仍执行事件选择、文件校验、实际 Vorbis 解码、缓存、流式读取、声源上限和静音清理。`SoundRegistry` 与静态 `decodeVorbisFile` 同样不会发出声音。

```powershell
g++ -std=c++17 -O2 -I include tests/audio_tests.cpp src/audio.cpp -o audio_tests.exe
.\audio_tests.exe assets
.\audio_tests.exe assets --decode-all
```

已验证全部 578 个原始 Ogg 文件，解码得到 491,652,364 个 PCM 帧；完整检查约 9 秒。还测试了资源池规则、已知 Java 随机序列、音量/音高计算、方位向量、CRC/截断拒绝、缓存复用、声源上限和静音行为。编译使用 GCC 8.3，`-Wall -Wextra -Wpedantic` 无警告。针对上游 seek 函数的 GCC 8.3 `maybe-uninitialized` 诊断仅在引入未修改的第三方源码时屏蔽；调用解码器之前额外验证 Ogg 容器边界。

未在自动测试中进行可听设备播放，也没有声称耳机方向、声卡延迟或混音结果已经通过实机听音验证。

## 明确未覆盖的行为

- 没有移植 MusicTicker 自动音乐调度、完整记录唱片交互、天气/生物自动音效触发；应用层需调用 `play`。
- 目前接口创建一次性声音。原 ITickableSound 的移动声源更新、重复/延时播放、暂停恢复及全部游戏状态关联尚未整体移植。
- 不支持 EAX、额外混响、声遮挡、HRTF 配置界面或设备热切换；使用提供的 OpenAL Soft 设备默认设置。
- 音频线程调度、声源抢占策略与随机种子生命周期不是原 Java SoundSystem 的逐位复制。权重分布和参数规则已按来源移植，Java 原本每个事件独立初始化的随机状态不会与本客户端同步。
- stb_vorbis 与原 JOrbis 解码器可能存在末位样本舍入差异；没有进行全部样本的原 Java 解码器逐位比较。多重/链接 Ogg 逻辑流和超过两声道的资源暂不支持。
- 音频播放和渲染不会证明运动、服务器交互或反作弊行为一致。

## 第三方来源

- **stb_vorbis**：官方 `nothings/stb`，固定提交 `2c980bb59875b0d32144a71867fbdebb2f77cd20`。文件 SHA-256 为 `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`；上游 MIT / public domain 双许可全文保留在源码末尾。固定 URL 与校验记录在 `third_party/stb_vorbis.provenance.json`。
- **OpenAL Soft 1.15.1**：来自用户提供的 LWJGL Windows natives JAR，导入前用 `1.8.9.json` 中 SHA-1 和长度验证该 JAR，然后提取 `OpenAL64.dll`。结果与原版本目录中的 DLL 完全相同。DLL SHA-256 为 `9261b66010a845ddef9f61d5e4266fe2f08a53f3605da002e9e8f8d202bdbc5e`；具体来源记录在 `assets/runtime/audio/openal-provenance.json`。LGPL 许可文本保存在 `third_party/OpenAL-Soft-COPYING`；对应源码版本为 [OpenAL Soft 1.15.1](https://github.com/kcat/openal-soft/tree/openal-soft-1.15.1)。原库采用动态链接，未改动 DLL。

常规资源准备与客户端运行不下载任何依赖；首次开发时下载的第三方解码源码和许可已经固定保存在项目中。
