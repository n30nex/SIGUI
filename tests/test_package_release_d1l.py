import json
import os
import subprocess
from datetime import datetime, timedelta, timezone
from pathlib import Path

from scripts import package_release_d1l
from scripts.verify_checksums import verify_sha256_manifest


def run_git(cwd: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ).stdout


def create_exact_bsp_patch_fixture(root: Path) -> Path:
    submodule = root / package_release_d1l.EXPECTED_BSP_SUBMODULE
    submodule.mkdir(parents=True)
    run_git(submodule, "init")
    run_git(submodule, "config", "user.email", "tests@example.invalid")
    run_git(submodule, "config", "user.name", "D1L Tests")
    run_git(submodule, "config", "core.autocrlf", "false")
    tracked = {
        "touch.c": (b"touch base\n", b"touch patched\n"),
        "compat.c": (b"compat base\n", b"compat patched\n"),
    }
    for name, (base, _patched) in tracked.items():
        (submodule / name).write_bytes(base)
    run_git(submodule, "add", ".")
    run_git(submodule, "commit", "-m", "base")

    patch_dir = root / "patches"
    patch_dir.mkdir(parents=True)
    for relative_patch, (name, (base, patched)) in zip(
        package_release_d1l.EXPECTED_BSP_PATCHES,
        tracked.items(),
    ):
        (submodule / name).write_bytes(patched)
        patch_text = run_git(
            submodule,
            "diff",
            "--binary",
            "--no-ext-diff",
            "--src-prefix=a/",
            "--dst-prefix=b/",
            "--",
            name,
        )
        (submodule / name).write_bytes(base)
        (root / relative_patch).write_text(patch_text, encoding="utf-8")

    run_git(root, "init")
    run_git(root, "config", "user.email", "tests@example.invalid")
    run_git(root, "config", "user.name", "D1L Tests")
    run_git(root, "config", "core.autocrlf", "false")
    run_git(root, "add", package_release_d1l.EXPECTED_BSP_SUBMODULE.as_posix(), "patches")
    run_git(root, "commit", "-m", "root")
    for relative_patch in package_release_d1l.EXPECTED_BSP_PATCHES:
        run_git(
            submodule,
            "apply",
            "--unidiff-zero",
            "--ignore-space-change",
            str(root / relative_patch),
        )
    return submodule


def write_fake_build(build: Path) -> None:
    (build / "bootloader").mkdir(parents=True)
    (build / "partition_table").mkdir(parents=True)
    (build / "bootloader" / "bootloader.bin").write_bytes(b"BOOT")
    (build / "partition_table" / "partition-table.bin").write_bytes(b"PART")
    (build / "meshcore_deskos_d1l.bin").write_bytes(b"APP")
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


def write_fake_notices(root: Path) -> None:
    (root / "docs").mkdir(exist_ok=True)
    (root / "LICENSE").write_text("project license\n", encoding="ascii")
    (root / "THIRD_PARTY_NOTICES.md").write_text("third party notices\n", encoding="ascii")
    (root / "docs" / "ATTRIBUTIONS.md").write_text("attributions\n", encoding="ascii")
    (root / "docs" / "SOURCE_AUDIT_AND_ATTRIBUTION.md").write_text("source audit\n", encoding="ascii")
    (root / "docs" / "USER_GUIDE_D1L.md").write_text("user guide\n", encoding="ascii")
    (root / "docs" / "DEVELOPER_GUIDE_D1L.md").write_text("developer guide\n", encoding="ascii")
    (root / "docs" / "FLASH_RECOVERY_D1L.md").write_text("flash recovery\n", encoding="ascii")
    (root / "docs" / "RP2040_SD_BRIDGE_FLASH_D1L.md").write_text("rp2040 guide\n", encoding="ascii")


def write_fake_config(root: Path) -> None:
    (root / "main").mkdir(exist_ok=True)
    (root / "main" / "d1l_config.h").write_text(
        '#define D1L_FIRMWARE_NAME "MeshCore DeskOS D1L"\n'
        '#define D1L_FIRMWARE_VERSION "1.0.0-rc1"\n',
        encoding="ascii",
    )


def write_fake_rp2040_artifacts(root: Path) -> Path:
    artifacts = root / "artifacts" / "rp2040-release-inputs"
    for name, payload in {
        "rp2040-sd-bridge-firmware": b"BRIDGE",
        "rp2040-sd-smoke-firmware": b"SMOKE",
        "rp2040-seeed-official-sd-smoke-firmware": b"OFFICIAL",
    }.items():
        artifact_dir = artifacts / name
        artifact_dir.mkdir(parents=True)
        uf2 = artifact_dir / f"{name}.uf2"
        uf2.write_bytes(payload)
        (artifact_dir / "SHA256SUMS.txt").write_text(
            f"{package_release_d1l.sha256_file(uf2)}  ./{uf2.name}\n",
            encoding="ascii",
        )
    return artifacts


