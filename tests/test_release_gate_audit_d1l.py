import hashlib
import json
from pathlib import Path

from scripts.release_gate_audit_d1l import build_audit, parse_args


COMMIT = "68350bf9f3fabfd2db4110ec6ffc36068056a060"
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
    assert gates["docs_current_evidence"]["ok"] is True


def test_release_gate_audit_blocks_public_release_without_p0_evidence(tmp_path: Path):
    write_core_evidence(tmp_path)

    report = build_audit(audit_args(tmp_path))
    gates = gate_by_id(report)

    assert report["ready_for_public_release"] is False
    assert gates["sd_acceptance_matrix"]["ok"] is False
    assert gates["full_duration_idle_soak"]["ok"] is False
    assert gates["manual_physical_ui_review"]["ok"] is False
    assert gates["full_rf_dm_acceptance"]["ok"] is False
    assert report["p0_failed_count"] == 4


def test_release_gate_audit_accepts_full_soak_when_duration_and_summary_pass(tmp_path: Path):
    write_core_evidence(tmp_path)
    write_json(
        tmp_path / "artifacts" / "soak" / "d1l-12h-soak.json",
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
