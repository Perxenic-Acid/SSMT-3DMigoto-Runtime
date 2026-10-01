[CmdletBinding()]
param(
    [switch]$SkipNativeBuild,

    [switch]$SkipRuntimeBuild,

    [string]$TestRuntimeDir,

    [string]$GamePreset = 'GIMI',

    [string]$GameExePath,

    [string]$GameArguments,

    [switch]$StopRunningGame,

    [string]$PluginHostConfig
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
Copy-Item -LiteralPath $IniPath -Destination "$IniPath.before-debug.bak" -Force
Set-TestLoaderSetting $IniPath 'launch' $GameExePath
if ($PSBoundParameters.ContainsKey('GameArguments')) {
    Set-TestLoaderSetting $IniPath 'launch_args' $GameArguments
}

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
