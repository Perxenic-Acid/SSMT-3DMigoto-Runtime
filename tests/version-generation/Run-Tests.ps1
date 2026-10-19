[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$runtimeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixture = Join-Path $tempRoot ('ssmt-runtime-version-' + [Guid]::NewGuid().ToString('N'))
$header = Join-Path $fixture 'version.generated.h'
$generator = Join-Path $fixture 'tools\generate-version.ps1'

function Read-Version {
    $parts = foreach ($name in @('MAJOR', 'MINOR', 'REVISION', 'BUILD')) {
        $match = Select-String -LiteralPath $header -Pattern "^#define VERSION_$name\s+(\d+)" | Select-Object -First 1
        if (-not $match) { throw "Missing VERSION_$name" }
        [int]$match.Matches[0].Groups[1].Value
    }
    $parts -join '.'
}

try {
    # 共享对象的隔离 clone 不携带标签，不改本机仓库，也不访问远端。
    & git clone --quiet --shared --no-tags --no-checkout -- $runtimeRoot $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Isolated clone failed' }
    New-Item -ItemType Directory -Path (Join-Path $fixture 'tools') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $runtimeRoot 'tools\generate-version.ps1') -Destination $generator
    Copy-Item -LiteralPath (Join-Path $runtimeRoot 'tools\version-baseline.json') -Destination (Join-Path $fixture 'tools\version-baseline.json')
    Copy-Item -LiteralPath (Join-Path $runtimeRoot 'version.h') -Destination (Join-Path $fixture 'version.h')
    $baseline = Get-Content -LiteralPath (Join-Path $fixture 'tools\version-baseline.json') -Raw | ConvertFrom-Json
    $count = & git -C $fixture rev-list "$($baseline.commit)..HEAD" --count
    if ($LASTEXITCODE -ne 0) { throw 'Version baseline missing from history' }
    $expected = "$($baseline.version.TrimStart('v')).$count"
    Push-Location -LiteralPath $fixture
    try {
        $engine = (Get-Process -Id $PID).Path
        & $engine -NoProfile -ExecutionPolicy Bypass -File $generator -VersionFile (Join-Path $fixture 'version.h') -OutputFile $header
        if ($LASTEXITCODE -ne 0) { throw 'Untagged version generation failed' }
        if ((Read-Version) -ne $expected) { throw "Untagged build must be $expected, got $(Read-Version)" }

        & git tag $baseline.version $baseline.commit
        if ($LASTEXITCODE -ne 0) { throw 'Fixture tag failed' }
        & $engine -NoProfile -ExecutionPolicy Bypass -File $generator -VersionFile (Join-Path $fixture 'version.h') -OutputFile $header
        if ($LASTEXITCODE -ne 0 -or (Read-Version) -ne $expected) { throw 'Tagged and untagged builds have different versions' }
    }
    finally { Pop-Location }
    Write-Host "PASS: tagged and tag-free clones both generate Runtime $expected"
}
finally {
    # 只移除本测试创建的临时目录，递归操作前验证绝对路径与固定前缀。
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        ([IO.Path]::GetFileName($resolved) -like 'ssmt-runtime-version-*') -and
        (Test-Path -LiteralPath $resolved)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
