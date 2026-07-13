import json
from pathlib import Path

from scripts.sd_retained_history_acceptance_d1l import (
    fingerprint_for_token,
    retained_canary_hash,
)


def clean_storage_status() -> dict:
    counters = {
        "sd_read_fail_count": 0,
        "sd_write_fail_count": 0,
        "sd_rename_fail_count": 0,
        "nvs_mirror_fail_count": 0,
        "sd_last_error": "ESP_OK",
        "nvs_mirror_last_error": "ESP_OK",
        "sd_degraded_latched": False,
    }
    return {
        "schema": 1,
        "ok": True,
        "cmd": "storage status",
        "manager": {"running": True, "state": "READY_SD"},
        "sd": {
            "state": "ready",
            "filesystem": "fat32",
            "interface": "rp2040_uart_bridge",
            "present": True,
            "mounted": True,
            "data_root_ready": True,
            "rp2040_protocol_supported": True,
            "status_stale": False,
            "presence_stale": False,
            "refresh_failures": 0,
            "file_ops": True,
            "atomic_rename": True,
            "file_line_max": 512,
            "file_chunk_max": 192,
            "path_max": 96,
        },
        "data_enabled": True,
        "data_backend": "mixed",
        "packet_log_backend": "sd",
        "stores": {
            "settings": "nvs",
            "identity": "nvs",
            "messages": "sd",
            "dm": "sd",
            "packets": "sd",
            "routes": "sd",
            "contacts": "nvs",
            "read_state": "nvs",
            "crashlog": "nvs",
            "map_tiles": "sd",
            "exports": "sd",
        },
        "message_store_backend": "sd",
        "dm_store_backend": "sd",
        "route_store_backend": "sd",
        "retained_nvs": {
            "partition": "d1l_retained",
            "marker_ready": True,
            "markers_complete": True,
            "anchor_ready": True,
            "sentinel_ready": True,
            "external_init_required": False,
            "initialized_this_boot": False,
            "ready": True,
            "init_error": "ESP_OK",
            "migrated_keys": 4,
            "migration_error": "ESP_OK",
        },
        "retained_sd": {
            "degraded": False,
            "backup_degraded": False,
            "stores": {
                name: dict(counters) for name in ("messages", "dm", "routes", "packets")
            },
        },
    }


def message_persistence() -> dict:
    return {
        "loaded": True,
        "dirty": False,
        "failures": 0,
        "sd": {
            "required": True,
            "generation": 3,
            "dirty": False,
            "reconcile_pending": False,
            "failures": 0,
            "last_error": "ESP_OK",
        },
        "nvs": {"dirty": False, "failures": 0, "last_error": "ESP_OK"},
    }


def packet_persistence() -> dict:
    return {
        "loaded": True,
        "dirty": False,
        "failures": 0,
        "reconcile": {"pending": False, "failures": 0, "last_error": "ESP_OK"},
        "sd": {
            "required": True,
            "generation": 3,
            "dirty": False,
            "failures": 0,
            "last_error": "ESP_OK",
        },
        "nvs": {"dirty": False, "failures": 0, "last_error": "ESP_OK"},
        "journal": {"dirty": False, "failures": 0, "last_error": "ESP_OK"},
    }


def route_persistence() -> dict:
    return {
        "dirty": False,
        "fail_count": 0,
        "clear_failure_latched": False,
        "clear_fail_count": 0,
        "clear_last_error": "ESP_OK",
        "sd_primary": {
            "required": True,
            "dirty": False,
            "reconcile_pending": False,
            "fail_count": 0,
            "last_error": "ESP_OK",
        },
        "nvs_fallback": {"dirty": False, "fail_count": 0, "last_error": "ESP_OK"},
    }


