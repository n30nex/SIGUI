import hashlib
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

from scripts import release_gate_audit_d1l as audit
from scripts.release_gate_audit_d1l import build_audit, parse_args


COMMIT = "68350bf9f3fabfd2db4110ec6ffc36068056a060"
STALE_COMMIT = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
RUN_ID = "28549761003"
COMPOSE_CAPTURE_TARGETS = [
    "public",
    "public-long",
    "dm",
    "dm-long",
    "public-search",
    "packet-search",
    "contact-edit",
    "onboarding",
    "map-location",
    "wifi-ssid",
    "wifi-password",
]
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
    "map_options": "map",
    "map_location": "map",
    "map_cache": "map",
}
STRICT_SD_GATE_IDS = (
    "sd_raw_diag_complete",
    "sd_boot_retry_manager",
    "sd_filecanary_independent",
    "sd_retained_canary_passed",
    "sd_reboot_remount_passed",
    "sd_map_tile_canary_passed",
    "sd_export_canary_passed",
    "sd_diagnostic_export_passed",
    "sd_data_export_passed",
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
        "network_tx": False,
        "map_network_requests": False,
        "map_network_evidence": {
            "command": "map tiles status",
            "measured": True,
            "before": {"ok": True, "network_requests": 7},
            "after": {"ok": True, "network_requests": 7},
            "before_count": 7,
            "after_count": 7,
            "delta": 0,
            "samples_valid": True,
            "unchanged": True,
        },
        "formats_sd": False,
        "skip_data_canary": False,
        "failure_count": 0,
        "checks": {
            "tab_switches_settle": True,
            "data_refresh_exercised": True,
            "data_refreshes_pass": True,
            "no_public_rf": True,
            "no_map_network_requests": True,
            "no_formatting": True,
            "no_stuck_pending": True,
            "final_active_tab_known": True,
        },
        "telemetry": {
            "health_sample_count": 180,
            "uptime_monotonic": True,
            "final_active_tab": "messages",
            "final_pending": False,
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
        "reference_png_path": "artifacts/ui-sim-reference/68350bf-actions28549761003/home.png",
        "reference_view": "home",
        "simulator_diff_path": "artifacts/hardware/com12/ui_pixel_capture_68350bf_simdiff.json",
        "simulator_diff_ok": True,
        "simulator_diff": {
            "schema": 1,
            "kind": "ui_capture_simulator_diff",
            "ok": True,
            "capture_png_path": "artifacts/hardware/com12/ui_pixel_capture_68350bf.png",
            "reference_png_path": "artifacts/ui-sim-reference/68350bf-actions28549761003/home.png",
            "reference_view": "home",
            "width": 480,
            "height": 480,
            "reference_width": 480,
            "reference_height": 480,
            "size_match": True,
            "different_pixel_ratio": 0.08,
            "mean_abs_delta": 12.0,
            "tile_mismatch_ratio": 0.04,
            "public_rf_tx": False,
            "formats_sd": False,
        },
        "onboarding_visible": False,
        "public_rf_tx": False,
        "formats_sd": False,
    }
    payload.update(overrides)
    return payload


def compose_keyboard_capture_payload(**overrides: object) -> dict:
    def capture(target: str) -> dict:
        probe_target = target.replace("-", "_")
        onboarding_visible = target == "onboarding"
        active_tab = {
            "packet-search": "packets",
            "contact-edit": "nodes",
            "onboarding": "home",
            "map-location": "map",
            "wifi-ssid": "settings",
            "wifi-password": "settings",
        }.get(target, "messages")
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
                "onboarding_visible": onboarding_visible,
                "dock_hidden": True,
                "dm_mode": target.startswith("dm"),
                "active_tab": active_tab,
                "sheet": {"x": 0, "y": 56, "w": 480, "h": 424},
                "textarea": {"x": 16, "y": 58, "w": 448, "h": 78},
                "keyboard": {"x": 16, "y": 158, "w": 448, "h": 258},
                "public_rf_tx": False,
                "formats_sd": False,
            },
            "capture": ui_pixel_capture_payload(onboarding_visible=onboarding_visible),
            "png_path": f"artifacts/hardware/com12/ui_compose_{target}.png",
            "raw_path": f"artifacts/hardware/com12/ui_compose_{target}.rgb565",
            "target_visible": True,
            "public_rf_tx": False,
            "network_tx": False,
            "map_network_requests": False,
            "formats_sd": False,
        }

    payload = {
        "ok": True,
        "kind": "ui_compose_keyboard_capture",
        "mode": "hardware",
        "port": "COM12",
        "targets": COMPOSE_CAPTURE_TARGETS,
        "captures": [capture(target) for target in COMPOSE_CAPTURE_TARGETS],
        "capture_count": len(COMPOSE_CAPTURE_TARGETS),
        "public_rf_tx": False,
        "network_tx": False,
        "map_network_requests": False,
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
        "read_only": True,
        "network_tx": False,
        "map_network_requests": False,
        "map_network_evidence": {
            "command": "map tiles status",
            "measured": True,
            "before": {"ok": True, "network_requests": 11},
            "after": {"ok": True, "network_requests": 11},
            "before_count": 11,
            "after_count": 11,
            "delta": 0,
            "samples_valid": True,
            "unchanged": True,
        },
        "background_download": False,
        "area_download": False,
        "visible_tile_limit": 9,
        "zoom_batch_limit": 1,
        "wifi_mutation": False,
        "storage_mutation": False,
        "events": [
            {"screen": screen, "tab": tab, "probe": probe_results[screen]}
            for screen, tab in SCROLL_SURFACES.items()
        ],
    }
    payload.update(overrides)
    return payload


def write_json(path: Path, payload: dict) -> None:
    payload = dict(payload)
    if "artifacts" in {part.lower() for part in path.parts} and not audit.artifact_commit_values(payload):
        lowered_name = path.name.lower()
        if COMMIT[:7] in lowered_name:
            payload["commit"] = COMMIT
        elif STALE_COMMIT[:7] in lowered_name:
            payload["commit"] = STALE_COMMIT
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


def write_manifest_file(directory: Path, name: str, payload: bytes = b"ok") -> None:
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / name
    target.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    (directory / "SHA256SUMS.txt").write_text(f"{digest}  ./{name}\n", encoding="ascii")


