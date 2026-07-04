import json
from pathlib import Path

from scripts import release_progress_dashboard as dashboard


def write_release_gate(root: Path) -> Path:
    gate_dir = root / "artifacts" / "release-gate"
    gate_dir.mkdir(parents=True)
    path = gate_dir / "release-gate-audit-test.json"
    path.write_text(
        json.dumps(
            {
                "commit": "abc123",
                "github_run_id": "12345",
                "ready_for_public_release": False,
                "failed_count": 2,
                "p0_failed_count": 1,
                "gates": [
                    {
                        "id": "ci_artifacts_checksums",
                        "title": "CI checksums",
                        "severity": "P0",
                        "ok": True,
                        "message": "ok",
                        "evidence": ["artifacts/github/run"],
                    },
                    {
                        "id": "ui_pixel_capture",
                        "title": "Pixel capture",
                        "severity": "P0",
                        "ok": False,
                        "message": "missing",
                        "evidence": [],
                    },
                    {
                        "id": "docs_current_evidence",
                        "title": "Docs",
                        "severity": "P1",
                        "ok": False,
                        "message": "stale",
                        "evidence": [],
                    },
                ],
            }
        ),
        encoding="utf-8",
    )
    return path


def write_hardware_validation(root: Path) -> None:
    hardware_dir = root / "artifacts" / "hardware"
    hardware_dir.mkdir(parents=True)
    (hardware_dir / "d1l-autonomous-hardware-validation-test.json").write_text(
        json.dumps(
            {
                "ok": True,
                "commit": "abc123",
                "github_actions_run": "12345",
                "rp2040_uf2_flash": False,
                "sd_suite_enabled": False,
                "steps": ["verify_input_artifacts", "flash_esp32"],
                "runs": [{"kind": "flash", "ok": True}, {"kind": "smoke", "ok": True}],
            }
        ),
        encoding="utf-8",
    )


def test_dashboard_status_summarizes_release_gate_and_hardware(tmp_path):
    write_release_gate(tmp_path)
    write_hardware_validation(tmp_path)

    status = dashboard.build_status(tmp_path)
    gate = status["release_gate"]
    hardware = status["hardware_validation"]

    assert gate["ready_for_public_release"] is False
    assert gate["total"] == 3
    assert gate["done"] == 1
    assert gate["p0_total"] == 2
    assert gate["p0_done"] == 1
    assert gate["p0_failed"][0]["id"] == "ui_pixel_capture"
    assert gate["groups"]["UI"]["p0_total"] == 1
    assert gate["groups"]["CI/Docs"]["total"] == 2
    assert hardware["present"] is True
    assert hardware["ok"] is True
    assert hardware["rp2040_uf2_flash"] is False
    assert hardware["sd_suite_enabled"] is False


def test_dashboard_html_contains_progress_and_open_gates(tmp_path):
    write_release_gate(tmp_path)

    html = dashboard.render_html(dashboard.build_status(tmp_path), refresh_sec=10)

    assert "MeshCore DeskOS D1L Release Progress" in html
    assert "NOT READY" in html
    assert "ui_pixel_capture" in html
    assert "Pixel capture" in html
    assert "/api/status" not in html
