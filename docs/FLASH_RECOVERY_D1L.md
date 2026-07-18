# Flash and Recovery

This guide applies to the MeshCore DeskOS D1L Core 1.0 package. Use only the
exact GitHub Actions package for the tagged commit after every checksum passes.

## Safety Rules

- Firmware binaries are built only in GitHub Actions.
- Use `COM12` for the D1L app, console, and flash target.
- `COM16` is reserved for separately authorized SD/RP2040 work and is not
  needed by the Core package.
- Back up flash before the first erase/full flash when possible.
- Never use COM8, COM11, or COM29.
- Never format SD.
- Normal project flash is non-erasing.

## Backup

```powershell
$env:D1L_PORT = "COM12"
python .\scripts\backup_flash_d1l.py --port $env:D1L_PORT --size 8MB
```

The script writes a `.bin`, `.sha256`, and metadata JSON under `artifacts/backups/`.

## Flash

```powershell
$env:D1L_PORT = "COM12"
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
```

Release packages also include generated explicit-port flash helpers:

```powershell
$env:D1L_PORT = "COM12"
.\flash_project.ps1 -Port $env:D1L_PORT
```

Erase requires an extra typed confirmation:

```powershell
$env:D1L_PORT = "COM12"
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT -Erase -BackupFirst
```

Release packages include `flash_full_8mb.ps1` for factory/recovery image flashing. It requires typed confirmation because it can overwrite persisted settings, contacts, messages, and logs.

## Monitor

```powershell
$env:D1L_PORT = "COM12"
.\scripts\monitor_d1l.ps1 -Port $env:D1L_PORT
```

Monitor logs are saved under `artifacts/logs/`.