def retained_readbacks(token: str) -> dict[str, dict]:
    fingerprint = fingerprint_for_token(token)
    text = f"sd-retained-canary {token}"
    public = {
        "schema": 1,
        "ok": True,
        "cmd": "messages public",
        "count": 1,
        "filtered": True,
        "search": token,
        "page_count": 1,
        "total_matches": 1,
        "persistence": message_persistence(),
        "persisted": True,
        "entries": [
            {
                "seq": 101,
                "direction": "tx",
                "author": "SD Canary",
                "text": text,
                "delivered": True,
            }
        ],
    }
    dm = {
        "schema": 1,
        "ok": True,
        "cmd": "messages dm",
        "count": 1,
        "filtered": True,
        "fingerprint": fingerprint,
        "page_count": 1,
        "total_matches": 1,
        "thread_count": 1,
        "persistence": message_persistence(),
        "persisted": True,
        "entries": [
            {
                "seq": 102,
                "fingerprint": fingerprint,
                "alias": "SD Canary",
                "direction": "tx",
                "text": text,
                "delivered": True,
                "acked": True,
                "ack_hash": retained_canary_hash(token),
            }
        ],
    }
    route = {
        "schema": 1,
        "ok": True,
        "cmd": "routes trace",
        "fingerprint": fingerprint,
        "route_count": 1,
        "entries": [
            {
                "seq": 103,
                "target": fingerprint,
                "label": "SD Canary",
                "kind": "sd_canary",
                "route": "local",
                "direction": "tx",
                "payload_len": len(text),
            }
        ],
    }
    packet = {
        "schema": 1,
        "ok": True,
        "cmd": "packets search",
        "count": 1,
        "filter": {"direction": "any", "kind": "any", "search": token},
        "persistence": packet_persistence(),
        "persisted": True,
        "entries": [
            {
                "seq": 104,
                "direction": "tx",
                "kind": "sd_canary",
                "payload_len": len(text),
                "note": f"sd-canary {token}",
            }
        ],
    }
    return {
        f"messages public search {token}": public,
        f"messages dm {fingerprint}": dm,
        f"routes trace {fingerprint}": route,
        f"packets search {token}": packet,
    }


