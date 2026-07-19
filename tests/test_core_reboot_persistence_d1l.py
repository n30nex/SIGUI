import base64
import copy
import json
from pathlib import Path

import pytest

from scripts import core_reboot_persistence_d1l as reboot


COMMIT = "a" * 40
RUN_ID = "123456789"
RUN_ATTEMPT = "1"
CANARY_BASE = reboot.candidate_canary(COMMIT, RUN_ID, RUN_ATTEMPT)
TOKEN = CANARY_BASE["token"]
MESSAGE_TEXT = CANARY_BASE["message_text"]
PUBLIC_KEY = CANARY_BASE["contact_public_key"]
FINGERPRINT = CANARY_BASE["contact_fingerprint"]
SOURCE_GIT = {
    "commit": COMMIT,
    "short_commit": COMMIT[:7],
    "branch": "release/24h-core",
    "dirty": False,
    "dirty_entries": [],
}


def raw_line(payload: bytes, observed_at: str = "2026-07-18T12:00:00Z"):
    return {
        "observed_at": observed_at,
        "size": len(payload),
        "sha256": reboot.sha256_bytes(payload),
        "base64": base64.b64encode(payload).decode("ascii"),
    }


def command_receipt(command: str, result: dict):
    payload = (json.dumps(result, separators=(",", ":")) + "\n").encode("ascii")
    return {
        "command": command,
        "expected_cmd": reboot.expected_command_name(command),
        "started_at": "2026-07-18T12:00:00Z",
        "ended_at": "2026-07-18T12:00:01Z",
        "raw_lines": [raw_line(payload)],
        "result": copy.deepcopy(result),
    }


def persistence():
    return {
        "loaded": True,
        "dirty": False,
        "revision": 5,
        "commits": 5,
        "failures": 0,
        "stale_snapshots": 0,
        "sd": {
            "required": False,
            "generation": 0,
            "dirty": False,
            "reconcile_pending": False,
            "commits": 0,
            "failures": 0,
            "last_error": "ESP_OK",
        },
        "nvs": {
            "dirty": False,
            "commits": 5,
            "failures": 0,
            "last_error": "ESP_OK",
        },
    }


def version():
    return {
        "schema": 1,
        "ok": True,
        "cmd": "version",
        "build_commit": COMMIT,
        "idf": "v5.5.4",
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
    }


def health(nonce: int, uptime: int, reset_reason: str):
    return {
        "schema": 1,
        "ok": True,
        "cmd": "health",
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "board_ready": True,
        "ui_ready": True,
        "nvs_ready": True,
        "boot_nonce": nonce,
        "uptime_ms": uptime,
        "reset_reason": reset_reason,
    }


def crashlog(seq: int, reset_reason: str):
    return {
        "schema": 1,
        "ok": True,
        "cmd": "crashlog",
        "total_written": seq,
        "entries": [
            {
                "seq": seq,
                "reset_reason": reset_reason,
                "crash_like": False,
            }
        ],
    }


def settings(name: str = f"D1L-Core-{COMMIT[:7]}"):
    return {
        "schema": 1,
        "ok": True,
        "cmd": "settings get",
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "node_name": name,
        "role": "companion",
        "onboarding_complete": True,
        "wifi_enabled": False,
        "ble_companion_enabled": False,
        "observer_enabled": False,
        "high_contrast": False,
        "night_mode": False,
        "path_hash_bytes": 1,
        "timezone": {
            "settings_ready": True,
            "settings_error": "ESP_OK",
            "schema_version": 1,
            "model": "fixed_utc_offset",
            "offset_minutes": -240,
            "label": "UTC-04:00",
            "auto_dst": False,
        },
        "radio": {
            "frequency_hz": 910525000,
            "bandwidth_khz": 62.5,
            "sf": 7,
            "cr": 5,
            "tx_power_dbm": 22,
            "rx_boost": True,
            "tcxo": "1.8V",
            "applied_to_radio": True,
            "radio_apply_pending": False,
            "radio_apply_error": "ESP_OK",
        },
    }


