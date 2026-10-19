# Runtime 版本生成回归

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/version-generation/Run-Tests.ps1
```

测试创建不带标签、共享本机 Git 对象的临时 clone，验证它和具有上游版本标签的 clone
生成相同的四段文件版本。只操作隔离目录，不修改本机标签，不访问远端或启动游戏。

SSMT fork 的云端可能只有 `rolling` 标签。`tools/version-baseline.json` 为这种 checkout
提供固定版本及祖先提交，build 段继续使用该祖先到 HEAD 的提交数；缺少完整历史时
明确失败，不发布看似构建成功的 `0.0.0.0`。有正常版本标签时仍优先使用现有标签逻辑。

修改版本基线时同步版本和对应提交，并验证该提交确实是当前源码的祖先。
不能用会随发布移动的 `rolling` 标签充当版本计数基线。发布工作流另行检查 DLL 的
真实版本资源，阻止零版本资产发布；本脚本不能替代组件编译验证。
