from pathlib import Path

from scripts import (
    scroll_probe_d1l,
    smoke_d1l,
    ui_capture_d1l,
    ui_capture_diff_d1l,
    ui_compose_keyboard_capture_d1l,
    ui_corruption_probe_d1l,
)

ROOT = Path(__file__).resolve().parents[1]


class FakeSerial:
    def __init__(self, responses: list[bytes]):
        self.responses = list(responses)
        self.timeout = 99.0
        self.seen_timeouts: list[float | None] = []

    def readline(self) -> bytes:
        self.seen_timeouts.append(self.timeout)
        if self.responses:
            return self.responses.pop(0)
        return b""


def test_ui_corruption_probe_dry_run_is_explicit_port_safe():
    report = ui_corruption_probe_d1l.dry_run_report(
        rounds=20,
        settle_sec=0.1,
        clear_crashlog_before_start=True,
        skip_data_canary=False,
    )

    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["hardware_required"] is False
    assert report["explicit_port_required"] is True
    assert report["kind"] == "ui_corruption_probe"
    assert report["rounds"] == 20
    assert report["release_min_rounds"] == 20
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert "heap_free" in report["telemetry_fields"]
    assert "ui_task_stack_free_words" in report["telemetry_fields"]
    assert report["tabs"] == ["home", "messages", "nodes", "map", "packets", "settings"]
    assert "ui status" in report["commands"]
    assert "ui tab packets" in report["commands"]
    assert "ui data-canary uiRx0001" in report["commands"]
    assert report["checks"]["data_refresh_exercised"] is True
    assert report["checks"]["no_stuck_pending"] is True
    assert report["checks"]["final_active_tab_known"] is True


def test_ui_corruption_probe_uses_nested_refresh_status_for_final_pending():
    report = ui_corruption_probe_d1l.summarize_telemetry(
        [
            {
                "kind": "tab",
                "statuses": [{"ok": True, "active_tab": "settings", "pending": False}],
                "health": {"ok": True, "uptime_ms": 1000},
            },
            {
                "kind": "data_refresh",
                "packet_tab": {
                    "statuses": [{"ok": True, "active_tab": "packets", "pending": False}],
                },
                "messages_tab": {
                    "statuses": [{"ok": True, "active_tab": "messages", "pending": False}],
                },
                "status": {"ok": True, "active_tab": "messages", "pending": False},
                "health": {"ok": True, "uptime_ms": 1200},
            },
        ]
    )

    assert report["uptime_monotonic"] is True
    assert report["final_active_tab"] == "messages"
    assert report["final_pending"] is False


def test_ui_corruption_probe_requires_token_in_result_entries():
    assert ui_corruption_probe_d1l.result_entries_contain_token(
        {"filter": {"search": "uiRx0001"}, "entries": []},
        "uiRx0001",
    ) is False
    assert ui_corruption_probe_d1l.result_entries_contain_token(
        {"entries": [{"note": "ui-canary uiRx0001"}]},
        "uiRx0001",
    ) is True


def test_scroll_probe_dry_run_and_screen_parser():
    screens = scroll_probe_d1l.parse_screens("home,public,dm-thread,nodes,packets,settings,storage,wi-fi,map")
    report = scroll_probe_d1l.dry_run_report(screens, dwell_sec=0.5, manual_touch=True)

    assert screens == [
        "home",
        "public_messages",
        "dm_thread",
        "nodes",
        "packets",
        "settings",
        "storage",
        "wifi",
        "map",
    ]
    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["explicit_port_required"] is True
    assert report["manual_touch"] is True
    assert {"screen": "storage", "tab": "settings", "label": "Storage settings"} in report["surface_plan"]
    assert {"screen": "dm_thread", "tab": "messages", "label": "DM thread"} in report["surface_plan"]
    assert "ui tab messages" in report["commands"]
    assert "ui scroll-probe storage" in report["commands"]
    assert "ui scroll-probe dm_thread" in report["commands"]
    assert "crashlog" in report["commands"]


