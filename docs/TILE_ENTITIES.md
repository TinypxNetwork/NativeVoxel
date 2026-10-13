# 箱子、头颅和旗帜

`tile_entities.hpp/.cpp` 提供方块实体的原生 CPU 渲染数据。支持普通/陷阱箱、小箱/大箱、末影箱、五种原版头颅，以及立式/墙上旗帜。运行时不启动 Java、Python、图形辅助进程或音频设备。

本模块不改变世界、库存、拾取或服务器状态。告示牌仍由现有主渲染器处理。

主 Renderer 已接入这些网格与纹理；主程序提供 S35 NBT、箱盖状态和皮肤缓存。本地和服务器场景均已实际绘制检查，CPU 参考对照的范围仍与 GPU 像素验证分开记录。

## 接入

```cpp
mc::TileEntityVisuals visuals;
visuals.load("assets", error);

mc::TileEntityPose pose;
pose.x = blockX; pose.y = blockY; pose.z = blockZ;
pose.renderX = blockX - renderOriginX;
pose.renderY = blockY - renderOriginY;
pose.renderZ = blockZ - renderOriginZ;
pose.partialTicks = partialTick;
pose.worldTime = totalWorldTime;
pose.lid = chestDisplayState;
pose.nbt = tileData ? &tileData->nbt : nullptr;
auto frame = visuals.build(world, pose, &skinCache);
```

`nbt` 是完整、未压缩的根标签字节，仅在调用期间借用。世界坐标用于查邻居及旗帜相位，`renderX/Y/Z` 是相对于渲染原点的坐标。`christmas` 由调用方按照原版 12 月 24～26 日规则设置。`destroyStage=-1` 表示正常材质，0～9 提供原版破坏纹理。

每个绘制项包含共享三角网格、OpenGL 列主序模型矩阵、纹理矩阵、纹理对象和剔除标志。纹理对象提供稳定 `key`、RGBA8 像素、尺寸、revision；像素从上到下排列。`repeat=true` 的破坏材质需要重复采样，并应用 `textureMatrix`，不要钳制到边缘。调用方应用对应光照/alpha 状态并提交 GPU 绘制；本模块不接管渲染 pass。

状态含义：`Drawn` 有正常绘制；`Hidden` 是不应重复绘制的大箱第二半；`MissingData` 是尚无旗帜 NBT、未加载资产或原版旗帜设计缓存已满；`InvalidData` 附诊断；`Unsupported` 留给其他渲染器。

必须从可见 World 方块中发现箱子位置，不能只遍历 S35 表，因为普通箱子通常没有 S35 描述包。`handlesBlock()` 识别 ID 54、130、146、144、176、177。主地形网格应排除这些位置原有的 builtin 近似几何，避免重复绘制；大箱被隐藏的一半同样不绘制旧网格。63/68 告示牌不在此列表中。

网络模块提供 `Game::tileEntities` 中的完整 S35 NBT，以及 `Game::blockActions` FIFO。消费 S24 前核对该位置当前 block ID。对于箱子 action 1：

```cpp
mc::TileEntityVisuals::blockAction(lid, event.action, event.parameter);
// 每个游戏 tick 执行一次：
mc::TileEntityVisuals::advanceLid(lid);
```

调用方按位置维护 `ChestLidState`，并在替换方块、卸载区块或切换世界时重置。此状态只控制显示，不播放箱子声音。`clear()` 清理本模块的 NBT、旗帜和玩家头颅缓存，保留静态资源。资源包重新加载时，同时清理调用方旧的 GPU 纹理缓存。

## 原版几何与动画

箱子采用 `ModelChest/ModelLargeChest` 的原始尺寸、铰链位置、UV 和绘制顺序：盖、锁扣、下箱体。邻接只连接相同箱子类型；西侧或北侧已有同类箱子的半块不重复绘制，正向邻居决定大箱模型。末影箱始终使用小箱。

箱盖每 tick 变化 0.1，保留原始 previous/current 状态，插值后使用 `1-(1-f)^3`，旋转 `-f*pi/2`。锁扣同步旋转。普通、陷阱、圣诞、小/大、末影材质均来自原资源，破坏阶段分别采用原版 4×4、8×4 UV 变换。

头颅采用 `ModelSkeletonHead/ModelHumanoidHead`，保留头部外层的 0.25 像素膨胀、地面旋转和四种墙面偏移。NBT 字段为 `SkullType`、`Rot` 与世界方块实体的 `Owner`，并支持旧 `ExtraType` 名称。破坏阶段按源分支使用 skeleton 模型，不绘制 humanoid 外层。

