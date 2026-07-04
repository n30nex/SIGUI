import hashlib
import json
from pathlib import Path

from scripts import release_gate_audit_d1l as audit
from scripts.release_gate_audit_d1l import build_audit, parse_args


COMMIT = "68350bf9f3fabfd2db4110ec6ffc36068056a060"
STALE_COMMIT = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
RUN_ID = "28549761003"
SCROLL_SURFACES = {
    "home": "home",
    "public_messages": "messages",
    "dm_thread": "messages",
    "nodes": "nodes",
    "packets": "packets",
    "settings": "settings",
    "storage": "settings",
    "wifi": "settings",
    "map": "map",
}
STRICT_SD_GATE_IDS = (
    "sd_raw_diag_complete",
    "sd_boot_retry_manager",
    "sd_filecanary_independent",
    "sd_retained_canary_passed",
    "sd_reboot_remount_passed",
    "sd_map_tile_canary_passed",
    "sd_fat32_only_enforced",
    "sd_no_format_language",
    "sd_power_rail_measured",
    "sd_32gb_max_matrix_passed",
)


def ui_corruption_probe_payload(**overrides: object) -> dict:
    payload = {
        "ok": True,
        "kind": "ui_corruption_probe",
        "port": "COM12",
        "rounds": 20,
        "release_min_rounds": 20,
        "data_refresh_events": 20,
        "public_rf_tx": False,
        "formats_sd": False,
        "skip_data_canary": False,
        "failure_count": 0,
        "checks": {
            "tab_switches_settle": True,
            "data_refresh_exercised": True,
            "data_refreshes_pass": True,
            "no_public_rf": True,
            "no_formatting": True,
        },
        "telemetry": {
            "health_sample_count": 180,
            "uptime_monotonic": True,
            "telemetry_fields": [
                "heap_free",
                "heap_min_free",
                "heap_largest_free",
                "lvgl_free_bytes",
                "lvgl_largest_free_bytes",
                "lvgl_used_pct",
                "ui_task_stack_free_words",
                "reset_reason",
            ],
        },
    }
    payload.update(overrides)
    return payload


def ui_pixel_capture_payload(**overrides: object) -> dict:
    payload = {
        "ok": True,
        "kind": "ui_pixel_capture",
        "mode": "hardware",
        "port": "COM12",
        "width": 480,
        "height": 480,
        "bytes_per_pixel": 2,
        "pixel_format": "rgb565-le",
        "total_bytes": 480 * 480 * 2,
        "captured_bytes": 480 * 480 * 2,
        "chunk_size": 1024,
        "chunk_count": 450,
        "crc32": "1234ABCD",
        "firmware_crc32": "1234ABCD",
        "png_path": "artifacts/hardware/com12/ui_pixel_capture_68350bf.png",
        "raw_path": "artifacts/hardware/com12/ui_pixel_capture_68350bf.rgb565",
        "public_rf_tx": False,
        "formats_sd": False,
    }
    payload.update(overrides)
    return payload


def compose_keyboard_capture_payload(**overrides: object) -> dict:
    def capture(target: str) -> dict:
        probe_target = target.replace("-", "_")
        return {
            "target": target,
            "ok": True,
            "compose_probe": {
                "ok": True,
                "cmd": "ui compose-probe",
                "target": probe_target,
                "target_supported": True,
                "sheet_visible": True,
                "textarea_visible": True,
                "keyboard_visible": True,
                "dock_hidden": True,
                "dm_mode": target.startswith("dm"),
                "active_tab": "messages",
                "sheet": {"x": 0, "y": 56, "w": 480, "h": 424},
                "textarea": {"x": 16, "y": 58, "w": 448, "h": 78},
                "keyboard": {"x": 16, "y": 158, "w": 448, "h": 258},
                "public_rf_tx": False,
                "formats_sd": False,
            },
            "capture": ui_pixel_capture_payload(),
            "png_path": f"artifacts/hardware/com12/ui_compose_{target}.png",
            "raw_path": f"artifacts/hardware/com12/ui_compose_{target}.rgb565",
            "public_rf_tx": False,
            "formats_sd": False,
        }

    payload = {
        "ok": True,
        "kind": "ui_compose_keyboard_capture",
        "mode": "hardware",
        "port": "COM12",
        "targets": ["public", "public-long", "dm", "dm-long"],
        "captures": [capture(target) for target in ("public", "public-long", "dm", "dm-long")],
        "capture_count": 4,
        "public_rf_tx": False,
        "formats_sd": False,
    }
    payload.update(overrides)
    return payload


def scroll_probe_payload(**overrides: object) -> dict:
    probe_results = {
        screen: {
            "ok": True,
            "surface": screen,
            "tab": tab,
            "target_found": True,
            "scrollable": True,
            "moved": True,
            "before_y": 0,
            "after_y": 120,
            "scroll_top_before": 0,
            "scroll_bottom_before": 120,
            "scroll_top_after": 120,
            "scroll_bottom_after": 0,
        }
        for screen, tab in SCROLL_SURFACES.items()
    }
    payload = {
        "ok": True,
        "port": "COM12",
        "failure_count": 0,
        "screens": list(SCROLL_SURFACES),
        "surface_plan": [
            {"screen": screen, "tab": tab, "label": screen.replace("_", " ").title()}
            for screen, tab in SCROLL_SURFACES.items()
        ],
        "probe_results": probe_results,
        "events": [
            {"screen": screen, "tab": tab, "probe": probe_results[screen]}
            for screen, tab in SCROLL_SURFACES.items()
        ],
    }
    payload.update(overrides)
    return payload


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


