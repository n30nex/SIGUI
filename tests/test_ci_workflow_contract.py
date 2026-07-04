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

    sd_gate = "if: needs.change-filter.outputs.include_sd_bridge == 'true'"
    assert "needs: change-filter" in host
    assert "python -m pytest tests -q" in host
    assert "python ./tools/ui_simulator.py --out artifacts/ui-sim" in host
    assert "python ./tools/ui_simulator.py --scenario large-mesh --out artifacts/ui-sim-large" in host
    assert "python ./tools/ui_simulator.py --scenario storage-states --out artifacts/ui-sim-storage" in host
    assert "python ./scripts/smoke_d1l.py --dry-run" in host
    assert "python ./scripts/ui_corruption_probe_d1l.py --dry-run --rounds 20" in host
    assert "ui_tab_abuse_d1l.py" not in host
    assert "python ./scripts/scroll_probe_d1l.py --dry-run --screens home,public_messages,dm_thread,nodes,packets,settings,storage,wifi,map" in host
    assert "python ./scripts/soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test" in host
    assert "python ./scripts/sd_file_canary_d1l.py --dry-run" in host
    assert "python ./scripts/sd_retained_history_acceptance_d1l.py --dry-run --token ci-dry-run" in host
    assert "python ./scripts/sd_map_tile_canary_d1l.py --dry-run --token ci-dry-run" in host
    assert "python ./scripts/sd_reboot_remount_acceptance_d1l.py --dry-run --token ci-dry-run" in host
    assert "python ./scripts/sd_export_canary_d1l.py --dry-run --token ci-dry-run" in host
    assert "python ./scripts/sd_diagnostic_export_d1l.py --dry-run --token ci-dry-run" in host
    assert "python ./scripts/sd_data_export_d1l.py --dry-run --token ci-dry-run" in host
    assert "python ./scripts/rp2040_sd_bridge_preflight_d1l.py --dry-run --artifact-dir artifacts/rp2040-sd-bridge" in host
    assert "python ./scripts/sd_boot_prepare_acceptance_d1l.py --dry-run --scenario all" in host
    assert host.count(sd_gate) >= 9
    assert "python ./scripts/verify_checksums.py artifacts" in host
    assert "python ./scripts/release_gate_audit_d1l.py --out artifacts/release-gate/d1l-release-gate-audit-ci.json" in host


def test_ci_detects_when_sd_bridge_scope_is_required():
    text = workflow_text()
    job = job_block("change-filter")

    assert "include_sd_bridge:" in text
    assert "default: false" in text
    assert "include_sd_bridge=true" in job
    assert "reason=manual_dispatch" in job
    assert "reason=sd_bridge_paths" in job
    assert "firmware/rp2040_sd_bridge/" in job
    assert "firmware/rp2040_sd_smoke/" in job
    assert "tools/rp2040_sd_protocol.py" in job
    assert "scripts/(rp2040_|sd_).*_d1l.py" in job


def test_ci_builds_rp2040_sd_bridge_only_in_actions_with_checksums():
    job = job_block("rp2040-sd-bridge-build")

    assert "needs: change-filter" in job
    assert "if: needs.change-filter.outputs.include_sd_bridge == 'true'" in job
    assert "arduino/setup-arduino-cli@v2" in job
    assert "package_rp2040_index.json" in job
    assert "arduino-cli core install rp2040:rp2040" in job
    assert "arduino-cli compile" in job
    assert "--fqbn rp2040:rp2040:seeed_indicator_rp2040" in job
    assert job.count('--build-property compiler.cpp.extra_flags="-DUSE_SD_CRC=1"') == 3
    assert "USE_SPI_ARRAY_TRANSFER" not in job
    assert "firmware/rp2040_sd_bridge/deskos_sd_bridge" in job
    assert "artifacts/rp2040-sd-bridge" in job
    assert "SHA256SUMS.txt" in job
    assert "python ./scripts/verify_checksums.py artifacts/rp2040-sd-bridge" in job
    assert "name: rp2040-sd-bridge-firmware" in job
    assert "path: artifacts/rp2040-sd-bridge/**" in job
    assert "firmware/rp2040_sd_bridge/smoke/seeed_official_sd_smoke" in job
    assert "artifacts/rp2040-seeed-official-sd-smoke" in job
    assert "python ./scripts/verify_checksums.py artifacts/rp2040-seeed-official-sd-smoke" in job
    assert "name: rp2040-seeed-official-sd-smoke-firmware" in job
    assert "path: artifacts/rp2040-seeed-official-sd-smoke/**" in job
    assert "if-no-files-found: error" in job
    assert "--upload" not in job
    assert "--port" not in job
    assert not re.search(r"\bCOM\d+\b", job, re.IGNORECASE)


def test_ci_verifies_firmware_and_release_checksums_after_packaging():
    job = job_block("firmware-build")

    assert "needs: [change-filter, rp2040-sd-bridge-build]" in job
    assert "needs.rp2040-sd-bridge-build.result == 'skipped'" in job
    assert "idf.py build" in job
    assert "actions/download-artifact@v7" in job
    assert "if: needs.change-filter.outputs.include_sd_bridge == 'true'" in job
    assert "pattern: rp2040-*-firmware" in job
    assert "path: artifacts/rp2040-release-inputs" in job
    assert "merge-multiple: false" in job
    assert "package_args=(--build-dir build --out-dir artifacts/release" in job
    assert 'python scripts/package_release_d1l.py "${package_args[@]}"' in job
    assert "--rp2040-artifact-root artifacts/rp2040-release-inputs" in job
    assert "python scripts/verify_checksums.py artifacts/firmware" in job
    assert "python scripts/verify_checksums.py artifacts/release" in job
    assert "--port" not in job
    assert not re.search(r"\bCOM\d+\b", job, re.IGNORECASE)