def message_page(command: str):
    is_dm = command == "messages dm"
    entry = (
        {
            "seq": 20,
            "uptime_ms": 1234,
            "fingerprint": FINGERPRINT,
            "alias": "Core Canary",
            "direction": "rx",
            "text": MESSAGE_TEXT,
            "rssi_dbm": -60,
            "snr_tenths": 40,
            "path_hash_bytes": 1,
            "path_hops": 0,
            "attempt": 0,
            "delivered": True,
            "acked": False,
            "ack_hash": 1,
            "ack_response": {
                "identity_valid": True,
                "state": "acked",
                "dispatch_count": 1,
                "last_kind": "ack",
                "last_error": "ESP_OK",
            },
        }
        if is_dm
        else {
            "seq": 10,
            "uptime_ms": 1000,
            "direction": "rx",
            "author": "Core Canary",
            "text": MESSAGE_TEXT,
            "rssi_dbm": -65,
            "snr_tenths": 35,
            "path_hash_bytes": 1,
            "path_hops": 0,
            "delivered": True,
        }
    )
    return {
        "schema": 1,
        "ok": True,
        "cmd": command,
        "count": 1,
        "capacity": 32,
        "total_written": 1,
        "dropped_oldest": 0,
        "filtered": False,
        "offset": 0,
        "page_size": 8,
        "page_count": 1,
        "total_matches": 1,
        "has_older": False,
        "next_offset": 0,
        "retained_epoch": 1,
        "content_revision": 1,
        "persistence": persistence(),
        "entries": [entry],
        "persisted": True,
    }


def unread():
    return {
        "schema": 1,
        "ok": True,
        "cmd": "messages unread",
        "public_unread": 0,
        "dm_unread": 0,
        "muted_dm_unread": 0,
        "dm_thread_count": 1,
        "last_public_read_seq": 10,
        "last_dm_read_seq": 20,
        "newest_public_rx_seq": 10,
        "newest_dm_rx_seq": 20,
        "mark_read_count": 2,
        "dm_threads": [
            {
                "fingerprint": FINGERPRINT,
                "last_read_seq": 20,
                "newest_rx_seq": 20,
                "unread": 0,
                "muted": False,
            }
        ],
        "persisted": True,
    }


def contacts():
    return {
        "schema": 1,
        "ok": True,
        "cmd": "contacts",
        "count": 1,
        "capacity": 32,
        "total_written": 1,
        "dropped_oldest": 0,
        "entries": [
            {
                "seq": 30,
                "created_ms": 100,
                "updated_ms": 100,
                "fingerprint": FINGERPRINT,
                "public_key": PUBLIC_KEY,
                "alias": "Core Canary",
                "heard_name": "",
                "type": "chat",
                "verification_source": "uri_import",
                "verified_at_ms": 100,
                "signed_advert_timestamp": 0,
                "last_heard_ms": 0,
                "canonical": True,
                "can_dm": True,
                "can_admin": False,
                "last_rssi_dbm": -60,
                "last_snr_tenths": 40,
                "path_hash_bytes": 1,
                "path_hops": 0,
                "out_path_known": True,
                "out_path_len": 0,
                "out_path_updated_ms": 100,
                "favorite": False,
                "muted": False,
            }
        ],
        "persisted": True,
    }


def state_capture(nonce: int, uptime: int, reset_reason: str, crash_seq: int):
    rows = [
        ("version", version()),
        ("health", health(nonce, uptime, reset_reason)),
        ("crashlog", crashlog(crash_seq, reset_reason)),
        ("settings get", settings()),
        ("messages public", message_page("messages public")),
        ("messages dm", message_page("messages dm")),
        ("messages unread", unread()),
        ("contacts", contacts()),
    ]
    return {
        "captured_at": "2026-07-18T12:00:00Z",
        "commands": [command_receipt(command, result) for command, result in rows],
    }


def port_sample(present: bool, monotonic: float):
    return {
        "observed_at": "2026-07-18T12:00:00Z",
        "monotonic_sec": monotonic,
        "port": "COM12",
        "present": present,
        "matches": (
            [
                {
                    "device": "COM12",
                    "description": "D1L USB serial",
                    "hwid": "USB VID:PID=1A86:7523 LOCATION=1-2",
                    "serial_number": "D1L-COM12",
                    "vid": 0x1A86,
                    "pid": 0x7523,
                    "location": "1-2",
                    "manufacturer": "wch.cn",
                    "product": "USB Serial",
                }
            ]
            if present
            else []
        ),
    }


