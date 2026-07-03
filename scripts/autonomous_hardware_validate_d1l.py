#!/usr/bin/env python3
"""Autonomous hardware validation runner for MeshCore DeskOS D1L.

This orchestrates existing evidence-producing scripts around the expected
current hardware routing: ESP32 console plus RP2040 USB/UF2.
It intentionally refuses the protected mesh/radio ports.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from artifact_metadata import git_metadata
    from flash_rp2040_sd_bridge_uf2 import (
        FlashGuardError,
        candidate_volumes,
        choose_volume,
        metadata_for_volume,
        parse_sha256sums,
        sha256_file,
    )
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import git_metadata
    from scripts.flash_rp2040_sd_bridge_uf2 import (
        FlashGuardError,
        candidate_volumes,
        choose_volume,
        metadata_for_volume,
        parse_sha256sums,
        sha256_file,
    )
    from scripts.smoke_d1l import send_console_command


DEFAULT_D1L_PORT = "COM" + "12"
DEFAULT_RP2040_PORT = "COM" + "16"
FORBIDDEN_PORTS = {"COM" + "11", "COM" + "29"}
OFFICIAL_SMOKE_UF2 = "seeed_official_sd_smoke.ino.uf2"
BRIDGE_UF2 = "deskos_sd_bridge.ino.uf2"
DEFAULT_SCROLL_SCREENS = "home,public_messages,dm_thread,nodes,packets,settings,storage,wifi,map"
BOOTLOADER_TOUCH_TIMEOUT_SECONDS = 3.0
BOOTLOADER_ENTRY_RESCAN_SECONDS = 3.0
RP2040_USB_VIDS = {"2E8A", "239A", "CAFE"}
RP2040_USB_KEYWORDS = (
    "rp2040",
    "raspberry pi pico",
    "pico",
    "bootsel",
    "circuitpython",
    "adafruit",
)
RP2040_DOUBLE_RESET_SWEEP_MS = (
    (50, 150, 1500),
    (30, 300, 1500),
    (100, 500, 2000),
)
RP2040_BAUD_PROBE_TIMEOUT_MS = 700
RP2040_BAUD_PROBE_COMMAND_TIMEOUT_SECONDS = 20.0


@dataclass(frozen=True)
class RunContext:
    root: Path
    commit: str
    short_commit: str
    github_run_id: str
    github_run_dir: Path
    d1l_port: str
    rp2040_port: str
    hardware_dir: Path
    rp2040_hardware_dir: Path
    baud: int
    esp32_flash_baud: int


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def normalize_port(value: str | None) -> str:
    return (value or "").strip().upper()


def enforce_port_guard(*ports: str | None) -> None:
    forbidden = sorted({normalize_port(port) for port in ports if normalize_port(port) in FORBIDDEN_PORTS})
    if forbidden:
        raise ValueError(f"Refusing to use forbidden port(s): {', '.join(forbidden)}")


def git_value(root: Path, *args: str) -> str | None:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    return result.stdout.strip() or None


def resolve_commit(root: Path, value: str | None) -> str:
    if value:
        return value
    commit = git_value(root, "rev-parse", "HEAD")
    if not commit:
        raise ValueError("Could not determine commit; pass --commit")
    return commit


def resolve_github_run_dir(root: Path, run_id: str | None, explicit_dir: str | None) -> Path:
    if explicit_dir:
        return (root / explicit_dir).resolve() if not Path(explicit_dir).is_absolute() else Path(explicit_dir)
    if not run_id:
        raise ValueError("Pass --github-run-id or --github-run-dir")
    current = root / "artifacts" / "github" / f"{run_id}-current"
    if current.exists():
        return current.resolve()
    return (root / "artifacts" / "github" / run_id).resolve()


def sha_from_run_dir(path: Path) -> str | None:
    for part in path.parts:
        if len(part) >= 40 and all(ch in "0123456789abcdefABCDEF" for ch in part[:40]):
            return part[:40]
    return None


def build_context(args: argparse.Namespace) -> RunContext:
    root = Path(args.root).resolve()
    d1l_port = normalize_port(args.d1l_port)
    rp2040_port = normalize_port(args.rp2040_port)
    enforce_port_guard(d1l_port, rp2040_port)
    commit = resolve_commit(root, args.commit)
    run_dir = resolve_github_run_dir(root, args.github_run_id, args.github_run_dir)
    run_id = args.github_run_id or args.github_run_dir_name or run_dir.name.removesuffix("-current")
    return RunContext(
        root=root,
        commit=commit,
        short_commit=commit[:7],
        github_run_id=run_id,
        github_run_dir=run_dir,
        d1l_port=d1l_port,
        rp2040_port=rp2040_port,
        hardware_dir=root / "artifacts" / "hardware" / d1l_port.lower(),
        rp2040_hardware_dir=root / "artifacts" / "hardware" / rp2040_port.lower(),
        baud=args.baud,
        esp32_flash_baud=args.esp32_flash_baud,
    )


def artifact_dirs(ctx: RunContext) -> dict[str, Path]:
    return {
        "esp32": ctx.github_run_dir / "d1l-firmware-artifacts",
        "esp32_build": ctx.github_run_dir / "d1l-firmware-artifacts" / "build",
        "bridge": ctx.github_run_dir / "rp2040-sd-bridge-firmware",
        "official_smoke": ctx.github_run_dir / "rp2040-seeed-official-sd-smoke-firmware",
    }


def official_sd_smoke_out(ctx: RunContext) -> Path:
    return ctx.rp2040_hardware_dir / f"rp2040_seeed_official_sd_smoke_{ctx.short_commit}_actions_{ctx.github_run_id}_{ctx.rp2040_port}.json"


def bridge_restore_out(ctx: RunContext) -> Path:
    return ctx.root / "artifacts" / "rp2040-flash" / f"rp2040-bridge-restore-copy-{ctx.short_commit}-{ctx.rp2040_port}.json"


def rp2040_access_precheck_out(ctx: RunContext) -> Path:
    return ctx.rp2040_hardware_dir / f"rp2040_access_precheck_{ctx.short_commit}_actions_{ctx.github_run_id}_{ctx.rp2040_port}.json"


def require_path(path: Path, label: str) -> Path:
    if not path.exists():
        raise FileNotFoundError(f"missing {label}: {path}")
    return path


def verify_named_artifact(artifact_dir: Path, filename: str, expected_sha256: str | None = None) -> dict:
    artifact_dir = artifact_dir.resolve()
    target = require_path(artifact_dir / filename, filename)
    sums = parse_sha256sums(require_path(artifact_dir / "SHA256SUMS.txt", "SHA256SUMS.txt"))
    expected = sums.get(filename) or sums.get(f"./{filename}")
    if not expected:
        raise FlashGuardError(f"SHA256SUMS.txt does not list {filename}")
    actual = sha256_file(target)
    if actual.lower() != expected.lower():
        raise FlashGuardError(f"checksum mismatch for {filename}: expected {expected}, actual {actual}")
    if expected_sha256 and actual.lower() != expected_sha256.lower():
        raise FlashGuardError(
            f"checksum mismatch for expected sha: expected {expected_sha256}, actual {actual}"
        )
    return {
        "path": str(target),
        "size": target.stat().st_size,
        "sha256": actual.upper(),
        "manifest": str(artifact_dir / "SHA256SUMS.txt"),
    }


def command_report(name: str, args: list[str], cwd: Path, timeout: int | None = None) -> dict:
    started_at = utc_now()
    try:
        result = subprocess.run(
            args,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
        )
    except Exception as exc:  # pragma: no cover - OS/tooling dependent
        return {
            "name": name,
            "ok": False,
            "args": args,
            "cwd": str(cwd),
            "started_at": started_at,
            "ended_at": utc_now(),
            "error": str(exc),
        }
    return {
        "name": name,
        "ok": result.returncode == 0,
        "returncode": result.returncode,
        "args": args,
        "cwd": str(cwd),
        "started_at": started_at,
        "ended_at": utc_now(),
        "stdout_tail": result.stdout[-4000:],
        "stderr_tail": result.stderr[-4000:],
    }


def esptool_flash_command(build_dir: Path, port: str, baud: int) -> list[str]:
    flasher = json.loads(require_path(build_dir / "flasher_args.json", "flasher_args.json").read_text())
    extra = flasher.get("extra_esptool_args", {})
    write_args = [str(item).replace("_", "-") if str(item).startswith("--") else str(item) for item in flasher.get("write_flash_args", [])]
    flash_files = flasher.get("flash_files", {})
    ordered_files = sorted(flash_files.items(), key=lambda item: int(str(item[0]), 0))
    file_args: list[str] = []
    for offset, rel_path in ordered_files:
        file_args.extend([str(offset), str((build_dir / str(rel_path)).resolve())])
    return [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        str(extra.get("chip", "esp32s3")),
        "-p",
        port,
        "-b",
        str(baud),
        "--before",
        str(extra.get("before", "default_reset")).replace("_", "-"),
        "--after",
        str(extra.get("after", "hard_reset")).replace("_", "-"),
        "write-flash",
        *write_args,
        *file_args,
    ]


def write_json(path: Path, payload: dict) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="ascii")
    return path


def flash_esp32(ctx: RunContext, *, dry_run: bool) -> dict:
    build_dir = artifact_dirs(ctx)["esp32_build"]
    cmd = esptool_flash_command(build_dir, ctx.d1l_port, ctx.esp32_flash_baud)
    out = ctx.hardware_dir / f"esp32_flash_{ctx.short_commit}_actions_{ctx.github_run_id}_{ctx.d1l_port}.json"
    report = {
        "schema": 1,
        "kind": "esp32_flash",
        "mode": "dry-run" if dry_run else "hardware",
        "port": ctx.d1l_port,
        "commit": ctx.commit,
        "github_actions_run": ctx.github_run_id,
        "public_rf_tx": False,
        "formats_sd": False,
        "command": cmd,
        "ok": True,
    }
    if not dry_run:
        report["result"] = command_report("esp32_flash", cmd, ctx.root, timeout=240)
        report["ok"] = report["result"].get("ok") is True
    write_json(out, report)
    report["path"] = str(out)
    return report


def wait_for_uf2_volume(timeout_sec: float, volume: str | None) -> tuple[Path, list[dict]]:
    deadline = time.monotonic() + timeout_sec
    last_candidates: list[dict] = []
    while True:
        try:
            selected, candidates = choose_volume(Path(volume) if volume else None)
            return selected, candidates
        except FlashGuardError:
            last_candidates = candidate_volumes()
            if time.monotonic() >= deadline:
                raise FlashGuardError(
                    f"RP2040 UF2 bootloader volume not found; candidates={last_candidates}"
                )
            time.sleep(0.5)


def uf2_volume_snapshot() -> dict[str, Any]:
    try:
        candidates = candidate_volumes()
    except Exception as exc:  # pragma: no cover - host/OS dependent
        return {"available": False, "candidates": [], "error": str(exc)}
    return {"available": bool(candidates), "candidates": candidates}


def rp2040_port_snapshot(port: str) -> dict[str, Any]:
    target = normalize_port(port)
    report: dict[str, Any] = {"port": target, "present": False, "matches": []}
    try:
        import serial.tools.list_ports
    except ImportError as exc:  # pragma: no cover - dependency dependent
        report["error"] = f"pyserial is required: {exc}"
        return report

    for item in serial.tools.list_ports.comports():
        device = str(getattr(item, "device", "") or "")
        if normalize_port(device) != target:
            continue
        match: dict[str, Any] = {"device": device}
        for attr in ["description", "hwid", "manufacturer", "product", "serial_number"]:
            value = getattr(item, attr, None)
            if value:
                match[attr] = str(value)
        for attr in ["vid", "pid"]:
            value = getattr(item, attr, None)
            if value is not None:
                match[attr] = f"{int(value):04X}"
        report["matches"].append(match)
    report["present"] = bool(report["matches"])
    return report


def serial_port_inventory() -> list[dict[str, Any]]:
    try:
        import serial.tools.list_ports
    except ImportError as exc:  # pragma: no cover - dependency dependent
        raise RuntimeError(f"pyserial is required: {exc}") from exc

    ports: list[dict[str, Any]] = []
    for item in serial.tools.list_ports.comports():
        record: dict[str, Any] = {"device": str(getattr(item, "device", "") or "")}
        for attr in ["description", "hwid", "manufacturer", "product", "serial_number"]:
            value = getattr(item, attr, None)
            if value:
                record[attr] = str(value)
        for attr in ["vid", "pid"]:
            value = getattr(item, attr, None)
            if value is not None:
                record[attr] = f"{int(value):04X}"
        ports.append(record)
    return ports


def rp2040_candidate_reasons(record: dict[str, Any]) -> list[str]:
    reasons: list[str] = []
    vid = str(record.get("vid", "")).upper()
    if vid in RP2040_USB_VIDS:
        reasons.append(f"vid:{vid}")
    text = " ".join(
        str(record.get(field, "") or "")
        for field in ["device", "description", "hwid", "manufacturer", "product"]
    ).lower()
    for keyword in RP2040_USB_KEYWORDS:
        if keyword in text:
            reasons.append(f"keyword:{keyword}")
    return sorted(set(reasons))


def rp2040_port_discovery(preferred_port: str, d1l_port: str) -> dict[str, Any]:
    preferred = rp2040_port_snapshot(preferred_port)
    report: dict[str, Any] = {
        "preferred_port": normalize_port(preferred_port),
        "d1l_port": normalize_port(d1l_port),
        "present": False,
        "selected_port": None,
        "preferred": preferred,
        "candidates": [],
        "skipped": [],
    }
    if preferred.get("present"):
        report["present"] = True
        report["selected_port"] = normalize_port(preferred_port)
        report["selected_reason"] = "configured_port_present"
        report["selected"] = preferred["matches"][0]
        return report

    try:
        inventory = serial_port_inventory()
    except Exception as exc:  # pragma: no cover - host/OS dependent
        report["error"] = str(exc)
        return report

    d1l = normalize_port(d1l_port)
    for record in inventory:
        device = normalize_port(str(record.get("device", "") or ""))
        if not device:
            continue
        if device == d1l:
            report["skipped"].append({"device": device, "reason": "d1l_console_port"})
            continue
        if device in FORBIDDEN_PORTS:
            report["skipped"].append({"device": device, "reason": "forbidden_port"})
            continue
        reasons = rp2040_candidate_reasons(record)
        if not reasons:
            continue
        candidate = dict(record)
        candidate["device"] = device
        candidate["match_reasons"] = reasons
        report["candidates"].append(candidate)

    if report["candidates"]:
        selected = report["candidates"][0]
        report["present"] = True
        report["selected_port"] = selected["device"]
        report["selected_reason"] = "rp2040_usb_descriptor"
        report["selected"] = selected
    return report


def d1l_console_ok(result: dict, *, require_protocol: bool = False) -> bool:
    if result.get("ok") is not True:
        return False
    if require_protocol and result.get("protocol_supported") is not True:
        return False
    return True


def enter_rp2040_bootloader_usb_touch(port: str) -> dict:
    started_at = utc_now()
    report = {
        "schema": 1,
        "kind": "rp2040_1200_baud_touch",
        "port": port,
        "baud": 1200,
        "started_at": started_at,
        "ended_at": None,
        "ok": True,
    }
    touch_code = (
        "import sys\n"
        "import serial\n"
        "ser = serial.Serial(port=sys.argv[1], baudrate=1200, timeout=0.2)\n"
        "ser.close()\n"
    )
    run_kwargs: dict[str, Any] = {}
    if os.name == "nt" and hasattr(subprocess, "CREATE_NO_WINDOW"):
        run_kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
    try:
        result = subprocess.run(
            [sys.executable, "-c", touch_code, port],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=BOOTLOADER_TOUCH_TIMEOUT_SECONDS,
            **run_kwargs,
        )
        report["returncode"] = result.returncode
        if result.stdout:
            report["stdout_tail"] = result.stdout[-1000:]
        if result.stderr:
            report["stderr_tail"] = result.stderr[-1000:]
    except subprocess.TimeoutExpired as exc:
        report["ok"] = True
        report["warning"] = f"1200-baud touch timed out after {BOOTLOADER_TOUCH_TIMEOUT_SECONDS}s"
        if exc.stderr:
            text = exc.stderr.decode("utf-8", errors="replace") if isinstance(exc.stderr, bytes) else str(exc.stderr)
            report["stderr_tail"] = text[-1000:]
    except Exception as exc:  # Windows often drops the CDC device mid-open.
        report["ok"] = True
        report["warning"] = str(exc)
    report["ended_at"] = utc_now()
    return report


def rp2040_double_reset_sweep(ctx: RunContext) -> tuple[list[dict], dict | None]:
    attempts: list[dict] = []
    for hold_ms, gap_ms, settle_ms in RP2040_DOUBLE_RESET_SWEEP_MS:
        command = f"rp2040 double-reset {hold_ms} {gap_ms} {settle_ms}"
        attempt: dict[str, Any] = {
            "command": command,
            "hold_ms": hold_ms,
            "gap_ms": gap_ms,
            "settle_ms": settle_ms,
        }
        timeout = max(8.0, settle_ms / 1000.0 + 5.0)
        attempt["result"] = send_d1l_console(ctx.d1l_port, ctx.baud, command, timeout)
        time.sleep(BOOTLOADER_ENTRY_RESCAN_SECONDS)
        attempt["uf2_volume"] = uf2_volume_snapshot()
        attempt["rp2040_port"] = rp2040_port_discovery(ctx.rp2040_port, ctx.d1l_port)
        attempts.append(attempt)
        if attempt["uf2_volume"].get("available") or attempt["rp2040_port"].get("present"):
            return attempts, attempt
    return attempts, None


def enter_rp2040_bootloader(ctx: RunContext, *, volume: str | None) -> dict:
    report: dict[str, Any] = {
        "schema": 1,
        "kind": "rp2040_bootloader_entry",
        "port": ctx.rp2040_port,
        "d1l_port": ctx.d1l_port,
        "public_rf_tx": False,
        "formats_sd": False,
        "manual_user_required": False,
        "started_at": utc_now(),
        "ok": False,
    }
    initial_volumes = uf2_volume_snapshot()
    report["uf2_volume_initial"] = initial_volumes
    if initial_volumes.get("available"):
        report["ok"] = True
        report["method"] = "uf2_already_available"
        report["ended_at"] = utc_now()
        return report

    ping = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 ping", 8.0, settle_sec=3.0)
    report["ping"] = ping
    if d1l_console_ok(ping, require_protocol=True):
        bridge = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 bootloader", 8.0)
        report["bridge_bootloader"] = bridge
        time.sleep(BOOTLOADER_ENTRY_RESCAN_SECONDS)
        report["uf2_volume_after_bridge_command"] = uf2_volume_snapshot()
        report["ok"] = bridge.get("ok") is True
        report["method"] = "rp2040_bridge_command"
        report["ended_at"] = utc_now()
        if not report["ok"]:
            report["error"] = "rp2040_bootloader_command_failed"
        return report

    port_initial = rp2040_port_discovery(ctx.rp2040_port, ctx.d1l_port)
    report["rp2040_port_initial"] = port_initial
    if port_initial.get("present"):
        selected_port = str(port_initial.get("selected_port") or ctx.rp2040_port)
        report["selected_rp2040_port"] = selected_port
        report["usb_touch"] = enter_rp2040_bootloader_usb_touch(selected_port)
        time.sleep(BOOTLOADER_ENTRY_RESCAN_SECONDS)
        report["uf2_volume_after_usb_touch"] = uf2_volume_snapshot()
        report["ok"] = True
        report["method"] = "rp2040_1200_baud_touch"
        report["ended_at"] = utc_now()
        return report

    sweep, success = rp2040_double_reset_sweep(ctx)
    report["double_reset_sweep"] = sweep
    if sweep:
        report["double_reset"] = sweep[0].get("result")
        report["uf2_volume_after_double_reset"] = sweep[-1].get("uf2_volume")
        report["rp2040_port_after_double_reset"] = sweep[-1].get("rp2040_port")
    if success and success["uf2_volume"].get("available"):
        report["ok"] = True
        report["method"] = "double_reset_sweep_revealed_uf2"
        report["successful_double_reset"] = success
        report["ended_at"] = utc_now()
        return report
    if success and success["rp2040_port"].get("present"):
        selected_port = str(success["rp2040_port"].get("selected_port") or ctx.rp2040_port)
        report["selected_rp2040_port"] = selected_port
        report["usb_touch_after_double_reset"] = enter_rp2040_bootloader_usb_touch(selected_port)
        time.sleep(BOOTLOADER_ENTRY_RESCAN_SECONDS)
        report["uf2_volume_after_double_reset_touch"] = uf2_volume_snapshot()
        report["ok"] = True
        report["method"] = "double_reset_sweep_then_rp2040_1200_baud_touch"
        report["successful_double_reset"] = success
        report["ended_at"] = utc_now()
        return report

    reset = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 reset", 8.0, settle_sec=3.0)
    report["reset"] = reset
    time.sleep(BOOTLOADER_ENTRY_RESCAN_SECONDS)
    port_after_reset = rp2040_port_discovery(ctx.rp2040_port, ctx.d1l_port)
    report["rp2040_port_after_reset"] = port_after_reset
    report["uf2_volume_after_reset"] = uf2_volume_snapshot()
    if report["uf2_volume_after_reset"].get("available"):
        report["ok"] = True
        report["method"] = "reset_revealed_uf2"
    elif port_after_reset.get("present"):
        selected_port = str(port_after_reset.get("selected_port") or ctx.rp2040_port)
        report["selected_rp2040_port"] = selected_port
        report["usb_touch_after_reset"] = enter_rp2040_bootloader_usb_touch(selected_port)
        time.sleep(BOOTLOADER_ENTRY_RESCAN_SECONDS)
        report["uf2_volume_after_reset_touch"] = uf2_volume_snapshot()
        report["ok"] = True
        report["method"] = "reset_then_rp2040_1200_baud_touch"
    else:
        report["ok"] = False
        report["method"] = "none"
        report["error"] = "rp2040_bootloader_unavailable"
    report["ended_at"] = utc_now()
    return report


def copy_named_uf2(
    *,
    artifact_dir: Path,
    filename: str,
    volume: str | None,
    timeout_sec: float,
) -> dict:
    artifact = verify_named_artifact(artifact_dir, filename)
    target_volume, candidates = wait_for_uf2_volume(timeout_sec, volume)
    destination = target_volume / filename
    shutil.copyfile(artifact["path"], destination)
    return {
        "ok": True,
        "artifact": artifact,
        "target_volume": metadata_for_volume(target_volume),
        "candidate_volumes": candidates,
        "destination": str(destination),
        "copied": True,
    }


def send_d1l_console(port: str, baud: int, command: str, timeout: float, *, settle_sec: float = 1.0) -> dict:
    try:
        import serial
    except ImportError as exc:  # pragma: no cover - dependency dependent
        return {"ok": False, "error": f"pyserial is required: {exc}", "cmd": command}
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(settle_sec)
        ser.reset_input_buffer()
        return send_console_command(ser, command, timeout)


def baud_probe_selected_deskos(result: dict) -> int | None:
    if result.get("ok") is not True or result.get("found_deskos") is not True:
        return None
    try:
        selected = int(result.get("selected_baud") or 0)
    except (TypeError, ValueError):
        return None
    return selected if selected > 0 else None


def rp2040_try_baud_probe(ctx: RunContext, report: dict[str, Any], label: str) -> bool:
    probe_key = f"baud_probe{label}"
    set_key = f"set_baud{label}"
    ping_key = f"ping_after_baud_probe{label}"
    report[probe_key] = send_d1l_console(
        ctx.d1l_port,
        ctx.baud,
        f"rp2040 baud-probe {RP2040_BAUD_PROBE_TIMEOUT_MS}",
        RP2040_BAUD_PROBE_COMMAND_TIMEOUT_SECONDS,
    )
    selected = baud_probe_selected_deskos(report[probe_key])
    if selected is None:
        return False
    report[set_key] = send_d1l_console(ctx.d1l_port, ctx.baud, f"rp2040 set-baud {selected}", 5.0)
    report[ping_key] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 ping", 8.0)
    if d1l_console_ok(report[ping_key], require_protocol=True):
        report["selected_rp2040_uart_baud"] = selected
        report["state"] = f"bridge_protocol_ready_after_baud_probe{label}"
        report["bootloader_entry"] = "rp2040 bootloader"
        return True
    return False


def rp2040_access_precheck(ctx: RunContext, *, dry_run: bool) -> dict:
    out = rp2040_access_precheck_out(ctx)
    report: dict[str, Any] = {
        "schema": 1,
        "kind": "rp2040_autonomous_access_precheck",
        "mode": "dry-run" if dry_run else "hardware",
        "d1l_port": ctx.d1l_port,
        "rp2040_port": ctx.rp2040_port,
        "commit": ctx.commit,
        "github_actions_run": ctx.github_run_id,
        "public_rf_tx": False,
        "formats_sd": False,
        "manual_user_required": False,
        "ok": True,
        "started_at": utc_now(),
    }

    def finish() -> dict:
        report["ended_at"] = utc_now()
        write_json(out, report)
        report["path"] = str(out)
        return report

    if dry_run:
        report["state"] = "dry_run"
        return finish()

    report["uf2_volume_initial"] = uf2_volume_snapshot()
    report["rp2040_port_initial"] = rp2040_port_discovery(ctx.rp2040_port, ctx.d1l_port)
    if report["uf2_volume_initial"].get("available"):
        report["state"] = "uf2_volume_available"
        return finish()
    if report["rp2040_port_initial"].get("present"):
        report["state"] = "usb_cdc_available_for_1200_baud_touch"
        report["selected_rp2040_port"] = report["rp2040_port_initial"].get("selected_port")
        return finish()

    report["status"] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 status", 8.0, settle_sec=3.0)
    report["ping"] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 ping", 8.0)
    if d1l_console_ok(report["ping"], require_protocol=True):
        report["state"] = "bridge_protocol_ready"
        report["bootloader_entry"] = "rp2040 bootloader"
        return finish()
    if rp2040_try_baud_probe(ctx, report, ""):
        return finish()

    sweep, success = rp2040_double_reset_sweep(ctx)
    report["double_reset_sweep"] = sweep
    if sweep:
        report["double_reset"] = sweep[0].get("result")
        report["uf2_volume_after_double_reset"] = sweep[-1].get("uf2_volume")
        report["rp2040_port_after_double_reset"] = sweep[-1].get("rp2040_port")
    if success and success["uf2_volume"].get("available"):
        report["state"] = "uf2_volume_available_after_double_reset"
        report["successful_double_reset"] = success
        return finish()
    if success and success["rp2040_port"].get("present"):
        report["state"] = "usb_cdc_available_after_double_reset"
        report["successful_double_reset"] = success
        report["selected_rp2040_port"] = success["rp2040_port"].get("selected_port")
        return finish()

    report["reset"] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 reset", 8.0)
    time.sleep(BOOTLOADER_ENTRY_RESCAN_SECONDS)
    report["uf2_volume_after_reset"] = uf2_volume_snapshot()
    report["rp2040_port_after_reset"] = rp2040_port_discovery(ctx.rp2040_port, ctx.d1l_port)
    report["ping_after_reset"] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 ping", 8.0, settle_sec=3.0)
    if report["uf2_volume_after_reset"].get("available"):
        report["state"] = "uf2_volume_available_after_reset"
    elif report["rp2040_port_after_reset"].get("present"):
        report["state"] = "usb_cdc_available_after_reset"
        report["selected_rp2040_port"] = report["rp2040_port_after_reset"].get("selected_port")
    elif d1l_console_ok(report["ping_after_reset"], require_protocol=True):
        report["state"] = "bridge_protocol_ready_after_reset"
        report["bootloader_entry"] = "rp2040 bootloader"
    elif rp2040_try_baud_probe(ctx, report, "_after_reset"):
        pass
    else:
        report["ok"] = False
        report["state"] = "no_autonomous_bootloader_path"
        report["error"] = "rp2040_bootloader_unavailable"
        report["next_action"] = "requires_firmware_bootloader_command_or_hardware_bootsel_control"
    return finish()


def capture_official_smoke(port: str, baud: int, timeout: float) -> dict:
    started_at = utc_now()
    report: dict[str, Any] = {
        "ok": False,
        "port": port,
        "baud": baud,
        "started_at": started_at,
        "ended_at": None,
        "lines": [],
        "parsed": [],
    }
    try:
        import serial

        with serial.Serial(port=port, baudrate=baud, timeout=0.25) as ser:
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                report["lines"].append(line)
                if line.startswith("{"):
                    try:
                        parsed = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    report["parsed"].append(parsed)
                    if parsed.get("test") == "seeed_official_sd_smoke":
                        report["result"] = parsed
                        report["ok"] = parsed.get("ok") is True
                        break
    except Exception as exc:  # pragma: no cover - serial dependent
        report["error"] = str(exc)
    report["ended_at"] = utc_now()
    return report


def run_official_sd_smoke(ctx: RunContext, *, volume: str | None, uf2_timeout: float, smoke_timeout: float, dry_run: bool) -> dict:
    out = official_sd_smoke_out(ctx)
    report: dict[str, Any] = {
        "schema": 1,
        "kind": "seeed_official_sd_smoke_capture",
        "mode": "dry-run" if dry_run else "hardware",
        "port": ctx.rp2040_port,
        "baud": ctx.baud,
        "commit": ctx.commit,
        "github_actions_run": ctx.github_run_id,
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
        "artifact_dir": str(artifact_dirs(ctx)["official_smoke"]),
    }
    if dry_run:
        report["planned_uf2"] = OFFICIAL_SMOKE_UF2
    else:
        report["bootloader_entry"] = enter_rp2040_bootloader(ctx, volume=volume)
        if report["bootloader_entry"].get("ok") is not True:
            report["ok"] = False
            report["error"] = report["bootloader_entry"].get("error", "rp2040_bootloader_unavailable")
            write_json(out, report)
            report["path"] = str(out)
            return report
        report["copy"] = copy_named_uf2(
            artifact_dir=artifact_dirs(ctx)["official_smoke"],
            filename=OFFICIAL_SMOKE_UF2,
            volume=volume,
            timeout_sec=uf2_timeout,
        )
        time.sleep(2.0)
        report["reset"] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 reset", 8.0)
        capture_discovery = rp2040_port_discovery(
            str(report["bootloader_entry"].get("selected_rp2040_port") or ctx.rp2040_port),
            ctx.d1l_port,
        )
        report["rp2040_port_for_smoke_capture"] = capture_discovery
        capture_port = str(capture_discovery.get("selected_port") or ctx.rp2040_port)
        report["capture_port"] = capture_port
        capture = capture_official_smoke(capture_port, ctx.baud, smoke_timeout)
        if not capture.get("result"):
            report["reset_retry"] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 reset", 8.0)
            capture_discovery = rp2040_port_discovery(capture_port, ctx.d1l_port)
            report["rp2040_port_for_smoke_capture_retry"] = capture_discovery
            capture_port = str(capture_discovery.get("selected_port") or capture_port)
            report["capture_port_retry"] = capture_port
            capture = capture_official_smoke(capture_port, ctx.baud, smoke_timeout)
        report.update(capture)
    write_json(out, report)
    report["path"] = str(out)
    return report


def restore_bridge(ctx: RunContext, *, volume: str | None, uf2_timeout: float, dry_run: bool) -> dict:
    out = bridge_restore_out(ctx)
    report: dict[str, Any] = {
        "schema": 1,
        "kind": "rp2040_bridge_restore",
        "mode": "dry-run" if dry_run else "hardware",
        "port": ctx.rp2040_port,
        "commit": ctx.commit,
        "github_actions_run": ctx.github_run_id,
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
        "artifact_dir": str(artifact_dirs(ctx)["bridge"]),
    }
    if dry_run:
        report["planned_uf2"] = BRIDGE_UF2
    else:
        report["bootloader_entry"] = enter_rp2040_bootloader(ctx, volume=volume)
        if report["bootloader_entry"].get("ok") is not True:
            report["ok"] = False
            report["error"] = report["bootloader_entry"].get("error", "rp2040_bootloader_unavailable")
            write_json(out, report)
            report["path"] = str(out)
            return report
        report["copy"] = copy_named_uf2(
            artifact_dir=artifact_dirs(ctx)["bridge"],
            filename=BRIDGE_UF2,
            volume=volume,
            timeout_sec=uf2_timeout,
        )
        time.sleep(2.0)
        report["reset"] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 reset", 8.0)
        time.sleep(2.0)
        report["ping"] = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 ping", 8.0)
        report["ok"] = (
            report["copy"].get("ok") is True
            and report["ping"].get("ok") is True
            and report["ping"].get("protocol_supported") is True
        )
        if not report["ok"]:
            report["error"] = "bridge_restore_not_verified"
    write_json(out, report)
    report["path"] = str(out)
    return report


def bridge_sha(ctx: RunContext) -> str:
    return verify_named_artifact(artifact_dirs(ctx)["bridge"], BRIDGE_UF2)["sha256"]


def run_existing_script(ctx: RunContext, name: str, args: list[str], out: Path, timeout: int | None, dry_run: bool) -> dict:
    command = [sys.executable, *args, "--out", str(out)]
    if dry_run and "--dry-run" not in command:
        command.append("--dry-run")
    report = {
        "schema": 1,
        "kind": name,
        "mode": "dry-run" if dry_run else "hardware",
        "commit": ctx.commit,
        "github_actions_run": ctx.github_run_id,
        "public_rf_tx": False,
        "formats_sd": False,
        "command": command,
        "path": str(out),
        "ok": True,
    }
    if not dry_run:
        report["result"] = command_report(name, command, ctx.root, timeout=timeout)
        report["ok"] = report["result"].get("ok") is True
    return report


def run_preflight(ctx: RunContext, dry_run: bool) -> dict:
    out = ctx.hardware_dir / f"rp2040_preflight_{ctx.short_commit}_actions_{ctx.github_run_id}_{ctx.d1l_port}.json"
    args = [
        str(ctx.root / "scripts" / "rp2040_sd_bridge_preflight_d1l.py"),
        "--port",
        ctx.d1l_port,
        "--timeout",
        "8",
        "--artifact-dir",
        str(artifact_dirs(ctx)["bridge"]),
        "--expected-sha256",
        bridge_sha(ctx),
    ]
    return run_existing_script(ctx, "rp2040_preflight", args, out, timeout=90, dry_run=dry_run)


def run_smoke(ctx: RunContext, dry_run: bool) -> dict:
    out = ctx.hardware_dir / f"smoke_{ctx.short_commit}_actions_{ctx.github_run_id}_{ctx.d1l_port}.json"
    args = [
        str(ctx.root / "scripts" / "smoke_d1l.py"),
        "--port",
        ctx.d1l_port,
        "--timeout",
        "5",
        "--persistence-test",
    ]
    return run_existing_script(ctx, "smoke", args, out, timeout=180, dry_run=dry_run)


def run_tab_abuse(ctx: RunContext, cycles: int, dry_run: bool) -> dict:
    out = ctx.hardware_dir / f"ui_tab_abuse_{ctx.short_commit}_actions_{ctx.github_run_id}_{ctx.d1l_port}.json"
    args = [
        str(ctx.root / "scripts" / "ui_tab_abuse_d1l.py"),
        "--port",
        ctx.d1l_port,
        "--timeout",
        "5",
        "--cycles",
        str(cycles),
        "--clear-crashlog-before-start",
    ]
    return run_existing_script(ctx, "ui_tab_abuse", args, out, timeout=max(600, cycles * 2), dry_run=dry_run)


def run_scroll_probe(ctx: RunContext, dry_run: bool) -> dict:
    out = ctx.hardware_dir / f"scroll_probe_{ctx.short_commit}_actions_{ctx.github_run_id}_{ctx.d1l_port}.json"
    args = [
        str(ctx.root / "scripts" / "scroll_probe_d1l.py"),
        "--port",
        ctx.d1l_port,
        "--timeout",
        "5",
        "--screens",
        DEFAULT_SCROLL_SCREENS,
        "--clear-crashlog-before-start",
    ]
    return run_existing_script(ctx, "scroll_probe", args, out, timeout=180, dry_run=dry_run)


def release_gate_command(ctx: RunContext, out: Path) -> list[str]:
    return [
        sys.executable,
        str(ctx.root / "scripts" / "release_gate_audit_d1l.py"),
        "--root",
        str(ctx.root),
        "--github-run-id",
        ctx.github_run_id,
        "--github-run-dir",
        str(ctx.github_run_dir),
        "--commit",
        ctx.commit,
        "--d1l-port",
        ctx.d1l_port,
        "--rp2040-port",
        ctx.rp2040_port,
        "--meshbot-port",
        "COM_DISABLED",
        "--hardware-dir",
        str(ctx.hardware_dir),
        "--rp2040-hardware-dir",
        str(ctx.rp2040_hardware_dir),
        "--out",
        str(out),
    ]


def run_release_gate(ctx: RunContext, dry_run: bool) -> dict:
    out = ctx.root / "artifacts" / "release-gate" / f"release-gate-audit-{ctx.short_commit}-actions{ctx.github_run_id}-autonomous-hw.json"
    cmd = release_gate_command(ctx, out)
    report = {
        "schema": 1,
        "kind": "release_gate_audit",
        "mode": "dry-run" if dry_run else "hardware",
        "command": cmd,
        "path": str(out),
        "ok": True,
    }
    if not dry_run:
        report["result"] = command_report("release_gate", cmd, ctx.root, timeout=60)
        report["ok"] = report["result"].get("ok") is True and out.exists()
        if out.exists():
            try:
                gate = json.loads(out.read_text(encoding="utf-8"))
                report["ready_for_public_release"] = gate.get("ready_for_public_release")
                report["failed_count"] = gate.get("failed_count")
                report["p0_failed_count"] = gate.get("p0_failed_count")
            except json.JSONDecodeError:
                report["ok"] = False
    return report


def exception_step(ctx: RunContext, kind: str, exc: Exception, *, out: Path | None = None, port: str | None = None) -> dict:
    report = {
        "schema": 1,
        "kind": kind,
        "mode": "hardware",
        "commit": ctx.commit,
        "github_actions_run": ctx.github_run_id,
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": False,
        "error": str(exc),
    }
    if port is not None:
        report["port"] = port
    if out is not None:
        report["path"] = str(out)
        write_json(out, report)
    return report


def download_artifacts(ctx: RunContext, dry_run: bool) -> dict:
    cmd = ["gh", "run", "download", ctx.github_run_id, "--dir", str(ctx.github_run_dir)]
    report = {
        "schema": 1,
        "kind": "github_artifact_download",
        "mode": "dry-run" if dry_run else "download",
        "command": cmd,
        "path": str(ctx.github_run_dir),
        "ok": True,
    }
    if not dry_run:
        ctx.github_run_dir.mkdir(parents=True, exist_ok=True)
        report["result"] = command_report("gh_run_download", cmd, ctx.root, timeout=300)
        report["ok"] = report["result"].get("ok") is True
    return report


def verify_inputs(ctx: RunContext, *, allow_download: bool, dry_run: bool) -> dict:
    dirs = artifact_dirs(ctx)
    missing = [str(path) for path in dirs.values() if not path.exists()]
    report = {
        "schema": 1,
        "kind": "input_artifact_check",
        "mode": "dry-run" if dry_run else "check",
        "github_run_dir": str(ctx.github_run_dir),
        "missing": missing,
        "ok": not missing,
    }
    if missing and allow_download:
        report["download"] = download_artifacts(ctx, dry_run=dry_run)
        report["ok"] = dry_run or report["download"].get("ok") is True
        if not dry_run:
            missing = [str(path) for path in dirs.values() if not path.exists()]
            report["missing_after_download"] = missing
            report["ok"] = report["ok"] and not missing
    return report


def plan_report(ctx: RunContext, args: argparse.Namespace) -> dict:
    return {
        "schema": 1,
        "kind": "d1l_autonomous_hardware_validation",
        "mode": "dry-run" if args.dry_run else "hardware",
        "commit": ctx.commit,
        "short_commit": ctx.short_commit,
        "github_actions_run": ctx.github_run_id,
        "github_run_dir": str(ctx.github_run_dir),
        "ports": {"d1l": ctx.d1l_port, "rp2040": ctx.rp2040_port, "forbidden": sorted(FORBIDDEN_PORTS)},
        "public_rf_tx": False,
        "formats_sd": False,
        "manual_user_required": False,
        "steps": [
            "verify_input_artifacts",
            *(["flash_esp32"] if not args.skip_esp32_flash else []),
            "rp2040_autonomous_access_precheck",
            *(["flash_official_rp2040_sd_smoke", "capture_official_rp2040_sd_smoke"] if not args.skip_rp2040_official_smoke else []),
            "restore_rp2040_bridge",
            "rp2040_bridge_preflight",
            "d1l_smoke",
            *(["d1l_500_cycle_tab_abuse", "d1l_scroll_probe"] if args.include_ui_probes else []),
            "release_gate_audit",
        ],
    }


def release_gate_summary(runs: list[dict]) -> dict:
    gate = next((step for step in runs if step.get("kind") == "release_gate_audit"), {})
    return {
        "path": gate.get("path"),
        "ready_for_public_release": gate.get("ready_for_public_release"),
        "failed_count": gate.get("failed_count"),
        "p0_failed_count": gate.get("p0_failed_count"),
    }


def attach_release_gate_summary(report: dict, runs: list[dict]) -> dict:
    summary = release_gate_summary(runs)
    report["release_gate"] = summary
    report["ready_for_public_release"] = summary.get("ready_for_public_release")
    report["release_ready"] = summary.get("ready_for_public_release") is True
    return summary


def run_bridge_restore_with_retry(ctx: RunContext, args: argparse.Namespace, runs: list[dict]) -> dict:
    last_report: dict[str, Any] = {}
    for attempt in range(2):
        try:
            report = restore_bridge(
                ctx,
                volume=args.uf2_volume,
                uf2_timeout=args.uf2_timeout,
                dry_run=args.dry_run,
            )
        except Exception as exc:
            report = exception_step(
                ctx,
                "rp2040_bridge_restore",
                exc,
                out=bridge_restore_out(ctx),
                port=ctx.rp2040_port,
            )
        report["attempt"] = attempt + 1
        runs.append(report)
        last_report = report
        if report.get("ok") is True:
            break
    return last_report


def run_validation(args: argparse.Namespace) -> dict:
    ctx = build_context(args)
    ctx.hardware_dir.mkdir(parents=True, exist_ok=True)
    ctx.rp2040_hardware_dir.mkdir(parents=True, exist_ok=True)

    report = plan_report(ctx, args)
    report["created_at"] = utc_now()
    report["git"] = git_metadata(ctx.root)
    runs: list[dict] = []
    report["runs"] = runs

    try:
        inputs = verify_inputs(ctx, allow_download=args.download_artifacts, dry_run=args.dry_run)
        runs.append(inputs)
        if not inputs.get("ok") and not args.dry_run:
            report["ok"] = False
            report["error"] = "input artifact check failed"
            return report

        if not args.skip_esp32_flash:
            esp32_flash = flash_esp32(ctx, dry_run=args.dry_run)
            runs.append(esp32_flash)
            if esp32_flash.get("ok") is not True and not args.dry_run:
                runs.append(run_release_gate(ctx, dry_run=args.dry_run))
                report["error"] = "esp32_flash_failed"
                report["ok"] = False
                attach_release_gate_summary(report, runs)
                return report

        access_precheck = rp2040_access_precheck(ctx, dry_run=args.dry_run)
        runs.append(access_precheck)
        if access_precheck.get("ok") is not True and not args.dry_run:
            runs.append(run_release_gate(ctx, dry_run=args.dry_run))
            report["error"] = access_precheck.get("error", "rp2040_bootloader_unavailable")
            report["ok"] = False
            attach_release_gate_summary(report, runs)
            return report

        if not args.skip_rp2040_official_smoke:
            try:
                runs.append(
                    run_official_sd_smoke(
                        ctx,
                        volume=args.uf2_volume,
                        uf2_timeout=args.uf2_timeout,
                        smoke_timeout=args.smoke_capture_timeout,
                        dry_run=args.dry_run,
                    )
                )
            except Exception as exc:
                runs.append(
                    exception_step(
                        ctx,
                        "seeed_official_sd_smoke_capture",
                        exc,
                        out=official_sd_smoke_out(ctx),
                        port=ctx.rp2040_port,
                    )
                )
            bridge_restore = run_bridge_restore_with_retry(ctx, args, runs)
        else:
            bridge_restore = run_bridge_restore_with_retry(ctx, args, runs)

        if bridge_restore.get("ok") is not True and not args.dry_run:
            runs.append(run_release_gate(ctx, dry_run=args.dry_run))
            report["error"] = "bridge_restore_not_verified"
            report["ok"] = False
            attach_release_gate_summary(report, runs)
            return report

        runs.append(run_preflight(ctx, dry_run=args.dry_run))
        runs.append(run_smoke(ctx, dry_run=args.dry_run))
        if args.include_ui_probes:
            runs.append(run_tab_abuse(ctx, cycles=args.tab_cycles, dry_run=args.dry_run))
            runs.append(run_scroll_probe(ctx, dry_run=args.dry_run))
        runs.append(run_release_gate(ctx, dry_run=args.dry_run))
    except Exception as exc:
        report["error"] = str(exc)
        report["ok"] = False
    else:
        report["ok"] = all(step.get("ok") is True for step in runs if step.get("kind") != "release_gate_audit")
        attach_release_gate_summary(report, runs)
    return report


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--github-run-id")
    parser.add_argument("--github-run-dir")
    parser.add_argument("--github-run-dir-name", help=argparse.SUPPRESS)
    parser.add_argument("--download-artifacts", action="store_true")
    parser.add_argument("--commit")
    parser.add_argument("--d1l-port", default=os.environ.get("D1L_PORT", DEFAULT_D1L_PORT))
    parser.add_argument("--rp2040-port", default=os.environ.get("D1L_RP2040_PORT", DEFAULT_RP2040_PORT))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--esp32-flash-baud", type=int, default=460800)
    parser.add_argument("--skip-esp32-flash", action="store_true")
    parser.add_argument("--skip-rp2040-official-smoke", action="store_true")
    parser.add_argument("--include-ui-probes", action="store_true")
    parser.add_argument("--tab-cycles", type=int, default=500)
    parser.add_argument("--uf2-volume")
    parser.add_argument("--uf2-timeout", type=float, default=30.0)
    parser.add_argument("--smoke-capture-timeout", type=float, default=30.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args(argv)
    if args.tab_cycles <= 0:
        parser.error("--tab-cycles must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    try:
        report = run_validation(args)
    except ValueError as exc:
        print(json.dumps({"schema": 1, "ok": False, "error": str(exc)}))
        return 2

    ctx = build_context(args)
    out = Path(args.out) if args.out else ctx.root / "artifacts" / "hardware" / f"d1l-autonomous-hardware-validation-{ctx.short_commit}-actions{ctx.github_run_id}.json"
    if not out.is_absolute():
        out = ctx.root / out
    write_json(out, report)
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