def test_scroll_probe_rejects_unknown_screen():
    try:
        scroll_probe_d1l.parse_screens("messages,unknown")
    except ValueError as exc:
        assert "unknown" in str(exc)
    else:
        raise AssertionError("parse_screens accepted an unknown screen")


def test_scroll_probe_allows_static_home_but_requires_other_surfaces_to_move():
    def event(screen: str) -> dict:
        tab = "home" if screen == "home" else "settings"
        return {
            "screen": screen,
            "tab": tab,
            "request": {"ok": True},
            "tab_active": True,
            "probe": {
                "ok": screen != "home",
                "surface": screen,
                "tab": tab,
                "target_found": True,
                "scrollable": True,
                "moved": False,
            },
            "status": {"active_tab": tab},
            "health": {"ok": True},
            "crashlog": {"entries": []},
        }

    assert scroll_probe_d1l.event_failed(event("home")) is False
    assert scroll_probe_d1l.event_failed(event("storage")) is True


def test_active_release_docs_do_not_treat_short_tab_abuse_as_final_proof():
    release_docs = [
        "README.md",
        "docs/ROADMAP.md",
        "docs/KNOWN_LIMITATIONS.md",
        "docs/RELEASE_CHECKLIST.md",
        "docs/TEST_PLAN_D1L.md",
    ]
    offenders = [
        rel
        for rel in release_docs
        if any(
            phrase in (ROOT / rel).read_text(encoding="utf-8")
            for phrase in ("100-cycle", "500-cycle tab-abuse", "500-cycle tab abuse")
        )
    ]
    assert offenders == []


def test_active_release_docs_mark_keyboard_p0_closed_and_keep_fast_path():
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    roadmap = (ROOT / "docs/ROADMAP.md").read_text(encoding="utf-8")
    checklist = (ROOT / "docs/RELEASE_CHECKLIST.md").read_text(encoding="utf-8")
    limitations = (ROOT / "docs/KNOWN_LIMITATIONS.md").read_text(encoding="utf-8")
    test_plan = (ROOT / "docs/TEST_PLAN_D1L.md").read_text(encoding="utf-8")
    release_docs = "\n".join((readme, roadmap, checklist, limitations, test_plan))

    assert "| On-screen keyboard and sheets |" not in roadmap
    assert "Expanded issue #2 compose/input keyboard capture from PR #35" in checklist
    assert "PR #35" in release_docs
    assert "28727064923" in release_docs
    assert "capture_count=12" in release_docs
    assert "ui_compose_keyboard_capture_d1l.py --port $env:D1L_PORT --targets all" in test_plan
    assert "broader keyboard/sheet physical review" not in release_docs
    assert "expanded `--targets all` keyboard/sheet physical review" not in release_docs
    assert "expanded `--targets all` keyboard/sheet review" not in release_docs
    assert "ui_navigation.c" in roadmap
    assert "ui_modal.c" in roadmap
    assert "ui_chrome.c" in roadmap


def test_smoke_knows_ui_console_commands():
    assert "ui status" in smoke_d1l.SMOKE_COMMANDS
    assert smoke_d1l.expected_command_name("ui tab packets") == "ui tab"
    assert smoke_d1l.expected_command_name("ui scroll-probe storage") == "ui scroll-probe"
    assert smoke_d1l.expected_command_name("ui compose-probe public-long") == "ui compose-probe"
    assert smoke_d1l.expected_command_name("ui data-canary uiRx0001") == "ui data-canary"
    assert smoke_d1l.expected_command_name("ui capture chunk 0 1024") == "ui capture chunk"


def test_ui_capture_dry_run_and_png_writer(tmp_path):
    report = ui_capture_d1l.dry_run_report(
        chunk_size=2048,
        prep_commands=["ui tab home"],
        reference_png="artifacts/ui-sim-reference/home.png",
        reference_view="home",
    )

    assert report["ok"] is True
    assert report["kind"] == "ui_pixel_capture"
    assert report["mode"] == "dry-run"
    assert report["explicit_port_required"] is True
    assert report["width"] == 480
    assert report["height"] == 480
    assert report["chunk_size"] == 1024
    assert report["prep_commands"] == ["ui tab home"]
    assert report["simulator_diff_required"] is True
    assert report["reference_view"] == "home"
    assert "ui tab home" in report["commands"]
    assert "ui capture begin" in report["commands"]
    assert "ui capture chunk 0 1024" in report["commands"]
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False

    raw = bytearray()
    for _ in range(480 * 480):
        raw.extend((0x00, 0xF8))
    png = tmp_path / "capture.png"
    ui_capture_d1l.write_png_from_rgb565_le(bytes(raw), png, 480, 480)

    assert png.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")


