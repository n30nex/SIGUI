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
    framework = "ESP-IDF v5.5.4"
    hardware_required = $false
    firmware_build = "disabled_local_github_actions_only"
    artifacts = @()
}

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
