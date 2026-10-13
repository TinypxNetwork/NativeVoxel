# 第一人称手臂与持物

`FirstPersonAssets` 提供原版第一人称矩阵、不可变三角网格与纹理图集。运行时完全使用 C++；Python/Pillow/NumPy 只在资源导入阶段使用。资源来自当前 `assets/resources`，因此会继承已经导入的 1.8.9 资源包覆盖。

```powershell
python tools/prepare_first_person.py --assets assets --mcp ../MCP-919-main
```

输出为 `runtime/first_person/models.bin`、`atlas.rgba`、图集预览和可审查的 JSON 报告。默认包包含 609 个模型、69,624 个顶点、335 个注册物品 ID、66,318 个元数据映射，568 张纹理排列在 1024×512 图集中。ID 60 和 62 在原版 RenderItem 也未注册模型，保留缺失材质行为。

转换器同时输出兼容的 `transforms.bin` 侧文件，供世界掉落物和固定展示复用 GROUND/FIXED 变换。原 NVFP0001 与第一人称接口保持兼容；具体使用方式见 [ITEM_ENTITIES.md](ITEM_ENTITIES.md)。

## 渲染接入

主程序已经接入本模块：在世界绘制后、UI 前使用独立第一人称缓冲绘制，保留透明纹理混合及相应深度写入规则。工具/物品切换、挥手、吃喝、格挡、拉弓输入来自控制器；当前仍未接入下文列出的所有特殊效果。

```cpp
mc::FirstPersonAssets held;
held.load("assets", error);
mc::FirstPersonPose pose;
pose.itemId = selectedItem.id; // 空手为 -1
pose.metadata = selectedItem.metadata;
pose.equip = 1.0f - interpolatedEquippedProgress;
pose.swing = interpolatedSwingProgress;
auto frame = held.build(pose);
```

图集为从上到下的 RGBA8。每个 `FirstPersonDraw` 包含稳定的共享网格指针、OpenGL 列主序矩阵、ARGB 染色和纹理类型。可按网格指针建立 VBO 缓存；每帧只计算姿态矩阵和短绘制列表，不重新挤出轮廓、解码贴图或生成模型。将矩阵应用在第一人称相机空间，使用当前手部 FOV/投影；不要再把玩家世界坐标或实体脚部坐标变换叠加到手臂网格。

`equip` 表示下落量，0 为完全抬起、1 为完全放下。`cameraPitch/Yaw` 与 `armPitch/Yaw` 均为度，后者为插值后的渲染手臂角度。`usingItem`、`use`、`useTicksRemaining`、`maxUseTicks` 和 `partialTicks` 驱动原版使用动画。物品切换的 20 Hz 状态更新、当前/上一帧装备进度、挥手进度以及相机手臂平滑由调用方维护。

纹理类型 `ItemAtlas` 使用本图集，`PlayerSkin` 使用当前玩家 `SkinCache` 的皮肤。`MapData` 供调用方绑定当前地图的 128×128 像素；没有数据时只显示背景并标记 `approximate`。`rightSleeve/leftSleeve` 控制皮肤外层。隐身会隐藏空手/地图双臂，持有的物品继续显示，符合源分支。

绘制时使用原版 alpha 阈值 0.1 和 SRC_ALPHA/ONE_MINUS_SRC_ALPHA 混合；法线和分层染色由调用方着色。`translucent` 对冰、染色玻璃/玻璃板、粘液块指示原版禁止深度写入的分支。普通玻璃为 CUTOUT，保留深度写入。负缩放的模型在导入加载时调整三角形顺序，保持正确剔除。

钟和指南针的原始 64/32 个帧通过 `textureFrame` 选择；世界时间、方向和平滑状态仍由调用方计算。弓按整数使用时长的 1、14、18 tick 阈值选择三种拉弓模型，鱼竿由 `fishingCast` 选择甩线模型。药水、刷怪蛋、皮革等保留五个 tint 索引；`colorOverride` 覆盖第 0 层。普通自动动画另写成现有 `AnimationAtlasCache` 格式：`runtime/animations/first_person.bin`，使用 `load(assets,error,"first_person")` 后将更新矩形上传本图集。

## 原版来源与验证

姿态顺序来自 `ItemRenderer`：相机手臂偏移、普通挥手、装备下落、吃喝、格挡、拉弓、空手以及地图双臂。矩阵继续应用 `RenderItem` 的缩放与 `display.firstperson` 继承、平移、Y/X/Z 旋转、缩放和模型居中。原始源文件在地图绘制之后仍调用通用物品绘制，该步骤也保留。

Steve/Alex 手臂从原版 `ModelPlayer`、`ModelBiped`、`ModelBox` 与 `TexturedQuad` 的尺寸、UV 和模型旋转实现。保留 4/3 像素手臂、0.25 像素外层膨胀、原版年龄 0 时的 ±0.1 弧度手臂偏转，以及地图右臂的持物角度。坐标为原始模型坐标除以 16，Y 向下。

生成类物品使用 `ItemModelGenerator` 的前后面与轮廓挤出：厚度 1/16，检查所有动画帧，alpha 只有 0 才视为透明，按行/列合并跨度，即使同一跨度内有间隙也遵循源行为。方块使用资源 JSON 的真实元素几何。所有生成面都经过现有 FaceBakery 等价转换。

验证记录：

- `tests/first_person_tests.cpp`：16,992 项检查；497 份实际调用原始 ItemRenderer 私有方法取得的矩阵、16 份原始 ModelPlayer 手臂网格，以及模型覆盖、弓/鱼竿/指南针选择、药水染色和地图绘制列表。最大矩阵/手臂误差小于 1.42e-23。
- `tools/verify_first_person_geometry.py`：273 张原版生成类纹理和 15 个非方形、透明孔洞、alpha=1、动画改变内部边界等样本；8,058 个 FaceBakery 面、31,656 项对比通过，最大绝对差小于 4.73e-7。
- `tools/export_first_person_reference.py`：重新编译并调用原始源文件生成矩阵/手臂样本。图形调用由测试专用矩阵记录器代替，不修改 ItemRenderer 公式，不启动图形或声音设备。

```powershell
D:/Strawberry/c/bin/g++.exe -std=c++17 -O2 -ffp-contract=off -I include tests/first_person_tests.cpp -o build/first_person_tests.exe -lstdc++fs -static-libgcc -static-libstdc++
./build/first_person_tests.exe .
python tools/verify_first_person_geometry.py
```

该测试文件直接包含实现以检查内部手臂网格，测试构建不要再次链接 `src/first_person.cpp`；正式程序应正常加入该源文件。

## 此阶段的边界

箱子、头颅、旗帜的 `builtin/entity` 持物模型目前使用带原纹理的近似几何，显式标记 `approximate`；动态旗帜图案和玩家头颅皮肤尚未包含。尚未提供物品附魔流光、屏幕着火/水下/窒息覆盖层。未经调用方提供真实帧的钟/指南针及没有地图像素的数据会标记近似。这里验证的是 CPU 变换和几何，并未宣称各 GPU 驱动的光照、光栅化像素完全相同。
