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
    assert report["dock_invariant_issues"] == []
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
    assert len(views) == 38
    for name, view in views.items():
        image_path = Path(view["screenshot"])
        assert image_path.exists(), name
        with Image.open(image_path) as image:
            assert image.size == (480, 480), name
        assert view["text_count"] > 0
        assert view["overflow"] == []
        assert view["touch_target_issues"] == []
        assert view["dock_expected"] is (name in ui_simulator.DOCKED_VIEWS)
        assert view["dock_rendered"] is (name in ui_simulator.DOCKED_VIEWS)
        assert view["dock_target_count"] == (5 if name in ui_simulator.DOCKED_VIEWS else 0)
        assert view["dock_invariant_ok"] is True


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
    assert messages["public_rendered_count"] <= 5
    assert messages["dm_source_count"] == 32
    assert messages["dm_rendered_count"] == 0
    messages_dm = views["messages_dm"]["metrics"]
    assert messages_dm["messages_mode"] == "dms"
    assert messages_dm["dm_source_count"] == 32
    assert messages_dm["dm_rendered_count"] <= 5

    nodes = views["nodes"]["metrics"]
    assert nodes["contacts_source_count"] == 18
    assert nodes["contacts_rendered_count"] <= 2
    assert nodes["heard_source_count"] == 96
    assert nodes["heard_query_count"] == 96
    assert nodes["heard_rendered_count"] <= 4

    packets = views["packets"]["metrics"]
    assert packets["packet_source_count"] == 128
    assert packets["packet_query_limit"] == 100
    assert packets["packet_total_matches"] == 128
    assert packets["packet_sd_history_page"] is True
    assert packets["packet_load_older_available"] is True
    assert "Load Older" in set(views["packets"]["labels"])

    dm_thread = views["dm_thread_sheet"]["metrics"]
    assert dm_thread["dm_thread_source_count"] == 32
    assert dm_thread["dm_thread_rendered_count"] <= 3
    assert dm_thread["dm_thread_page_limit"] == 5
    assert dm_thread["dm_thread_load_older_available"] is True
    assert "Load Older" in set(views["dm_thread_sheet"]["labels"])
    trace = views["route_trace_sheet"]["metrics"]
    assert trace["route_trace_rendered_count"] <= 2

    public_history = views["public_history_sheet"]["metrics"]
    assert public_history["public_history_source_count"] == 48
    assert public_history["public_history_rendered_count"] <= 3
    assert public_history["public_history_page_limit"] == 5
    assert public_history["public_history_load_older_available"] is True
    assert "Load Older" in set(views["public_history_sheet"]["labels"])