def write_meshcore_conformance(
    root: Path,
    commit: str,
    *,
    generated_at: datetime | None = None,
    **overrides: object,
) -> Path:
    payload = {
        "schema_version": 1,
        "artifact_type": package_release_d1l.MESHCORE_CONFORMANCE_ARTIFACT_TYPE,
        "generated_at": (generated_at or datetime.now(timezone.utc)).isoformat().replace("+00:00", "Z"),
        "passed": True,
        "status": "pass",
        "execution_complete": True,
        "coverage_boundary": package_release_d1l.MESHCORE_CONFORMANCE_BOUNDARY,
        "coverage_level": package_release_d1l.MESHCORE_CONFORMANCE_BOUNDARY,
        "closure_ready": False,
        "issue_65_closure_eligible": False,
        "source_verification": {"repository_commit": commit},
    }
    payload.update(overrides)
    path = root / f"meshcore_conformance_{commit}.json"
    path.write_text(json.dumps(payload), encoding="utf-8")
    return path


def test_release_package_contains_flash_set_update_and_full_image(tmp_path, monkeypatch):
    root = tmp_path
    build = root / "build"
    out = root / "artifacts" / "release"
    write_fake_build(build)
    write_fake_notices(root)
    write_fake_config(root)
    rp2040_artifacts = write_fake_rp2040_artifacts(root)
    commit = "a" * 40
    conformance = write_meshcore_conformance(root, commit)
    monkeypatch.setenv("GITHUB_SHA", commit)

    manifest = package_release_d1l.create_release_package(
        root=root,
        build_dir=build,
        out_dir=out,
        package_name="d1l-test",
        full_size=0x20000,
        rp2040_artifact_root=rp2040_artifacts,
        meshcore_conformance_json=conformance,
    )

    package_dir = out / "d1l-test"
    assert manifest["schema"] == 1
    assert manifest["project"] == package_release_d1l.PROJECT
    assert manifest["app_version"] == "1.0.0-rc1"
    assert "workflow" in manifest
    assert (package_dir / "firmware" / "bootloader.bin").read_bytes() == b"BOOT"
    assert (package_dir / "firmware" / "partition-table.bin").read_bytes() == b"PART"
    assert (package_dir / "firmware" / "meshcore_deskos_d1l.bin").read_bytes() == b"APP"
    assert (package_dir / "update" / "meshcore_deskos_d1l-app.bin").read_bytes() == b"APP"
    assert (package_dir / "notices" / "LICENSE").read_text(encoding="ascii") == "project license\n"
    assert (package_dir / "notices" / "THIRD_PARTY_NOTICES.md").read_text(encoding="ascii") == "third party notices\n"
    assert (package_dir / "notices" / "ATTRIBUTIONS.md").read_text(encoding="ascii") == "attributions\n"
    assert (
        package_dir / "notices" / "SOURCE_AUDIT_AND_ATTRIBUTION.md"
    ).read_text(encoding="ascii") == "source audit\n"
    assert [item["path"] for item in manifest["notice_files"]] == [
        "notices/LICENSE",
        "notices/THIRD_PARTY_NOTICES.md",
        "notices/ATTRIBUTIONS.md",
        "notices/SOURCE_AUDIT_AND_ATTRIBUTION.md",
    ]
    assert [item["path"] for item in manifest["release_docs"]] == [
        "docs/USER_GUIDE_D1L.md",
        "docs/DEVELOPER_GUIDE_D1L.md",
        "docs/FLASH_RECOVERY_D1L.md",
        "docs/RP2040_SD_BRIDGE_FLASH_D1L.md",
    ]
    assert [item["name"] for item in manifest["rp2040_artifacts"]] == [
        "rp2040-sd-bridge-firmware",
        "rp2040-sd-smoke-firmware",
        "rp2040-seeed-official-sd-smoke-firmware",
    ]
    for artifact in manifest["rp2040_artifacts"]:
        assert artifact["uf2_files"]
        assert (package_dir / artifact["uf2_files"][0]).is_file()
    conformance_metadata = manifest["meshcore_conformance"]
    packaged_conformance = package_dir / conformance_metadata["path"]
    assert packaged_conformance.read_bytes() == conformance.read_bytes()
    assert conformance_metadata["source_commit"] == commit
    assert conformance_metadata["coverage_level"] == "wire_envelope_only"
    assert conformance_metadata["closure_ready"] is False
    assert conformance_metadata["issue_65_closure_eligible"] is False
    assert conformance_metadata["sha256"] == package_release_d1l.sha256_file(packaged_conformance)

    full = package_dir / manifest["full_flash_image"]["path"]
    image = full.read_bytes()
    assert len(image) == 0x20000
    assert image[0:4] == b"BOOT"
    assert image[0x8000 : 0x8004] == b"PART"
    assert image[0x10000 : 0x10003] == b"APP"
    assert image[0x9000] == 0xFF

    sha_text = (package_dir / "SHA256SUMS.txt").read_text(encoding="ascii")
    assert "./firmware/meshcore_deskos_d1l.bin" in sha_text
    assert "./full-flash/meshcore_deskos_d1l-full-8mb.bin" in sha_text
    assert "./manifest.json" in sha_text
    assert "./rp2040/rp2040-sd-bridge-firmware/rp2040-sd-bridge-firmware.uf2" in sha_text
    assert "./rp2040/rp2040-seeed-official-sd-smoke-firmware/rp2040-seeed-official-sd-smoke-firmware.uf2" in sha_text
    assert "./rp2040/rp2040-sd-bridge-firmware/SHA256SUMS.txt" in sha_text
    assert "./rp2040/rp2040-sd-smoke-firmware/SHA256SUMS.txt" in sha_text
    assert "./rp2040/rp2040-seeed-official-sd-smoke-firmware/SHA256SUMS.txt" in sha_text
    assert "./docs/USER_GUIDE_D1L.md" in sha_text
    assert "./notices/LICENSE" in sha_text
    assert "./notices/THIRD_PARTY_NOTICES.md" in sha_text
    assert f"./evidence/meshcore_conformance_{commit}.json" in sha_text
    assert verify_sha256_manifest(package_dir / "SHA256SUMS.txt")
    for artifact in manifest["rp2040_artifacts"]:
        nested_manifest = package_dir / "rp2040" / artifact["name"] / "SHA256SUMS.txt"
        assert verify_sha256_manifest(nested_manifest)
    readme = (package_dir / "README_RELEASE.md").read_text(encoding="ascii")
    assert "App image: `firmware/meshcore_deskos_d1l.bin`" in readme
    assert "`rp2040/` contains the Actions-built RP2040 SD bridge" in readme
    assert "`docs/` contains the user guide" in readme
    assert "`notices/` contains the project license" in readme
    assert "structural prerequisite and does not close issue #65" in readme


