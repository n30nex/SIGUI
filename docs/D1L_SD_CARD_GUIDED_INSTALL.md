# D1L SD Card Guided Install

Prefer `scripts/autonomous_hardware_validate_d1l.py` for COM12/COM16 hardware
validation. Use this guided flow only when COM12 is working but the RP2040 does
not answer the DeskOS bridge protocol and no autonomous UF2 path appears. The
only manual action is putting the RP2040 into BOOTSEL/UF2 mode twice: once for
the official SD smoke proof and once to restore the DeskOS SD bridge.

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
python .\scripts\guided_sd_install_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --d1l-port COM12 --rp2040-port COM16
```

Autonomous exact-artifact SD refresh:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --refresh-rp2040-smoke
```

The script will:

1. Bind the host-success marker, release manifest, packaged files, and standalone
   firmware hashes to the requested canonical 40-hex commit and explicitly
   supplied numeric Actions run.
2. Request the configured COM16 RP2040 bootloader transition and accept only an
   explicitly authorized UF2 volume or one newly correlated UF2 volume.
3. Copy only `seeed_official_sd_smoke.ino.uf2`.
4. Capture the official Seeed SD smoke JSON from COM16, then capture the bounded
   RP2040-unavailable fallback receipt before restoring the production bridge.
5. Restore the checksum-verified `deskos_sd_bridge.ino.uf2`, run a preflight,
   and require a clean `READY_SD` zero-counter gate before diagnostics.
6. Poll the isolated raw diagnostic only until its bounded deadline.
7. Restore that exact bridge UF2 again and reflash the checksum-verified ESP32
   project image to establish a clean post-diagnostic boot boundary.
8. Run a fresh preflight and require `READY_SD`, non-stale SD status, all four
   retained stores on SD, zero retained failure counters, and no degradation
   latch.
9. Only after that clean gate, run file/export/map/retained/reboot canaries.
   Any failed later SD stage preserves
   its receipt, runs the release audit, attempts bounded exact recovery, and
   stops all subsequent canaries.

A UF2 disk that was present before the commanded D1L bootloader transition is
not eligible for automatic selection. Pass `--uf2-volume <drive>:` to authorize
that exact pre-existing volume; otherwise the runner requires exactly one newly
appeared UF2 volume correlated with the COM12/COM16 transition.
COM17 or any other discovered RP2040-looking serial device is inventory-only;
the runner fails closed when configured COM16 is absent and never touches an
alternative port.

For ESP32/UI-only firmware validation after the RP2040 bridge has already been
proved, do not run the SD command above. Skip the SD suite entirely when the fix
does not touch storage:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --skip-sd-suite --include-ui-probes
```

That path flashes only the ESP32 artifact on COM12 and does not copy any RP2040
UF2 file.

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

This proves the installed FAT32 card path. Public release still requires actual
no-card and unformatted/non-FAT32 evidence, <=32GB multi-card evidence, and
SD-slot electrical/power evidence.

If official smoke fails, preserve the generated artifact; it contains the raw
SD diagnostic fields needed to distinguish card/power/SPI/filesystem failures.
If official smoke passes but bridge preflight fails, the SD hardware path works
and the remaining issue is in the DeskOS bridge/protocol path.
