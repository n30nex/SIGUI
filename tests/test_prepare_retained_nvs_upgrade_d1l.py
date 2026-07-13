import hashlib
import json
import struct
from pathlib import Path
from types import SimpleNamespace

import pytest

from scripts import prepare_retained_nvs_upgrade_d1l as prepare


ROOT = Path(__file__).resolve().parents[1]


def table_bytes(entries: list[prepare.PartitionEntry]) -> bytes:
    packed = bytearray()
    for entry in entries:
        packed.extend(
            struct.pack(
                "<HBBII16sI",
                prepare.PARTITION_MAGIC,
                entry.type,
                entry.subtype,
                entry.offset,
                entry.size,
                entry.name.encode("ascii").ljust(16, b"\0"),
                entry.flags,
            )
        )
    packed.extend(b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(packed).digest())
    return bytes(packed).ljust(prepare.PARTITION_TABLE_READ_SIZE, b"\xff")


def failed_candidate_storage(marker_ready: bool = False) -> dict:
    retained = dict(prepare.EXPECTED_FAILED_RETAINED_STATE)
    retained["marker_ready"] = marker_ready
    return {"retained_nvs": retained}


def write_json(path: Path, value: dict) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def build_incident_evidence(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, raw_sha256: str
) -> tuple[Path, Path, Path, dict]:
    artifact = {
        "manifest_sha256": "a" * 64,
        "bootloader_sha256": "b" * 64,
        "partition_table_sha256": prepare.FAILED_PRE_ANCHOR_TABLE_SHA256,
        "application_sha256": "c" * 64,
    }
    flash = {
        "schema": 1,
        "kind": "esp32_flash",
        "mode": "hardware",
        "port": "COM12",
        "commit": prepare.FAILED_PRE_ANCHOR_COMMIT,
        "github_actions_run": "29174653535",
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
        "flashed_at": "2026-07-12T01:11:27Z",
        "artifact_verification": {
            "ok": True,
            "manifest_complete": True,
            "manifest_sha256": artifact["manifest_sha256"],
            "required_flash_roles": {
                "0x0": "build/bootloader/bootloader.bin",
                "0x8000": "build/partition_table/partition-table.bin",
                "0x10000": "build/meshcore_deskos_d1l.bin",
            },
            "flash_files": [
                {"offset": 0, "sha256": artifact["bootloader_sha256"]},
                {
                    "offset": prepare.PARTITION_TABLE_OFFSET,
                    "sha256": artifact["partition_table_sha256"],
                },
                {"offset": 0x10000, "sha256": artifact["application_sha256"]},
            ],
        },
    }
    flash_path = tmp_path / "flash.json"
    write_json(flash_path, flash)

    status = failed_candidate_storage()
    first_boot = {
        "schema": 1,
        "mode": "hardware-preflight",
        "port": "COM12",
        "commit": prepare.FAILED_PRE_ANCHOR_COMMIT,
        "public_rf_tx": False,
        "formats_sd": False,
        "copies_uf2": False,
        "ok": True,
        "serial_commands_ok": True,
        "git": {
            "commit": prepare.FAILED_PRE_ANCHOR_COMMIT,
            "dirty": False,
        },
        "initial_storage_status": status,
        "storage_status": status,
        "post_mount_storage_statuses": [status],
    }
    first_boot_path = tmp_path / "first-boot.json"
    write_json(first_boot_path, first_boot)

    manifest = {
        "schema": 1,
        "kind": "d1l_retained_failed_candidate_evidence",
        "audit_state": "accepted_failed_first_boot",
        "commit": prepare.FAILED_PRE_ANCHOR_COMMIT,
        "github_actions_run": "29174653535",
        "port": "COM12",
        "device": {
            "chip": prepare.ESPTOOL_CHIP,
            "mac": "d8:3b:da:75:72:6c",
            "identity_fingerprint": "FB15C73CA24D6DAF",
        },
        "first_boot_failed_before_retained_initialization": True,
        "flash_receipt": {
            "name": flash_path.name,
            "sha256": prepare.file_sha256(flash_path),
        },
        "first_boot_receipt": {
            "name": first_boot_path.name,
            "sha256": prepare.file_sha256(first_boot_path),
        },
        "artifact": artifact,
        "expected_retained_state": dict(prepare.EXPECTED_FAILED_RETAINED_STATE),
        "pre_erase_retained_region": {
            "offset": prepare.RETAINED_OFFSET,
            "size": prepare.RETAINED_SIZE,
            "sha256": raw_sha256,
            "all_ff": False,
            "temporary_capture_deleted": True,
            "backup_created": False,
        },
    }
    manifest_path = tmp_path / "manifest.json"
    write_json(manifest_path, manifest)
    monkeypatch.setattr(
        prepare,
        "FAILED_PRE_ANCHOR_EVIDENCE_MANIFEST_SHA256",
        prepare.file_sha256(manifest_path),
    )
    return manifest_path, flash_path, first_boot_path, manifest


