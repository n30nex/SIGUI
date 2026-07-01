import json
from pathlib import Path

from PIL import Image

from tools import ui_simulator


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_ui_simulator_generates_checked_480x480_screens(tmp_path):
    report = ui_simulator.generate(tmp_path)

    assert report["ok"] is True
    assert report["scenario"] == "default"
    assert report["display"] == {"width": 480, "height": 480}
    assert report["snapshot_counts"]["heard"] == 3
    assert report["overflow_count"] == 0
    assert report["touch_target_issue_count"] == 0
    assert report["required_labels_missing"] == []
    assert report["flow_report"]["ok"] is True
    assert report["flow_report"]["target_overlaps"] == []
    assert report["flow_report"]["format_actions"] == []

    report_path = tmp_path / "ui-sim-report.json"
    saved_report = json.loads(report_path.read_text(encoding="utf-8"))
    assert saved_report["ok"] is True
    assert saved_report["schema"] == 2

    views = {view["name"]: view for view in report["views"]}
    assert set(views) == set(ui_simulator.RENDERERS)
    for name, view in views.items():
        image_path = Path(view["screenshot"])
        assert image_path.exists(), name
        with Image.open(image_path) as image:
            assert image.size == (480, 480), name
        assert view["text_count"] > 0
        assert view["overflow"] == []
        assert view["touch_target_issues"] == []


def test_ui_simulator_large_mesh_stress_is_bounded(tmp_path):
    report = ui_simulator.generate(tmp_path, scenario="large-mesh")

    assert report["ok"] is True
    assert report["scenario"] == "large-mesh"
    assert report["overflow_count"] == 0
    assert report["touch_target_issue_count"] == 0
    assert report["flow_report"]["ok"] is True
    assert report["required_labels_missing"] == []
    assert report["snapshot_counts"]["heard"] == 96
    assert report["snapshot_counts"]["public_messages"] == 48
    assert report["snapshot_counts"]["dm_messages"] == 32

    views = {view["name"]: view for view in report["views"]}
    messages = views["messages"]["metrics"]
    assert messages["public_source_count"] == 48
    assert messages["public_rendered_count"] <= 4
    assert messages["dm_source_count"] == 32
    assert messages["dm_rendered_count"] <= 3

    nodes = views["nodes"]["metrics"]
    assert nodes["contacts_source_count"] == 18
    assert nodes["contacts_rendered_count"] <= 2
    assert nodes["heard_source_count"] == 96
    assert nodes["heard_rendered_count"] <= 4

    dm_thread = views["dm_thread_sheet"]["metrics"]
    assert dm_thread["dm_thread_source_count"] == 32
    assert dm_thread["dm_thread_rendered_count"] <= 3
    trace = views["route_trace_sheet"]["metrics"]
    assert trace["route_trace_rendered_count"] <= 2

    public_history = views["public_history_sheet"]["metrics"]
    assert public_history["public_history_source_count"] == 48
    assert public_history["public_history_rendered_count"] <= 3


