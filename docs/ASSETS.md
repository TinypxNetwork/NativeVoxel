# 1.8.9 资源导入与 C++ 模型缓存

`tools/setup_assets.py` 是完整资源准备入口，`build.ps1` 默认调用它。首次运行从 Mojang 官方 HTTPS 地址下载 Minecraft 1.8.9 的版本记录、客户端 JAR、1.8 资源索引及其对象，并下载包含 OpenAL64.dll 的 Windows natives 归档。下载量约 118 MiB，缓存位于 `.cache/minecraft/`，输出位于 `assets/`。版本记录固定到官方 SHA-1，后续文件按记录里的 SHA-1 和大小校验；通过校验的缓存不会重复下载，损坏文件会重新获取。下载最多重试三次，校验完成后才原子替换目标文件。

普通构建使用仓库里的小型注册表、字符映射、旗帜和药水常量，不依赖 MCP 或 Java。常量由 `tools/export_asset_constants.py` 从参考源码导出，并记录源码哈希。资源准备需要 Python 3.10+、Pillow 和 NumPy，安装命令为 `python -m pip install -r requirements-dev.txt`。资源准备完成后，游戏 EXE 不需要 Python 或 Java。

```powershell
python tools/setup_assets.py
python tools/setup_assets.py --offline
# 可选：使用已有 MCP 目录，全程只读该目录，不执行下载。
python tools/setup_assets.py --mcp D:/MCP-919-main
python tests/assets_tests.py
```

默认包之外，可以添加 1.8.9 `pack_format: 1` 的 ZIP 或目录。多次传入 `--pack`，后面的优先级更高；改动资源包后重新运行导入程序。

```powershell
python tools/setup_assets.py --pack D:/packs/example.zip --pack D:/packs/local-overrides
```

## 资源保存与校验

- 自动下载模式先使用固定的 Mojang SHA-1 校验版本记录，再验证客户端、索引、每个资源对象及 Windows natives。`--offline` 禁止下载，遇到缺失或损坏缓存会报错。`--cache` 可指定缓存目录，`--workers` 可设置 1–32 个并发下载任务，默认 8 个。
- 使用 `--mcp` 时，底层 `prepare_assets.py` 只读取本地文件，不访问网络或修改 MCP 源文件。客户端 JAR 的 SHA-1 和长度必须与该目录中的 `1.8.9.json` 一致，索引与对象也会校验；此模式以用户提供的版本记录为依据，不独立验证该记录的来源。
- 索引中每个对象都校验 SHA-1 和长度，并恢复原来的逻辑资源名。缺失或损坏默认导致导入失败，详细信息写入 `import-report.json`。`--allow-missing` 仅用于允许缺失的索引资源继续导入，不能跳过 JAR、索引或模型纹理完整性检查。
- 原 JAR 资源保存在 `assets/vanilla/jar/`；索引资源保存在 `assets/vanilla/index/`；原索引、版本记录和文件校验清单也保留。资源包原文件另外保存在 `assets/resourcepacks/<优先级>/`。
- `assets/resources/` 是应用覆盖后的资源树。例如原皮肤在 `assets/resources/assets/minecraft/textures/entity/steve.png`。
- 音效 OGG、`sounds.json`、所有语言、字体页、物品/实体/界面纹理和模型原文都保留。资源文件完整导入不等于相关游戏功能已经全部实现。
- ZIP 路径穿越、Windows 特殊路径、大小写冲突会被拒绝。再次导入时仅依据上一次清单移除已经不再生效的受管理资源文件。

首次本地导入结果：3,086 个 JAR 资源文件、734 个索引条目，全部通过校验；有效资源 3,820 个，包含 578 个 OGG 音频和 149 个语言文件。准确结果以生成的报告为准。

## 模型转换

模型转换参考以下原源码：

- `Block.java::registerBlocks` 提供全部 198 个旧版方块 ID。
- 各 `Block*.java::getStateFromMeta` 与 `BlockModelShapes::registerAllBlocks` 提供元数据与模型状态映射。
- `EnumFaceDirection`、`BlockPart::getFaceUvs`、`BlockFaceUV`、`FaceBakery` 和 `ModelRotation` 提供顶点顺序、默认 UV、UV 旋转、元素旋转/缩放和 UV 锁定规则。
- `WeightedBakedModel` 和 `MathHelper::getCoordinateRandom` 提供权重排序与位置随机选择。C++ 显式模拟 Java 整数溢出，避免依赖有符号溢出的未定义行为。

导入器生成全部 198 × 16 个元数据映射，并转换资源树中所有 blockstate JSON 的每一个 variant，包括围栏连接、楼梯内外角、门、红石线路、植物等邻接状态。基础状态保留 Java 的默认实际状态；世界中的邻接信息需要调用方传给 `Assets::modelWithProperties`，仅调用 `model(state)` 不足以处理这些状态。