def run_args(tmp_path: Path, evidence: tuple[Path, Path, Path, dict]) -> SimpleNamespace:
    manifest, flash, first_boot, _ = evidence
    return SimpleNamespace(
        port="COM12",
        expected_predecessor_commit=prepare.FAILED_PRE_ANCHOR_COMMIT,
        confirm_scope=prepare.CONFIRM_SCOPE,
        receipt=tmp_path / "receipt.json",
        timeout=0.01,
        post_reset_settle=0.0,
        failed_candidate_evidence_manifest=manifest,
        failed_candidate_flash_receipt=flash,
        failed_candidate_first_boot_receipt=first_boot,
    )


def install_fake_serial(monkeypatch: pytest.MonkeyPatch) -> None:
    def fake_serial(_port: str, command: str, _timeout: float) -> dict:
        if command == "version":
            return {"ok": True, "cmd": command, "build_commit": prepare.FAILED_PRE_ANCHOR_COMMIT}
        if command == "storage status":
            return failed_candidate_storage()
        raise AssertionError(command)

    monkeypatch.setattr(prepare, "serial_json_command", fake_serial)


def test_partition_parser_requires_exact_layout_hash_and_valid_md5():
    predecessor_bytes = table_bytes(prepare.PREDECESSOR_ENTRIES)
    dedicated_bytes = table_bytes(prepare.DEDICATED_RETAINED_ENTRIES)
    predecessor = prepare.parse_partition_table(predecessor_bytes)
    dedicated = prepare.parse_partition_table(dedicated_bytes)

    assert prepare.classify_layout(predecessor) == "supported_predecessor"
    assert prepare.classify_layout(dedicated) == "dedicated_retained"
    assert prepare.partition_table_sha256(predecessor_bytes) == prepare.PREDECESSOR_TABLE_SHA256
    assert prepare.partition_table_sha256(dedicated_bytes) == prepare.FAILED_PRE_ANCHOR_TABLE_SHA256

    duplicate = prepare.PREDECESSOR_ENTRIES + [
        prepare.PartitionEntry("nvs", 1, 2, 0x800000, 0x1000, 0)
    ]
    with pytest.raises(ValueError, match="duplicate"):
        prepare.parse_partition_table(table_bytes(duplicate))

    overlap = list(prepare.PREDECESSOR_ENTRIES)
    overlap[1] = prepare.PartitionEntry("phy_init", 1, 1, 0xE000, 0x2000, 0)
    with pytest.raises(ValueError, match="overlapping"):
        prepare.parse_partition_table(table_bytes(overlap))

    unexpected = list(prepare.DEDICATED_RETAINED_ENTRIES) + [
        prepare.PartitionEntry("extra", 1, 0x40, 0x800000, 0x1000, 0)
    ]
    assert prepare.classify_layout(prepare.parse_partition_table(table_bytes(unexpected))) == "unsupported"

    corrupt = bytearray(dedicated_bytes)
    corrupt[len(prepare.DEDICATED_RETAINED_ENTRIES) * 32 + 16] ^= 0x01
    with pytest.raises(ValueError, match="MD5"):
        prepare.parse_partition_table(bytes(corrupt))


