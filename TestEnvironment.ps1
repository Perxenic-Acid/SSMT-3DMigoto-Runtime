function Resolve-TestRuntimeDirectory {
    param(
        [string]$TestRuntimeDir,
        [string]$GamePreset = 'GIMI'
    )

    if (-not $TestRuntimeDir) {
        $TestRuntimeDir = $env:SSMT_TEST_RUNTIME_DIR
    }

    if (-not $TestRuntimeDir) {
        if ($GamePreset -notmatch '^[A-Za-z0-9_-]+$') {
            throw "Invalid game preset: $GamePreset"
        }
        $configPath = Join-Path $env:LOCALAPPDATA "SSMT4GlobalConfigs\Games\$GamePreset\Config.json"
        if (Test-Path -LiteralPath $configPath -PathType Leaf) {
            $config = Get-Content -Raw -LiteralPath $configPath -Encoding UTF8 | ConvertFrom-Json
            $TestRuntimeDir = $config.installDir
        }
    }

    if (-not $TestRuntimeDir -and $GamePreset -eq 'GIMI') {
        $TestRuntimeDir = Join-Path $env:USERPROFILE 'Desktop\SSMT3\SSMTDefaultCacheFolder\3Dmigoto\GIMI'
    }

    if (-not $TestRuntimeDir -or -not (Test-Path -LiteralPath $TestRuntimeDir -PathType Container)) {
        throw "Runtime directory for $GamePreset was not found. Set -TestRuntimeDir or SSMT_TEST_RUNTIME_DIR."
    }

    return (Resolve-Path -LiteralPath $TestRuntimeDir).ProviderPath
}

function Set-TestIniSetting {
    param(
        [string]$IniPath,
        [string]$Section,
        [string]$Name,
        [string]$Value
    )

    # 按节修改，避免误改 Mod 命令或其他节中的同名键。
    $text = [IO.File]::ReadAllText($IniPath)
    $sectionPattern = '(?ims)(^\[' + [regex]::Escape($Section) + '\][^\r\n]*(?:\r?\n|\z))(.*?)(?=^\[|\z)'
    $sectionMatch = [regex]::Match($text, $sectionPattern)
    if (-not $sectionMatch.Success) {
        $updated = $text.TrimEnd("`r", "`n") + "`r`n`r`n[$Section]`r`n$Name = $Value`r`n"
        [IO.File]::WriteAllText($IniPath, $updated, (New-Object Text.UTF8Encoding($false)))
        return
    }
    $body = $sectionMatch.Groups[2].Value
    $settingPattern = '(?im)^[\t ]*' + [regex]::Escape($Name) + '[\t ]*=[^\r\n]*'
    $replacement = "$Name = $Value"
    if ([regex]::IsMatch($body, $settingPattern)) {
        $body = [regex]::Replace($body, $settingPattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $replacement })
    } else {
        $body += "`r`n$replacement`r`n"
    }
    $header = $sectionMatch.Groups[1].Value
    if (-not $header.EndsWith("`n")) { $header += "`r`n" }
    $updated = $text.Substring(0, $sectionMatch.Index) + $header + $body +
        $text.Substring($sectionMatch.Index + $sectionMatch.Length)
    if ($updated -ne $text) {
        [IO.File]::WriteAllText($IniPath, $updated, (New-Object Text.UTF8Encoding($false)))
    }
}

function Set-TestLoaderSetting {
    param([string]$IniPath, [string]$Name, [string]$Value)

    # 保留现有调用方的 Loader 校验；日志诊断则允许补充原本不存在的节。
    if (-not [regex]::IsMatch([IO.File]::ReadAllText($IniPath), '(?im)^\[Loader\][^\r\n]*(?:\r?\n|\z)')) {
        throw "Missing [Loader] section: $IniPath"
    }
    Set-TestIniSetting $IniPath 'Loader' $Name $Value
}
