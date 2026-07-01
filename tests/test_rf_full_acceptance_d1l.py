from pathlib import Path

from scripts import rf_full_acceptance_d1l as rf_accept
from scripts.smoke_d1l import expected_command_name


def test_rf_full_acceptance_dry_run_is_dm_only():
    report = rf_accept.dry_run_report(
        port="COM12",
        bot_status_path=Path(r"F:\Meshcorebot\logs\meshcorebot.status.json"),
        bot_port="COM11",
        fingerprint="0BF0A701D5AE2DB6",
        public_key=rf_accept.DEFAULT_D1L_PUBLIC_KEY,
        token="rf_unit",
        send_outbound=True,
    )

    assert report["ok"] is True
    assert report["hardware_required"] is False
    assert report["discord_command"] == f"+dm {rf_accept.DEFAULT_D1L_PUBLIC_KEY} rf_unit_in"
    assert "mesh send dm 0BF0A701D5AE2DB6 rf_unit_out" in report["commands"]
    assert "mesh send dm 0BF0A701D5AE2DB6 rf_unit_direct" in report["commands"]
    assert not any(command.startswith("mesh send public ") for command in report["commands"])


def test_rf_full_acceptance_report_requires_real_inbound_ack_and_direct_route():
    steps = [
        {
            "command": "identity status",
            "result": {"ok": True, "cmd": "identity status", "fingerprint": "BA14729E8588E30B"},
        },
        {
            "command": "mesh send dm 0BF0A701D5AE2DB6 rf_unit_out",
            "result": {"ok": True, "cmd": "mesh send dm"},
        },
        {
            "command": "packets search rf_unit_out",
            "result": {"ok": True, "cmd": "packets search", "entries": [{"note": "rf_unit_out"}]},
        },
        {
            "command": "messages dm 0BF0A701D5AE2DB6",
            "result": {
                "ok": True,
                "cmd": "messages dm",
                "fingerprint": "0BF0A701D5AE2DB6",
                "entries": [{"direction": "rx", "text": "rf_unit_in"}],
            },
        },
        {
            "command": "mesh send dm 0BF0A701D5AE2DB6 rf_unit_direct",
            "result": {"ok": True, "cmd": "mesh send dm"},
        },
        {
            "command": "packets search rf_unit_direct",
            "result": {"ok": True, "cmd": "packets search", "entries": [{"note": "rf_unit_direct"}]},
        },
        {
            "command": "messages dm 0BF0A701D5AE2DB6",
            "result": {
                "ok": True,
                "cmd": "messages dm",
                "fingerprint": "0BF0A701D5AE2DB6",
                "entries": [
                    {
                        "direction": "tx",
                        "text": "rf_unit_out",
                        "acked": True,
                        "ack_hash": 4815162342,
                    },
                    {"direction": "rx", "text": "rf_unit_in"},
                    {"direction": "tx", "text": "rf_unit_direct"},
                ],
            },
        },
        {
            "command": "packets",
            "result": {
                "ok": True,
                "cmd": "packets",
                "entries": [
                    {"kind": "dm_ack", "note": "ack matched"},
                    {"kind": "path_return", "note": "path from peer"},
                ],
            },
        },
        {
            "command": "routes trace 0BF0A701D5AE2DB6",
            "result": {
                "ok": True,
                "cmd": "routes trace",
                "fingerprint": "0BF0A701D5AE2DB6",
                "best_route": "direct",
                "entries": [
                    {
                        "target": "0BF0A701D5AE2DB6",
                        "kind": "dm_text",
                        "direction": "tx",
                        "route": "direct",
                    }
                ],
            },
        },
        {"command": "health", "result": {"ok": True, "cmd": "health", "board_ready": True, "ui_ready": True}},
    ]
    meshbot = {
        "serial": {"active_port": "COM11", "meshcore_connected": True},
        "discord": {"connected": True},
    }

    report = rf_accept.build_report(
        port="COM12",
        baud=115200,
        bot_status_path=Path("status.json"),
        bot_port="COM11",
        fingerprint="0BF0A701D5AE2DB6",
        public_key=rf_accept.DEFAULT_D1L_PUBLIC_KEY,
        token="rf_unit",
        send_outbound=True,
        steps=steps,
        meshbot_before=meshbot,
        meshbot_after=meshbot,
        inbound_seen_at="2026-07-01T00:00:00+00:00",
    )

    assert report["ok"] is True
    assert report["checks"]["identity_public_key_matches"] is True
    assert report["checks"]["inbound_dm"] is True
    assert report["checks"]["ack_path"] is True
    assert report["checks"]["direct_route"] is True
    assert report["checks"]["no_public_commands"] is True


