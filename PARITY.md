# Minecraft 1.8.9 移植核对记录

参考：用户提供的 `MCP-919-main/src/minecraft/net/minecraft`（MCP 9.19 / Minecraft 1.8.9）。核对日期：2026-10-15。原参考目录只读，不随本仓库发布。

这是按实际代码入口整理的功能核对，不是按 Java 类数量计算的完成率。NativeVoxel 已具备原生客户端的主要链路，但仍未实现完整的单人生存和原版服务器模拟。

## 本轮补齐

| 原有缺口 | 本轮实现 | MCP 行为参考 |
| --- | --- | --- |
| 箱子等容器不能拆除；非空熔炉、酿造台被拒绝破坏 | 容器库存拆分掉落、NBT 保留、机器状态清理，保留双箱另一半和末影箱个人库存 | `inventory/InventoryHelper.java`、`block/BlockChest.java`、`BlockFurnace.java`、`BlockBrewingStand.java`、`BlockHopper.java`、`BlockDispenser.java`、`BlockEnderChest.java` |
| 原版存档里的 Item/XP 只保留在原始 NBT，不能拾取；新增掉落物只存在于原生附加存档 | 标准 Anvil `Entities` 与掉落物/经验球模拟互通，处理附加存档优先级和流式加载，保存时更新当前实体状态 | `world/chunk/storage/AnvilChunkLoader.java`、`entity/Entity.java`、`entity/item/EntityItem.java`、`entity/item/EntityXPOrb.java` |
| S36 告示牌编辑请求被忽略 | 告示牌四行编辑、Unicode 输入、文字宽度限制和 C12 提交，并接入本地放置流程 | `client/network/NetHandlerPlayClient.java`、`client/gui/inventory/GuiEditSign.java`、`network/play/client/C12PacketUpdateSign.java`、`item/ItemSign.java` |

对应实现集中在 `simulation_entities.cpp`、`simulation_containers.cpp`、`simulation_anvil.cpp`、`world_io.cpp`、`local_session.cpp`、`game.cpp`、`controller.cpp` 和 `main.cpp`。

## 仍缺少的系统

| 范围 | 当前状态与剩余工作 |
| --- | --- |
| 生物与生存 | 已有基础玩家物理、食物、伤害和部分投射物。仍缺完整自然刷怪、AI、寻路、繁殖、战斗、生存交互、睡眠和重生流程的全面对照。 |
| 方块更新 | 光照、熔炉和酿造已接入。仍缺通用计划刻、完整红石、活塞、流体传播、重力方块，以及其余成对方块和方块实体的生命周期。 |
| 容器自动化 | 容器存取与本轮玩家破坏时的库存掉落可用。比较器通知、漏斗运输、发射器/投掷器触发行为及其红石时序仍未实现。 |
| 世界生成 | 已有三维度基础地形及多项独立生成算法。主世界/下界完整 population、结构、矿物和植被装饰仍需按原版随机数及相邻区块顺序接入实际游戏。 |
| 维度与载具 | 仍缺完整传送门创建/搜索/跨维度流程、载具控制、实体推挤及乘坐行为对照。 |
| 存档 | 本轮处理 Item/XP；其余生物和载具等实体仍主要保留原始 NBT，不构成本地模拟。附加存档仍保存原生模拟内部状态。快照仅包含已加载区块。流式加载遇到无法导入的实体时拒绝保存，避免静默删除原始实体；成功重载后恢复保存。 |
| 联机 | 已有协议 47 主要世界、实体、背包和聊天同步。本轮接入 S36/C12；地图、床上睡眠表现、统计、部分实体交互和服务器资源包下载/启用仍待补齐。 |
| 物品 | 已有合成、熔炼、酿造、部分投射物和掉落。完整钓鱼计时/战利品、鸡蛋孵化、更多物品右键行为仍未完成。 |
| 画面与声音 | 已有实体、装备、粒子、资源包热载、动态光照和声音。地图显示、部分实体特殊渲染、末影龙死亡流程、天气/粒子/音乐的全面对照，以及高于 1 级的 mipmap 仍有缺口。 |
| 外部服务 | 无内置微软账号登录界面；正版会话由环境变量提供。废弃的 Twitch/Realms 网络服务未恢复。 |

上述缺口中，完整红石、生物 AI 和结构生成需要单独的实现与参考验证，不能由本轮三个功能的测试结果推断为已经完成。也不能据此保证所有服务器或反作弊插件兼容。

## 可重复验证

使用 README 中的 Windows MinGW-w64、zlib 和 OpenSSL 环境，在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File tests/run.ps1
```

这个入口从当前源码重新编译并运行协议、模拟、Anvil、本地会话和告示牌五组回归，不需要 Minecraft 资源、MCP 或 Java。它会创建独立的 `build/parity-*` 目录，保留可执行文件和 `results.json`；测试只使用独立构造的存档。

本轮五组全部通过：协议/移动 2,212 项、模拟 470 项、本地会话 170 项、告示牌 42 项，以及 Anvil 读写全组。存档回归包含无敌物品的伤害保护、保存重载、实体导入失败时的保存保护，以及容器掉落和告示牌编辑的跨模块往返。

原版 Java 对照工具和图形资源属于本地开发环境；上面的公共回归入口不依赖它们。完整客户端的编译命令见中英文 README。

本轮验证还包括 GCC 8.3 完整客户端构建、告示牌隐藏窗口渲染，以及本机协议 47 测试服务器收到真实 GUI 中文/emoji 输入、换行、退格和 ESC 提交的 C12 数据包。容器库存内核与原 MCP `InventoryHelper.java` 对照了 512 个容器、15,031 堆掉落和 90,186 个数值；整数、偏移和随机状态一致，浮点最大绝对误差为 `5.55e-17`（容差 `5e-16`）。这些结果只覆盖上述行为。
