from pathlib import Path

from scripts import manual_ui_review_d1l as review


def confirmations(value: bool = True) -> dict[str, bool]:
    return {name: value for name in review.REQUIRED_CONFIRMATIONS}


def test_manual_review_requires_photos_and_all_confirmations(tmp_path: Path):
    photo_dir = tmp_path / "photos"
    photo_dir.mkdir()
    (photo_dir / "home.png").write_bytes(b"png")
    (photo_dir / "notes.txt").write_text("not a photo", encoding="ascii")

    report = review.build_review(
        port="COM" + "12",
        reviewer="tester",
        photo_dir=photo_dir,
        confirmations=confirmations(),
        notes="physical review complete",
    )

    assert report["ok"] is True
    assert report["photo_count"] == 1
    assert report["missing_confirmations"] == []
    assert report["confirmations"]["display_stable"] is True


def test_manual_review_fails_without_photos_or_confirmation(tmp_path: Path):
    flags = confirmations()
    flags["dm_workflow"] = False

    report = review.build_review(
        port="COM" + "12",
        reviewer="tester",
        photo_dir=tmp_path / "missing",
        confirmations=flags,
    )

    assert report["ok"] is False
    assert report["photo_count"] == 0
    assert report["missing_confirmations"] == ["dm_workflow"]