def test_erase_authorization_requires_exact_commit_table_and_incident_evidence():
    predecessor = sorted(prepare.SUPPORTED_PREDECESSOR_COMMITS)[0]
    ok, reason = prepare.erase_authorized(
        "supported_predecessor",
        predecessor,
        predecessor,
        prepare.PREDECESSOR_TABLE_SHA256,
        {},
    )
    assert ok is True
    assert "artifact hash" in reason

    ok, _ = prepare.erase_authorized(
        "supported_predecessor", predecessor, predecessor, "0" * 64, {}
    )
    assert ok is False

    candidate = prepare.FAILED_PRE_ANCHOR_COMMIT
    ok, _ = prepare.erase_authorized(
        "dedicated_retained",
        candidate,
        candidate,
        prepare.FAILED_PRE_ANCHOR_TABLE_SHA256,
        failed_candidate_storage(),
        None,
    )
    assert ok is False

    ok, reason = prepare.erase_authorized(
        "dedicated_retained",
        candidate,
        candidate,
        prepare.FAILED_PRE_ANCHOR_TABLE_SHA256,
        failed_candidate_storage(),
        {"manifest_sha256": "a" * 64},
    )
    assert ok is True
    assert "one-time" in reason

    ok, _ = prepare.erase_authorized(
        "dedicated_retained",
        candidate,
        candidate,
        prepare.FAILED_PRE_ANCHOR_TABLE_SHA256,
        failed_candidate_storage(marker_ready=True),
        {"manifest_sha256": "a" * 64},
    )
    assert ok is False


def test_incident_evidence_binds_receipts_device_and_raw_region(tmp_path, monkeypatch):
    raw_sha = hashlib.sha256(b"known failed bytes").hexdigest()
    manifest, flash, first_boot, _ = build_incident_evidence(
        tmp_path, monkeypatch, raw_sha
    )
    result = prepare.validate_failed_candidate_evidence(
        manifest,
        flash,
        first_boot,
        port="COM12",
        live_mac="d8:3b:da:75:72:6c",
        raw_retained_sha256=raw_sha,
    )
    assert result["raw_retained_sha256"] == raw_sha
    assert result["storage_status_samples"] == 3

    with pytest.raises(ValueError, match="retained bytes"):
        prepare.validate_failed_candidate_evidence(
            manifest,
            flash,
            first_boot,
            port="COM12",
            live_mac="d8:3b:da:75:72:6c",
            raw_retained_sha256="0" * 64,
        )
    with pytest.raises(ValueError, match="MAC"):
        prepare.validate_failed_candidate_evidence(
            manifest,
            flash,
            first_boot,
            port="COM12",
            live_mac="00:00:00:00:00:00",
            raw_retained_sha256=raw_sha,
        )

    flash.write_text(flash.read_text(encoding="utf-8") + " ", encoding="utf-8")
    with pytest.raises(ValueError, match="flash receipt"):
        prepare.validate_failed_candidate_evidence(
            manifest,
            flash,
            first_boot,
            port="COM12",
            live_mac="d8:3b:da:75:72:6c",
            raw_retained_sha256=raw_sha,
        )


def test_committed_incident_manifest_matches_the_code_pinned_lf_hash():
    manifest = (
        ROOT
        / "docs"
        / "evidence"
        / "retained_nvs_failed_candidate_78beca8_COM12.json"
    )
    assert b"\r\n" not in manifest.read_bytes()
    assert (
        prepare.file_sha256(manifest)
        == prepare.FAILED_PRE_ANCHOR_EVIDENCE_MANIFEST_SHA256
    )


