import hashlib
import json
from pathlib import Path

from scripts.release_gate_audit_d1l import build_audit, parse_args


COMMIT = "68350bf9f3fabfd2db4110ec6ffc36068056a060"
STALE_COMMIT = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
RUN_ID = "28549761003"


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
    write_json(hardware / "ui_tab_abuse_68350bf.json", {"ok": True, "port": "COM12", "cycles": 100, "failure_count": 0})
    write_json(
        hardware / "scroll_probe_68350bf.json",
        {
            "ok": True,
            "port": "COM12",
            "failure_count": 0,
            "screens": ["messages", "nodes", "packets", "settings", "map"],
        },
    )
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
    for name in ("DESKOSFINAL.md", "ROADMAP.md", "RELEASE_CHECKLIST.md", "KNOWN_LIMITATIONS.md"):
        (root / "docs").mkdir(exist_ok=True)
        (root / "docs" / name).write_text(
            "release_gate_audit_d1l.py\nready_for_public_release=false\nNo release tag should be cut until\n",
            encoding="utf-8",
        )


def write_official_seeed_smoke_evidence(root: Path, commit: str = COMMIT) -> None:
    write_json(
        root / "artifacts" / "hardware" / "com12" / f"seeed_official_sd_smoke_{commit[:7]}.json",
        {
            "schema": 1,
            "kind": "seeed_official_sd_smoke_capture",
            "port": "COM12",
            "firmware_commit": commit,
            "public_rf_tx": False,
            "formats_sd": False,
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
    assert gates["ui_tab_abuse"]["ok"] is True
    assert gates["ui_scroll_probe"]["ok"] is True
    assert gates["outbound_dm_com11"]["ok"] is True
    assert gates["sd_official_seeed_smoke_passed"]["ok"] is False
    assert gates["docs_current_evidence"]["ok"] is True


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
    assert report["p0_failed_count"] == 5


def test_release_gate_audit_accepts_ready_no_format_sd_preflight(tmp_path: Path):
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
                "sd_state": "ready",
                "next_action": "run_sd_file_and_export_acceptance",
            },
            "artifact": {"sha256": "032ff80a0f94613bb18742e08cb97aa548bff882c3afacaf15f5c01"},
        },
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["sd_acceptance_matrix"]["ok"] is True
    assert gates["sd_acceptance_matrix"]["details"]["formats_sd"] is False
    assert gates["sd_acceptance_matrix"]["details"]["no_device_format_policy_ok"] is True


def test_release_gate_audit_accepts_official_seeed_sd_smoke_artifact(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_official_seeed_smoke_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["sd_official_seeed_smoke_passed"]["ok"] is True
    assert gates["sd_official_seeed_smoke_passed"]["evidence"] == [
        "artifacts/hardware/com12/seeed_official_sd_smoke_68350bf.json"
    ]
    assert gates["sd_official_seeed_smoke_passed"]["details"]["inner_test"] == "seeed_official_sd_smoke"
    assert gates["sd_official_seeed_smoke_passed"]["details"]["fat_type"] == 32
    assert report["p0_failed_count"] == 4


def test_release_gate_audit_rejects_old_smoke_wrapper_when_inner_sd_failed(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    write_json(
        hardware / "rp2040_seeed_official_sd_smoke_68350bf.json",
        {
            "schema": 1,
            "kind": "rp2040_seeed_sd_smoke",
            "port": "COM12",
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
    assert report["p0_failed_count"] == 5


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
    (hardware / "ui_tab_abuse_68350bf.json").unlink()
    write_json(
        hardware / "ui_tab_abuse_deadbee.json",
        {"ok": True, "port": "COM12", "cycles": 100, "failure_count": 0},
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_tab_abuse"]["ok"] is False
    assert gates["ui_tab_abuse"]["details"]["path_found"] is False


def test_release_gate_audit_rejects_mismatched_commit_metadata_even_when_filename_matches(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    write_json(hardware / "smoke_68350bf.json", {"ok": True, "port": "COM12", "firmware_commit": STALE_COMMIT})
    write_json(
        hardware / "ui_tab_abuse_68350bf.json",
        {"ok": True, "port": "COM12", "cycles": 100, "failure_count": 0, "firmware_commit": STALE_COMMIT},
    )
    write_json(
        hardware / "scroll_probe_68350bf.json",
        {
            "ok": True,
            "port": "COM12",
            "failure_count": 0,
            "screens": ["messages", "nodes", "packets", "settings", "map"],
            "git": {"head_sha": STALE_COMMIT},
        },
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
        hardware / "seeed_official_sd_smoke_68350bf.json",
        {
            "schema": 1,
            "kind": "seeed_official_sd_smoke_capture",
            "port": "COM12",
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

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    for gate_id in (
        "com12_smoke",
        "ui_tab_abuse",
        "ui_scroll_probe",
        "outbound_dm_com11",
        "sd_official_seeed_smoke_passed",
        "sd_acceptance_matrix",
        "full_duration_idle_soak",
        "manual_physical_ui_review",
        "full_rf_dm_acceptance",
    ):
        assert gates[gate_id]["ok"] is False
    assert gates["com12_smoke"]["details"]["path_found"] is False
    assert gates["ui_tab_abuse"]["details"]["path_found"] is False
    assert gates["ui_scroll_probe"]["details"]["path_found"] is False
    assert gates["outbound_dm_com11"]["details"]["path_found"] is False
    assert gates["sd_official_seeed_smoke_passed"]["evidence"] == []
    assert gates["sd_acceptance_matrix"]["evidence"] == []
    assert gates["full_duration_idle_soak"]["evidence"] == []
    assert gates["manual_physical_ui_review"]["details"]["review_found"] is False
    assert gates["manual_physical_ui_review"]["details"]["photo_count"] == 1
    assert gates["full_rf_dm_acceptance"]["details"]["candidate_count"] == 0


def test_release_gate_audit_accepts_matching_commit_metadata_without_commit_filename(tmp_path: Path):
    write_core_evidence(tmp_path)
    hardware = tmp_path / "artifacts" / "hardware" / "com12"
    (hardware / "ui_tab_abuse_68350bf.json").unlink()
    write_json(
        hardware / "ui_tab_abuse_latest.json",
        {"ok": True, "port": "COM12", "cycles": 100, "failure_count": 0, "firmware_commit": COMMIT},
    )

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert gates["ui_tab_abuse"]["ok"] is True
    assert gates["ui_tab_abuse"]["evidence"] == ["artifacts/hardware/com12/ui_tab_abuse_latest.json"]


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