def test_release_package_rejects_mismatched_or_expired_meshcore_evidence(tmp_path, monkeypatch):
    build = tmp_path / "build"
    out = tmp_path / "artifacts" / "release"
    write_fake_build(build)
    commit = "b" * 40
    monkeypatch.setenv("GITHUB_SHA", commit)

    mismatched = write_meshcore_conformance(
        tmp_path,
        commit,
        source_verification={"repository_commit": "c" * 40},
    )
    try:
        package_release_d1l.create_release_package(
            root=tmp_path,
            build_dir=build,
            out_dir=out,
            package_name="mismatch",
            full_size=0x20000,
            meshcore_conformance_json=mismatched,
        )
    except ValueError as exc:
        assert "source_commit" in str(exc)
    else:
        raise AssertionError("mismatched MeshCore commit was accepted")

    expired = write_meshcore_conformance(
        tmp_path,
        commit,
        generated_at=datetime.now(timezone.utc)
        - timedelta(days=package_release_d1l.MESHCORE_CONFORMANCE_MAX_AGE_DAYS + 1),
    )
    try:
        package_release_d1l.create_release_package(
            root=tmp_path,
            build_dir=build,
            out_dir=out,
            package_name="expired",
            full_size=0x20000,
            meshcore_conformance_json=expired,
        )
    except ValueError as exc:
        assert "expired" in str(exc)
    else:
        raise AssertionError("expired MeshCore evidence was accepted")

    far_future = write_meshcore_conformance(
        tmp_path,
        commit,
        generated_at=datetime(9999, 12, 31, tzinfo=timezone.utc),
    )
    try:
        package_release_d1l.create_release_package(
            root=tmp_path,
            build_dir=build,
            out_dir=out,
            package_name="future-overflow",
            full_size=0x20000,
            meshcore_conformance_json=far_future,
        )
    except ValueError as exc:
        assert "future" in str(exc) or "supported range" in str(exc)
    else:
        raise AssertionError("out-of-range MeshCore evidence was accepted")


