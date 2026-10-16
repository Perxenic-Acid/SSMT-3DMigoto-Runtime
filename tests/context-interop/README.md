# D3D11 Context 虚表共存回归

在 Windows 独立进程中创建 WARP 设备，先热加载独立目录中的 SSMT Runtime，再把返回的 Context 虚表替换为只含 COM 方法的副本，调用着色器资源绑定、Draw、清屏命令和 Flush。无需游戏，不向游戏目录写文件。EXE 同目录不能放 `d3d11.dll`，否则会变成代理冷加载。

```powershell
cmake -S tests/context-interop -B tests/context-interop/build -A x64
cmake --build tests/context-interop/build --config Release
ctest --test-dir tests/context-interop/build -C Release --output-on-failure
```

默认测试 `x64/Release/d3d11.dll`；可在配置时用 `-DSSMT_RUNTIME_DLL=绝对路径` 指定其他版本。每个测试使用独立的 DLL/INI 目录，覆盖 hunting=0/2，以及原始虚表、128 项副本和 134 项 ID3D11DeviceContext1 副本。128 项用于复现现有 Bridge 行为，不表示它包含完整的 Context1 接口。本测试只调用标准 Context 方法，不证明第三方对扩展 COM 接口的复制正确。

2026-10-15 的修复前版本在四项副本场景均以 `0xC0000005` 执行空地址崩溃，原始虚表两项正常；修复后六项均完成。Runtime 原先在 COM 虚表尾部追加三个帧分析内部方法，副本没有这些入口。现在通过对象成员调用对应实现，保留普通 Context 的空操作和 FrameAnalysisContext 的日志、选项及资源导出职责，不改变 Plugin ABI。

夹具也可手动加载附加 DLL（参数按顺序追加），设置 `SSMT_CONTEXT_TEST_HARDWARE=1` 可改用硬件设备。使用与实际组件匹配的隔离配置，DLL 及日志应位于测试目录。真实 ReShade + Bridge 在旧 Runtime 下同样崩溃，新版本可完成接口调用。Bridge + OptiScaler 的独立夹具路径未能查询到待测 Runtime 的私有 Context 身份，退出 7；这不能作为 Runtime 共存通过的证据，更不能据此判断游戏内 NR 成功或失败。

测试同时检查退出码与完成标记，并校验返回对象的 Runtime 身份，防止只加载 DLL、绕过 Runtime 或提前退出被误报成功。WARP 回归和硬件接口测试均不能替代原神的 FSR2 dispatch、NR Feature 18 成功帧与 Mod 画面验收。

2026-10-16 另修复 `HackerSwapChain::GetDevice` 返回底层设备的问题：交换链持有的 `HackerDevice` 已有引用，取回设备时使用它的 `QueryInterface`，由现有包装 / Hook 策略决定接口身份并增加调用方引用。否则客户端重新取得的 Context 会绕过替换命令，尽管原始创建入口和 Present 仍经过 Runtime。

对应的远程热注入回归在 Native 仓库 `3Dmigoto-Injector-V2` 的 `InjectorUnitySwapChain` 用例中：使用 WARP、BGRA / Device1、独立交换链，检查取回的 Context 身份，并回读由 INI 命令改变的清屏颜色。修复前退出 7，修复后通过；本目录原有六项 Context 回归也通过。这证明该绕过路径已修复，尚不能证明崩铁实机无 Mod 的全部原因已消除。真实 ReShade 硬件组合尚未完成验收。
