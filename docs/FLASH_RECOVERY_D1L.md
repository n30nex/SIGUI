# Flash and Recovery

## Safety Rules

- Never default a serial port.
- Use `D1L_PORT` or pass `-Port` / `--port`.
- Back up flash before the first erase/full flash when possible.
- Do not use COM8, COM11, or COM29 for the D1L.

## Backup

```powershell
$env:D1L_PORT = "COMx"
python .\scripts\backup_flash_d1l.py --port $env:D1L_PORT --size 8MB
```

The script writes a `.bin`, `.sha256`, and metadata JSON under `artifacts/backups/`.

## Flash

```powershell
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
```

Release packages also include generated explicit-port flash helpers:

```powershell
.\flash_project.ps1 -Port $env:D1L_PORT
```

Erase requires an extra typed confirmation:

```powershell
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT -Erase -BackupFirst
```

Release packages include `flash_full_8mb.ps1` for factory/recovery image flashing. It requires typed confirmation because it can overwrite persisted settings, contacts, messages, and logs.

## Monitor

```powershell
.\scripts\monitor_d1l.ps1 -Port $env:D1L_PORT
```

Monitor logs are saved under `artifacts/logs/`.
