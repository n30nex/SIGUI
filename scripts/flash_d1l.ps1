param(
    [string]$Port = $env:D1L_PORT,
    [switch]$Erase,
    [switch]$BackupFirst
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "No D1L port supplied. Set D1L_PORT or pass -Port."
}

if ($BackupFirst) {
    & python (Join-Path $PSScriptRoot "backup_flash_d1l.py") --port $Port --size 8MB
    if ($LASTEXITCODE -ne 0) {
        throw "Backup failed; refusing to flash."
    }
}

Push-Location $root
try {
    if ($Erase) {
        $confirm = Read-Host "Type ERASE-$Port to erase before flashing"
        if ($confirm -ne "ERASE-$Port") {
            throw "Erase confirmation failed."
        }
        & idf.py -p $Port erase-flash
        if ($LASTEXITCODE -ne 0) {
            throw "erase-flash failed with exit code $LASTEXITCODE"
        }
    }
    & idf.py -p $Port flash
    if ($LASTEXITCODE -ne 0) {
        throw "flash failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
