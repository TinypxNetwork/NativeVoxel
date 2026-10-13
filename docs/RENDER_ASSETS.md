# 原版界面、字体、物品图标与纹理动画

本模块提供 `include/mc/render_assets.hpp`、`src/render_assets.cpp` 和 `tools/prepare_render_assets.py`。所有可见图像来自已导入的 Minecraft 1.8.9 原资源；没有生成替代材质。图标颜色和物品模型注册表来自实际执行原 MCP getter 的开发期导出结果。

正常构建流程：

```powershell
python tools/prepare_assets.py --mcp E:/Minecraft_For_C/MCP-919-main --output assets
python tools/prepare_render_assets.py --mcp E:/Minecraft_For_C/MCP-919-main --assets assets
python tests/render_assets_tests.py
```

这两步需要 Python、Pillow 与 NumPy。客户端运行不需要 Python 或 Java。重新执行源数据参考导出才需要 JDK：`python tools/export_render_assets.py`；已有的 `tools/reference/item_render_registry.json` 与 `font_width_reference.bin` 足够支持普通构建和测试。

## 原界面图像

所有 GUI PNG 保留原目录层级并转换为 NVTX：4 字节 `NVTX`、小端 uint32 宽/高，随后是自上而下 RGBA8。通用位置是 `assets/runtime/gui/<原 textures/gui 下的路径>.rgba`。同时提供便于应用层调用的别名：

| 别名（位于 runtime/gui，后缀 .rgba） | 原图 |
| --- | --- |
| minecraft / mojang | title/minecraft.png / title/mojang.png |
| options_background | options_background.png |
| panorama_0 … panorama_5 | title/background/panorama_0.png … panorama_5.png |
| inventory / crafting / chest | container/inventory.png / crafting_table.png / generic_54.png |
| furnace / dispenser / hopper / anvil | 对应 container 原 PNG |
| brewing / enchanting / beacon | brewing_stand.png / enchanting_table.png / beacon.png |
| horse / villager | 对应 container 原 PNG |

`runtime/gui/container/` 包含全部原容器图像及 creative_inventory 子目录。`runtime/gui/index.json` 和 `index.txt` 提供名称、路径和尺寸清单。导出仅恢复原图；布局、按钮行为、容器内容和动画摄像机由主界面/渲染器负责。

## 字体接口

`UnicodeFont::load` 读取原 65,536 字节 `glyph_sizes.bin`、256 项 ASCII 映射和字宽表。全部 222 张原 Unicode 页面均在 `runtime/font/unicode_page_XX.rgba` 中；`index.txt/json` 标明可用页。页面图片可以由渲染器按需加载。

`glyph(codepoint,forceUnicode)` 返回 `GlyphMetrics`：

- `page=-1` 为 ASCII，0–255 为 Unicode 页面，−2 表示没有图形。
- `u0/v0/u1/v1` 为归一化纹理坐标；`width/height` 是原字体像素单位下的绘制尺寸。
- `advance` 是 `renderChar` 对应的绘制推进值；`measuredWidth` / `charWidth` 对应 `FontRenderer.getCharWidth`。原版二者并不总相等，因此分别保留。
- 空格宽度 4，段落符号 `§` 的测量结果为 −1，留给调用方处理格式控制码。Unicode 字形高度 7.99，源图采样高度 15.98；保持原 1.8.9 的细小 UV 偏移。

开发期参考导出直接调用原 `FontRenderer.getCharWidth`，对全部 65,536 个 UTF-16 单元分别检查普通与强制 Unicode 模式，共 131,072 项结果，原生实现全部一致。

有一个明确的显示修正：原 glyph_sizes 中 50 个字节的最高位为 1。原 Java `byte >>> 4` 会先符号扩展，产生极大的错误 UV；本模块按该文件的两个无符号半字节解释，避免出现错误的大四边形。这些字形的 `correctedLegacyBounds=true`，完整测量宽度仍与原 Java 一致。常见中文页面不依赖该修正。双向文字重排、阿拉伯文字塑形和格式码布局不由此低层资源接口实现。

