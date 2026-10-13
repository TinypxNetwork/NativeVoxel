# 方块选框、射线与实体基础判定尺寸

`World::selectionBounds(x,y,z)` 返回世界坐标中的原版选框 AABB。`World::raycast(startX,startY,startZ,endX,endY,endZ,stopOnLiquid,ignoreBlocksWithoutBoundingBox,returnLastMiss)` 返回 `BlockRayHit`：`hit` 为方块命中，`miss` 为请求保留的最后不可碰撞位置，两个字段都为 false 表示没有结果。`face` 对应下/上/北/南/西/东的 0–5，`hitX/Y/Z` 为世界坐标。

选框、射线形状和实体移动碰撞是不同的原版接口。告示牌、火把、植物等没有实体碰撞箱，但仍可被鼠标选中；农田、雪层、仙人掌也不能把移动碰撞箱直接当成选择形状。门、箱子、围墙、围栏、玻璃板、藤蔓会读取相邻状态。1.8.9 楼梯的射线算法按六个八分体检测，和其内外角移动碰撞算法分别实现。

`selection_data.hpp` 由 `tools/export_selection.py` 从原始 Java 方法导出 3,168 个元数据输入，合并为 134 组记录；同时保留选框和射线所用的边界。火把的边界由原始 `collisionRayTrace` 初始化。上下文形状由 `World` 根据当前相邻方块补足。

射线遍历使用原 `World.rayTraceBlocks` 的轴交叉顺序、相等参数取舍、零参数修正、201 次循环上限和 MISS 返回规则。单格求交沿用 `Vec3` 的近乎平行阈值和 `Block.collisionRayTrace` 的面检查顺序。不从视觉模型或移动碰撞箱猜测命中面。

运行以下开发验证：

```powershell
python tools/verify_selection.py
```

它重新编译所给 `World`、`Vec3` 与全部方块源码，以原 Java 实现作为参考。当前 **19,968 行结果精确一致**：3,375 个选框和 16,593 条射线；包含全部有效元数据输入、邻接形状、双层门、告示牌、火把、楼梯角部、藤蔓顶部支撑、流体/碰撞箱过滤/MISS 标志、DDA 边界与循环上限，以及 2,000 条确定种子的混合场景射线。报告在 `build/selection_reference/picking_report.json`。

Java 方块使用共享可变边界，验证在选择前调用原始状态与射线初始化，与实际鼠标指向后查询选框的流程一致。未复制任意、不相关渲染调用留下的历史缓存副作用；未知或非法状态，以及非有限/世界范围外的射线不在一致性保证范围内。

## 实体基础尺寸

`entity_dimensions.hpp` 提供无链接依赖的 `mc::entityDimensions(kind,type)`，kind 0/1/2 分别是玩家、协议生物、协议对象。结果包含：

- `valid`、`width`、`height`、`collisionBorder`、`collidable`。
- `customBounds`：悬挂实体需另外计算朝向相关 AABB。
- `multipart`：如末影龙，父实体与可命中的分部不同。
- `className`：原版实体类型，便于诊断。

`tools/export_entity_dimensions.py` 重新编译 100 个原始实体源文件，调用与生成包相同的构造器和实际 getter，生成玩家、33 个生物类型和 21 个对象类型，共 55 条记录。特别使用下落方块的位置构造器；其仅接受 World 的构造器没有相同尺寸初始化。所有生成后的 C++ 浮点位和碰撞标志均已逐项与 Java 导出数据比较，`tests/entity_dimensions_tests.cpp` 通过。报告在 `build/entity_dimensions/report.json`。

尺寸是成体、普通亚种、史莱姆尺寸 1 的基础值。调用方需要应用元数据与生命周期：幼体/幼年僵尸的基础宽高乘 0.5f，史莱姆和岩浆怪宽高为 `0.51000005f * size`；本份源码中凋灵骷髅为 `0.72f × 2.535f`，远古守卫者为 `1.9975f × 1.9975f`。小型/marker 盔甲架、死亡/旁观状态也需分别处理。物品展示框和拴绳结的名义宽高不表示完整选择 AABB，末影龙父实体不能代替其分部命中箱。

这些接口提供选择与点击的几何依据，不代表完整实体互动、生命状态或全部服务端行为已经实现。