def test_ui_simulator_covers_current_touch_surfaces(tmp_path):
    report = ui_simulator.generate(tmp_path)
    labels_by_view = {view["name"]: set(view["labels"]) for view in report["views"]}

    assert {"Time", "Wi-Fi", "BLE", "SD", "Public", "DMs", "Last Messages", "Local Repeaters"} <= labels_by_view["home"]
    assert {"Messages", "Read", "Compose", "History", "Test", "Public", "Direct"} <= labels_by_view["messages"]
    assert {"Nodes", "Contacts", "Heard Nodes", "DM"} <= labels_by_view["nodes"]
    assert {"Map", "Tile Cache", "Downloads", "Offline Cache", "Center", "Unset", "Routes"} <= labels_by_view["map"]
    assert "No network tile download until Wi-Fi runtime" in labels_by_view["map"]
    assert {"Packets", "Signal", "Mesh Roles", "All", "RX", "TX", "Text", "Search", "Routes", "Packet Feed"} <= labels_by_view["packets"]
    assert {"Settings", "Storage", "NVS fallback"} <= labels_by_view["settings"]
    assert {"Radio Settings", "Freq 910.525 MHz", "-25k", "+25k", "Cycle BW", "Save"} <= labels_by_view["radio_settings_sheet"]
    assert {
        "Storage Setup",
        "SD Card",
        "Backends",
        "format not_available",
        "No automatic format. Confirmation required before SD setup.",
    } <= labels_by_view["storage_setup_sheet"]
    assert {"Room Servers", "Repeater Candidates", "Close"} <= labels_by_view["mesh_roles_sheet"]
    assert {"Advert", "Zero-hop advert", "Zero Hop", "Flood advert", "Flood", "Close"} <= labels_by_view["advert_sheet"]
    assert {"Packet Search", "Search kind, note, raw hex", "Apply", "Clear", "Close"} <= labels_by_view["packet_search_sheet"]
    assert {"Public History", "Public scrollback", "Search", "Clear", "Close"} <= labels_by_view["public_history_sheet"]
    assert {"Public Search", "Search author or message", "Apply", "Clear", "Close"} <= labels_by_view["public_search_sheet"]
    assert {"Contact Detail", "Trace", "Edit", "Export", "Fav", "Mute"} <= labels_by_view["contact_detail_sheet"]
    assert {"Edit Contact", "Contact alias", "Save", "Forget", "Close"} <= labels_by_view["contact_edit_sheet"]
    assert {"Contact Export", "MeshCore QR", "Fingerprint", "URI", "Close"} <= labels_by_view["contact_export_sheet"]
    assert {"DM Thread", "Thread 2 rows", "Reply", "Read"} <= labels_by_view["dm_thread_sheet"]
    assert {"Route Trace", "Trace", "Contact Path", "Best Evidence", "Close"} <= labels_by_view["route_trace_sheet"]
    assert {"Route Detail", "Packet Detail", "Raw Hex"} <= (labels_by_view["route_detail_sheet"] | labels_by_view["packet_detail_sheet"])
    assert {"First boot setup", "Node name", "Start", "Use Defaults"} <= labels_by_view["onboarding_sheet"]


def test_ui_simulator_reports_touch_targets_and_flows(tmp_path):
    report = ui_simulator.generate(tmp_path)
    views = {view["name"]: view for view in report["views"]}
    actions_by_view = {
        name: {target["action"]: target for target in view["touch_targets"]}
        for name, view in views.items()
    }

    assert ui_simulator.EXPECTED_FLOWS
    assert report["flow_report"]["skipped_flows"] == []
    assert report["flow_report"]["missing_steps"] == []
    assert report["flow_report"]["target_issues"] == []
    assert report["flow_report"]["target_overlaps"] == []
    assert report["flow_report"]["format_actions"] == []

    flow_names = {flow["name"] for flow in report["flow_report"]["flows"]}
    assert {
        "first_boot_onboarding",
        "lock_overlay_unlock",
        "public_compose_and_send",
        "public_history_search",
        "dm_thread_read_and_reply",
        "contact_detail_management",
        "contact_edit_alias_and_forget",
        "map_page_policy",
        "packet_filters_search_and_details",
        "mesh_roles_browser",
        "settings_radio_storage_and_advert",
    } <= flow_names

    assert actions_by_view["messages"]["open_public_compose"]["destination"] == "compose_sheet"
    assert actions_by_view["messages"]["open_public_history"]["destination"] == "public_history_sheet"
    assert actions_by_view["home"]["open_map"]["destination"] == "map"
    assert actions_by_view["messages"]["send_public_test"]["public_rf_tx"] is True
    assert actions_by_view["compose_sheet"]["send_public_text"]["public_rf_tx"] is True
    assert actions_by_view["settings"]["open_advert_sheet"]["destination"] == "advert_sheet"
    assert actions_by_view["advert_sheet"]["send_advert_zero"]["rf_tx"] is True
    assert actions_by_view["advert_sheet"]["send_advert_flood"]["rf_tx"] is True
    assert actions_by_view["contact_edit_sheet"]["forget_contact"]["destructive"] is True
    assert actions_by_view["storage_setup_sheet"]["close_storage_setup"]["formats_sd"] is False
    assert actions_by_view["lock_overlay"]["unlock"]["destination"] == "home"

    for view in report["views"]:
        for target in view["touch_targets"]:
            assert target["width"] >= ui_simulator.MIN_TOUCH_TARGET, (view["name"], target["label"])
            assert target["height"] >= ui_simulator.MIN_TOUCH_TARGET, (view["name"], target["label"])
            assert target["offscreen"] is False, (view["name"], target["label"])
            assert target["top_bar_overlap"] is False, (view["name"], target["label"])
            assert target["dock_overlap"] is False, (view["name"], target["label"])


