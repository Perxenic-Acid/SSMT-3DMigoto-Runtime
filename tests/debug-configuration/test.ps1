[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$runtimeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $runtimeRoot 'TestEnvironment.ps1')
$buildRoot = Join-Path $runtimeRoot 'build'
$fixtureRoot = Join-Path $buildRoot ('debug-config-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
$ini = Join-Path $fixtureRoot 'd3dx.ini'
$encoding = New-Object Text.UTF8Encoding($false)

function Assert-True([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }
}

try {
    # 无可执行入口；PrepareOnly 必须在任何启动行为之前返回。
    $fakeGame = Join-Path $fixtureRoot 'SSMT-NeverLaunched-Test.exe'
    [IO.File]::WriteAllText($fakeGame, 'not executable')
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'Run.exe'), 'not executable')
    $modSection = "[CommandList保留]`r`nlog_level = mod-value`r`nhunting = untouched`r`n"
    $original = "[Loader] ; 保留注释`r`nlaunch = old.exe`r`nlaunch_args = --keep`r`n" +
        "[Logging]`r`ncalls = 0`r`ndebug = 0`r`nlog_level = disabled`r`n" +
        "[Hunting]`r`nhunting = 2`r`nanalyse_options = dump_vb buf`r`n" + $modSection
    [IO.File]::WriteAllText($ini, $original, $encoding)

    $options = @{
        TestRuntimeDir = $fixtureRoot
        GameExePath = $fakeGame
        SkipNativeBuild = $true
        SkipRuntimeBuild = $true
        PrepareOnly = $true
    }
    & (Join-Path $runtimeRoot 'debug.ps1') @options -FrameAnalysisLogOnly
    $prepared = [IO.File]::ReadAllText($ini)
    Assert-True ($prepared.Contains('log_level = info')) '默认调试必须启用 Info，覆盖旧 disabled。'
    Assert-True ($prepared.Contains('hunting = 1')) '单帧诊断必须可直接触发。'
    Assert-True ($prepared.Contains('analyse_frame = no_modifiers VK_F8')) '单帧诊断需要明确按键。'
    Assert-True ($prepared.Contains('analyse_options = log')) '诊断不得转储整帧资源。'
    Assert-True ($prepared.Contains($modSection)) '不得改写其他节中的同名 Mod 命令。'
    Assert-True ($prepared.Contains('launch_args = --keep')) '未指定参数时必须保留原参数。'
    $backups = @(Get-ChildItem -LiteralPath $fixtureRoot -Filter '*.before-debug-*.bak')
    Assert-True ($backups.Count -eq 1) '首次调试必须有独立备份。'
    Assert-True ([IO.File]::ReadAllText($backups[0].FullName) -ceq $original) '备份必须完整保留原配置。'

    & (Join-Path $runtimeRoot 'debug.ps1') @options -LogLevel preserve
    Assert-True ([IO.File]::ReadAllText($ini) -ceq $prepared) 'Preserve 不应重写日志与帧分析设置。'
    $backups = @(Get-ChildItem -LiteralPath $fixtureRoot -Filter '*.before-debug-*.bak')
    Assert-True ($backups.Count -eq 2) '重复调试不能覆盖之前的备份。'
    & (Join-Path $runtimeRoot 'debug.ps1') @options -LogLevel disabled
    Assert-True ([IO.File]::ReadAllText($ini).Contains('log_level = disabled')) '显式禁用日志仍须可用。'

    [IO.File]::WriteAllText($ini, '[Loader]', $encoding)
    Set-TestLoaderSetting $ini 'launch' 'C:\测试\game.exe'
    Set-TestIniSetting $ini 'Logging' 'log_level' 'info'
    $minimal = [IO.File]::ReadAllText($ini)
    Assert-True ($minimal.Contains("[Loader]`r`n")) '文件尾的空节必须正常追加设置。'
    Assert-True (([regex]::Matches($minimal, '\[Loader\]')).Count -eq 1) '不得创建重复 Loader 节。'
    Assert-True ($minimal.Contains("[Logging]`r`nlog_level = info")) '缺失日志节应补充。'
    Write-Host 'PASS：诊断配置、保留设置、备份及无启动准备。'
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    if ([IO.Path]::GetDirectoryName($resolvedFixture) -ne [IO.Path]::GetFullPath($buildRoot)) {
        throw "拒绝清理测试目录之外的路径：$resolvedFixture"
    }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}