def test_ui_capture_diff_accepts_matching_reference_and_rejects_material_difference(tmp_path):
    raw = bytearray()
    for _ in range(480 * 480):
        raw.extend((0x00, 0xF8))
    capture = tmp_path / "capture.png"
    reference = tmp_path / "reference.png"
    ui_capture_d1l.write_png_from_rgb565_le(bytes(raw), capture, 480, 480)
    ui_capture_d1l.write_png_from_rgb565_le(bytes(raw), reference, 480, 480)

    matching = ui_capture_diff_d1l.compare_pngs(capture, reference, reference_view="home")

    assert matching["ok"] is True
    assert matching["kind"] == "ui_capture_simulator_diff"
    assert matching["reference_view"] == "home"
    assert matching["different_pixel_ratio"] == 0
    assert matching["public_rf_tx"] is False
    assert matching["formats_sd"] is False

    changed = bytearray(raw)
    for pixel in range(0, 220 * 480):
        offset = pixel * 2
        changed[offset] = 0x1F
        changed[offset + 1] = 0x00
    bad_reference = tmp_path / "bad-reference.png"
    ui_capture_d1l.write_png_from_rgb565_le(bytes(changed), bad_reference, 480, 480)

    different = ui_capture_diff_d1l.compare_pngs(
        capture,
        bad_reference,
        reference_view="home",
        max_different_ratio=0.10,
        max_tile_mismatch_ratio=0.10,
    )

    assert different["ok"] is False
    assert different["error"] == "material_pixel_difference"
    assert different["tile_mismatch_ratio"] > 0.10


def test_ui_capture_ready_wait_requires_fresh_non_pending_frame():
    baseline = {"ok": True, "shadow_ready": True, "frame_seq": 10, "flush_count": 20}

    assert ui_capture_d1l.capture_status_is_ready(
        {"ok": True, "shadow_ready": True, "frame_seq": 10, "flush_count": 20},
        baseline,
    ) is False
    assert ui_capture_d1l.capture_status_is_ready(
        {
            "ok": True,
            "shadow_ready": True,
            "frame_seq": 11,
            "flush_count": 20,
            "render_pending": True,
        },
        baseline,
    ) is False
    assert ui_capture_d1l.capture_status_is_ready(
        {"ok": True, "shadow_ready": True, "frame_seq": 11, "flush_count": 20},
        baseline,
    ) is True
    assert ui_capture_d1l.capture_status_is_ready(
        {"ok": True, "shadow_ready": True, "frame_seq": 1, "flush_count": 1},
        {"ok": False, "code": "TIMEOUT"},
    ) is True


def test_ui_compose_keyboard_capture_dry_run_is_targeted_and_safe():
    report = ui_compose_keyboard_capture_d1l.dry_run_report(
        targets=ui_compose_keyboard_capture_d1l.parse_targets("all"),
        chunk_size=2048,
    )

    assert report["ok"] is True
    assert report["kind"] == "ui_compose_keyboard_capture"
    assert report["mode"] == "dry-run"
    assert report["explicit_port_required"] is True
    assert report["targets"] == ui_compose_keyboard_capture_d1l.ALL_TARGETS
    assert "ui compose-probe public" in report["commands"]
    assert "ui compose-probe dm-long" in report["commands"]
    assert "ui compose-probe public-search" in report["commands"]
    assert "ui compose-probe packet-search" in report["commands"]
    assert "ui compose-probe contact-edit" in report["commands"]
    assert "ui compose-probe onboarding" in report["commands"]
    assert "ui compose-probe map-location" in report["commands"]
    assert "ui compose-probe map-provider" in report["commands"]
    assert "ui compose-probe wifi-ssid" in report["commands"]
    assert "ui compose-probe wifi-password" in report["commands"]
    assert "ui capture chunk 0 1024" in report["commands"]
    assert report["chunk_size"] == 1024
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False


