from pathlib import Path

import pytest

from scripts import rf_full_acceptance_d1l as rf_accept
from scripts.smoke_d1l import expected_command_name


class FakeSerial:
    def __init__(self):
        self.reset_count = 0

    def reset_input_buffer(self):
        self.reset_count += 1


def test_rf_full_acceptance_dry_run_is_dm_only():
    report = rf_accept.dry_run_report(
        port="COM12",
        peer_status_path=Path("peer-status.json"),
        peer_port="COM17",
        fingerprint="0BF0A701D5AE2DB6",
        public_key=rf_accept.DEFAULT_D1L_PUBLIC_KEY,
        token="rf_unit",
        send_outbound=True,
    )

    assert report["ok"] is True
    assert report["schema"] == rf_accept.RF_FULL_ACCEPTANCE_SCHEMA == 2
    assert report["hardware_required"] is False
    assert report["physical_observed"] is False
    assert report["dry_run"] is True
    assert report["dm_rf_tx"] is False
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
    peer = {
        "serial": {"active_port": "COM17", "meshcore_connected": True},
        "discord": {"connected": True},
    }

    report = rf_accept.build_report(
        port="COM12",
        baud=115200,
        peer_status_path=Path("status.json"),
        peer_port="COM17",
        fingerprint="0BF0A701D5AE2DB6",
        public_key=rf_accept.DEFAULT_D1L_PUBLIC_KEY,
        token="rf_unit",
        send_outbound=True,
        steps=steps,
        peer_before=peer,
        peer_after=peer,
        inbound_seen_at="2026-07-01T00:00:00+00:00",
    )

    assert report["ok"] is True
    assert report["schema"] == rf_accept.RF_FULL_ACCEPTANCE_SCHEMA == 2
    assert report["hardware_required"] is True
    assert report["physical_observed"] is True
    assert report["dry_run"] is False
    assert report["dm_rf_tx"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["controlled_peer"]["evidence_source"] == "explicit_peer_status"
    assert report["controlled_peer"]["port"] == "COM17"
    assert report["checks"]["identity_public_key_matches"] is True
    assert report["checks"]["controlled_peer_observed"] is True
    assert report["checks"]["inbound_dm"] is True
    assert report["checks"]["ack_path"] is True
    assert report["checks"]["direct_route"] is True
    assert report["checks"]["no_public_commands"] is True

    d1l_observed = rf_accept.build_report(
        port="COM12",
        baud=115200,
        peer_status_path=None,
        peer_port=None,
        fingerprint="0BF0A701D5AE2DB6",
        public_key=rf_accept.DEFAULT_D1L_PUBLIC_KEY,
        token="rf_unit",
        send_outbound=True,
        steps=steps,
        peer_before=None,
        peer_after=None,
        inbound_seen_at="2026-07-01T00:00:00+00:00",
    )

    assert d1l_observed["ok"] is True
    assert d1l_observed["controlled_peer"] == {
        "fingerprint": "0BF0A701D5AE2DB6",
        "evidence_source": "d1l_bidirectional_rf",
        "port": None,
        "status_path": None,
    }


def test_rf_full_acceptance_rejects_missing_inbound_token():
    peer = {
        "serial": {"active_port": "COM17", "meshcore_connected": True},
        "discord": {"connected": True},
    }
    report = rf_accept.build_report(
        port="COM12",
        baud=115200,
        peer_status_path=Path("status.json"),
        peer_port="COM17",
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
        peer_before=peer,
        peer_after=peer,
        inbound_seen_at=None,
    )

    assert report["ok"] is False
    assert report["checks"]["inbound_dm"] is False
    assert report["checks"]["ack_path"] is False
    assert report["checks"]["direct_route"] is False


def test_rf_full_acceptance_rejects_stale_packet_ack_without_tx_ack():
    peer = {
        "serial": {"active_port": "COM17", "meshcore_connected": True},
        "discord": {"connected": True},
    }
    report = rf_accept.build_report(
        port="COM12",
        baud=115200,
        peer_status_path=Path("status.json"),
        peer_port="COM17",
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
        peer_before=peer,
        peer_after=peer,
        inbound_seen_at="2026-07-01T00:00:00+00:00",
    )

    assert report["ok"] is False
    assert report["checks"]["inbound_dm"] is True
    assert report["checks"]["ack_path"] is False
    assert report["checks"]["direct_route"] is True


def test_rf_full_acceptance_accepts_truncated_ack_kind_when_tx_is_acked():
    peer = {
        "serial": {"active_port": "COM17", "meshcore_connected": True},
        "discord": {"connected": True},
    }
    report = rf_accept.build_report(
        port="COM12",
        baud=115200,
        peer_status_path=Path("status.json"),
        peer_port="COM17",
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
            {"command": "packets", "result": {"ok": True, "entries": [{"kind": "dm_ack_unmatche"}]}},
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
        peer_before=peer,
        peer_after=peer,
        inbound_seen_at="2026-07-01T00:00:00+00:00",
    )

    assert report["ok"] is True
    assert report["checks"]["ack_path"] is True


def test_rf_full_acceptance_prefixes_match_console_response_names():
    assert expected_command_name("mesh send dm 0BF0A701D5AE2DB6 hello") == "mesh send dm"
    assert expected_command_name("messages dm 0BF0A701D5AE2DB6") == "messages dm"


def test_rf_full_acceptance_retries_read_only_timeout(monkeypatch):
    responses = iter([
        {"ok": False, "cmd": "identity status", "code": "TIMEOUT"},
        {"ok": True, "cmd": "identity status", "fingerprint": "BA14729E8588E30B"},
    ])
    calls = []

    def fake_send(_ser, command, _timeout):
        calls.append(command)
        return next(responses)

    monkeypatch.setattr(rf_accept, "send_console_command", fake_send)
    monkeypatch.setattr(rf_accept.time, "sleep", lambda _seconds: None)
    ser = FakeSerial()

    result = rf_accept.send_acceptance_command(ser, "identity status", 1.0)

    assert result["ok"] is True
    assert calls == ["identity status", "identity status"]
    assert ser.reset_count == 1


def test_rf_full_acceptance_does_not_retry_dm_send_timeout(monkeypatch):
    calls = []

    def fake_send(_ser, command, _timeout):
        calls.append(command)
        return {"ok": False, "cmd": "mesh send dm", "code": "TIMEOUT"}

    monkeypatch.setattr(rf_accept, "send_console_command", fake_send)
    ser = FakeSerial()

    result = rf_accept.send_acceptance_command(ser, "mesh send dm 0BF0A701D5AE2DB6 token", 1.0)

    assert result["ok"] is False
    assert calls == ["mesh send dm 0BF0A701D5AE2DB6 token"]
    assert ser.reset_count == 0
    assert expected_command_name("messages dm 0BF0A701D5AE2DB6") == "messages dm"
    assert expected_command_name("packets search rf_unit") == "packets search"


@pytest.mark.parametrize(
    "port",
    ["COM8", " com11 ", r"\\.\COM29", "//?/COM11"],
)
def test_rf_full_acceptance_rejects_forbidden_d1l_or_peer_ports(port):
    with pytest.raises(ValueError, match="forbidden"):
        rf_accept.enforce_port_policy(port)
    with pytest.raises(ValueError, match="forbidden"):
        rf_accept.enforce_port_policy("COM12", port)


def test_rf_full_acceptance_rejects_non_serial_peer_port():
    with pytest.raises(ValueError, match="invalid controlled-peer port"):
        rf_accept.enforce_port_policy("COM12", "COM_DISABLED")
