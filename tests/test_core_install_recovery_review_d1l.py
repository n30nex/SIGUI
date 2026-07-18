from scripts import core_install_recovery_review_d1l as review


COMMIT = "c" * 40
RUN_ID = "7654321"
RUN_ATTEMPT = "2"


def confirmations(value: bool = True) -> dict[str, bool]:
    return {
        name: value for name in review.REQUIRED_INSTALL_CONFIRMATIONS
    }


def manifest() -> dict:
    return {
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "firmware_commit": COMMIT,
        "actions_run": RUN_ID,
        "full_feature_release_ready": False,
        "rp2040_artifacts": [],
        "update_image": None,
        "workflow": {
            "sha": COMMIT,
            "run_id": RUN_ID,
            "run_attempt": RUN_ATTEMPT,
            "repository": "n30nex/SIGUI",
        },
        "install_recovery_guide": {
            "usb_only": True,
            "normal_install_script": "flash_project.ps1",
            "normal_install_port": "COM12",
            "normal_install_preserves_unrelated_nvs": True,
            "normal_install_package_root_only": True,
            "normal_install_checksum_verified": True,
            "recovery_script": "flash_full_8mb.ps1",
            "recovery_requires_typed_confirmation": True,
            "recovery_checksum_verified": True,
            "install_guide": "docs/CORE_INSTALL_RECOVERY.md",
            "recovery_guide": "docs/CORE_INSTALL_RECOVERY.md",
            "no_on_device_sd_format": True,
        },
    }


def reviewed_files() -> list[dict]:
    return [
        {
            "path": f"package/{path}",
            "package_path": path,
            "size": 1,
            "sha256": "d" * 64,
        }
        for path in review.REVIEWED_PACKAGE_FILES
    ]


def test_manifest_validation_binds_exact_run_attempt_and_core_profile():
    assert review.validate_package_manifest(
        manifest(),
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )

    changed = manifest()
    changed["workflow"]["run_attempt"] = "3"
    assert not review.validate_package_manifest(
        changed,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )


def test_review_requires_exact_files_distinct_people_and_all_confirmations():
    files = reviewed_files()
    report = review.build_review(
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        operator="device operator",
        reviewer="release reviewer",
        package_manifest=files[0],
        reviewed_files=files,
        package_manifest_valid=True,
        checksum_tree_valid=True,
        confirmations=confirmations(),
    )

    assert report["ok"] is True
    assert report["port"] == "COM12"
    assert report["formats_sd"] is False


def test_review_fails_closed_on_checksum_or_confirmation_failure():
    files = reviewed_files()
    flags = confirmations()
    flags["full_flash_data_loss_warning"] = False
    report = review.build_review(
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        operator="operator",
        reviewer="reviewer",
        package_manifest=files[0],
        reviewed_files=files,
        package_manifest_valid=True,
        checksum_tree_valid=False,
        confirmations=flags,
    )

    assert report["ok"] is False
    assert report["missing_confirmations"] == [
        "full_flash_data_loss_warning"
    ]


def test_review_rejects_extra_or_missing_package_file_receipts():
    files = reviewed_files()[:-1]
    report = review.build_review(
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        operator="operator",
        reviewer="reviewer",
        package_manifest=files[0],
        reviewed_files=files,
        package_manifest_valid=True,
        checksum_tree_valid=True,
        confirmations=confirmations(),
    )

    assert report["ok"] is False
