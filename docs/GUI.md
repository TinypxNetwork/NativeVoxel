# 原生物品栏与容器界面

`InventoryScreen` 位于 `include/mc/gui_inventory.hpp` 和 `src/gui_inventory.cpp`。主程序在物品栏页面调用 `draw(renderer,game,controller,window,i18n,scale)`；返回 true 时由主程序关闭窗口并恢复鼠标捕获。打开界面的那次 E 按键不会立即将其关闭。已经打开的服务器容器被服务器关闭后，界面也会退出。

所有普通容器操作交给 `Controller::clickSlot`，界面不会独立计算交易结果或绕过其槽位规则。`InventoryLayout::hit` 和 `InventoryScreen::interact` 可独立测试，不需要创建图形窗口或连接服务器。

## 普通物品栏

- 玩家界面保留原四个装备槽、2×2 合成格、结果槽、背包、快捷栏和玩家模型位置。
- 箱子使用原版 1–6 行背景拼接和槽位坐标。
- 工作台、熔炉、酿造台、漏斗、发射器/投掷器、铁砧、附魔台、信标、村民与马匹界面使用相应原材质及原容器槽位坐标。
- 显示原物品图标、数量、耐久条、鼠标指向高亮、光标持有物品和提示框。熔炉进度读取服务器窗口属性。
- 普通点击、右键分半/放一个、Shift 转移、数字键交换、Q/Ctrl-Q 丢弃、双击收集和左右拖动分配经过 Controller 的原有逻辑，并发送对应的 C0E 点击窗口数据。

专用容器控件也已接通原有控制器：

- 铁砧使用原位置的名称框和背景，支持 Unicode 输入、Ctrl-A、左右/Home/End、Backspace/Delete，限制为原版 30 个 UTF-16 单元。普通默认名称规范化为空重命名请求；实际名称通过 `Controller::renameItem` 发送 `MC|ItemName`，显示控制器计算的结果与经验费用。文本框聚焦时 E 是可输入字符。
- 村民界面显示选中交易的两项输入、输出、耗尽标记及原左右箭头。上一项/下一项调用 `selectTrade`，发送 `MC|TrSel`；首尾禁用按钮不发送操作。
- 附魔台三项选择使用原背景、等级图标和标准银河字母纹理。字词使用原 EnchantmentNameParts 词表及 Java 随机序列，由窗口 XP seed 决定。等级、青金石和物品条件按控制器检查，点击经 `enchantItem` 发送 C11；界面不会在服务器结果之前自行扣除材料。鼠标悬停显示服务器给出的附魔线索与费用。
- 酿造进度条和气泡使用原倒计时公式及材质区域。
- 信标按钮按等级开放主/次效果，选择时只修改本地窗口选项；确认且有付款物品时发送 `MC|Beacon` 的主/次效果，取消不应用效果。两者均通知主程序关闭界面。

物品名称优先采用开发期从 `Item.getUnlocalizedName(ItemStack)` 导出的翻译键，因此 `diamond_sword` 会查找正确的 `item.swordDiamond.name`。生成文件为 `runtime/items/names.txt`。NBT `display.Name` 优先于普通名称；显示 Lore，读取皮革 `display.color`，识别 Unbreakable 并隐藏其耐久条。NBT 解析使用项目统一解析器，不将 NBT 内容作为代码执行。

## 创造模式

目录来自原 `CreativeTabs.displayAllReleventItems` 和 `Item.getSubItems` 的实际执行结果，保留原顺序、元数据和 NBT。包含 12 个页签，建筑页 158 项，搜索目录 599 个原版子物品。附魔书、药水、刷怪蛋和旗帜的原始堆栈标签均保留。

原页签、目录、搜索栏、滚动条和快捷栏使用 `creative_inventory` 原纹理。创造模式目录只在 `Game.gameMode == 1` 且 `Game.creativeMode` 为真、玩家已生成并且没有普通服务器容器打开时启用。界面不会自行授予创造权限。

在创造目录中点击物品会选择本地光标堆栈；Shift 选择最大堆叠，数字键将最大堆叠放入对应快捷栏。实际槽位变更使用 C10 创造槽位数据；每个拖经槽位按原代码同步，拖动结束再同步九个快捷栏槽位，因此拖经两个槽位会产生 11 个 C10。普通生存物品栏和已打开的服务器箱子不会发送创造槽位数据。

创造模式的槽位预测通过 `Controller::clickSlot(...,notifyServer=false)` 执行，然后同步改变的槽位。该调用不会发送 C0E，也不会消耗 C0E 交易序号。

开发期重新生成目录：

```powershell
python tools/export_creative_catalog.py
python tools/export_render_assets.py
python tools/prepare_render_assets.py
```

普通构建只使用已保存的 `tools/reference/creative_catalog.bin` 和物品名称参考数据，不需要 Java。资源准备将目录复制到 `assets/runtime/gui/creative_catalog.bin`。来源、哈希和分类数量保存在同目录的参考 JSON 记录中。

## 测试与限制

`tests/gui_inventory_tests.cpp` 使用真实 Game、Controller 和协议序列化，但将 Renderer 替换为无图形测试桩，不连接服务器。968 项检查覆盖槽位/专用控件边界、光标变化、C0E 点击及拖动、C10 创造更新与权限、C11 附魔选择、C17 重命名/交易/信标数据、原目录、NBT 名称和 Unicode 编辑。`render_assets_tests.py` 另校验标准银河字体原图及其它资源。

此实现仍不是全部 GuiContainer 系列的逐行移植：附魔台三维书本动画、马匹模型预览、附魔流光、触屏操作、完整高级提示以及全部创造模式特殊拖动动作仍需完善。搜索支持键盘文字输入、ASCII 不区分大小写及中文原文匹配，尚未实现全部剪贴板、鼠标文字选区和控件按键音行为。文字编辑保持合法 UTF-8 边界，补充字符的删除以完整码点为单位。

功能与数据包测试不等于对所有服务器插件或反作弊系统的兼容性认证。
