import argparse
import json
from pathlib import Path

import pytest

from scripts import core_release_gate_audit_d1l as audit
from scripts import core_smoke_d1l
from scripts import release_gate_audit_d1l as full_audit
from scripts import soak_d1l as soak_runner


COMMIT = "a" * 40
RUN_ID = "123456789"
RUN_ATTEMPT = "1"


def write_json(path: Path, value: dict) -> Path:
    path.write_text(json.dumps(value), encoding="utf-8")
    return path


def passing_core_smoke() -> dict:
    probes = []
    for probe in core_smoke_d1l.mutation_probe_plan("disabled"):
        probes.append(
            {
                **probe,
                "result": {
                    "ok": False,
                    "cmd": probe["command"],
                    "code": "ESP_ERR_NOT_SUPPORTED",
                    "release_profile": "core_1_0",
                    "feature": probe["feature"],
                },
            }
        )
    def health(nonce: int, *, reset_reason: str = "SW") -> dict:
        return {
            "schema": 1,
            "cmd": "health",
            "ok": True,
            "build_commit": COMMIT,
            "release_profile": "core_1_0",
            "sd_history_mode": "disabled",
            "board_ready": True,
            "ui_ready": True,
            "boot_nonce": nonce,
            "reset_reason": reset_reason,
        }

    def settings(name: str) -> dict:
        return {
            "schema": 1,
            "cmd": "settings get",
            "ok": True,
            "node_name": name,
            "path_hash_bytes": 2,
        }

    def reboot() -> dict:
        return {
            "schema": 1,
            "cmd": "reboot",
            "ok": True,
            "rebooting": True,
            "reset_scope": "system",
            "storage_manager_quiesced": True,
            "retained_worker_quiesced": True,
            "rp2040_bridge_quiesced": True,
            "connectivity_prepare": "ESP_OK",
            "retained_flush": "ESP_OK",
            "route_flush": "ESP_OK",
        }

    storage = {
        "schema": 1,
        "cmd": "storage status",
        "ok": True,
        "manager": {
            "running": False,
            "force_nvs": True,
            "state": "READY_NVS",
        },
        "data_enabled": False,
        "data_backend": "nvs",
        "message_store_backend": "nvs",
        "dm_store_backend": "nvs",
        "packet_log_backend": "nvs",
        "route_store_backend": "nvs",
        "map_tile_backend": "unavailable",
        "map_tile_cache_ready": False,
        "map_tile_download_supported": False,
        "sd": {
            "rp2040_bridge_ready": False,
            "rp2040_protocol_supported": False,
            "mounted": False,
            "data_root_ready": False,
            "file_ops": False,
            "atomic_rename": False,
        },
        "stores": {
            "settings": "nvs",
            "identity": "nvs",
            "messages": "nvs",
            "dm": "nvs",
            "packets": "nvs",
            "routes": "nvs",
            "contacts": "nvs",
            "read_state": "nvs",
            "crashlog": "nvs",
            "map_tiles": "unavailable",
            "exports": "serial",
        },
        "fallback": "nvs",
        "retained_nvs": {
            "marker_ready": True,
            "markers_complete": True,
            "anchor_ready": True,
            "sentinel_ready": True,
            "ready": True,
        },
    }
    version = {
        "schema": 1,
        "cmd": "version",
        "ok": True,
        "build_commit": COMMIT,
        "idf": "v5.5.4",
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
    }
    supported_results = []
    for command in core_smoke_d1l.CORE_SMOKE_COMMANDS:
        cmd = audit.expected_command_name(command)
        if cmd == "storage status":
            supported_results.append(storage)
        elif cmd == "crashlog":
            supported_results.append(
                {
                    "schema": 1,
                    "cmd": "crashlog",
                    "ok": True,
                    "entries": [],
                }
            )
        else:
            supported_results.append(
                {"schema": 1, "cmd": cmd, "ok": True}
            )
    original = settings("DeskOS")
    changed = settings("DeskOS-C")
    persistence_steps = [
        {"command": "settings get", "result": original},
        {
            "command": "settings set name DeskOS-C",
            "result": {
                "schema": 1,
                "cmd": "settings set name",
                "ok": True,
            },
        },
        {"command": "settings get", "result": changed},
        {"command": "health", "result": health(10)},
        {"command": "reboot", "result": reboot()},
        {"command": "health", "result": health(11)},
        {"command": "settings get", "result": changed},
        {
            "command": "settings set name DeskOS",
            "result": {
                "schema": 1,
                "cmd": "settings set name",
                "ok": True,
            },
        },
        {"command": "settings get", "result": original},
        {"command": "health", "result": health(11)},
        {"command": "reboot", "result": reboot()},
        {"command": "health", "result": health(12)},
        {"command": "settings get", "result": original},
    ]
    return {
        "kind": "core_smoke",
        "mode": "hardware",
        "ok": True,
        "closure_eligible": True,
        "hardware_required": True,
        "physical_observed": True,
        "port": "COM12",
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "expected_firmware_commit": COMMIT,
        "device_build_commit": COMMIT,
        "github_actions_run": RUN_ID,
        "workflow_run_attempt": RUN_ATTEMPT,
        "firmware_identity_required": True,
        "firmware_identity_ok": True,
        "git": {"commit": COMMIT, "dirty": False, "dirty_entries": []},
        "checks": {
            "exact_candidate": True,
            "esp_idf_v5_5_4": True,
            "core_profile": True,
            "exact_sd_history_mode": True,
            "supported_commands_pass": True,
            "disabled_sd_nvs_authoritative": True,
            "pre_ui_crashlog_clean": True,
            "unavailable_mutations_rejected": True,
            "health_ready": True,
            "persistence_pass": True,
            "no_public_rf": True,
            "no_sd_format": True,
        },
        "supported_commands_executed": list(
            core_smoke_d1l.CORE_SMOKE_COMMANDS
        ),
        "unavailable_mutation_probes": probes,
        "persistence": {
            "schema": 1,
            "kind": "core_settings_persistence",
            "ok": True,
            "mutation_started": True,
            "reboot_count": 2,
            "first_reboot_proven": True,
            "persisted_after_reboot": True,
            "original_restored": True,
            "steps": persistence_steps,
        },
        "public_rf_tx": False,
        "formats_sd": False,
        "results": [version, health(9, reset_reason="POWERON")]
        + supported_results
        + [health(12)],
    }