def write_esp32_actions_artifact(run_dir: Path) -> dict:
    artifact = run_dir / "d1l-firmware-artifacts"
    build = artifact / "build"
    files = {
        "build/bootloader/bootloader.bin": b"BOOT",
        "build/partition_table/partition-table.bin": b"PART",
        "build/meshcore_deskos_d1l.bin": b"APP",
    }
    flasher = {
        "flash_files": {
            "0x0": "bootloader/bootloader.bin",
            "0x8000": "partition_table/partition-table.bin",
            "0x10000": "meshcore_deskos_d1l.bin",
        }
    }
    files["build/flasher_args.json"] = json.dumps(flasher, sort_keys=True).encode("utf-8")
    for relative, payload in files.items():
        target = artifact / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    rows = [
        f"{hashlib.sha256(payload).hexdigest()}  ./{relative}"
        for relative, payload in sorted(files.items())
    ]
    manifest = artifact / "SHA256SUMS.txt"
    manifest.write_text("\n".join(rows) + "\n", encoding="ascii")
    return {"artifact": artifact, "manifest": manifest, "flasher": flasher, "files": files}


def smoke_device_payload(commit: str = COMMIT, port: str = "COM12") -> dict:
    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "commit": commit,
        "expected_firmware_commit": commit,
        "device_build_commit": commit,
        "firmware_identity_required": True,
        "firmware_identity_ok": True,
        "ok": True,
        "results": [
            {"schema": 1, "ok": True, "cmd": "version", "build_commit": commit}
        ],
    }


def write_esp32_flash_receipt(
    root: Path,
    run_dir: Path,
    *,
    commit: str = COMMIT,
    run_id: str = RUN_ID,
    port: str = "COM12",
) -> Path:
    artifact = run_dir / "d1l-firmware-artifacts"
    manifest = artifact / "SHA256SUMS.txt"
    manifest_entries = audit.checksum_manifest_entries(manifest)
    flasher_path = artifact / "build" / "flasher_args.json"
    flasher = json.loads(flasher_path.read_text(encoding="utf-8"))
    flash_rows = []
    command = ["python", "-m", "esptool", "--chip", "esp32s3", "-p", port, "write-flash"]
    for raw_offset, relative in sorted(
        flasher["flash_files"].items(), key=lambda item: int(item[0], 0)
    ):
        offset = int(raw_offset, 0)
        artifact_relative = f"build/{relative}"
        target = artifact / artifact_relative
        flash_rows.append(
            {
                "offset": offset,
                "offset_hex": hex(offset),
                "path": artifact_relative,
                "size": target.stat().st_size,
                "sha256": manifest_entries[artifact_relative].upper(),
            }
        )
        command.extend([hex(offset), str(target)])
    payload = {
        "schema": 1,
        "kind": "esp32_flash",
        "mode": "hardware",
        "port": port,
        "commit": commit,
        "github_actions_run": run_id,
        "public_rf_tx": False,
        "formats_sd": False,
        "artifact_verification": {
            "ok": True,
            "manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest().upper(),
            "manifest_entry_count": len(manifest_entries),
            "manifest_complete": True,
            "flasher_args_sha256": hashlib.sha256(flasher_path.read_bytes()).hexdigest().upper(),
            "flash_files": flash_rows,
        },
        "command": command,
        "result": {
            "name": "esp32_flash",
            "ok": True,
            "returncode": 0,
            "args": command,
        },
        "flashed_at": datetime.now(timezone.utc).isoformat(),
        "ok": True,
    }
    path = (
        root
        / "artifacts"
        / "hardware"
        / port.lower()
        / f"esp32_flash_{commit[:7]}_actions_{run_id}_{port}.json"
    )
    write_json(path, payload)
    return path


def write_supported_sdk_lock(
    root: Path,
    version: str = audit.SUPPORTED_ESP_IDF_LOCK_VERSION,
) -> None:
    (root / "dependencies.lock").write_text(
        "dependencies:\n"
        "  idf:\n"
        "    source:\n"
        "      type: idf\n"
        f"    version: {version}\n"
        "manifest_hash: test\n"
        "target: esp32s3\n"
        "version: 2.0.0\n",
        encoding="utf-8",
    )


def write_supported_sdk_workflow(root: Path, image: str = audit.SUPPORTED_ESP_IDF_IMAGE) -> None:
    workflow = root / ".github" / "workflows" / "d1l-ci.yml"
    workflow.parent.mkdir(parents=True, exist_ok=True)
    workflow.write_text(
        "name: d1l-ci\n"
        "jobs:\n"
        "  firmware-build:\n"
        "    runs-on: ubuntu-latest\n"
        f"    container: {image}\n"
        "    steps: []\n",
        encoding="utf-8",
    )
    write_supported_sdk_lock(root)


def meshcore_conformance_payload(
    commit: str = COMMIT,
    *,
    generated_at: datetime | None = None,
    **overrides: object,
) -> dict:
    payload = {
        "schema_version": 1,
        "artifact_type": audit.MESHCORE_CONFORMANCE_ARTIFACT_TYPE,
        "generated_at": (generated_at or datetime.now(timezone.utc)).isoformat().replace("+00:00", "Z"),
        "passed": True,
        "status": "pass",
        "execution_complete": True,
        "coverage_boundary": audit.MESHCORE_CONFORMANCE_BOUNDARY,
        "coverage_level": audit.MESHCORE_CONFORMANCE_BOUNDARY,
        "closure_ready": False,
        "issue_65_closure_eligible": False,
        "source_verification": {"repository_commit": commit},
    }
    payload.update(overrides)
    return payload


