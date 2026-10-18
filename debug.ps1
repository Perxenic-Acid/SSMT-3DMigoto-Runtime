[CmdletBinding()]
param(
    [switch]$SkipNativeBuild,

    [switch]$SkipRuntimeBuild,

    [string]$TestRuntimeDir,

    [string]$GamePreset = 'GIMI',

    [string]$GameExePath,

    [string]$GameArguments,

    [switch]$StopRunningGame,

    [string]$PluginHostConfig,

    [ValidateSet('info', 'debug', 'disabled', 'preserve')]
    [string]$LogLevel = 'info',

    [switch]$FrameAnalysisLogOnly,

    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'

$RuntimeRoot = $PSScriptRoot
$RepoRoot = Split-Path -Parent $RuntimeRoot
. (Join-Path $RuntimeRoot 'TestEnvironment.ps1')
$TestRuntimeDir = Resolve-TestRuntimeDirectory -TestRuntimeDir $TestRuntimeDir -GamePreset $GamePreset
$RunExe = Join-Path $TestRuntimeDir 'Run.exe'
$IniPath = Join-Path $TestRuntimeDir 'd3dx.ini'

function Stop-TestGame {
    param([string]$ProcessName)
    $processes = @(
        Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
    )

    foreach ($process in $processes) {
        Write-Host "Stopping running test game process: $($process.Id)"
        Stop-Process -Id $process.Id -Force
        Wait-Process `
            -Id $process.Id `
            -Timeout 15 `
            -ErrorAction SilentlyContinue

        if (Get-Process -Id $process.Id -ErrorAction SilentlyContinue) {
            throw "Test game process did not exit: $($process.Id)"
        }
    }
}

foreach ($Path in @($RunExe, $IniPath)) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required test path does not exist: $Path"
    }
}

if ($PluginHostConfig -and -not (Test-Path -LiteralPath $PluginHostConfig -PathType Leaf)) {
    throw "PluginHost config does not exist: $PluginHostConfig"
}

if (-not $GameExePath) {
    $iniText = [IO.File]::ReadAllText($IniPath)
    $loader = [regex]::Match($iniText, '(?ims)^\[Loader\][^\r\n]*\r?\n(.*?)(?=^\[|\z)')
    $launch = [regex]::Match($loader.Groups[1].Value, '(?im)^\s*launch\s*=\s*([^\r\n]+)')
    $GameExePath = $launch.Groups[1].Value.Trim().Trim('"')
}
if (-not $GameExePath -or -not (Test-Path -LiteralPath $GameExePath -PathType Leaf)) {
    throw 'Game executable was not found. Set -GameExePath or [Loader] launch in d3dx.ini.'
}

$gameProcessName = [IO.Path]::GetFileNameWithoutExtension($GameExePath)
if (Get-Process -Name $gameProcessName -ErrorAction SilentlyContinue) {
    if (-not $StopRunningGame) {
        throw "Test game is already running: $gameProcessName. Stop it manually or pass -StopRunningGame."
    }
    Stop-TestGame -ProcessName $gameProcessName
}
# 每次保留独立原件，重复调试不能覆盖关闭诊断前的配置。
$backupSuffix = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$configBackup = "$IniPath.before-debug-$backupSuffix.bak"
Copy-Item -LiteralPath $IniPath -Destination $configBackup
Write-Host "原配置备份：$configBackup"
Set-TestLoaderSetting $IniPath 'launch' $GameExePath
if ($PSBoundParameters.ContainsKey('GameArguments')) {
    Set-TestLoaderSetting $IniPath 'launch_args' $GameArguments
}
if ($LogLevel -ne 'preserve') {
    # log_level 优先于 calls/debug；Info 记录初始化和资源错误，不逐次记录 Draw。
    Set-TestIniSetting $IniPath 'Logging' 'log_level' $LogLevel
}
if ($FrameAnalysisLogOnly) {
    Set-TestIniSetting $IniPath 'Hunting' 'hunting' '1'
    Set-TestIniSetting $IniPath 'Hunting' 'analyse_frame' 'no_modifiers VK_F8'
    Set-TestIniSetting $IniPath 'Hunting' 'analyse_options' 'log'
    Write-Host '单帧日志诊断：进入异常界面后按一次 F8，仅采集调用与 Mod 命令，不转储整帧资源。'
    Write-Host '诊断结束并退出游戏后，可从上面的备份恢复 d3dx.ini；如已另改配置，请仅恢复 Logging / Hunting 对应设置。'
}
Write-Host "本次 Runtime 日志：$(Join-Path $TestRuntimeDir 'd3d11_log.txt')（启动游戏后生成；prepare-only 不代表已有新日志）"

if (-not $SkipNativeBuild) {
    & (Join-Path $RepoRoot 'build_native.ps1') `
        -Configuration Release `
        -BuildTestPlugin:([bool]$PluginHostConfig) `
        -DeployToTestRuntime `
        -TestRuntimeDir $TestRuntimeDir
}

if (-not $SkipRuntimeBuild) {
    & (Join-Path $RuntimeRoot 'release.ps1') -TestRuntimeDir $TestRuntimeDir
}

if ($PrepareOnly) {
    Write-Host '准备完成，未启动游戏或注入器。'
    return
}

$startOptions = @{
    FilePath = $RunExe
    WorkingDirectory = $TestRuntimeDir
    WindowStyle = 'Hidden'
    Verb = 'RunAs'
}
if ($PluginHostConfig) {
    $configPath = (Resolve-Path -LiteralPath $PluginHostConfig).ProviderPath
    $startOptions.ArgumentList = '--plugin-host-config "' + $configPath + '"'
}
Start-Process @startOptions

Write-Host "Game launch requested from: $TestRuntimeDir"