def test_core_smoke_gate_passes_exact_fixture_and_rejects_dry_or_wrong_sha(
    tmp_path,
):
    path = write_json(tmp_path / "smoke.json", passing_core_smoke())
    assert audit.core_smoke_gate(
        path, tmp_path, COMMIT, "disabled", RUN_ID, RUN_ATTEMPT
    ).ok

    failed = passing_core_smoke()
    failed["dry_run"] = True
    write_json(path, failed)
    assert not audit.core_smoke_gate(
        path, tmp_path, COMMIT, "disabled", RUN_ID, RUN_ATTEMPT
    ).ok

    failed = passing_core_smoke()
    failed["unavailable_mutation_probes"] = [
        probe
        for probe in failed["unavailable_mutation_probes"]
        if probe["command"] != "packets clear"
    ]
    write_json(path, failed)
    assert not audit.core_smoke_gate(
        path, tmp_path, COMMIT, "disabled", RUN_ID, RUN_ATTEMPT
    ).ok

    failed = passing_core_smoke()
    failed["device_build_commit"] = "b" * 40
    write_json(path, failed)
    assert not audit.core_smoke_gate(
        path, tmp_path, COMMIT, "disabled", RUN_ID, RUN_ATTEMPT
    ).ok


def passing_soak(*, active: bool) -> dict:
    duration = 3600 if active else 1800
    interval = 300
    samples = []
    for index in range(duration // interval + 1):
        tx_packets = index * (1 if active else 0)
        rx_packets = index if active else 0
        storage = {
            "schema": 1,
            "cmd": "storage status",
            "ok": True,
            "manager": {
                "running": False,
                "force_nvs": True,
                "state": "READY_NVS",
            },
            "data_enabled": False,
            "data_backend": "nvs",
            "message_store_backend": "nvs",
            "dm_store_backend": "nvs",
            "packet_log_backend": "nvs",
            "route_store_backend": "nvs",
            "map_tile_backend": "unavailable",
            "map_tile_cache_ready": False,
            "map_tile_download_supported": False,
            "sd": {
                "state": "disabled",
                "filesystem": "unavailable",
                "interface": "disabled",
                "rp2040_bridge_ready": False,
                "rp2040_protocol_supported": False,
                "mounted": False,
                "data_root_ready": False,
                "file_ops": False,
                "atomic_rename": False,
                "file_line_max": 0,
                "file_chunk_max": 0,
                "path_max": 0,
                "status_stale": False,
                "presence_stale": False,
                "refresh_failures": 0,
            },
            "stores": {
                "settings": "nvs",
                "identity": "nvs",
                "messages": "nvs",
                "dm": "nvs",
                "packets": "nvs",
                "routes": "nvs",
                "contacts": "nvs",
                "read_state": "nvs",
                "crashlog": "nvs",
                "map_tiles": "unavailable",
                "exports": "serial",
            },
            "fallback": "nvs",
            "retained_nvs": {
                "marker_ready": True,
                "markers_complete": True,
                "anchor_ready": True,
                "sentinel_ready": True,
                "ready": True,
            },
        }
        samples.append(
            {
                "label": "start" if index == 0 else f"sample-{index}",
                "elapsed_sec": index * interval,
                "aborted_after_timeout": None,
                "results": [
                    {
                        "schema": 1,
                        "cmd": "health",
                        "ok": True,
                        "build_commit": COMMIT,
                        "release_profile": "core_1_0",
                        "sd_history_mode": "disabled",
                        "board_ready": True,
                        "ui_ready": True,
                        "boot_nonce": 99,
                        "uptime_ms": 1000 + index * interval * 1000,
                        "heap_free": 500000 - index * 100,
                        "psram_free": 1000000 - index * 100,
                        "heap_min_free": 450000,
                        "psram_min_free": 900000,
                        "current_task_stack_free_words": 4096,
                        "ui_task_stack_free_words": 4096,
                        "retained_task_stack_free_bytes": 8192,
                        "lvgl_used_pct": 20,
                    },
                    {
                        "schema": 1,
                        "cmd": "mesh status",
                        "ok": True,
                        "state": "ready",
                        "identity_ready": True,
                        "radio_ready": True,
                        "rx_packets": rx_packets,
                        "tx_packets": tx_packets,
                        "runtime": {
                            "queue_drops": 0,
                            "callback_event_drops": 0,
                            "command_queue_saturation": 0,
                            "priority_queue_saturation": 0,
                            "command_queue_depth": 0,
                            "priority_queue_depth": 0,
                            "event_queue_depth": 0,
                        },
                    },
                    {
                        "schema": 1,
                        "cmd": "signal",
                        "ok": True,
                        "sample_count": index + 1,
                    },
                    {
                        "schema": 1,
                        "cmd": "messages unread",
                        "ok": True,
                    },
                    {
                        "schema": 1,
                        "cmd": "packets",
                        "ok": True,
                        "total_written": tx_packets + rx_packets,
                    },
                    {
                        "schema": 1,
                        "cmd": "crashlog",
                        "ok": True,
                        "entries": [],
                        "count": 0,
                        "total_written": 0,
                    },
                    storage,
                ],
            }
        )
    active_events = (
        [
            {
                "elapsed_sec": 1 + index * 600,
                "command": (
                    "mesh send dm 0123456789ABCDEF core_soak"
                ),
                "fingerprint": "0123456789ABCDEF",
                "text": "core_soak",
                "result": {
                    "schema": 1,
                    "cmd": "mesh send dm",
                    "ok": True,
                },
            }
            for index in range(6)
        ]
        if active
        else []
    )
    summary = soak_runner.summarize_soak(
        samples=samples,
        active_events=active_events,
        require_rx_delta=active,
        min_rx_delta=1,
        min_tx_delta=6 if active else 0,
        sample_storage=True,
        sd_file_canary=False,
        allow_sd_unavailable=True,
    )
    return {
        "schema": 1,
        "mode": "hardware",
        "ok": True,
        "closure_eligible": True,
        "physical_observed": True,
        "port": "COM12",
        "expected_firmware_commit": COMMIT,
        "device_build_commit": COMMIT,
        "github_actions_run": RUN_ID,
        "workflow_run_attempt": RUN_ATTEMPT,
        "firmware_identity_required": True,
        "firmware_identity_ok": True,
        "git": {"commit": COMMIT, "dirty": False, "dirty_entries": []},
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "device_idf_version": "v5.5.4",
        "device_release_profile": "core_1_0",
        "device_sd_history_mode": "disabled",
        "started_at": "2026-07-18T00:00:00Z",
        "ended_at": (
            "2026-07-18T01:00:00Z"
            if active
            else "2026-07-18T00:30:00Z"
        ),
        "duration_sec": duration,
        "sample_interval_sec": interval,
        "preflight_commands": ["version"],
        "preflight_failure": None,
        "version_preflight": {
            "schema": 1,
            "cmd": "version",
            "ok": True,
            "build_commit": COMMIT,
            "idf": "v5.5.4",
            "release_profile": "core_1_0",
            "sd_history_mode": "disabled",
        },
        "setup_events": [
            {
                "elapsed_sec": 0.0,
                "cmd": "version",
                "result": {
                    "schema": 1,
                    "cmd": "version",
                    "ok": True,
                    "build_commit": COMMIT,
                    "idf": "v5.5.4",
                    "release_profile": "core_1_0",
                    "sd_history_mode": "disabled",
                },
            }
        ],
        "commands": list(soak_runner.SOAK_COMMANDS)
        + [soak_runner.STORAGE_STATUS_COMMAND],
        "sample_storage": True,
        "sd_file_canary": False,
        "active_dm_fingerprint": "0123456789ABCDEF" if active else None,
        "active_dm_text": "core_soak" if active else None,
        "active_command": (
            "mesh send dm 0123456789ABCDEF core_soak" if active else None
        ),
        "dm_rf_tx": active,
        "active_events": active_events,
        "allow_sd_unavailable": True,
        "require_rx_delta": active,
        "min_rx_delta": 1,
        "min_tx_delta": 6 if active else 0,
        "clear_crashlog_before_start": False,
        "public_rf_tx": False,
        "formats_sd": False,
        "aborted_after_timeout": None,
        "samples": samples,
        "summary": summary,
    }


def test_core_soak_requires_full_duration_exact_identity_and_nvs():
    active = passing_soak(active=True)
    idle = passing_soak(active=False)

    assert audit.soak_artifact_ok(
        active,
        commit=COMMIT,
        sd_history_mode="disabled",
        active=True,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )
    assert audit.soak_artifact_ok(
        idle,
        commit=COMMIT,
        sd_history_mode="disabled",
        active=False,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )

    active["duration_sec"] = 3599
    assert not audit.soak_artifact_ok(
        active,
        commit=COMMIT,
        sd_history_mode="disabled",
        active=True,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )
    active = passing_soak(active=True)
    active["summary"]["storage_data_backends"] = ["sd"]
    assert not audit.soak_artifact_ok(
        active,
        commit=COMMIT,
        sd_history_mode="disabled",
        active=True,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )
    active = passing_soak(active=True)
    active["samples"][-1]["results"][1]["runtime"][
        "command_queue_saturation"
    ] = 1
    assert not audit.soak_artifact_ok(
        active,
        commit=COMMIT,
        sd_history_mode="disabled",
        active=True,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )


def test_active_soak_peer_flow_recomputes_raw_listener_sidecars(
    tmp_path: Path,
):
    peer_public_key = "0123456789abcdef" + "22" * 24
    d1l_public_key = "abcdef012345" + "33" * 26
    fingerprint = peer_public_key[:16].upper()

    def status(
        *,
        rx: int,
        tx: int,
        reply: int,
        ack_miss: int,
        rx_at: str,
        tx_at: str,
    ) -> dict:
        return {
            "run_id": "listener-run-1",
            "service": "openclaw-radio-listener",
            "serial": {
                "port": "COM15",
                "mesh_connected": True,
                "public_key": peer_public_key,
            },
            "mesh": {
                "last_rx_sender": d1l_public_key[:12],
                "last_rx_kind": "dm",
                "last_tx_kind": "dm",
                "last_rx_at": rx_at,
                "last_tx_at": tx_at,
            },
            "counters": {
                "rx_dm_total": rx,
                "tx_dm_total": tx,
                "local_fast_reply_total": reply,
                "tx_dm_ack_miss_total": ack_miss,
            },
        }

    before = status(
        rx=10,
        tx=20,
        reply=30,
        ack_miss=1,
        rx_at="before-rx",
        tx_at="before-tx",
    )
    after = status(
        rx=16,
        tx=26,
        reply=36,
        ack_miss=1,
        rx_at="after-rx",
        tx_at="after-tx",
    )
    before_path = write_json(tmp_path / "before.json", before)
    after_path = write_json(tmp_path / "after.json", after)
    source_path = str(
        audit.rf_acceptance.RADIO_LISTENER_STATUS_PATH.resolve()
    )

    def row(path: Path) -> dict:
        return {
            "path": path.relative_to(tmp_path).as_posix(),
            "size": path.stat().st_size,
            "sha256": audit.sha256_file(path),
            "source_path": source_path,
        }

    data = passing_soak(active=True)
    data.update(
        {
            "active_dm_text": "core_soak_test",
            "active_interval_sec": 600,
            "controlled_peer_before": (
                audit.rf_acceptance.status_snapshot(before)
            ),
            "controlled_peer_after": (
                audit.rf_acceptance.status_snapshot(after)
            ),
            "controlled_peer_before_receipt": row(before_path),
            "controlled_peer_after_receipt": row(after_path),
            "controlled_peer_counter_deltas": {
                "rx_dm_total": 6,
                "tx_dm_total": 6,
                "local_fast_reply_total": 6,
                "tx_dm_ack_miss_total": 0,
            },
            "controlled_peer_successful_send_count": 6,
            "controlled_peer_expected_send_count": 6,
            "controlled_peer_flow_ok": True,
        }
    )
    rf = {
        "target_fingerprint": fingerprint,
        "d1l_public_key": d1l_public_key,
        "controlled_peer_adapter": "openclaw_radio_listener",
        "controlled_peer": {
            "port": "COM15",
            "fingerprint": fingerprint,
            "public_key": peer_public_key,
            "status_path": source_path,
        },
    }

    ok, details = audit.active_soak_peer_flow_ok(data, rf, tmp_path)
    assert ok is True
    assert details["successful_send_count"] == 6
    assert details["expected_send_count"] == 6

    data["controlled_peer_counter_deltas"]["rx_dm_total"] = 5
    assert audit.active_soak_peer_flow_ok(data, rf, tmp_path)[0] is False


def test_reboot_gate_delegates_to_strict_r11_validator(
    tmp_path: Path, monkeypatch
):
    matrix_path = write_json(tmp_path / "matrix.json", {})
    calls = []

    def strict_validator(path, **kwargs):
        calls.append((path, kwargs))
        return (
            True,
            [],
            {
                "software_cycle_count": 5,
                "cold_cycle_count": 3,
                "claim": (
                    "same_exact_candidate_non_erasing_reinstall"
                ),
                "github_actions_run": RUN_ID,
                "workflow_run_attempt": RUN_ATTEMPT,
            },
        )

    monkeypatch.setattr(
        audit,
        "validate_core_reboot_persistence_receipt",
        strict_validator,
    )
    gate = audit.reboot_persistence_gate(
        matrix_path,
        tmp_path,
        COMMIT,
        "disabled",
        RUN_ID,
        RUN_ATTEMPT,
    )

    assert gate.ok is True
    assert calls == [
        (
            matrix_path,
            {
                "root": tmp_path,
                "expected_commit": COMMIT,
                "expected_run_id": RUN_ID,
                "expected_run_attempt": RUN_ATTEMPT,
            },
        )
    ]
    monkeypatch.setattr(
        audit, "validate_core_reboot_persistence_receipt", None
    )
    assert not audit.reboot_persistence_gate(
        matrix_path,
        tmp_path,
        COMMIT,
        "disabled",
        RUN_ID,
        RUN_ATTEMPT,
    ).ok


class DummyImportedGate:
    def __init__(self, ok=True):
        self.ok = ok

    def to_dict(self):
        return {
            "id": "imported",
            "severity": "P0",
            "ok": self.ok,
            "title": "imported",
            "evidence": [],
            "details": {},
        }


def audit_args(tmp_path: Path) -> argparse.Namespace:
    return argparse.Namespace(
        root=str(tmp_path),
        dry_run=False,
        github_run_id=RUN_ID,
        github_run_attempt=RUN_ATTEMPT,
        github_run_dir=str(tmp_path / "github"),
        commit=COMMIT,
        d1l_port="COM12",
        sd_history_mode="disabled",
        hardware_dir=str(tmp_path / "hardware"),
        soak_dir=str(tmp_path / "soak"),
        actions_run_receipt=None,
        core_smoke=None,
        core_ui=None,
        manual_review=None,
        reboot_receipt=None,
        rf_receipt=None,
        active_soak=None,
        idle_soak=None,
        sd_receipt=None,
        install_review=None,
        defect_receipt=None,
        out=None,
    )


def patch_all_gates(monkeypatch, tmp_path: Path, *, one_failure=False):
    package = tmp_path / "package"
    package.mkdir()
    (package / "manifest.json").write_text("{}", encoding="ascii")
    monkeypatch.setattr(audit, "find_release_package", lambda _root: package)
    monkeypatch.setattr(
        audit,
        "actions_inventory_gate",
        lambda *_args: audit.CoreGate(
            "inventory", not one_failure, "inventory"
        ),
    )
    for name in (
        "audit_runner_source_gate",
        "actions_run_metadata_gate",
        "core_immutable_source_inputs_gate",
        "core_flash_receipt_gate",
    ):
        monkeypatch.setattr(
            audit,
            name,
            lambda *_args, **_kwargs: audit.CoreGate(
                name, True, name
            ),
        )
    for name in (
        "immutable_release_source_inputs_gate",
        "host_checks_success_gate",
        "checksum_gate",
        "meshcore_conformance_evidence_gate",
        "meshcore_signed_advert_evidence_gate",
        "esp32_flash_receipt_gate",
        "notices_gate",
    ):
        monkeypatch.setattr(
            audit, name, lambda *_args, **_kwargs: DummyImportedGate(True)
        )
    monkeypatch.setattr(
        audit,
        "package_gate",
        lambda *_args: audit.CoreGate("package", True, "package"),
    )
    for name in (
        "core_smoke_gate",
        "core_ui_gate",
        "manual_review_gate",
        "reboot_persistence_gate",
        "rf_gate",
        "sd_decision_gate",
        "soak_gate",
        "install_review_gate",
        "defect_gate",
    ):
        monkeypatch.setattr(
            audit,
            name,
            lambda *_args, **_kwargs: audit.CoreGate(name, True, name),
        )


def test_core_audit_ready_field_is_independent_and_full_remains_false(
    tmp_path, monkeypatch
):
    patch_all_gates(monkeypatch, tmp_path)
    report = audit.build_audit(audit_args(tmp_path))

    assert report["core_release_ready"] is True
    assert report["full_feature_release_ready"] is False
    assert report["github_actions_run_attempt"] == int(RUN_ATTEMPT)
    assert report["workflow_run_attempt"] == int(RUN_ATTEMPT)
    assert (
        report["github_actions_run_attempt"]
        == report["workflow_run_attempt"]
    )
    assert "ready_for_public_release" not in report


def test_core_audit_fails_closed_when_one_core_gate_fails(
    tmp_path, monkeypatch
):
    patch_all_gates(monkeypatch, tmp_path, one_failure=True)
    report = audit.build_audit(audit_args(tmp_path))

    assert report["core_release_ready"] is False
    assert report["full_feature_release_ready"] is False
    assert report["p0_failed_count"] == 1


def test_core_audit_fails_closed_for_red_non_p0_gate(
    tmp_path, monkeypatch
):
    patch_all_gates(monkeypatch, tmp_path)
    monkeypatch.setattr(
        audit,
        "actions_inventory_gate",
        lambda *_args: audit.CoreGate(
            "inventory", False, "inventory", severity="P1"
        ),
    )
    report = audit.build_audit(audit_args(tmp_path))

    assert report["core_release_ready"] is False
    assert report["failed_count"] == 1
    assert report["p0_failed_count"] == 0


def test_core_audit_rejects_conditional_final_sd_mode(tmp_path):
    args = audit_args(tmp_path)
    args.sd_history_mode = "conditional"
    with pytest.raises(ValueError, match="disabled with NVS"):
        audit.build_audit(args)


def test_defect_gate_delegates_to_strict_raw_api_validator(
    tmp_path, monkeypatch
):
    path = write_json(tmp_path / "issues.json", {})
    calls = []

    def validator(receipt, **kwargs):
        calls.append((receipt, kwargs))
        return True, [], {
            "raw_capture_ok": True,
            "release_gate_ok": True,
        }

    monkeypatch.setattr(
        audit, "validate_core_github_defect_receipt", validator
    )
    gate = audit.defect_gate(
        path, tmp_path, COMMIT, RUN_ID, RUN_ATTEMPT
    )

    assert gate.ok is True
    assert calls == [
        (
            path,
            {
                "root": tmp_path,
                "commit": COMMIT,
                "run_id": RUN_ID,
                "run_attempt": RUN_ATTEMPT,
            },
        )
    ]
    monkeypatch.setattr(
        audit,
        "validate_core_github_defect_receipt",
        lambda *_args, **_kwargs: (
            True,
            [],
            {"release_gate_ok": False},
        ),
    )
    assert not audit.defect_gate(
        path, tmp_path, COMMIT, RUN_ID, RUN_ATTEMPT
    ).ok
    monkeypatch.setattr(
        audit,
        "validate_core_github_defect_receipt",
        lambda *_args, **_kwargs: (_ for _ in ()).throw(
            ValueError("tampered")
        ),
    )
    raised = audit.defect_gate(
        path, tmp_path, COMMIT, RUN_ID, RUN_ATTEMPT
    )
    assert raised.ok is False
    assert raised.details["reasons"] == [
        "strict_r8_defect_validator_failed:ValueError"
    ]
    monkeypatch.setattr(
        audit, "validate_core_github_defect_receipt", None
    )
    assert not audit.defect_gate(
        path, tmp_path, COMMIT, RUN_ID, RUN_ATTEMPT
    ).ok


def test_existing_full_default_audit_contract_is_unchanged():
    args = full_audit.parse_args([])

    assert full_audit.FULL_SOAK_SECONDS == 12 * 60 * 60
    assert args.commit is None
    assert args.github_run_id is None
    assert args.fail_on_open_p0 is False
    assert not hasattr(args, "release_profile")