def write_manifest_file(directory: Path, name: str, payload: bytes = b"ok") -> None:
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / name
    target.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    (directory / "SHA256SUMS.txt").write_text(f"{digest}  ./{name}\n", encoding="ascii")


def write_release_package(run_dir: Path) -> Path:
    package = run_dir / "d1l-release-package" / f"d1l-release-{COMMIT}"
    write_manifest_file(package, "README_RELEASE.md", b"release")
    notices = {
        "notices/LICENSE": b"project license",
        "notices/THIRD_PARTY_NOTICES.md": b"third party",
        "notices/ATTRIBUTIONS.md": b"attributions",
        "notices/SOURCE_AUDIT_AND_ATTRIBUTION.md": b"source audit",
    }
    rows = [(package / "SHA256SUMS.txt").read_text(encoding="ascii").strip()]
    for relative, payload in notices.items():
        target = package / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        rows.append(f"{hashlib.sha256(payload).hexdigest()}  ./{relative}")
    (package / "SHA256SUMS.txt").write_text("\n".join(rows) + "\n", encoding="ascii")
    write_json(
        package / "manifest.json",
        {"notice_files": [{"path": relative} for relative in notices]},
    )
    return package


def write_core_evidence(root: Path) -> None:
    run_dir = root / "artifacts" / "github" / RUN_ID
    write_manifest_file(run_dir / "d1l-firmware-artifacts", "firmware.bin", b"firmware")
    write_manifest_file(run_dir / "rp2040-sd-bridge-firmware", "deskos_sd_bridge.ino.uf2", b"uf2")
    write_manifest_file(
        run_dir / "rp2040-seeed-official-sd-smoke-firmware",
        "seeed_official_sd_smoke.ino.uf2",
        b"official-smoke-uf2",
    )
    write_release_package(run_dir)

    hardware = root / "artifacts" / "hardware" / "com12"
    write_json(hardware / "smoke_68350bf.json", {"ok": True, "port": "COM12"})
    write_json(hardware / "ui_corruption_probe_68350bf.json", ui_corruption_probe_payload())
    write_json(hardware / "ui_pixel_capture_68350bf.json", ui_pixel_capture_payload())
    write_json(hardware / "ui_compose_keyboard_capture_68350bf.json", compose_keyboard_capture_payload())
    write_json(hardware / "scroll_probe_68350bf.json", scroll_probe_payload())
    write_json(
        hardware / "dm_probe_68350bf.json",
        {
            "ok": True,
            "port": "COM12",
            "meshbot_expected_port": "COM11",
            "public_rf_transmit": False,
            "checks": {
                "meshbot_on_expected_port": True,
                "send_ok": True,
                "messages_dm_has_token": True,
                "packets_search_has_token": True,
                "route_trace_has_target": True,
                "meshbot_rx_contact_delta": True,
                "health_ready": True,
                "no_public_commands": True,
            },
        },
    )
    write_json(
        hardware / "rp2040_preflight_68350bf.json",
        {
            "ok": True,
            "ready_for_sd_acceptance": False,
            "candidate_volumes": [],
            "classification": {
                "storage_file_gate_ready": False,
                "sd_state": "setup_required",
                "uf2_volume_available": False,
            },
            "artifact": {"sha256": "032ff80a0f94613bb18742e08cb97aa548bff882c3afacaf15f5c01"},
        },
    )
    (root / "README.md").write_text(
        "release_gate_audit_d1l.py\nready_for_public_release=false\nNo release tag should be cut until\n",
        encoding="utf-8",
    )
    for name in ("ROADMAP.md", "RELEASE_CHECKLIST.md", "KNOWN_LIMITATIONS.md"):
        (root / "docs").mkdir(exist_ok=True)
        (root / "docs" / name).write_text(
            "release_gate_audit_d1l.py\nready_for_public_release=false\nNo release tag should be cut until\n",
            encoding="utf-8",
        )


def write_official_seeed_smoke_evidence(root: Path, commit: str = COMMIT) -> None:
    write_json(
        root / "artifacts" / "hardware" / "com16" / f"seeed_official_sd_smoke_{commit[:7]}.json",
        {
            "schema": 1,
            "kind": "seeed_official_sd_smoke_capture",
            "port": "COM16",
            "firmware_commit": commit,
            "public_rf_tx": False,
            "formats_sd": False,
            "result": {
                "test": "seeed_official_sd_smoke",
                "ok": True,
                "mount": True,
                "fat32": True,
                "needs_fat32": False,
                "root_open": True,
                "mkdir": True,
                "write": True,
                "read": True,
                "rename": True,
                "stat": True,
                "delete": True,
                "public_rf_tx": False,
                "formats_sd": False,
                "will_format": False,
                "format_performed": False,
                "detect_used_for_ok": False,
                "power_measured": False,
                "power_state": "gpio18_commanded_high_not_measured",
                "detect": "low",
                "detect_pullup": 0,
                "detect_pulldown": 0,
                "diag_ran": True,
                "raw_present": True,
                "raw_cmd8_echo_ok": True,
                "raw_acmd41_ready": True,
                "raw_err": 0,
                "raw_cmd0_ready": 255,
                "raw_cmd0": 1,
                "raw_cmd8_ready": 255,
                "raw_cmd8": 1,
                "raw_r70": 0,
                "raw_r71": 0,
                "raw_r72": 1,
                "raw_r73": 170,
                "raw_acmd41": 0,
                "format": "non_destructive",
                "fat_type": 32,
                "max_card_gb": 32,
            },
        },
    )


