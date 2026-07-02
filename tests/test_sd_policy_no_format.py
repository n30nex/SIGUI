from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_no_on_device_sd_format_command_remains():
    retired_request = "DESKOS_SD_" + "FORMAT"
    retired_confirmation = "FORMAT-" + "DESKOS-SD"
    retired_api = "d1l_rp2040_bridge_" + "format_sd"
    retired_storage_api = "d1l_storage_" + "format_sd_confirmed"

    checked_roots = ("main", "firmware", "scripts", "tools", "docs")
    offenders: list[str] = []
    for root_name in checked_roots:
        for path in (ROOT / root_name).rglob("*"):
            if not path.is_file() or path.suffix.lower() not in {".c", ".h", ".ino", ".py", ".md", ".ps1", ".sh"}:
                continue
            if path.name == "MeshCore_DeskOS_D1L_Final_Roadmap_and_Codex_Handoff_2026-07-02.md":
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if any(token in text for token in (retired_request, retired_confirmation, retired_api, retired_storage_api)):
                offenders.append(str(path.relative_to(ROOT)))

    assert offenders == []


def test_sd_user_policy_copy_is_fat32_only():
    ui = read("main/ui/ui_phase1.c")
    console = read("main/comms/usb_console.c")
    storage = read("main/storage/storage_status.c")
    bridge = read("firmware/rp2040_sd_bridge/deskos_sd_bridge/deskos_sd_bridge.ino")

    assert "DeskOS creates its folders automatically and never formats cards on-device." in ui
    assert '\\"policy\\":\\"no_device_format\\"' in console
    assert "prepare_fat32_on_computer" in storage
    assert "not_fat32_or_unmountable" in bridge
    assert "needs_fat32=" in bridge
