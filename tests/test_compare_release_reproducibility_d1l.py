import copy
import json
from datetime import datetime, timezone
from pathlib import Path

import pytest

from scripts import (
    compare_release_reproducibility_d1l,
    package_release_d1l,
    sbom_d1l,
)


COMMIT = "a" * 40


def source_identity(commit: str = COMMIT) -> dict:
    return {
        "commit": commit,
        "created": "2026-07-13T18:01:56Z",
        "repository": sbom_d1l.PROJECT_REPOSITORY,
        "submodules": [
            {
                "path": "third_party/MeshCore",
                "url": "https://github.com/meshcore-dev/MeshCore.git",
                "commit": "1" * 40,
                "license": "MIT",
            },
            {
                "path": "third_party/sensecap_indicator_esp32",
                "url": "https://github.com/Seeed-Solution/sensecap_indicator_esp32.git",
                "commit": "2" * 40,
                "license": "Apache-2.0",
            },
        ],
    }


def write_source_tree(root: Path) -> None:
    for relative in sbom_d1l.REQUIRED_SOURCE_INPUTS:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"fixture for {relative}\n", encoding="utf-8")

    for relative in (
        "docs/USER_GUIDE_D1L.md",
        "docs/DEVELOPER_GUIDE_D1L.md",
        "docs/FLASH_RECOVERY_D1L.md",
        "docs/RP2040_SD_BRIDGE_FLASH_D1L.md",
    ):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"fixture for {relative}\n", encoding="utf-8")

    (root / "main" / "d1l_config.h").write_text(
        '#define D1L_FIRMWARE_NAME "MeshCore DeskOS D1L"\n'
        '#define D1L_FIRMWARE_VERSION "1.0.0-test"\n',
        encoding="ascii",
    )
    requirements = root / "requirements" / "ci-host-windows.txt"
    requirements.parent.mkdir(parents=True, exist_ok=True)
    requirements.write_text("pytest==8.4.1\nruff==0.12.2\n", encoding="ascii")

    digest = "1" * 64
    build_inputs = {
        "schema": 1,
        "kind": "d1l_build_inputs",
        "esp_idf": {
            "version": "v5.5.4",
            "container": {
                "reference": f"docker.io/espressif/idf@sha256:{digest}",
                "index_digest": f"sha256:{digest}",
            },
        },
        "host_python": {
            "version": "3.13.6",
            "architecture": "x64",
            "requirements": {
                "path": "requirements/ci-host-windows.txt",
                "sha256": sbom_d1l.sha256_file(requirements),
            },
        },
    }
    build_lock = root / ".github" / "d1l-build-inputs.json"
    build_lock.parent.mkdir(parents=True, exist_ok=True)
    build_lock.write_text(
        json.dumps(build_inputs, indent=2, sort_keys=True) + "\n", encoding="ascii"
    )


def write_build(build: Path, app_payload: bytes = b"APP") -> None:
    (build / "bootloader").mkdir(parents=True)
    (build / "partition_table").mkdir(parents=True)
    (build / "bootloader" / "bootloader.bin").write_bytes(b"BOOT")
    (build / "partition_table" / "partition-table.bin").write_bytes(b"PART")
    (build / "meshcore_deskos_d1l.bin").write_bytes(app_payload)
    (build / "meshcore_deskos_d1l.elf").write_bytes(b"ELF")
    (build / "meshcore_deskos_d1l.map").write_text("MAP", encoding="ascii")
    (build / "flasher_args.json").write_text(
        json.dumps(
            {
                "flash_settings": {
                    "flash_mode": "dio",
                    "flash_size": "8MB",
                    "flash_freq": "80m",
                },
                "flash_files": {
                    "0x0": "bootloader/bootloader.bin",
                    "0x10000": "meshcore_deskos_d1l.bin",
                    "0x8000": "partition_table/partition-table.bin",
                },
            }
        ),
        encoding="ascii",
    )


def write_conformance(root: Path, commit: str = COMMIT) -> Path:
    path = root / f"meshcore_conformance_{commit}.json"
    path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "artifact_type": package_release_d1l.MESHCORE_CONFORMANCE_ARTIFACT_TYPE,
                "generated_at": datetime.now(timezone.utc)
                .isoformat()
                .replace("+00:00", "Z"),
                "passed": True,
                "status": "pass",
                "execution_complete": True,
                "coverage_boundary": package_release_d1l.MESHCORE_CONFORMANCE_BOUNDARY,
                "coverage_level": package_release_d1l.MESHCORE_CONFORMANCE_BOUNDARY,
                "closure_ready": False,
                "issue_65_closure_eligible": False,
                "source_verification": {"repository_commit": commit},
            },
            sort_keys=True,
        ),
        encoding="ascii",
    )
    return path


