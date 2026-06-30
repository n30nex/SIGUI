# D1L RP2040 SD Bridge Flash And Proof

This procedure is for the D1L RP2040 SD bridge firmware only. It is not the
ESP32-S3 DeskOS firmware flash path.

## Inputs

- Use a GitHub Actions `rp2040-sd-bridge-firmware` artifact. Do not build the
  RP2040 bridge on the Windows host.
- Use an empty or sacrificial SD card for the first validation. `DESKOS_SD_STATUS`
  does not format, but a usable card may get a `/deskos` directory.
- Use `COM12` only for post-flash ESP32 serial validation.
- Do not use COM11 or COM29 for this work.
- Do not send Public RF.

## Verify Artifact

```powershell
python .\scripts\verify_checksums.py artifacts\github\<run-id>\rp2040-sd-bridge-firmware
Get-FileHash artifacts\github\<run-id>\rp2040-sd-bridge-firmware\deskos_sd_bridge.ino.uf2 -Algorithm SHA256
```

The UF2 from Actions run `28445509629` was previously verified as:

```text
689F85820F118C8F6EA06F0E13ED469D18391231D689720AB8625AD228298AEF
```

## Flash

1. Put the D1L RP2040, not the ESP32-S3, into UF2/BOOTSEL mass-storage mode.
2. Confirm Windows mounted a UF2 bootloader volume. The volume should expose
   UF2 bootloader metadata such as `INFO_UF2.TXT` or `INDEX.HTM`.
3. Copy only `deskos_sd_bridge.ino.uf2` to that RP2040 UF2 volume.
4. Wait for the volume to disconnect/reboot, then power-cycle the D1L if needed.

Do not use `flash_d1l.ps1`, `flash_project.ps1`, or `esptool` for this RP2040
step; those are ESP32-S3 paths.

## Prove

Run these from the repo root after the RP2040 reboots:

```powershell
python .\scripts\smoke_d1l.py --port COM12 --out artifacts\smoke\d1l-smoke-rp2040-sd-bridge-COM12.json
python .\scripts\sd_file_canary_d1l.py --port COM12 --out artifacts\sd-canary\d1l-sd-file-canary-COM12.json
python .\scripts\soak_d1l.py --port COM12 --duration-sec 90 --sample-interval-sec 30 --out artifacts\soak\d1l-passive-soak-rp2040-sd-bridge-COM12.json
```

Expected proof with a ready card:

- `storage status` reports `sd.rp2040_protocol_supported=true`.
- `sd.file_ops=true`, `sd.atomic_rename=true`, `sd.file_line_max >= 512`,
  `sd.file_chunk_max >= 192`, and `sd.path_max >= 96`.
- `packet_log_backend="sd"`, `data_backend="mixed"`, and
  `setup_action="packet_log_canary_enabled"`.
- `storage filecanary` returns `ok=true`, `rename_replace=true`,
  `read_final=true`, `delete_final=true`, and `stat_deleted=true`.
- The passive soak reports zero active Public TX, zero crash-like resets, and
  monotonic uptime.

Expected proof before the RP2040 bridge is flashed, with no card, or with an
unsupported card is a safe refusal: onboard NVS remains the fallback and no
format is performed.
