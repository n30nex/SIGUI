import json
import os
from pathlib import Path

from scripts import package_release_d1l


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
        (artifact_dir / f"{name}.uf2").write_bytes(payload)
        (artifact_dir / "SHA256SUMS.txt").write_text("placeholder\n", encoding="ascii")
    return artifacts


def test_release_package_contains_flash_set_update_and_full_image(tmp_path):
    root = tmp_path
    build = root / "build"
    out = root / "artifacts" / "release"
    write_fake_build(build)
    write_fake_notices(root)
    write_fake_config(root)
    rp2040_artifacts = write_fake_rp2040_artifacts(root)

    manifest = package_release_d1l.create_release_package(
        root=root,
        build_dir=build,
        out_dir=out,
        package_name="d1l-test",
        full_size=0x20000,
        rp2040_artifact_root=rp2040_artifacts,
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
    assert "./docs/USER_GUIDE_D1L.md" in sha_text
    assert "./notices/LICENSE" in sha_text
    assert "./notices/THIRD_PARTY_NOTICES.md" in sha_text
    readme = (package_dir / "README_RELEASE.md").read_text(encoding="ascii")
    assert "App image: `firmware/meshcore_deskos_d1l.bin`" in readme
    assert "`rp2040/` contains the Actions-built RP2040 SD bridge" in readme
    assert "`docs/` contains the user guide" in readme
    assert "`notices/` contains the project license" in readme


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


def test_git_info_treats_expected_bsp_patch_as_clean(monkeypatch, tmp_path):
    def fake_git_value(root, *args):
        if args == ("status", "--porcelain"):
            return "M third_party/sensecap_indicator_esp32"
        if args == ("rev-parse", "HEAD"):
            return "abc123"
        if args == ("rev-parse", "--short", "HEAD"):
            return "abc123"
        if args == ("branch", "--show-current"):
            return "feature/test"
        return None

    monkeypatch.setattr(package_release_d1l, "git_value", fake_git_value)
    monkeypatch.setattr(package_release_d1l, "expected_bsp_patch_applied", lambda root: True)

    info = package_release_d1l.git_info(tmp_path)

    assert info["dirty"] is False
    assert info["dirty_entries"] == []
    assert info["source_patches"] == ["patches/sensecap_indicator_touch_fix.patch"]