def test_run_persists_intent_and_uses_quiescent_reset_sequence(tmp_path, monkeypatch):
    raw_bytes = bytes([0xA5]) * prepare.RETAINED_SIZE
    raw_sha = hashlib.sha256(raw_bytes).hexdigest()
    evidence = build_incident_evidence(tmp_path, monkeypatch, raw_sha)
    args = run_args(tmp_path, evidence)
    install_fake_serial(monkeypatch)
    calls: list[tuple[list[str], str, str]] = []
    raw_capture_path: Path | None = None

    def fake_esptool(_port, command, _timeout, *, before, after):
        nonlocal raw_capture_path
        calls.append((list(command), before, after))
        if command[0] == "read-flash":
            output = Path(command[-1])
            address = int(command[1], 0)
            if address == prepare.PARTITION_TABLE_OFFSET:
                output.write_bytes(table_bytes(prepare.DEDICATED_RETAINED_ENTRIES))
            elif "retained-before" in output.name:
                output.write_bytes(raw_bytes)
                raw_capture_path = output
            else:
                output.write_bytes(b"\xff" * prepare.RETAINED_SIZE)
        if command[0] == "erase-region":
            assert raw_capture_path is not None
            assert not raw_capture_path.exists()
            intent = json.loads(Path(args.receipt).read_text(encoding="utf-8"))
            assert intent["stage"] == "erase_started"
            assert intent["erase_started"] is True
            assert intent["erase_completed"] is False
        return {
            "chip": "esp32-s3",
            "mac": "d8:3b:da:75:72:6c",
            "before": before,
            "after": after,
        }

    monkeypatch.setattr(prepare, "run_esptool", fake_esptool)
    report = prepare.run(args)

    assert report["ok"] is True
    assert report["stage"] == "verified_blank"
    assert report["erase_completed"] is True
    assert report["verify_completed"] is True
    assert [(row[0][0], row[1], row[2]) for row in calls] == [
        ("read-flash", "default-reset", "hard-reset"),
        ("read-flash", "default-reset", "no-reset"),
        ("erase-region", "no-reset", "no-reset"),
        ("read-flash", "no-reset", "no-reset"),
        ("read-mac", "no-reset", "hard-reset"),
    ]
    persisted = json.loads(Path(args.receipt).read_text(encoding="utf-8"))
    assert persisted["stage"] == "verified_blank"
    assert persisted["verify_all_ff"] is True


def test_verify_failure_preserves_completed_erase_receipt(tmp_path, monkeypatch):
    raw_bytes = bytes([0x5A]) * prepare.RETAINED_SIZE
    raw_sha = hashlib.sha256(raw_bytes).hexdigest()
    evidence = build_incident_evidence(tmp_path, monkeypatch, raw_sha)
    args = run_args(tmp_path, evidence)
    install_fake_serial(monkeypatch)
    calls: list[tuple[str, str, str]] = []

    def fake_esptool(_port, command, _timeout, *, before, after):
        calls.append((command[0], before, after))
        if command[0] == "read-flash":
            output = Path(command[-1])
            address = int(command[1], 0)
            if address == prepare.PARTITION_TABLE_OFFSET:
                output.write_bytes(table_bytes(prepare.DEDICATED_RETAINED_ENTRIES))
            elif "retained-before" in output.name:
                output.write_bytes(raw_bytes)
            else:
                output.write_bytes(b"\xff" * (prepare.RETAINED_SIZE - 1) + b"\x00")
        return {
            "chip": "esp32-s3",
            "mac": "d8:3b:da:75:72:6c",
            "before": before,
            "after": after,
        }

    monkeypatch.setattr(prepare, "run_esptool", fake_esptool)
    with pytest.raises(prepare.UpgradePreparationError, match="not completely blank"):
        prepare.run(args)

    persisted = json.loads(Path(args.receipt).read_text(encoding="utf-8"))
    assert persisted["ok"] is False
    assert persisted["stage"] == "failed"
    assert persisted["failed_from_stage"] == "verify_read_completed"
    assert persisted["erase_started"] is True
    assert persisted["erase_completed"] is True
    assert persisted["verify_started"] is True
    assert persisted["verify_read_completed"] is True
    assert persisted["verify_completed"] is False
    assert persisted["verify_read_size"] == prepare.RETAINED_SIZE
    assert persisted["verify_all_ff"] is False
    assert len(persisted["verify_sha256"]) == 64
    assert persisted["verify_read"]["after"] == "no-reset"
    assert "storage_status_before" in persisted
    assert "partition_table_sha256" in persisted
    assert calls[-1] == ("read-flash", "no-reset", "no-reset")
    assert not any(call == ("read-mac", "no-reset", "hard-reset") for call in calls)