def write_routes_probe_evidence(root: Path, commit: str = COMMIT) -> None:
    payload = {
        "schema": 1,
        "mode": "hardware-route-probe",
        "port": "COM12",
        "dm_rf_tx": True,
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
        "checks": {
            "trace_reports_active_probe": True,
            "probe_queued": True,
            "probe_is_dm_rf": True,
            "probe_not_public_rf": True,
            "token_generated": True,
            "packets_search_has_token": True,
            "messages_dm_has_token": True,
            "routes_trace_has_probe": True,
            "health_ready": True,
        },
        "steps": [
            {
                "command": "messages dm 0BF0A701D5AE2DB6",
                "result": {
                    "ok": True,
                    "entries": [
                        {
                            "direction": "tx",
                            "text": "trace_unit",
                            "acked": True,
                            "ack_hash": 1234,
                        }
                    ],
                },
            },
            {
                "command": "routes trace 0BF0A701D5AE2DB6",
                "result": {
                    "ok": True,
                    "entries": [
                        {
                            "kind": "trace_probe",
                            "direction": "tx",
                            "route": "direct",
                        }
                    ],
                },
            },
        ],
    }
    write_json(root / "artifacts" / "hardware" / "com12" / f"routes_probe_{commit[:7]}.json", payload)


def ready_storage_status() -> dict:
    return {
        "ok": True,
        "cmd": "storage status",
        "sd": {
            "state": "ready",
            "present": True,
            "mounted": True,
            "data_root_ready": True,
            "rp2040_protocol_supported": True,
            "file_ops": True,
            "atomic_rename": True,
            "file_line_max": 512,
            "file_chunk_max": 192,
            "path_max": 96,
        },
        "data_enabled": True,
        "data_backend": "mixed",
        "message_store_backend": "sd",
        "dm_store_backend": "sd",
        "route_store_backend": "sd",
        "packet_log_backend": "sd",
        "stores": {"messages": "sd", "dm": "sd", "routes": "sd", "packets": "sd"},
    }


def no_format_storage_setup() -> dict:
    return {
        "cmd": "storage setup",
        "ok": True,
        "policy": "no_device_format",
        "needs_fat32": True,
        "will_format": False,
        "format_requested": False,
        "format_performed": False,
        "fallback": "nvs",
    }


