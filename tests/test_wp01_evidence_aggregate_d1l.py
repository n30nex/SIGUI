import copy
import json
import shutil
from pathlib import Path

import pytest

from scripts import release_gate_audit_d1l as audit
from scripts import autonomous_hardware_validate_d1l as runner
from scripts import wp01_evidence_aggregate_d1l as wp01


COMMIT = "ad0aa5bda21435846f6e7fcdde8fd87a85c5da5c"
RUN_ID = "29222903210"


def write_actions_fixture(root: Path) -> Path:
    run_dir = root / "artifacts" / "github" / f"{RUN_ID}-current"
    build = run_dir / "d1l-firmware-artifacts" / "build"
    source_files = {
        "bootloader/bootloader.bin": b"bootloader",
        "partition_table/partition-table.bin": b"partition-table",
        "meshcore_deskos_d1l.bin": b"application",
    }
    for relative, content in source_files.items():
        path = build / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    (build / "flasher_args.json").write_text(
        json.dumps(
            {
                "flash_files": {
                    "0x0": "bootloader/bootloader.bin",
                    "0x8000": "partition_table/partition-table.bin",
                    "0x10000": "meshcore_deskos_d1l.bin",
                }
            }
        ),
        encoding="utf-8",
    )
    artifact_root = build.parent
    checksum_rows = []
    for path in sorted(item for item in artifact_root.rglob("*") if item.is_file()):
        checksum_rows.append(
            f"{wp01.sha256_file(path)}  {path.relative_to(artifact_root).as_posix()}"
        )
    (artifact_root / "SHA256SUMS.txt").write_text(
        "\n".join(checksum_rows) + "\n",
        encoding="ascii",
    )

    host = run_dir / "d1l-host-artifacts" / "host-checks"
    host.mkdir(parents=True, exist_ok=True)
    (host / f"d1l_host_checks_success_{COMMIT}.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "artifact_type": "d1l_host_checks_success",
                "status": "pass",
                "passed": True,
                "all_prior_steps_completed": True,
                "job": "host-checks",
                "repository_commit": COMMIT,
                "workflow_run_id": RUN_ID,
                "workflow_run_attempt": "1",
            }
        ),
        encoding="utf-8",
    )

    bridge = run_dir / "rp2040-sd-bridge-firmware"
    bridge.mkdir(parents=True, exist_ok=True)
    bridge_uf2 = bridge / runner.BRIDGE_UF2
    bridge_uf2.write_bytes(b"exact-rp2040-bridge-uf2")
    (bridge / "SHA256SUMS.txt").write_text(
        f"{wp01.sha256_file(bridge_uf2)}  {runner.BRIDGE_UF2}\n",
        encoding="ascii",
    )

    package = run_dir / "d1l-release-package" / f"d1l-release-{COMMIT}"
    flash_files = []
    roles = (
        ("bootloader", "bootloader/bootloader.bin", "firmware/bootloader.bin", "0x0"),
        (
            "partition-table",
            "partition_table/partition-table.bin",
            "firmware/partition-table.bin",
            "0x8000",
        ),
        ("app", "meshcore_deskos_d1l.bin", "firmware/meshcore_deskos_d1l.bin", "0x10000"),
    )
    for role, source_name, package_name, offset in roles:
        source = build / source_name
        packaged = package / package_name
        packaged.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, packaged)
        flash_files.append(
            {
                "role": role,
                "source": source_name,
                "path": package_name,
                "offset": offset,
                "size": source.stat().st_size,
                "sha256": wp01.sha256_file(source),
            }
        )

    group_files = []
    for source in sorted(path for path in bridge.iterdir() if path.is_file()):
        package_name = f"rp2040/rp2040-sd-bridge-firmware/{source.name}"
        packaged = package / package_name
        packaged.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, packaged)
        group_files.append(
            {
                "path": package_name,
                "size": source.stat().st_size,
                "sha256": wp01.sha256_file(source),
            }
        )
    package.mkdir(parents=True, exist_ok=True)
    (package / "manifest.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "package": f"d1l-release-{COMMIT}",
                "git": {"commit": COMMIT, "dirty": False},
                "workflow": {
                    "run_id": RUN_ID,
                    "run_attempt": "1",
                    "sha": COMMIT,
                    "ref": "refs/heads/codex/d1l-retained-merge",
                    "repository": "n30nex/SIGUI",
                },
                "flash_files": flash_files,
                "rp2040_artifacts": [
                    {
                        "name": "rp2040-sd-bridge-firmware",
                        "path": "rp2040/rp2040-sd-bridge-firmware",
                        "files": group_files,
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    return run_dir


def add_official_smoke_fixture(run_dir: Path) -> None:
    direct = run_dir / "rp2040-seeed-official-sd-smoke-firmware"
    direct.mkdir(parents=True, exist_ok=True)
    uf2 = direct / runner.OFFICIAL_SMOKE_UF2
    uf2.write_bytes(b"exact-official-sd-smoke-uf2")
    (direct / "SHA256SUMS.txt").write_text(
        f"{wp01.sha256_file(uf2)}  {runner.OFFICIAL_SMOKE_UF2}\n",
        encoding="ascii",
    )
    package = run_dir / "d1l-release-package" / f"d1l-release-{COMMIT}"
    files = []
    prefix = "rp2040/rp2040-seeed-official-sd-smoke-firmware"
    for source in sorted(path for path in direct.iterdir() if path.is_file()):
        package_name = f"{prefix}/{source.name}"
        packaged = package / package_name
        packaged.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, packaged)
        files.append(
            {
                "path": package_name,
                "size": source.stat().st_size,
                "sha256": wp01.sha256_file(source),
            }
        )
    manifest_path = package / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["rp2040_artifacts"].append(
        {
            "name": "rp2040-seeed-official-sd-smoke-firmware",
            "path": prefix,
            "files": files,
        }
    )
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")


def write_hardware_validation_report(
    root: Path,
    run_dir: Path,
    *,
    include_official_smoke: bool = False,
) -> Path:
    ctx, actions_provenance, esp32_verification = wp01._verified_actions_inputs(
        root,
        run_dir,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        include_official_smoke=include_official_smoke,
    )
    bridge_uf2 = run_dir / "rp2040-sd-bridge-firmware" / runner.BRIDGE_UF2
    runs = [
        {
            "schema": 1,
            "kind": "input_artifact_check",
            "mode": "check",
            "ok": True,
            "provenance": actions_provenance,
        },
        {
            "schema": 1,
            "kind": "esp32_flash",
            "mode": "hardware",
            "ok": True,
            "commit": COMMIT,
            "github_actions_run": RUN_ID,
            "port": "COM12",
            "artifact_verification": esp32_verification,
            "public_rf_tx": False,
            "formats_sd": False,
            "result": {"name": "esp32_flash", "ok": True, "returncode": 0},
        },
        {
            "schema": 1,
            "kind": "rp2040_bridge_restore",
            "mode": "hardware",
            "ok": True,
            "commit": COMMIT,
            "github_actions_run": RUN_ID,
            "port": "COM16",
            "public_rf_tx": False,
            "formats_sd": False,
            "copy": {
                "ok": True,
                "copied": True,
                "artifact": {
                    "path": str(bridge_uf2.resolve()),
                    "size": bridge_uf2.stat().st_size,
                    "sha256": wp01.sha256_file(bridge_uf2).upper(),
                },
            },
            "ping": {"ok": True, "protocol_supported": True},
        },
    ]
    report = {
        "schema": 1,
        "kind": "d1l_autonomous_hardware_validation",
        "mode": "hardware",
        "ok": False,
        "commit": COMMIT,
        "github_actions_run": RUN_ID,
        "ports": {"d1l": "COM12", "rp2040": "COM16"},
        "public_rf_tx": False,
        "formats_sd": False,
        "runs": runs,
    }
    path = root / "artifacts" / "hardware" / "com12" / (
        f"d1l_autonomous_hardware_validation_{COMMIT[:7]}.json"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report), encoding="utf-8")
    return path


def write_provenance(root: Path) -> tuple[dict, Path, Path, Path]:
    run_dir = write_actions_fixture(root)
    validation_report = write_hardware_validation_report(root, run_dir)
    provenance = wp01.build_wp01_provenance_receipt(
        validation_report,
        root=root,
        github_run_dir=run_dir,
        commit=COMMIT,
        github_actions_run=RUN_ID,
        d1l_port="COM12",
        rp2040_port="COM16",
    )
    assert provenance["ok"] is True
    path = root / f"wp01_exact_pair_provenance_{COMMIT[:7]}.json"
    path.write_text(json.dumps(provenance, sort_keys=True), encoding="utf-8")
    return provenance, path, run_dir, validation_report


def common(
    kind: str,
    provenance_sha256: str = "a" * 64,
    provenance: dict | None = None,
) -> dict:
    return {
        "schema": 1,
        "kind": kind,
        "mode": "hardware",
        "ok": True,
        "commit": COMMIT,
        "github_actions_run": RUN_ID,
        "ports": {"d1l": "COM12", "rp2040": "COM16"},
        "public_rf_tx": False,
        "formats_sd": False,
        "provenance": copy.deepcopy(provenance or {}),
        "provenance_receipt_sha256": provenance_sha256,
        "wdt_count": 0,
        "panic_count": 0,
        "brownout_count": 0,
        "terminal_timeout_count": 0,
    }


def reboot_cycle() -> dict:
    return {
        "ok": True,
        "post_reboot_reset_reason": "SW",
        "reboot_proven": True,
        "storage_manager_quiesced": True,
        "retained_worker_quiesced": True,
        "rp2040_bridge_quiesced": True,
        "retained_flush": "ESP_OK",
        "route_flush": "ESP_OK",
        "pending_dirty": False,
        "wdt": False,
        "panic": False,
        "brownout": False,
        "retained_task_stack_free_bytes": 8192,
    }


def payloads(
    provenance_sha256: str = "a" * 64,
    provenance: dict | None = None,
) -> dict[str, dict]:
    inserted = {
        **common("sd_inserted_stability", provenance_sha256, provenance),
        "duration_sec": 300,
        "sample_count": 6,
        "status_poll_count": 6,
        "card_inserted_all_samples": True,
        "final_ready_sd": True,
        "false_no_card_count": 0,
        "unintended_backend_generation_count": 0,
        "retained_failure_count": 0,
        "reset_count": 0,
    }
    remove_cycle = {
        "ok": True,
        "true_removal_detected": True,
        "nvs_fallback_active": True,
        "deterministic_remount": True,
        "generation_changed": True,
        "read_merge_before_overwrite": True,
        "all_stores_reconciled": True,
        "write_before_edge": True,
        "write_during_edge": True,
        "write_after_edge": True,
        "reset_storm": False,
        "delete_or_rename_against_replacement": False,
        "user_data_loss": False,
    }
    remove = {
        **common("sd_remove_reinsert", provenance_sha256, provenance),
        "cycle_count": 10,
        "cycles": [copy.deepcopy(remove_cycle) for _ in range(10)],
        "all_cycles_passed": True,
        "reset_storm_count": 0,
        "cross_media_mutation_count": 0,
        "data_loss_count": 0,
    }
    reboots = {
        **common("retained_reboot_matrix", provenance_sha256, provenance),
        "cycle_count": 5,
        "cycles": [reboot_cycle() for _ in range(5)],
        "all_cycles_passed": True,
        "dirty_recovery_failure_count": 0,
    }
    soak = {
        **common("storage_active_soak", provenance_sha256, provenance),
        "duration_sec": 7200,
        "sample_count": 121,
        "storage_active": True,
        "dirty_event_counts": {"public": 2, "dm": 2, "routes": 2, "packets": 2},
        "controlled_reboot_count": 2,
        "controlled_reboots": [reboot_cycle(), reboot_cycle()],
        "final_ready_sd": True,
        "retained_task_stack_free_bytes_floor": 8192,
        "false_no_card_count": 0,
        "unintended_backend_generation_count": 0,
        "retained_failure_count": 0,
    }
    return {
        "sd_inserted_stability": inserted,
        "sd_remove_reinsert": remove,
        "retained_reboot_matrix": reboots,
        "storage_active_soak": soak,
    }


@pytest.mark.parametrize("kind", wp01.WP01_ARTIFACT_KINDS)
def test_wp01_artifact_validators_accept_exact_complete_evidence(tmp_path: Path, kind: str):
    provenance, provenance_path, _, _ = write_provenance(tmp_path)
    provenance_sha256 = wp01.sha256_file(provenance_path)
    assert wp01.wp01_artifact_ok(
        payloads(provenance_sha256, provenance)[kind],
        kind,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        provenance,
        provenance_sha256,
    ) is True


@pytest.mark.parametrize(
    ("kind", "mutate"),
    [
        ("sd_inserted_stability", lambda value: value.update(duration_sec=299)),
        (
            "sd_remove_reinsert",
            lambda value: value["cycles"][0].update(delete_or_rename_against_replacement=True),
        ),
        (
            "retained_reboot_matrix",
            lambda value: value["cycles"][0].update(post_reboot_reset_reason="WDT"),
        ),
        (
            "storage_active_soak",
            lambda value: value["dirty_event_counts"].update(packets=0),
        ),
    ],
)
def test_wp01_artifact_validators_fail_closed_on_missing_closure_condition(
    tmp_path: Path, kind, mutate
):
    provenance, provenance_path, _, _ = write_provenance(tmp_path)
    provenance_sha256 = wp01.sha256_file(provenance_path)
    payload = payloads(provenance_sha256, provenance)[kind]
    mutate(payload)
    assert wp01.wp01_artifact_ok(
        payload,
        kind,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        provenance,
        provenance_sha256,
    ) is False


def test_wp01_artifact_validator_rejects_stale_run_forbidden_port_and_safety_claims(
    tmp_path: Path,
):
    provenance, provenance_path, _, _ = write_provenance(tmp_path)
    provenance_sha256 = wp01.sha256_file(provenance_path)
    payload = payloads(provenance_sha256, provenance)["sd_inserted_stability"]
    payload["github_actions_run"] = "29222903209"
    assert wp01.wp01_artifact_ok(
        payload,
        "sd_inserted_stability",
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        provenance,
        provenance_sha256,
    ) is False


def test_wp01_artifact_validator_rejects_absent_or_mismatched_exact_pair_provenance(
    tmp_path: Path,
):
    provenance, provenance_path, run_dir, _ = write_provenance(tmp_path)
    provenance_sha256 = wp01.sha256_file(provenance_path)
    payload = payloads(provenance_sha256, provenance)["sd_inserted_stability"]
    payload.pop("provenance")
    assert wp01.wp01_artifact_ok(
        payload,
        "sd_inserted_stability",
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        provenance,
        provenance_sha256,
    ) is False

    payload = payloads(provenance_sha256, provenance)["sd_inserted_stability"]
    payload["provenance"]["rp2040_device_build_commit"] = "0" * 40
    assert wp01.wp01_artifact_ok(
        payload,
        "sd_inserted_stability",
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        provenance,
        provenance_sha256,
    ) is False

    fake_hash = copy.deepcopy(provenance)
    fake_hash["release_manifest_sha256"] = "1" * 64
    assert wp01.wp01_provenance_receipt_ok(
        fake_hash,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        root=tmp_path,
        github_run_dir=run_dir,
    ) is False

    payload = payloads(provenance_sha256, provenance)["sd_inserted_stability"]
    payload["ports"]["d1l"] = "COM11"
    assert wp01.wp01_artifact_ok(
        payload,
        "sd_inserted_stability",
        COMMIT,
        RUN_ID,
        "COM11",
        "COM16",
        provenance,
        provenance_sha256,
    ) is False

    payload = payloads(provenance_sha256, provenance)["sd_inserted_stability"]
    payload["formats_sd"] = True
    assert wp01.wp01_artifact_ok(
        payload,
        "sd_inserted_stability",
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        provenance,
        provenance_sha256,
    ) is False


def test_wp01_provenance_recomputes_checksums_and_rejects_invalid_ports(tmp_path: Path):
    provenance, _, run_dir, _ = write_provenance(tmp_path)
    assert wp01.wp01_provenance_receipt_ok(
        provenance,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        root=tmp_path,
        github_run_dir=run_dir,
    ) is True
    assert wp01.wp01_provenance_receipt_ok(
        provenance,
        COMMIT,
        RUN_ID,
        "",
        "COM16",
        root=tmp_path,
        github_run_dir=run_dir,
    ) is False

    bridge = run_dir / "rp2040-sd-bridge-firmware" / runner.BRIDGE_UF2
    bridge.write_bytes(b"checksum-mismatch-with-manifest")
    assert wp01.wp01_provenance_receipt_ok(
        provenance,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        root=tmp_path,
        github_run_dir=run_dir,
    ) is False


def test_wp01_provenance_recomputes_real_two_rp2040_group_shape(tmp_path: Path):
    run_dir = write_actions_fixture(tmp_path)
    add_official_smoke_fixture(run_dir)
    validation_report = write_hardware_validation_report(
        tmp_path,
        run_dir,
        include_official_smoke=True,
    )

    provenance = wp01.build_wp01_provenance_receipt(
        validation_report,
        root=tmp_path,
        github_run_dir=run_dir,
        commit=COMMIT,
        github_actions_run=RUN_ID,
        d1l_port="COM12",
        rp2040_port="COM16",
    )

    assert provenance["ok"] is True
    assert [
        group["name"] for group in provenance["artifact_provenance"]["rp2040_artifact_groups"]
    ] == [
        "rp2040-sd-bridge-firmware",
        "rp2040-seeed-official-sd-smoke-firmware",
    ]


def write_inputs(root: Path) -> tuple[dict[str, Path], Path, Path]:
    provenance, provenance_path, run_dir, _ = write_provenance(root)
    provenance_sha256 = wp01.sha256_file(provenance_path)
    paths = {}
    for kind, payload in payloads(provenance_sha256, provenance).items():
        path = root / f"{kind}_{COMMIT[:7]}.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        paths[kind] = path
    return paths, provenance_path, run_dir


def test_wp01_aggregate_hash_binds_all_four_exact_artifacts(tmp_path: Path):
    paths, provenance_path, run_dir = write_inputs(tmp_path)
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    report = wp01.build_aggregate(
        paths,
        provenance_path,
        commit=COMMIT,
        github_actions_run=RUN_ID,
        d1l_port="COM12",
        rp2040_port="COM16",
        root=tmp_path,
        github_run_dir=run_dir,
    )
    hashes = {kind: wp01.sha256_file(path) for kind, path in paths.items()}
    provenance_sha256 = wp01.sha256_file(provenance_path)

    assert report["ok"] is True
    assert wp01.wp01_aggregate_artifact_ok(
        report,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        hashes,
        provenance_sha256,
        provenance,
    ) is True

    hashes["storage_active_soak"] = "0" * 64
    assert wp01.wp01_aggregate_artifact_ok(
        report,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        hashes,
        provenance_sha256,
        provenance,
    ) is False

    assert wp01.wp01_aggregate_artifact_ok(
        report,
        COMMIT,
        RUN_ID,
        "COM12",
        "COM16",
        hashes,
        "0" * 64,
        provenance,
    ) is False


def test_wp01_aggregate_preserves_invalid_input_and_fails_closed(tmp_path: Path):
    paths, provenance_path, run_dir = write_inputs(tmp_path)
    invalid = json.loads(paths["retained_reboot_matrix"].read_text(encoding="utf-8"))
    invalid["cycles"][0]["retained_worker_quiesced"] = False
    paths["retained_reboot_matrix"].write_text(json.dumps(invalid), encoding="utf-8")

    report = wp01.build_aggregate(
        paths,
        provenance_path,
        commit=COMMIT,
        github_actions_run=RUN_ID,
        d1l_port="COM12",
        rp2040_port="COM16",
        root=tmp_path,
        github_run_dir=run_dir,
    )

    assert report["ok"] is False
    assert report["checks"]["retained_reboot_matrix"] is False
    assert report["failures"] == ["missing_or_invalid:retained_reboot_matrix"]


def test_release_audit_recognizes_only_hash_bound_complete_wp01_evidence(tmp_path: Path):
    paths, provenance_path, run_dir = write_inputs(tmp_path)
    report = wp01.build_aggregate(
        paths,
        provenance_path,
        commit=COMMIT,
        github_actions_run=RUN_ID,
        d1l_port="COM12",
        rp2040_port="COM16",
        root=tmp_path,
        github_run_dir=run_dir,
    )
    aggregate = tmp_path / f"wp01_acceptance_{COMMIT[:7]}_COM12_COM16.json"
    aggregate.write_text(json.dumps(report), encoding="utf-8")

    gate = audit.wp01_evidence_gate(
        tmp_path, tmp_path, run_dir, COMMIT, RUN_ID, "COM12", "COM16"
    ).to_dict()
    assert gate["ok"] is True
    assert len(gate["evidence"]) == 6

    stale = json.loads(paths["sd_inserted_stability"].read_text(encoding="utf-8"))
    stale["github_actions_run"] = "29222903209"
    paths["sd_inserted_stability"].write_text(json.dumps(stale), encoding="utf-8")
    gate = audit.wp01_evidence_gate(
        tmp_path, tmp_path, run_dir, COMMIT, RUN_ID, "COM12", "COM16"
    ).to_dict()
    assert gate["ok"] is False
    assert gate["details"]["sd_inserted_stability"]["artifact_ok"] is False