def write_release_package(
    run_dir: Path,
    *,
    commit: str = COMMIT,
    conformance: dict | None = None,
    workflow_run_id: str = RUN_ID,
    git_dirty: bool = False,
) -> Path:
    package = run_dir / "d1l-release-package" / f"d1l-release-{COMMIT}"
    package.mkdir(parents=True, exist_ok=True)
    (package / "README_RELEASE.md").write_bytes(b"release")
    notices = {
        "notices/LICENSE": b"project license",
        "notices/THIRD_PARTY_NOTICES.md": b"third party",
        "notices/ATTRIBUTIONS.md": b"attributions",
        "notices/SOURCE_AUDIT_AND_ATTRIBUTION.md": b"source audit",
    }
    for relative, payload in notices.items():
        target = package / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    report = conformance or meshcore_conformance_payload(commit)
    generated_at = datetime.fromisoformat(report["generated_at"].replace("Z", "+00:00"))
    expires_at = generated_at + timedelta(days=audit.MESHCORE_CONFORMANCE_MAX_AGE_DAYS)
    evidence_relative = f"evidence/meshcore_conformance_{commit}.json"
    evidence_path = package / evidence_relative
    write_json(evidence_path, report)
    evidence_bytes = evidence_path.read_bytes()
    write_json(
        package / "manifest.json",
        {
            "git": {
                "commit": commit,
                "dirty": git_dirty,
                "dirty_entries": [" M main/example.c"] if git_dirty else [],
            },
            "workflow": {"sha": commit, "run_id": workflow_run_id},
            "notice_files": [{"path": relative} for relative in notices],
            "meshcore_conformance": {
                "artifact_type": audit.MESHCORE_CONFORMANCE_ARTIFACT_TYPE,
                "path": evidence_relative,
                "size": len(evidence_bytes),
                "sha256": hashlib.sha256(evidence_bytes).hexdigest(),
                "source_commit": commit,
                "generated_at": generated_at.astimezone(timezone.utc).isoformat().replace("+00:00", "Z"),
                "expires_at": expires_at.astimezone(timezone.utc).isoformat().replace("+00:00", "Z"),
                "max_age_days": audit.MESHCORE_CONFORMANCE_MAX_AGE_DAYS,
                "coverage_boundary": audit.MESHCORE_CONFORMANCE_BOUNDARY,
                "coverage_level": audit.MESHCORE_CONFORMANCE_BOUNDARY,
                "closure_ready": False,
                "issue_65_closure_eligible": False,
                "passed": True,
                "execution_complete": True,
            },
        },
    )
    rows = []
    for path in sorted(package.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS.txt":
            relative = path.relative_to(package).as_posix()
            rows.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  ./{relative}")
    (package / "SHA256SUMS.txt").write_text("\n".join(rows) + "\n", encoding="ascii")
    return package


def write_host_checks_success(
    run_dir: Path,
    *,
    commit: str = COMMIT,
    run_id: str = RUN_ID,
) -> Path:
    marker = (
        run_dir
        / "d1l-host-artifacts"
        / "host-checks"
        / f"d1l_host_checks_success_{commit}.json"
    )
    write_json(
        marker,
        {
            "schema": 1,
            "artifact_type": audit.HOST_CHECKS_ARTIFACT_TYPE,
            "status": "pass",
            "passed": True,
            "all_prior_steps_completed": True,
            "job": "host-checks",
            "repository_commit": commit,
            "workflow_run_id": run_id,
            "workflow_run_attempt": "1",
        },
    )
    return marker


def write_core_evidence(root: Path) -> None:
    write_supported_sdk_workflow(root)
    run_dir = root / "artifacts" / "github" / RUN_ID
    write_host_checks_success(run_dir)
    write_esp32_actions_artifact(run_dir)
    write_manifest_file(run_dir / "rp2040-sd-bridge-firmware", "deskos_sd_bridge.ino.uf2", b"uf2")
    write_manifest_file(
        run_dir / "rp2040-seeed-official-sd-smoke-firmware",
        "seeed_official_sd_smoke.ino.uf2",
        b"official-smoke-uf2",
    )
    write_release_package(run_dir)

    hardware = root / "artifacts" / "hardware" / "com12"
    write_esp32_flash_receipt(root, run_dir)
    write_json(hardware / "smoke_68350bf.json", smoke_device_payload())
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


def write_esp32_only_actions_package(root: Path) -> None:
    run_dir = root / "artifacts" / "github" / RUN_ID
    write_manifest_file(run_dir / "d1l-firmware-artifacts", "firmware.bin", b"firmware")
    write_release_package(run_dir)


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
        "manager": {"running": True, "state": "READY_SD"},
        "sd": {
            "state": "ready",
            "filesystem": "fat32",
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
            "storage_status": ready_storage_status(),
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
        root / "artifacts" / "hardware" / "com12" / f"rp2040_raw_diag_{commit[:7]}_COM12.json",
        {
            "schema": 1,
            "ok": True,
            "port": "COM12",
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
            "storage_before": ready_storage_status(),
            "storage_after": ready_storage_status(),
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
            "reboot_command_passed": True,
            "reboot_route_flush": "ESP_OK",
            "pre_reboot_boot_nonce": 111,
            "post_reboot_boot_nonce": 222,
            "reboot_proven": True,
            "storage_file_gate_ready_before": True,
            "storage_file_gate_ready_after": True,
            "retained_history_sd_ready_before": True,
            "retained_history_sd_ready_after": True,
            "storage_before": ready_storage_status(),
            "storage_after": ready_storage_status(),
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
            "pre_remount_command_passed": True,
            "post_remount_command_passed": True,
            "retained_history_sd_ready_before": True,
            "retained_history_sd_ready_after": True,
            "retained_canary_passed": True,
            "pre_reboot_readbacks_ok": True,
            "post_reboot_readbacks_ok": True,
            "reboot_command_passed": True,
            "reboot_route_flush": "ESP_OK",
            "pre_reboot_boot_nonce": 111,
            "post_reboot_boot_nonce": 222,
            "reboot_proven": True,
            "pre_map_tile_canary_passed": True,
            "post_map_tile_canary_passed": True,
            "storage_before_reboot": ready_storage_status(),
            "storage_after_reboot": ready_storage_status(),
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
            "storage_before": ready_storage_status(),
            "storage_after": ready_storage_status(),
            "commands": ["storage status", "storage map-tile-canary map1", "storage status", "health"],
            **({"firmware_commit": metadata_commit} if metadata_commit else {}),
        },
    )


def write_export_evidence(root: Path, commit: str = COMMIT, metadata_commit: str | None = None) -> None:
    common = {
        "schema": 1,
        "mode": "hardware",
        "port": "COM12",
        "public_rf_tx": False,
        "formats_sd": False,
        "allow_unavailable": False,
        "ok": True,
        "storage_file_gate_ready_before": True,
        "storage_file_gate_ready_after": True,
        "export_backend_ready_before": True,
        "export_backend_ready_after": True,
        "storage_before": ready_storage_status(),
        "storage_after": ready_storage_status(),
        **({"firmware_commit": metadata_commit} if metadata_commit else {}),
    }
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_export_canary_{commit[:7]}.json",
        {
            **common,
            "canary_passed": True,
            "canary_unavailable_ok": False,
            "canary": {"write_tmp": True, "read_tmp": True, "rename_replace": True, "read_final": True},
            "commands": ["storage status", "storage export-canary ex1", "storage status", "health"],
        },
    )
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_diagnostic_export_{commit[:7]}.json",
        {
            **common,
            "diagnostic_export_passed": True,
            "diagnostic_export_unavailable_ok": False,
            "export": {"bytes": 512, "chunks_written": 3, "final_verified_bytes": 512},
            "commands": ["storage status", "storage export-diagnostics diag1", "storage status", "health", "crashlog"],
        },
    )
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"sd_data_export_{commit[:7]}.json",
        {
            **common,
            "data_export_passed": True,
            "data_export_unavailable_ok": False,
            "export": {
                "bytes": 512,
                "chunks_written": 3,
                "final_verified_bytes": 512,
                "private_identity_exported": False,
                "sampled": True,
            },
            "commands": ["storage status", "storage export-data data1", "storage status", "health"],
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
    write_export_evidence(root, commit, metadata_commit)
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


def test_artifact_commit_matching_requires_consistent_canonical_metadata(tmp_path: Path):
    path = tmp_path / f"artifact_{COMMIT[:7]}.json"

    assert audit.artifact_commit_matches(path, {"commit": COMMIT}, COMMIT) is True
    assert audit.artifact_commit_matches(
        path,
        {"commit": COMMIT[:12], "git": {"commit": COMMIT}},
        COMMIT,
    ) is True
    assert audit.artifact_commit_matches(
        path,
        {"commit": COMMIT, "firmware_commit": STALE_COMMIT},
        COMMIT,
    ) is False
    assert audit.artifact_commit_matches(
        path,
        {"commit": COMMIT[:7] + "-actions"},
        COMMIT,
    ) is False
    assert audit.artifact_commit_matches(path, {}, COMMIT) is False


def test_release_gate_audit_passes_proven_core_gates(tmp_path: Path):
    write_core_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["supported_sdk_baseline"]["ok"] is True
    assert gates["same_run_host_checks_passed"]["ok"] is True
    assert gates["supported_sdk_baseline"]["details"]["expected_image"] == "espressif/idf:v5.5.4"
    assert gates["ci_artifacts_checksums"]["ok"] is True
    assert gates["meshcore_wire_conformance_packaged"]["ok"] is True
    assert gates["meshcore_wire_conformance_packaged"]["details"]["closure_ready"] is False
    assert gates["meshcore_wire_conformance_packaged"]["details"]["issue_65_closure_eligible"] is False
    assert gates["meshcore_full_conformance_complete"]["ok"] is False
    assert gates["meshcore_full_conformance_complete"]["severity"] == "P0"
    assert gates["meshcore_full_conformance_complete"]["details"][
        "wire_envelope_evidence_is_sufficient"
    ] is False
    assert gates["release_notices_included"]["ok"] is True
    assert gates["exact_actions_esp32_flash"]["ok"] is True
    assert gates["com12_smoke"]["ok"] is True
    assert gates["ui_corruption_probe"]["ok"] is True
    assert gates["ui_pixel_capture"]["ok"] is True
    assert gates["ui_compose_keyboard_capture"]["ok"] is True
    assert gates["ui_scroll_probe"]["ok"] is True
    assert gates["outbound_dm_com11"]["ok"] is True
    assert gates["sd_official_seeed_smoke_passed"]["ok"] is False
    assert gates["docs_current_evidence"]["ok"] is True


def test_flash_receipt_gate_fails_when_skip_flash_leaves_no_receipt(tmp_path: Path):
    write_core_evidence(tmp_path)
    receipt = next(
        (tmp_path / "artifacts" / "hardware" / "com12").glob("esp32_flash_*.json")
    )
    receipt.unlink()

    gate = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]

    assert gate["ok"] is False
    assert "flash_receipt_missing" in gate["details"]["failures"]


