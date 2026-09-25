[CmdletBinding()]
param(
    [switch]$SkipNativeBuild,

    [switch]$SkipRuntimeBuild
)

$ErrorActionPreference = 'Stop'

$RuntimeRoot = $PSScriptRoot
$RepoRoot = Split-Path -Parent $RuntimeRoot
$TestRuntimeDir = Join-Path `
    $env:USERPROFILE `
    'Desktop\SSMT3\SSMTDefaultCacheFolder\3Dmigoto\GIMI'
$RunExe = Join-Path $TestRuntimeDir 'Run.exe'
$PluginHostConfig = Join-Path `
    $RepoRoot `
    'native\SSMT-PluginHost.test.json'

function Stop-TestGame {
    $processes = @(
        Get-Process -Name 'YuanShen' -ErrorAction SilentlyContinue
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

Stop-TestGame

foreach ($Path in @($TestRuntimeDir, $RunExe, $PluginHostConfig)) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required test path does not exist: $Path"
    }
}

$hostConfig = Get-Content -Raw -LiteralPath $PluginHostConfig |
    ConvertFrom-Json

if ($hostConfig.plugins -notcontains 'ssmt_test_plugin.dll') {
    throw (
        "Debug integration testing requires ssmt_test_plugin.dll " +
        "in $PluginHostConfig."
    )
}

if (-not $SkipNativeBuild) {
    & (Join-Path $RepoRoot 'build_native.ps1') `
        -Configuration Release `
        -BuildTestPlugin `
        -DeployToTestRuntime
}

if (-not $SkipRuntimeBuild) {
    & (Join-Path $RuntimeRoot 'release.ps1')
}

if (-not (Get-Command wt.exe -ErrorAction SilentlyContinue)) {
    throw 'Windows Terminal (wt.exe) was not found.'
}

$innerScript = @"
Set-Location -LiteralPath '$TestRuntimeDir'
& '$RunExe' --plugin-host-config '$PluginHostConfig'
"@

$encoded = [Convert]::ToBase64String(
    [System.Text.Encoding]::Unicode.GetBytes($innerScript)
)

Start-Process -Verb RunAs -FilePath 'wt.exe' -ArgumentList @(
    'new-tab',
    '-p', '{3838b87d-f3e4-4b05-8668-1bfd3ed2a45a}',
    'pwsh.exe',
    '-NoExit',
    '-EncodedCommand', $encoded
)

Write-Host 'Release runtime deployed and game launch requested.'