def test_ui_simulator_covers_current_touch_surfaces(tmp_path):
    report = ui_simulator.generate(tmp_path)
    views_by_name = {view["name"]: view for view in report["views"]}
    labels_by_view = {view["name"]: set(view["labels"]) for view in report["views"]}
    expected_docked_views = frozenset(
        {
            "messages",
            "messages_dm",
            "nodes",
            "map",
            "packets",
            "settings",
            "settings_tools_expanded",
            "settings_connections_expanded",
            "settings_storage_maps_expanded",
            "settings_device_expanded",
            "settings_support_expanded",
            "settings_advanced_expanded",
        }
    )
    assert ui_simulator.DOCKED_VIEWS == expected_docked_views

    assert {
        "Messages",
        "Network",
        "Map",
        "More",
        "Device status",
        "Settings and support",
        "Time",
        "Wi-Fi",
        "BLE",
        "SD",
    } <= labels_by_view["home"]
    assert "DeskOS" in labels_by_view["home"]
    assert "Mesh ready, listening" not in labels_by_view["home"]
    assert "D1L Desk" not in labels_by_view["home"]
    assert not any(label.startswith("--:--  Mesh") for label in labels_by_view["home"])
    assert ui_simulator.HOME_TOP_BAR_H == 16
    assert "RX" not in labels_by_view["home"]
    assert "TX" not in labels_by_view["home"]
    assert not any(target["kind"] == "dock_tab" for target in views_by_name["home"]["touch_targets"])
    for view_name in (
        "messages",
        "messages_dm",
        "nodes",
        "map",
        "packets",
        "settings",
        "settings_tools_expanded",
        "settings_connections_expanded",
        "settings_storage_maps_expanded",
        "settings_device_expanded",
        "settings_support_expanded",
        "settings_advanced_expanded",
    ):
        assert any(target["kind"] == "dock_tab" for target in views_by_name[view_name]["touch_targets"])
    assert {"Messages", "Read", "Compose", "History", "Test", "Public", "DMs", "Public Channel"} <= labels_by_view["messages"]
    assert {"Messages", "Public", "DMs", "DM Conversations"} <= labels_by_view["messages_dm"]
    assert {"Nodes", "Contacts", "Heard Nodes", "All Heard", "DM", "CMP", "ROOM", "RPT"} <= labels_by_view["nodes"]
    assert {"Map", "Tiles", "Set Pin", "Tile Cache", "Downloads", "No Offline Tiles", "Center", "Unset", "Routes"} <= labels_by_view["map"]
    assert "Connect Wi-Fi and download allowed tiles for your area." in labels_by_view["map"]
    assert "Allowed provider and visible attribution required." in labels_by_view["map"]
    assert {
        "Map Tiles",
        "Allowed provider template",
        "Attribution",
        "Zoom 12",
        "Save",
        "Clear",
        "Live download unavailable",
        "Tile render pending; use serial canaries for SD cache proof.",
        "No public OSM bulk tile servers. Visible attribution is required.",
        "Keyboard",
        "Close",
    } <= labels_by_view["map_tiles_sheet"]
    assert {
        "Set D1L Location",
        "Map needs your D1L location",
        "Enter decimal degrees",
        "Latitude",
        "Longitude",
        "Keyboard",
        "Save",
        "Clear",
        "Skip",
    } <= labels_by_view["map_location_sheet"]
    assert {"Packets", "live tail  rssi -41  snr 30  avg -46", "Mesh Roles", "All", "RX", "TX", "Text", "Search", "Pause", "Routes", "Packet Feed"} <= labels_by_view["packets"]
    assert {
        "More",
        "Settings and tools",
        "Tools",
        "Packets and diagnostics",
        "Connections",
        "Storage & maps",
        "Device",
        "Support",
        "Advanced",
    } <= labels_by_view["settings"]
    assert {"More", "Tools", "Packets and diagnostics", "Packets", "Diagnostics"} <= labels_by_view["settings_tools_expanded"]
    assert {"More", "Connections", "Wi-Fi", "Bluetooth", "Radio"} <= labels_by_view["settings_connections_expanded"]
    assert {"More", "Storage & maps", "SD Card", "Offline Maps"} <= labels_by_view["settings_storage_maps_expanded"]
    assert {"More", "Device", "Display", "Identity"} <= labels_by_view["settings_device_expanded"]
    assert {"More", "Support", "About", "Version 1.0.0-rc1"} <= labels_by_view["settings_support_expanded"]
    assert {"More", "Advanced", "Mesh advertise", "Broadcast presence"} <= labels_by_view["settings_advanced_expanded"]
    assert {
        "Wi-Fi Setup",
        "Profile and state",
        "Network name",
        "Password",
        "SSID",
        "Optional",
        "Scan",
        "Connect",
        "Scan to list nearby 2.4 GHz networks",
        "Keyboard",
        "Save",
        "Clear",
        "Enable",
    } <= labels_by_view["wifi_setup_sheet"]
    assert {
        "BLE Setup",
        "Companion state",
        "BLE companion transport is unavailable in this release.",
        "No BLE pairing or transport artifact is present for public release.",
        "Enable unavailable",
        "Pair unavailable",
        "Forget unavailable",
    } <= labels_by_view["ble_setup_sheet"]
    assert {"Display", "Screen controls", "Brightness", "Night", "Contrast", "Timeout"} <= labels_by_view["display_settings_sheet"]
    assert {"Diagnostics", "Advanced health", "Health", "Crashlog", "Export", "Soak"} <= labels_by_view["diagnostics_sheet"]
    assert {"Compose Public", "Public message", "20/138", "Keyboard", "Send", "Clear", "Close"} <= labels_by_view["compose_sheet"]
    assert {"Radio Settings", "Freq 910.525 MHz", "-25k", "+25k", "Cycle BW", "Save"} <= labels_by_view["radio_settings_sheet"]
    assert {
        "SD Card",
        "FAT32 only, no format",
        "SD Card",
        "Backends",
        "No device format; onboard NVS remains fallback.",
    } <= labels_by_view["storage_setup_sheet"]
    assert {"Room Servers", "Repeater Candidates", "Close"} <= labels_by_view["mesh_roles_sheet"]
    assert {"Advert", "Zero-hop advert", "Zero Hop", "Flood advert", "Flood", "Close"} <= labels_by_view["advert_sheet"]
    assert {"Packet Search", "Search kind, note, raw hex", "Apply", "Clear", "Close"} <= labels_by_view["packet_search_sheet"]
    assert {"Public History", "Public scrollback", "Search", "Clear", "Close"} <= labels_by_view["public_history_sheet"]
    assert {"Public Search", "Search author or message", "Apply", "Clear", "Close"} <= labels_by_view["public_search_sheet"]
    assert {"Message Detail", "Sender", "Message", "Signal", "Path", "Advanced", "Reply", "Close"} <= labels_by_view["message_detail_sheet"]
    assert {"Contact Detail", "Trace", "Edit", "Export", "Fav", "Mute"} <= labels_by_view["contact_detail_sheet"]
    assert {"Node Detail", "Role", "Fingerprint", "Public key", "Signal", "Path", "Last heard", "Close"} <= labels_by_view["node_detail_sheet"]
    assert {"Edit Contact", "Contact alias", "Save", "Forget", "Close"} <= labels_by_view["contact_edit_sheet"]
    assert {"Contact Export", "MeshCore QR", "Fingerprint", "URI", "Close"} <= labels_by_view["contact_export_sheet"]
    assert {"DM Thread", "Thread 2/2 rows", "Reply", "Read"} <= labels_by_view["dm_thread_sheet"]
    assert {"Route Trace", "Trace", "Contact Path", "Best Evidence", "Close"} <= labels_by_view["route_trace_sheet"]
    assert {"Route Detail", "Packet Detail", "Advanced", "Raw Hex"} <= (labels_by_view["route_detail_sheet"] | labels_by_view["packet_detail_sheet"])
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
        "home_launcher_navigation",
        "public_compose_and_send",
        "public_history_search",
        "public_message_detail",
        "public_message_reply",
        "dm_thread_read_and_reply",
        "node_detail_inspection",
        "contact_detail_management",
        "contact_edit_alias_and_forget",
        "map_page_policy",
        "packet_filters_search_and_details",
        "mesh_roles_browser",
        "more_category_navigation",
        "more_tools_expanded_navigation",
        "more_connections_expanded_navigation",
        "more_storage_maps_expanded_navigation",
        "more_device_expanded_navigation",
        "more_support_expanded_navigation",
        "more_advanced_expanded_navigation",
        "settings_radio_storage_and_advert",
        "settings_display_and_diagnostics",
    } <= flow_names

    assert actions_by_view["messages"]["open_public_compose"]["destination"] == "compose_sheet"
    assert actions_by_view["messages"]["open_messages_public"]["destination"] == "messages"
    assert actions_by_view["messages"]["open_messages_dm"]["destination"] == "messages_dm"
    assert actions_by_view["messages"]["open_public_history"]["destination"] == "public_history_sheet"
    assert actions_by_view["messages"]["open_message_detail"]["destination"] == "message_detail_sheet"
    assert actions_by_view["messages_dm"]["open_dm_thread"]["destination"] == "dm_thread_sheet"
    assert actions_by_view["message_detail_sheet"]["close_message_detail"]["destination"] == "messages"
    assert actions_by_view["message_detail_sheet"]["toggle_message_detail_advanced"]["height"] >= ui_simulator.MIN_TOUCH_TARGET
    assert actions_by_view["message_detail_sheet"]["open_public_reply"]["destination"] == "compose_sheet"
    assert not any(target["kind"] == "dock_tab" for target in views["compose_sheet"]["touch_targets"])
    assert not any(target["kind"] == "dock_tab" for target in views["home"]["touch_targets"])
    assert actions_by_view["home"]["open_messages_public"]["visual_box"] == [12, 16, 234, 156]
    assert actions_by_view["home"]["open_nodes"]["visual_box"] == [246, 16, 468, 156]
    assert actions_by_view["home"]["open_map"]["visual_box"] == [12, 164, 234, 304]
    assert actions_by_view["home"]["open_settings"]["visual_box"] == [246, 164, 468, 304]
    assert actions_by_view["home"]["open_device_status"]["visual_box"] == [12, 312, 468, 428]
    assert actions_by_view["home"]["open_device_status"]["kind"] == "device_status_card"
    for docked_view in (
        "messages",
        "nodes",
        "map",
        "packets",
        "settings",
        "settings_tools_expanded",
        "settings_connections_expanded",
        "settings_storage_maps_expanded",
        "settings_device_expanded",
        "settings_support_expanded",
        "settings_advanced_expanded",
    ):
        dock_targets = [target for target in views[docked_view]["touch_targets"] if target["kind"] == "dock_tab"]
        assert [target["label"] for target in dock_targets] == [
            "Home tab",
            "Msg tab",
            "Network tab",
            "Map tab",
            "More tab",
        ], docked_view
        assert all(target["width"] == 96 for target in dock_targets), docked_view
    assert actions_by_view["home"]["open_messages_public"]["destination"] == "messages"
    assert actions_by_view["home"]["open_nodes"]["destination"] == "nodes"
    assert actions_by_view["home"]["open_map"]["destination"] == "map"
    assert actions_by_view["home"]["open_settings"]["destination"] == "settings"
    assert actions_by_view["home"]["open_device_status"]["destination"] == "settings"
    assert actions_by_view["wifi_setup_sheet"]["close_wifi_setup"]["destination"] == "settings"
    assert actions_by_view["wifi_setup_sheet"]["wifi_scan"]["destination"] is None
    assert actions_by_view["wifi_setup_sheet"]["wifi_connect"]["destination"] is None
    assert actions_by_view["wifi_setup_sheet"]["edit_wifi_ssid"]["kind"] == "text_field"
    assert actions_by_view["wifi_setup_sheet"]["edit_wifi_password"]["kind"] == "text_field"
    assert not any(target["kind"] == "dock_tab" for target in views["wifi_setup_sheet"]["touch_targets"])
    assert actions_by_view["ble_setup_sheet"]["close_ble_setup"]["destination"] == "settings"
    assert actions_by_view["map"]["open_map_tiles"]["destination"] == "map_tiles_sheet"
    assert actions_by_view["map_tiles_sheet"]["edit_map_tile_provider"]["kind"] == "text_field"
    assert actions_by_view["map_tiles_sheet"]["edit_map_tile_attribution"]["kind"] == "text_field"
    assert actions_by_view["map_tiles_sheet"]["save_map_tile_provider"]["height"] >= ui_simulator.MIN_TOUCH_TARGET
    assert "download_center_tile" not in actions_by_view["map_tiles_sheet"]
    assert actions_by_view["map_tiles_sheet"]["close_map_tiles"]["destination"] == "map"
    assert not any(target["kind"] == "dock_tab" for target in views["map_tiles_sheet"]["touch_targets"])
    assert actions_by_view["map"]["open_map_location_picker"]["destination"] == "map_location_sheet"
    assert actions_by_view["map_location_sheet"]["save_map_location"]["destination"] == "map"
    assert actions_by_view["map_location_sheet"]["edit_map_latitude"]["kind"] == "text_field"
    assert actions_by_view["map_location_sheet"]["edit_map_longitude"]["kind"] == "text_field"
    assert actions_by_view["map_location_sheet"]["skip_map_location"]["destination"] == "map"
    assert actions_by_view["map_location_sheet"]["clear_d1l_pin"]["destination"] == "map"
    assert actions_by_view["nodes"]["open_node_detail"]["destination"] == "node_detail_sheet"
    assert actions_by_view["node_detail_sheet"]["close_node_detail"]["destination"] == "nodes"
    assert actions_by_view["messages"]["send_public_test"]["public_rf_tx"] is True
    assert actions_by_view["compose_sheet"]["send_public_text"]["public_rf_tx"] is True
    assert {
        "toggle_more_tools",
        "toggle_more_connections",
        "toggle_more_storage_maps",
        "toggle_more_device",
        "toggle_more_support",
        "toggle_more_advanced",
    } == set(actions_by_view["settings"]) - {"open_home", "open_messages", "open_nodes", "open_map", "open_settings"}
    assert all(
        actions_by_view["settings"][action]["kind"] == "menu_category"
        for action in (
            "toggle_more_tools",
            "toggle_more_connections",
            "toggle_more_storage_maps",
            "toggle_more_device",
            "toggle_more_support",
            "toggle_more_advanced",
        )
    )
    assert actions_by_view["settings"]["toggle_more_tools"]["visual_box"] == [18, 110, 462, 158]
    assert actions_by_view["settings"]["toggle_more_connections"]["visual_box"] == [18, 162, 462, 210]
    assert actions_by_view["settings"]["toggle_more_storage_maps"]["visual_box"] == [18, 214, 462, 262]
    assert actions_by_view["settings"]["toggle_more_device"]["visual_box"] == [18, 266, 462, 314]
    assert actions_by_view["settings"]["toggle_more_support"]["visual_box"] == [18, 318, 462, 366]
    assert actions_by_view["settings"]["toggle_more_advanced"]["visual_box"] == [18, 370, 462, 418]
    assert actions_by_view["settings_tools_expanded"]["open_packets"]["destination"] == "packets"
    assert actions_by_view["settings_tools_expanded"]["open_diagnostics"]["destination"] == "diagnostics_sheet"
    assert actions_by_view["settings_connections_expanded"]["open_wifi_settings"]["destination"] == "wifi_setup_sheet"
    assert actions_by_view["settings_connections_expanded"]["open_ble_settings"]["destination"] == "ble_setup_sheet"
    assert actions_by_view["settings_connections_expanded"]["open_radio_settings"]["destination"] == "radio_settings_sheet"
    assert actions_by_view["settings_storage_maps_expanded"]["open_storage_setup"]["destination"] == "storage_setup_sheet"
    assert actions_by_view["settings_storage_maps_expanded"]["open_map_tiles"]["destination"] == "map_tiles_sheet"
    assert actions_by_view["settings_device_expanded"]["open_display_settings"]["destination"] == "display_settings_sheet"
    assert actions_by_view["settings_advanced_expanded"]["open_advert_sheet"]["destination"] == "advert_sheet"
    assert not any(target["label"] == "Identity" for target in views["settings_device_expanded"]["touch_targets"])
    assert not any(target["label"] == "About" for target in views["settings_support_expanded"]["touch_targets"])
    for key, expected_leaves, expected_actions in (
        ("tools", 2, 2),
        ("connections", 3, 3),
        ("storage_maps", 2, 2),
        ("device", 2, 1),
        ("support", 1, 0),
        ("advanced", 1, 1),
    ):
        expanded_view = views[f"settings_{key}_expanded"]
        assert expanded_view["metrics"]["more_expanded_category"] == key
        assert expanded_view["metrics"]["more_leaf_count"] == expected_leaves
        assert expanded_view["metrics"]["more_actionable_leaf_count"] == expected_actions
        assert expanded_view["metrics"]["more_scroll_anchor_y"] == 110
    assert actions_by_view["display_settings_sheet"]["close_display_settings"]["destination"] == "settings"
    assert actions_by_view["diagnostics_sheet"]["close_diagnostics"]["destination"] == "settings"
    assert actions_by_view["packets"]["pause_packet_feed"]["height"] >= ui_simulator.MIN_TOUCH_TARGET
    assert actions_by_view["packet_detail_sheet"]["toggle_packet_detail_advanced"]["height"] >= ui_simulator.MIN_TOUCH_TARGET
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
            assert target["unexpected_dock"] is False, (view["name"], target["label"])