def test_flash_receipt_gate_rejects_stale_commit_and_tampered_hash(tmp_path: Path):
    write_core_evidence(tmp_path)
    receipt = next(
        (tmp_path / "artifacts" / "hardware" / "com12").glob("esp32_flash_*.json")
    )
    payload = json.loads(receipt.read_text(encoding="utf-8"))
    payload["commit"] = STALE_COMMIT
    write_json(receipt, payload)
    stale = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]
    assert stale["ok"] is False
    assert "flash_receipt_missing" in stale["details"]["failures"]

    write_core_evidence(tmp_path)
    receipt = next(
        (tmp_path / "artifacts" / "hardware" / "com12").glob("esp32_flash_*.json")
    )
    payload = json.loads(receipt.read_text(encoding="utf-8"))
    payload["artifact_verification"]["flash_files"][0]["sha256"] = "0" * 64
    write_json(receipt, payload)
    tampered = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]
    assert tampered["ok"] is False
    assert "flash_files_exact" in tampered["details"]["failures"]


@pytest.mark.parametrize(
    ("field", "value", "failure"),
    [
        ("github_actions_run", "999999", "receipt_run_id"),
        ("port", "COM9", "receipt_port"),
    ],
)
def test_flash_receipt_gate_rejects_wrong_run_or_port(
    tmp_path: Path, field: str, value: str, failure: str
):
    write_core_evidence(tmp_path)
    receipt = next(
        (tmp_path / "artifacts" / "hardware" / "com12").glob("esp32_flash_*.json")
    )
    payload = json.loads(receipt.read_text(encoding="utf-8"))
    payload[field] = value
    write_json(receipt, payload)

    gate = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]

    assert gate["ok"] is False
    assert failure in gate["details"]["failures"]


def test_flash_receipt_gate_rejects_command_file_outside_selected_artifact(
    tmp_path: Path,
):
    write_core_evidence(tmp_path)
    run_dir = tmp_path / "artifacts" / "github" / RUN_ID
    artifact = run_dir / "d1l-firmware-artifacts"
    receipt = next(
        (tmp_path / "artifacts" / "hardware" / "com12").glob("esp32_flash_*.json")
    )
    payload = json.loads(receipt.read_text(encoding="utf-8"))
    expected_app = artifact / "build" / "meshcore_deskos_d1l.bin"
    outside_app = tmp_path / "evil" / "build" / "meshcore_deskos_d1l.bin"
    outside_app.parent.mkdir(parents=True)
    outside_app.write_bytes(expected_app.read_bytes())
    command = list(payload["command"])
    command[command.index(str(expected_app))] = str(outside_app)
    payload["command"] = command
    payload["result"]["args"] = command
    write_json(receipt, payload)

    gate = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]

    assert gate["ok"] is False
    assert "flash_command_files_exact" in gate["details"]["failures"]


