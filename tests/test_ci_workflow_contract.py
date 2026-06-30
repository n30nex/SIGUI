import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def workflow_text() -> str:
    return (ROOT / ".github" / "workflows" / "d1l-ci.yml").read_text(encoding="utf-8")


def job_block(name: str) -> str:
    text = workflow_text()
    start = text.index(f"  {name}:")
    next_job = re.search(r"\n  [a-zA-Z0-9_-]+:\n", text[start + 1 :])
    if not next_job:
        return text[start:]
    return text[start : start + 1 + next_job.start()]


def test_ci_host_checks_are_host_only_for_sd_bridge():
    host = job_block("host-checks")

    for forbidden in [
        "build_d1l.ps1",
        "idf.py",
        "esptool",
        "flash_d1l",
        "monitor_d1l",
        "backup_flash",
        "--port",
        "mesh send public",
    ]:
        assert forbidden not in host
    assert not re.search(r"\bCOM\d+\b", host, re.IGNORECASE)

    assert "python -m pytest tests -q" in host
    assert "python ./tools/ui_simulator.py --out artifacts/ui-sim" in host
    assert "python ./tools/ui_simulator.py --scenario large-mesh --out artifacts/ui-sim-large" in host
    assert "python ./scripts/smoke_d1l.py --dry-run" in host
    assert "python ./scripts/sd_file_canary_d1l.py --dry-run" in host
    assert "python ./scripts/sd_retained_history_acceptance_d1l.py --dry-run --token ci-dry-run" in host
    assert "python ./scripts/sd_export_canary_d1l.py --dry-run --token ci-dry-run" in host
    assert "python ./scripts/verify_checksums.py artifacts" in host


def test_ci_builds_rp2040_sd_bridge_only_in_actions_with_checksums():
    job = job_block("rp2040-sd-bridge-build")

    assert "arduino/setup-arduino-cli@v2" in job
    assert "package_rp2040_index.json" in job
    assert "arduino-cli core install rp2040:rp2040" in job
    assert "arduino-cli compile" in job
    assert "--fqbn rp2040:rp2040:seeed_indicator_rp2040" in job
    assert "firmware/rp2040_sd_bridge/deskos_sd_bridge" in job
    assert "artifacts/rp2040-sd-bridge" in job
    assert "SHA256SUMS.txt" in job
    assert "python ./scripts/verify_checksums.py artifacts/rp2040-sd-bridge" in job
    assert "name: rp2040-sd-bridge-firmware" in job
    assert "path: artifacts/rp2040-sd-bridge/**" in job
    assert "if-no-files-found: error" in job
    assert "--upload" not in job
    assert "--port" not in job
    assert not re.search(r"\bCOM\d+\b", job, re.IGNORECASE)
