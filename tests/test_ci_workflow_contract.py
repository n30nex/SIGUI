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
    assert "python ./scripts/scroll_probe_d1l.py --dry-run --screens home,public_messages,dm_thread,nodes,packets,settings,storage,storage_card,storage_data,wifi,map,map_options,map_location,map_cache" in host
    assert "python ./tools/ui_simulator.py --scenario map-ready --view map --view map_options --view map_location --view map_cache --out artifacts/ui-sim-map-ready" in host
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
    assert "python -m pytest -q tests/test_checksum_manifest.py tests/test_package_release_d1l.py" in host
    assert "python ./scripts/verify_checksums.py artifacts" not in host
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


def test_ci_gates_firmware_on_pinned_meshcore_wire_conformance():
    job = job_block("meshcore-conformance")
    firmware = job_block("firmware-build")

    assert "runs-on: ubuntu-24.04" in job
    assert "timeout-minutes: 10" in job
    assert "actions/checkout@v7" in job
    assert "submodules: recursive" in job
    assert "sudo apt-get install -y clang-18" in job
    assert "clang-18 --version" in job
    assert "clang++-18 --version" in job
    assert "ASAN_OPTIONS: halt_on_error=1:abort_on_error=1:detect_leaks=1" in job
    assert 'D1L_MESHCORE_CONFORMANCE_CI: "1"' in job
    assert "UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1" in job
    assert "python ./scripts/meshcore_conformance_d1l.py" in job
    assert "--cc clang-18" in job
    assert "--cxx clang++-18" in job
    assert '--commit "$GITHUB_SHA"' in job
    assert "--seed 13746277" in job
    assert "--runs 100000" in job
    assert "--dry-run" not in job
    assert 'artifacts/meshcore-conformance/meshcore_conformance_${GITHUB_SHA}.json' in job
    assert "name: d1l-meshcore-wire-conformance" in job
    assert "path: artifacts/meshcore-conformance/**" in job
    assert "if-no-files-found: error" in job
    assert "idf.py" not in job
    assert "esptool" not in job
    assert "--port" not in job
    assert not re.search(r"\bCOM\d+\b", job, re.IGNORECASE)

    assert "needs: [change-filter, host-checks, meshcore-conformance, rp2040-sd-bridge-build]" in firmware
    assert "needs.host-checks.result == 'success'" in firmware
    assert "needs.meshcore-conformance.result == 'success'" in firmware
    assert "name: Download exact MeshCore conformance evidence" in firmware
    assert "uses: actions/download-artifact@v7" in firmware
    assert "name: d1l-meshcore-wire-conformance" in firmware
    assert "path: artifacts/meshcore-conformance-input" in firmware
    assert "name: Verify exact MeshCore conformance evidence" in firmware
    assert 'source.get("repository_commit") == os.environ["GITHUB_SHA"]' in firmware
    assert 'report.get("coverage_boundary") == "wire_envelope_only"' in firmware
    assert 'report.get("coverage_level") == "wire_envelope_only"' in firmware
    assert 'report.get("closure_ready") is False' in firmware
    assert 'report.get("issue_65_closure_eligible") is False' in firmware
    assert "D1L_MESHCORE_CONFORMANCE_JSON=" in firmware


def test_ci_verifies_firmware_and_release_checksums_after_packaging():
    job = job_block("firmware-build")

    assert "needs: [change-filter, host-checks, meshcore-conformance, rp2040-sd-bridge-build]" in job
    assert "needs.host-checks.result == 'success'" in job
    assert "needs.meshcore-conformance.result == 'success'" in job
    assert "needs.rp2040-sd-bridge-build.result == 'skipped'" in job
    assert job.count("container: espressif/idf:v5.5.4") == 1
    assert "espressif/idf:release-v5.1" not in job
    assert not re.search(r"container:\s*espressif/idf:(?:latest|release-v)", job)
    assert "idf.py build" in job
    assert "name: Capture ESP-IDF migration state" in job
    assert "name: d1l-idf55-migration-state" in job
    assert "path: artifacts/idf-migration/**" in job
    assert "dependencies.lock.patch" in job
    assert "build/config/sdkconfig.json" in job
    assert "git diff --exit-code -- dependencies.lock" in job
    assert "git diff --exit-code" not in job.replace(
        "git diff --exit-code -- dependencies.lock", ""
    )
    assert (
        job.index("name: Build D1L firmware")
        < job.index("name: Capture ESP-IDF migration state")
        < job.index("name: d1l-idf55-migration-state")
        < job.index("name: Require committed dependency solution")
        < job.index("name: Collect firmware artifacts")
        < job.index("name: Package D1L release")
    )
    assert "actions/download-artifact@v7" in job
    assert "if: needs.change-filter.outputs.include_sd_bridge == 'true'" in job
    assert "pattern: rp2040-*-firmware" in job
    assert "path: artifacts/rp2040-release-inputs" in job
    assert "merge-multiple: false" in job
    assert "package_args=(--build-dir build --out-dir artifacts/release" in job
    assert '--meshcore-conformance-json "$D1L_MESHCORE_CONFORMANCE_JSON"' in job
    assert 'python scripts/package_release_d1l.py "${package_args[@]}"' in job
    assert "--rp2040-artifact-root artifacts/rp2040-release-inputs" in job
    assert "python scripts/verify_checksums.py artifacts/firmware" in job
    assert "python scripts/verify_checksums.py artifacts/release" in job
    assert "test -f sdkconfig" in job
    assert "test -f build/config/sdkconfig.json" in job
    assert "cp --parents sdkconfig build/config/sdkconfig.json artifacts/firmware/" in job
    assert "--port" not in job
    assert not re.search(r"\bCOM\d+\b", job, re.IGNORECASE)


def test_ci_records_exact_host_success_only_after_all_host_checks_pass():
    host = job_block("host-checks")
    firmware = job_block("firmware-build")

    assert "name: Record exact host-check success evidence" in host
    assert 'artifact_type = "d1l_host_checks_success"' in host
    assert 'status = "pass"' in host
    assert "passed = $true" in host
    assert "all_prior_steps_completed = $true" in host
    assert "repository_commit = $env:GITHUB_SHA" in host
    assert "workflow_run_id = [string]$env:GITHUB_RUN_ID" in host
    assert 'd1l_host_checks_success_{0}.json' in host
    assert host.index("name: Host tests") < host.index("name: Record exact host-check success evidence")
    assert host.index("name: Record exact host-check success evidence") < host.index("name: d1l-host-artifacts")
    assert "needs.host-checks.result == 'success'" in firmware