`modelWithProperties` 接受已映射的属性键，例如 `north=true`、`shape=inner_left`、`snowy=true`。门的上下半部分必须结合相邻方块的朝向、开关和铰链。双层植物上半部必须依据下半部选择植物类型；花盆内容需要对应的方块实体数据。接口不会执行方块游戏逻辑。

模型继承遵循 1.8.9：有父模型时使用父元素与父环境光遮蔽设置，子纹理引用覆盖父纹理；同时指定非空 `elements` 与 `parent` 会被拒绝。未限定命名空间的资源引用默认使用 `minecraft`。不支持后续版本引入的 multipart blockstate 格式。

所有网格顶点为方块内坐标，1 等于一米；UV 是图集中归一化坐标，PNG 顶部对应 v=0。四边形从外部看逆时针排列。方向顺序与 `EnumFacing` 一致：下、上、北、南、西、东。`cullFace=-1` 代表不依邻接方块隐藏；`tintIndex` 保留原值，由渲染端应用生物群系/红石/植物颜色。

不合法的元数据不是原版有效状态。例如年龄超过 7 的小麦会使 Java 状态属性校验失败；原生缓存为这些输入生成默认模型并在 `state-models.json` 标记 `invalid_metadata`。这不是宣称异常输入行为与 Java 完全一致。

## 原生文件格式

`assets.bin` 使用小端字节序，不直接序列化 C++ struct，因此不依赖编译器对齐：

| 内容 | 编码 |
| --- | --- |
| 文件标识 | 8 字节 `NVASSET1` |
| 图集宽高、字体宽高、模型/variant/状态数量 | 7 个 uint32 |
| 模型 | uint8 渲染层，uint8 flags，uint16 保留，uint32 四边形数量 |
| 四边形 | int8 朝向，int8 cullFace，int16 tintIndex，uint8 shade，3 字节保留，3 float 法线，4 × 5 float 顶点 x/y/z/u/v |
| Variant | 长度前缀 UTF-8 资源名、属性串，uint32 选项数量，逐项 uint32 模型编号/权重 |
| 旧版状态 | 长度前缀 UTF-8 方块名、资源名、属性串，uint32 variant 编号 |

字符串长度前缀为 uint32。模型 flags 位 0/1/2/3 分别是 `fullCube`、`occludes`、`ambientOcclusion`、`approximate`。渲染层 0/1/2 分别为不透明、透明裁剪、半透明。

`atlas.rgba` 与 `font.rgba` 是无头部 RGBA8，尺寸来自 `assets.bin`。图集默认每边挤出 2 像素，UV 向内缩 0.01 像素。直接生成完整 mip 链会在低分辨率级别混合邻近纹理；建议默认使用 `GL_NEAREST`，若使用普通 mip 生成，应按 `atlas.json` 限制 mip 层数或由渲染器另做分块 mip。

`runtime/steve.rgba`、`runtime/gui/icons.rgba`、`runtime/gui/widgets.rgba`、`runtime/gui/inventory.rgba` 的头部为 4 字节 `NVTX`、uint32 宽、uint32 高，随后是自上而下 RGBA8 像素。

`Assets::load` 检查格式、维度、表大小、索引、权重、浮点数和文件长度；加载失败保留上一次成功加载的资源。`atlas.json`、`state-models.json`、`model-variants.json`、`model-report.json` 可用于检查输出。`source-inventory.json` 列出全部原 Java 文件的包领域、路径、SHA-1、字节数和行数，并明确区分已导入资源与尚未移植的游戏逻辑。

## 尚未达到原版一致的部分

- 流体目前是按 level 生成的静态高度模型；没有原版四角高度混合、流向 UV 与邻接液体完整剔除。
- 箱子、告示牌、头颅、旗帜、末地传送门与移动活塞使用明确标记的近似或空模型；方块实体渲染、动画、告示牌文字、旗帜图案、双箱连接仍需独立移植。
- 基础图集以原动画序列第一帧初始化；独立 [渲染资源模块](RENDER_ASSETS.md) 提供 20 Hz 动画、原插值规则与带 padding 的更新矩形，由渲染器上传。
- 环境光遮蔽开关、面着色与 tint 索引保存在缓存中；实际光照计算是否实现取决于渲染模块。渲染器实现细节以项目 README 为准。
- 原生物品栏图标、ASCII/Unicode 字符宽度与字形坐标由独立 [渲染资源模块](RENDER_ASSETS.md) 提供。完整物品持握视角、全部动态实体层、复杂文字排版与运行时资源包重载仍未全部覆盖。声音事件解析、OGG 解码、空间播放和流式播放由独立 [音频模块](AUDIO.md) 提供。
- 模型转换使用 Python 浮点运算后写入 float32，未声称与 Java FaceBakery 的每一步 float32 舍入逐位相同。渲染资源测试不验证网络/运动/反作弊兼容性。

这些限制明确保留在报告中。本模块提供可检查的资源导入和模型转换基础，不代表整个 Minecraft 已经完整移植。

## 原生实体模型模块

