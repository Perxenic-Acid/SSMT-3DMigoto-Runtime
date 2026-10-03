# WWMI 与 ReShade 共存

如果直接启动鸣潮时 ReShade 正常，但经 Run.exe 启动后只剩下 3DMigoto，先检查游戏目录中的 ReShade.log。
本机复测发现 ReShade 初始化后立即出现 `Exiting`，未创建效果运行时。`PreventUnloading` 可防止卸载，但单独使用时仍会遗漏 DXGI 交换链接管，无法显示菜单。

本机使用的修复方式是通过 Run.exe 的现有额外 DLL 功能预加载 ReShade：

1. 将游戏目录中的 dxgi.dll **复制**为同目录中的 ReShade64.dll，保留 dxgi.dll 供直接启动使用。
2. 在 SSMT4 的鸣潮设置中，将 ReShade64.dll 的完整路径加入额外 DLL 列表，关闭 Shell 启动。
3. 使用支持暂停启动、额外 DLL 预加载的新版 Run.exe。仅安装旧版 Run.exe 时需要先更新它。

对应 d3dx.ini 的设置：

```ini
[Loader]
inject_dlls = 游戏目录的完整路径\ReShade64.dll
launch_args = -dx11 -krqlv=hd
```

测试时同时在游戏目录的 ReShade.ini 中设置：

```ini
[INSTALL]
PreventUnloading=1
```

这是 ReShade 提供的模块驻留选项，重启游戏后生效。它保持 ReShade 加载到游戏进程退出。
仅在已安装 ReShade 且出现上述现象时应用。不设置 proxy_d3d11：本机试验该方式出现 DXGI 工厂递归并崩溃。
RHI 更新 dxgi.dll 后需要同步更新 ReShade64.dll，避免继续预加载旧版。

本机验证使用 ReShade 6.8.0、WWMI、鸣潮 `-dx11 -krqlv=hd`，预加载方案的日志确认 ReShade 接管交换链、加载 DLSS5 插件、绘制状态 HUD，并持续执行 NR。
用户已确认 ReShade 菜单与 Mod 同时正常。角色界面兼容性修正及复测记录见下文，数小时的连续运行尚未验证。

## 角色界面切换时卡顿、闪退

2026-10-02 的现场排查发现，共存模式在角色界面停留、切换时发生 `0xC0000005`。直接启动且只加载 DLSS5 的对照正常。系统转储没有保存线程栈内存，因此通过临时 ReShade 诊断插件记录异常现场，再用微软公开符号确认故障位置为 `CDXGIOutputDuplication::Initialize` / `IDXGIOutput1::DuplicateOutput`。

此调用要求真实的 DXGI 适配器。[ReShade 6.8 的设备查询实现](https://github.com/crosire/reshade/blob/v6.8.0/source/d3d11/d3d11_device.cpp) 会识别来自系统 DXGI 的调用并绕过代理。查询经 HackerDevice 转发后，ReShade 看到的调用来源变成 3DMigoto，可能返回代理适配器，导致系统把代理对象数据当作内部函数表使用。

修正位于 HackerDevice：设备创建时探测 ReShade 的原始对象查询接口，并缓存系统 DXGI 的模块范围；只对来自该模块的查询取得原始设备，再转发目标接口查询。每次查询增加的临时引用均在返回前释放，避免额外设备引用妨碍 runtime 的零引用清理。普通游戏查询保持原有包装路径，未安装 ReShade 时不启用此分支。

`tests/dxgi-interop` 提供独立复现程序。原版 WWMI runtime 与 ReShade 共存时复现了相同崩溃栈，修正版的纯 D3D11、仅 Mod、仅 ReShade、两者共存四组均成功。此测试不加载游戏、DLSS5 或角色 Mod。

修正版部署至 WWMI 后，经桌面一键脚本启动，移除了临时诊断插件。用户复测角色界面停留、反复切换模型，确认未观察到闪退或卡死；日志确认 DLSS5 持续运行超过五分钟，未再记录同类访问异常。原 runtime DLL 保留了本机备份，可在退出游戏后恢复。

仅修改 `[System] load_library_redirect` 为 1 的试验仍然崩溃，因此保留原来的值 2。该选项不属于 `[Loader]`。修正需要编译部署新的 `d3d11.dll`，只更新 Run.exe 或启动脚本不足以修复此故障。

C 盘当时剩余约 5.6 GiB，分页文件位于 D 盘，未发现系统资源耗尽事件。磁盘容量应另行关注；独立程序复现的是设备代理接口错误，不能由增加 C 盘空间替代 runtime 修正。

runtime 的开发脚本默认选择 GIMI，可用 `-GamePreset SRMI` 等参数选择其他游戏，也可用 `-TestRuntimeDir` 或 `SSMT_TEST_RUNTIME_DIR` 直接指定测试目录。
测试鸣潮时须显式指定 `-GamePreset WWMI` 或 `-TestRuntimeDir`，并按需传入 `-GameArguments '-dx11 -krqlv=hd'`。省略 `-GameArguments` 时不会改写现有启动参数。
Rust 工具链尚未准备好时，可用 `./runtime/debug.ps1 -SkipNativeBuild -GamePreset WWMI` 只编译部署 C++ runtime 并通过 Run.exe 启动鸣潮。脚本不会默认关闭已经运行的游戏；需要此行为时显式传入 `-StopRunningGame`。
需要 PluginHost 集成测试时，额外指定 `-PluginHostConfig`。

提供给非开发者的启动入口是主仓库的 `scripts/start-wuwa-dlss5-mod.ps1`。
部署时把它与 TestEnvironment.ps1 放到同一目录，再建立调用该脚本的桌面快捷方式。
脚本会同步 RHI 更新后的 ReShade 副本、保留额外 DLL 列表，并在失败时显示中文提示和日志位置；已经运行游戏时不会关闭游戏。
该脚本只复现本次鸣潮环境，不是插件组合的通用实现；新增或变更插件时，应由启用插件的贡献生成启动计划，不能沿用脚本中的固定文件名和参数作为默认值。