def test_flash_receipt_gate_rejects_extra_write_flash_pair(tmp_path: Path):
    write_core_evidence(tmp_path)
    receipt = next(
        (tmp_path / "artifacts" / "hardware" / "com12").glob("esp32_flash_*.json")
    )
    payload = json.loads(receipt.read_text(encoding="utf-8"))
    nvs_image = tmp_path / "unexpected-nvs.bin"
    nvs_image.write_bytes(b"must not be flashed")
    command = [*payload["command"], "0x9000", str(nvs_image)]
    payload["command"] = command
    payload["result"]["args"] = command
    write_json(receipt, payload)

    gate = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]

    assert gate["ok"] is False
    assert "flash_command_files_exact" in gate["details"]["failures"]


def test_flash_receipt_gate_requires_known_d1l_flash_roles(tmp_path: Path):
    write_core_evidence(tmp_path)
    run_dir = tmp_path / "artifacts" / "github" / RUN_ID
    artifact = run_dir / "d1l-firmware-artifacts"
    flasher_path = artifact / "build" / "flasher_args.json"
    flasher = json.loads(flasher_path.read_text(encoding="utf-8"))
    flasher["flash_files"].pop("0x10000")
    flasher_path.write_text(json.dumps(flasher, sort_keys=True), encoding="utf-8")
    manifest = artifact / "SHA256SUMS.txt"
    manifest.write_text(
        "\n".join(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  ./"
            f"{path.relative_to(artifact).as_posix()}"
            for path in sorted(artifact.rglob("*"))
            if path.is_file() and path != manifest
        )
        + "\n",
        encoding="ascii",
    )
    write_esp32_flash_receipt(tmp_path, run_dir)

    gate = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]

    assert gate["ok"] is False
    assert gate["details"]["failures"] == ["required_flash_roles"]


def test_flash_receipt_gate_rejects_checksummed_extra_flasher_role(tmp_path: Path):
    write_core_evidence(tmp_path)
    run_dir = tmp_path / "artifacts" / "github" / RUN_ID
    artifact = run_dir / "d1l-firmware-artifacts"
    flasher_path = artifact / "build" / "flasher_args.json"
    flasher = json.loads(flasher_path.read_text(encoding="utf-8"))
    extra = artifact / "build" / "unexpected-nvs.bin"
    extra.write_bytes(b"must not be flashed")
    flasher["flash_files"]["0x9000"] = "unexpected-nvs.bin"
    flasher_path.write_text(json.dumps(flasher, sort_keys=True), encoding="utf-8")
    manifest = artifact / "SHA256SUMS.txt"
    manifest.write_text(
        "\n".join(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  ./"
            f"{path.relative_to(artifact).as_posix()}"
            for path in sorted(artifact.rglob("*"))
            if path.is_file() and path != manifest
        )
        + "\n",
        encoding="ascii",
    )
    write_esp32_flash_receipt(tmp_path, run_dir)

    gate = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]

    assert gate["ok"] is False
    assert gate["details"]["failures"] == ["required_flash_roles"]


def test_flash_receipt_gate_rejects_flasher_path_escape(tmp_path: Path):
    write_core_evidence(tmp_path)
    run_dir = tmp_path / "artifacts" / "github" / RUN_ID
    artifact = run_dir / "d1l-firmware-artifacts"
    flasher_path = artifact / "build" / "flasher_args.json"
    flasher = json.loads(flasher_path.read_text(encoding="utf-8"))
    outside_app = run_dir / "outside-app.bin"
    outside_app.write_bytes(b"APP")
    flasher["flash_files"]["0x10000"] = "../../outside-app.bin"
    flasher_path.write_text(json.dumps(flasher, sort_keys=True), encoding="utf-8")

    gate = gate_by_id(build_audit(audit_args(tmp_path)))["exact_actions_esp32_flash"]

    assert gate["ok"] is False
    assert "flasher_flash_files_invalid" in gate["details"]["failures"]


def test_com12_smoke_rejects_wrong_device_build_commit(tmp_path: Path):
    write_core_evidence(tmp_path)
    smoke = tmp_path / "artifacts" / "hardware" / "com12" / "smoke_68350bf.json"
    payload = json.loads(smoke.read_text(encoding="utf-8"))
    payload["device_build_commit"] = STALE_COMMIT
    payload["results"][0]["build_commit"] = STALE_COMMIT
    payload["firmware_identity_ok"] = True
    write_json(smoke, payload)

    gate = gate_by_id(build_audit(audit_args(tmp_path)))["com12_smoke"]

    assert gate["ok"] is False
    assert gate["details"]["path_found"] is True


