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
    assert report["required_labels_missing"] == []

    report_path = tmp_path / "ui-sim-report.json"
    assert json.loads(report_path.read_text(encoding="utf-8"))["ok"] is True

    views = {view["name"]: view for view in report["views"]}
    assert set(views) == set(ui_simulator.RENDERERS)
    for name, view in views.items():
        image_path = Path(view["screenshot"])
        assert image_path.exists(), name
        with Image.open(image_path) as image:
            assert image.size == (480, 480), name
        assert view["text_count"] > 0
        assert view["overflow"] == []


def test_ui_simulator_large_mesh_stress_is_bounded(tmp_path):
    report = ui_simulator.generate(tmp_path, scenario="large-mesh")

    assert report["ok"] is True
    assert report["scenario"] == "large-mesh"
    assert report["overflow_count"] == 0
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

    assert {"Messages", "Read", "Compose", "History", "Test", "Public", "Direct"} <= labels_by_view["messages"]
    assert {"Nodes", "Contacts", "Heard Nodes", "DM"} <= labels_by_view["nodes"]
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


def test_ui_simulator_storage_state_scenarios_fit(tmp_path):
    scenarios = {
        "storage-no-card": ("insert_card", "not_available"),
        "storage-format-required": ("format_confirmation_required", "confirm_required"),
        "storage-root-missing": ("manual_format_required", "not_available"),
        "storage-ready-pending-migration": ("store_migration_pending", "not_needed"),
        "storage-ready-packet-log-sd": ("packet_log_canary_enabled", "not_needed"),
        "storage-ready-retained-history-sd": ("retained_history_sd_enabled", "not_needed"),
    }

    for scenario, (setup_action, format_action) in scenarios.items():
        report = ui_simulator.generate(tmp_path / scenario, views=("settings", "storage_setup_sheet"), scenario=scenario)
        labels_by_view = {view["name"]: set(view["labels"]) for view in report["views"]}
        storage_view = next(view for view in report["views"] if view["name"] == "storage_setup_sheet")

        assert report["ok"] is True, scenario
        assert report["overflow_count"] == 0, scenario
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
        else:
            assert "messages NVS / packets NVS / routes NVS" in labels_by_view["storage_setup_sheet"]
        assert storage_view["overflow"] == []


def test_ui_simulator_is_documented_and_run_in_ci():
    workflow = read(".github/workflows/d1l-ci.yml")
    test_plan = read("docs/TEST_PLAN_D1L.md")
    roadmap = read("docs/ROADMAP.md")
    checklist = read("docs/RELEASE_CHECKLIST.md")

    assert "Pillow" in workflow
    assert "python ./tools/ui_simulator.py --out artifacts/ui-sim" in workflow
    assert "python ./tools/ui_simulator.py --scenario large-mesh --out artifacts/ui-sim-large" in workflow
    assert "python .\\tools\\ui_simulator.py --out artifacts\\ui-sim" in test_plan
    assert "python .\\tools\\ui_simulator.py --scenario large-mesh --out artifacts\\ui-sim-large" in test_plan
    assert "tools/ui_simulator.py" in roadmap
    assert "large-mesh" in roadmap
    assert "Simulator screenshots captured" in checklist
    assert "Large simulated mesh UI stress passes" in checklist