def reboot_command():
    return command_receipt(
        "reboot",
        {
            "schema": 1,
            "ok": True,
            "cmd": "reboot",
            "rebooting": True,
            "reset_scope": "system",
            "storage_manager_quiesced": True,
            "retained_worker_quiesced": True,
            "rp2040_bridge_quiesced": True,
            "connectivity_prepare": "ESP_OK",
            "retained_flush": "ESP_OK",
            "route_flush": "ESP_OK",
        },
    )


def boot_lines():
    reset = b"rst:0x3 (RTC_SW_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
    help_row = b'{"schema":1,"ok":true,"cmd":"help"}\r\n'
    return [raw_line(b"ESP-ROM:esp32s3-20210327\r\n"), raw_line(reset), raw_line(help_row)]


def local_canary_result():
    return {
        "schema": 1,
        "ok": True,
        "cmd": "core retained-canary",
        "token": TOKEN,
        "message_text": MESSAGE_TEXT,
        "fingerprint": FINGERPRINT.lower(),
        "public_key": PUBLIC_KEY,
        "public_seq": 10,
        "dm_seq": 20,
        "contact_seq": 30,
        "contact_result": "created",
        "persisted": True,
        "retention": "nvs",
        "backend_mode": "nvs_disabled",
        "synthetic_local": True,
        "retained_flush": "ESP_OK",
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "sd_access": False,
        "rp2040_access": False,
        "formats_sd": False,
        "predecessor_evidence_used": False,
    }


def seed_receipt():
    capture = state_capture(100, 600000, "SW", 10)
    projection, errors, _ = reboot.recompute_state_capture(capture, COMMIT)
    assert not errors
    return {
        "schema": 1,
        "kind": "core_retained_state_seed",
        "mode": "hardware",
        "ok": True,
        "closure_eligible": False,
        "hardware_required": True,
        "physical_observed": True,
        "port": "COM12",
        "baud": 115200,
        "commit": COMMIT,
        "github_actions_run": RUN_ID,
        "workflow_run_attempt": RUN_ATTEMPT,
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "git": SOURCE_GIT,
        "captured_at": "2026-07-18T12:00:00Z",
        "canary": {
            **CANARY_BASE,
            "public_seq": 10,
            "dm_seq": 20,
            "contact_seq": 30,
        },
        "initial_state_capture": copy.deepcopy(capture),
        "local_retained_canary_mutation": command_receipt(
            f"core retained-canary {TOKEN}",
            local_canary_result(),
        ),
        "settings_canary_mutation": command_receipt(
            f"settings set name D1L-Core-{COMMIT[:7]}",
            {
                "schema": 1,
                "ok": True,
                "cmd": "settings set name",
                "persisted": True,
                "node_name": f"D1L-Core-{COMMIT[:7]}",
            },
        ),
        "state_capture": capture,
        "projection_sha256": reboot.projection_sha256(projection),
        "checks": {},
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "sd_access": False,
        "rp2040_access": False,
        "formats_sd": False,
        "predecessor_evidence_used": False,
    }


def write_json(path: Path, value: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="ascii")


def row(path: Path, root: Path):
    return {
        "path": path.relative_to(root).as_posix(),
        "size": path.stat().st_size,
        "sha256": reboot.sha256_file(path),
    }


def flash_receipt(root: Path, seed: dict):
    results = [
        version(),
        health(110, 10000, "SW"),
        settings(),
        message_page("messages public"),
        message_page("messages dm"),
        contacts(),
    ]
    snapshot_projection = {"canary": "bound"}
    snapshots = []
    for phase in ("pre_flash", "post_flash"):
        path = root / f"{phase}.json"
        write_json(
            path,
            {
                "schema": 1,
                "kind": "core_retained_state_snapshot",
                "mode": "hardware",
                "phase": phase,
                "port": "COM12",
                "expected_firmware_commit": COMMIT,
                "results": results,
                "projection": snapshot_projection,
                "projection_sha256": reboot.projection_sha256(snapshot_projection),
            },
        )
        snapshots.append(row(path, root))
    raw = root / "flash.log"
    raw.write_bytes(b"esptool write-flash success\n")
    return {
        "schema": 1,
        "kind": "esp32_flash",
        "mode": "hardware",
        "scope": "core-retained-reflash-only",
        "flash_phase": "retained-reflash",
        "ok": True,
        "closure_eligible": True,
        "hardware_required": True,
        "physical_observed": True,
        "port": "COM12",
        "commit": COMMIT,
        "github_actions_run": RUN_ID,
        "workflow_run_attempt": RUN_ATTEMPT,
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "firmware_identity_ok": True,
        "runner_source_identity_ok": True,
        "retained_state_preserved": True,
        "git": SOURCE_GIT,
        "command": [
            "python",
            "-m",
            "esptool",
            "--port",
            "COM12",
            "write-flash",
            "0x0",
            "bootloader.bin",
        ],
        "raw_flash_log": row(raw, root),
        "retained_state_before": snapshots[0],
        "retained_state_after": snapshots[1],
        "erase_flash": False,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "sd_access": False,
        "rp2040_access": False,
        "formats_sd": False,
        "legacy_suite_ran": False,
    }