def test_meshcore_packaged_evidence_gate_rejects_mismatch_expiry_and_tampering(
    tmp_path: Path, monkeypatch
):
    valid_run = tmp_path / "valid"
    valid_package = write_release_package(valid_run)
    valid = audit.meshcore_conformance_evidence_gate(
        valid_run, tmp_path, COMMIT, expected_run_id=RUN_ID
    ).to_dict()

    assert valid["ok"] is True
    assert valid["details"]["coverage_level"] == "wire_envelope_only"
    assert valid["details"]["closure_ready"] is False
    assert "does not close issue #65" in valid["message"]

    mismatched_run = tmp_path / "mismatched"
    write_release_package(mismatched_run, commit=STALE_COMMIT)
    mismatched = audit.meshcore_conformance_evidence_gate(
        mismatched_run, tmp_path, COMMIT, expected_run_id=RUN_ID
    ).to_dict()
    assert mismatched["ok"] is False
    assert "metadata_source_commit" in mismatched["details"]["failures"]
    assert "report_source_commit" in mismatched["details"]["failures"]

    expired_run = tmp_path / "expired"
    expired_at = datetime.now(timezone.utc) - timedelta(
        days=audit.MESHCORE_CONFORMANCE_MAX_AGE_DAYS + 1
    )
    write_release_package(
        expired_run,
        conformance=meshcore_conformance_payload(generated_at=expired_at),
    )
    expired = audit.meshcore_conformance_evidence_gate(
        expired_run, tmp_path, COMMIT, expected_run_id=RUN_ID
    ).to_dict()
    assert expired["ok"] is False
    assert "evidence_not_expired" in expired["details"]["failures"]

    evidence = valid_package / f"evidence/meshcore_conformance_{COMMIT}.json"
    evidence.write_text(evidence.read_text(encoding="utf-8") + "\n", encoding="utf-8")
    tampered = audit.meshcore_conformance_evidence_gate(
        valid_run, tmp_path, COMMIT, expected_run_id=RUN_ID
    ).to_dict()
    assert tampered["ok"] is False
    assert "evidence_sha256_mismatch" in tampered["details"]["failures"]
    assert "evidence_size_mismatch" in tampered["details"]["failures"]

    wrong_run_dir = tmp_path / "wrong-run"
    write_release_package(wrong_run_dir, workflow_run_id="999999")
    wrong_run = audit.meshcore_conformance_evidence_gate(
        wrong_run_dir,
        tmp_path,
        COMMIT,
        expected_run_id=RUN_ID,
    ).to_dict()
    assert wrong_run["ok"] is False
    assert "package_workflow_run_id" in wrong_run["details"]["failures"]

    dirty_run_dir = tmp_path / "dirty-run"
    write_release_package(dirty_run_dir, git_dirty=True)
    dirty = audit.meshcore_conformance_evidence_gate(
        dirty_run_dir,
        tmp_path,
        COMMIT,
        expected_run_id=RUN_ID,
    ).to_dict()
    assert dirty["ok"] is False
    assert "package_git_clean" in dirty["details"]["failures"]

    overflow_run_dir = tmp_path / "overflow"
    overflow_package = write_release_package(overflow_run_dir)
    overflow_evidence = overflow_package / f"evidence/meshcore_conformance_{COMMIT}.json"
    overflow_report = json.loads(overflow_evidence.read_text(encoding="utf-8"))
    overflow_report["generated_at"] = "9999-12-31T00:00:00Z"
    write_json(overflow_evidence, overflow_report)
    overflow_manifest_path = overflow_package / "manifest.json"
    overflow_manifest = json.loads(overflow_manifest_path.read_text(encoding="utf-8"))
    overflow_metadata = overflow_manifest["meshcore_conformance"]
    overflow_metadata["generated_at"] = overflow_report["generated_at"]
    overflow_metadata["expires_at"] = "9999-12-31T23:59:59Z"
    overflow_metadata["size"] = overflow_evidence.stat().st_size
    overflow_metadata["sha256"] = hashlib.sha256(overflow_evidence.read_bytes()).hexdigest()
    write_json(overflow_manifest_path, overflow_manifest)
    overflow = audit.meshcore_conformance_evidence_gate(
        overflow_run_dir,
        tmp_path,
        COMMIT,
        expected_run_id=RUN_ID,
    ).to_dict()
    assert overflow["ok"] is False
    assert "expires_at_computable" in overflow["details"]["failures"]
    assert "generated_at_not_future" in overflow["details"]["failures"]

    unreadable_run_dir = tmp_path / "unreadable"
    write_release_package(unreadable_run_dir)

    def unreadable_hash(_path: Path) -> str:
        raise OSError("simulated evidence read failure")

    monkeypatch.setattr(audit, "sha256_file", unreadable_hash)
    unreadable = audit.meshcore_conformance_evidence_gate(
        unreadable_run_dir,
        tmp_path,
        COMMIT,
        expected_run_id=RUN_ID,
    ).to_dict()
    assert unreadable["ok"] is False
    assert "evidence_hash_unreadable" in unreadable["details"]["failures"]


def test_package_evidence_requires_explicit_expected_run_id(tmp_path: Path):
    run_dir = tmp_path / "run"
    write_release_package(run_dir)

    gate = audit.meshcore_conformance_evidence_gate(
        run_dir, tmp_path, COMMIT
    ).to_dict()

    assert gate["ok"] is False
    assert "expected_run_id_missing" in gate["details"]["failures"]
    assert "package_workflow_run_id" in gate["details"]["failures"]


def test_host_checks_gate_rejects_missing_or_cross_run_marker(tmp_path: Path):
    run_dir = tmp_path / "run"
    missing = audit.host_checks_success_gate(
        run_dir, tmp_path, COMMIT, RUN_ID
    ).to_dict()
    assert missing["ok"] is False
    assert "host_checks_marker_missing" in missing["details"]["failures"]

    marker = write_host_checks_success(run_dir, run_id="999999")
    cross_run = audit.host_checks_success_gate(
        run_dir, tmp_path, COMMIT, RUN_ID
    ).to_dict()
    assert marker.is_file()
    assert cross_run["ok"] is False
    assert "marker_run_id" in cross_run["details"]["failures"]


def test_full_meshcore_conformance_stays_fail_closed_after_wire_gate_passes():
    gate = audit.meshcore_full_conformance_gate().to_dict()

    assert gate["id"] == "meshcore_full_conformance_complete"
    assert gate["severity"] == "P0"
    assert gate["ok"] is False
    assert gate["details"]["issue"] == 65
    assert gate["details"]["closure_artifact_required"] is True
    assert gate["details"]["closure_artifact_present"] is False
    assert "wire-envelope evidence does not prove" in gate["message"]


def test_supported_sdk_gate_fails_closed_without_workflow_or_firmware_job(tmp_path: Path):
    missing = audit.supported_sdk_gate(tmp_path).to_dict()

    assert missing["ok"] is False
    assert missing["severity"] == "P0"
    assert missing["evidence"] == []
    assert missing["details"]["workflow_found"] is False
    assert missing["details"]["firmware_job_found"] is False

    workflow = tmp_path / ".github" / "workflows" / "d1l-ci.yml"
    workflow.parent.mkdir(parents=True, exist_ok=True)
    workflow.write_text("name: d1l-ci\njobs:\n  host-checks:\n    runs-on: windows-latest\n", encoding="utf-8")
    missing_job = audit.supported_sdk_gate(tmp_path).to_dict()

    assert missing_job["ok"] is False
    assert missing_job["details"]["workflow_found"] is True
    assert missing_job["details"]["firmware_job_found"] is False


def test_supported_sdk_gate_rejects_moving_eol_and_unapproved_tags(tmp_path: Path):
    for image, moving in (
        ("espressif/idf:release-v5.1", True),
        ("espressif/idf:release-v5.5", True),
        ("espressif/idf:latest", True),
        ("espressif/idf:v5.5.3", False),
    ):
        write_supported_sdk_workflow(tmp_path, image)
        gate = audit.supported_sdk_gate(tmp_path).to_dict()

        assert gate["ok"] is False
        assert gate["details"]["configured_images"] == [image]
        assert gate["details"]["moving_images"] == ([image] if moving else [])
        assert gate["details"]["exact_release_pin"] is False


