# D1L SD Card Guided Install

Use this when COM12 is working but the RP2040 does not answer the DeskOS bridge
protocol and no autonomous UF2 path appears. The only manual action is putting
the RP2040 into BOOTSEL/UF2 mode twice: once for the official SD smoke proof and
once to restore the DeskOS SD bridge.

## Before Running

- Use the latest green GitHub Actions run artifacts.
- Prepare the SD card as FAT32 on a computer, then insert it in the D1L.
- Keep D1L post-flash validation on `COM12`.
- Do not use `COM11` or `COM29`.
- Do not format the SD card from the device, script, serial console, or UI.
- Do not send Public RF.

## Guided Command

From the repository root:

```powershell
python .\scripts\guided_sd_install_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --d1l-port COM12 --rp2040-port COM16
```

The script will:

1. Verify `rp2040-seeed-official-sd-smoke-firmware` and
   `rp2040-sd-bridge-firmware` checksums.
2. Pause for the RP2040 UF2/BOOTSEL disk.
3. Copy only `seeed_official_sd_smoke.ino.uf2`.
4. Capture the official Seeed SD smoke JSON from the RP2040 USB serial port.
5. Pause for the RP2040 UF2/BOOTSEL disk again.
6. Copy only `deskos_sd_bridge.ino.uf2`.
7. Verify `rp2040 ping` on COM12.
8. Run the RP2040 SD bridge preflight.
9. If preflight reports the SD file gate ready, run the short SD file/export
   canaries.

The report is written to:

```text
artifacts\hardware\com12\d1l-guided-sd-install-<sha7>-actions<run-id>.json
```

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

If official smoke fails, preserve the generated artifact; it contains the raw
SD diagnostic fields needed to distinguish card/power/SPI/filesystem failures.
If official smoke passes but bridge preflight fails, the SD hardware path works
and the remaining issue is in the DeskOS bridge/protocol path.