`include/mc/entity_models.hpp` 和 `src/entity_models.cpp` 提供独立的 `EntityModels`，不依赖世界、协议或 OpenGL。`load(assetsDirectory,error)` 读取导入器生成的 `runtime/entities/index.txt` 与 NVTX 纹理。默认包的全部 142 张实体 PNG 都转换为 RGBA，同时保留原 PNG；解码像素与原图逐项比较测试。

`build(kind,type,pose)` 返回三角形顶点和 `textures()` 中的纹理编号。`kind=0` 为玩家，`kind=1` 的 type 是协议实体编号。当前扩展覆盖 33 个生物/盔甲架类型，`kind=2` 支持船 1、矿车 10、盔甲架 78；其他未支持类型返回 `supported=false` 和空网格。新增模型、原 Java 几何导出、纹理组合、姿态输入和差分验证详见 [实体模型说明](ENTITY_MODELS.md)。

| 类型 | 原源码模型 | 已移植基础内容 |
| --- | --- | --- |
| 玩家 | ModelPlayer、ModelBiped | Steve / Alex、双层皮肤、行走、头部观察、挥手、潜行、骑乘、持物姿势、幼体比例 |
| 50 | ModelCreeper | 头、躯干、四肢及行走 |
| 51 | ModelSkeleton | 细四肢骨架、普通/凋灵纹理与比例 |
| 53 / 54 / 57 | ModelZombie | 巨人、僵尸、僵尸猪人的对应纹理与姿态 |
| 52 / 59 | ModelSpider | 躯干、八条腿原旋转动画、洞穴蜘蛛比例 |
| 58 | ModelEnderman | 长四肢、头部、持方块/攻击基础姿态 |
| 60 / 67 | ModelSilverfish / ModelEnderMite | 分节躯体与摆动 |
| 61 | ModelBlaze | 头部与三层旋转火棍 |
| 55 / 62 | ModelSlime / ModelMagmaCube | 内核/外表、眼口或分层几何 |
| 56 / 94 | ModelGhast / ModelSquid | 身体与触手；恶魂触手长度使用 Java Random(1660) |
| 90 / 91 / 92 / 96 | ModelPig / ModelSheep2 / ModelCow | 猪、羊的裸身体、牛、哞菇的牛体；四足行走、幼体比例 |
| 93 | ModelChicken | 头、喙、肉垂、身体、双腿与翼 |
| 97 / 99 | ModelSnowMan / ModelIronGolem | 雪人/铁傀儡原方块几何和基础肢体动画 |
| 120 | ModelVillager | 头鼻父子层级、长袍、交叉手臂、行走、职业纹理 |
| 95 / 98 / 100 / 101 | ModelWolf / ModelOcelot / ModelHorse / ModelRabbit | 狼/猫坐姿、马四肢/进食/扬蹄/鞍与箱子、兔子跳跃；纹理变体与幼体 |
| 63 / 64 / 65 / 66 / 68 | ModelDragon / ModelWither / ModelBat / ModelWitch / ModelGuardian | 龙的颈尾与双翼、凋灵侧头、蝙蝠悬挂/扑翼、女巫鼻子、守卫者脊刺和尾巴 |
| 30；对象 1 / 10 / 78 | ModelArmorStand / ModelBoat / ModelMinecart | 盔甲架协议姿态、船体、矿车车体 |

`EntityPose.headYaw/headPitch` 使用相对身体的弧度，方向遵循 Minecraft 约定；世界位置与身体 yaw 由渲染器应用。输出坐标脚下为原点、Y 向上、正面为 +Z。原 `ModelRenderer` 使用 `T * Rz * Ry * Rx`；网格生成器保持此旋转顺序、子模型层级、`ModelBox` 各面纹理展开、mirror 反转与外衣膨胀。UV 根据原模型的纹理尺寸归一化，因此高分辨率资源包不改变贴图位置。

`variant` 的当前约定：玩家 0 Steve / 1 Alex；骷髅 0 普通 / 1 凋灵；村民 0–4 分别是农民、图书管理员、牧师、铁匠、屠夫；末影人 bit 0 持方块、bit 1 攻击；恶魂 0 普通 / 非零射击纹理；史莱姆/岩浆怪大于 0 时为尺寸倍数。`scale` 是额外统一缩放，不应再次重复应用已经由 variant 提供的史莱姆尺寸。

`approximate` 标识有额外显示限制的模型族：羊毛层及其颜色、哞菇蘑菇、雪傀儡南瓜、发光眼、充能效果、持有物品、盔甲、斗篷、名字标签、史莱姆完整透明/挤压、岩浆怪跳跃分层、恶魂膨胀、乌贼实体状态动画、铁傀儡攻击/献花等尚未整体移植。鸡翼与乌贼触手使用周期参数近似，而不是原实体的逐帧状态插值。原版特殊动画、渲染层和全部实体逻辑仍需独立实现；本模块不宣称实体行为一致。
