# 掉落物与经验球渲染数据

`item_entities.hpp/.cpp` 提供原版 `RenderEntityItem`、`RenderXPOrb` 的 CPU 绘制数据。它不更新实体位置、拾取、经验、合并、寿命或任何网络状态。渲染器取得模型、列主序矩阵、纹理类别、ARGB 和光照后负责 GPU 提交。

## 接入

```cpp
mc::DroppedItemPose itemPose;
itemPose.itemId = stack.id;
itemPose.metadata = stack.damage;
itemPose.count = stack.count;
itemPose.ageTicks = age;
itemPose.partialTicks = partialTick;
itemPose.hoverStart = hoverStart;
itemPose.x = renderX; itemPose.y = renderY; itemPose.z = renderZ;
auto itemFrame = mc::ItemEntityVisuals::dropped(firstPersonAssets, itemPose);

mc::XPOrbPose orbPose;
orbPose.amount = entity.xpValue;
orbPose.xpColor = colorTicks;
orbPose.partialTicks = partialTick;
orbPose.viewYaw = playerViewY;
orbPose.viewPitch = playerViewX;
orbPose.x = renderX; orbPose.y = renderY; orbPose.z = renderZ;
orbPose.packedLight = ordinaryEntityLight;
auto orbFrame = mc::ItemEntityVisuals::orb(orbPose);
```

坐标必须是插值后的、相对于渲染原点的坐标，转换在源代码相同的位置降为 float。角度与原版 `RenderManager.playerViewY/playerViewX` 一致，单位为度。掉落物 `hoverStart` 是客户端创建实体时的持久随机相位，单位为弧度；不要每帧重新随机。年龄、颜色计数与部分 tick 都由调用方提供。

掉落物的原始 Slot 字节位于 `Game::Entity::metadata[10]` 的 type 5 值，由调用方通过已有 Slot/NBT 读取器解码。S11 的经验数量由网络模块保存在 `Entity::xpValue`；旧 `type` 赋值保留兼容，但新代码应使用明确的数量字段。

`ItemEntityTexture::ItemAtlas` 使用已经加载的第一人称物品图集；所有复制体继续共享同一网格指针，可复用同一个 VBO。`ExperienceOrb` 使用已有导入纹理 `runtime/entities/minecraft/experience_orb.rgba`。经验球每个贴图档位的网格只生成一次，颜色变化不重新生成几何。

`ItemEntityFrame::packedLight` 是原版 `(sky<<16)|block` 形式的光照，分量通常为 0..240。掉落物保留输入，经验球按源方法增强方块光。两种实体都返回源阴影参数 size=0.15、opacity=0.75，阴影实际绘制由调用方负责。

## 物品显示变换与兼容性

资源转换器 `tools/prepare_first_person.py` 新增 `runtime/first_person/transforms.bin`，保存每个模型的 `gui3d` 与经过继承/钳制的 GROUND、FIXED 平移、旋转、缩放。NVFP0001 原有模型和图集格式没有改变。609 个模型记录中包括缺失模型及钟/指南针帧。

`FirstPersonAssets::itemModel(id,metadata,ItemModelTransform::Ground)` 返回共享几何、分层颜色及 `GROUND * RenderItem缩放/居中` 矩阵；`Fixed` 返回对应固定展示变换。`None` 只应用 RenderItem 自身的缩放/居中。这些接口没有第一人称额外的 2 倍缩放。`groundScale` 单独保留，因为悬浮高度与平面堆叠间距会使用它。

旧缓存没有侧文件时，原有 `load()` 和第一人称 `build()` 继续正常工作，`worldModelsReady()` 返回 false；新增世界物品接口不生成未定义的绘制。侧文件存在时校验模型数量、名称和数值。负缩放所需的相反三角顺序单独缓存，不影响同一模型在第一人称或其他显示模式中的网格。

## 源规则