def test_wrong_raw_evidence_refuses_before_erase_and_resets(tmp_path, monkeypatch):
    actual_raw = bytes([0x33]) * prepare.RETAINED_SIZE
    evidence = build_incident_evidence(
        tmp_path, monkeypatch, hashlib.sha256(b"different").hexdigest()
    )
    args = run_args(tmp_path, evidence)
    install_fake_serial(monkeypatch)
    commands: list[str] = []

    def fake_esptool(_port, command, _timeout, *, before, after):
        commands.append(command[0])
        if command[0] == "read-flash":
            output = Path(command[-1])
            if int(command[1], 0) == prepare.PARTITION_TABLE_OFFSET:
                output.write_bytes(table_bytes(prepare.DEDICATED_RETAINED_ENTRIES))
            else:
                output.write_bytes(actual_raw)
        return {
            "chip": "esp32-s3",
            "mac": "d8:3b:da:75:72:6c",
            "before": before,
            "after": after,
        }

    monkeypatch.setattr(prepare, "run_esptool", fake_esptool)
    with pytest.raises(prepare.UpgradePreparationError, match="retained bytes"):
        prepare.run(args)

    assert "erase-region" not in commands
    assert commands[-1] == "read-mac"
    persisted = json.loads(Path(args.receipt).read_text(encoding="utf-8"))
    assert persisted["erase_started"] is False
    assert persisted["failure_reset"]["after"] == "hard-reset"


def test_table_and_raw_reads_must_be_the_same_esp32(tmp_path, monkeypatch):
    raw_bytes = bytes([0x77]) * prepare.RETAINED_SIZE
    evidence = build_incident_evidence(
        tmp_path, monkeypatch, hashlib.sha256(raw_bytes).hexdigest()
    )
    args = run_args(tmp_path, evidence)
    install_fake_serial(monkeypatch)
    commands: list[str] = []

    def fake_esptool(_port, command, _timeout, *, before, after):
        commands.append(command[0])
        if command[0] == "read-flash":
            output = Path(command[-1])
            if int(command[1], 0) == prepare.PARTITION_TABLE_OFFSET:
                output.write_bytes(table_bytes(prepare.DEDICATED_RETAINED_ENTRIES))
                mac = "d8:3b:da:75:72:6c"
            else:
                output.write_bytes(raw_bytes)
                mac = "d8:3b:da:75:72:6d"
        else:
            mac = "d8:3b:da:75:72:6d"
        return {"chip": "esp32-s3", "mac": mac, "before": before, "after": after}

    monkeypatch.setattr(prepare, "run_esptool", fake_esptool)
    with pytest.raises(prepare.UpgradePreparationError, match="different devices"):
        prepare.run(args)

    assert "erase-region" not in commands
    assert commands[-1] == "read-mac"


