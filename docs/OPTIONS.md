# 按键和声音设置界面

`gui_options.hpp/.cpp` 实现 `OptionsPage::Controls` 和 `OptionsPage::Sounds`，直接修改主程序的 `Settings`。界面不操作游戏网络、音频设备、窗口模式或设置文件；主程序负责应用和保存改变后的设置。

```cpp
mc::OptionsScreen optionsScreen;
// 进入页面时调用 reset()；beginUI、默认背景与 endUI 由主程序负责。
if (optionsScreen.draw(mc::OptionsPage::Controls, settings, renderer,
                       window, language, guiScale)) {
    settings.save(optionsPath);
    // 返回上一级设置页面。
}
```

`draw()` 返回 true 表示点击完成或在未捕获按键时按下 Esc。`capturingKey()` 表示正在等待绑定输入。新设置页面的 Esc 处理应交给本模块；捕获期间 Esc 的含义是解绑，不是关闭。绑定期间主程序也应避免让对应输入同时触发游戏动作。`reset()` 清除捕获、拖动和滚动状态，不回滚已经修改的设置。

## 按键界面

位置和滚动布局取自原始 `GuiControls`、`GuiKeyBindingList`、`GuiSlot`：

- 顶部反转鼠标/灵敏度按钮位于 `width/2-155` 与 `width/2+5`，y=18；按钮 150×20。
- 列表视口 y=63 到 `height-32`，行距 20，首行 y=67；按翻译后的类别和动作名称排序。
- 每行绑定按钮为 75×20，x=`width/2-19`；重置为 50×20，x=`width/2+66`。
- 底部完成/全部重置位于 y=`height-29`；按键没有改变时全部重置不可点击。

点击绑定按钮后，下一个键盘或鼠标按键替换该绑定；进入捕获状态的初次点击会被消费。Esc 清除绑定；无绑定的 0 值不视为冲突。同一非零按键的多个绑定以红色显示，当前捕获项显示黄色尖括号。鼠标按钮名称使用原版 `key.mouseButton` 翻译。

滚轮每格移动半行，即 10 个 GUI 像素；也可以拖动滚动条或列表背景。改变窗口大小会重新钳制滚动位置，视口外的隐藏行不会收到点击。列表使用平铺的原始背景纹理与覆盖层裁切，适配现有延迟提交的 UI 绘制；无需直接操纵 OpenGL 状态。

灵敏度滑块使用原版 `(mouseX-(x+4))/(width-8)` 映射，范围 [0,1]；显示 0% 到 200% 以及原版两端文字。反转鼠标修改 `Settings::invertMouse`。触屏模式目前没有输入实现，因此原位置按钮保持禁用。若主程序有尚未接入的绑定，可以调用 `setBindingEnabled(name,false)`，使对应绑定和重置按钮不可点击。

键码遵循 `Settings` 的 Win32 表示，保留左右 Shift/Ctrl/Alt 的区别。`draw()` 直接使用平台层的 `Window::pressedKeyOrder`，按实际事件顺序捕获同帧输入，也支持鼠标侧键 4/5；进入绑定状态的初次点击仍会被消费。左右修饰键和侧键绑定可经设置文件独立保存、重载。任意 Unicode 字符绑定不由此接口扩展。

## 声音界面

位置取自 `GuiScreenOptionsSounds`。总音量是 310×20 的宽滑块，y=`height/6-12`；其他分类按 150×20、两列、行距 24 排列。完成按钮位于 `height/6+168`。

`Settings::soundLevels` 的九个位置依次为：总音量、音乐、唱片/音符盒、天气、方块、敌对生物、友好生物、玩家、环境。每个滑块支持点击、连续拖动、越界钳制和松开提交，直接更新对应分类。零值显示原版“关/OFF”，其他值显示整数百分比。界面本身不播放任何测试声音或点击声音；音频应用仍由主程序统一负责。

## 验证

`OptionsInput` 和 `layout()` 是确定性的输入/命中路径，正式 `draw()` 也使用相同逻辑。`tests/gui_options_tests.cpp` 链接真实 `Settings`，仅替换图形/窗口显示部分以避免创建设备。

目前 153 项检查通过：原始按钮位置和边界、真实 `draw()` 输入适配中的事件先后顺序、左右修饰键和鼠标侧键重绑及保存重载、初次点击消费、Esc 解绑、冲突和重置、禁用动作、焦点丢失、滚轮/拖动/窗口缩放、隐藏行命中、全部九类音量及未知选项保留。该测试不声称进行了逐像素图形验证或完整原始 Java 界面的运行时差分。

```powershell
D:/Strawberry/c/bin/g++.exe -std=c++17 -O2 -ffp-contract=off -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I include tests/gui_options_tests.cpp src/gui_options.cpp src/settings.cpp -o build/gui_options_tests.exe -luser32 -lstdc++fs -static-libgcc -static-libstdc++
./build/gui_options_tests.exe .
```

正式构建加入 `src/gui_options.cpp` 和主程序已有的 `src/settings.cpp`，不需要额外第三方库。设置保存时机、声音实时生效、灵敏度/反转在相机输入中的应用，以及全局热键优先级属于主程序集成。