def clean_git_info(commit: str = COMMIT) -> dict:
    return {
        "commit": commit,
        "short_commit": commit[:7],
        "branch": "main",
        "dirty": False,
        "dirty_entries": [],
        "source_patches": [],
    }


def build_pair(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    *,
    run_ids: tuple[str, str] = ("1001", "1002"),
    app_payloads: tuple[bytes, bytes] = (b"APP", b"APP"),
    second_package_name: str | None = None,
) -> tuple[Path, Path, Path, dict]:
    root = tmp_path / "source"
    root.mkdir()
    write_source_tree(root)
    conformance = write_conformance(root)
    identity = source_identity()
    monkeypatch.setattr(package_release_d1l, "git_info", lambda _root: clean_git_info())
    monkeypatch.setattr(
        package_release_d1l,
        "discover_source_identity",
        lambda _root, _expected: identity,
    )
    monkeypatch.setenv("GITHUB_SHA", COMMIT)
    monkeypatch.setenv("GITHUB_REPOSITORY", "n30nex/SIGUI")
    monkeypatch.setenv("GITHUB_WORKFLOW", "d1l-ci")
    monkeypatch.setenv("GITHUB_REF", "refs/heads/main")
    monkeypatch.setenv("GITHUB_RUN_ATTEMPT", "1")
    monkeypatch.setenv("GITHUB_SERVER_URL", "https://github.com")

    packages = []
    for index, (run_id, app_payload) in enumerate(
        zip(run_ids, app_payloads, strict=True), start=1
    ):
        build = root / f"build-{index}"
        write_build(build, app_payload)
        monkeypatch.setenv("GITHUB_RUN_ID", run_id)
        package_name = (
            second_package_name
            if index == 2 and second_package_name is not None
            else f"d1l-release-{COMMIT}"
        )
        package_release_d1l.create_release_package(
            root=root,
            build_dir=build,
            out_dir=root / "artifacts" / f"run-{index}",
            package_name=package_name,
            full_size=0x20000,
            meshcore_conformance_json=conformance,
        )
        packages.append(root / "artifacts" / f"run-{index}" / package_name)
    return root, packages[0], packages[1], identity


def compare(root: Path, first: Path, second: Path, identity: dict) -> dict:
    return compare_release_reproducibility_d1l.compare_release_packages(
        root,
        first,
        second,
        COMMIT,
        source_identity=identity,
        enforce_clean_source=False,
    )


def test_two_distinct_actions_packages_are_reproducible_and_receipt_is_deterministic(
    tmp_path, monkeypatch
):
    root, first, second, identity = build_pair(tmp_path, monkeypatch)

    receipt = compare(root, first, second, identity)
    repeated = compare(root, first, second, identity)

    assert receipt == repeated
    assert receipt["status"] == "pass"
    assert receipt["reproducible"] is True
    assert receipt["failures"] == []
    assert all(receipt["comparison"].values())
    assert (
        receipt["packages"][0]["manifest_sha256"]
        != receipt["packages"][1]["manifest_sha256"]
    )
    assert (
        receipt["packages"][0]["checksum_manifest_sha256"]
        != receipt["packages"][1]["checksum_manifest_sha256"]
    )
    assert (
        receipt["packages"][0]["stable_payload_fingerprint"]
        == receipt["packages"][1]["stable_payload_fingerprint"]
    )
    observed = set(
        receipt["permitted_nonreproducible_metadata"]["observed_manifest_differences"]
    )
    assert {"/source_build_dir", "/workflow/run_id", "/workflow/run_url"} <= observed
    assert observed <= set(compare_release_reproducibility_d1l.PERMITTED_MANIFEST_PATHS)
    serialized = compare_release_reproducibility_d1l.canonical_json(receipt)
    assert str(root) not in serialized
    assert serialized == compare_release_reproducibility_d1l.canonical_json(repeated)


def test_same_actions_run_is_rejected(tmp_path, monkeypatch):
    root, first, second, identity = build_pair(
        tmp_path, monkeypatch, run_ids=("1001", "1001")
    )

    receipt = compare(root, first, second, identity)

    assert receipt["reproducible"] is False
    assert receipt["comparison"]["distinct_actions_runs"] is False
    assert {item["code"] for item in receipt["failures"]} == {"same_actions_run"}