def test_incident_manifest_receipt_port_and_semantic_tampering_refuse(
    tmp_path, monkeypatch
):
    raw_sha = hashlib.sha256(b"incident").hexdigest()

    case_manifest = tmp_path / "manifest-tamper"
    case_manifest.mkdir()
    manifest, flash, first_boot, _ = build_incident_evidence(
        case_manifest, monkeypatch, raw_sha
    )
    manifest.write_text(manifest.read_text(encoding="utf-8") + " ", encoding="utf-8")
    with pytest.raises(ValueError, match="manifest is not the audited"):
        prepare.validate_failed_candidate_evidence(
            manifest,
            flash,
            first_boot,
            port="COM12",
            live_mac="d8:3b:da:75:72:6c",
            raw_retained_sha256=raw_sha,
        )

    case_boot = tmp_path / "boot-hash-tamper"
    case_boot.mkdir()
    manifest, flash, first_boot, _ = build_incident_evidence(
        case_boot, monkeypatch, raw_sha
    )
    first_boot.write_text(
        first_boot.read_text(encoding="utf-8") + " ", encoding="utf-8"
    )
    with pytest.raises(ValueError, match="first-boot receipt"):
        prepare.validate_failed_candidate_evidence(
            manifest,
            flash,
            first_boot,
            port="COM12",
            live_mac="d8:3b:da:75:72:6c",
            raw_retained_sha256=raw_sha,
        )

    case_port = tmp_path / "port-tamper"
    case_port.mkdir()
    manifest, flash, first_boot, _ = build_incident_evidence(
        case_port, monkeypatch, raw_sha
    )
    with pytest.raises(ValueError, match="incident port"):
        prepare.validate_failed_candidate_evidence(
            manifest,
            flash,
            first_boot,
            port="COM13",
            live_mac="d8:3b:da:75:72:6c",
            raw_retained_sha256=raw_sha,
        )

    case_semantics = tmp_path / "semantic-tamper"
    case_semantics.mkdir()
    manifest, flash, first_boot, manifest_value = build_incident_evidence(
        case_semantics, monkeypatch, raw_sha
    )
    flash_value = json.loads(flash.read_text(encoding="utf-8"))
    flash_value["formats_sd"] = True
    write_json(flash, flash_value)
    manifest_value["flash_receipt"]["sha256"] = prepare.file_sha256(flash)
    write_json(manifest, manifest_value)
    monkeypatch.setattr(
        prepare,
        "FAILED_PRE_ANCHOR_EVIDENCE_MANIFEST_SHA256",
        prepare.file_sha256(manifest),
    )
    with pytest.raises(ValueError, match="incident flash"):
        prepare.validate_failed_candidate_evidence(
            manifest,
            flash,
            first_boot,
            port="COM12",
            live_mac="d8:3b:da:75:72:6c",
            raw_retained_sha256=raw_sha,
        )


def test_erase_command_failure_preserves_started_intent(tmp_path, monkeypatch):
    raw_bytes = bytes([0x44]) * prepare.RETAINED_SIZE
    evidence = build_incident_evidence(
        tmp_path, monkeypatch, hashlib.sha256(raw_bytes).hexdigest()
    )
    args = run_args(tmp_path, evidence)
    install_fake_serial(monkeypatch)

    def fake_esptool(_port, command, _timeout, *, before, after):
        if command[0] == "read-flash":
            output = Path(command[-1])
            if int(command[1], 0) == prepare.PARTITION_TABLE_OFFSET:
                output.write_bytes(table_bytes(prepare.DEDICATED_RETAINED_ENTRIES))
            else:
                output.write_bytes(raw_bytes)
        if command[0] == "erase-region":
            raise RuntimeError("simulated erase transport failure")
        return {
            "chip": "esp32-s3",
            "mac": "d8:3b:da:75:72:6c",
            "before": before,
            "after": after,
        }

    monkeypatch.setattr(prepare, "run_esptool", fake_esptool)
    with pytest.raises(prepare.UpgradePreparationError, match="erase transport"):
        prepare.run(args)

    persisted = json.loads(Path(args.receipt).read_text(encoding="utf-8"))
    assert persisted["stage"] == "failed"
    assert persisted["failed_from_stage"] == "erase_started"
    assert persisted["erase_started"] is True
    assert persisted["erase_completed"] is False
    assert persisted["verify_started"] is False


