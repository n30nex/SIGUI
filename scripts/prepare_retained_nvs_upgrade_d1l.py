#!/usr/bin/env python3
"""Prepare the D1L retained-NVS region for the partition-layout upgrade.

Firmware deliberately refuses to classify or erase ambiguous nonblank bytes.
This installer-side tool therefore requires exact running-firmware provenance,
an exact partition-table artifact hash, a typed erase scope, and (for the one
known failed pre-anchor candidate) a hash-pinned incident evidence bundle plus
the exact current raw retained-region hash and device identity.

Only ``d1l_retained`` at 0x7E1000..0x7FFFFF can be erased. Default NVS, the
metadata sector, SD storage, and RF state are never modified by this tool.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from smoke_d1l import open_d1l_serial, send_console_command
except ModuleNotFoundError:  # Imported as ``scripts.prepare_...`` by pytest.
    from scripts.smoke_d1l import open_d1l_serial, send_console_command


PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_ARTIFACT_SIZE = 0xC00
PARTITION_TABLE_READ_SIZE = 0x1000
RETAINED_OFFSET = 0x7E1000
RETAINED_SIZE = 0x1F000
CONFIRM_SCOPE = "ERASE-d1l_retained-0x7E1000-0x1F000"
ESPTOOL_CHIP = "esp32s3"
FAILED_PRE_ANCHOR_COMMIT = "78beca8ea430202a534fb143f2f51dd158ed9e52"
FAILED_PRE_ANCHOR_TABLE_SHA256 = (
    "8fff27ab4abf09fccacfe7503cd190a5f7ec46c756df56b1051b89f9ffca17d1"
)
FAILED_PRE_ANCHOR_EVIDENCE_MANIFEST_SHA256 = (
    "e6216fb47225dd59ffb46b21c9c34ec73670d4f7d68f650a6a6feb1c18942e07"
)
PREDECESSOR_TABLE_SHA256 = (
    "420931e3f5af072d899b5357c043fd9f283bf165fc9df6113b6d43d82b48a06d"
)
SUPPORTED_PREDECESSOR_TABLE_SHA256 = {
    "727bd235174303b12c034d3b3c9e1a9f1a0c0e67": PREDECESSOR_TABLE_SHA256,
    "70c69535c1d655b19c2bc43ddb307141590fdad5": PREDECESSOR_TABLE_SHA256,
    "3a9651046bfce37cfc47acb04a84251cd93eedb8": PREDECESSOR_TABLE_SHA256,
}
SUPPORTED_PREDECESSOR_COMMITS = frozenset(SUPPORTED_PREDECESSOR_TABLE_SHA256)
PARTITION_MAGIC = 0x50AA
PARTITION_MD5_MAGIC = 0xEBEB
EXPECTED_FAILED_RETAINED_STATE = {
    "partition": "d1l_retained",
    "marker_ready": False,
    "initialized_this_boot": False,
    "ready": False,
    "init_error": "ESP_ERR_INVALID_STATE",
    "migrated_keys": 0,
    "migration_error": "ESP_ERR_INVALID_STATE",
}


@dataclass(frozen=True)
class PartitionEntry:
    name: str
    type: int
    subtype: int
    offset: int
    size: int
    flags: int


PREDECESSOR_ENTRIES = [
    PartitionEntry("nvs", 1, 2, 0x9000, 0x6000, 0),
    PartitionEntry("phy_init", 1, 1, 0xF000, 0x1000, 0),
    PartitionEntry("factory", 0, 0, 0x10000, 0x7F0000, 0),
]
DEDICATED_RETAINED_ENTRIES = [
    PartitionEntry("nvs", 1, 2, 0x9000, 0x6000, 0),
    PartitionEntry("phy_init", 1, 1, 0xF000, 0x1000, 0),
    PartitionEntry("factory", 0, 0, 0x10000, 0x7D0000, 0),
    PartitionEntry("d1l_ret_meta", 1, 0x40, 0x7E0000, 0x1000, 0),
    PartitionEntry("d1l_retained", 1, 2, RETAINED_OFFSET, RETAINED_SIZE, 0),
]


class UpgradePreparationError(RuntimeError):
    def __init__(self, report: dict[str, Any], cause: Exception):
        super().__init__(str(cause))
        self.report = report


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def exact_commit(value: object) -> str | None:
    text = str(value or "").strip().lower()
    if len(text) != 40 or any(char not in "0123456789abcdef" for char in text):
        return None
    return text


def normalize_sha256(value: object) -> str | None:
    text = str(value or "").strip().lower()
    if len(text) != 64 or any(char not in "0123456789abcdef" for char in text):
        return None
    return text


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"{label} is not readable JSON: {path}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{label} must contain one JSON object")
    return value


def parse_partition_table(data: bytes) -> list[PartitionEntry]:
    if len(data) < PARTITION_TABLE_ARTIFACT_SIZE:
        raise ValueError("partition table read is shorter than the 0xC00 artifact")

    entries: list[PartitionEntry] = []
    entry_struct = struct.Struct("<HBBII16sI")
    md5_seen = False
    for offset in range(0, PARTITION_TABLE_ARTIFACT_SIZE, entry_struct.size):
        chunk = data[offset : offset + entry_struct.size]
        magic = int.from_bytes(chunk[:2], "little")
        if magic == PARTITION_MAGIC:
            _, part_type, subtype, address, size, raw_name, flags = (
                entry_struct.unpack(chunk)
            )
            try:
                name = raw_name.split(b"\0", 1)[0].decode("ascii", errors="strict")
            except UnicodeDecodeError as exc:
                raise ValueError("partition name is not ASCII") from exc
            if not name or size == 0:
                raise ValueError("partition entries require nonempty names and sizes")
            entries.append(
                PartitionEntry(name, part_type, subtype, address, size, flags)
            )
            continue
        if magic == PARTITION_MD5_MAGIC:
            if chunk[2:16] != b"\xff" * 14:
                raise ValueError("partition table MD5 record padding is invalid")
            if hashlib.md5(data[:offset]).digest() != chunk[16:32]:  # noqa: S324
                raise ValueError("partition table MD5 record does not match")
            if any(byte != 0xFF for byte in data[offset + 32 :]):
                raise ValueError("partition table contains data after its MD5 record")
            md5_seen = True
            break
        if magic == 0xFFFF:
            raise ValueError("partition table ended before its required MD5 record")
        raise ValueError(f"partition table contains invalid magic 0x{magic:04X}")

    if not md5_seen:
        raise ValueError("partition table has no valid MD5 record")
    names = [entry.name for entry in entries]
    if len(names) != len(set(names)):
        raise ValueError("partition table contains duplicate names")
    ordered = sorted(entries, key=lambda entry: entry.offset)
    for previous, current in zip(ordered, ordered[1:]):
        if previous.offset + previous.size > current.offset:
            raise ValueError("partition table contains overlapping entries")
    return entries


def partition_table_sha256(data: bytes) -> str:
    if len(data) < PARTITION_TABLE_ARTIFACT_SIZE:
        raise ValueError("partition table read is shorter than the artifact")
    return hashlib.sha256(data[:PARTITION_TABLE_ARTIFACT_SIZE]).hexdigest()


def classify_layout(entries: list[PartitionEntry]) -> str:
    if entries == PREDECESSOR_ENTRIES:
        return "supported_predecessor"
    if entries == DEDICATED_RETAINED_ENTRIES:
        return "dedicated_retained"
    return "unsupported"


def failed_candidate_state_is_erasable(storage_status: dict[str, Any]) -> bool:
    retained = storage_status.get("retained_nvs")
    return bool(
        isinstance(retained, dict)
        and all(retained.get(key) == value for key, value in EXPECTED_FAILED_RETAINED_STATE.items())
    )


def _required_flash_file(
    flash_files: object, offset: int, expected_sha256: object
) -> bool:
    if not isinstance(flash_files, list):
        return False
    expected = normalize_sha256(expected_sha256)
    if expected is None:
        return False
    matches = [
        row
        for row in flash_files
        if isinstance(row, dict)
        and row.get("offset") == offset
        and normalize_sha256(row.get("sha256")) == expected
    ]
    return len(matches) == 1


def validate_failed_candidate_evidence(
    manifest_path: Path,
    flash_receipt_path: Path,
    first_boot_receipt_path: Path,
    *,
    port: str,
    live_mac: str,
    raw_retained_sha256: str,
) -> dict[str, Any]:
    manifest_digest = file_sha256(manifest_path)
    if manifest_digest != FAILED_PRE_ANCHOR_EVIDENCE_MANIFEST_SHA256:
        raise ValueError("failed-candidate evidence manifest is not the audited file")
    manifest = read_json_object(manifest_path, "failed-candidate evidence manifest")

    required_manifest = {
        "schema": 1,
        "kind": "d1l_retained_failed_candidate_evidence",
        "audit_state": "accepted_failed_first_boot",
        "commit": FAILED_PRE_ANCHOR_COMMIT,
        "github_actions_run": "29174653535",
        "first_boot_failed_before_retained_initialization": True,
    }
    if any(manifest.get(key) != value for key, value in required_manifest.items()):
        raise ValueError("failed-candidate evidence manifest fields do not match")
    if not manifest.get("port") or str(port).upper() != str(manifest["port"]).upper():
        raise ValueError("current port does not match the audited incident port")
    if manifest.get("expected_retained_state") != EXPECTED_FAILED_RETAINED_STATE:
        raise ValueError("manifest retained-state assertion is not exact")

    device = manifest.get("device")
    if not isinstance(device, dict) or device.get("chip") != ESPTOOL_CHIP:
        raise ValueError("manifest device chip is not the D1L ESP32-S3")
    if str(device.get("mac", "")).lower() != str(live_mac).lower():
        raise ValueError("live ESP32 MAC does not match the audited device")

    raw_region = manifest.get("pre_erase_retained_region")
    if not isinstance(raw_region, dict):
        raise ValueError("manifest has no audited raw retained-region fingerprint")
    if (
        raw_region.get("offset") != RETAINED_OFFSET
        or raw_region.get("size") != RETAINED_SIZE
        or normalize_sha256(raw_region.get("sha256")) != raw_retained_sha256
        or raw_region.get("all_ff") is not False
        or raw_region.get("temporary_capture_deleted") is not True
        or raw_region.get("backup_created") is not False
    ):
        raise ValueError("live retained bytes do not match the one-time audited incident")

    flash_binding = manifest.get("flash_receipt")
    boot_binding = manifest.get("first_boot_receipt")
    if not isinstance(flash_binding, dict) or not isinstance(boot_binding, dict):
        raise ValueError("manifest receipt bindings are missing")
    if (
        flash_receipt_path.name != flash_binding.get("name")
        or file_sha256(flash_receipt_path)
        != normalize_sha256(flash_binding.get("sha256"))
    ):
        raise ValueError("flash receipt does not match the audited incident")
    if (
        first_boot_receipt_path.name != boot_binding.get("name")
        or file_sha256(first_boot_receipt_path)
        != normalize_sha256(boot_binding.get("sha256"))
    ):
        raise ValueError("first-boot receipt does not match the audited incident")

    flash = read_json_object(flash_receipt_path, "failed-candidate flash receipt")
    if not (
        flash.get("schema") == 1
        and flash.get("kind") == "esp32_flash"
        and flash.get("mode") == "hardware"
        and flash.get("ok") is True
        and str(flash.get("port", "")).upper() == str(port).upper()
        and exact_commit(flash.get("commit")) == FAILED_PRE_ANCHOR_COMMIT
        and str(flash.get("github_actions_run")) == manifest["github_actions_run"]
        and flash.get("public_rf_tx") is False
        and flash.get("formats_sd") is False
        and isinstance(flash.get("flashed_at"), str)
    ):
        raise ValueError("flash receipt semantics do not prove the exact incident flash")
    artifact = manifest.get("artifact")
    verification = flash.get("artifact_verification")
    if not isinstance(artifact, dict) or not isinstance(verification, dict):
        raise ValueError("artifact verification evidence is missing")
    artifact_hashes = {
        key: normalize_sha256(artifact.get(key))
        for key in (
            "manifest_sha256",
            "bootloader_sha256",
            "partition_table_sha256",
            "application_sha256",
        )
    }
    if any(value is None for value in artifact_hashes.values()):
        raise ValueError("manifest artifact hashes are not exact SHA256 values")
    if not (
        verification.get("ok") is True
        and verification.get("manifest_complete") is True
        and normalize_sha256(verification.get("manifest_sha256"))
        == artifact_hashes["manifest_sha256"]
        and verification.get("required_flash_roles")
        == {
            "0x0": "build/bootloader/bootloader.bin",
            "0x8000": "build/partition_table/partition-table.bin",
            "0x10000": "build/meshcore_deskos_d1l.bin",
        }
        and _required_flash_file(
            verification.get("flash_files"), 0, artifact.get("bootloader_sha256")
        )
        and _required_flash_file(
            verification.get("flash_files"),
            PARTITION_TABLE_OFFSET,
            artifact.get("partition_table_sha256"),
        )
        and _required_flash_file(
            verification.get("flash_files"), 0x10000, artifact.get("application_sha256")
        )
        and len(verification.get("flash_files", [])) == 3
    ):
        raise ValueError("flash receipt artifact hashes or roles do not match")

    first_boot = read_json_object(
        first_boot_receipt_path, "failed-candidate first-boot receipt"
    )
    git = first_boot.get("git")
    if not (
        first_boot.get("schema") == 1
        and first_boot.get("mode") == "hardware-preflight"
        and first_boot.get("ok") is True
        and first_boot.get("serial_commands_ok") is True
        and str(first_boot.get("port", "")).upper() == str(port).upper()
        and exact_commit(first_boot.get("commit")) == FAILED_PRE_ANCHOR_COMMIT
        and first_boot.get("public_rf_tx") is False
        and first_boot.get("formats_sd") is False
        and first_boot.get("copies_uf2") is False
        and isinstance(git, dict)
        and exact_commit(git.get("commit")) == FAILED_PRE_ANCHOR_COMMIT
        and git.get("dirty") is False
    ):
        raise ValueError("first-boot receipt semantics do not prove the incident")
    status_samples = [
        first_boot.get("initial_storage_status"),
        first_boot.get("storage_status"),
        *first_boot.get("post_mount_storage_statuses", []),
    ]
    if len(status_samples) < 3 or not all(
        isinstance(sample, dict) and failed_candidate_state_is_erasable(sample)
        for sample in status_samples
    ):
        raise ValueError("first-boot receipt does not repeatedly prove failed pre-init state")

    return {
        "manifest": str(manifest_path),
        "manifest_sha256": manifest_digest,
        "flash_receipt": str(flash_receipt_path),
        "flash_receipt_sha256": file_sha256(flash_receipt_path),
        "first_boot_receipt": str(first_boot_receipt_path),
        "first_boot_receipt_sha256": file_sha256(first_boot_receipt_path),
        "github_actions_run": manifest["github_actions_run"],
        "device_mac": str(live_mac).lower(),
        "audited_identity_fingerprint": device.get("identity_fingerprint"),
        "raw_retained_sha256": raw_retained_sha256,
        "storage_status_samples": len(status_samples),
    }


def erase_authorized(
    layout: str,
    expected_commit: str,
    device_commit: str,
    table_sha256: str,
    storage_status: dict[str, Any],
    failed_candidate_evidence: dict[str, Any] | None = None,
) -> tuple[bool, str]:
    if exact_commit(expected_commit) is None or device_commit != expected_commit:
        return False, "exact running commit does not match the authorized predecessor"
    if layout == "supported_predecessor":
        expected_table = SUPPORTED_PREDECESSOR_TABLE_SHA256.get(expected_commit)
        if expected_table is None:
            return False, "running commit is not in the audited predecessor allowlist"
        if table_sha256 != expected_table:
            return False, "predecessor partition-table artifact hash does not match"
        return True, "exact predecessor layout, artifact hash, and running commit verified"
    if layout == "dedicated_retained":
        if expected_commit != FAILED_PRE_ANCHOR_COMMIT:
            return False, "dedicated layout is not the known failed pre-anchor candidate"
        if table_sha256 != FAILED_PRE_ANCHOR_TABLE_SHA256:
            return False, "failed candidate partition-table artifact hash does not match"
        if not failed_candidate_state_is_erasable(storage_status):
            return False, "candidate retained state may contain initialized user data"
        if failed_candidate_evidence is None:
            return False, "exact one-time failed-candidate evidence is required"
        return True, "one-time failed candidate, device, raw bytes, and receipts verified"
    return False, "partition layout is not an authorized predecessor"


def serial_json_command(
    port: str, command: str, timeout_s: float = 4.0, baud: int = 115200
) -> dict[str, Any]:
    import serial  # Lazy import keeps host-only parser tests dependency-light.

    device = open_d1l_serial(
        serial, port=port, baudrate=baud, timeout=min(0.2, timeout_s)
    )
    try:
        device.reset_input_buffer()
        return send_console_command(device, command, timeout_s)
    finally:
        device.close()


def run_esptool(
    port: str,
    command: list[str],
    timeout_s: float,
    *,
    before: str = "default-reset",
    after: str = "hard-reset",
) -> dict[str, Any]:
    argv = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        ESPTOOL_CHIP,
        "--port",
        port,
        "--before",
        before,
        "--after",
        after,
        *command,
    ]
    completed = subprocess.run(
        argv,
        check=True,
        capture_output=True,
        text=True,
        timeout=timeout_s,
    )
    mac_matches = re.findall(
        r"\bMAC:\s*([0-9a-f]{2}(?::[0-9a-f]{2}){5})\b",
        completed.stdout,
        flags=re.IGNORECASE,
    )
    chip_matches = re.findall(
        r"Connected to\s+(ESP32-[A-Z0-9]+)", completed.stdout, flags=re.IGNORECASE
    )
    return {
        "argv": argv[1:],
        "returncode": completed.returncode,
        "before": before,
        "after": after,
        "chip": chip_matches[-1].lower() if chip_matches else None,
        "mac": mac_matches[-1].lower() if mac_matches else None,
        "stdout_tail": completed.stdout[-2000:],
        "stderr_tail": completed.stderr[-2000:],
    }


def dry_run_report(expected_commit: str) -> dict[str, Any]:
    return {
        "schema": 1,
        "ok": True,
        "cmd": "prepare retained nvs upgrade",
        "dry_run": True,
        "expected_predecessor_commit": expected_commit,
        "esptool_chip": ESPTOOL_CHIP,
        "reads_preflash_partition_table": True,
        "requires_exact_partition_table_hash": True,
        "requires_exact_running_commit": True,
        "requires_predecessor_provenance": True,
        "requires_failed_candidate_incident_manifest": True,
        "requires_failed_candidate_receipt_hashes": True,
        "requires_failed_candidate_device_identity": True,
        "requires_failed_candidate_raw_region_hash": True,
        "supported_predecessor_commits": sorted(SUPPORTED_PREDECESSOR_COMMITS),
        "erase_offset": RETAINED_OFFSET,
        "erase_size": RETAINED_SIZE,
        "confirmation": CONFIRM_SCOPE,
        "erases_default_nvs": False,
        "erases_meta_partition": False,
        "formats_sd": False,
        "public_rf_tx": False,
        "backup_created": False,
    }


def write_receipt(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
    try:
        with temporary.open("w", encoding="utf-8", newline="\n") as destination:
            destination.write(payload)
            destination.flush()
            os.fsync(destination.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass


def _base_report(args: argparse.Namespace, expected_commit: str) -> dict[str, Any]:
    return {
        "schema": 1,
        "ok": False,
        "cmd": "prepare retained nvs upgrade",
        "stage": "starting",
        "started_at_utc": utc_now(),
        "updated_at_utc": utc_now(),
        "port": args.port,
        "expected_predecessor_commit": expected_commit,
        "esptool_chip": ESPTOOL_CHIP,
        "erase_offset": RETAINED_OFFSET,
        "erase_size": RETAINED_SIZE,
        "erase_started": False,
        "erase_completed": False,
        "verify_started": False,
        "verify_read_completed": False,
        "verify_completed": False,
        "verify_all_ff": False,
        "erases_default_nvs": False,
        "erases_meta_partition": False,
        "formats_sd": False,
        "public_rf_tx": False,
        "backup_created": False,
    }


def _advance_receipt(
    receipt: Path, report: dict[str, Any], stage: str, **updates: Any
) -> None:
    report.update(updates)
    report["stage"] = stage
    report["updated_at_utc"] = utc_now()
    write_receipt(receipt, report)


def run(args: argparse.Namespace) -> dict[str, Any]:
    expected_commit = exact_commit(args.expected_predecessor_commit)
    if expected_commit is None:
        raise ValueError("--expected-predecessor-commit must be an exact 40-character SHA")
    if args.confirm_scope != CONFIRM_SCOPE:
        raise ValueError(f"--confirm-scope must equal {CONFIRM_SCOPE}")
    if args.receipt is None:
        raise ValueError("--receipt is required for a hardware erase")

    receipt = Path(args.receipt)
    report = _base_report(args, expected_commit)
    write_receipt(receipt, report)
    bootloader_quiesced = False

    try:
        with tempfile.TemporaryDirectory(prefix="d1l-retained-init-") as temp_dir:
            temp = Path(temp_dir)
            table_path = temp / "partition-table.bin"
            table_read = run_esptool(
                args.port,
                [
                    "read-flash",
                    hex(PARTITION_TABLE_OFFSET),
                    hex(PARTITION_TABLE_READ_SIZE),
                    str(table_path),
                ],
                120.0,
                before="default-reset",
                after="hard-reset",
            )
            table_bytes = table_path.read_bytes()
            entries = parse_partition_table(table_bytes)
            layout = classify_layout(entries)
            table_digest = partition_table_sha256(table_bytes)
            table_mac = table_read.get("mac")
            if table_read.get("chip") != "esp32-s3" or not isinstance(table_mac, str):
                raise RuntimeError("partition-table read did not prove the expected ESP32-S3 identity")
            _advance_receipt(
                receipt,
                report,
                "partition_table_verified",
                layout=layout,
                partition_table_sha256=table_digest,
                partition_entries=[asdict(entry) for entry in entries],
                partition_table_read=table_read,
                table_read_device_mac=table_mac,
            )

            time.sleep(max(0.0, args.post_reset_settle))
            version = serial_json_command(args.port, "version", args.timeout)
            storage = serial_json_command(args.port, "storage status", args.timeout)
            device_commit = exact_commit(version.get("build_commit"))
            if device_commit is None:
                raise RuntimeError("device version response has no exact build_commit")
            _advance_receipt(
                receipt,
                report,
                "live_provenance_verified",
                device_build_commit=device_commit,
                version_before=version,
                storage_status_before=storage,
            )

            raw_path = temp / "retained-before.bin"
            raw_read = run_esptool(
                args.port,
                ["read-flash", hex(RETAINED_OFFSET), hex(RETAINED_SIZE), str(raw_path)],
                180.0,
                before="default-reset",
                after="no-reset",
            )
            bootloader_quiesced = True
            try:
                raw_bytes = raw_path.read_bytes()
            finally:
                raw_path.unlink(missing_ok=True)
            if len(raw_bytes) != RETAINED_SIZE:
                raise RuntimeError("pre-erase retained read returned the wrong size")
            raw_digest = hashlib.sha256(raw_bytes).hexdigest()
            live_mac = raw_read.get("mac")
            if raw_read.get("chip") != "esp32-s3" or not isinstance(live_mac, str):
                raise RuntimeError("esptool did not prove the expected ESP32-S3 identity")
            if live_mac.lower() != table_mac.lower():
                raise RuntimeError("partition-table and retained reads came from different devices")

            failed_evidence = None
            if layout == "dedicated_retained":
                evidence_paths = (
                    args.failed_candidate_evidence_manifest,
                    args.failed_candidate_flash_receipt,
                    args.failed_candidate_first_boot_receipt,
                )
                if any(path is None for path in evidence_paths):
                    raise ValueError(
                        "dedicated-layout recovery requires the audited manifest, flash receipt, and first-boot receipt"
                    )
                failed_evidence = validate_failed_candidate_evidence(
                    Path(evidence_paths[0]),
                    Path(evidence_paths[1]),
                    Path(evidence_paths[2]),
                    port=args.port,
                    live_mac=live_mac,
                    raw_retained_sha256=raw_digest,
                )

            authorized, reason = erase_authorized(
                layout,
                expected_commit,
                device_commit,
                table_digest,
                storage,
                failed_evidence,
            )
            if not authorized:
                raise RuntimeError(reason)
            _advance_receipt(
                receipt,
                report,
                "authorized_pre_erase",
                authorization_reason=reason,
                failed_candidate_evidence=failed_evidence,
                raw_retained_before_sha256=raw_digest,
                raw_retained_before_all_ff=all(byte == 0xFF for byte in raw_bytes),
                raw_retained_read=raw_read,
                device_mac=live_mac,
            )

            _advance_receipt(
                receipt,
                report,
                "erase_started",
                erase_started=True,
                erase_started_at_utc=utc_now(),
            )
            erase = run_esptool(
                args.port,
                ["erase-region", hex(RETAINED_OFFSET), hex(RETAINED_SIZE)],
                180.0,
                before="no-reset",
                after="no-reset",
            )
            _advance_receipt(
                receipt,
                report,
                "erase_completed",
                erase_completed=True,
                erase_completed_at_utc=utc_now(),
                erase=erase,
            )

            _advance_receipt(
                receipt,
                report,
                "verify_started",
                verify_started=True,
                verify_started_at_utc=utc_now(),
            )
            verify_path = temp / "retained-verify.bin"
            verify = run_esptool(
                args.port,
                [
                    "read-flash",
                    hex(RETAINED_OFFSET),
                    hex(RETAINED_SIZE),
                    str(verify_path),
                ],
                180.0,
                before="no-reset",
                after="no-reset",
            )
            verify_bytes = verify_path.read_bytes()
            blank = len(verify_bytes) == RETAINED_SIZE and all(
                byte == 0xFF for byte in verify_bytes
            )
            _advance_receipt(
                receipt,
                report,
                "verify_read_completed",
                verify_read_completed=True,
                verify_read_completed_at_utc=utc_now(),
                verify_read_size=len(verify_bytes),
                verify_all_ff=blank,
                verify_sha256=hashlib.sha256(verify_bytes).hexdigest(),
                verify_read=verify,
            )
            if not blank:
                raise RuntimeError("scoped erase verification was not completely blank")

            post_verify_reset = run_esptool(
                args.port,
                ["read-mac"],
                120.0,
                before="no-reset",
                after="hard-reset",
            )
            bootloader_quiesced = False
            if (
                post_verify_reset.get("chip") != "esp32-s3"
                or str(post_verify_reset.get("mac", "")).lower()
                != live_mac.lower()
            ):
                raise RuntimeError("post-verification reset did not match the audited ESP32-S3")

            _advance_receipt(
                receipt,
                report,
                "verified_blank",
                ok=True,
                verify_completed=True,
                verify_completed_at_utc=utc_now(),
                verify_all_ff=True,
                post_verify_reset=post_verify_reset,
                completed_at_utc=utc_now(),
            )
            return report
    except Exception as exc:
        failed_from_stage = report.get("stage")
        if bootloader_quiesced and report.get("erase_started") is not True:
            try:
                report["failure_reset"] = run_esptool(
                    args.port,
                    ["read-mac"],
                    120.0,
                    before="no-reset",
                    after="hard-reset",
                )
            except Exception as reset_exc:
                report["failure_reset_error"] = str(reset_exc)
        report.update(
            {
                "ok": False,
                "stage": "failed",
                "failed_from_stage": failed_from_stage,
                "failed_at_utc": utc_now(),
                "updated_at_utc": utc_now(),
                "error": str(exc),
            }
        )
        write_receipt(receipt, report)
        raise UpgradePreparationError(report, exc) from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=None)
    parser.add_argument("--expected-predecessor-commit", required=True)
    parser.add_argument("--confirm-scope", default=None)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--post-reset-settle", type=float, default=7.0)
    parser.add_argument("--failed-candidate-evidence-manifest", type=Path)
    parser.add_argument("--failed-candidate-flash-receipt", type=Path)
    parser.add_argument("--failed-candidate-first-boot-receipt", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    expected = exact_commit(args.expected_predecessor_commit)
    if expected is None:
        parser.error("--expected-predecessor-commit must be an exact 40-character SHA")
    if args.dry_run:
        print(json.dumps(dry_run_report(expected), indent=2, sort_keys=True))
        return 0
    if not args.port:
        parser.error("--port is required unless --dry-run is used")
    if args.receipt is None:
        parser.error("--receipt is required for a hardware erase")

    try:
        report = run(args)
    except UpgradePreparationError as exc:
        print(json.dumps(exc.report, indent=2, sort_keys=True))
        return 1
    except Exception as exc:
        failure = _base_report(args, expected)
        failure.update(
            {
                "stage": "failed",
                "failed_from_stage": "argument_validation",
                "failed_at_utc": utc_now(),
                "error": str(exc),
            }
        )
        write_receipt(args.receipt, failure)
        print(json.dumps(failure, indent=2, sort_keys=True))
        return 1

    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
