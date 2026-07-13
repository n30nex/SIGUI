from pathlib import Path

import pytest

from scripts import storage_active_soak_d1l as active


COMMIT = "ad0aa5bda21435846f6e7fcdde8fd87a85c5da5c"


def test_dry_run_is_strict_non_rf_alternating_plan():
    report = active.dry_run_report(
        port="COM12",
        baud=115200,
        expected_firmware_commit=COMMIT,
    )

    assert report["ok"] is True
    assert report["event_topology"] == [
        "segment",
        "reboot",
        "segment",
        "reboot",
        "segment",
        "reboot",
        "segment",
    ]
    assert report["scheduled_segment_duration_sec"] == 7200
    assert report["public_rf_tx"] is False
    assert report["dm_rf_tx"] is False
    assert report["formats_sd"] is False
    for event in report["events"]:
        nested = event["report"]
        assert nested["public_rf_tx"] is False
        assert nested["formats_sd"] is False
        if event["kind"] == "segment":
            assert nested["active_command"] is None
            assert nested["dm_rf_tx"] is False
            assert nested["command_retries"] == 0
        else:
            assert "storage retained-canary " in "\n".join(nested["commands"])


@pytest.mark.parametrize(
    ("segment_count", "segment_duration_sec"),
    [(1, 3600.0), (2, 1799.0)],
)
def test_dry_run_rejects_non_qualifying_source_shape(
    segment_count: int, segment_duration_sec: float
):
    with pytest.raises(ValueError):
        active.dry_run_report(
            port="COM12",
            baud=115200,
            expected_firmware_commit=COMMIT,
            segment_count=segment_count,
            segment_duration_sec=segment_duration_sec,
        )


def test_hardware_runner_uses_no_transport_retries_and_requires_canary_safety(
    tmp_path: Path, monkeypatch
):
    segment_calls = []
    reboot_calls = []

    def fake_segment(**kwargs):
        segment_calls.append(kwargs)
        return {"schema": 1, "mode": "hardware", "ok": True}

    def fake_reboot(*args, **kwargs):
        reboot_calls.append((args, kwargs))
        token = args[3]
        return {
            "schema": 1,
            "mode": "hardware",
            "ok": True,
            "results": [
                {
                    "schema": 1,
                    "ok": True,
                    "cmd": "storage retained-canary",
                    "token": token,
                    "public_rf_tx": False,
                    "dm_rf_tx": False,
                    "formats_sd": False,
                }
            ],
        }

    def fake_stamp(report, _root):
        report.setdefault("commit", COMMIT)
        report.setdefault(
            "git", {"commit": COMMIT, "dirty": False, "dirty_entries": []}
        )
        return report

    monkeypatch.setattr(active, "run_serial_soak", fake_segment)
    monkeypatch.setattr(active, "run_acceptance", fake_reboot)
    monkeypatch.setattr(active, "stamp_report", fake_stamp)

    report = active.run_storage_active_soak(
        root=tmp_path,
        port="COM12",
        baud=115200,
        timeout=5.0,
        expected_firmware_commit=COMMIT,
        segment_count=2,
        segment_duration_sec=1800.0,
    )

    assert report["ok"] is True
    assert report["event_topology"] == ["segment", "reboot", "segment"]
    assert len(segment_calls) == 2
    assert len(reboot_calls) == 1
    assert all(call["command_retries"] == 0 for call in segment_calls)
    assert all(call["active_dm_fingerprint"] is None for call in segment_calls)
    assert reboot_calls[0][1]["include_reboot"] is True
    assert reboot_calls[0][1]["expected_firmware_commit"] == COMMIT


def test_hardware_runner_fails_when_canary_lacks_explicit_dm_safety(
    tmp_path: Path, monkeypatch
):
    monkeypatch.setattr(
        active,
        "run_serial_soak",
        lambda **_kwargs: {"schema": 1, "mode": "hardware", "ok": True},
    )
    monkeypatch.setattr(
        active,
        "run_acceptance",
        lambda *_args, **_kwargs: {
            "schema": 1,
            "mode": "hardware",
            "ok": True,
            "results": [
                {
                    "schema": 1,
                    "ok": True,
                    "cmd": "storage retained-canary",
                    "public_rf_tx": False,
                    "formats_sd": False,
                }
            ],
        },
    )
    monkeypatch.setattr(active, "stamp_report", lambda report, _root: report)

    report = active.run_storage_active_soak(
        root=tmp_path,
        port="COM12",
        baud=115200,
        timeout=5.0,
        expected_firmware_commit=COMMIT,
        segment_count=2,
        segment_duration_sec=1800.0,
    )

    assert report["ok"] is False
    assert report["failure"] == "reboot_1_canary_safety_missing"
    assert report["events"][-1]["canary_safety_explicit"] is False