def test_supported_predecessor_path_uses_exact_table_hash_without_incident_bundle(
    tmp_path, monkeypatch
):
    predecessor = sorted(prepare.SUPPORTED_PREDECESSOR_COMMITS)[0]
    args = SimpleNamespace(
        port="COM_TEST",
        expected_predecessor_commit=predecessor,
        confirm_scope=prepare.CONFIRM_SCOPE,
        receipt=tmp_path / "predecessor-receipt.json",
        timeout=0.01,
        post_reset_settle=0.0,
        failed_candidate_evidence_manifest=None,
        failed_candidate_flash_receipt=None,
        failed_candidate_first_boot_receipt=None,
    )

    def fake_serial(_port: str, command: str, _timeout: float) -> dict:
        if command == "version":
            return {"ok": True, "cmd": command, "build_commit": predecessor}
        if command == "storage status":
            return {"ok": True, "cmd": command}
        raise AssertionError(command)

    def fake_esptool(_port, command, _timeout, *, before, after):
        if command[0] == "read-flash":
            output = Path(command[-1])
            if int(command[1], 0) == prepare.PARTITION_TABLE_OFFSET:
                output.write_bytes(table_bytes(prepare.PREDECESSOR_ENTRIES))
            elif "retained-before" in output.name:
                output.write_bytes(bytes([0x88]) * prepare.RETAINED_SIZE)
            else:
                output.write_bytes(b"\xff" * prepare.RETAINED_SIZE)
        return {
            "chip": "esp32-s3",
            "mac": "11:22:33:44:55:66",
            "before": before,
            "after": after,
        }

    monkeypatch.setattr(prepare, "serial_json_command", fake_serial)
    monkeypatch.setattr(prepare, "run_esptool", fake_esptool)
    report = prepare.run(args)
    assert report["ok"] is True
    assert report["layout"] == "supported_predecessor"
    assert report["failed_candidate_evidence"] is None
    assert report["partition_table_sha256"] == prepare.PREDECESSOR_TABLE_SHA256


def test_dry_run_and_source_keep_the_erase_narrow_and_audited():
    report = prepare.dry_run_report("a" * 40)
    assert report["ok"] is True
    assert report["erase_offset"] == 0x7E1000
    assert report["erase_size"] == 0x1F000
    assert report["confirmation"] == prepare.CONFIRM_SCOPE
    assert report["esptool_chip"] == "esp32s3"
    assert report["reads_preflash_partition_table"] is True
    assert report["requires_exact_partition_table_hash"] is True
    assert report["requires_exact_running_commit"] is True
    assert report["requires_failed_candidate_incident_manifest"] is True
    assert report["requires_failed_candidate_raw_region_hash"] is True
    assert report["erases_default_nvs"] is False
    assert report["erases_meta_partition"] is False
    assert report["formats_sd"] is False
    assert report["public_rf_tx"] is False
    assert report["backup_created"] is False

    source = (
        ROOT / "scripts" / "prepare_retained_nvs_upgrade_d1l.py"
    ).read_text(encoding="utf-8")
    assert '["erase-region", hex(RETAINED_OFFSET), hex(RETAINED_SIZE)]' in source
    assert "erase-flash" not in source
    assert "TemporaryDirectory" in source
    assert "verify_all_ff" in source
    assert "FAILED_PRE_ANCHOR_EVIDENCE_MANIFEST_SHA256" in source
    assert 'serial_json_command(args.port, "identity status"' not in source
    assert 'before="no-reset",\n                after="no-reset"' in source
