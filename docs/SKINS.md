# 玩家皮肤

`SkinCache` 接收协议 47 的 S38 Player List Item 中的 Base64 `textures` 属性，异步加载官方皮肤。运行时使用 C++、WinHTTP 和 Windows WIC，不启动 Java、Python、浏览器或图像转换进程。原始 MCP 源码保持不变。

## 接口与接入

```cpp
mc::SkinCache skins;
std::string error;
skins.initialize("assets", error);
skins.request(player.uuid, player.texturesProperty, player.texturesSignature);
auto skin = skins.get(player.uuid);
```

`get()` 立即返回默认或已下载的不可变皮肤，不等待网络。RGBA 数据为 64×64、从上到下排列；`textureKey` 可作为 GPU 纹理缓存键，`revision` 表示已发布内容的变化，`slim` 决定 Steve/Alex 手臂宽度。默认图像来自当前资源包导入结果 `runtime/entities/minecraft/{steve,alex}.rgba`。没有皮肤属性时，按原版 UUID.hashCode 的奇偶性选择默认模型。

S38 ADD_PLAYER 的属性由网络模块保存在 `PlayerInfo::texturesProperty` 与 `texturesSignature`；其他列表更新保留已有属性。相同属性的重复请求不会重新解析、排队或解码。切换服务器时调用 `clear()`，它会取消活动请求并丢弃旧世代的异步结果。失败请求保留默认皮肤，网络错误按 10、20、40、80、160、300 秒退避；调用方再次提交相同属性时才会重试。队列满后的重试间隔为 1 秒。

单个后台线程承担磁盘读取、HTTP 与 PNG 解码。队列最多 256 项、玩家记录最多 4,096 项、已解码图像缓存最多 256 项。磁盘缓存为 `runtime/skin-cache/<SHA256(规范 URL)>.png`；资源目录不可写时仍可使用内存缓存。这里的 SHA-256 用于 URL 的稳定文件名，并非对 URL 中的内容散列作真实性验证。

构建需加入 `src/skins.cpp`。Windows 链接库为 `winhttp`、`ole32`、`windowscodecs`、`uuid`、现有 `crypto`；GCC 8 还需要 `stdc++fs`。

## 图像与网络边界

支持官方标准的 64×64 和旧版 64×32 PNG。旧版左右肢体镜像补全、各区域透明/不透明处理按原始 `ImageBufferDownload` 顺序执行。半透明 RGB 也经过 Java2D 的定点预乘与除法舍入，透明像素的颜色处理与原版一致。

只接受 `http://textures.minecraft.net/texture/<十六进制标识>` 或 HTTPS 等价形式，并统一使用 HTTPS；不会访问属性提供的其他主机、端口、路径、查询或重定向。TLS 证书验证保持启用，禁止自动 Windows HTTP 凭据，设置连接/读写超时。编码 PNG 最大 1 MiB，Base64 属性、JSON 深度、节点数、尺寸均有限制。

`texturesSignature` 目前保留但尚未做 Mojang 公钥签名验证。HTTPS 主机验证证明下载来源，不证明属性与玩家身份的绑定；不能把此阶段描述为已经实现完整的 Mojang 属性签名认证。自定义外部皮肤主机、披风、资源包扩展皮肤尺寸不在本接口范围内。

## 验证

`tools/export_skin_reference.py` 编译并直接执行原始 `ImageBufferDownload.java`，生成 20 个参考样本：现代/旧版、全不透明/混合透明，以及全部 65,536 种通道值与 alpha 组合。样本和来源散列位于 `tools/reference/skin_reference.*`。

`tests/skins_tests.cpp` 通过 393,607 项检查，包含参考图像逐字节比较、4 个 WIC PNG 解码逐字节比较、URL/JSON/Base64 拒绝边界、UUID 默认模型、离线磁盘缓存、取消/清空、重初始化和失败状态保留。测试不需要网络或声音设备。

```powershell
D:/Strawberry/c/bin/g++.exe -std=c++17 -O2 -I include tests/skins_tests.cpp src/skins.cpp -o build/skins_tests.exe -lwinhttp -lole32 -lwindowscodecs -luuid -lcrypto -lstdc++fs -static-libgcc -static-libstdc++
./build/skins_tests.exe .
```

可选 `tests/skins_network_smoke.cpp` 接收资产目录和仅含 Base64 属性的文本文件；它不会读取账号凭据。已使用公开 Mojang 皮肤属性实测原生 WinHTTP HTTPS 下载及 WIC 解码，得到 64×64、16,384 字节 RGBA。该联网测试不应作为每次离线构建的必需步骤。