玩家头颅复用 `SkinCache`。有纹理属性时异步加载，未完成时使用原版 UUID 默认皮肤；头颅请求使用独立缓存标识，避免自定义头颅属性替换同 UUID 在线玩家的皮肤。已有玩家皮肤可以复用。默认/下载图像和已生成头颅纹理不会每帧重新解码。SkinCache 的 HTTPS 来源检查与签名验证边界见 [SKINS.md](SKINS.md)。缺少纹理属性时不新增按名称查询 Mojang Profile 的网络流程。

旗帜采用 `ModelBanner` 的旗面、支柱、横杆，墙旗不绘制支柱。保持原始比例 0.6666667，位置、旋转以及 `(x*7+y*9+z*13)+worldTime+partial` 的余弦飘动公式。整数和浮点运算遵循源顺序。

## 原生旗帜合成

模式注册表来自原始 `TileEntityBanner.EnumBannerPattern`，共 39 种；染料使用 `EnumDyeColor.getMapColor()` 的 16 个颜色，而非羊的 diffuse 颜色。

`LayeredColorMaskTexture` 实际最多处理 17 层，包括底色。原始图案 alpha 只决定该像素是否参与，真正覆盖 alpha 来自图案红色通道。RGB 由最初的 banner_base 像素与 MapColor 相乘；按原始顺序覆盖到结果上，包含 Java2D 的定点预乘、合成与除法舍入。

不认识的 Pattern ID 按原版忽略，越界染料值按原版回落到黑色；非 8 位 RGBA 图案和尺寸不匹配的图案跳过。当前导入器要求 banner_base 为 8 位 RGB/RGBA，对灰度或索引色底图明确报错，避免假装实现 Java 对这些特殊 BufferedImage 类型的量化行为。

生成纹理缓存保留原版 256 种设计和 60 秒闲置淘汰规则，满且都在使用时不创建新设计。网格长期共享，旗帜图案只在新设计出现时合成。原始 NBT 解析另有 1,024 条/16 MiB 原始字节缓存；未变化的数据不重复建树，并遵守原始 NBT 包的 2 MiB 上限。

## 准备与验证

```powershell
python tools/prepare_tile_entities.py --assets assets --mcp ../MCP-919-main
```

导入器生成 `runtime/tiles/patterns.bin`、注册表/颜色/源散列报告和十张破坏纹理。箱子、头颅、banner_base 和图案继续复用现有 `runtime/entities/minecraft` 资源；不改动地形/持物图集或原始 MCP 文件。

已通过 2,023,796 项原生检查：

- 97 份实际执行原始 `LayeredColorMaskTexture.loadTexture` 的合成结果逐字节一致，覆盖 39 种图案、16 种染料、RGB/RGBA 底图、透明度、顺序、17 层上限及忽略规则；另验证导入后的真实底图/图案/MapColor 产生相同结果。
- 518 个原始 tile renderer 场景、5,214 个面。实际执行原始 ModelRenderer、ModelBox、TexturedQuad 和渲染器，用 CPU 记录器替代 GL display list/矩阵/纹理绑定；模型、UV、法线、材质、剔除和破坏纹理矩阵一致，最大矩阵误差小于 6.07e-21。
- 实际执行原始 `TileEntityChest.update` 的 80 tick 与原生箱盖状态一致；测试 World 的声音调用为空操作。
- NBT 改变与缓存失效、错误数据、邻接类型、未知类型/告示牌排除、离线 SkinCache 头颅、玩家皮肤隔离、缓存上限和失败重载保留状态均通过。

```powershell
python tools/export_banner_reference.py
python tools/export_tile_geometry_reference.py
D:/Strawberry/c/bin/g++.exe -std=c++17 -O2 -ffp-contract=off -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I include tests/tile_entities_tests.cpp src/tile_entities.cpp src/world.cpp src/skins.cpp src/protocol.cpp -o build/tile_entities_tests.exe -lwinhttp -lole32 -lwindowscodecs -luuid -lcrypto -lws2_32 -ldnsapi -lz -lstdc++fs -static-libgcc -static-libstdc++
./build/tile_entities_tests.exe .
```

正式构建加入 `src/tile_entities.cpp`，使用项目已有的 World/NBT/SkinCache 及其库。Java 仅用于开发参考导出，测试不创建窗口或音频设备。验证范围是 CPU 计算和图像合成，GPU pass、阴影及最终画面由主渲染器接入后另行验证。

本次未扩展末影箱粒子、其他方块实体、物品持有态的 builtin 渲染或告示牌路径。
