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

function Set-TestLoaderSetting {
    param(
        [string]$IniPath,
        [string]$Name,
        [string]$Value
    )

    # 仅修改 Loader 节，保留用户的 Mod 配置及其他同名设置。
    $text = [IO.File]::ReadAllText($IniPath)
    $sectionPattern = '(?ims)(^\[Loader\][^\r\n]*\r?\n)(.*?)(?=^\[|\z)'
    $section = [regex]::Match($text, $sectionPattern)
    if (-not $section.Success) {
        throw "Missing [Loader] section: $IniPath"
    }
    $body = $section.Groups[2].Value
    $settingPattern = '(?im)^[\t ]*' + [regex]::Escape($Name) + '[\t ]*=[^\r\n]*'
    $replacement = "$Name = $Value"
    if ([regex]::IsMatch($body, $settingPattern)) {
        $body = [regex]::Replace($body, $settingPattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $replacement })
    } else {
        $body += "`r`n$replacement`r`n"
    }
    $updated = $text.Substring(0, $section.Index) + $section.Groups[1].Value + $body +
        $text.Substring($section.Index + $section.Length)
    if ($updated -ne $text) {
        [IO.File]::WriteAllText($IniPath, $updated, (New-Object Text.UTF8Encoding($false)))
    }
}
