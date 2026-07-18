#!/usr/bin/env python3
"""Full D1L-port-only RF acceptance runner for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import re
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from artifact_metadata import git_metadata, stamp_report
    from smoke_d1l import open_d1l_serial, send_console_command
    from verify_checksums import is_link_or_reparse, sha256_file
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import git_metadata, stamp_report
    from scripts.smoke_d1l import open_d1l_serial, send_console_command
    from scripts.verify_checksums import is_link_or_reparse, sha256_file


DEFAULT_TARGET_FINGERPRINT = "0BF0A701D5AE2DB6"
DEFAULT_D1L_PUBLIC_KEY = "ba14729e8588e30b44b36ff9c6c5511b9d88bf787196c6a46de102af6ebafa07"
RF_FULL_ACCEPTANCE_SCHEMA = 2
FORBIDDEN_PORTS = {"COM" + str(number) for number in (8, 11, 29)}
D1L_REQUIRED_PORT = "COM12"
RF_PEER_FORBIDDEN_PORTS = FORBIDDEN_PORTS | {D1L_REQUIRED_PORT, "COM16"}
RADIO_LISTENER_PORT = "COM15"
RADIO_LISTENER_REPLY = "Test OK DM."
RADIO_LISTENER_PROFILE = "openclaw_radio_listener"
RADIO_LISTENER_CONTACT_NAME = "CoreTestPeer"
RADIO_LISTENER_STATUS_PATH = Path(
    r"F:\openclaw\runtime\workspace\radio_listener.status.json"
)


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def default_token(commit: str | None = None) -> str:
    prefix = f"rf_accept_{commit[:7]}" if commit else "rf_accept"
    return f"{prefix}_{utc_stamp()}"


def normalize_port(value: str | None) -> str | None:
    if not isinstance(value, str) or not value.strip():
        return None
    normalized = value.strip().upper().replace("/", "\\")
    for prefix in ("\\\\.\\", "\\\\?\\"):
        if normalized.startswith(prefix):
            normalized = normalized[len(prefix):]
            break
    return normalized


def exact_commit(value: object) -> str | None:
    if not isinstance(value, str):
        return None
    normalized = value.strip().lower()
    if re.fullmatch(r"[0-9a-f]{40}", normalized) is None:
        return None
    return normalized


def enforce_port_policy(port: str, peer_port: str | None = None) -> tuple[str, str | None]:
    normalized_port = normalize_port(port)
    normalized_peer = normalize_port(peer_port)
    if normalized_port in FORBIDDEN_PORTS:
        raise ValueError(f"refusing forbidden D1L port {normalized_port}")
    if normalized_peer in FORBIDDEN_PORTS:
        raise ValueError(f"refusing forbidden controlled-peer port {normalized_peer}")
    if normalized_port is None:
        raise ValueError("an explicit D1L port is required")
    if re.fullmatch(r"COM[1-9][0-9]*", normalized_port) is None:
        raise ValueError(f"invalid D1L port {normalized_port}")
    if normalized_port != D1L_REQUIRED_PORT:
        raise ValueError(
            f"D1L RF acceptance requires {D1L_REQUIRED_PORT}; got {normalized_port}"
        )
    if (
        normalized_peer is not None
        and re.fullmatch(r"COM[1-9][0-9]*", normalized_peer) is None
    ):
        raise ValueError(f"invalid controlled-peer port {normalized_peer}")
    if normalized_peer in RF_PEER_FORBIDDEN_PORTS:
        raise ValueError(
            f"refusing controlled-peer port {normalized_peer} for RF acceptance"
        )
    if normalized_peer is not None and normalized_peer == normalized_port:
        raise ValueError("D1L and controlled-peer ports must be distinct")
    return normalized_port, normalized_peer


def firmware_identity_matches(
    version_result: object, expected_commit: str
) -> bool:
    return (
        isinstance(version_result, dict)
        and version_result.get("ok") is True
        and exact_commit(version_result.get("build_commit")) == expected_commit
    )


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def get_path(obj: dict | None, *parts: str, default=None):
    current = obj
    for part in parts:
        if not isinstance(current, dict) or part not in current:
            return default
        current = current[part]
    return current


def status_snapshot(status: dict | None) -> dict:
    if not status:
        return {}
    counters = status.get("counters") if isinstance(status.get("counters"), dict) else {}
    return {
        "serial": status.get("serial") if isinstance(status.get("serial"), dict) else {},
        "discord": status.get("discord") if isinstance(status.get("discord"), dict) else {},
        "mesh": status.get("mesh") if isinstance(status.get("mesh"), dict) else {},
        "counters": {
            "rx_contact_total": counters.get("rx_contact_total"),
            "rx_log_total": counters.get("rx_log_total"),
            "relay_success_total": counters.get("relay_success_total"),
            "discord_send_success_total": counters.get("discord_send_success_total"),
            "rx_dm_total": counters.get("rx_dm_total"),
            "tx_dm_total": counters.get("tx_dm_total"),
            "tx_dm_ack_miss_total": counters.get("tx_dm_ack_miss_total"),
            "local_fast_reply_total": counters.get("local_fast_reply_total"),
        },
        "run_id": status.get("run_id"),
        "service": status.get("service"),
        "status_written_at": status.get("status_written_at"),
    }


def radio_listener_connected(
    status: dict | None, peer_port: str | None, fingerprint: str
) -> bool:
    serial_status = get_path(status, "serial", default={})
    public_key = get_path(status, "serial", "public_key")
    return (
        isinstance(serial_status, dict)
        and normalize_port(serial_status.get("port")) == peer_port
        and serial_status.get("mesh_connected") is True
        and exact_public_key(public_key) is not None
        and public_key_fingerprint(str(public_key or "")) == fingerprint
        and status.get("service") == "openclaw-radio-listener"
        and isinstance(status.get("run_id"), str)
        and bool(status.get("run_id"))
    )


def counter_value(status: dict | None, name: str) -> int | None:
    value = get_path(status, "counters", name)
    return (
        value
        if isinstance(value, int) and not isinstance(value, bool) and value >= 0
        else None
    )


def counter_delta(
    before: dict | None, after: dict | None, name: str
) -> int | None:
    left = counter_value(before, name)
    right = counter_value(after, name)
    return right - left if left is not None and right is not None else None


def new_exact_listener_reply(
    before: dict | None,
    after: dict | None,
    fingerprint: str,
) -> bool:
    before_rows = {
        json.dumps(entry, sort_keys=True, separators=(",", ":"))
        for entry in entries(before)
        if entry.get("direction") == "rx"
        and entry.get("text") == RADIO_LISTENER_REPLY
        and entry_matches_fingerprint(before, entry, fingerprint)
    }
    return any(
        entry.get("direction") == "rx"
        and entry.get("text") == RADIO_LISTENER_REPLY
        and entry_matches_fingerprint(after, entry, fingerprint)
        and json.dumps(entry, sort_keys=True, separators=(",", ":"))
        not in before_rows
        for entry in entries(after)
    )


def capture_peer_status(
    source_path: Path,
    capture_path: Path,
    root: Path,
) -> tuple[dict, dict]:
    raw = source_path.read_bytes()
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError("controlled-peer status must be a JSON object")
    root_resolved = root.resolve(strict=True)
    capture_path = capture_path.resolve(strict=False)
    try:
        capture_path.relative_to(root_resolved)
    except ValueError as exc:
        raise ValueError("controlled-peer capture must stay inside root") from exc
    cursor = capture_path.parent
    while cursor != root_resolved:
        if cursor.exists() and is_link_or_reparse(cursor):
            raise ValueError(
                "controlled-peer capture parent cannot be a link/reparse point"
            )
        cursor = cursor.parent
    capture_path.parent.mkdir(parents=True, exist_ok=True)
    if capture_path.exists():
        raise ValueError(
            f"refusing to overwrite controlled-peer capture: {capture_path}"
        )
    capture_path.write_bytes(raw)
    resolved = capture_path.resolve(strict=True)
    resolved.relative_to(root_resolved)
    if is_link_or_reparse(resolved):
        raise ValueError("controlled-peer capture cannot be a link/reparse point")
    return value, {
        "path": resolved.relative_to(root_resolved).as_posix(),
        "size": resolved.stat().st_size,
        "sha256": sha256_file(resolved),
        "source_path": str(source_path.resolve()),
        "captured_at": datetime.now(timezone.utc).isoformat(),
    }


def contains_token(value, token: str) -> bool:
    return token in json.dumps(value, sort_keys=True)


def public_key_fingerprint(public_key: str) -> str | None:
    key = str(public_key or "").strip().upper()
    if len(key) < 16:
        return None
    prefix = key[:16]
    if not all(char in "0123456789ABCDEF" for char in prefix):
        return None
    return prefix


def exact_public_key(public_key: object) -> str | None:
    if not isinstance(public_key, str):
        return None
    normalized = public_key.strip().lower()
    return (
        normalized
        if re.fullmatch(r"[0-9a-f]{64}", normalized)
        else None
    )


def listener_sender_matches(
    status: dict | None, d1l_public_key: object
) -> bool:
    public_key = exact_public_key(d1l_public_key)
    sender = get_path(status, "mesh", "last_rx_sender")
    return (
        public_key is not None
        and isinstance(sender, str)
        and re.fullmatch(r"[0-9A-Fa-f]{12}", sender) is not None
        and sender.upper() == public_key[:12].upper()
    )


def contact_import_command(public_key: str) -> str:
    normalized = exact_public_key(public_key)
    if normalized is None:
        raise ValueError("controlled-peer public key must be exactly 64 hex")
    return (
        "contacts import meshcore://contact/add?"
        f"name={RADIO_LISTENER_CONTACT_NAME}"
        f"&public_key={normalized}&type=1"
    )


def contact_import_ok(
    result: object, public_key: str, fingerprint: str
) -> bool:
    normalized = exact_public_key(public_key)
    return (
        isinstance(result, dict)
        and result.get("ok") is True
        and result.get("cmd") == "contacts import"
        and result.get("persisted") is True
        and result.get("result") in {"created", "updated", "promoted"}
        and result.get("verification_source") == "uri_import"
        and str(result.get("fingerprint") or "").upper() == fingerprint
        and exact_public_key(result.get("public_key")) == normalized
        and result.get("alias") == RADIO_LISTENER_CONTACT_NAME
        and result.get("type") == "chat"
        and result.get("canonical") is True
        and result.get("can_dm") is True
        and result.get("can_admin") is False
    )


def contacts_has_exact_peer(
    result: object, public_key: str, fingerprint: str
) -> bool:
    normalized = exact_public_key(public_key)
    if not isinstance(result, dict) or result.get("ok") is not True:
        return False
    matches = [
        entry
        for entry in entries(result)
        if str(entry.get("fingerprint") or "").upper() == fingerprint
    ]
    return (
        normalized is not None
        and len(matches) == 1
        and exact_public_key(matches[0].get("public_key")) == normalized
        and matches[0].get("alias") == RADIO_LISTENER_CONTACT_NAME
        and matches[0].get("type") == "chat"
        and matches[0].get("verification_source") == "uri_import"
        and matches[0].get("canonical") is True
        and matches[0].get("can_dm") is True
        and matches[0].get("can_admin") is False
    )


def entries(value: dict | None) -> list[dict]:
    rows = value.get("entries") if isinstance(value, dict) else None
    return rows if isinstance(rows, list) else []


def entry_matches_fingerprint(value: dict | None, entry: dict, fingerprint: str) -> bool:
    top_level = value.get("fingerprint") if isinstance(value, dict) else None
    entry_fingerprint = entry.get("fingerprint")
    return (not top_level or top_level == fingerprint) and (not entry_fingerprint or entry_fingerprint == fingerprint)


def messages_have_inbound_token(value: dict | None, token: str, fingerprint: str) -> bool:
    for entry in entries(value):
        text = entry.get("text")
        if (
            entry.get("direction") == "rx"
            and isinstance(text, str)
            and token in text
            and entry_matches_fingerprint(value, entry, fingerprint)
        ):
            return True
    return False


def messages_have_tx_token(value: dict | None, token: str, fingerprint: str) -> bool:
    for entry in entries(value):
        text = entry.get("text")
        if (
            entry.get("direction") == "tx"
            and isinstance(text, str)
            and token in text
            and entry_matches_fingerprint(value, entry, fingerprint)
        ):
            return True
    return False


def messages_have_acked_tx(value: dict | None, token: str, fingerprint: str) -> bool:
    for entry in entries(value):
        ack_hash = entry.get("ack_hash")
        one_entry = {
            "fingerprint": value.get("fingerprint") if isinstance(value, dict) else None,
            "entries": [entry],
        }
        if (
            messages_have_tx_token(one_entry, token, fingerprint)
            and entry.get("acked") is True
            and ack_hash not in (None, "", 0, "0", "0x0", "0X0")
        ):
            return True
    return False


def packets_have_ack_or_path(value: dict | None) -> bool:
    for entry in entries(value):
        kind = str(entry.get("kind", "")).lower()
        if kind.startswith("dm_ack") or kind.startswith("path_return"):
            return True
    return False


def route_has_direct_path(value: dict | None, fingerprint: str) -> bool:
    if not isinstance(value, dict) or value.get("ok") is not True:
        return False
    if value.get("fingerprint") != fingerprint:
        return False
    for entry in entries(value):
        if (
            entry.get("target") == fingerprint
            and entry.get("kind") == "dm_text"
            and entry.get("direction") == "tx"
            and entry.get("route") == "direct"
        ):
            return True
    return False


def _positive_seq(value: object) -> int | None:
    return (
        value
        if isinstance(value, int)
        and not isinstance(value, bool)
        and value >= 1
        else None
    )


def new_entries_by_seq(
    before: dict | None, after: dict | None
) -> list[dict]:
    before_rows = entries(before)
    after_rows = entries(after)
    before_seq_list = [
        seq
        for row in before_rows
        if (seq := _positive_seq(row.get("seq"))) is not None
    ]
    after_seqs = [
        seq
        for row in after_rows
        if (seq := _positive_seq(row.get("seq"))) is not None
    ]
    if (
        len(before_seq_list) != len(before_rows)
        or len(before_seq_list) != len(set(before_seq_list))
        or len(after_seqs) != len(after_rows)
        or len(after_seqs) != len(set(after_seqs))
    ):
        return []
    before_seqs = set(before_seq_list)
    baseline_max = max(before_seq_list, default=0)
    fresh = [
        row
        for row in after_rows
        if _positive_seq(row.get("seq")) not in before_seqs
    ]
    if any(
        (_positive_seq(row.get("seq")) or 0) <= baseline_max
        for row in fresh
    ):
        return []
    return fresh


def correlated_listener_transaction(
    *,
    baseline_messages: dict | None,
    final_messages: dict | None,
    baseline_packets: dict | None,
    final_packets: dict | None,
    baseline_route: dict | None,
    final_route: dict | None,
    outbound_token: str,
    fingerprint: str,
) -> dict:
    new_messages = new_entries_by_seq(
        baseline_messages, final_messages
    )
    new_packets = new_entries_by_seq(baseline_packets, final_packets)
    new_routes = new_entries_by_seq(baseline_route, final_route)
    tx_rows = [
        row
        for row in new_messages
        if row.get("direction") == "tx"
        and row.get("text")
        == f"core acceptance test {outbound_token}"
        and entry_matches_fingerprint(
            final_messages, row, fingerprint
        )
    ]
    reply_rows = [
        row
        for row in new_messages
        if row.get("direction") == "rx"
        and row.get("text") == RADIO_LISTENER_REPLY
        and entry_matches_fingerprint(
            final_messages, row, fingerprint
        )
    ]
    tx = tx_rows[0] if len(tx_rows) == 1 else {}
    reply = reply_rows[0] if len(reply_rows) == 1 else {}
    ack_hash = tx.get("ack_hash")
    ack_hash = (
        ack_hash
        if isinstance(ack_hash, int)
        and not isinstance(ack_hash, bool)
        and 0 < ack_hash <= 0xFFFFFFFF
        else None
    )
    tx_ack_response = tx.get("ack_response")
    tx_ack_response = (
        tx_ack_response if isinstance(tx_ack_response, dict) else {}
    )
    reply_ack_response = reply.get("ack_response")
    reply_ack_response = (
        reply_ack_response
        if isinstance(reply_ack_response, dict)
        else {}
    )
    tx_row_ok = (
        bool(tx)
        and not contains_token(baseline_messages, outbound_token)
        and tx.get("acked") is True
        and tx.get("delivered") is True
        and ack_hash is not None
        and tx_ack_response
        == {
            "identity_valid": False,
            "state": "legacy_unverified",
            "dispatch_count": 0,
            "last_kind": "none",
            "last_error": "ESP_OK",
        }
    )
    reply_ack_ok = (
        bool(reply)
        and reply_ack_response.get("identity_valid") is True
        and reply_ack_response.get("state") == "sent"
        and isinstance(
            reply_ack_response.get("dispatch_count"), int
        )
        and not isinstance(
            reply_ack_response.get("dispatch_count"), bool
        )
        and reply_ack_response["dispatch_count"] >= 1
        and reply_ack_response.get("last_kind")
        in {
            "direct_ack",
            "flood_ack",
            "flood_ack_path",
            "path_ack",
        }
        and reply_ack_response.get("last_error") == "ESP_OK"
    )
    ack_note = (
        f"ack {ack_hash} {RADIO_LISTENER_CONTACT_NAME}"
        if ack_hash is not None
        else ""
    )
    packet_rows = [
        row
        for row in new_packets
        if row.get("direction") == "rx"
        and row.get("kind") == "dm_ack"
        and row.get("note") == ack_note
    ]
    route_rows = [
        row
        for row in new_routes
        if row.get("target") == fingerprint
        and row.get("kind") == "dm_ack"
        and row.get("direction") == "rx"
        and row.get("route") == "direct"
    ]
    packet = packet_rows[0] if len(packet_rows) == 1 else {}
    route = route_rows[0] if len(route_rows) == 1 else {}
    metadata_pairs = (
        ("rssi_dbm", "last_rssi_dbm"),
        ("snr_tenths", "last_snr_tenths"),
        ("path_hash_bytes", "path_hash_bytes"),
        ("path_hops", "path_hops"),
        ("payload_len", "payload_len"),
    )
    packet_metadata_ok = (
        isinstance(packet.get("rssi_dbm"), int)
        and not isinstance(packet.get("rssi_dbm"), bool)
        and isinstance(packet.get("snr_tenths"), int)
        and not isinstance(packet.get("snr_tenths"), bool)
        and isinstance(packet.get("path_hash_bytes"), int)
        and not isinstance(packet.get("path_hash_bytes"), bool)
        and 0 <= packet["path_hash_bytes"] <= 64
        and isinstance(packet.get("path_hops"), int)
        and not isinstance(packet.get("path_hops"), bool)
        and 0 <= packet["path_hops"] <= 64
        and isinstance(packet.get("payload_len"), int)
        and not isinstance(packet.get("payload_len"), bool)
        and 0 < packet["payload_len"] <= 65535
    )
    packet_route_metadata_match = (
        bool(packet and route)
        and packet_metadata_ok
        and all(
            packet.get(packet_field) == route.get(route_field)
            for packet_field, route_field in metadata_pairs
        )
    )
    ack_path_ok = (
        tx_row_ok
        and reply_ack_ok
        and len(packet_rows) == 1
        and packet_route_metadata_match
    )
    direct_route_ok = (
        ack_path_ok
        and len(route_rows) == 1
        and route.get("route") == "direct"
    )
    return {
        "ok": ack_path_ok and direct_route_ok,
        "outbound_dm_seq": _positive_seq(tx.get("seq")),
        "inbound_reply_seq": _positive_seq(reply.get("seq")),
        "ack_hash": ack_hash,
        "reply_ack_state": reply_ack_response.get("state"),
        "reply_ack_kind": reply_ack_response.get("last_kind"),
        "packet_seq": _positive_seq(packet.get("seq")),
        "route_seq": _positive_seq(route.get("seq")),
        "packet_route_metadata_match": packet_route_metadata_match,
        "ack_path_ok": ack_path_ok,
        "direct_route_ok": direct_route_ok,
    }


def command_has_public_tx(command: str) -> bool:
    return command.strip().lower().startswith("mesh send public ")


def command_step(steps: list[dict], command: str) -> dict | None:
    for step in steps:
        if step.get("command") == command:
            return step
    return None


def first_command_step(steps: list[dict], command: str) -> dict | None:
    return command_step(steps, command)


def latest_command_step(steps: list[dict], command: str) -> dict | None:
    for step in reversed(steps):
        if step.get("command") == command:
            return step
    return None


def latest_step_with_prefix(steps: list[dict], prefix: str) -> dict | None:
    for step in reversed(steps):
        if str(step.get("command", "")).startswith(prefix):
            return step
    return None


def discord_command(public_key: str, inbound_token: str) -> str:
    return f"+dm {public_key} {inbound_token}"


def dry_run_report(
    *,
    port: str,
    peer_status_path: Path | None,
    peer_port: str | None,
    fingerprint: str,
    public_key: str,
    token: str,
    send_outbound: bool,
    expected_commit: str | None = None,
    github_run_id: str | None = None,
    workflow_run_attempt: str | None = None,
) -> dict:
    outbound_token = f"{token}_out"
    inbound_token = f"{token}_in"
    direct_token = f"{token}_direct"
    commands = [
        "identity status",
        "contacts",
    ]
    if send_outbound:
        commands.extend(
            [
                f"mesh send dm {fingerprint} {outbound_token}",
                f"packets search {outbound_token}",
            ]
        )
    commands.append(f"messages dm {fingerprint}")
    if send_outbound:
        commands.extend(
            [
                "packets",
                f"routes trace {fingerprint}",
                f"mesh send dm {fingerprint} {direct_token}",
                f"packets search {direct_token}",
                f"messages dm {fingerprint}",
            ]
        )
    commands.extend(["packets", f"routes trace {fingerprint}", "health"])
    controlled_peer = {
        "fingerprint": fingerprint,
        "evidence_source": (
            "explicit_peer_status"
            if peer_status_path is not None and peer_port is not None
            else "d1l_bidirectional_rf"
        ),
        "port": peer_port,
        "status_path": str(peer_status_path) if peer_status_path is not None else None,
    }
    return {
        "schema": RF_FULL_ACCEPTANCE_SCHEMA,
        "mode": "dry-run-rf-full-acceptance",
        "hardware_required": False,
        "physical_observed": False,
        "dry_run": True,
        "simulated": False,
        "simulation": False,
        "source_inspection": False,
        "execution_complete": False,
        "closure_eligible": False,
        "dm_rf_tx": False,
        "public_rf_tx": False,
        "formats_sd": False,
        "port": port,
        "controlled_peer": controlled_peer,
        "target_fingerprint": fingerprint,
        "d1l_public_key": public_key,
        "token": token,
        "expected_firmware_commit": exact_commit(expected_commit),
        "github_actions_run": (
            str(github_run_id) if github_run_id is not None else None
        ),
        "workflow_run_attempt": (
            str(workflow_run_attempt)
            if workflow_run_attempt is not None
            else None
        ),
        "device_release_profile": None,
        "device_sd_history_mode": None,
        "firmware_identity_required": True,
        "firmware_identity_ok": False,
        "outbound_token": outbound_token,
        "inbound_token": inbound_token,
        "direct_token": direct_token,
        "expected_identity_fingerprint": public_key_fingerprint(public_key),
        "discord_command": discord_command(public_key, inbound_token),
        "public_rf_transmit": False,
        "commands": commands,
        "ok": True,
    }


def build_report(
    *,
    port: str,
    baud: int,
    peer_status_path: Path | None,
    peer_port: str | None,
    fingerprint: str,
    public_key: str,
    token: str,
    send_outbound: bool,
    steps: list[dict],
    peer_before: dict | None,
    peer_after: dict | None,
    inbound_seen_at: str | None,
    expected_commit: str | None = None,
    peer_before_receipt: dict | None = None,
    peer_after_receipt: dict | None = None,
    github_run_id: str | None = None,
    workflow_run_attempt: str | None = None,
) -> dict:
    outbound_token = f"{token}_out"
    inbound_token = f"{token}_in"
    direct_token = f"{token}_direct"
    listener_mode = (
        peer_port == RADIO_LISTENER_PORT
        and (
            get_path(peer_before, "service") == "openclaw-radio-listener"
            or get_path(peer_after, "service") == "openclaw-radio-listener"
        )
    )
    outbound_text = (
        f"core acceptance test {outbound_token}"
        if listener_mode
        else outbound_token
    )
    outbound_command = f"mesh send dm {fingerprint} {outbound_text}"
    direct_command = f"mesh send dm {fingerprint} {direct_token}"
    peer_public_key = exact_public_key(
        get_path(peer_before, "serial", "public_key")
    )
    peer_after_public_key = exact_public_key(
        get_path(peer_after, "serial", "public_key")
    )
    import_command = (
        contact_import_command(peer_public_key)
        if listener_mode and peer_public_key is not None
        else None
    )
    identity_step = latest_command_step(steps, "identity status")
    first_contacts = first_command_step(steps, "contacts")
    latest_contacts = latest_command_step(steps, "contacts")
    import_step = (
        command_step(steps, import_command)
        if import_command is not None
        else None
    )
    outbound_step = command_step(steps, outbound_command)
    outbound_packets = command_step(steps, f"packets search {outbound_token}")
    direct_step = command_step(steps, direct_command)
    latest_messages = latest_step_with_prefix(steps, f"messages dm {fingerprint}")
    first_messages = first_command_step(
        steps, f"messages dm {fingerprint}"
    )
    latest_packets = latest_command_step(steps, "packets")
    latest_route = latest_command_step(steps, f"routes trace {fingerprint}")
    latest_health = latest_command_step(steps, "health")
    version_step = latest_command_step(steps, "version")
    identity_result = identity_step.get("result", {}) if identity_step else {}
    route_result = latest_route.get("result", {}) if latest_route else {}
    packets_result = latest_packets.get("result", {}) if latest_packets else {}
    messages_result = latest_messages.get("result", {}) if latest_messages else {}
    baseline_messages_result = (
        first_messages.get("result", {}) if first_messages else {}
    )
    first_packets = first_command_step(steps, "packets")
    baseline_packets_result = (
        first_packets.get("result", {}) if first_packets else {}
    )
    first_route = first_command_step(
        steps, f"routes trace {fingerprint}"
    )
    baseline_route_result = (
        first_route.get("result", {}) if first_route else {}
    )
    health_result = latest_health.get("result", {}) if latest_health else {}
    version_result = version_step.get("result", {}) if version_step else {}
    contact_before_result = (
        first_contacts.get("result", {}) if first_contacts else {}
    )
    contact_after_result = (
        latest_contacts.get("result", {}) if latest_contacts else {}
    )
    contact_import_result = (
        import_step.get("result", {}) if import_step else {}
    )
    commands = [str(step.get("command", "")) for step in steps]
    identity_fingerprint = str(identity_result.get("fingerprint") or "").upper()
    expected_identity = public_key_fingerprint(public_key)
    peer_status_requested = peer_status_path is not None or peer_port is not None
    if listener_mode:
        peer_status_ok = (
            radio_listener_connected(peer_before, peer_port, fingerprint)
            and radio_listener_connected(peer_after, peer_port, fingerprint)
            and peer_before.get("run_id") == peer_after.get("run_id")
            and peer_public_key is not None
            and peer_after_public_key == peer_public_key
        )
        rx_dm_delta = counter_delta(peer_before, peer_after, "rx_dm_total")
        tx_dm_delta = counter_delta(peer_before, peer_after, "tx_dm_total")
        fast_reply_delta = counter_delta(
            peer_before, peer_after, "local_fast_reply_total"
        )
        ack_miss_delta = counter_delta(
            peer_before, peer_after, "tx_dm_ack_miss_total"
        )
        peer_counter_ok = (
            rx_dm_delta == 1
            and tx_dm_delta == 1
            and fast_reply_delta == 1
            and ack_miss_delta == 0
            and listener_sender_matches(peer_after, public_key)
            and get_path(peer_after, "mesh", "last_rx_kind") == "dm"
            and get_path(peer_after, "mesh", "last_tx_kind") == "dm"
            and get_path(peer_before, "mesh", "last_rx_at")
            != get_path(peer_after, "mesh", "last_rx_at")
            and get_path(peer_before, "mesh", "last_tx_at")
            != get_path(peer_after, "mesh", "last_tx_at")
        )
    else:
        peer_status_ok = (
            normalize_port(
                get_path(peer_before, "serial", "active_port")
            )
            == peer_port
            and get_path(
                peer_before, "serial", "meshcore_connected"
            )
            is True
            and get_path(peer_before, "discord", "connected") is True
        ) or (
            normalize_port(
                get_path(peer_after, "serial", "active_port")
            )
            == peer_port
            and get_path(peer_after, "serial", "meshcore_connected")
            is True
            and get_path(peer_after, "discord", "connected") is True
        )
        rx_dm_delta = None
        tx_dm_delta = None
        fast_reply_delta = None
        ack_miss_delta = None
        peer_counter_ok = peer_status_ok
    outbound_ok = bool(
        send_outbound
        and outbound_step
        and outbound_step.get("result", {}).get("ok") is True
        and outbound_packets
        and contains_token(outbound_packets.get("result"), outbound_token)
    )
    inbound_ok = bool(
        latest_messages
        and (
            new_exact_listener_reply(
                baseline_messages_result,
                messages_result,
                fingerprint,
            )
            if listener_mode
            else messages_have_inbound_token(
                messages_result, inbound_token, fingerprint
            )
        )
    )
    transaction_correlation = (
        correlated_listener_transaction(
            baseline_messages=baseline_messages_result,
            final_messages=messages_result,
            baseline_packets=baseline_packets_result,
            final_packets=packets_result,
            baseline_route=baseline_route_result,
            final_route=route_result,
            outbound_token=outbound_token,
            fingerprint=fingerprint,
        )
        if listener_mode
        else None
    )
    if listener_mode:
        ack_path_ok = bool(
            send_outbound
            and transaction_correlation
            and transaction_correlation.get("ack_path_ok") is True
        )
        direct_route_ok = bool(
            send_outbound
            and transaction_correlation
            and transaction_correlation.get("direct_route_ok") is True
        )
    else:
        ack_path_ok = bool(
            messages_have_acked_tx(
                messages_result, outbound_token, fingerprint
            )
            and packets_have_ack_or_path(packets_result)
        )
        direct_send_step = direct_step
        direct_route_ok = bool(
            send_outbound
            and direct_send_step
            and direct_send_step.get("result", {}).get("ok") is True
            and messages_have_tx_token(
                messages_result, direct_token, fingerprint
            )
            and route_has_direct_path(route_result, fingerprint)
        )
    controlled_peer_observed = (
        peer_status_ok
        if peer_status_requested
        else inbound_ok and ack_path_ok and direct_route_ok
    )
    checks = {
        "identity_public_key_matches": bool(
            identity_result.get("ok") is True
            and expected_identity
            and identity_fingerprint == expected_identity
        ),
        "controlled_peer_observed": controlled_peer_observed,
        "controlled_peer_status_connected": (
            peer_status_ok and peer_counter_ok
            if peer_status_requested
            else True
        ),
        "controlled_peer_contact_ready": (
            bool(
                listener_mode
                and peer_public_key is not None
                and import_command is not None
                and contact_import_ok(
                    contact_import_result,
                    peer_public_key,
                    fingerprint,
                )
                and contacts_has_exact_peer(
                    contact_after_result,
                    peer_public_key,
                    fingerprint,
                )
            )
            if listener_mode
            else True
        ),
        "outbound_dm": outbound_ok,
        "inbound_dm": inbound_ok,
        "ack_path": ack_path_ok,
        "direct_route": direct_route_ok,
        "health_ready": bool(
            health_result.get("ok") is True
            and health_result.get("board_ready") is True
            and health_result.get("ui_ready") is True
        ),
        "no_public_commands": not any(command_has_public_tx(command) for command in commands),
    }
    if expected_commit is not None:
        checks["exact_candidate"] = firmware_identity_matches(
            version_result, expected_commit
        )
    controlled_peer = {
        "fingerprint": fingerprint,
        "evidence_source": (
            "explicit_peer_status"
            if peer_status_requested
            else "d1l_bidirectional_rf"
        ),
        "port": peer_port,
        "status_path": str(peer_status_path) if peer_status_path is not None else None,
    }
    if listener_mode:
        controlled_peer["public_key"] = peer_public_key
    return {
        "schema": RF_FULL_ACCEPTANCE_SCHEMA,
        "mode": "rf-full-acceptance",
        "hardware_required": True,
        "physical_observed": True,
        "dry_run": False,
        "simulated": False,
        "simulation": False,
        "source_inspection": False,
        "execution_complete": True,
        "closure_eligible": True,
        "dm_rf_tx": bool(
            send_outbound
            and outbound_step
            and (listener_mode or direct_step)
        ),
        "public_rf_tx": False,
        "formats_sd": False,
        "port": port,
        "baud": baud,
        "controlled_peer": controlled_peer,
        "controlled_peer_adapter": (
            RADIO_LISTENER_PROFILE if listener_mode else "meshcorebot"
        ),
        "target_fingerprint": fingerprint,
        "d1l_public_key": public_key,
        "expected_identity_fingerprint": expected_identity,
        "identity_fingerprint": identity_fingerprint,
        "token": token,
        "expected_firmware_commit": expected_commit,
        "github_actions_run": (
            str(github_run_id) if github_run_id is not None else None
        ),
        "workflow_run_attempt": (
            str(workflow_run_attempt)
            if workflow_run_attempt is not None
            else None
        ),
        "device_build_commit": version_result.get("build_commit"),
        "device_idf_version": version_result.get("idf"),
        "device_release_profile": version_result.get("release_profile"),
        "device_sd_history_mode": version_result.get("sd_history_mode"),
        "firmware_identity_required": expected_commit is not None,
        "firmware_identity_ok": checks.get("exact_candidate"),
        "outbound_token": outbound_token,
        "inbound_token": (
            RADIO_LISTENER_REPLY if listener_mode else inbound_token
        ),
        "direct_token": direct_token,
        "discord_command": discord_command(public_key, inbound_token),
        "public_rf_transmit": False,
        "inbound_seen_at": inbound_seen_at,
        "controlled_peer_before": status_snapshot(peer_before),
        "controlled_peer_after": status_snapshot(peer_after),
        "controlled_peer_before_receipt": peer_before_receipt,
        "controlled_peer_after_receipt": peer_after_receipt,
        "controlled_peer_counter_deltas": {
            "rx_dm_total": rx_dm_delta,
            "tx_dm_total": tx_dm_delta,
            "local_fast_reply_total": fast_reply_delta,
            "tx_dm_ack_miss_total": ack_miss_delta,
        },
        "transaction_correlation": transaction_correlation,
        "controlled_peer_contact_setup": {
            "name": RADIO_LISTENER_CONTACT_NAME,
            "public_key": peer_public_key,
            "fingerprint": fingerprint,
            "import_command": import_command,
            "before": contact_before_result,
            "import_result": contact_import_result,
            "after": contact_after_result,
        },
        "checks": checks,
        "steps": steps,
        "ok": all(checks.values()),
    }


def command_can_retry(command: str) -> bool:
    return not command.strip().lower().startswith("mesh send ")


def send_acceptance_command(ser, command: str, timeout: float, retries: int = 1) -> dict:
    result = send_console_command(ser, command, timeout)
    for _ in range(max(0, retries)):
        if result.get("code") != "TIMEOUT" or not command_can_retry(command):
            break
        time.sleep(0.2)
        ser.reset_input_buffer()
        result = send_console_command(ser, command, timeout)
    return result


def run_hardware(
    *,
    port: str,
    baud: int,
    timeout: float,
    wait_sec: float,
    poll_sec: float,
    peer_status_path: Path | None,
    peer_port: str | None,
    fingerprint: str,
    public_key: str,
    token: str,
    send_outbound: bool,
    expected_commit: str,
    github_run_id: str,
    workflow_run_attempt: str,
    peer_capture_dir: Path | None = None,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    root = Path(__file__).resolve().parents[1]
    source_git = git_metadata(root)
    outbound_token = f"{token}_out"
    inbound_token = f"{token}_in"
    direct_token = f"{token}_direct"
    normalized_commit = exact_commit(expected_commit)
    if normalized_commit is None:
        raise ValueError("expected_commit must be an exact 40-character hexadecimal SHA")
    if (
        exact_public_key(public_key) is None
        or re.fullmatch(r"[0-9A-F]{16}", str(fingerprint).upper())
        is None
    ):
        raise ValueError(
            "RF acceptance requires exact D1L public key and peer fingerprint"
        )
    if (
        not str(github_run_id).isdigit()
        or int(github_run_id) < 1
        or not str(workflow_run_attempt).isdigit()
        or int(workflow_run_attempt) < 1
    ):
        raise ValueError(
            "GitHub run id and run attempt must be positive integers"
        )
    if not (
        exact_commit(source_git.get("commit")) == normalized_commit
        and source_git.get("dirty") is False
        and source_git.get("dirty_entries") == []
    ):
        raise ValueError(
            "RF acceptance must run from the exact clean candidate source"
        )
    port, peer_port = enforce_port_policy(port, peer_port)
    if peer_status_path is None or peer_port is None:
        raise ValueError(
            "hardware RF acceptance requires an explicitly assigned "
            "controlled-peer status path and port"
        )
    if (
        peer_port == RADIO_LISTENER_PORT
        and peer_status_path.resolve()
        != RADIO_LISTENER_STATUS_PATH.resolve()
    ):
        raise ValueError(
            "COM15 acceptance requires the exact OpenClaw radio listener "
            f"status path {RADIO_LISTENER_STATUS_PATH}"
        )
    steps: list[dict] = []
    peer_before = None
    peer_before_receipt = None
    peer_after_receipt = None
    inbound_seen_at = None
    capture_dir = (
        peer_capture_dir
        or root / "artifacts" / "hardware" / "com12" / "rf-peer"
    )
    safe_token = re.sub(r"[^A-Za-z0-9_.-]", "_", token)
    before_capture = capture_dir / f"{safe_token}_peer_before.json"
    after_capture = capture_dir / f"{safe_token}_peer_after.json"

    def run_command(ser, command: str, command_timeout: float | None = None) -> dict:
        result = send_acceptance_command(ser, command, command_timeout or timeout)
        steps.append({"command": command, "result": result})
        return result

    with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        version = run_command(ser, "version")
        if not (
            firmware_identity_matches(version, normalized_commit)
            and version.get("idf") == "v5.5.4"
            and version.get("release_profile") == "core_1_0"
            and version.get("sd_history_mode") == "disabled"
        ):
            return {
                "schema": RF_FULL_ACCEPTANCE_SCHEMA,
                "mode": "rf-full-acceptance",
                "hardware_required": True,
                "physical_observed": True,
                "dry_run": False,
                "simulated": False,
                "simulation": False,
                "source_inspection": False,
                "execution_complete": False,
                "closure_eligible": False,
                "dm_rf_tx": False,
                "public_rf_tx": False,
                "formats_sd": False,
                "port": port,
                "baud": baud,
                "expected_firmware_commit": normalized_commit,
                "github_actions_run": str(github_run_id),
                "workflow_run_attempt": str(workflow_run_attempt),
                "device_build_commit": version.get("build_commit"),
                "device_release_profile": version.get("release_profile"),
                "device_sd_history_mode": version.get("sd_history_mode"),
                "firmware_identity_required": True,
                "firmware_identity_ok": False,
                "controlled_peer": {
                    "fingerprint": fingerprint,
                    "evidence_source": "explicit_peer_status",
                    "port": peer_port,
                    "status_path": (
                        str(peer_status_path)
                        if peer_status_path is not None
                        else None
                    ),
                },
                "checks": {
                    "exact_candidate": False,
                    "no_public_commands": True,
                },
                "steps": steps,
                "ok": False,
            }
        peer_before, peer_before_receipt = capture_peer_status(
            peer_status_path,
            before_capture,
            root,
        )
        listener_mode = peer_port == RADIO_LISTENER_PORT
        if listener_mode and not radio_listener_connected(
            peer_before, peer_port, fingerprint
        ):
            raise ValueError(
                "COM15 OpenClaw listener status/public-key identity is not ready"
            )
        run_command(ser, "identity status")
        run_command(ser, "contacts")
        if listener_mode:
            peer_public_key = exact_public_key(
                get_path(peer_before, "serial", "public_key")
            )
            if peer_public_key is None:
                raise ValueError(
                    "COM15 listener status has no exact 64-hex public key"
                )
            import_result = run_command(
                ser, contact_import_command(peer_public_key)
            )
            contacts_after = run_command(ser, "contacts")
            if not (
                contact_import_ok(
                    import_result, peer_public_key, fingerprint
                )
                and contacts_has_exact_peer(
                    contacts_after, peer_public_key, fingerprint
                )
            ):
                raise ValueError(
                    "Controlled COM15 peer contact import/verification failed"
                )
        baseline_messages = run_command(
            ser, f"messages dm {fingerprint}"
        )
        run_command(ser, "packets")
        run_command(ser, f"routes trace {fingerprint}")
        if send_outbound:
            outbound_text = (
                f"core acceptance test {outbound_token}"
                if listener_mode
                else outbound_token
            )
            run_command(
                ser,
                f"mesh send dm {fingerprint} {outbound_text}",
                max(timeout, 8.0),
            )
            time.sleep(2.0)
            run_command(ser, f"packets search {outbound_token}")
        deadline = time.time() + wait_sec
        while time.time() < deadline:
            messages = run_command(ser, f"messages dm {fingerprint}")
            inbound_found = (
                new_exact_listener_reply(
                    baseline_messages, messages, fingerprint
                )
                if listener_mode
                else messages_have_inbound_token(
                    messages, inbound_token, fingerprint
                )
            )
            if inbound_found:
                inbound_seen_at = datetime.now(timezone.utc).isoformat()
                break
            time.sleep(max(0.25, poll_sec))
        if send_outbound:
            run_command(ser, "packets")
            run_command(ser, f"routes trace {fingerprint}")
            if not listener_mode:
                run_command(
                    ser,
                    f"mesh send dm {fingerprint} {direct_token}",
                    max(timeout, 8.0),
                )
                time.sleep(2.0)
                run_command(ser, f"packets search {direct_token}")
            run_command(ser, f"messages dm {fingerprint}")
        run_command(ser, "packets")
        run_command(ser, f"routes trace {fingerprint}")
        run_command(ser, "health")

    if peer_port == RADIO_LISTENER_PORT:
        status_deadline = time.time() + max(10.0, wait_sec)
        while time.time() < status_deadline:
            candidate = read_json(peer_status_path)
            if (
                counter_delta(peer_before, candidate, "rx_dm_total") == 1
                and counter_delta(peer_before, candidate, "tx_dm_total") == 1
                and counter_delta(
                    peer_before, candidate, "local_fast_reply_total"
                )
                == 1
            ):
                break
            time.sleep(max(0.25, poll_sec))
    peer_after, peer_after_receipt = capture_peer_status(
        peer_status_path,
        after_capture,
        root,
    )
    return build_report(
        port=port,
        baud=baud,
        peer_status_path=peer_status_path,
        peer_port=peer_port,
        fingerprint=fingerprint,
        public_key=public_key,
        token=token,
        send_outbound=send_outbound,
        steps=steps,
        peer_before=peer_before,
        peer_after=peer_after,
        inbound_seen_at=inbound_seen_at,
        expected_commit=normalized_commit,
        peer_before_receipt=peer_before_receipt,
        peer_after_receipt=peer_after_receipt,
        github_run_id=str(github_run_id),
        workflow_run_attempt=str(workflow_run_attempt),
    )


def default_out_path(report: dict) -> Path:
    if report.get("mode") == "dry-run-rf-full-acceptance":
        return Path("artifacts") / "smoke" / f"d1l-rf-full-acceptance-dry-run-{utc_stamp()}.json"
    port = str(report.get("port") or "unknown").lower()
    token = str(report.get("token") or utc_stamp()).replace(":", "_")
    return Path("artifacts") / "hardware" / port / f"rf_full_acceptance_{token}.json"


def write_report(report: dict, out_path: Path | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    path = out_path or default_out_path(report)
    if not path.is_absolute():
        path = root / path
    stamp_report(report, root)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--wait-sec", type=float, default=90.0)
    parser.add_argument("--poll-sec", type=float, default=3.0)
    parser.add_argument("--fingerprint", default=os.environ.get("D1L_DM_TARGET", DEFAULT_TARGET_FINGERPRINT))
    parser.add_argument("--d1l-public-key", default=os.environ.get("D1L_PUBLIC_KEY", DEFAULT_D1L_PUBLIC_KEY))
    parser.add_argument(
        "--peer-status",
        "--bot-status",
        dest="peer_status",
        default=os.environ.get("MESH_PEER_STATUS_PATH"),
    )
    parser.add_argument(
        "--peer-port",
        "--bot-port",
        dest="peer_port",
        default=os.environ.get("MESH_PEER_PORT"),
    )
    parser.add_argument("--token", default=None)
    parser.add_argument("--commit", default=None)
    parser.add_argument("--github-run-id")
    parser.add_argument("--github-run-attempt")
    parser.add_argument("--skip-outbound", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    token = args.token or default_token(args.commit)
    if bool(args.peer_status) != bool(args.peer_port):
        parser.error("--peer-status and --peer-port must be supplied together")
    peer_status_path = Path(args.peer_status) if args.peer_status else None
    if args.port:
        try:
            port, peer_port = enforce_port_policy(args.port, args.peer_port)
        except ValueError as exc:
            parser.error(str(exc))
    elif args.dry_run:
        port = D1L_REQUIRED_PORT
        if args.peer_port:
            try:
                _, peer_port = enforce_port_policy(D1L_REQUIRED_PORT, args.peer_port)
            except ValueError as exc:
                parser.error(str(exc))
        else:
            peer_port = None
    else:
        parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
    normalized_commit = exact_commit(args.commit)
    if not args.dry_run and normalized_commit is None:
        parser.error(
            "--commit is required for hardware RF acceptance and must be an "
            "exact 40-character hexadecimal SHA"
        )
    if not args.dry_run and (
        not str(args.github_run_id or "").isdigit()
        or not str(args.github_run_attempt or "").isdigit()
        or int(args.github_run_attempt) < 1
    ):
        parser.error(
            "hardware RF acceptance requires positive --github-run-id "
            "and --github-run-attempt"
        )
    if not args.dry_run and (peer_status_path is None or peer_port is None):
        parser.error(
            "Hardware RF acceptance requires --peer-status and --peer-port for "
            "the explicitly assigned controlled peer"
        )
    if args.dry_run:
        report = dry_run_report(
            port=port,
            peer_status_path=peer_status_path,
            peer_port=peer_port,
            fingerprint=args.fingerprint,
            public_key=args.d1l_public_key,
            token=token,
            send_outbound=not args.skip_outbound,
            expected_commit=args.commit,
            github_run_id=args.github_run_id,
            workflow_run_attempt=args.github_run_attempt,
        )
    else:
        if peer_status_path is not None and not peer_status_path.exists():
            parser.error(f"Controlled-peer status file not found: {peer_status_path}")
        report = run_hardware(
            port=port,
            baud=args.baud,
            timeout=args.timeout,
            wait_sec=args.wait_sec,
            poll_sec=args.poll_sec,
            peer_status_path=peer_status_path,
            peer_port=peer_port,
            fingerprint=args.fingerprint,
            public_key=args.d1l_public_key,
            token=token,
            send_outbound=not args.skip_outbound,
            expected_commit=normalized_commit,
            github_run_id=str(args.github_run_id),
            workflow_run_attempt=str(args.github_run_attempt),
        )

    written = write_report(report, Path(args.out) if args.out else None)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}, indent=2))
    if report.get("mode") == "dry-run-rf-full-acceptance":
        print(f"Controlled-peer inbound command: {report['discord_command']}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
