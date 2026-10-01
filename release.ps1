[CmdletBinding()]
param(
    [string]$TestRuntimeDir,
    [string]$GamePreset = 'GIMI',
    [string]$PlatformToolset
)

$ErrorActionPreference = 'Stop'

$RuntimeRoot = $PSScriptRoot
$SolutionPath = Join-Path $RuntimeRoot 'StereoVisionHacks.sln'
. (Join-Path $RuntimeRoot 'TestEnvironment.ps1')
$TestRuntimeDir = Resolve-TestRuntimeDirectory -TestRuntimeDir $TestRuntimeDir -GamePreset $GamePreset

function Resolve-MSBuild {
    $fromPath = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue

    if ($fromPath) {
        return $fromPath.Source
    }

    $vswhere = Join-Path `
        ([Environment]::GetFolderPath('ProgramFilesX86')) `
        'Microsoft Visual Studio\Installer\vswhere.exe'

    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'MSBuild was not found on PATH and vswhere.exe is unavailable.'
    }

    $installationPath = & $vswhere `
        -latest `
        -products '*' `
        -requires Microsoft.Component.MSBuild `
        -property installationPath `
        | Select-Object -First 1

    foreach ($relativePath in @(
        'MSBuild\Current\Bin\amd64\MSBuild.exe',
        'MSBuild\Current\Bin\MSBuild.exe'
    )) {
        $candidate = Join-Path $installationPath $relativePath

        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw "MSBuild was not found in Visual Studio: $installationPath"
}

if (-not (Test-Path -LiteralPath $SolutionPath -PathType Leaf)) {
    throw "Runtime solution does not exist: $SolutionPath"
}

if (-not (Test-Path -LiteralPath $TestRuntimeDir -PathType Container)) {
    throw "Test runtime directory does not exist: $TestRuntimeDir"
}

$MSBuild = Resolve-MSBuild

if (-not $PlatformToolset) {
    $targetsPath = & $MSBuild (Join-Path $RuntimeRoot 'DirectX11\DirectX11.vcxproj') `
        /p:Configuration=Release /p:Platform=x64 /getProperty:VCTargetsPath /nologo
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to resolve Visual C++ targets path.'
    }
    $toolsetsPath = Join-Path ($targetsPath | Select-Object -Last 1) 'Platforms\x64\PlatformToolsets'
    if (-not (Test-Path -LiteralPath (Join-Path $toolsetsPath 'v143'))) {
        $PlatformToolset = Get-ChildItem -LiteralPath $toolsetsPath -Directory |
            Where-Object { $_.Name -match '^v\d+$' } |
            Sort-Object { [int]$_.Name.Substring(1) } -Descending |
            Select-Object -First 1 -ExpandProperty Name
        if (-not $PlatformToolset) {
            throw "No Visual C++ platform toolset found: $toolsetsPath"
        }
    }
}

$buildArguments = @(
    $SolutionPath, '/m', '/t:DirectX11',
    '/p:Configuration=Release', '/p:Platform=x64', '/v:minimal'
)
if ($PlatformToolset) {
    $buildArguments += "/p:PlatformToolset=$PlatformToolset"
}
& $MSBuild @buildArguments

if ($LASTEXITCODE -ne 0) {
    throw "SSMT 3DMigoto Runtime build failed with exit code $LASTEXITCODE"
}

$RuntimeArtifacts = @(
    'd3d11.dll',
    'd3d11.pdb'
)

foreach ($Name in $RuntimeArtifacts) {
    $Source = Join-Path $RuntimeRoot "x64\Release\$Name"

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Runtime build artifact not found: $Source"
    }

    Copy-Item `
        -LiteralPath $Source `
        -Destination (Join-Path $TestRuntimeDir $Name) `
        -Force
}

Write-Host "Runtime Release build deployed to: $TestRuntimeDir"