## 物品图标

`ItemIconAtlas::load` 读取 `runtime/items/icons.bin` 和原始 RGBA 图集。`icon(itemId,metadata,appearance)` 返回图层数组，每层含图集 UV、ARGB 颜色和 tintIndex；按顺序绘制所有图层。

默认导出 337 个注册物品中的 335 个可见图标 ID、66,318 个元数据映射、624 个图标模型。ID 60（farmland）和 62（lit_furnace）在原 `RenderItem` 中也没有注册图标，因此保留原版缺失材质行为。

`RenderItem` 的静态注册和五个动态 ItemMeshDefinition 由原 Java 代码执行导出，避免手写猜测模型名或枚举元数据。颜色由原 `Item.getColorFromItemStack` 取得；包括全部 65,536 个药水元数据颜色、刷怪蛋主/副色、默认皮革颜色和其他 tint。该参考数据为原默认包，纹理与 JSON 模型仍按当前资源包重新转换。

生成类平面物品保持原纹理图层，药水液体、瓶体、皮革覆盖层、刷怪蛋斑点等不会被压成单色。`ItemAppearance.color` 可覆盖第 0 tint 层的 RGB，例如 NBT 皮革染色或自定义药水颜色。完整 NBT 解释由应用层负责。

可损耗物品使用元数据 0 选择通常图标，对应 `ItemModelMesher.getMetadata`。`ItemAppearance.frame` 可单独选择弓的四种姿态、鱼竿收线/抛线图标，以及时钟和指南针的原纹理帧，共 102 个可选帧；时钟时间与指南针方向的状态计算仍需调用方提供。

有 JSON 立体模型的方块物品使用原 `RenderItem.setupGuiTransform`、模型 GUI 变换及 `RenderHelper` 光照方向，在开发资源准备阶段烘焙成小图。保持模型继承、面 UV、旋转与原 item tint。此离线栅格化不声称与各种 OpenGL 驱动的逐像素结果完全相同。

箱子、头颅、旗帜等 builtin/entity 图标采用原纹理的近似几何，标记 `approximate`。动态旗帜图案、玩家头皮肤、附魔流光和资源包自定义色表导致的逻辑颜色变化未在此接口中完整实现；数量和耐久条也由界面单独绘制。所有限制都在 `runtime/items/report.json` 中列出。

## 动画图集更新

`AnimationAtlasCache::load(assetDirectory,error,"terrain")` 读取 `runtime/animations/terrain.bin`。`sample(gameTick)` 使用整数游戏 tick（20 Hz），返回当前需要变更的纹理矩形。将每个 `AtlasUpdate` 上传到原地形图集的 x/y 位置即可；矩形已经包含挤出的边缘 padding。

九种原地形动画完整保留 PNG 帧序列、显式 frame 列表、单帧持续时间、循环和 interpolate 标记。插值遵循原 `TextureAtlasSprite`：RGB 按 double 权重插值后截断为整数，alpha 保留当前帧值，不做 alpha 插值。相同帧、相同像素和重复 tick 不产生多余上传；可直接跳到任意游戏 tick。

Padding 使用最近邻挤出并逐字节保留 RGBA；避免 Pillow 默认缩放的预乘 alpha 舍入改变半透明边缘。测试验证第 0 tick 的每一块动画更新矩形与基础地形图集完全相同。普通自动生成的完整 mip 链仍会混合不同精灵；渲染器应限制 mip 级别或单独生成精灵 mip。

## 验证记录

`tests/render_assets_tests.py` 检查原 GUI/Unicode 页像素、原 Java 全字符宽度、原物品模型/元数据/颜色、生成类纹理像素不变、源模型 GUI 变换继承、动画边界与循环、重复帧、RGB/alpha 插值及半透明 padding。原生接口测试临时编译 C++ 程序运行，不创建窗口或音频设备。GCC 8.3 使用 `-Wall -Wextra -Wpedantic` 编译通过。