def test_ui_simulator_storage_state_scenarios_fit(tmp_path):
    scenarios = {
        "storage-no-card": (
            "insert_card",
            "FAT32 only, no format",
            "No device format; onboard NVS remains fallback.",
        ),
        "storage-needs-fat32": (
            "prepare_fat32_on_computer",
            "FAT32 card required",
            "Prepare FAT32 on a computer; DeskOS only creates folders.",
        ),
        "storage-mount-error": (
            "inspect_rp2040_sd_mount_error_firmware_path",
            "Firmware mount issue",
            "Inspect RP2040 mount diagnostics; do not format on-device.",
        ),
        "storage-probe-error": (
            "inspect_rp2040_sd_cmd0_firmware_path",
            "Firmware probe issue",
            "Inspect RP2040 CMD0/CMD8 diagnostics; do not format on-device.",
        ),
        "storage-root-missing": (
            "retry_storage_mount",
            "FAT32 only, no format",
            "No device format; onboard NVS remains fallback.",
        ),
        "storage-ready-pending-migration": (
            "store_migration_pending",
            "FAT32 only, no format",
            "No device format; onboard NVS remains fallback.",
        ),
        "storage-ready-packet-log-sd": (
            "packet_log_canary_enabled",
            "FAT32 only, no format",
            "No device format; onboard NVS remains fallback.",
        ),
        "storage-ready-retained-history-sd": (
            "retained_history_sd_enabled",
            "FAT32 only, no format",
            "No device format; onboard NVS remains fallback.",
        ),
        "storage-ready-map-tiles-sd": (
            "retained_history_sd_enabled",
            "FAT32 only, no format",
            "No device format; onboard NVS remains fallback.",
        ),
    }

    for scenario, (setup_action, subtitle, guidance) in scenarios.items():
        report = ui_simulator.generate(tmp_path / scenario, views=("settings", "storage_setup_sheet"), scenario=scenario)
        labels_by_view = {view["name"]: set(view["labels"]) for view in report["views"]}
        storage_view = next(view for view in report["views"] if view["name"] == "storage_setup_sheet")

        assert report["ok"] is True, scenario
        assert report["overflow_count"] == 0, scenario
        assert report["touch_target_issue_count"] == 0, scenario
        assert report["flow_report"]["format_actions"] == [], scenario
        assert report["required_labels_missing"] == [], scenario
        assert {
            "More",
            "Settings and tools",
            "Tools",
            "Packets and diagnostics",
            "Connections",
            "Storage & maps",
            "Device",
            "Support",
            "Advanced",
        } <= labels_by_view["settings"]
        assert {
            "SD Card",
            subtitle,
            "Backends",
            f"setup {setup_action}",
            guidance,
        } <= labels_by_view["storage_setup_sheet"]
        if scenario == "storage-ready-packet-log-sd":
            assert "messages NVS / packets SD / routes NVS" in labels_by_view["storage_setup_sheet"]
        elif scenario == "storage-ready-retained-history-sd":
            assert "messages SD / packets SD / routes SD" in labels_by_view["storage_setup_sheet"]
        elif scenario == "storage-ready-map-tiles-sd":
            assert "msg/pkt/route/map SD" in labels_by_view["storage_setup_sheet"]
            map_report = ui_simulator.generate(tmp_path / f"{scenario}-map", views=("map",), scenario=scenario)
            map_view = map_report["views"][0]
            assert map_report["ok"] is True, scenario
            assert map_view["metrics"]["map_tile_cache_ready"] is True
            assert map_view["metrics"]["map_tile_download_supported"] is False
            assert map_view["metrics"]["map_tile_render_supported"] is False
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
