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

## ReShade 接口升级与 Mod 绕过

`context-upgrade` 只用 WARP：创建设备时不请求立即 Context，再通过 Device1–5、Context1–4、IUnknown 和 DeviceChild 取回接口，回读 INI 替换后的清屏颜色。另直接调用新版立即 / 延迟 Context 方法并执行命令列表，避免只检查 DLL 或热键就误报成功。

2026-10-18 修复前，Device2–5、Context2–4 及 IUnknown / DeviceChild 转换返回底层对象，替换清屏无效。ReShade 6.8.0 的 `check_and_upgrade_interface` 会采用升级后的指针，因此该漏洞可使渲染绕过 Runtime，而 Present、F10 与 Hunting 仍然工作。现将接口尾部能力局部转发，继承的绘制及绑定保持经过 Runtime；新增接口沿用 `allow_platform_update` 门禁。GetImmediateContext1–3 共用资源初始化路径，不额外持有底层对象引用。

修复后本目录十项软件回归通过。WARP 不会执行 ReShade 的硬件设备代理分支；该结果证明接口绕过已修复，原神 Feeder 的 Mod 画面、NR 效果及硬件驱动稳定性仍需游戏实测。

## 新版纹理创建与替换

`texture-interop` 只用 WARP，通过真实 INI 的 `checktextureoverride` 将蓝色纹理替换为绿色 DDS，并读取实际绑定及像素。分别覆盖旧版与 Device3 的 2D / 3D 创建、ShaderResourceView1，以及多次释放后交替创建匹配和不匹配内容；反例须保持原贴图。另检查创建时的尺寸覆盖、只验证参数时的 `S_FALSE`、失败返回，以及系统确实支持的非默认 `ROW_MAJOR` 布局。

2026-10-18，旧 Runtime 的旧版入口通过，而 `CreateTexture2D1` / `CreateTexture3D1` 未登记资源，纹理替换和尺寸覆盖均被跳过。修复后新旧入口共用原有哈希、创建覆盖、Hunting 元信息及释放跟踪，实际创建仍调用对应版本的系统 API，保留 `TextureLayout`。本目录十一项软件回归通过；只检查 DLL、Context 身份或清屏命令无法覆盖此缺陷。此结果证明新版资源路径的缺陷已修复，尚不能确定它解释了玩家纯 3DMigoto 下的全部贴图异常，需要实机画面确认。
