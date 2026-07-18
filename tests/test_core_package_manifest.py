import pytest

from scripts import package_release_d1l
from tests.test_package_release_d1l import (
    install_fake_source_identity,
    write_fake_build,
    write_fake_config,
    write_fake_notices,
    write_fake_rp2040_artifacts,
)


def test_core_disabled_package_binds_truth_and_omits_rp2040(
    tmp_path, monkeypatch
):
    root = tmp_path
    build = root / "build"
    out = root / "release"
    write_fake_build(build)
    write_fake_notices(root)
    write_fake_config(root)
    rp2040 = write_fake_rp2040_artifacts(root)
    commit = "a" * 40
    monkeypatch.setenv("GITHUB_SHA", commit)
    monkeypatch.setenv("GITHUB_RUN_ID", "123456789")
    monkeypatch.setenv("GITHUB_RUN_ATTEMPT", "1")
    monkeypatch.setenv("GITHUB_REPOSITORY", "n30nex/SIGUI")
    monkeypatch.setenv("GITHUB_REF", "refs/heads/release/24h-core")
    install_fake_source_identity(monkeypatch, commit)

    manifest = package_release_d1l.create_release_package(
        root=root,
        build_dir=build,
        out_dir=out,
        package_name="core-disabled",
        full_size=0x20000,
        rp2040_artifact_root=rp2040,
        release_profile="core_1_0",
        sd_history_mode="disabled",
    )

    package = out / "core-disabled"
    truth = package_release_d1l.core_capability_truth("disabled")
    assert manifest["release_profile"] == "core_1_0"
    assert manifest["firmware_commit"] == commit
    assert manifest["actions_run"] == "123456789"
    assert manifest["actions_run_attempt"] == "1"
    assert manifest["supported_capabilities"] == truth["supported_capabilities"]
    assert (
        manifest["unavailable_capabilities"]
        == truth["unavailable_capabilities"]
    )
    assert "sd_history" in manifest["unavailable_capabilities"]
    assert manifest["sd_history_mode"] == "disabled"
    assert manifest["storage_authority"] == "nvs"
    assert manifest["full_feature_release_ready"] is False
    assert manifest["rp2040_artifacts"] == []
    assert manifest["update_image"] is None
    assert not (package / "rp2040").exists()
    assert not (package / "update").exists()
    assert manifest["release_docs"] == [
        {
            "path": "docs/CORE_INSTALL_RECOVERY.md",
            "source": "generated_core_profile",
            "size": (
                package / "docs" / "CORE_INSTALL_RECOVERY.md"
            ).stat().st_size,
            "sha256": package_release_d1l.sha256_file(
                package / "docs" / "CORE_INSTALL_RECOVERY.md"
            ),
        }
    ]
    assert (package / "docs" / "CORE_INSTALL_RECOVERY.md").is_file()
    for excluded in (
        "USER_GUIDE_D1L.md",
        "DEVELOPER_GUIDE_D1L.md",
        "FLASH_RECOVERY_D1L.md",
        "RP2040_SD_BRIDGE_FLASH_D1L.md",
    ):
        assert not (package / "docs" / excluded).exists()
    assert (package / "SUPPORTED_FEATURES.md").is_file()
    supported_text = (package / "SUPPORTED_FEATURES.md").read_text(
        encoding="ascii"
    )
    assert "SD history is disabled and deferred" in supported_text
    assert "NVS is authoritative" in supported_text
    assert "Never format an SD card on the device" in supported_text
    assert manifest["supported_features"]["sha256"] == (
        package_release_d1l.sha256_file(package / "SUPPORTED_FEATURES.md")
    )
    assert manifest["install_recovery_guide"]["normal_install_port"] == "COM12"
    assert manifest["install_recovery_guide"]["install_guide"] == (
        "docs/CORE_INSTALL_RECOVERY.md"
    )
    assert (
        manifest["install_recovery_guide"]["no_on_device_sd_format"] is True
    )
    assert "Core 1.0 D1L flashing requires COM12" in (
        package / "flash_project.ps1"
    ).read_text(encoding="ascii")
    flash_script = (package / "flash_project.ps1").read_text(
        encoding="ascii"
    )
    assert "Duplicate checksum path" in flash_script
    assert "Unchecksummed package file" in flash_script
    assert "complete one-to-one package file inventory" in flash_script
    assert flash_script.index("Assert-PackageChecksums") < flash_script.index(
        "python -m esptool"
    )
    assert "./rp2040/" not in (package / "SHA256SUMS.txt").read_text(
        encoding="ascii"
    )

    capability_payload = package_release_d1l.load_required_json_object(
        package / manifest["capability_manifest"]["path"],
        "Core capability manifest",
    )
    assert capability_payload["release_profile"] == "core_1_0"
    assert capability_payload["sd_history_mode"] == "disabled"
    assert capability_payload["supported_capabilities"] == truth[
        "supported_capabilities"
    ]
    assert capability_payload["capabilities"] == [
        {"id": capability, "core_state": "supported"}
        for capability in truth["supported_capabilities"]
    ] + [
        {"id": capability, "core_state": "unavailable"}
        for capability in truth["unavailable_capabilities"]
    ]
    assert capability_payload["full_feature_release_ready"] is False