def write_ready_sd_preflight(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"rp2040_preflight_{commit[:7]}.json",
        {
            "ok": True,
            "public_rf_tx": False,
            "formats_sd": False,
            "copies_uf2": False,
            "ready_for_sd_acceptance": True,
            "candidate_volumes": [],
            "classification": {
                "storage_file_gate_ready": True,
                "sd_state": "ready",
                "next_action": "run_sd_file_and_export_acceptance",
            },
            "artifact": {"sha256": "032ff80a0f94613bb18742e08cb97aa548bff882c3afacaf15f5c01"},
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def write_raw_diag_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    fields = {
        "pins": "det7-cs13-sck10-mosi11-miso12-pwr18",
        "hz": "1000000",
        "pin_sck": "1",
        "pin_mosi": "1",
        "pin_miso": "1",
        "pin_cs": "1",
        "selected_power": "high",
        "selected_mode": "dedicated",
    }
    for prefix in audit.RAW_DIAG_PROBE_PREFIXES:
        for suffix in audit.RAW_DIAG_PROBE_SUFFIXES:
            fields[f"{prefix}{suffix}"] = "1"
    line = "DESKOS_SD_DIAG " + " ".join(f"{key}={value}" for key, value in fields.items())
    write_json(
        root / "artifacts" / "hardware" / "com16" / f"rp2040_direct_diag_{commit[:7]}.json",
        {
            "schema": 1,
            "ok": True,
            "port": "COM16",
            "public_rf_tx": False,
            "formats_sd": False,
            "diag": line,
            "fields": fields,
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def boot_prepare_payload(
    scenario: str,
    classification: str,
    *,
    commit: str | None = None,
    storage_after: dict | None = None,
    storage_setup: dict | None = None,
    storage_file_gate_ready: bool = False,
    retained_store_gate_ready: bool = False,
    filecanary_passed: bool | None = None,
) -> dict:
    payload = {
        "schema": 1,
        "mode": "hardware",
        "port": "COM12",
        "scenario": scenario,
        "public_rf_tx": False,
        "formats_sd": False,
        "format_command_sent": False,
        "format_confirmed": False,
        "format_allowed": False,
        "commands_safe": True,
        "scenario_passed": True,
        "classification": classification,
        "storage_file_gate_ready": storage_file_gate_ready,
        "retained_store_gate_ready": retained_store_gate_ready,
        "filecanary_passed": filecanary_passed,
        "ok": True,
        "storage_after": storage_after or {},
        "storage_setup": storage_setup,
        "health": {"ok": True},
        "commands": ["rp2040 ping", "storage status", "storage remount", "storage status", "health"],
    }
    if commit:
        payload["firmware_commit"] = commit
    return payload


def write_boot_prepare_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    target_commit = metadata_commit or commit
    hardware = root / "artifacts" / "hardware" / "com12"
    ready = ready_storage_status()
    unformatted_storage = {
        "ok": True,
        "cmd": "storage status",
        "setup_action": "prepare_fat32_on_computer",
        "data_backend": "nvs",
        "sd": {
            "state": "not_fat32_or_unmountable",
            "present": True,
            "mounted": False,
            "data_root_ready": False,
            "rp2040_protocol_supported": True,
            "needs_fat32": True,
            "file_ops": False,
            "atomic_rename": False,
        },
    }
    scenarios = {
        "no-card": boot_prepare_payload(
            "no-card",
            "no_card_fallback",
            commit=target_commit,
            storage_after={"ok": True, "sd": {"state": "no_card", "present": False}},
        ),
        "correct-structure": boot_prepare_payload(
            "correct-structure",
            "ready_sd_file_gate",
            commit=target_commit,
            storage_after=ready,
            storage_file_gate_ready=True,
            retained_store_gate_ready=True,
            filecanary_passed=True,
        ),
        "missing-structure": boot_prepare_payload(
            "missing-structure",
            "ready_sd_file_gate",
            commit=target_commit,
            storage_after=ready,
            storage_file_gate_ready=True,
            retained_store_gate_ready=True,
            filecanary_passed=True,
        ),
        "unformatted": boot_prepare_payload(
            "unformatted",
            "computer_fat32_required",
            commit=target_commit,
            storage_after=unformatted_storage,
            storage_setup=no_format_storage_setup(),
        ),
        "existing-data": boot_prepare_payload(
            "existing-data",
            "existing_data_not_wiped",
            commit=target_commit,
            storage_after={
                "ok": True,
                "setup_action": "prepare_fat32_on_computer",
                "sd": {"state": "deskos_manifest_invalid", "needs_fat32": True},
            },
            storage_setup=no_format_storage_setup(),
        ),
        "rp2040-unavailable": boot_prepare_payload(
            "rp2040-unavailable",
            "bridge_unavailable_fallback",
            commit=target_commit,
            storage_after={"ok": True, "sd": {"state": "rp2040_unavailable"}},
        ),
    }
    for scenario, payload in scenarios.items():
        underscored = scenario.replace("-", "_")
        write_json(hardware / f"sd_boot_prepare_{underscored}_{commit[:7]}.json", payload)


def write_file_canary_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_file_canary_{commit[:7]}.json",
        {
            "schema": 1,
            "mode": "hardware",
            "port": "COM12",
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": True,
            "allow_unavailable": False,
            "canary_passed": True,
            "canary_unavailable_ok": False,
            "storage_file_gate_ready_before": True,
            "storage_file_gate_ready_after": True,
            "retained_history_sd_ready_before": False,
            "retained_history_sd_ready_after": False,
            "commands": ["storage status", "storage filecanary", "storage status", "packets", "health"],
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def write_retained_canary_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_retained_history_{commit[:7]}.json",
        {
            "schema": 1,
            "mode": "hardware",
            "port": "COM12",
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": True,
            "allow_unavailable": False,
            "retained_canary_passed": True,
            "filecanary_passed": True,
            "pre_reboot_readbacks_ok": True,
            "post_reboot_readbacks_ok": True,
            "commands": ["storage status", "storage filecanary", "storage retained-canary sd1", "reboot", "health"],
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def write_reboot_remount_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_reboot_remount_{commit[:7]}.json",
        {
            "schema": 1,
            "mode": "hardware",
            "port": "COM12",
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": True,
            "pre_remount_ready": True,
            "post_remount_ready": True,
            "retained_canary_passed": True,
            "pre_reboot_readbacks_ok": True,
            "post_reboot_readbacks_ok": True,
            "pre_map_tile_canary_passed": True,
            "post_map_tile_canary_passed": True,
            "commands": ["storage status", "storage remount", "storage map-tile-canary sd1", "reboot", "storage map-tile-check sd1", "health"],
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def write_map_tile_canary_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_map_tile_canary_{commit[:7]}.json",
        {
            "schema": 1,
            "mode": "hardware",
            "port": "COM12",
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": True,
            "allow_unavailable": False,
            "canary_passed": True,
            "canary_unavailable_ok": False,
            "storage_file_gate_ready_before": True,
            "storage_file_gate_ready_after": True,
            "map_tile_backend_ready_before": True,
            "map_tile_backend_ready_after": True,
            "commands": ["storage status", "storage map-tile-canary map1", "storage status", "health"],
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def write_power_rail_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_electrical_{commit[:7]}.json",
        {
            "schema": 1,
            "mode": "hardware-electrical",
            "port": "COM12",
            "ok": True,
            "public_rf_tx": False,
            "formats_sd": False,
            "sd_power_rail_measured": True,
            "vcc_at_socket_volts": 3.29,
            "sku": "SenseCAP Indicator D1L",
            "checks": {
                "gpio18_high": True,
                "gnd_continuity": True,
                "cs_toggles_at_socket": True,
                "sck_clocks_at_socket": True,
                "mosi_commands_at_socket": True,
                "miso_response_or_idle_high": True,
            },
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def write_sd_matrix_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    cards = [
        {"label": "8gb-fat32", "capacity_gb": 8, "fat_type": 32, "ok": True, "official_seeed_smoke_passed": True},
        {"label": "16gb-fat32", "capacity_gb": 16, "fat_type": 32, "ok": True, "official_seeed_smoke_passed": True},
        {"label": "32gb-fat32", "capacity_gb": 32, "fat_type": 32, "ok": True, "official_seeed_smoke_passed": True},
    ]
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_matrix_{commit[:7]}.json",
        {
            "schema": 1,
            "port": "COM12",
            "ok": True,
            "public_rf_tx": False,
            "formats_sd": False,
            "cards": cards,
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def write_strict_sd_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    write_raw_diag_evidence(root, commit, metadata_commit)
    write_boot_prepare_evidence(root, commit, metadata_commit)
    write_file_canary_evidence(root, commit, metadata_commit)
    write_retained_canary_evidence(root, commit, metadata_commit)
    write_reboot_remount_evidence(root, commit, metadata_commit)
    write_map_tile_canary_evidence(root, commit, metadata_commit)
    write_power_rail_evidence(root, commit, metadata_commit)
    write_sd_matrix_evidence(root, commit, metadata_commit)


def audit_args(root: Path):
    return parse_args(
        [
            "--root",
            str(root),
            "--github-run-id",
            RUN_ID,
            "--commit",
            COMMIT,
            "--hardware-dir",
            str(root / "artifacts" / "hardware" / "com12"),
            "--rp2040-hardware-dir",
            str(root / "artifacts" / "hardware" / "com16"),
            "--soak-dir",
            str(root / "artifacts" / "soak"),
        ]
    )


def gate_by_id(report: dict) -> dict:
    return {gate["id"]: gate for gate in report["gates"]}


def test_release_gate_audit_passes_proven_core_gates(tmp_path: Path):
    write_core_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ci_artifacts_checksums"]["ok"] is True
    assert gates["release_notices_included"]["ok"] is True
    assert gates["com12_smoke"]["ok"] is True
    assert gates["ui_corruption_probe"]["ok"] is True
    assert gates["ui_pixel_capture"]["ok"] is True
    assert gates["ui_compose_keyboard_capture"]["ok"] is True
    assert gates["ui_scroll_probe"]["ok"] is True
    assert gates["outbound_dm_com11"]["ok"] is True
    assert gates["sd_official_seeed_smoke_passed"]["ok"] is False
    assert gates["docs_current_evidence"]["ok"] is True


def test_release_gate_audit_dry_run_without_downloaded_actions_artifacts_fails_closed(tmp_path: Path):
    report = build_audit(
        parse_args(
            [
                "--root",
                str(tmp_path),
                "--hardware-dir",
                str(tmp_path / "artifacts" / "hardware" / "com12"),
                "--soak-dir",
                str(tmp_path / "artifacts" / "soak"),
            ]
        )
    )
    gates = gate_by_id(report)

    assert report["ready_for_public_release"] is False
    assert gates["ci_artifacts_checksums"]["ok"] is False
    assert gates["ci_artifacts_checksums"]["details"]["missing"] == []


def test_release_gate_audit_blocks_public_release_without_p0_evidence(tmp_path: Path):
    write_core_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert report["ready_for_public_release"] is False
    assert gates["sd_official_seeed_smoke_passed"]["ok"] is False
    assert gates["sd_acceptance_matrix"]["ok"] is False
    assert gates["full_duration_idle_soak"]["ok"] is False
    assert gates["manual_physical_ui_review"]["ok"] is False
    assert gates["full_rf_dm_acceptance"]["ok"] is False
    for gate_id in STRICT_SD_GATE_IDS:
        assert gates[gate_id]["ok"] is False
    assert report["p0_failed_count"] == 15


def test_release_gate_audit_accepts_ready_no_format_sd_preflight(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_ready_sd_preflight(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["sd_acceptance_matrix"]["ok"] is True
    assert gates["sd_acceptance_matrix"]["details"]["formats_sd"] is False
    assert gates["sd_acceptance_matrix"]["details"]["no_device_format_policy_ok"] is True
    assert gates["sd_filecanary_independent"]["ok"] is False
    assert gates["sd_retained_canary_passed"]["ok"] is False
    assert report["ready_for_public_release"] is False


def test_release_gate_audit_accepts_official_seeed_sd_smoke_artifact(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_official_seeed_smoke_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["sd_official_seeed_smoke_passed"]["ok"] is True
    assert gates["sd_official_seeed_smoke_passed"]["evidence"] == [
        "artifacts/hardware/com16/seeed_official_sd_smoke_68350bf.json"
    ]
    assert gates["sd_official_seeed_smoke_passed"]["details"]["inner_test"] == "seeed_official_sd_smoke"
    assert gates["sd_official_seeed_smoke_passed"]["details"]["fat_type"] == 32
    assert gates["sd_official_seeed_smoke_passed"]["details"]["raw_diagnostics"]["raw_acmd41"] == 0
    assert gates["sd_official_seeed_smoke_passed"]["details"]["power_state"] == "gpio18_commanded_high_not_measured"
    assert report["p0_failed_count"] == 14


def test_release_gate_audit_surfaces_failed_official_seeed_raw_diagnostics(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com16"
    write_json(
        hardware / "rp2040_seeed_official_sd_smoke_68350bf.json",
        {
            "schema": 1,
            "kind": "seeed_official_sd_smoke_capture",
            "port": "COM16",
            "commit": COMMIT,
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": False,
            "matched": {
                "test": "seeed_official_sd_smoke",
                "ok": False,
                "mount": False,
                "fat32": False,
                "needs_fat32": False,
                "root_open": False,
                "mkdir": False,
                "write": False,
                "read": False,
                "rename": False,
                "stat": False,
                "delete": False,
                "public_rf_tx": False,
                "formats_sd": False,
                "will_format": False,
                "format_performed": False,
                "detect_used_for_ok": False,
                "power_measured": False,
                "power_state": "gpio18_commanded_high_not_measured",
                "detect": "floating",
                "detect_pullup": 1,
                "detect_pulldown": 0,
                "diag_ran": True,
                "raw_present": False,
                "raw_cmd8_echo_ok": False,
                "raw_acmd41_ready": False,
                "raw_err": 3,
                "raw_data": 0,
                "raw_miso_pullup": 1,
                "raw_miso_idle": 0,
                "raw_idle_ff": 0,
                "raw_cmd0_ready": 0,
                "raw_cmd0": 0,
                "raw_cmd8_ready": 0,
                "raw_cmd8": 0,
                "raw_r70": 0,
                "raw_r71": 0,
                "raw_r72": 0,
                "raw_r73": 0,
                "raw_acmd41": 255,
                "format": "non_destructive",
                "fat_type": 0,
                "max_card_gb": 32,
            },
        },
    )

    report = build_audit(audit_args(tmp_path))
    gate = gate_by_id(report)["sd_official_seeed_smoke_passed"]

    assert gate["ok"] is False
    assert gate["details"]["inner_test"] == "seeed_official_sd_smoke"
    assert gate["details"]["raw_diagnostics"]["raw_cmd0"] == 0
    assert gate["details"]["raw_diagnostics"]["raw_err"] == 3
    assert gate["details"]["power_measured"] is False
    assert gate["details"]["detect"] == "floating"


def test_release_gate_audit_accepts_strict_sd_artifact_gates(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_official_seeed_smoke_evidence(tmp_path)
    write_ready_sd_preflight(tmp_path)
    write_strict_sd_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["sd_acceptance_matrix"]["ok"] is True
    for gate_id in STRICT_SD_GATE_IDS:
        assert gates[gate_id]["ok"] is True
    assert gates["sd_filecanary_independent"]["details"]["retained_history_sd_ready_before"] is False
    assert gates["sd_32gb_max_matrix_passed"]["details"]["capacities_gb"] == [8.0, 16.0, 32.0]
    assert report["ready_for_public_release"] is False
    assert report["p0_failed_count"] == 3


def test_release_gate_audit_rejects_dry_run_sd_artifacts_as_release_evidence(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    write_json(
        hardware / "sd_file_canary_68350bf.json",
        {
            "schema": 1,
            "mode": "dry-run",
            "hardware_required": False,
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": True,
            "commands": ["storage status", "storage filecanary"],
        },
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["sd_filecanary_independent"]["ok"] is False
    assert gates["sd_filecanary_independent"]["details"]["mode"] == "dry-run"


def test_release_gate_audit_rejects_old_smoke_wrapper_when_inner_sd_failed(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com16"
    write_json(
        hardware / "rp2040_seeed_official_sd_smoke_68350bf.json",
        {
            "schema": 1,
            "kind": "rp2040_seeed_sd_smoke",
            "port": "COM16",
            "commit": COMMIT,
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": True,
            "results": [
                {
                    "command": "SD",
                    "matched": "SEEED_SD_SMOKE sd ok=0 pins=cs13-sck10-mosi11-miso12-pwr18 hz=1000000 public_rf_tx=0 formats_sd=0",
                }
            ],
        },
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["sd_official_seeed_smoke_passed"]["ok"] is False
    assert gates["sd_official_seeed_smoke_passed"]["details"]["inner_test"] is None


def test_release_gate_audit_blocks_obsolete_sd_format_guidance_without_echoing_action(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    write_json(
        hardware / "rp2040_preflight_68350bf.json",
        {
            "ok": True,
            "public_rf_tx": False,
            "formats_sd": False,
            "copies_uf2": False,
            "ready_for_sd_acceptance": True,
            "candidate_volumes": [],
            "classification": {
                "storage_file_gate_ready": True,
                "sd_state": "setup_required",
                "next_action": "run_guarded_format_or_swap_known_good_sd_card",
            },
            "artifact": {"sha256": "032ff80a0f94613bb18742e08cb97aa548bff882c3afacaf15f5c01"},
        },
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)
    sd_gate = gates["sd_acceptance_matrix"]

    assert sd_gate["ok"] is False
    assert sd_gate["details"]["no_device_format_policy_ok"] is False
    assert sd_gate["details"]["obsolete_format_action_blocked"] is True
    assert sd_gate["details"]["classification"]["next_action"] == "confirm_fat32_card_or_inspect_rp2040_sd_mount_path"
    assert "run_guarded_format_or_swap_known_good_sd_card" not in json.dumps(sd_gate)


def test_release_gate_audit_recognizes_supplemental_route_probe_without_passing_full_rf(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_routes_probe_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["supplemental_dm_route_probe"]["ok"] is True
    assert gates["supplemental_dm_route_probe"]["severity"] == "P1"
    assert gates["supplemental_dm_route_probe"]["evidence"] == [
        "artifacts/hardware/com12/routes_probe_68350bf.json"
    ]
    assert gates["supplemental_dm_route_probe"]["details"]["scope"] == "supplementary_dm_only_not_full_rf_acceptance"
    assert gates["full_rf_dm_acceptance"]["ok"] is False
    assert gates["full_rf_dm_acceptance"]["details"]["candidate_count"] == 0
    assert report["ready_for_public_release"] is False
    assert report["p0_failed_count"] == 15


def test_release_gate_audit_accepts_full_soak_when_duration_and_summary_pass(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_json(
        tmp_path / "artifacts" / "soak" / "d1l-12h-soak_68350bf.json",
        {
            "ok": True,
            "mode": "hardware",
            "duration_sec": 43200,
            "summary": {
                "ok": True,
                "threshold_failures": [],
                "board_ready_all": True,
                "ui_ready_all": True,
                "uptime_monotonic": True,
            },
        },
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["full_duration_idle_soak"]["ok"] is True


def test_release_gate_audit_ignores_stale_hardware_artifacts(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    (hardware / "ui_corruption_probe_68350bf.json").unlink()
    write_json(
        hardware / "ui_corruption_probe_deadbee.json",
        ui_corruption_probe_payload(),
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_corruption_probe"]["ok"] is False
    assert gates["ui_corruption_probe"]["details"]["path_found"] is False


def test_release_gate_audit_requires_targeted_ui_corruption_rounds(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    write_json(
        hardware / "ui_corruption_probe_68350bf.json",
        ui_corruption_probe_payload(rounds=3, data_refresh_events=3),
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_corruption_probe"]["ok"] is False


def test_release_gate_audit_requires_hardware_ui_pixel_capture(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    write_json(
        hardware / "ui_pixel_capture_68350bf.json",
        ui_pixel_capture_payload(captured_bytes=1024, crc32=""),
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_pixel_capture"]["ok"] is False
    assert gates["ui_pixel_capture"]["details"]["path_found"] is True


def test_release_gate_audit_requires_compose_keyboard_capture_geometry(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = compose_keyboard_capture_payload()
    payload["captures"][0]["compose_probe"]["keyboard"]["h"] = 180
    write_json(hardware / "ui_compose_keyboard_capture_68350bf.json", payload)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_compose_keyboard_capture"]["ok"] is False
    assert gates["ui_compose_keyboard_capture"]["details"]["path_found"] is True


def test_release_gate_audit_requires_all_scroll_surfaces(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    missing_storage = {key: value for key, value in SCROLL_SURFACES.items() if key != "storage"}
    write_json(
        hardware / "scroll_probe_68350bf.json",
        scroll_probe_payload(
            screens=list(missing_storage),
            surface_plan=[
                {"screen": screen, "tab": tab, "label": screen.replace("_", " ").title()}
                for screen, tab in missing_storage.items()
            ],
        ),
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_scroll_probe"]["ok"] is False


def test_release_gate_audit_requires_scroll_probe_movement(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = scroll_probe_payload()
    payload["probe_results"]["storage"]["ok"] = False
    payload["probe_results"]["storage"]["moved"] = False
    for event in payload["events"]:
        if event["screen"] == "storage":
            event["probe"] = payload["probe_results"]["storage"]
    write_json(hardware / "scroll_probe_68350bf.json", payload)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_scroll_probe"]["ok"] is False


def test_release_gate_audit_rejects_mismatched_commit_metadata_even_when_filename_matches(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    write_json(hardware / "smoke_68350bf.json", {"ok": True, "port": "COM12", "firmware_commit": STALE_COMMIT})
    write_json(
        hardware / "ui_corruption_probe_68350bf.json",
        ui_corruption_probe_payload(firmware_commit=STALE_COMMIT),
    )
    write_json(
        hardware / "scroll_probe_68350bf.json",
        scroll_probe_payload(git={"head_sha": STALE_COMMIT}),
    )
    write_json(
        hardware / "dm_probe_68350bf.json",
        {
            "ok": True,
            "port": "COM12",
            "meshbot_expected_port": "COM11",
            "public_rf_transmit": False,
            "artifact": {"commit": STALE_COMMIT},
            "checks": {
                "meshbot_on_expected_port": True,
                "send_ok": True,
                "messages_dm_has_token": True,
                "packets_search_has_token": True,
                "route_trace_has_target": True,
                "meshbot_rx_contact_delta": True,
                "health_ready": True,
                "no_public_commands": True,
            },
        },
    )
    write_json(
        hardware / "rp2040_preflight_68350bf.json",
        {
            "ok": True,
            "ready_for_sd_acceptance": True,
            "candidate_volumes": [],
            "classification": {"storage_file_gate_ready": True},
            "firmwareCommit": STALE_COMMIT,
        },
    )
    write_json(
        tmp_path / "artifacts" / "hardware" / "com16" / "seeed_official_sd_smoke_68350bf.json",
        {
            "schema": 1,
            "kind": "seeed_official_sd_smoke_capture",
            "port": "COM16",
            "firmware_commit": STALE_COMMIT,
            "result": {
                "test": "seeed_official_sd_smoke",
                "ok": True,
                "mount": True,
                "root_open": True,
                "mkdir": True,
                "write": True,
                "read": True,
                "rename": True,
                "stat": True,
                "delete": True,
                "public_rf_tx": False,
                "formats_sd": False,
                "format": "non_destructive",
                "fat_type": 32,
                "max_card_gb": 32,
            },
        },
    )
    write_json(
        tmp_path / "artifacts" / "soak" / "d1l-12h-soak_68350bf.json",
        {
            "ok": True,
            "mode": "hardware",
            "duration_sec": 43200,
            "commit": STALE_COMMIT,
            "summary": {
                "ok": True,
                "threshold_failures": [],
                "board_ready_all": True,
                "ui_ready_all": True,
                "uptime_monotonic": True,
            },
        },
    )
    write_json(hardware / "manual_touch_review_68350bf.json", {"ok": True, "source": {"commit": STALE_COMMIT}})
    (hardware / "photos").mkdir(parents=True)
    (hardware / "photos" / "screen.jpg").write_bytes(b"screen")
    write_json(
        hardware / "rf_full_acceptance_68350bf.json",
        {
            "ok": True,
            "source": {"commit": STALE_COMMIT},
            "checks": {
                "inbound_dm": True,
                "ack_path": True,
                "direct_route": True,
            },
        },
    )
    write_strict_sd_evidence(tmp_path, metadata_commit=STALE_COMMIT)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    for gate_id in (
        "com12_smoke",
        "ui_corruption_probe",
        "ui_scroll_probe",
        "outbound_dm_com11",
        "sd_official_seeed_smoke_passed",
        "sd_acceptance_matrix",
        *STRICT_SD_GATE_IDS,
        "full_duration_idle_soak",
        "manual_physical_ui_review",
        "full_rf_dm_acceptance",
    ):
        assert gates[gate_id]["ok"] is False
    assert gates["com12_smoke"]["details"]["path_found"] is False
    assert gates["ui_corruption_probe"]["details"]["path_found"] is False
    assert gates["ui_scroll_probe"]["details"]["path_found"] is False
    assert gates["outbound_dm_com11"]["details"]["path_found"] is False
    assert gates["sd_official_seeed_smoke_passed"]["evidence"] == []
    assert gates["sd_acceptance_matrix"]["evidence"] == []
    for gate_id in STRICT_SD_GATE_IDS:
        assert gates[gate_id]["evidence"] == []
    assert gates["full_duration_idle_soak"]["evidence"] == []
    assert gates["manual_physical_ui_review"]["details"]["review_found"] is False
    assert gates["manual_physical_ui_review"]["details"]["photo_count"] == 1
    assert gates["full_rf_dm_acceptance"]["details"]["candidate_count"] == 0


def test_release_gate_audit_accepts_matching_commit_metadata_without_commit_filename(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    (hardware / "ui_corruption_probe_68350bf.json").unlink()
    write_json(
        hardware / "ui_corruption_probe_latest.json",
        ui_corruption_probe_payload(firmware_commit=COMMIT),
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_corruption_probe"]["ok"] is True
    assert gates["ui_corruption_probe"]["evidence"] == [
        "artifacts/hardware/com12/ui_corruption_probe_latest.json"
    ]


def test_release_gate_audit_discovers_autonomous_script_artifact_dirs(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    (hardware / "smoke_68350bf.json").unlink()
    (hardware / "ui_corruption_probe_68350bf.json").unlink()
    (hardware / "scroll_probe_68350bf.json").unlink()

    write_json(
        tmp_path / "artifacts" / "smoke" / "d1l-smoke-COM12-actions-68350bf.json",
        {"ok": True, "port": "COM12", "firmware_commit": COMMIT},
    )
    write_json(
        tmp_path / "artifacts" / "ui-corruption-probe" / "d1l-ui-corruption-probe-COM12-actions-68350bf.json",
        ui_corruption_probe_payload(firmware_commit=COMMIT),
    )
    write_json(
        tmp_path / "artifacts" / "scroll-probe" / "d1l-scroll-probe-COM12-actions-68350bf.json",
        scroll_probe_payload(firmware_commit=COMMIT),
    )
    write_official_seeed_smoke_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["com12_smoke"]["ok"] is True
    assert gates["com12_smoke"]["evidence"] == ["artifacts/smoke/d1l-smoke-COM12-actions-68350bf.json"]
    assert gates["ui_corruption_probe"]["ok"] is True
    assert gates["ui_corruption_probe"]["evidence"] == [
        "artifacts/ui-corruption-probe/d1l-ui-corruption-probe-COM12-actions-68350bf.json"
    ]
    assert gates["ui_scroll_probe"]["ok"] is True
    assert gates["ui_scroll_probe"]["evidence"] == [
        "artifacts/scroll-probe/d1l-scroll-probe-COM12-actions-68350bf.json"
    ]
    assert gates["sd_official_seeed_smoke_passed"]["ok"] is True
    assert gates["sd_official_seeed_smoke_passed"]["evidence"] == [
        "artifacts/hardware/com16/seeed_official_sd_smoke_68350bf.json"
    ]


def test_release_gate_audit_accepts_full_rf_acceptance_artifact(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_json(
        tmp_path / "artifacts" / "hardware" / "com12" / "rf_full_acceptance_68350bf.json",
        {
            "ok": True,
            "checks": {
                "inbound_dm": True,
                "ack_path": True,
                "direct_route": True,
            },
        },
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["full_rf_dm_acceptance"]["ok"] is True
