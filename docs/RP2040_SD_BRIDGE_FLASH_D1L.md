# D1L RP2040 SD Bridge Flash And Proof

This procedure is for the D1L RP2040 SD bridge firmware only. It is not the
ESP32-S3 DeskOS firmware flash path.

## Inputs

- Use a GitHub Actions `rp2040-sd-bridge-firmware` artifact. Do not build the
  RP2040 bridge on the Windows host.
- Use an empty or sacrificial SD card for the first validation. `DESKOS_SD_STATUS`
  does not format, but a usable card may get a `/deskos` directory.
- The operator has allowed formatting the SD card inserted in the D1L for
  production validation. Use only a sacrificial or unformatted card, and format
  only through the guarded confirmation flow documented below.
- Use `COM12` only for post-flash ESP32 serial validation.
- Do not use COM11 or COM29 for this work.
- Do not send Public RF.

## Verify Artifact

```powershell
python .\scripts\verify_checksums.py artifacts\github\<run-id>\rp2040-sd-bridge-firmware
Get-FileHash artifacts\github\<run-id>\rp2040-sd-bridge-firmware\deskos_sd_bridge.ino.uf2 -Algorithm SHA256
python .\scripts\flash_rp2040_sd_bridge_uf2.py --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --expected-sha256 <sha256> --list-volumes
python .\scripts\rp2040_sd_bridge_preflight_d1l.py --port COM12 --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --expected-sha256 <sha256> --out artifacts\rp2040-preflight\d1l-rp2040-sd-bridge-preflight-COM12.json
```

The last hardware-tested UF2 before the safe-status/explicit-mount follow-up was
from Actions run `28499319258` for commit
`a8268073ae290567e26f27491c8ffa167f6f8d57`:

```text
AA71CD32C9433F1D57B1C3F243ABB2A7535728E6A3905C6A424A1E77D5F3E57E
```

Earlier Actions artifact `28494746866` was copied once after the RP2040 UF2
volume mounted as `RPI-RP2` at `G:\`. Post-copy COM12 preflight then reported
`state="rp2040_protocol_pending"` and `storage status` / `storage diag`
timeouts, so follow-up bridge changes were built by Actions. Run `28499319258`
flashed the ESP32 image and copied the RP2040 UF2 after the ESP32
`rp2040 reset` command cleared the RP2040 USB/CDC wedge; COM12 then proved
`rp2040 ping` works with `sd_touched=false`, but the SD-touching status/diag
path still timed out. Current preflight therefore includes `rp2040 ping` to
prove the flashed bridge app answers without touching SD, then uses explicit
`storage mount` for the SD-touch attempt.

The preflight command is non-destructive. It verifies the RP2040 artifact when
provided, lists UF2 bootloader volumes, queries only the selected D1L serial
port with `rp2040 status`, `rp2040 ping`, safe `storage status`, explicit
`storage mount`, bounded safe-status polling while `state="mount_pending"`,
optional `storage diag`, and `health`,
and reports the next safe action as JSON. `rp2040 ping` must report
`sd_touched=false`; `storage mount` and `storage diag` are non-formatting and
may be unavailable on older bridge firmware. If preflight reports
`state="rp2040_protocol_pending"` or `state="sd_card_not_present_diag_pending"`
and no UF2 volume is available, put the RP2040 into UF2/BOOTSEL mode before
running the copy helper. If it reports `state="sd_mount_required"`, run the
explicit mount path. If it reports `state="sd_mount_pending"` or
`state="sd_status_pending"` after a successful ping, keep the current UF2
installed and inspect the status/mount path instead of copying the same UF2
again.

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
python .\scripts\rp2040_sd_bridge_preflight_d1l.py --port COM12 --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --out artifacts\rp2040-preflight\d1l-rp2040-sd-bridge-postflash-COM12.json
python .\scripts\smoke_d1l.py --port COM12 --out artifacts\smoke\d1l-smoke-rp2040-sd-bridge-COM12.json
python .\scripts\sd_boot_prepare_acceptance_d1l.py --port COM12 --scenario correct-structure --out artifacts\sd-boot-prepare\d1l-sd-boot-correct-structure-COM12.json
python .\scripts\sd_boot_prepare_acceptance_d1l.py --port COM12 --scenario missing-structure --out artifacts\sd-boot-prepare\d1l-sd-boot-missing-structure-COM12.json
python .\scripts\sd_boot_prepare_acceptance_d1l.py --port COM12 --scenario unformatted --allow-format-confirm --out artifacts\sd-boot-prepare\d1l-sd-boot-unformatted-format-COM12.json
python .\scripts\sd_file_canary_d1l.py --port COM12 --out artifacts\sd-canary\d1l-sd-file-canary-COM12.json
python .\scripts\sd_export_canary_d1l.py --port COM12 --token export1 --out artifacts\sd-export-canary\d1l-sd-export-canary-COM12.json
python .\scripts\sd_diagnostic_export_d1l.py --port COM12 --token diag1 --out artifacts\sd-diagnostic-export\d1l-sd-diagnostic-export-COM12.json
python .\scripts\sd_data_export_d1l.py --port COM12 --token data1 --out artifacts\sd-data-export\d1l-sd-data-export-COM12.json
python .\scripts\sd_map_tile_canary_d1l.py --port COM12 --token map1 --out artifacts\sd-map-tile-canary\d1l-sd-map-tile-canary-COM12.json
python .\scripts\sd_retained_history_acceptance_d1l.py --port COM12 --out artifacts\sd-retained-history\d1l-sd-retained-history-COM12.json
python .\scripts\soak_d1l.py --port COM12 --duration-sec 90 --sample-interval-sec 30 --sample-storage --sd-file-canary --out artifacts\soak\d1l-passive-soak-rp2040-sd-bridge-COM12.json
```