def write_reboot_source(
    path: Path,
    *,
    commit: str,
    token: str,
    before_nonce: int,
    after_nonce: int,
    crash_total: int,
) -> Path:
    fingerprint = fingerprint_for_token(token)
    readbacks = retained_readbacks(token)
    version = {"schema": 1, "ok": True, "cmd": "version", "build_commit": commit}
    crash_before = {
        "schema": 1,
        "ok": True,
        "cmd": "crashlog",
        "total_written": crash_total,
        "entries": [
            {"seq": crash_total, "reset_reason": "SW", "crash_like": False}
        ],
    }
    crash_after = {
        "schema": 1,
        "ok": True,
        "cmd": "crashlog",
        "total_written": crash_total + 1,
        "entries": [
            {"seq": crash_total, "reset_reason": "SW", "crash_like": False},
            {"seq": crash_total + 1, "reset_reason": "SW", "crash_like": False},
        ],
    }
    mount = {
        "schema": 1,
        "ok": True,
        "cmd": "storage remount",
        "retained_worker_quiesce_acquired": True,
    }
    canary = {
        "schema": 1,
        "ok": True,
        "cmd": "storage retained-canary",
        "token": token,
        "fingerprint": fingerprint,
        "storage_manager_quiesced": True,
        "retained_worker_quiesced": True,
        "public_seq": 101,
        "dm_seq": 102,
        "route_seq": 103,
        "packet_seq": 104,
        "backends": {name: "sd" for name in ("messages", "dm", "routes", "packets")},
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "formats_sd": False,
    }
    persistence = {
        f"messages public search {token}": json.loads(
            json.dumps(readbacks[f"messages public search {token}"])
        ),
        f"messages dm {fingerprint}": json.loads(
            json.dumps(readbacks[f"messages dm {fingerprint}"])
        ),
        "routes": {
            "schema": 1,
            "ok": True,
            "cmd": "routes",
            "persisted": True,
            "persistence": route_persistence(),
        },
        f"packets search {token}": json.loads(
            json.dumps(readbacks[f"packets search {token}"])
        ),
        "storage status": clean_storage_status(),
    }
    pre_commands = [
        "version",
        "crashlog",
        "storage status",
        "storage remount",
        "storage status",
        "storage filecanary",
        f"storage retained-canary {token}",
        f"storage map-tile-canary {token}",
        *readbacks,
        "storage status",
        "health",
        *persistence,
    ]
    pre_results = [
        version,
        crash_before,
        clean_storage_status(),
        mount,
        clean_storage_status(),
        {
            "schema": 1,
            "ok": True,
            "cmd": "storage filecanary",
            "rename_replace": True,
            "read_final": True,
            "delete_final": True,
            "stat_deleted": True,
        },
        canary,
        {
            "schema": 1,
            "ok": True,
            "cmd": "storage map-tile-canary",
            "token": token,
            "rename_replace": True,
            "read_final": True,
            "public_rf_tx": False,
            "formats_sd": False,
        },
        *[json.loads(json.dumps(value)) for value in readbacks.values()],
        clean_storage_status(),
        {
            "schema": 1,
            "ok": True,
            "cmd": "health",
            "boot_nonce": before_nonce,
            "reset_reason": "SW",
            "retained_task_stack_free_bytes": 8192,
        },
        *[json.loads(json.dumps(value)) for value in persistence.values()],
    ]
    reboot = {
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
    }
    post_commands = [
        "storage status",
        "storage remount",
        "storage status",
        *readbacks,
        f"storage map-tile-check {token}",
        "health",
        "version",
        "crashlog",
        *persistence,
    ]
    post_results = [
        clean_storage_status(),
        json.loads(json.dumps(mount)),
        clean_storage_status(),
        *[json.loads(json.dumps(value)) for value in readbacks.values()],
        {
            "schema": 1,
            "ok": True,
            "cmd": "storage map-tile-check",
            "token": token,
            "stat_final": True,
            "read_final": True,
            "public_rf_tx": False,
            "formats_sd": False,
        },
        {
            "schema": 1,
            "ok": True,
            "cmd": "health",
            "boot_nonce": after_nonce,
            "reset_reason": "SW",
            "retained_task_stack_free_bytes": 8192,
        },
        json.loads(json.dumps(version)),
        crash_after,
        *[json.loads(json.dumps(value)) for value in persistence.values()],
    ]
    commands = [*pre_commands, "reboot", *post_commands]
    results = [*pre_results, reboot, *post_results]
    report = {
        "schema": 1,
        "mode": "hardware",
        "ok": True,
        "port": "COM12",
        "baud": 115200,
        "token": token,
        "commit": commit,
        "git": {"commit": commit, "dirty": False, "dirty_entries": []},
        "expected_firmware_commit": commit,
        "pre_device_build_commit": commit,
        "device_build_commit": commit,
        "firmware_identity_required": True,
        "pre_firmware_identity_ok": True,
        "post_firmware_identity_ok": True,
        "firmware_identity_ok": True,
        "persistence_clean_required": True,
        "pre_reboot_persistence_checked": True,
        "pre_reboot_persistence_clean": True,
        "pre_reboot_pending_dirty": False,
        "pre_reboot_persistence_poll_attempts_used": 1,
        "pre_reboot_persistence": json.loads(json.dumps(persistence)),
        "post_reboot_persistence_checked": True,
        "post_reboot_persistence_clean": True,
        "post_reboot_pending_dirty": False,
        "persistence_poll_attempts_used": 1,
        "post_reboot_persistence": json.loads(json.dumps(persistence)),
        "crashlog_transition_required": True,
        "crashlog_transition_ok": True,
        "timed_out_command": None,
        "unexpected_restart_before_reboot": False,
        "pre_reboot_gate_passed": True,
        "pre_reboot_health_ok": True,
        "reboot_attempted": True,
        "reboot_skipped_reason": None,
        "reboot_reset_scope": "system",
        "reboot_connectivity_prepare": "ESP_OK",
        "pre_reboot_boot_nonce": before_nonce,
        "post_reboot_boot_nonce": after_nonce,
        "post_reboot_reset_reason": "SW",
        "reboot_nonce_proven": True,
        "reboot_proven": True,
        "health_ok": True,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "formats_sd": False,
        "commands": commands,
        "results": results,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report), encoding="utf-8")
    return path


