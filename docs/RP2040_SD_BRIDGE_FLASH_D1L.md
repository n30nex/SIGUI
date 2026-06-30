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
python .\scripts\flash_rp2040_sd_bridge_uf2.py --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --expected-sha256 <sha256> --list-volumes
```

The UF2 from Actions run `28445509629` was previously verified as:

```text
689F85820F118C8F6EA06F0E13ED469D18391231D689720AB8625AD228298AEF
```

## Flash

1. Put the D1L RP2040, not the ESP32-S3, into UF2/BOOTSEL mass-storage mode.
2. Confirm Windows mounted a UF2 bootloader volume. The volume should expose
   UF2 bootloader metadata such as `INFO_UF2.TXT` or `INDEX.HTM`.
3. Dry-run the guarded copy helper. This verifies `SHA256SUMS.txt`, confirms
   the target has UF2 bootloader metadata, and refuses ambiguous/missing volumes:

   ```powershell
   python .\scripts\flash_rp2040_sd_bridge_uf2.py --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --volume <RP2040_UF2_DRIVE>: --expected-sha256 <sha256>
   ```

4. Copy only `deskos_sd_bridge.ino.uf2` with the explicit `--copy` flag:

   ```powershell
   python .\scripts\flash_rp2040_sd_bridge_uf2.py --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --volume <RP2040_UF2_DRIVE>: --expected-sha256 <sha256> --copy --out artifacts\rp2040-flash\rp2040-sd-bridge-uf2-copy.json
   ```

5. Wait for the volume to disconnect/reboot, then power-cycle the D1L if needed.

Do not use `flash_d1l.ps1`, `flash_project.ps1`, or `esptool` for this RP2040
step; those are ESP32-S3 paths.

## Prove

Run these from the repo root after the RP2040 reboots:

```powershell
python .\scripts\smoke_d1l.py --port COM12 --out artifacts\smoke\d1l-smoke-rp2040-sd-bridge-COM12.json
python .\scripts\sd_file_canary_d1l.py --port COM12 --out artifacts\sd-canary\d1l-sd-file-canary-COM12.json
python .\scripts\sd_retained_history_acceptance_d1l.py --port COM12 --out artifacts\sd-retained-history\d1l-sd-retained-history-COM12.json
python .\scripts\soak_d1l.py --port COM12 --duration-sec 90 --sample-interval-sec 30 --sample-storage --sd-file-canary --out artifacts\soak\d1l-passive-soak-rp2040-sd-bridge-COM12.json
```

Expected proof with a ready card:

- `storage status` reports `sd.rp2040_protocol_supported=true`.
- `sd.file_ops=true`, `sd.atomic_rename=true`, `sd.file_line_max >= 512`,
  `sd.file_chunk_max >= 192`, and `sd.path_max >= 96`.
- `message_store_backend="sd"`, `dm_store_backend="sd"`,
  `route_store_backend="sd"`, `packet_log_backend="sd"`,
  `data_backend="mixed"`, and `setup_action="retained_history_sd_enabled"`.
- `storage filecanary` returns `ok=true`, `rename_replace=true`,
  `read_final=true`, `delete_final=true`, and `stat_deleted=true`.
- `storage retained-canary <token>` returns `ok=true`, appends synthetic Public,
  DM, route, and packet rows without Public RF or formatting, and
  `sd_retained_history_acceptance_d1l.py` proves those rows are readable before
  and after a reboot.
- The passive soak reports zero active Public TX, zero crash-like resets, and
  monotonic uptime, plus stable SD state/backend samples and repeated passing
  `storage filecanary` results.

Expected proof before the RP2040 bridge is flashed, with no card, or with an
unsupported card is a safe refusal: onboard NVS remains the fallback and no
format is performed. For that pre-flash/fallback state only, the soak command
may be run with `--allow-sd-unavailable` so the expected `storage filecanary`
preflight refusal is recorded without failing the passive stability window:

```powershell
python .\scripts\soak_d1l.py --port COM12 --duration-sec 90 --sample-interval-sec 30 --sample-storage --sd-file-canary --allow-sd-unavailable --out artifacts\soak\d1l-passive-soak-sd-aware-pre-rp2040-flash-COM12.json
```

The host simulator can print the deterministic filecanary request/reply grammar
without hardware:

```powershell
python .\tools\rp2040_sd_protocol.py --scenario ready --file-canary-transcript
```
