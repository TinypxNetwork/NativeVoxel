# 原版 Anvil 地形读取与新目录导出

`mc::worldio` 在 C++ 中读取 1.8.9 存档的 `level.dat` 和 `.mca` 区域文件，无运行时 Java 依赖。这是地形与元数据的持久化，尚不是完整单人游戏服务端。独立的原版生成模块见 [WORLDGEN.md](WORLDGEN.md)。

## API

```cpp
#include "mc/world_io.hpp"

mc::World world;
mc::worldio::SaveInfo info;
std::string error;
bool ok = mc::worldio::loadWorld("E:/saves/MyWorld", world, info, 8, &error);
// info.x/y/z/yaw/pitch 为保存的玩家视角；没有 Player 时使用世界出生点。

mc::Player player;
player.teleport(info.x, info.y, info.z);
player.yaw = info.yaw;
player.pitch = info.pitch;
bool saved = mc::worldio::saveNewWorld("E:/saves/MyWorld-native-copy", world, player, info, &error);
```

读取半径按区块计，范围 0–32，以保存的玩家坐标或出生点为中心，载入 `(2r+1)²` 范围内已经存在的区块。它不会为缺少的区域生成地形。保存的维度为 -1、0、1 时，分别读取 `DIM-1/region`、`region`、`DIM1/region`。末地和下界不使用天空光。

`loadWorld` 成功后才替换传入的世界和 `SaveInfo`；失败保留调用方原数据。`SaveInfo.metadata` 保留原 NBT 结构，供随后导出时保存未知标签、实体和方块实体等数据。

`saveNewWorld` **只接受尚不存在的目录**，空的现有目录也会被拒绝。它也拒绝在原始导入存档内部创建输出文件。目录采用独占创建；失败时仅清理由本次调用创建且重新解析验证过的输出目录。输入存档始终以只读方式打开。

原生生成的世界可显式设置 `SaveInfo.generation`，记录 Default、Large Biomes 或 Amplified 生成器、结构开关、游戏模式和命令开关。`terrainPopulated` 默认为 false，适用于尚未完成 population 的区块；只有调用方完成该阶段后才应设为 true。原版生成元数据不会被写成演示世界的平坦配置，生存模式也使用对应的玩家能力标志。没有设置此可选值时，保留原有预览导出及导入元数据的行为。

将 `PopulationOutput.blockEntities` 交给 `SaveInfo.blockEntityUpdates`，即可把地牢箱子、附魔书、刷怪笼等完整 NBT 写入 Anvil。更新按顺序合并；TAG_End 删除相应位置的旧方块实体。未更新的方块实体及未知区块标签保留。更新的位置、NBT 坐标、类型、列表元素类型和 modified UTF-8 在创建输出目录前检查。只有当前已加载的区块会写入文件。

## 已支持的数据

- `level.dat` 的 gzip NBT，存档名、种子、出生点、玩家位置/视角/维度、世界边界。
- 区域文件 8 KiB 表头、32×32 区块索引、负坐标区域、扇区偏移/长度与重叠检查。
- 区块压缩类型 1（gzip）、2（zlib）、3（原始 NBT）。新导出使用 gzip `level.dat` 与 zlib 区块。
- 1.8 的 `Sections`、`Y`、`Blocks`、`Data`、可选 `Add`、`BlockLight`、`SkyLight`、`Biomes`。方块 ID 的高四位与全部元数据原样保留。
- NBT 的 byte、short、int、long、float、double、byte array、string、list、compound、int array，以及通用解析器中的 long array。
- Java modified UTF-8，包括 NUL、UTF-16 代理对和无配对代理单元的无损表示。

每份解压后的 NBT 上限 64 MiB，整个导入范围累计 NBT 上限 512 MiB，嵌套最多 64 层，单份 NBT 节点数最多 500,000。负长度、截断、错误标签类型、非法区块节及无效压缩均会返回错误，不部分应用到世界中。

## 导出范围与保留规则

导出的是**当前已经载入的区块子集**。它不是原始整个存档的备份，未载入的区块不会自动复制。若希望原版随后加载其余原始区块，应另行使用原始存档的独立副本；本 API 不覆盖现有存档。

导出导入过的区块时，保留其未知 NBT、`Entities`、`TileEntities`、`TileTicks`、原始生成器设置和已有 `TerrainPopulated` 标志，替换方块节、群系、高度图和玩家位置/视角/速度等已知字段。显式提供的方块实体更新及 `generation` 设置会覆盖对应字段。已有实体和方块实体只是保留，当前原生客户端尚未完整模拟它们；修改有关方块后，调用方仍须提供对应的 NBT 更新。

新生成的预览存档使用 1.8.9 Anvil 格式，已有区块来自 NativeVoxel 的演示地形。导出的新存档将未存储区域的生成器设为普通平坦配置，便于原版打开；这不代表 NativeVoxel 已实现该生成器。新导出的区块标记为需要原版检查光照，同时保留已存储的光照数组。

`loadChunk` 可以从同一原始存档按需读取一个已有区块，并带回方块实体更新。应用层的 `LocalSession` 已接入随玩家移动的加载，先读保存的区块再考虑原生生成；详见 [LOCAL_SESSION.md](LOCAL_SESSION.md)。

1.13+ 调色板/压平格式、现代外部 `.mcc` 区块、非原版维度、完整区块卸载/后台读写、全部实体仿真、红石和世界随机刻不属于当前实现。

## 验证

`tests/world_io_tests.cpp` 使用独立构造的二进制 NBT/区域夹具验证：

- gzip/zlib/raw 三种区块、负区域坐标、上下不同区块节与 `Add` 高位。
- 方块元数据、方块光、天空光、群系、玩家与出生点、末地/下界。
- 全部 NBT 类型、NUL/中文/补充平面 Unicode 的读取与重新保存。
- 修改地形后导出再读取，未知标签及实体数据保留，原始输入文件字节不变。
- 已有输出目录拒绝、原始存档内部输出拒绝、格式损坏时世界不变。
- NativeVoxel 预览地形写成新存档再读回。
- 生成器/生存模式元数据、未完成 population 标记、生成和删除方块实体、附魔书的 typed NBT 与中文/NUL/补充平面自定义名称。
- 对导入存档再次应用方块实体更新，并拒绝坐标不一致、超范围数值、混合类型列表等无效更新。

这些测试已在 GCC 8.3/Windows 下通过。构建需要 zlib、NBT 桥接使用的协议 Reader，以及 GCC 8 的 `-lstdc++fs`；`test.ps1` 已配置依赖。

另外执行 `python tools/verify_world_io.py`，会导出一份新预览存档，并重新编译所给源码的 `AnvilChunkLoader`、`RegionFile`、`RegionFileCache`、`CompressedStreamTools`，由原版 Java 读取。当前验证通过：9 个区块、43 节、152,384 个非空气方块被原版正确读取，保存的玩家位置及路径方块正确，读取前后存档文件字节不变。报告和可供原版打开的存档保留在 `build/world_io_reference`。

同一脚本还导出独立的生成元数据／方块实体夹具，由原版 Anvil 加载器构造真实箱子和刷怪笼。已验证附魔书、名称、刷怪笼参数、删除记录、生存能力与未完成 population 标志；该夹具为 9 个区块、43 节、152,386 个非空气方块，读取前后文件字节不变。报告为 `build/world_io_reference/tiles_report.json`。