def cycle_receipt(
    cycle_type: str,
    ordinal: int,
    matrix_id: str,
    seed_hash: str,
    flash_hash: str,
    previous_hash: str,
):
    reset_reason = "SW" if cycle_type == "software" else "POWERON"
    report = reboot._cycle_base(
        matrix_id=matrix_id,
        cycle_type=cycle_type,
        ordinal=ordinal,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        seed_sha256=seed_hash,
        flash_sha256=flash_hash,
        previous_sha256=previous_hash,
    )
    report["pre"] = state_capture(1000 + ordinal, 600000, "SW", 100 + ordinal)
    report["post"] = state_capture(
        2000 + ordinal, 10000, reset_reason, 101 + ordinal
    )
    if cycle_type == "software":
        report["action"] = {
            "kind": "software_reboot",
            "reboot_command": reboot_command(),
            "boot_raw_lines": boot_lines(),
            "boot_analysis": {},
            "port_disappear_required": False,
            "port_before": port_sample(True, 0.0),
            "port_after": port_sample(True, 1.0),
        }
    else:
        report["action"] = {
            "kind": "operator_controlled_cold_power_cycle",
            "operator_interactive": True,
            "minimum_power_off_sec": 2.0,
            "observed_power_off_sec": 2.1,
            "port_before": port_sample(True, 0.0),
            "disappear_samples": [
                port_sample(True, 0.1),
                port_sample(False, 0.2),
            ],
            "power_off_samples": [
                port_sample(False, 0.2),
                port_sample(False, 2.3),
            ],
            "reappear_samples": [
                port_sample(False, 2.4),
                port_sample(True, 2.5),
            ],
            "port_after": port_sample(True, 2.5),
        }
    report["checks"] = {}
    report["ok"] = True
    report["ended_at"] = "2026-07-18T12:01:00Z"
    return report


def valid_matrix_tree(tmp_path: Path):
    seed = seed_receipt()
    seed_path = tmp_path / "seed.json"
    write_json(seed_path, seed)
    seed_row = row(seed_path, tmp_path)
    flash = flash_receipt(tmp_path, seed)
    flash_path = tmp_path / "flash.json"
    write_json(flash_path, flash)
    flash_row = row(flash_path, tmp_path)
    matrix_id = "1" * 32
    previous = flash_row["sha256"]
    cycle_rows = []
    for cycle_type, count in (
        ("software", reboot.SOFTWARE_CYCLE_COUNT),
        ("cold", reboot.COLD_CYCLE_COUNT),
    ):
        for ordinal in range(1, count + 1):
            receipt = cycle_receipt(
                cycle_type,
                ordinal,
                matrix_id,
                seed_row["sha256"],
                flash_row["sha256"],
                previous,
            )
            path = tmp_path / "cycles" / f"{cycle_type}_{ordinal}.json"
            write_json(path, receipt)
            cycle_row = row(path, tmp_path)
            cycle_rows.append(cycle_row)
            previous = cycle_row["sha256"]
    live = state_capture(3000, 10000, "SW", 200)
    live_projection, errors, _ = reboot.recompute_state_capture(live, COMMIT)
    assert not errors
    matrix = {
        "schema": 1,
        "kind": "core_reboot_persistence_matrix",
        "mode": "hardware",
        "ok": True,
        "closure_eligible": True,
        "hardware_required": True,
        "physical_observed": True,
        "matrix_id": matrix_id,
        "port": "COM12",
        "baud": 115200,
        "commit": COMMIT,
        "github_actions_run": RUN_ID,
        "workflow_run_attempt": RUN_ATTEMPT,
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "claim": reboot.CLAIM,
        "cross_version_migration_proven": False,
        "predecessor_evidence_used": False,
        "git": SOURCE_GIT,
        "seed_receipt": seed_row,
        "closing_flash_receipt": flash_row,
        "post_reinstall_live_capture": live,
        "post_reinstall_projection_sha256": reboot.projection_sha256(
            live_projection
        ),
        "software_cycle_count": 5,
        "cold_cycle_count": 3,
        "cycle_receipts": cycle_rows,
        "all_child_receipts_unique": True,
        "hash_chain_tail": previous,
        "started_at": "2026-07-18T12:00:00Z",
        "ended_at": "2026-07-18T13:00:00Z",
        "public_rf_tx": False,
        "formats_sd": False,
    }
    matrix_path = tmp_path / "matrix.json"
    write_json(matrix_path, matrix)
    return matrix_path, matrix


