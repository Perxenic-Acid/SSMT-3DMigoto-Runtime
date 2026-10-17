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

测试同时检查退出码与完成标记，并校验返回对象的 Runtime 身份，防止只加载 DLL、绕过 Runtime 或提前退出被误报成功。交换链检查还创建隐藏 WARP 窗口，要求 `IDXGISwapChain4` 与继承的 `IUnknown` 保持同一身份，并通过包装器调用 `SetHDRMetaData` 和 `Present(TEST)`。WARP 回归和硬件接口测试均不能替代原神的 FSR2 dispatch、NR Feature 18 成功帧与 Mod 画面验收。

2026-10-16 另修复 `HackerSwapChain::GetDevice` 返回底层设备的问题：交换链持有的 `HackerDevice` 已有引用，取回设备时使用它的 `QueryInterface`，由现有包装 / Hook 策略决定接口身份并增加调用方引用。否则客户端重新取得的 Context 会绕过替换命令，尽管原始创建入口和 Present 仍经过 Runtime。

对应的远程热注入回归在 Native 仓库 `3Dmigoto-Injector-V2` 的 `InjectorUnitySwapChain` 用例中：使用 WARP、BGRA / Device1、独立交换链，检查取回的 Context 身份，并回读由 INI 命令改变的清屏颜色。修复前退出 7，修复后通过；本目录原有六项 Context 回归也通过。这证明该绕过路径已修复，尚不能证明崩铁实机无 Mod 的全部原因已消除。真实 ReShade 硬件组合尚未完成验收。

## F8 与重复 Present

`capture-Present`、`capture-Present1` 和 `capture-Deferred` 使用 WARP 测试真实帧分析。INI 的反向 F24 绑定会在第一次处理输入时触发采集，不发送桌面按键、不抢前台、不启动游戏。夹具连续提交三次没有游戏 Draw 的 Present，再绘制一个带 VB 的三角形；检查本次新建目录中的 VB 为 48 字节、日志包含 Draw 与文件映射，且采集结束后的第二次 Draw 没有额外导出。延迟 Context 用例还验证 `ExecuteCommandList` 可以结束这一帧。

修复前两个立即 Context 用例都创建了空采集目录，VB 数为零；修复后通过。辅助交换链也会执行游戏设备的帧动作，故不能把每一次 Present 都当作新游戏帧结束。现在只在游戏 Context 有 Draw、Dispatch 或命令列表提交后推进帧分析边界；Present 中的自定义着色器不作为此依据。输入轮询仍照常执行，等待期间可用原绑定中止采集。此回归证明提前结束路径已修复，不能替代实际游戏与图形插件组合验收，也不能单凭它解释历史日志缺少映射的全部原因。

这三项回归拒绝 `SSMT_CONTEXT_TEST_HARDWARE`，避免转为硬件 GPU 压力测试。它们使用各自 DLL 目录，保留日志和转储以供检查，不清理玩家目录。