def test_supported_sdk_gate_accepts_only_pinned_v5_5_4(tmp_path: Path):
    write_supported_sdk_workflow(tmp_path)

    gate = audit.supported_sdk_gate(tmp_path).to_dict()

    assert gate["ok"] is True
    assert gate["details"]["expected_version"] == "v5.5.4"
    assert gate["details"]["configured_images"] == ["espressif/idf:v5.5.4"]
    assert gate["details"]["moving_images"] == []
    assert gate["details"]["exact_release_pin"] is True
    assert gate["details"]["component_lock_found"] is True
    assert gate["details"]["expected_lock_version"] == "5.5.4"
    assert gate["details"]["locked_idf_version"] == "5.5.4"
    assert gate["details"]["exact_component_lock"] is True


def test_supported_sdk_gate_rejects_missing_malformed_or_stale_component_lock(tmp_path: Path):
    write_supported_sdk_workflow(tmp_path)
    component_lock = tmp_path / "dependencies.lock"

    component_lock.unlink()
    missing = audit.supported_sdk_gate(tmp_path).to_dict()
    assert missing["ok"] is False
    assert missing["details"]["component_lock_found"] is False
    assert missing["details"]["locked_idf_version"] is None

    component_lock.write_text(
        "dependencies:\n  idf:\n    source:\n      type: idf\n    version:\n",
        encoding="utf-8",
    )
    malformed = audit.supported_sdk_gate(tmp_path).to_dict()
    assert malformed["ok"] is False
    assert malformed["details"]["component_lock_found"] is True
    assert malformed["details"]["locked_idf_version"] is None

    write_supported_sdk_lock(tmp_path, "5.1.7")
    stale = audit.supported_sdk_gate(tmp_path).to_dict()
    assert stale["ok"] is False
    assert stale["details"]["locked_idf_version"] == "5.1.7"
    assert stale["details"]["exact_component_lock"] is False


