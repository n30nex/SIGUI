param(
    [string]$Port = $env:D1L_PORT,
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "No D1L port supplied. Set D1L_PORT or pass -Port."
}

$logDir = Join-Path $root "artifacts\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $logDir "d1l-monitor-$stamp.log"

Push-Location $root
try {
    Write-Host "Logging monitor output to $logPath"
    & idf.py -p $Port monitor -b $Baud 2>&1 | Tee-Object -FilePath $logPath
} finally {
    Pop-Location
}
