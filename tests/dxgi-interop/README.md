# DXGI 桌面复制共存测试

此测试在独立进程中创建 D3D11 设备并调用 `IDXGIOutput1::DuplicateOutput`，用于复现和验证 3DMigoto 与 ReShade 的设备代理兼容性。需要 Windows、本地交互桌面和有效的 GPU 输出，无法用无显示器的 CI 替代游戏测试。不会取得桌面帧。

在本目录构建：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

将以下文件放入 `build/Release`，与 `interop-test.exe` 同目录：

- 待测 runtime 的 `d3d11.dll`。
- ReShade 6.8 的 64 位附加组件版本 DLL，命名为 `ReShade64.dll`。
- `ReShade.ini`：含 `[INSTALL]` 和 `PreventUnloading=1`。重命名方式预加载 ReShade 时，配置文件必须存在。
- `d3dx.ini`：使用下面的独立测试配置，不复制游戏 Mod 配置。

```ini
[Loader]
target=interop-test.exe
[System]
load_library_redirect=2
[Device]
allow_platform_update=1
[Logging]
calls=1
```

按顺序启动四个独立进程：

```powershell
./build/Release/interop-test.exe plain
./build/Release/interop-test.exe mod
./build/Release/interop-test.exe reshade
./build/Release/interop-test.exe both
```

成功输出 `DuplicateOutput=00000000`，退出码为 0。此目录不应放置 DLSS5 或其他附加组件，以便隔离接口兼容性。

本机实测原版 WWMI runtime 在 `both` 模式发生 `0xC0000005`，调用栈为 `CDXGIOutputDuplication::Initialize` → `CDXGIOutput::DuplicateOutputInternal` → `CDXGIOutput::DuplicateOutput`，与鸣潮的崩溃一致；修正后四种模式全部成功。测试发生未处理异常时，会将 `interop-crash.dmp` 写入可执行文件所在目录，便于核对调用栈。