def test_ui_compose_keyboard_capture_accepts_all_and_underscore_aliases():
    assert ui_compose_keyboard_capture_d1l.parse_targets("all") == ui_compose_keyboard_capture_d1l.ALL_TARGETS
    assert ui_compose_keyboard_capture_d1l.parse_targets("wifi_password,map_provider") == [
        "wifi-password",
        "map-provider",
    ]


def test_ui_compose_keyboard_capture_preserves_pixels_when_probe_geometry_fails(tmp_path, monkeypatch):
    def fake_capture_frame(**kwargs):
        assert kwargs["prep_commands"] == ["ui compose-probe public"]
        assert kwargs["prep_ok_required"] is False
        assert kwargs["post_prep_settle_sec"] == 0.25
        return {
            "schema": 1,
            "kind": "ui_pixel_capture",
            "mode": "hardware",
            "ok": False,
            "pixel_capture_ok": True,
            "prep_ok": False,
            "prep_failures": [
                {
                    "schema": 1,
                    "ok": False,
                    "cmd": "ui compose-probe",
                    "target": "public",
                    "keyboard": {"x": 16, "y": 322, "w": 448, "h": 258},
                }
            ],
            "events": [
                {
                    "schema": 1,
                    "ok": False,
                    "cmd": "ui compose-probe",
                    "target": "public",
                    "keyboard": {"x": 16, "y": 322, "w": 448, "h": 258},
                }
            ],
            "width": 2,
            "height": 1,
            "bytes_per_pixel": 2,
            "pixel_format": "rgb565-le",
            "total_bytes": 4,
            "onboarding_visible": False,
            "raw_bytes": bytes([0x00, 0xF8, 0xE0, 0x07]),
            "public_rf_tx": False,
            "formats_sd": False,
        }

    monkeypatch.setattr(ui_compose_keyboard_capture_d1l, "capture_frame", fake_capture_frame)

    result = ui_compose_keyboard_capture_d1l.capture_target(
        "public",
        "COM12",
        115200,
        0.1,
        1024,
        tmp_path,
        "compose-bad-geometry",
    )

    assert result["ok"] is False
    assert result["capture"]["pixel_capture_ok"] is True
    assert result["capture"]["prep_ok"] is False
    assert result["target_visible"] is False
    assert result["png_path"].endswith("compose-bad-geometry-public.png")
    assert result["raw_path"].endswith("compose-bad-geometry-public.rgb565")
    assert Path(result["png_path"]).read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    assert Path(result["raw_path"]).read_bytes() == bytes([0x00, 0xF8, 0xE0, 0x07])


def test_smoke_command_reader_bounds_readline_timeout_and_restores_serial_timeout():
    ser = FakeSerial(
        [
            b'{"schema":1,"ok":true,"cmd":"noise"}\n',
            b'{"schema":1,"ok":true,"cmd":"ui status","active_tab":"home"}\n',
        ]
    )

    result = smoke_d1l.read_command_result(ser, "ui status", timeout=0.25)

    assert result["ok"] is True
    assert result["cmd"] == "ui status"
    assert ser.timeout == 99.0
    assert ser.seen_timeouts
    assert max(timeout for timeout in ser.seen_timeouts if timeout is not None) <= 0.25


def test_smoke_command_reader_reports_ignored_json_on_timeout():
    ser = FakeSerial([b'{"schema":1,"ok":true,"cmd":"stale"}\n'])

    result = smoke_d1l.read_command_result(ser, "health", timeout=0.01)

    assert result["ok"] is False
    assert result["code"] == "TIMEOUT"
    assert result["ignored_json_count"] == 1
    assert result["ignored_json"] == [{"cmd": "stale", "ok": True}]
    assert ser.timeout == 99.0
