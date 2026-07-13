from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def function_body(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_retained_store_diagnostics_report_target_truth_and_reboot_is_combined():
    console = read("main/comms/usb_console.c")
    worker = read("main/mesh/route_store_worker.c")

    public = function_body(
        console, "static void cmd_messages_public", "static void cmd_messages_clear"
    )
    dm = function_body(
        console, "static void cmd_messages_dm", "static void cmd_messages_dm_clear"
    )
    packets = function_body(
        console, "static void print_packet_entries_json", "static bool read_packet_token"
    )

    for body in (public, dm, packets):
        assert r'\"generation\":%lu' in body
        assert r'\"nvs\":{\"dirty\":%s' in body
        assert "persistence_dirty" in body

    assert r'\"persistence\":{\"loaded\":%s,\"dirty\":%s' in public
    assert r'\"persistence\":{\"loaded\":%s,\"dirty\":%s' in dm
    assert r'\"persistence\":{\"loaded\":%s,\"dirty\":%s' in packets
    assert "bool_json(stats.loaded && !stats.persistence_dirty)" in public
    assert "bool_json(stats.loaded && !stats.persistence_dirty)" in dm
    assert "bool_json(stats->loaded && !stats->persistence_dirty)" in packets
    assert r'\"journal\":{\"dirty\":%s' in packets
    assert "sd_primary_reconcile_pending" in packets

    assert "flush_retained_stores_locked(true)" in worker
    assert "flush_retained_stores_locked(false)" in worker
    for store in ("message_store", "dm_store", "packet_log", "route_store"):
        assert f"d1l_{store}_flush()" in worker
        assert f"d1l_{store}_flush_if_due()" in worker

    reboot = function_body(
        console, '} else if (strcmp(line, "reboot") == 0) {',
        '} else if (strcmp(line, "factory-reset-confirm") == 0) {'
    )
    assert "d1l_route_store_worker_force_flush" in reboot
    assert "retained storage flush failed; reboot cancelled" in reboot
    assert reboot.index("d1l_route_store_worker_force_flush") < reboot.index(
        "esp_rom_software_reset_system()"
    )
    assert "esp_restart()" not in reboot
    assert r'\"retained_flush\":\"ESP_OK\"' in reboot
    assert reboot.index("d1l_storage_manager_quiesce_begin") < reboot.index(
        "d1l_route_store_worker_force_flush"
    )
    assert reboot.index("d1l_route_store_worker_force_flush") < reboot.index(
        "d1l_route_store_worker_quiesce_begin"
    )
    assert reboot.index("d1l_route_store_worker_quiesce_begin") < reboot.index(
        "d1l_rp2040_bridge_quiesce_begin"
    )
    assert r'\"storage_manager_quiesced\":true' in reboot
    assert r'\"retained_worker_quiesced\":true' in reboot
    assert r'\"rp2040_bridge_quiesced\":true' in reboot
