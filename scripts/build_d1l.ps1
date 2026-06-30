param(
    [switch]$RequireFirmware,
    [string]$OutDir = "artifacts\build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null

if ($RequireFirmware) {
    throw "Local firmware builds are disabled for this workspace. Use GitHub Actions d1l-ci firmware-build for binaries."
}

$manifest = [ordered]@{
    schema = 1
    project = "MeshCore DeskOS D1L"
    framework = "ESP-IDF v5.1.x"
    hardware_required = $false
    firmware_build = "disabled_local_github_actions_only"
    artifacts = @()
}

function Set-D1lSafeSdkconfig {
    param([string]$ConfigPath)

    if (-not (Test-Path -LiteralPath $ConfigPath)) {
        return
    }

    $text = Get-Content -Raw -LiteralPath $ConfigPath
    $updated = $text
    $updated = $updated -replace '(?m)^CONFIG_LCD_AVOID_TEAR=y$', '# CONFIG_LCD_AVOID_TEAR is not set'
    $updated = $updated -replace '(?m)^CONFIG_LCD_LVGL_DIRECT_MODE=y$', '# CONFIG_LCD_LVGL_DIRECT_MODE is not set'

    if ($updated -ne $text) {
        Set-Content -Encoding ASCII -LiteralPath $ConfigPath -Value $updated
        Write-Host "Updated local sdkconfig to disable LCD avoid-tear/direct-mode for D1L stability"
    }
}

Set-D1lSafeSdkconfig -ConfigPath (Join-Path $root "sdkconfig")

$manifestPath = Join-Path $out "build-manifest.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding ASCII -LiteralPath $manifestPath
Write-Host "Wrote $manifestPath"

$pytest = Get-Command pytest -ErrorAction SilentlyContinue
if ($pytest) {
    Push-Location $root
    try {
        & python -m pytest tests
        if ($LASTEXITCODE -ne 0) {
            throw "pytest failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "pytest not found; skipping host tests"
}