def test_generated_flash_scripts_require_explicit_port(tmp_path):
    root = tmp_path
    build = root / "build"
    out = root / "artifacts" / "release"
    write_fake_build(build)

    package_release_d1l.create_release_package(
        root=root,
        build_dir=build,
        out_dir=out,
        package_name="d1l-test",
        full_size=0x20000,
    )

    ps1 = (out / "d1l-test" / "flash_project.ps1").read_text(encoding="ascii")
    sh = (out / "d1l-test" / "flash_project.sh").read_text(encoding="ascii")
    full_ps1 = (out / "d1l-test" / "flash_full_8mb.ps1").read_text(encoding="ascii")

    assert "$env:D1L_PORT" in ps1
    assert "No D1L port supplied" in ps1
    assert "${D1L_PORT:?" in sh
    assert os.access(out / "d1l-test" / "flash_project.sh", os.X_OK)
    assert "FULL-FLASH-$Port" in full_ps1
    assert "COM7" not in ps1
    assert "COM11" not in ps1


def test_esp32_only_release_package_omits_rp2040_artifacts(tmp_path):
    root = tmp_path
    build = root / "build"
    out = root / "artifacts" / "release"
    write_fake_build(build)
    write_fake_notices(root)
    write_fake_config(root)

    manifest = package_release_d1l.create_release_package(
        root=root,
        build_dir=build,
        out_dir=out,
        package_name="d1l-esp32-only",
        full_size=0x20000,
    )

    package_dir = out / "d1l-esp32-only"
    assert manifest["rp2040_artifacts"] == []
    assert not (package_dir / "rp2040").exists()
    sha_text = (package_dir / "SHA256SUMS.txt").read_text(encoding="ascii")
    assert "./firmware/meshcore_deskos_d1l.bin" in sha_text
    assert "./rp2040/" not in sha_text
    readme = (package_dir / "README_RELEASE.md").read_text(encoding="ascii")
    assert "`rp2040/` is omitted from this ESP32-only package" in readme
    assert "`include_sd_bridge=true`" in readme


def test_git_info_treats_expected_bsp_patches_as_clean(monkeypatch, tmp_path):
    def fake_git_value(root, *args):
        if args == ("status", "--porcelain"):
            return " m third_party/sensecap_indicator_esp32"
        if args == ("rev-parse", "HEAD"):
            return "abc123"
        if args == ("rev-parse", "--short", "HEAD"):
            return "abc123"
        if args == ("branch", "--show-current"):
            return "feature/test"
        return None

    monkeypatch.setattr(package_release_d1l, "git_value", fake_git_value)
    monkeypatch.setattr(package_release_d1l, "exact_expected_bsp_patch_state", lambda root: True)

    info = package_release_d1l.git_info(tmp_path)

    assert info["dirty"] is False
    assert info["dirty_entries"] == []
    assert info["source_patches"] == [
        "patches/sensecap_indicator_touch_fix.patch",
        "patches/sensecap_indicator_idf55_compat.patch",
    ]


def test_exact_bsp_patch_state_accepts_only_the_tracked_patch_tree(tmp_path):
    create_exact_bsp_patch_fixture(tmp_path)

    assert package_release_d1l.exact_expected_bsp_patch_state(tmp_path) is True
    info = package_release_d1l.git_info(tmp_path)
    assert info["dirty"] is False
    assert info["dirty_entries"] == []
    assert info["source_patches"] == [path.as_posix() for path in package_release_d1l.EXPECTED_BSP_PATCHES]


def test_exact_bsp_patch_state_rejects_extra_tracked_delta(tmp_path):
    submodule = create_exact_bsp_patch_fixture(tmp_path)
    with (submodule / "touch.c").open("ab") as stream:
        stream.write(b"unexpected tracked delta\n")

    assert package_release_d1l.exact_expected_bsp_patch_state(tmp_path) is False
    assert package_release_d1l.git_info(tmp_path)["dirty"] is True


def test_exact_bsp_patch_state_rejects_untracked_content(tmp_path):
    submodule = create_exact_bsp_patch_fixture(tmp_path)
    (submodule / "unexpected.c").write_bytes(b"unexpected untracked content\n")

    assert package_release_d1l.exact_expected_bsp_patch_state(tmp_path) is False
    assert package_release_d1l.git_info(tmp_path)["dirty"] is True


def test_expected_bsp_patch_set_fails_closed_when_any_reverse_check_fails(monkeypatch, tmp_path):
    submodule = tmp_path / package_release_d1l.EXPECTED_BSP_SUBMODULE
    submodule.mkdir(parents=True)
    for relative_patch in package_release_d1l.EXPECTED_BSP_PATCHES:
        patch = tmp_path / relative_patch
        patch.parent.mkdir(parents=True, exist_ok=True)
        patch.write_text("patch", encoding="utf-8")

    calls = []

    def fake_command_succeeds(cwd, args):
        calls.append((cwd, args))
        return len(calls) == 1

    monkeypatch.setattr(package_release_d1l, "command_succeeds", fake_command_succeeds)

    assert package_release_d1l.expected_bsp_patches_applied(tmp_path) is False
    assert len(calls) == 2