@pytest.mark.parametrize("port", ["COM8", "COM11", "COM16", "COM29", "COM13"])
def test_only_com12_is_admitted(port):
    with pytest.raises(ValueError, match="requires COM12"):
        reboot.enforce_core_port(port)


def test_ed25519_seed_derivation_matches_rfc8032_vector():
    seed = bytes.fromhex(
        "9d61b19deffd5a60ba844af492ec2cc4"
        "4449c5697b326919703bac031cae7f60"
    )

    public_key = reboot.ed25519_public_key_from_seed(seed)

    assert public_key.hex() == (
        "d75a980182b10ab7d54bfed3c964073a"
        "0ee172f3daa62325af021a68f707511a"
    )


def test_candidate_canary_is_exact_run_and_attempt_bound():
    canary = reboot.candidate_canary(COMMIT, RUN_ID, RUN_ATTEMPT)

    assert canary == CANARY_BASE
    assert canary["token"].startswith("core-")
    assert len(canary["token"]) == 29
    assert canary["contact_fingerprint"] == PUBLIC_KEY[:16].upper()
    assert (
        reboot.candidate_canary(COMMIT, RUN_ID, "2")["token"]
        != canary["token"]
    )


def test_source_guard_rejects_dirty_or_wrong_checkout(tmp_path):
    with pytest.raises(ValueError, match="exact clean candidate"):
        reboot.exact_source_git(
            tmp_path,
            COMMIT,
            metadata={**SOURCE_GIT, "dirty": True, "dirty_entries": ["M file"]},
        )
    with pytest.raises(ValueError, match="exact clean candidate"):
        reboot.exact_source_git(
            tmp_path,
            COMMIT,
            metadata={**SOURCE_GIT, "commit": "b" * 40},
        )


def test_state_projection_is_recomputed_from_raw_and_contains_canaries():
    capture = state_capture(1, 10000, "SW", 10)
    projection, errors, raw = reboot.recompute_state_capture(capture, COMMIT)

    assert errors == []
    assert projection is not None
    assert raw["health"]["boot_nonce"] == 1
    ok, canary_errors = reboot.canary_check(
        projection,
        canary={
            **CANARY_BASE,
            "public_seq": 10,
            "dm_seq": 20,
            "contact_seq": 30,
        },
    )
    assert ok is True
    assert canary_errors == []


def test_raw_command_tampering_is_rejected():
    row = command_receipt("version", version())
    row["result"]["build_commit"] = "b" * 40

    result, errors = reboot.recompute_raw_command(row)

    assert result["build_commit"] == COMMIT
    assert "parsed result does not match raw result" in errors[0]


def test_contact_snapshot_must_not_be_truncated():
    capture = state_capture(1, 10000, "SW", 10)
    contact = capture["commands"][-1]["result"]
    contact["count"] = 2
    capture["commands"][-1] = command_receipt("contacts", contact)

    projection, errors, _ = reboot.recompute_state_capture(capture, COMMIT)

    assert projection is not None
    assert any("snapshot is truncated" in error for error in errors)


def test_state_preservation_rejects_removed_message_and_read_state_change():
    baseline, errors, _ = reboot.recompute_state_capture(
        state_capture(1, 10000, "SW", 10), COMMIT
    )
    assert not errors
    observed = copy.deepcopy(baseline)
    observed["direct_messages"]["entries"] = []
    observed["read_state"]["last_dm_read_seq"] = 0

    ok, reasons = reboot.state_preserved(baseline, observed)

    assert ok is False
    assert "direct_messages lost retained rows" in reasons
    assert "message read-state changed or was lost" in reasons


