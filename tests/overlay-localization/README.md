# 叠加层中文与字体回归

先构建 x64 Release Runtime，再在 Runtime 根目录执行：

```powershell
cmake -S tests/overlay-localization -B tests/overlay-localization/build -A x64
cmake --build tests/overlay-localization/build --config Release
ctest --test-dir tests/overlay-localization/build -C Release --output-on-failure
```

测试使用软件 WARP 和隐藏窗口，不启动游戏，不发送桌面按键。
需要 Windows 已安装 Microsoft YaHei；缺少该字体的测试机应观察到字体测试失败，
不能把这种环境下的英文回退当作中文绘制验收。

- 验证 `auto`、`en`、`zh_s` 以及无效配置处理。
- 验证窄 / 宽格式串、Unicode 路径、动态宽度哈希、错误码和超长消息。
- 扫描整份翻译目录，比较原文与译文的 printf 参数序列，并拒绝重复格式键。
- 对照 `IniHandler.cpp`、`Override.cpp` 的实际叠加层格式串检查覆盖率，包含相邻字符串
  拼接；同时检查表达式语法错误及已知原因，新增重载错误未收录时测试失败。
- 验证重复配置节、另一种未知配置项格式、哈希解析、表达式求值错误的中文，
  保留原始英文、文件名、变量、错误位置和参数顺序；未知原因及 Mod 表达式原样保留。
- 实际系统字形上传并用 SpriteBatch 绘制，回读绿色提示及橙红色重载告警像素，
  并生成 `overlay-preview.bmp`。
- 加载实际 Runtime，在中英文模式下各执行 32 次 Present，检查原有 PS SRV、
  空 RTV、视口和拓扑恢复，以及 GDI 对象数量稳定。
- 中文模式日志应只有一次字库创建记录，英文模式不应创建中文字体。

Runtime 的 `[Hunting] overlay_language` 默认 `auto`，简体中文 Windows 使用 `zh_s`，
其他系统使用英文；也可显式指定。配置重载后切换显示语言。
字体缺失或图集创建失败会保留英文显示，原始英文日志始终保留。
这些测试不能替代游戏内字体大小、位置及多个叠加层共存的视觉验收。
