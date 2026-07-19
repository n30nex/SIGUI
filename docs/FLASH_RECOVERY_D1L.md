# Core 1.0 Install and Recovery

This repository guide applies only to an extracted MeshCore DeskOS D1L Core
1.0 release package. Build firmware only in GitHub Actions and use the exact
package whose commit and Actions run match the release candidate.

## Safety rules

- Verify every package file against the package-root `SHA256SUMS.txt` before
  running either flash helper.
- Use `COM12` for the D1L app, console, and flash target.
- `COM16` is reserved for separately authorized SD/RP2040 work and is not
  needed by the Core package.
- Never use COM8, COM11, or COM29.
- Never format SD.
- A normal install is non-erasing. Do not use the full-flash helper for an
  update.

## Verify the extracted package

Run this from the extracted package root:

```powershell
$ErrorActionPreference = "Stop"
Get-Content .\SHA256SUMS.txt | ForEach-Object {
    if ($_ -notmatch '^([0-9a-f]{64})  \./(.+)$') {
        throw "Invalid SHA256SUMS.txt row: $_"
    }
    $expected = $Matches[1]
    $path = Join-Path (Get-Location) $Matches[2]
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) {
        throw "Checksum mismatch: $path"
    }
}
```

Stop if any checksum, expected commit, Actions run, release profile, or SD mode
does not match the candidate being released.

## Normal non-erasing install

The extracted package supplies the only supported normal-install command:

```powershell
$env:D1L_PORT = "COM12"
.\flash_project.ps1 -Port $env:D1L_PORT
```

Do not substitute a repository build directory, `idf.py flash`, a predecessor
artifact, or an unverified binary.

## Destructive recovery only

Use `flash_full_8mb.ps1` only when normal install cannot recover the device.
It writes the full 8 MB image and can overwrite settings, contacts, messages,
and logs. Confirm that a recoverable backup exists when possible, re-verify
the package checksums, then run:

```powershell
$env:D1L_PORT = "COM12"
.\flash_full_8mb.ps1 -Port $env:D1L_PORT
```

The helper requires the typed confirmation `FULL-FLASH-COM12`. Cancel if the
port or confirmation text differs.
