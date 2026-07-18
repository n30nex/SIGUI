import json
from pathlib import Path

import pytest

from scripts import core_install_recovery_review_d1l as install_review
from scripts import core_release_gate_audit_d1l as audit


COMMIT = "a" * 40
RUN_ID = "123456789"
RUN_ATTEMPT = "2"


def write_package(root: Path) -> Path:
    package = root / "artifacts" / "github" / RUN_ID / "release"
    (package / "docs").mkdir(parents=True)
    manifest = {
        "release_profile": "core_1_0",
        "firmware_commit": COMMIT,
        "actions_run": RUN_ID,
        "actions_run_attempt": RUN_ATTEMPT,
        "sd_history_mode": "disabled",
        "full_feature_release_ready": False,
        "rp2040_artifacts": [],
        "update_image": None,
        "git": {
            "commit": COMMIT,
            "dirty": False,
            "dirty_entries": [],
        },
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
    (package / "manifest.json").write_text(
        json.dumps(manifest), encoding="ascii"
    )
    for relative in (
        "flash_project.ps1",
        "flash_full_8mb.ps1",
        "README_RELEASE.md",
        "SUPPORTED_FEATURES.md",
        "docs/CORE_INSTALL_RECOVERY.md",
    ):
        path = package / relative
        path.write_text(f"{relative}\n", encoding="ascii")
    rows = []
    for path in sorted(
        candidate
        for candidate in package.rglob("*")
        if candidate.is_file()
    ):
        relative = path.relative_to(package).as_posix()
        rows.append(
            f"{install_review.sha256_file(path)}  ./{relative}"
        )
    (package / "SHA256SUMS.txt").write_text(
        "\n".join(rows) + "\n", encoding="ascii"
    )
    return package


def confirmations() -> dict[str, bool]:
    return {
        name: True for name in install_review.INSTALL_REVIEW_CONFIRMATIONS
    }


def clean_source(monkeypatch):
    monkeypatch.setattr(
        install_review,
        "git_metadata",
        lambda _root: {
            "commit": COMMIT,
            "dirty": False,
            "dirty_entries": [],
        },
    )


def test_capture_and_audit_recompute_exact_install_review(
    tmp_path: Path, monkeypatch
):
    package = write_package(tmp_path)
    clean_source(monkeypatch)
    path = install_review.capture(
        root=tmp_path,
        package_dir=package,
        out_path=tmp_path / "evidence" / "install-review.json",
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        operator="release-operator",
        reviewer="independent-reviewer",
        confirmations=confirmations(),
    )

    receipt = json.loads(path.read_text(encoding="ascii"))
    assert receipt["hardware_required"] is False
    assert receipt["physical_observed"] is False
    assert receipt["closure_eligible"] is True
    assert receipt["github_actions_run_attempt"] == 2
    assert receipt["workflow_run_attempt"] == RUN_ATTEMPT
    assert receipt["predecessor_evidence_used"] is False
    assert receipt["reused"] is False
    assert receipt["fabricated"] is False
    assert receipt["public_rf_tx"] is False
    assert receipt["formats_sd"] is False
    assert [row["path"] for row in receipt["reviewed_files"]] == list(
        install_review.REVIEWED_PACKAGE_FILES
    )
    ok, reasons, details = install_review.validate_install_review_receipt(
        path,
        root=tmp_path,
        package_dir=package,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )
    assert ok is True
    assert reasons == []
    assert details["checksum_tree_ok"] is True
    assert audit.install_review_gate(
        path,
        tmp_path,
        COMMIT,
        RUN_ID,
        RUN_ATTEMPT,
        package,
    ).ok

    (package / "README_RELEASE.md").write_text(
        "tampered\n", encoding="ascii"
    )
    ok, reasons, _ = install_review.validate_install_review_receipt(
        path,
        root=tmp_path,
        package_dir=package,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )
    assert ok is False
    assert "checksum_tree_invalid" in reasons
    assert not audit.install_review_gate(
        path,
        tmp_path,
        COMMIT,
        RUN_ID,
        RUN_ATTEMPT,
        package,
    ).ok


def test_install_review_rejects_people_confirmations_and_attempt(
    tmp_path: Path, monkeypatch
):
    package = write_package(tmp_path)
    clean_source(monkeypatch)
    with pytest.raises(ValueError, match="distinct"):
        install_review.capture(
            root=tmp_path,
            package_dir=package,
            out_path=tmp_path / "same-person.json",
            commit=COMMIT,
            run_id=RUN_ID,
            run_attempt=RUN_ATTEMPT,
            operator="same",
            reviewer="Same",
            confirmations=confirmations(),
        )
    extra = confirmations()
    extra["invented_confirmation"] = True
    with pytest.raises(ValueError, match="exact"):
        install_review.capture(
            root=tmp_path,
            package_dir=package,
            out_path=tmp_path / "extra.json",
            commit=COMMIT,
            run_id=RUN_ID,
            run_attempt=RUN_ATTEMPT,
            operator="operator",
            reviewer="reviewer",
            confirmations=extra,
        )
    path = install_review.capture(
        root=tmp_path,
        package_dir=package,
        out_path=tmp_path / "valid.json",
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        operator="operator",
        reviewer="reviewer",
        confirmations=confirmations(),
    )
    assert not install_review.validate_install_review_receipt(
        path,
        root=tmp_path,
        package_dir=package,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt="3",
    )[0]
    receipt = json.loads(path.read_text(encoding="ascii"))
    receipt["reused"] = True
    path.write_text(json.dumps(receipt), encoding="ascii")
    assert not install_review.validate_install_review_receipt(
        path,
        root=tmp_path,
        package_dir=package,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )[0]