def test_ui_simulator_storage_state_scenarios_fit(tmp_path):
    scenarios = {
        "storage-no-card": ("insert_card", "not_available"),
        "storage-format-required": ("format_confirmation_required", "confirm_required"),
        "storage-root-missing": ("manual_format_required", "not_available"),
        "storage-ready-pending-migration": ("store_migration_pending", "not_needed"),
        "storage-ready-packet-log-sd": ("packet_log_canary_enabled", "not_needed"),
        "storage-ready-retained-history-sd": ("retained_history_sd_enabled", "not_needed"),
        "storage-ready-map-tiles-sd": ("retained_history_sd_enabled", "not_needed"),
    }

    for scenario, (setup_action, format_action) in scenarios.items():
        report = ui_simulator.generate(tmp_path / scenario, views=("settings", "storage_setup_sheet"), scenario=scenario)
        labels_by_view = {view["name"]: set(view["labels"]) for view in report["views"]}
        storage_view = next(view for view in report["views"] if view["name"] == "storage_setup_sheet")

        assert report["ok"] is True, scenario
        assert report["overflow_count"] == 0, scenario
        assert report["touch_target_issue_count"] == 0, scenario
        assert report["flow_report"]["format_actions"] == [], scenario
        assert report["required_labels_missing"] == [], scenario
        assert {"Settings", "Radio", "Advert", "Storage"} <= labels_by_view["settings"]
        assert {
            "Storage Setup",
            "Backends",
            f"setup {setup_action}",
            f"format {format_action}",
            "No automatic format. Confirmation required before SD setup.",
        } <= labels_by_view["storage_setup_sheet"]
        if scenario == "storage-ready-packet-log-sd":
            assert "Mixed storage" in labels_by_view["settings"]
            assert "messages NVS / packets SD / routes NVS" in labels_by_view["storage_setup_sheet"]
        elif scenario == "storage-ready-retained-history-sd":
            assert "Mixed storage" in labels_by_view["settings"]
            assert "messages SD / packets SD / routes SD" in labels_by_view["storage_setup_sheet"]
        elif scenario == "storage-ready-map-tiles-sd":
            assert "Mixed storage" in labels_by_view["settings"]
            assert "msg/pkt/route/map SD" in labels_by_view["storage_setup_sheet"]
            map_report = ui_simulator.generate(tmp_path / f"{scenario}-map", views=("map",), scenario=scenario)
            map_view = map_report["views"][0]
            assert map_report["ok"] is True, scenario
            assert map_view["metrics"]["map_tile_cache_ready"] is True
            assert map_view["metrics"]["map_tile_download_supported"] is False
            assert "sd_map_tiles_ready" in set(map_view["labels"])
        else:
            assert "messages NVS / packets NVS / routes NVS" in labels_by_view["storage_setup_sheet"]
        assert storage_view["overflow"] == []


def test_ui_simulator_manual_location_scenario_fits(tmp_path):
    report = ui_simulator.generate(tmp_path / "manual-location", views=("map",), scenario="manual-location")
    view = report["views"][0]
    labels = set(view["labels"])

    assert report["ok"] is True
    assert report["overflow_count"] == 0
    assert report["touch_target_issue_count"] == 0
    assert report["required_labels_missing"] == []
    assert {"Map", "Center", "Manual", "43.6532000, -79.3832000"} <= labels
    assert view["metrics"]["map_location_set"] is True
    assert view["metrics"]["map_center_source"] == "manual"
    assert view["metrics"]["map_center_lat_e7"] == 436532000
    assert view["metrics"]["map_center_lon_e7"] == -793832000


def test_ui_simulator_is_documented_and_run_in_ci():
    workflow = read(".github/workflows/d1l-ci.yml")
    test_plan = read("docs/TEST_PLAN_D1L.md")
    roadmap = read("docs/ROADMAP.md")
    checklist = read("docs/RELEASE_CHECKLIST.md")

    assert "Pillow" in workflow
    assert "python ./tools/ui_simulator.py --out artifacts/ui-sim" in workflow
    assert "python ./tools/ui_simulator.py --scenario large-mesh --out artifacts/ui-sim-large" in workflow
    assert "python ./tools/ui_simulator.py --scenario storage-states --out artifacts/ui-sim-storage" in workflow
    assert "python .\\tools\\ui_simulator.py --out artifacts\\ui-sim" in test_plan
    assert "python .\\tools\\ui_simulator.py --scenario large-mesh --out artifacts\\ui-sim-large" in test_plan
    assert "tools/ui_simulator.py" in roadmap
    assert "large-mesh" in roadmap
    assert "Simulator screenshots captured" in checklist
    assert "Large simulated mesh UI stress passes" in checklist