def test_software_cycle_is_recomputed_from_raw_not_checks():
    baseline, errors, _ = reboot.recompute_state_capture(
        state_capture(1, 10000, "SW", 10), COMMIT
    )
    assert not errors
    receipt = cycle_receipt(
        "software", 1, "1" * 32, "2" * 64, "3" * 64, "3" * 64
    )
    receipt["checks"] = {"all_green": False}

    ok, reasons = reboot.recompute_cycle(
        receipt,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        matrix_id="1" * 32,
        baseline=baseline,
    )

    assert ok is True
    assert reasons == []


def test_software_cycle_rejects_raw_poweron_banner():
    baseline, errors, _ = reboot.recompute_state_capture(
        state_capture(1, 10000, "SW", 10), COMMIT
    )
    assert not errors
    receipt = cycle_receipt(
        "software", 1, "1" * 32, "2" * 64, "3" * 64, "3" * 64
    )
    receipt["action"]["boot_raw_lines"][1] = raw_line(
        b"rst:0x1 (POWERON_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
    )

    ok, reasons = reboot.recompute_cycle(
        receipt,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        matrix_id="1" * 32,
        baseline=baseline,
    )

    assert ok is False
    assert "software reboot raw boot/reset evidence failed" in reasons


def test_cold_cycle_duration_is_derived_from_raw_monotonic_samples():
    action = cycle_receipt(
        "cold", 1, "1" * 32, "2" * 64, "3" * 64, "3" * 64
    )["action"]
    action["observed_power_off_sec"] = 99.0

    ok, errors = reboot._port_action_recomputed(action, cycle_type="cold")

    assert ok is False
    assert errors == ["cold cycle raw port transition evidence failed"]


def test_valid_matrix_recomputes_all_eight_unique_chained_children(tmp_path):
    matrix_path, _ = valid_matrix_tree(tmp_path)

    ok, errors, _ = reboot.validate_core_reboot_persistence_receipt(
        matrix_path,
        root=tmp_path,
        expected_commit=COMMIT,
        expected_run_id=RUN_ID,
        expected_run_attempt=RUN_ATTEMPT,
    )

    assert ok is True, errors
    assert errors == []


def test_matrix_validator_rejects_zero_actions_run_id(tmp_path):
    matrix_path, _ = valid_matrix_tree(tmp_path)

    ok, errors, _ = reboot.validate_core_reboot_persistence_receipt(
        matrix_path,
        root=tmp_path,
        expected_commit=COMMIT,
        expected_run_id="0",
        expected_run_attempt=RUN_ATTEMPT,
    )

    assert ok is False
    assert errors == ["validator expected identity is invalid"]


def test_matrix_rejects_copied_cycle_receipt(tmp_path):
    matrix_path, matrix = valid_matrix_tree(tmp_path)
    matrix["cycle_receipts"][1] = copy.deepcopy(matrix["cycle_receipts"][0])
    write_json(matrix_path, matrix)

    ok, errors, _ = reboot.validate_core_reboot_persistence_receipt(
        matrix_path,
        root=tmp_path,
        expected_commit=COMMIT,
        expected_run_id=RUN_ID,
        expected_run_attempt=RUN_ATTEMPT,
    )

    assert ok is False
    assert any("duplicate cycle path" in error for error in errors)
    assert any("duplicate cycle content/hash" in error for error in errors)


def test_matrix_rejects_cross_version_migration_claim(tmp_path):
    matrix_path, matrix = valid_matrix_tree(tmp_path)
    matrix["claim"] = "upgrade_migration"
    matrix["cross_version_migration_proven"] = True
    write_json(matrix_path, matrix)

    ok, errors, _ = reboot.validate_core_reboot_persistence_receipt(
        matrix_path,
        root=tmp_path,
        expected_commit=COMMIT,
        expected_run_id=RUN_ID,
        expected_run_attempt=RUN_ATTEMPT,
    )

    assert ok is False
    assert "matrix: claim mismatch" in errors
    assert "matrix: cross_version_migration_proven mismatch" in errors


