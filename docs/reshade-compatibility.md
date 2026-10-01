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
用户已确认 ReShade 菜单与 Mod 同时正常。游戏内长时间运行仍需另行验证。

runtime 的开发脚本默认选择 GIMI，可用 `-GamePreset SRMI` 等参数选择其他游戏，也可用 `-TestRuntimeDir` 或 `SSMT_TEST_RUNTIME_DIR` 直接指定测试目录。
测试鸣潮时须显式指定 `-GamePreset WWMI` 或 `-TestRuntimeDir`，并按需传入 `-GameArguments '-dx11 -krqlv=hd'`。省略 `-GameArguments` 时不会改写现有启动参数。
Rust 工具链尚未准备好时，可用 `./runtime/debug.ps1 -SkipNativeBuild -GamePreset WWMI` 只编译部署 C++ runtime 并通过 Run.exe 启动鸣潮。脚本不会默认关闭已经运行的游戏；需要此行为时显式传入 `-StopRunningGame`。
需要 PluginHost 集成测试时，额外指定 `-PluginHostConfig`。

提供给非开发者的启动入口是主仓库的 `scripts/start-wuwa-dlss5-mod.ps1`。
部署时把它与 TestEnvironment.ps1 放到同一目录，再建立调用该脚本的桌面快捷方式。
脚本会同步 RHI 更新后的 ReShade 副本、保留额外 DLL 列表，并在失败时显示中文提示和日志位置；已经运行游戏时不会关闭游戏。
该脚本只复现本次鸣潮环境，不是插件组合的通用实现；新增或变更插件时，应由启用插件的贡献生成启动计划，不能沿用脚本中的固定文件名和参数作为默认值。