def test_release_gate_checksum_allows_esp32_only_actions_package(tmp_path: Path):
    write_esp32_only_actions_package(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gate = gate_by_id(report)["ci_artifacts_checksums"]

    assert gate["ok"] is True
    assert gate["details"]["missing"] == []
    assert sorted(gate["details"]["optional_missing"]) == [
        f"artifacts/github/{RUN_ID}/rp2040-sd-bridge-firmware/SHA256SUMS.txt",
        f"artifacts/github/{RUN_ID}/rp2040-seeed-official-sd-smoke-firmware/SHA256SUMS.txt",
    ]


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
    assert report["p0_failed_count"] == 19


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
    assert report["p0_failed_count"] == 18


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
    assert report["p0_failed_count"] == 4


def test_release_gate_audit_rejects_stale_sd_status_even_when_cached_ready_fields_pass(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_official_seeed_smoke_evidence(tmp_path)
    write_ready_sd_preflight(tmp_path)
    write_strict_sd_evidence(tmp_path)

    canary_path = (
        tmp_path
        / "artifacts"
        / "hardware"
        / "com12"
        / f"sd_file_canary_{COMMIT[:7]}.json"
    )
    canary = json.loads(canary_path.read_text(encoding="utf-8"))
    canary["storage_after"]["sd"]["status_stale"] = True
    canary["storage_after"]["sd"]["refresh_failures"] = 3
    write_json(canary_path, canary)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["sd_filecanary_independent"]["ok"] is False
    assert audit.storage_file_gate_ready_status(canary["storage_after"]) is False


def test_release_gate_ready_status_rejects_unconfirmed_card_presence():
    status = ready_storage_status()
    assert audit.storage_file_gate_ready_status(status) is True

    status["sd"]["presence_stale"] = True
    assert audit.storage_file_gate_ready_status(status) is False

    status = ready_storage_status()
    status["sd"]["filesystem"] = "exfat"
    assert audit.storage_file_gate_ready_status(status) is False


def test_release_gate_audit_rejects_retained_readbacks_from_nvs_without_post_reboot_sd(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_official_seeed_smoke_evidence(tmp_path)
    write_ready_sd_preflight(tmp_path)
    write_strict_sd_evidence(tmp_path)

    retained_path = (
        tmp_path
        / "artifacts"
        / "hardware"
        / "com12"
        / f"sd_retained_history_{COMMIT[:7]}.json"
    )
    retained = json.loads(retained_path.read_text(encoding="utf-8"))
    retained["storage_after"] = {
        "ok": True,
        "data_backend": "nvs",
        "stores": {"messages": "nvs", "dm": "nvs", "routes": "nvs", "packets": "nvs"},
        "sd": {
            "state": "no_card",
            "present": False,
            "mounted": False,
            "data_root_ready": False,
            "rp2040_protocol_supported": True,
            "file_ops": False,
            "atomic_rename": False,
            "status_stale": False,
            "presence_stale": False,
            "refresh_failures": 0,
        },
    }
    # Keep the summary flag optimistic to prove the gate validates raw post-reboot status.
    retained["storage_file_gate_ready_after"] = True
    write_json(retained_path, retained)

    report = build_audit(audit_args(tmp_path))
    gate = gate_by_id(report)["sd_retained_canary_passed"]

    assert retained["pre_reboot_readbacks_ok"] is True
    assert retained["post_reboot_readbacks_ok"] is True
    assert audit.storage_file_gate_ready_status(retained["storage_after"]) is False
    assert gate["ok"] is False


def test_release_gate_audit_rejects_nvs_backends_behind_ready_file_gate(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_strict_sd_evidence(tmp_path)
    retained_path = (
        tmp_path
        / "artifacts"
        / "hardware"
        / "com12"
        / f"sd_retained_history_{COMMIT[:7]}.json"
    )
    retained = json.loads(retained_path.read_text(encoding="utf-8"))
    storage_after = ready_storage_status()
    storage_after.update(
        {
            "data_backend": "nvs",
            "message_store_backend": "nvs",
            "dm_store_backend": "nvs",
            "route_store_backend": "nvs",
            "packet_log_backend": "nvs",
            "stores": {
                "messages": "nvs",
                "dm": "nvs",
                "routes": "nvs",
                "packets": "nvs",
            },
        }
    )
    retained["storage_after"] = storage_after
    retained["storage_file_gate_ready_after"] = True
    retained["retained_history_sd_ready_after"] = True
    write_json(retained_path, retained)

    report = build_audit(audit_args(tmp_path))
    gate = gate_by_id(report)["sd_retained_canary_passed"]

    assert audit.storage_file_gate_ready_status(storage_after) is True
    assert audit.retained_history_sd_ready_status(storage_after) is False
    assert gate["ok"] is False


def test_release_gate_audit_rejects_failed_remount_summary(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_strict_sd_evidence(tmp_path)
    remount_path = (
        tmp_path
        / "artifacts"
        / "hardware"
        / "com12"
        / f"sd_reboot_remount_{COMMIT[:7]}.json"
    )
    remount = json.loads(remount_path.read_text(encoding="utf-8"))
    remount["post_remount_command_passed"] = False
    write_json(remount_path, remount)

    report = build_audit(audit_args(tmp_path))
    gate = gate_by_id(report)["sd_reboot_remount_passed"]

    assert gate["ok"] is False
    assert gate["details"]["post_remount_command_passed"] is False


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
    assert report["p0_failed_count"] == 19


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


def test_release_gate_audit_requires_ui_corruption_probe_to_finish_not_pending(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = ui_corruption_probe_payload()
    payload["checks"]["no_stuck_pending"] = False
    payload["telemetry"]["final_pending"] = True
    write_json(hardware / "ui_corruption_probe_68350bf.json", payload)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_corruption_probe"]["ok"] is False


def test_release_gate_audit_rejects_corruption_probe_when_map_network_counter_grows(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = ui_corruption_probe_payload()
    payload["map_network_evidence"].update(
        {
            "after": {"ok": True, "network_requests": 8},
            "after_count": 8,
            "delta": 1,
            "unchanged": False,
        }
    )
    write_json(hardware / "ui_corruption_probe_68350bf.json", payload)

    report = build_audit(audit_args(tmp_path))
    assert gate_by_id(report)["ui_corruption_probe"]["ok"] is False


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


def test_release_gate_audit_requires_simulator_diff_for_pixel_capture(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = ui_pixel_capture_payload(simulator_diff_ok=False)
    payload["simulator_diff"]["ok"] = False
    payload["simulator_diff"]["error"] = "material_pixel_difference"
    write_json(hardware / "ui_pixel_capture_68350bf.json", payload)

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


def test_release_gate_audit_rejects_onboarding_covered_compose_capture(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = compose_keyboard_capture_payload()
    payload["captures"][0]["compose_probe"]["onboarding_visible"] = True
    payload["captures"][0]["capture"]["onboarding_visible"] = True
    payload["captures"][0]["target_visible"] = False
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


def test_release_gate_audit_rejects_map_probe_network_or_area_download(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = scroll_probe_payload(
        network_tx=True,
        map_network_requests=True,
        area_download=True,
    )
    write_json(hardware / "scroll_probe_68350bf.json", payload)

    report = build_audit(audit_args(tmp_path))
    assert gate_by_id(report)["ui_scroll_probe"]["ok"] is False


def test_release_gate_audit_rejects_scroll_probe_when_map_network_counter_grows(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = scroll_probe_payload()
    payload["map_network_evidence"].update(
        {
            "after": {"ok": True, "network_requests": 12},
            "after_count": 12,
            "delta": 1,
            "unchanged": False,
        }
    )
    write_json(hardware / "scroll_probe_68350bf.json", payload)

    report = build_audit(audit_args(tmp_path))
    assert gate_by_id(report)["ui_scroll_probe"]["ok"] is False


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


def test_release_gate_audit_allows_static_home_launcher_scroll_probe(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    payload = scroll_probe_payload()
    payload["probe_results"]["home"]["ok"] = False
    payload["probe_results"]["home"]["moved"] = False
    payload["probe_results"]["home"]["after_y"] = 0
    payload["probe_results"]["home"]["scroll_bottom_before"] = 0
    payload["probe_results"]["home"]["scroll_bottom_after"] = 0
    for event in payload["events"]:
        if event["screen"] == "home":
            event["probe"] = payload["probe_results"]["home"]
    write_json(hardware / "scroll_probe_68350bf.json", payload)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_scroll_probe"]["ok"] is True


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
    (hardware / "ui_compose_keyboard_capture_68350bf.json").unlink()
    (hardware / "scroll_probe_68350bf.json").unlink()

    write_json(
        tmp_path / "artifacts" / "smoke" / "d1l-smoke-COM12-actions-68350bf.json",
        smoke_device_payload(),
    )
    write_json(
        tmp_path / "artifacts" / "ui-corruption-probe" / "d1l-ui-corruption-probe-COM12-actions-68350bf.json",
        ui_corruption_probe_payload(firmware_commit=COMMIT),
    )
    write_json(
        tmp_path / "artifacts" / "scroll-probe" / "d1l-scroll-probe-COM12-actions-68350bf.json",
        scroll_probe_payload(firmware_commit=COMMIT),
    )
    write_json(
        tmp_path / "artifacts" / "ui-capture" / "d1l-compose-keyboard-capture-COM12-actions-68350bf.json",
        compose_keyboard_capture_payload(firmware_commit=COMMIT),
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
    assert gates["ui_compose_keyboard_capture"]["ok"] is True
    assert gates["ui_compose_keyboard_capture"]["evidence"] == [
        "artifacts/ui-capture/d1l-compose-keyboard-capture-COM12-actions-68350bf.json"
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


def test_release_gate_audit_rejects_unchanged_reboot_nonce(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_strict_sd_evidence(tmp_path)
    evidence = next(
        (tmp_path / "artifacts" / "hardware" / "com12").glob("sd_reboot_remount_*.json")
    )
    payload = json.loads(evidence.read_text(encoding="utf-8"))
    payload["post_reboot_boot_nonce"] = payload["pre_reboot_boot_nonce"]
    payload["reboot_proven"] = True
    write_json(evidence, payload)

    report = build_audit(audit_args(tmp_path))

    gate = gate_by_id(report)["sd_reboot_remount_passed"]
    assert gate["ok"] is False
    assert gate["details"]["pre_reboot_boot_nonce"] == 111
    assert gate["details"]["post_reboot_boot_nonce"] == 111
    assert gate["details"]["reboot_proven"] is True


def test_release_gate_audit_rejects_missing_route_flush_proof(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_strict_sd_evidence(tmp_path)
    evidence = next(
        (tmp_path / "artifacts" / "hardware" / "com12").glob(
            "sd_reboot_remount_*.json"
        )
    )
    payload = json.loads(evidence.read_text(encoding="utf-8"))
    payload.pop("reboot_route_flush")
    write_json(evidence, payload)

    report = build_audit(audit_args(tmp_path))

    gate = gate_by_id(report)["sd_reboot_remount_passed"]
    assert gate["ok"] is False
    assert gate["details"]["reboot_route_flush"] is None