def test_flash_validator_rejects_bootstrap_or_erasing_flash(tmp_path):
    seed = seed_receipt()
    flash = flash_receipt(tmp_path, seed)
    flash["flash_phase"] = "bootstrap"
    flash["command"].insert(-2, "erase-flash")

    errors = reboot.validate_closing_flash_receipt(
        flash,
        root=tmp_path,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        canary=seed["canary"],
    )

    assert "flash: flash_phase mismatch" in errors
    assert any("not exact non-erasing" in error for error in errors)


def test_seed_validator_requires_raw_settings_canary_mutation():
    seed = seed_receipt()
    seed["settings_canary_mutation"]["result"]["persisted"] = False

    projection, errors = reboot.validate_seed_receipt(
        seed,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )

    assert projection is not None
    assert any("parsed result does not match raw result" in error for error in errors)


def test_seed_validator_requires_raw_candidate_local_canary_mutation():
    seed = seed_receipt()
    seed["local_retained_canary_mutation"]["result"]["sd_access"] = True

    projection, errors = reboot.validate_seed_receipt(
        seed,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )

    assert projection is not None
    assert any("parsed result does not match raw result" in error for error in errors)


def test_seed_producer_creates_run_bound_local_canary_before_settings(
    tmp_path, monkeypatch
):
    class SerialContext:
        def __enter__(self):
            return self

        def __exit__(self, *_args):
            return False

        def reset_input_buffer(self):
            return None

    commands = []
    captures = iter(
        [
            state_capture(1, 10000, "SW", 10),
            state_capture(1, 11000, "SW", 10),
        ]
    )
    monkeypatch.setattr(
        reboot, "_open_serial", lambda _module, _timeout: SerialContext()
    )
    monkeypatch.setattr(
        reboot,
        "capture_state",
        lambda *_args, **_kwargs: next(captures),
    )

    def fake_command(_serial, command, *_args, **_kwargs):
        commands.append(command)
        if command.startswith("core retained-canary "):
            return command_receipt(command, local_canary_result())
        return command_receipt(
            command,
            {
                "schema": 1,
                "ok": True,
                "cmd": "settings set name",
                "persisted": True,
                "node_name": CANARY_BASE["settings_node_name"],
            },
        )

    monkeypatch.setattr(reboot, "read_raw_command", fake_command)
    out = tmp_path / "seed.json"

    report = reboot.seed_retained_state(
        root=tmp_path,
        out=out,
        serial_module=object(),
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        timeout=1.0,
        source_git=SOURCE_GIT,
    )

    assert report["ok"] is True, report["checks"]["errors"]
    assert commands == [
        f"core retained-canary {TOKEN}",
        f"settings set name {CANARY_BASE['settings_node_name']}",
    ]
    assert json.loads(out.read_text(encoding="ascii"))["canary"] == {
        **CANARY_BASE,
        "public_seq": 10,
        "dm_seq": 20,
        "contact_seq": 30,
    }


def test_seed_producer_does_not_mutate_wrong_candidate(tmp_path, monkeypatch):
    class SerialContext:
        def __enter__(self):
            return self

        def __exit__(self, *_args):
            return False

    wrong = state_capture(1, 10000, "SW", 10)
    wrong_version = version()
    wrong_version["build_commit"] = "b" * 40
    wrong["commands"][0] = command_receipt("version", wrong_version)
    monkeypatch.setattr(
        reboot, "_open_serial", lambda _module, _timeout: SerialContext()
    )
    monkeypatch.setattr(
        reboot,
        "capture_state",
        lambda *_args, **_kwargs: wrong,
    )

    def unexpected_mutation(*_args, **_kwargs):
        raise AssertionError("wrong candidate must not receive a mutation")

    monkeypatch.setattr(reboot, "read_raw_command", unexpected_mutation)

    report = reboot.seed_retained_state(
        root=tmp_path,
        out=tmp_path / "wrong-seed.json",
        serial_module=object(),
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        timeout=1.0,
        source_git=SOURCE_GIT,
    )

    assert report["ok"] is False
    assert report["local_retained_canary_mutation"] is None
    assert report["settings_canary_mutation"] is None


def test_output_writer_refuses_overwrite(tmp_path):
    path = tmp_path / "receipt.json"
    reboot.write_json_once(path, tmp_path, {"schema": 1})

    with pytest.raises(ValueError, match="refusing to overwrite"):
        reboot.write_json_once(path, tmp_path, {"schema": 1})