掉落物数量 1、2～16、17～32、33～48、49 以上分别显示 1、2、3、4、5 份。悬浮量为 `sin((age+partial)/10+hoverStart)*0.1+0.1`，并增加 `0.25*groundScaleY`；自转角为 `((age+partial)/20+hoverStart)*(180/pi)`。MathHelper 使用原版查表正弦。

立体物品每次绘制重设 Java Random 为种子 187，第一份没有随机偏移，其他份三个轴分别取 `((nextFloat()*2)-1)*0.15`，随后缩放 0.5。平面物品先沿 Z 移动 `-0.046875*(copies-1)*0.5`，每画一份再移动 `0.046875*groundScaleZ`；保留源代码中初始偏移不乘 groundScale 的顺序。

经验球使用阈值 3、7、17、37、73、149、307、617、1237、2477，选择 64×64 图集中 4×4 排列的 16×16 格。模型的原始四角为 x=[-0.5,0.5]、y=[-0.25,0.75]，缩放为 0.3，朝向为 `Ry(180-viewYaw)*Rx(-viewPitch)`。保留源法线 (0,1,0)，而非根据四边形重新计算。

经验球相位 `(xpColor+partial)/2` 驱动红色和蓝色；绿色恒为 255，alpha 恒为 128。红色公式为 `(sin(phase)+1)*0.5*255`，蓝色为 `(sin(phase+4.1887903)+1)*0.1*255`，均按 Java float 运算后截断。方块光增加 120，上限 240，天空光保留。

掉落物源渲染会开启 alpha 混合并保留深度写入，与第一人称透明方块关闭深度写入的路径不同。XP 源提交 alpha=128，但本模块不替调用方设置全局混合/光照状态。CPU 数据的正确性不等同于 GPU 光栅化、光照已完成验证。

## 验证与构建

`tools/export_item_entity_reference.py` 重新编译并执行原始 `RenderEntityItem`、`RenderXPOrb`、`EntityXPOrb`、`EntityItem` 和模型变换源码。GL 调用记录矩阵，模型绘制入口记录每份复制体；嵌套显示变换另用原始 `ModelBlock` 继承解析核对。XP 的 Entity 基类只提供受控普通光照，实际增亮仍运行原始 `EntityXPOrb.getBrightnessForRender`。

- 272 个掉落物场景、977 个复制矩阵与 Java 完全一致，覆盖数量边界、平面/立体、显示缩放、年龄、相位与原点。
- 728 个经验球场景的贴图档位、颜色、alpha、光照、朝向矩阵、四角 UV/法线完全一致，含随机相机角度和坐标。
- 608 个实际模型的 GROUND/FIXED 共 10,944 个分量与原版解析完全一致。
- `tests/item_entities_tests.cpp` 通过 72,012 项检查，另验证 335 个物品 ID、网格共享、药水多层染色、负缩放独立绕序、旧缓存兼容和损坏侧文件拒绝。
- 原有 16,992 项第一人称检查仍通过。

```powershell
python tools/prepare_first_person.py
python tools/export_item_entity_reference.py
D:/Strawberry/c/bin/g++.exe -std=c++17 -O2 -ffp-contract=off -I include tests/item_entities_tests.cpp src/item_entities.cpp src/first_person.cpp src/world.cpp -o build/item_entities_tests.exe -lstdc++fs -static-libgcc -static-libstdc++
./build/item_entities_tests.exe .
```

正式程序加入 `src/item_entities.cpp`，复用已有 `first_person.cpp` 和 `world.cpp`（MathHelper 正弦）。不新增运行时第三方依赖，也不需要 Java/Python 或音频设备。

箱子、头颅、旗帜沿用已有的 builtin/entity 近似模型并保留 `approximate` 标记。动态旗帜图案、附魔流光和物品实体名称标签未在本次数据模块中新增；FIXED 导出提供模型变换，并未单独实现完整物品展示框逻辑。
