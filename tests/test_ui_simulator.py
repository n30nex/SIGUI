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


def test_ui_simulator_covers_current_touch_surfaces(tmp_path):
    report = ui_simulator.generate(tmp_path)
    labels_by_view = {view["name"]: set(view["labels"]) for view in report["views"]}

    assert {"Messages", "Read", "Compose", "Test", "Public", "Direct"} <= labels_by_view["messages"]
    assert {"Nodes", "Contacts", "Heard Nodes", "DM"} <= labels_by_view["nodes"]
    assert {"Packets", "Signal", "Mesh Roles", "All", "RX", "TX", "Text", "Search", "Routes", "Packet Feed"} <= labels_by_view["packets"]
    assert {"Radio Settings", "Freq 910.525 MHz", "-25k", "+25k", "Cycle BW", "Save"} <= labels_by_view["radio_settings_sheet"]
    assert {"Room Servers", "Repeater Candidates", "Close"} <= labels_by_view["mesh_roles_sheet"]
    assert {"Packet Search", "Search kind, note, raw hex", "Apply", "Clear", "Close"} <= labels_by_view["packet_search_sheet"]
    assert {"Contact Detail", "Export", "Fav", "Mute"} <= labels_by_view["contact_detail_sheet"]
    assert {"Contact Export", "MeshCore QR", "Fingerprint", "URI", "Close"} <= labels_by_view["contact_export_sheet"]
    assert {"DM Thread", "Reply", "Read"} <= labels_by_view["dm_thread_sheet"]
    assert {"Route Detail", "Packet Detail", "Raw Hex"} <= (labels_by_view["route_detail_sheet"] | labels_by_view["packet_detail_sheet"])
    assert {"First boot setup", "Node name", "Start", "Use Defaults"} <= labels_by_view["onboarding_sheet"]


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
