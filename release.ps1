$ErrorActionPreference = 'Stop'

$RuntimeRoot = $PSScriptRoot
$SolutionPath = Join-Path $RuntimeRoot 'StereoVisionHacks.sln'
$TestRuntimeDir = Join-Path `
    $env:USERPROFILE `
    'Desktop\SSMT3\SSMTDefaultCacheFolder\3Dmigoto\GIMI'

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

& $MSBuild $SolutionPath `
    /m `
    /p:Configuration=Release `
    /p:Platform=x64 `
    /v:minimal

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