def test_rf_full_acceptance_rejects_missing_inbound_token():
    meshbot = {
        "serial": {"active_port": "COM11", "meshcore_connected": True},
        "discord": {"connected": True},
    }
    report = rf_accept.build_report(
        port="COM12",
        baud=115200,
        bot_status_path=Path("status.json"),
        bot_port="COM11",
        fingerprint="0BF0A701D5AE2DB6",
        public_key=rf_accept.DEFAULT_D1L_PUBLIC_KEY,
        token="rf_unit",
        send_outbound=False,
        steps=[
            {"command": "identity status", "result": {"ok": True, "fingerprint": "BA14729E8588E30B"}},
            {"command": "messages dm 0BF0A701D5AE2DB6", "result": {"ok": True, "entries": []}},
            {"command": "packets", "result": {"ok": True, "entries": [{"kind": "dm_ack"}]}},
            {
                "command": "routes trace 0BF0A701D5AE2DB6",
                "result": {
                    "ok": True,
                    "fingerprint": "0BF0A701D5AE2DB6",
                    "entries": [
                        {
                            "target": "0BF0A701D5AE2DB6",
                            "kind": "dm_text",
                            "direction": "tx",
                            "route": "direct",
                        }
                    ],
                },
            },
            {"command": "health", "result": {"ok": True, "board_ready": True, "ui_ready": True}},
        ],
        meshbot_before=meshbot,
        meshbot_after=meshbot,
        inbound_seen_at=None,
    )

    assert report["ok"] is False
    assert report["checks"]["inbound_dm"] is False
    assert report["checks"]["ack_path"] is False
    assert report["checks"]["direct_route"] is False


def test_rf_full_acceptance_rejects_stale_packet_ack_without_tx_ack():
    meshbot = {
        "serial": {"active_port": "COM11", "meshcore_connected": True},
        "discord": {"connected": True},
    }
    report = rf_accept.build_report(
        port="COM12",
        baud=115200,
        bot_status_path=Path("status.json"),
        bot_port="COM11",
        fingerprint="0BF0A701D5AE2DB6",
        public_key=rf_accept.DEFAULT_D1L_PUBLIC_KEY,
        token="rf_unit",
        send_outbound=True,
        steps=[
            {"command": "identity status", "result": {"ok": True, "fingerprint": "BA14729E8588E30B"}},
            {"command": "mesh send dm 0BF0A701D5AE2DB6 rf_unit_out", "result": {"ok": True}},
            {"command": "packets search rf_unit_out", "result": {"ok": True, "entries": [{"note": "rf_unit_out"}]}},
            {"command": "mesh send dm 0BF0A701D5AE2DB6 rf_unit_direct", "result": {"ok": True}},
            {
                "command": "messages dm 0BF0A701D5AE2DB6",
                "result": {
                    "ok": True,
                    "fingerprint": "0BF0A701D5AE2DB6",
                    "entries": [
                        {"direction": "tx", "text": "rf_unit_out", "acked": False, "ack_hash": 0},
                        {"direction": "rx", "text": "rf_unit_in"},
                        {"direction": "tx", "text": "rf_unit_direct"},
                    ],
                },
            },
            {"command": "packets", "result": {"ok": True, "entries": [{"kind": "dm_ack"}]}},
            {
                "command": "routes trace 0BF0A701D5AE2DB6",
                "result": {
                    "ok": True,
                    "fingerprint": "0BF0A701D5AE2DB6",
                    "entries": [
                        {
                            "target": "0BF0A701D5AE2DB6",
                            "kind": "dm_text",
                            "direction": "tx",
                            "route": "direct",
                        }
                    ],
                },
            },
            {"command": "health", "result": {"ok": True, "board_ready": True, "ui_ready": True}},
        ],
        meshbot_before=meshbot,
        meshbot_after=meshbot,
        inbound_seen_at="2026-07-01T00:00:00+00:00",
    )

    assert report["ok"] is False
    assert report["checks"]["inbound_dm"] is True
    assert report["checks"]["ack_path"] is False
    assert report["checks"]["direct_route"] is True


def test_rf_full_acceptance_prefixes_match_console_response_names():
    assert expected_command_name("mesh send dm 0BF0A701D5AE2DB6 hello") == "mesh send dm"
    assert expected_command_name("messages dm 0BF0A701D5AE2DB6") == "messages dm"
    assert expected_command_name("packets search rf_unit") == "packets search"