def test_payload_digest_mismatch_is_rejected(tmp_path, monkeypatch):
    root, first, second, identity = build_pair(
        tmp_path, monkeypatch, app_payloads=(b"APP-A", b"APP-B")
    )

    receipt = compare(root, first, second, identity)

    codes = {item["code"] for item in receipt["failures"]}
    assert receipt["reproducible"] is False
    assert receipt["comparison"]["payload_sha256_match"] is False
    assert "payload_sha256_mismatch" in codes
    assert "firmware/meshcore_deskos_d1l.bin" in next(
        item["detail"]
        for item in receipt["failures"]
        if item["code"] == "payload_sha256_mismatch"
    )


def test_expected_source_sha_mismatch_is_rejected(tmp_path, monkeypatch):
    root, first, second, identity = build_pair(tmp_path, monkeypatch)

    receipt = compare_release_reproducibility_d1l.compare_release_packages(
        root,
        first,
        second,
        "b" * 40,
        source_identity=identity,
        enforce_clean_source=False,
    )

    assert receipt["reproducible"] is False
    assert receipt["failures"] == [
        {
            "code": "source_sha_mismatch",
            "detail": "supplied source identity does not match expected SHA",
        }
    ]


def test_changed_source_input_invalidates_both_packages(tmp_path, monkeypatch):
    root, first, second, identity = build_pair(tmp_path, monkeypatch)
    (root / ".github" / "workflows" / "d1l-ci.yml").write_text(
        "changed after package generation\n", encoding="ascii"
    )

    receipt = compare(root, first, second, identity)

    assert receipt["reproducible"] is False
    assert receipt["packages"] == [
        {"label": "first", "valid": False, "error_code": "invalid_sbom"},
        {"label": "second", "valid": False, "error_code": "invalid_sbom"},
    ]
    assert {item["code"] for item in receipt["failures"]} == {"invalid_sbom"}


def test_invalid_toolchain_lock_is_rejected(tmp_path, monkeypatch):
    root, first, _second, _identity = build_pair(tmp_path, monkeypatch)
    statement = json.loads(
        (first / f"provenance_{COMMIT}.json").read_text(encoding="ascii")
    )
    materials, _subjects = compare_release_reproducibility_d1l.descriptor_maps(
        statement
    )
    modified_materials = copy.deepcopy(materials)
    lock = root / ".github" / "d1l-build-inputs.json"
    lock_data = json.loads(lock.read_text(encoding="ascii"))
    lock_data["esp_idf"]["container"]["reference"] = "docker.io/espressif/idf:mutable"
    lock.write_text(
        json.dumps(lock_data, indent=2, sort_keys=True) + "\n", encoding="ascii"
    )
    modified_materials[".github/d1l-build-inputs.json"]["digest"] = {
        "sha256": sbom_d1l.sha256_file(lock)
    }

    with pytest.raises(
        compare_release_reproducibility_d1l.ComparisonError,
        match="ESP-IDF container digest is not immutable",
    ) as error:
        compare_release_reproducibility_d1l.validate_toolchain_lock(
            root, modified_materials
        )

    assert error.value.code == "invalid_toolchain_lock"


def test_wrong_package_identity_is_rejected(tmp_path, monkeypatch):
    root, first, second, identity = build_pair(
        tmp_path, monkeypatch, second_package_name="d1l-release-not-the-source-sha"
    )

    receipt = compare(root, first, second, identity)

    assert receipt["reproducible"] is False
    assert receipt["packages"][1] == {
        "label": "second",
        "valid": False,
        "error_code": "package_identity_mismatch",
    }
    assert any(
        item["code"] == "package_identity_mismatch" for item in receipt["failures"]
    )


def test_production_wrapper_rejects_dirty_source_before_reading_packages(
    tmp_path, monkeypatch
):
    monkeypatch.setattr(
        compare_release_reproducibility_d1l,
        "git_info",
        lambda _root: {"commit": COMMIT, "dirty": True},
    )

    receipt = compare_release_reproducibility_d1l.compare_release_packages(
        tmp_path,
        tmp_path / "missing-first",
        tmp_path / "missing-second",
        COMMIT,
    )

    assert receipt["reproducible"] is False
    assert receipt["failures"] == [
        {
            "code": "dirty_source",
            "detail": "comparison requires a clean source worktree",
        }
    ]
