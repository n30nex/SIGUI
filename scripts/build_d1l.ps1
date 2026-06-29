param(
    [switch]$RequireFirmware,
    [string]$OutDir = "artifacts\build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null

$manifest = [ordered]@{
    schema = 1
    project = "MeshCore DeskOS D1L"
    framework = "ESP-IDF v5.1.x"
    hardware_required = $false
    firmware_build = "not_started"
    artifacts = @()
}

$idf = Get-Command idf.py -ErrorAction SilentlyContinue
if ($idf) {
    Push-Location $root
    try {
        & idf.py build
        if ($LASTEXITCODE -ne 0) {
            throw "idf.py build failed with exit code $LASTEXITCODE"
        }
        $manifest.firmware_build = "passed"
        Get-ChildItem -Path (Join-Path $root "build") -File -Include *.bin,*.elf -Recurse | ForEach-Object {
            $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName
            $shaPath = "$($_.FullName).sha256"
            "$($hash.Hash)  $($_.Name)" | Set-Content -Encoding ASCII -LiteralPath $shaPath
            $manifest.artifacts += [ordered]@{
                path = $_.FullName.Replace($root + "\", "")
                sha256 = $hash.Hash
            }
        }
    } finally {
        Pop-Location
    }
} else {
    $manifest.firmware_build = "skipped_no_idf"
    if ($RequireFirmware) {
        throw "idf.py was not found. Install/export ESP-IDF v5.1.x or rerun without -RequireFirmware for host-only checks."
    }
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
