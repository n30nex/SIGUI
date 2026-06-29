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


def test_release_package_contains_flash_set_update_and_full_image(tmp_path):
    root = tmp_path
    build = root / "build"
    out = root / "artifacts" / "release"
    write_fake_build(build)

    manifest = package_release_d1l.create_release_package(
        root=root,
        build_dir=build,
        out_dir=out,
        package_name="d1l-test",
        full_size=0x20000,
    )

    package_dir = out / "d1l-test"
    assert manifest["schema"] == 1
    assert manifest["project"] == package_release_d1l.PROJECT
    assert (package_dir / "firmware" / "bootloader.bin").read_bytes() == b"BOOT"
    assert (package_dir / "firmware" / "partition-table.bin").read_bytes() == b"PART"
    assert (package_dir / "firmware" / "meshcore_deskos_d1l.bin").read_bytes() == b"APP"
    assert (package_dir / "update" / "meshcore_deskos_d1l-app.bin").read_bytes() == b"APP"

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
    readme = (package_dir / "README_RELEASE.md").read_text(encoding="ascii")
    assert "App image: `firmware/meshcore_deskos_d1l.bin`" in readme


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