def write_inserted_soak(path: Path, *, commit: str) -> Path:
    crashlog = {
        "schema": 1,
        "ok": True,
        "cmd": "crashlog",
        "count": 1,
        "total_written": 1,
        "entries": [{"seq": 1, "reset_reason": "SW", "crash_like": False}],
    }
    commands = [
        "health",
        "mesh status",
        "signal",
        "messages unread",
        "packets",
        "crashlog",
        "storage status",
        "storage filecanary",
    ]
    samples = []
    for index, elapsed in enumerate((0, 60, 120, 180, 240, 300)):
        samples.append(
            {
                "label": "start" if index == 0 else ("final" if index == 5 else f"sample-{index}"),
                "elapsed_sec": float(elapsed),
                "aborted_after_timeout": None,
                "results": [
                    {
                        "schema": 1,
                        "ok": True,
                        "cmd": "health",
                        "boot_nonce": 42,
                        "uptime_ms": 1000 + elapsed * 1000,
                    },
                    {"schema": 1, "ok": True, "cmd": "mesh status"},
                    {"schema": 1, "ok": True, "cmd": "signal"},
                    {"schema": 1, "ok": True, "cmd": "messages unread"},
                    {
                        "schema": 1,
                        "ok": True,
                        "cmd": "packets",
                        "persistence": packet_persistence(),
                    },
                    json.loads(json.dumps(crashlog)),
                    clean_storage_status(),
                    {
                        "schema": 1,
                        "ok": True,
                        "cmd": "storage filecanary",
                        "rename_replace": True,
                        "read_final": True,
                        "delete_final": True,
                        "stat_deleted": True,
                    },
                ],
            }
        )
    version = {"schema": 1, "ok": True, "cmd": "version", "build_commit": commit}
    report = {
        "schema": 1,
        "mode": "hardware",
        "ok": True,
        "port": "COM12",
        "baud": 115200,
        "commit": commit,
        "git": {"commit": commit, "dirty": False, "dirty_entries": []},
        "preflight_commands": ["version"],
        "expected_firmware_commit": commit,
        "device_build_commit": commit,
        "firmware_identity_required": True,
        "firmware_identity_ok": True,
        "version_preflight": version,
        "preflight_failure": None,
        "duration_sec": 300.0,
        "commands": commands,
        "sample_storage": True,
        "sd_file_canary": True,
        "allow_sd_unavailable": False,
        "active_command": None,
        "dm_rf_tx": False,
        "public_rf_tx": False,
        "formats_sd": False,
        "aborted_after_timeout": None,
        "summary": {
            "ok": True,
            "command_failure_count": 0,
            "command_failures": [],
            "threshold_failures": [],
            "command_timeout_seen": False,
            "unexpected_console_restart_seen": False,
        },
        "samples": samples,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report), encoding="utf-8")
    return path