Expected proof with a ready card:

- `storage status` reports `sd.rp2040_protocol_supported=true`.
- `storage mount` returns `ok=true` and a ready SD state before file canaries
  are attempted.
- `sd_boot_prepare_acceptance_d1l.py` reports ready file gates for correct and
  missing-structure cards without formatting, and reports `format_allowed=true`
  only for the explicitly authorized unformatted-card format proof.
- `rp2040 ping` reports `ok=true`, `protocol_supported=true`,
  `sd_touched=false`, `public_rf_tx=false`, and `formats_sd=false`.
- `sd.file_ops=true`, `sd.atomic_rename=true`, `sd.file_line_max >= 512`,
  `sd.file_chunk_max >= 192`, and `sd.path_max >= 96`.
- `message_store_backend="sd"`, `dm_store_backend="sd"`,
  `route_store_backend="sd"`, `packet_log_backend="sd"`,
  `data_backend="mixed"`, and `setup_action="retained_history_sd_enabled"`.
- `storage filecanary` returns `ok=true`, `rename_replace=true`,
  `read_final=true`, `delete_final=true`, and `stat_deleted=true`.
- `storage export-canary <token>` returns `ok=true`, `write_tmp=true`,
  `read_tmp=true`, `rename_replace=true`, `stat_final=true`, `read_final=true`,
  leaves `exports/diagnostics/export-canary-<token>.json` present, and reports
  `public_rf_tx=false` plus `formats_sd=false`.
- `storage export-diagnostics <token>` returns `ok=true`, writes a chunked
  diagnostic bundle to `exports/diagnostics/diagnostic-export-<token>.json`,
  verifies temp and final readback, leaves the final JSON present, and reports
  `public_rf_tx=false` plus `formats_sd=false`.
- `storage export-data <token>` returns `ok=true`, writes a chunked sampled
  user-data bundle to `exports/data/data-export-<token>.json`, verifies temp
  and final readback, leaves the final JSON present, reports
  `private_identity_exported=false`, and reports `public_rf_tx=false` plus
  `formats_sd=false`.
- `storage map-tile-canary <token>` returns `ok=true`, writes
  `map/tiles/z12/x1/y2-<token>.tile` through temp write/read and atomic rename,
  leaves the final synthetic tile present, and reports `public_rf_tx=false`
  plus `formats_sd=false`.
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
python .\tools\rp2040_sd_protocol.py --scenario ready --map-tile-canary-transcript --token map1
```
