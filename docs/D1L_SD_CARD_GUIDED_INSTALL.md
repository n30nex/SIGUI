# D1L SD Card Guided Install

Use this flow when an operator needs one public, repeatable SD install and
validation command. It can download or accept GitHub Actions artifacts, flash
the ESP32 app on COM12, guide the RP2040 UF2/BOOTSEL steps on COM16, run the SD
checks, and write one validation report. The script first tries a safe RP2040
USB CDC 1200-baud bootloader touch; the manual fallback is putting the RP2040
into BOOTSEL/UF2 mode for the official SD smoke proof and bridge restore.

## Before Running

- Use the latest green GitHub Actions run artifacts.
- Prepare the SD card as FAT32 on a computer, then insert it in the D1L.
- Keep D1L post-flash validation on `COM12`.
- Do not use `COM8`, `COM11`, or `COM29`.
- Do not format the SD card from the device, script, serial console, or UI.
- Do not send Public RF.

## Guided Command

From the repository root:

```powershell
python .\scripts\guided_sd_install_d1l.py --download-artifacts --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --d1l-port COM12 --rp2040-port COM16
```

By default, the guided command verifies `d1l-firmware-artifacts`, flashes the
ESP32 app image from that Actions build on COM12, and then starts the RP2040 SD
flow. If the matching ESP32 image is already flashed, add
`--skip-esp32-flash` and keep the generated report as a partial/operator
artifact; the final release gate requires a full guided report with the ESP32
flash included.

Autonomous SD refresh, when the bench should avoid manual BOOTSEL prompts:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --download-artifacts --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --refresh-rp2040-smoke
```

The script will:

1. Verify `d1l-firmware-artifacts`, `rp2040-seeed-official-sd-smoke-firmware`,
   and `rp2040-sd-bridge-firmware` checksums.
2. Flash the ESP32 app image from `d1l-firmware-artifacts\build` on COM12.
3. Try the RP2040 bootloader touch, then pause for the UF2/BOOTSEL disk if
   needed.
4. Copy only `seeed_official_sd_smoke.ino.uf2`.
5. Capture the official Seeed SD smoke JSON from the RP2040 USB serial port.
6. Try the RP2040 bootloader touch again, then pause for the UF2/BOOTSEL disk if
   needed.
7. Copy only `deskos_sd_bridge.ino.uf2`.
8. Verify `rp2040 ping` on COM12.
9. Run the RP2040 SD bridge preflight.
10. If preflight reports the SD file gate ready, run the short SD file/export
   canaries. The autonomous runner additionally captures raw diagnostics,
   map-tile, retained-history, reboot/remount, and RP2040-unavailable evidence.

For ESP32/UI-only firmware validation after the RP2040 bridge has already been
proved, do not run the refresh command above. Use the default existing-bridge
path, or skip the SD suite entirely when the fix does not touch storage:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --skip-sd-suite --include-ui-probes
```

That path flashes only the ESP32 artifact on COM12 and does not copy any RP2040
UF2 file.

The report is written to:

```text
artifacts\hardware\com12\d1l-guided-sd-install-<sha7>-actions<run-id>.json
```

The release gate looks for that report as `guided_sd_install_validation`. A
passing final report must be `mode="guided-hardware"`, use COM12/COM16, include
the ESP32 flash, report official smoke and bridge ping success, include SD
canary results, and keep `public_rf_tx=false` plus `formats_sd=false`.

## At Each Prompt

Put the D1L RP2040, not the ESP32-S3, into UF2/BOOTSEL mode. The expected
Windows result is a mounted UF2 disk containing `INFO_UF2.TXT` or `INDEX.HTM`.
After the disk appears, press Enter in the script window.

If Windows mounts more than one UF2 disk, rerun with an explicit volume:

```powershell
python .\scripts\guided_sd_install_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --uf2-volume E:
```

## Passing Result

For SD support to be considered working:

- Official smoke reports `test="seeed_official_sd_smoke"` and `ok=true`.
- Bridge restore reports `rp2040 ping` `ok=true` and
  `protocol_supported=true`.
- Preflight reports `ready_for_sd_acceptance=true`.
- SD canaries report `ok=true`.
- Every report keeps `public_rf_tx=false` and `formats_sd=false`.

This proves the installed FAT32 card path. Public release still requires actual
no-card and unformatted/non-FAT32 evidence, <=32GB multi-card evidence, and
SD-slot electrical/power evidence.

If official smoke fails, preserve the generated artifact; it contains the raw
SD diagnostic fields needed to distinguish card/power/SPI/filesystem failures.
If official smoke passes but bridge preflight fails, the SD hardware path works
and the remaining issue is in the DeskOS bridge/protocol path.