def storage_active_segment(
    *, commit: str, boot_nonce: int, crash_total: int, duration_sec: float = 1800.0
) -> dict:
    commands = [
        "health",
        "mesh status",
        "signal",
        "messages unread",
        "packets",
        "crashlog",
        "storage status",
        "storage filecanary",
    ]
    crashlog = {
        "schema": 1,
        "ok": True,
        "cmd": "crashlog",
        "count": 1,
        "total_written": crash_total,
        "entries": [
            {"seq": crash_total, "reset_reason": "SW", "crash_like": False}
        ],
    }
    samples = []
    for index, elapsed in enumerate((0.0, duration_sec)):
        samples.append(
            {
                "label": "start" if index == 0 else "final",
                "elapsed_sec": elapsed,
                "aborted_after_timeout": None,
                "results": [
                    {
                        "schema": 1,
                        "ok": True,
                        "cmd": "health",
                        "boot_nonce": boot_nonce,
                        "uptime_ms": 1000 + int(elapsed * 1000),
                        "retained_task_stack_free_bytes": 8192,
                    },
                    {"schema": 1, "ok": True, "cmd": "mesh status"},
                    {"schema": 1, "ok": True, "cmd": "signal"},
                    {"schema": 1, "ok": True, "cmd": "messages unread"},
                    {
                        "schema": 1,
                        "ok": True,
                        "cmd": "packets",
                        "persistence": packet_persistence(),
                    },
                    json.loads(json.dumps(crashlog)),
                    clean_storage_status(),
                    {
                        "schema": 1,
                        "ok": True,
                        "cmd": "storage filecanary",
                        "rename_replace": True,
                        "read_final": True,
                        "delete_final": True,
                        "stat_deleted": True,
                    },
                ],
            }
        )
    version = {"schema": 1, "ok": True, "cmd": "version", "build_commit": commit}
    return {
        "schema": 1,
        "mode": "hardware",
        "ok": True,
        "port": "COM12",
        "baud": 115200,
        "commit": commit,
        "git": {"commit": commit, "dirty": False, "dirty_entries": []},
        "preflight_commands": ["version"],
        "expected_firmware_commit": commit,
        "device_build_commit": commit,
        "firmware_identity_required": True,
        "firmware_identity_ok": True,
        "version_preflight": version,
        "preflight_failure": None,
        "duration_sec": duration_sec,
        "sample_interval_sec": duration_sec,
        "commands": commands,
        "sample_storage": True,
        "sd_file_canary": True,
        "allow_sd_unavailable": False,
        "active_command": None,
        "active_events": [],
        "command_retries": 0,
        "dm_rf_tx": False,
        "public_rf_tx": False,
        "formats_sd": False,
        "aborted_after_timeout": None,
        "summary": {
            "ok": True,
            "command_failure_count": 0,
            "command_failures": [],
            "threshold_failures": [],
            "command_timeout_seen": False,
            "unexpected_console_restart_seen": False,
            "command_retry_count": 0,
            "command_recovered_after_retry_count": 0,
            "command_retry_failure_count": 0,
        },
        "samples": samples,
    }


def write_storage_active_soak_source(
    path: Path, *, commit: str, segment_count: int = 4
) -> Path:
    events = []
    for index in range(1, segment_count + 1):
        events.append(
            {
                "kind": "segment",
                "index": index,
                "report": storage_active_segment(
                    commit=commit,
                    boot_nonce=99 + index,
                    crash_total=19 + index,
                ),
            }
        )
        if index == segment_count:
            continue
        reboot_path = path.parent / f"active-reboot-{index}.json"
        write_reboot_source(
            reboot_path,
            commit=commit,
            token=f"active{index}",
            before_nonce=99 + index,
            after_nonce=100 + index,
            crash_total=19 + index,
        )
        reboot = json.loads(reboot_path.read_text(encoding="utf-8"))
        events.append(
            {
                "kind": "reboot",
                "index": index,
                "token": f"active{index}",
                "canary_safety_explicit": True,
                "report": reboot,
            }
        )
    report = {
        "schema": 1,
        "kind": "d1l_storage_active_soak_source",
        "mode": "hardware",
        "ok": True,
        "port": "COM12",
        "baud": 115200,
        "commit": commit,
        "git": {"commit": commit, "dirty": False, "dirty_entries": []},
        "expected_firmware_commit": commit,
        "segment_count": segment_count,
        "segment_duration_sec": 1800.0,
        "scheduled_segment_duration_sec": segment_count * 1800.0,
        "sample_interval_sec": 1800.0,
        "event_topology": [event["kind"] for event in events],
        "events": events,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "formats_sd": False,
        "failure": None,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report), encoding="utf-8")
    return path
