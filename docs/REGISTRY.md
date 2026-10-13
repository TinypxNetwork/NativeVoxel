# 从原版源码导出的注册表

`include/mc/registry.hpp` 提供原生只读查询；`registry_data.hpp` 是生成文件。再生成命令：

```powershell
python tools/export_registry.py --mcp ../MCP-919-main
```

工具编译所给 MCP 的方块、物品、群系和 Bootstrap 源文件，在无图形的 Java 测试进程中调用真实 getter，然后生成 C++17 常量数据。原生客户端不运行 Java。导出包含 198 个方块、337 个有效物品、18 组去重后的工具强度/采集规则和 61 个群系。原始 JSON、报告和源文件 SHA-256 位于 `build/registry_export`。

方块数据包括实际类名、材质、硬度、工具要求、按元数据的可替换标志、脚步/破坏/放置声音、音量/音高、摩擦、光线不透明度，以及完整方块和材质标志。`World` 使用这些原始属性处理摩擦、材质碰撞、方块连接和高度图。

物品数据包括实际类名、堆叠数、耐久、使用时间/动作、子类型、盔甲位置、`ItemBlock` 对应方块、食物属性和武器属性修饰值。`strength(itemId, blockId)` 与 `canHarvest` 来自实际 Java 方法；`foodHeal(id, metadata)` 和 `foodSaturation` 包含鱼类等不同元数据的差异。

`attackDamage` 是装备的 operation 0 属性修饰值，不包含玩家基础伤害。`armorSlot` 是 `ItemArmor.armorType`：0 头盔、1 胸甲、2 护腿、3 靴子；非盔甲为 -1，南瓜/头颅等特殊装备由物品控制器单独处理。`blockId` 严格对应 `Block.getBlockFromItem`；特殊种子、告示牌、桶等的放置逻辑不能仅靠此字段推导。

群系温度、降雨与草/树叶/水颜色采样于 `(0,64,0)`，采样前加载原版资源包的草地和树叶色图，包含沼泽与恶地覆写。它是基准色；完整高度温度噪声、沼泽空间噪声及相邻群系混色仍由渲染/世界模块进一步处理。

`tests/registry_tests.cpp` 验证编译和查询接口、雪层替换、黑曜石采集等级、工具耐久、弓/牛奶使用动作、食物元数据、盔甲位置与注册数量。
