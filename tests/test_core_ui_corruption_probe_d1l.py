from scripts import core_ui_corruption_probe_d1l as core_ui


def test_core_ui_sequence_is_exact_and_excludes_unavailable_destinations():
    assert core_ui.CORE_TAB_SEQUENCE == (
        "home",
        "messages",
        "nodes",
        "packets",
        "settings",
    )
    assert core_ui.core_tab_sequence_ok(list(core_ui.CORE_TAB_SEQUENCE))
    assert not core_ui.core_tab_sequence_ok(
        ["home", "messages", "nodes", "map", "packets", "settings"]
    )
    assert not core_ui.core_tab_sequence_ok(
        ["home", "messages", "nodes", "settings", "packets"]
    )


def test_core_ui_plan_is_non_closing_and_has_no_map_or_network_command():
    plan = core_ui.command_plan(20)

    assert plan["ok"] is False
    assert plan["closure_eligible"] is False
    assert plan["hardware_required"] is True
    assert plan["tabs"] == list(core_ui.CORE_TAB_SEQUENCE)
    assert "map" not in plan["tabs"]
    assert plan["scroll_surfaces"] == [
        "home",
        "public_messages",
        "dm_thread",
        "nodes",
        "packets",
        "settings",
    ]
    assert plan["compose_targets"] == [
        "public",
        "public-long",
        "dm",
        "dm-long",
        "public-search",
        "dm-search",
        "packet-search",
        "contact-edit",
        "onboarding",
    ]
    assert not any(
        command.startswith(("map ", "wifi ", "ble "))
        or command == "ui tab map"
        for command in plan["commands"]
    )
    assert plan["public_rf_tx"] is False
    assert plan["network_tx"] is False
    assert plan["map_network_requests"] is False
    assert plan["formats_sd"] is False