def test_core_supported_optional_package_requires_paired_rp2040(
    tmp_path, monkeypatch
):
    build = tmp_path / "build"
    write_fake_build(build)
    write_fake_notices(tmp_path)
    write_fake_config(tmp_path)
    commit = "b" * 40
    monkeypatch.setenv("GITHUB_SHA", commit)
    monkeypatch.setenv("GITHUB_RUN_ID", "234567890")
    monkeypatch.setenv("GITHUB_RUN_ATTEMPT", "1")
    monkeypatch.setenv("GITHUB_REPOSITORY", "n30nex/SIGUI")
    monkeypatch.setenv("GITHUB_REF", "refs/heads/release/24h-core")
    install_fake_source_identity(monkeypatch, commit)

    with pytest.raises(ValueError, match="paired RP2040"):
        package_release_d1l.create_release_package(
            root=tmp_path,
            build_dir=build,
            out_dir=tmp_path / "release",
            package_name="core-sd",
            full_size=0x20000,
            release_profile="core_1_0",
            sd_history_mode="supported_optional",
        )


def test_core_package_requires_exact_actions_run(tmp_path, monkeypatch):
    build = tmp_path / "build"
    write_fake_build(build)
    write_fake_notices(tmp_path)
    write_fake_config(tmp_path)
    commit = "c" * 40
    monkeypatch.setenv("GITHUB_SHA", commit)
    monkeypatch.delenv("GITHUB_RUN_ID", raising=False)
    monkeypatch.setenv("GITHUB_RUN_ATTEMPT", "1")
    install_fake_source_identity(monkeypatch, commit)

    with pytest.raises(ValueError, match="numeric GitHub Actions run ID"):
        package_release_d1l.create_release_package(
            root=tmp_path,
            build_dir=build,
            out_dir=tmp_path / "release",
            package_name="core-no-run",
            full_size=0x20000,
            release_profile="core_1_0",
            sd_history_mode="disabled",
        )


def test_build_release_settings_read_exact_cmake_cache(tmp_path):
    (tmp_path / "CMakeCache.txt").write_text(
        "\n".join(
            (
                "D1L_RELEASE_PROFILE:STRING=core_1_0",
                "D1L_SD_HISTORY_MODE:STRING=disabled",
            )
        )
        + "\n",
        encoding="ascii",
    )

    assert package_release_d1l.build_release_settings(tmp_path) == (
        "core_1_0",
        "disabled",
    )
    with pytest.raises(ValueError, match="does not match configured firmware"):
        package_release_d1l.build_release_settings(
            tmp_path, release_profile="full_feature"
        )


def test_default_callable_package_profile_behavior_is_unchanged(tmp_path):
    assert package_release_d1l.build_release_settings(tmp_path) == (
        "full_feature",
        "conditional",
    )
