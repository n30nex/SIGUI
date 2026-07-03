#!/usr/bin/env python3
"""Guided D1L RP2040 SD install and proof runner.

This is the intentionally small manual fallback for boards where the ESP32 can
reset the RP2040 but cannot assert BOOTSEL. The operator performs only the
RP2040 BOOTSEL/UF2 step; checksum validation, UF2 copying, smoke capture, bridge
restore, and post-restore proof stay scripted and recorded.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from flash_rp2040_sd_bridge_uf2 import (
        FlashGuardError,
        candidate_volumes,
        choose_volume,
        copy_uf2,
        metadata_for_volume,
        verify_artifact,
    )
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.flash_rp2040_sd_bridge_uf2 import (
        FlashGuardError,
        candidate_volumes,
        choose_volume,
        copy_uf2,
        metadata_for_volume,
        verify_artifact,
    )
    from scripts.smoke_d1l import send_console_command


DEFAULT_D1L_PORT = "COM" + "12"
DEFAULT_RP2040_PORT = "COM" + "16"
FORBIDDEN_PORTS = {"COM" + "11", "COM" + "29"}
OFFICIAL_SMOKE_UF2 = "seeed_official_sd_smoke.ino.uf2"
BRIDGE_UF2 = "deskos_sd_bridge.ino.uf2"


@dataclass(frozen=True)
class GuidedContext:
    root: Path
    commit: str
    short_commit: str
    github_run_id: str
    github_run_dir: Path
    d1l_port: str
    rp2040_port: str
    baud: int
    out: Path


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
        path = Path(explicit_dir)
        return path.resolve() if path.is_absolute() else (root / path).resolve()
    if not run_id:
        raise ValueError("Pass --github-run-id or --github-run-dir")
    current = root / "artifacts" / "github" / f"{run_id}-current"
    if current.exists():
        return current.resolve()
    return (root / "artifacts" / "github" / run_id).resolve()


def default_out(ctx: GuidedContext) -> Path:
    return (
        ctx.root
        / "artifacts"
        / "hardware"
        / ctx.d1l_port.lower()
        / f"d1l-guided-sd-install-{ctx.short_commit}-actions{ctx.github_run_id}.json"
    )


def build_context(args: argparse.Namespace) -> GuidedContext:
    root = Path(args.root).resolve()
    d1l_port = normalize_port(args.d1l_port)
    rp2040_port = normalize_port(args.rp2040_port)
    enforce_port_guard(d1l_port, rp2040_port)
    commit = resolve_commit(root, args.commit)
    run_dir = resolve_github_run_dir(root, args.github_run_id, args.github_run_dir)
    run_id = args.github_run_id or run_dir.name.removesuffix("-current")
    ctx = GuidedContext(
        root=root,
        commit=commit,
        short_commit=commit[:7],
        github_run_id=run_id,
        github_run_dir=run_dir,
        d1l_port=d1l_port,
        rp2040_port=rp2040_port,
        baud=args.baud,
        out=Path("__placeholder__"),
    )
    out = Path(args.out) if args.out else default_out(ctx)
    if not out.is_absolute():
        out = root / out
    return GuidedContext(
        root=ctx.root,
        commit=ctx.commit,
        short_commit=ctx.short_commit,
        github_run_id=ctx.github_run_id,
        github_run_dir=ctx.github_run_dir,
        d1l_port=ctx.d1l_port,
        rp2040_port=ctx.rp2040_port,
        baud=ctx.baud,
        out=out,
    )


def artifact_dirs(ctx: GuidedContext) -> dict[str, Path]:
    return {
        "official_smoke": ctx.github_run_dir / "rp2040-seeed-official-sd-smoke-firmware",
        "bridge": ctx.github_run_dir / "rp2040-sd-bridge-firmware",
    }


def verify_inputs(ctx: GuidedContext) -> dict[str, Any]:
    dirs = artifact_dirs(ctx)
    return {
        "official_smoke": verify_artifact(dirs["official_smoke"], uf2_name=OFFICIAL_SMOKE_UF2),
        "bridge": verify_artifact(dirs["bridge"], uf2_name=BRIDGE_UF2),
    }


def wait_for_uf2_volume(
    *,
    timeout_sec: float,
    volume: str | None,
    extra_roots: list[Path] | None = None,
) -> tuple[Path, list[dict[str, Any]]]:
    deadline = time.monotonic() + timeout_sec
    last_candidates: list[dict[str, Any]] = []
    while True:
        try:
            selected, candidates = choose_volume(Path(volume) if volume else None, extra_roots)
            return selected, candidates
        except FlashGuardError:
            last_candidates = candidate_volumes(extra_roots)
            if time.monotonic() >= deadline:
                raise FlashGuardError(
                    f"RP2040 UF2 bootloader volume not found; candidates={last_candidates}"
                )
            time.sleep(0.5)


def instruction_lines(stage: str, uf2_name: str) -> list[str]:
    title = "official SD smoke" if stage == "official_smoke" else "production SD bridge"
    return [
        f"Put the D1L RP2040 into UF2/BOOTSEL mode for {title}.",
        "Use the RP2040 internal BOOTSEL button path, not ESP32 flashing.",
        "Wait for Windows to mount the RP2040 UF2 disk with INFO_UF2.TXT or INDEX.HTM.",
        f"The script will copy only {uf2_name}.",
        "Do not use COM11 or COM29. Do not format the SD card on-device.",
    ]


def prompt_for_bootsel(stage: str, uf2_name: str, *, no_prompt: bool) -> None:
    print()
    for line in instruction_lines(stage, uf2_name):
        print(line)
    print()
    if not no_prompt:
        input("Press Enter after the RP2040 UF2 disk is mounted...")


def copy_stage(
    *,
    ctx: GuidedContext,
    stage: str,
    artifact_dir: Path,
    uf2_name: str,
    volume: str | None,
    timeout_sec: float,
    dry_run: bool,
    no_prompt: bool,
    extra_roots: list[Path] | None = None,
) -> dict[str, Any]:
    report: dict[str, Any] = {
        "schema": 1,
        "kind": "guided_rp2040_uf2_copy",
        "stage": stage,
        "mode": "dry-run" if dry_run else "copy",
        "manual_user_required": True,
        "public_rf_tx": False,
        "formats_sd": False,
        "sd_touched": False,
        "artifact_dir": str(artifact_dir),
        "uf2_name": uf2_name,
        "started_at": utc_now(),
        "ok": True,
    }
    try:
        report["artifact"] = verify_artifact(artifact_dir, uf2_name=uf2_name)
        report["instructions"] = instruction_lines(stage, uf2_name)
        if dry_run:
            report["planned_copy"] = True
            return report
        prompt_for_bootsel(stage, uf2_name, no_prompt=no_prompt)
        target, candidates = wait_for_uf2_volume(
            timeout_sec=timeout_sec,
            volume=volume,
            extra_roots=extra_roots,
        )
        report["selected_volume"] = metadata_for_volume(target)
        report["candidate_volumes"] = candidates
        report["copy"] = copy_uf2(
            artifact_dir,
            target,
            do_copy=True,
            uf2_name=uf2_name,
            extra_roots=extra_roots,
        )
        report["ok"] = report["copy"].get("copied") is True
    except Exception as exc:
        report["ok"] = False
        report["error"] = str(exc)
    finally:
        report["ended_at"] = utc_now()
    return report


def serial_inventory() -> list[dict[str, Any]]:
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


def rp2040_port_candidates(d1l_port: str) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    d1l = normalize_port(d1l_port)
    for record in serial_inventory():
        device = normalize_port(str(record.get("device", "") or ""))
        if not device or device == d1l or device in FORBIDDEN_PORTS:
            continue
        text = " ".join(str(record.get(field, "") or "") for field in record).lower()
        vid = str(record.get("vid", "")).upper()
        if vid in {"2E8A", "239A", "CAFE"} or any(
            token in text for token in ("rp2040", "pico", "bootsel", "circuitpython", "adafruit")
        ):
            candidate = dict(record)
            candidate["device"] = device
            candidates.append(candidate)
    return candidates


def wait_for_rp2040_port(ctx: GuidedContext, timeout_sec: float) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_sec
    last_candidates: list[dict[str, Any]] = []
    while time.monotonic() < deadline:
        candidates = rp2040_port_candidates(ctx.d1l_port)
        last_candidates = candidates
        for candidate in candidates:
            if normalize_port(str(candidate.get("device"))) == ctx.rp2040_port:
                return {"present": True, "selected_port": ctx.rp2040_port, "candidates": candidates}
        if candidates:
            return {"present": True, "selected_port": candidates[0]["device"], "candidates": candidates}
        time.sleep(0.5)
    return {"present": False, "selected_port": None, "candidates": last_candidates}


def capture_official_smoke(port: str, baud: int, timeout_sec: float) -> dict[str, Any]:
    report: dict[str, Any] = {
        "schema": 1,
        "kind": "guided_seeed_official_sd_smoke_capture",
        "port": port,
        "baud": baud,
        "public_rf_tx": False,
        "formats_sd": False,
        "started_at": utc_now(),
        "lines": [],
        "parsed": [],
        "ok": False,
    }
    try:
        import serial

        with serial.Serial(port=port, baudrate=baud, timeout=0.25) as ser:
            deadline = time.monotonic() + timeout_sec
            while time.monotonic() < deadline:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                report["lines"].append(line)
                if not line.startswith("{"):
                    continue
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


def send_d1l_console(port: str, baud: int, command: str, timeout: float) -> dict[str, Any]:
    try:
        import serial
    except ImportError as exc:  # pragma: no cover - dependency dependent
        return {"schema": 1, "ok": False, "cmd": command, "error": f"pyserial is required: {exc}"}
    try:
        with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
            time.sleep(1.0)
            ser.reset_input_buffer()
            return send_console_command(ser, command, timeout)
    except Exception as exc:  # pragma: no cover - serial dependent
        return {"schema": 1, "ok": False, "cmd": command, "error": str(exc)}


def run_json_script(
    *,
    ctx: GuidedContext,
    label: str,
    args: list[str],
    out: Path,
    timeout_sec: int,
    dry_run: bool,
) -> dict[str, Any]:
    out.parent.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, *args, "--out", str(out)]
    report: dict[str, Any] = {
        "schema": 1,
        "kind": label,
        "mode": "dry-run" if dry_run else "hardware",
        "command": command,
        "path": str(out),
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
    }
    if dry_run:
        report["planned"] = True
        return report
    try:
        result = subprocess.run(
            command,
            cwd=ctx.root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout_sec,
        )
        report["returncode"] = result.returncode
        report["stdout_tail"] = result.stdout[-4000:]
        report["stderr_tail"] = result.stderr[-4000:]
        report["ok"] = result.returncode == 0
        if out.exists():
            report["artifact"] = json.loads(out.read_text(encoding="ascii"))
    except Exception as exc:
        report["ok"] = False
        report["error"] = str(exc)
    return report


def run_canaries(ctx: GuidedContext, *, dry_run: bool) -> list[dict[str, Any]]:
    token = f"guided_{ctx.short_commit}"
    specs = [
        (
            "sd_file_canary",
            [str(ctx.root / "scripts" / "sd_file_canary_d1l.py"), "--port", ctx.d1l_port],
            ctx.root / "artifacts" / "sd-canary" / f"d1l-sd-file-canary-guided-{ctx.short_commit}-{ctx.d1l_port}.json",
            60,
        ),
        (
            "sd_export_canary",
            [
                str(ctx.root / "scripts" / "sd_export_canary_d1l.py"),
                "--port",
                ctx.d1l_port,
                "--token",
                token,
            ],
            ctx.root / "artifacts" / "sd-export-canary" / f"d1l-sd-export-canary-guided-{ctx.short_commit}-{ctx.d1l_port}.json",
            60,
        ),
        (
            "sd_diagnostic_export",
            [
                str(ctx.root / "scripts" / "sd_diagnostic_export_d1l.py"),
                "--port",
                ctx.d1l_port,
                "--token",
                token,
            ],
            ctx.root / "artifacts" / "sd-diagnostic-export" / f"d1l-sd-diagnostic-export-guided-{ctx.short_commit}-{ctx.d1l_port}.json",
            60,
        ),
    ]
    return [
        run_json_script(ctx=ctx, label=label, args=args, out=out, timeout_sec=timeout, dry_run=dry_run)
        for label, args, out, timeout in specs
    ]


def write_report(report: dict[str, Any], out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")


def run_guided_install(ctx: GuidedContext, args: argparse.Namespace) -> dict[str, Any]:
    dirs = artifact_dirs(ctx)
    report: dict[str, Any] = {
        "schema": 1,
        "kind": "d1l_guided_sd_install",
        "mode": "dry-run" if args.dry_run else "guided-hardware",
        "manual_user_required": True,
        "manual_steps": [
            "put_rp2040_in_bootsel_for_official_sd_smoke",
            "put_rp2040_in_bootsel_for_bridge_restore",
        ],
        "commit": ctx.commit,
        "short_commit": ctx.short_commit,
        "github_actions_run": ctx.github_run_id,
        "github_run_dir": str(ctx.github_run_dir),
        "ports": {"d1l": ctx.d1l_port, "rp2040": ctx.rp2040_port, "forbidden": sorted(FORBIDDEN_PORTS)},
        "public_rf_tx": False,
        "formats_sd": False,
        "created_at": utc_now(),
        "ok": True,
        "runs": [],
    }
    try:
        report["artifacts"] = verify_inputs(ctx)
    except Exception as exc:
        report["ok"] = False
        report["error"] = str(exc)
        return report

    official_copy = copy_stage(
        ctx=ctx,
        stage="official_smoke",
        artifact_dir=dirs["official_smoke"],
        uf2_name=OFFICIAL_SMOKE_UF2,
        volume=args.uf2_volume,
        timeout_sec=args.uf2_timeout,
        dry_run=args.dry_run,
        no_prompt=args.no_prompt,
    )
    report["runs"].append(official_copy)
    if official_copy.get("ok") is not True:
        report["ok"] = False
        report["error"] = "official_smoke_uf2_copy_failed"
        return report

    if args.dry_run:
        capture = {
            "schema": 1,
            "kind": "guided_seeed_official_sd_smoke_capture",
            "mode": "dry-run",
            "planned": True,
            "port": ctx.rp2040_port,
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": True,
        }
    else:
        time.sleep(args.post_copy_delay)
        port_discovery = wait_for_rp2040_port(ctx, args.rp2040_port_timeout)
        capture_port = str(port_discovery.get("selected_port") or ctx.rp2040_port)
        capture = capture_official_smoke(capture_port, ctx.baud, args.smoke_capture_timeout)
        capture["rp2040_port_discovery"] = port_discovery
    report["runs"].append(capture)

    bridge_copy = copy_stage(
        ctx=ctx,
        stage="bridge_restore",
        artifact_dir=dirs["bridge"],
        uf2_name=BRIDGE_UF2,
        volume=args.uf2_volume,
        timeout_sec=args.uf2_timeout,
        dry_run=args.dry_run,
        no_prompt=args.no_prompt,
    )
    report["runs"].append(bridge_copy)
    if bridge_copy.get("ok") is not True:
        report["ok"] = False
        report["error"] = "bridge_restore_uf2_copy_failed"
        return report

    if args.dry_run:
        bridge_ping = {"schema": 1, "cmd": "rp2040 ping", "mode": "dry-run", "planned": True, "ok": True}
    else:
        time.sleep(args.post_copy_delay)
        reset = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 reset", 8.0)
        bridge_ping = send_d1l_console(ctx.d1l_port, ctx.baud, "rp2040 ping", 8.0)
        bridge_ping = {"schema": 1, "kind": "bridge_ping_after_restore", "reset": reset, "ping": bridge_ping, "ok": bridge_ping.get("ok") is True}
    report["runs"].append(bridge_ping)

    preflight_out = (
        ctx.root
        / "artifacts"
        / "rp2040-preflight"
        / f"d1l-rp2040-sd-bridge-guided-postflash-{ctx.short_commit}-actions{ctx.github_run_id}-{ctx.d1l_port}.json"
    )
    preflight = run_json_script(
        ctx=ctx,
        label="rp2040_sd_bridge_preflight",
        args=[
            str(ctx.root / "scripts" / "rp2040_sd_bridge_preflight_d1l.py"),
            "--port",
            ctx.d1l_port,
            "--artifact-dir",
            str(dirs["bridge"]),
        ],
        out=preflight_out,
        timeout_sec=120,
        dry_run=args.dry_run,
    )
    report["runs"].append(preflight)

    preflight_artifact = preflight.get("artifact") if isinstance(preflight.get("artifact"), dict) else {}
    ready_for_canaries = preflight_artifact.get("ready_for_sd_acceptance") is True
    report["ready_for_sd_acceptance"] = ready_for_canaries
    if not args.skip_canaries and (args.dry_run or ready_for_canaries):
        report["runs"].extend(run_canaries(ctx, dry_run=args.dry_run))
    elif not args.skip_canaries:
        report["canaries_skipped"] = "preflight_not_ready_for_sd_acceptance"

    report["official_smoke_ok"] = capture.get("ok") is True
    report["bridge_ping_ok"] = bridge_ping.get("ok") is True
    report["ok"] = (
        report["official_smoke_ok"]
        and report["bridge_ping_ok"]
        and preflight.get("ok") is True
        and (args.skip_canaries or args.dry_run or all(run.get("ok") is True for run in report["runs"] if str(run.get("kind", "")).startswith("sd_")))
    )
    if not report["ok"]:
        report["error"] = "guided_sd_install_not_fully_verified"
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--github-run-id")
    parser.add_argument("--github-run-dir")
    parser.add_argument("--commit")
    parser.add_argument("--d1l-port", default=os.environ.get("D1L_PORT", DEFAULT_D1L_PORT))
    parser.add_argument("--rp2040-port", default=os.environ.get("D1L_RP2040_PORT", DEFAULT_RP2040_PORT))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--uf2-volume", help="Explicit RP2040 UF2 volume, e.g. E:")
    parser.add_argument("--uf2-timeout", type=float, default=180.0)
    parser.add_argument("--rp2040-port-timeout", type=float, default=45.0)
    parser.add_argument("--smoke-capture-timeout", type=float, default=60.0)
    parser.add_argument("--post-copy-delay", type=float, default=2.0)
    parser.add_argument("--skip-canaries", action="store_true")
    parser.add_argument("--no-prompt", action="store_true", help="Do not pause for Enter; just wait for UF2 volumes")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    try:
        ctx = build_context(args)
        report = run_guided_install(ctx, args)
        write_report(report, ctx.out)
        report["path"] = str(ctx.out)
        print(json.dumps(report))
        return 0 if report.get("ok") else 1
    except Exception as exc:
        fallback = {
            "schema": 1,
            "kind": "d1l_guided_sd_install",
            "ok": False,
            "error": str(exc),
            "manual_user_required": True,
            "public_rf_tx": False,
            "formats_sd": False,
        }
        print(json.dumps(fallback))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
