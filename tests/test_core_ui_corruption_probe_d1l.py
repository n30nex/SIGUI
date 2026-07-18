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


def test_core_ui_plan_is_non_closing_and_only_probes_excluded_ui_fail_closed():
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
    assert plan["unavailable_ui_probes"] == [
        {"command": "ui tab map", "feature": "map"},
        {
            "command": "ui scroll-probe wi-fi",
            "feature": "wifi_user_control",
        },
        {"command": "ui scroll-probe map-menu", "feature": "map"},
        {
            "command": "ui scroll-probe contact-route",
            "feature": "user_trace",
        },
        {"command": "ui scroll-probe mesh-roles", "feature": "admin"},
        {
            "command": "ui compose-probe map_location",
            "feature": "location",
        },
        {
            "command": "ui compose-probe wifi-pass",
            "feature": "wifi_user_control",
        },
        {
            "command": "ui scroll-probe storage-card",
            "feature": "sd_history",
        },
    ]
    assert not any(
        command.startswith(("map ", "wifi ", "ble "))
        for command in plan["commands"]
    )
    assert plan["public_rf_tx"] is False
    assert plan["network_tx"] is False
    assert plan["map_network_requests"] is False
    assert plan["formats_sd"] is False


def _status(tab: str = "home") -> dict:
    return {
        "schema": 1,
        "ok": True,
        "cmd": "ui status",
        "build_commit": "a" * 40,
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "active_tab": tab,
        "pending": False,
    }


def _unavailable_events() -> list[dict]:
    return [
        {
            **probe,
            "before": _status(),
            "result": {
                "schema": 1,
                "ok": False,
                "cmd": probe["command"],
                "code": "ESP_ERR_NOT_SUPPORTED",
                "release_profile": "core_1_0",
                "feature": probe["feature"],
            },
            "after": _status(),
        }
        for probe in core_ui.unavailable_ui_probe_plan("disabled")
    ]


def test_unavailable_ui_events_require_exact_rejection_and_stable_tab():
    events = _unavailable_events()
    assert core_ui.unavailable_ui_events_ok(
        events, "a" * 40, "disabled"
    )
    assert not core_ui.unavailable_ui_events_ok(
        events[:-1], "a" * 40, "disabled"
    )

    tampered = _unavailable_events()
    tampered[1]["after"] = _status("messages")
    assert not core_ui.unavailable_ui_events_ok(
        tampered, "a" * 40, "disabled"
    )

    tampered = _unavailable_events()
    tampered[2]["result"]["feature"] = "location"
    assert not core_ui.unavailable_ui_events_ok(
        tampered, "a" * 40, "disabled"
    )
