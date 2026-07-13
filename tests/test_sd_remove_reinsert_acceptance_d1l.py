import copy
import json

from scripts import sd_remove_reinsert_acceptance_d1l as accept


COMMIT = "e03b8a3d8f24acbf7f5f177ceebc1f3a85a93a81"


def storage_status(attempt: int, mode: str) -> dict:
    sd_mode = mode == "sd"
    backend = "sd" if sd_mode else "nvs"
    clean_store = {
        "sd_read_fail_count": 0,
        "sd_write_fail_count": 0,
        "sd_rename_fail_count": 0,
        "sd_last_error": "ESP_OK",
        "sd_degraded_latched": False,
        "nvs_mirror_fail_count": 0,
        "nvs_mirror_last_error": "ESP_OK",
    }
    return {
        "schema": 1,
        "ok": True,
        "cmd": "storage status",
        "manager": {
            "running": True,
            "state": "READY_SD" if sd_mode else "NO_CARD",
            "attempt": attempt,
        },
        "sd": {
            "state": "ready" if sd_mode else "no_card",
            "rp2040_bridge_ready": True,
            "rp2040_protocol_supported": True,
            "present": sd_mode,
            "presence_stale": False,
            "mounted": sd_mode,
            "data_root_ready": sd_mode,
            "file_ops": sd_mode,
            "atomic_rename": sd_mode,
            "status_stale": False,
        },
        "message_store_backend": backend,
        "dm_store_backend": backend,
        "route_store_backend": backend,
        "packet_log_backend": backend,
        "retained_nvs": {
            "marker_ready": True,
            "markers_complete": True,
            "anchor_ready": True,
            "sentinel_ready": True,
            "ready": True,
            "init_error": "ESP_OK",
            "migration_error": "ESP_OK",
        },
        "retained_sd": {
            "degraded": False,
            "backup_degraded": False,
            "stores": {name: dict(clean_store) for name in accept.STORE_NAMES},
        },
    }


def canary(token: str, mode: str, generation: int) -> dict:
    backend = "sd" if mode == "sd" else "nvs"
    return {
        "schema": 1,
        "ok": True,
        "cmd": "storage retained-canary",
        "token": token,
        "fingerprint": accept.fingerprint_for_token(token),
        "public_seq": generation * 10 + 1,
        "dm_seq": generation * 10 + 2,
        "route_seq": generation * 10 + 3,
        "packet_seq": generation * 10 + 4,
        "storage_manager_quiesced": True,
        "retained_worker_quiesced": True,
        "backend_mode": mode,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "formats_sd": False,
        "backends": {name: backend for name in accept.STORE_NAMES},
        "backend_generations": {name: generation for name in accept.STORE_NAMES},
    }


def readbacks(token: str, receipt: dict) -> dict[str, dict]:
    fingerprint = accept.fingerprint_for_token(token)
    text = f"sd-retained-canary {token}"
    return {
        f"messages public search {token}": {
            "ok": True,
            "cmd": "messages public",
            "filtered": True,
            "search": token,
            "count": 1,
            "page_count": 1,
            "total_matches": 1,
            "entries": [{
                "seq": receipt["public_seq"],
                "direction": "tx",
                "author": "SD Canary",
                "text": text,
                "delivered": True,
            }],
        },
        f"messages dm {fingerprint}": {
            "ok": True,
            "cmd": "messages dm",
            "filtered": True,
            "fingerprint": fingerprint,
            "count": 1,
            "page_count": 1,
            "total_matches": 1,
            "thread_count": 1,
            "entries": [{
                "seq": receipt["dm_seq"],
                "fingerprint": fingerprint,
                "alias": "SD Canary",
                "direction": "tx",
                "text": text,
                "delivered": True,
                "acked": True,
                "ack_hash": accept.retained_canary_hash(token),
            }],
        },
        f"routes trace {fingerprint}": {
            "ok": True,
            "cmd": "routes trace",
            "fingerprint": fingerprint,
            "route_count": 1,
            "entries": [{
                "seq": receipt["route_seq"],
                "target": fingerprint,
                "label": "SD Canary",
                "kind": "sd_canary",
                "route": "local",
                "direction": "tx",
                "payload_len": len(text),
            }],
        },
        f"packets search {token}": {
            "ok": True,
            "cmd": "packets search",
            "count": 1,
            "filter": {"direction": "any", "kind": "any", "search": token},
            "entries": [{
                "seq": receipt["packet_seq"],
                "direction": "tx",
                "kind": "sd_canary",
                "payload_len": len(text),
                "note": f"sd-canary {token}",
            }],
        },
    }


def persistence(token: str, mode: str, generation: int) -> dict[str, dict]:
    sd_required = mode == "sd"
    deferred_sd_sync = mode == "nvs_no_card"
    fingerprint = accept.fingerprint_for_token(token)
    message_persistence = {
        "loaded": True,
        "dirty": False,
        "failures": 0,
        "sd": {
            "required": sd_required,
            "generation": generation,
            "dirty": deferred_sd_sync,
            "reconcile_pending": deferred_sd_sync,
            "failures": 0,
            "last_error": "ESP_OK",
        },
        "nvs": {"dirty": False, "failures": 0, "last_error": "ESP_OK"},
    }
    return {
        f"messages public search {token}": {
            "ok": True,
            "cmd": "messages public",
            "persisted": True,
            "persistence": copy.deepcopy(message_persistence),
        },
        f"messages dm {fingerprint}": {
            "ok": True,
            "cmd": "messages dm",
            "persisted": True,
            "persistence": copy.deepcopy(message_persistence),
        },
        "routes": {
            "ok": True,
            "cmd": "routes",
            "persisted": True,
            "persistence": {
                "dirty": False,
                "fail_count": 0,
                "clear_failure_latched": False,
                "clear_fail_count": 0,
                "clear_last_error": "ESP_OK",
                "sd_primary": {
                    "required": sd_required,
                    "backend_generation": generation,
                    "dirty": deferred_sd_sync,
                    "reconcile_pending": deferred_sd_sync,
                    "fail_count": 0,
                    "last_error": "ESP_OK",
                },
                "nvs_fallback": {
                    "dirty": False,
                    "fail_count": 0,
                    "last_error": "ESP_OK",
                },
            },
        },
        f"packets search {token}": {
            "ok": True,
            "cmd": "packets search",
            "persisted": True,
            "persistence": {
                "loaded": True,
                "dirty": False,
                "failures": 0,
                "reconcile": {"pending": False, "failures": 0, "last_error": "ESP_OK"},
                "sd": {
                    "required": sd_required,
                    "generation": generation,
                    "dirty": False,
                    "failures": 0,
                    "last_error": "ESP_OK",
                },
                "nvs": {"dirty": False, "failures": 0, "last_error": "ESP_OK"},
                "journal": {"dirty": False, "failures": 0, "last_error": "ESP_OK"},
            },
        },
        "storage status": storage_status(generation, mode),
    }


def bundle(token: str, mode: str, generation: int) -> dict:
    receipt = canary(token, mode, generation)
    snapshot = persistence(token, mode, generation)
    return {
        "token": token,
        "mode": mode,
        "canary": receipt,
        "readbacks": readbacks(token, receipt),
        "persistence": snapshot,
        "persistence_attempts_used": 1,
        "generations": {name: generation for name in accept.STORE_NAMES},
        "ok": True,
    }


def post_canaries(token: str) -> dict:
    return {
        "file": {
            "ok": True,
            "cmd": "storage filecanary",
            "rename_replace": True,
            "read_final": True,
            "delete_final": True,
            "stat_deleted": True,
        },
        "map": {
            "ok": True,
            "cmd": "storage map-tile-canary",
            "token": token,
            "write_tmp": True,
            "read_tmp": True,
            "rename_replace": True,
            "stat_final": True,
            "read_final": True,
            "public_rf_tx": False,
            "formats_sd": False,
            "path": f"/deskos/map/tiles/z12/x1/y2-{token}.tile",
            "tmp_path": f"/deskos/map/tiles/z12/x1/y2-{token}.tmp",
        },
        "export": {
            "ok": True,
            "cmd": "storage export-canary",
            "token": token,
            "write_tmp": True,
            "read_tmp": True,
            "rename_replace": True,
            "stat_final": True,
            "read_final": True,
            "public_rf_tx": False,
            "formats_sd": False,
            "path": f"/deskos/exports/diagnostics/export-canary-{token}.json",
            "tmp_path": f"/deskos/exports/diagnostics/export-canary-{token}.tmp",
        },
    }


def add_event(events: list[dict], cycle: int, phase: str, command: str | None = None, action: str | None = None):
    if action:
        events.append({
            "sequence": len(events) + 1,
            "kind": "prompt",
            "cycle": cycle,
            "phase": phase,
            "action": action,
            "message": action,
        })
    else:
        events.append({
            "sequence": len(events) + 1,
            "kind": "command",
            "cycle": cycle,
            "phase": phase,
            "command": command,
            "result": {"ok": True, "cmd": command},
        })


def valid_report() -> dict:
    events: list[dict] = []
    add_event(events, 0, "initial_version", "version")
    add_event(events, 0, "initial_health", "health")
    add_event(events, 0, "initial_crashlog", "crashlog")
    initial_crashlog = {"ok": True, "cmd": "crashlog", "total_written": 4}
    cycles = []
    for index in range(1, accept.REQUIRED_CYCLES + 1):
        tokens = {
            phase: accept.cycle_token("rrtest", index, phase)
            for phase in ("before", "absent", "after")
        }
        before = bundle(tokens["before"], "sd", index * 10 + 1)
        absent = bundle(tokens["absent"], "nvs_no_card", index * 10 + 2)
        after = bundle(tokens["after"], "sd", index * 10 + 4)
        reconcile = persistence(tokens["absent"], "sd", index * 10 + 3)
        cycles.append({
            "cycle": index,
            "tokens": tokens,
            "baseline_ready_samples": [storage_status(index * 10 + 1, "sd"), storage_status(index * 10 + 2, "sd")],
            "before_remove": before,
            "no_card_samples": [storage_status(index * 10 + 3, "nvs_no_card"), storage_status(index * 10 + 4, "nvs_no_card")],
            "absent": absent,
            "ready_samples": [storage_status(index * 10 + 5, "sd"), storage_status(index * 10 + 6, "sd")],
            "prewrite": {
                "earlier_readbacks": {
                    "before": copy.deepcopy(before["readbacks"]),
                    "absent": copy.deepcopy(absent["readbacks"]),
                },
                "reconcile": reconcile,
                "reconcile_attempts_used": 1,
                "reconcile_generations": {name: index * 10 + 3 for name in accept.STORE_NAMES},
                "generation_changed": True,
                "gate_passed": True,
            },
            "after_reinsert": after,
            "post_canaries": post_canaries(tokens["after"]),
            "health": {"ok": True, "cmd": "health", "boot_nonce": 99},
            "crashlog": copy.deepcopy(initial_crashlog),
            "ok": True,
        })
        for phase, command, action in (
            ("before_remove_canary", f"storage retained-canary {tokens['before']}", None),
            ("prompt_remove", None, "remove"),
            ("no_card_poll", "storage status", None),
            ("absent_canary", f"storage retained-canary {tokens['absent']}", None),
            ("prompt_reinsert", None, "reinsert"),
            ("ready_poll", "storage status", None),
            ("prewrite_reconcile", "routes", None),
            ("after_reinsert_canary", f"storage retained-canary {tokens['after']}", None),
            ("post_file_canary", "storage filecanary", None),
            ("post_map_canary", f"storage map-tile-canary {tokens['after']}", None),
            ("post_export_canary", f"storage export-canary {tokens['after']}", None),
            ("post_health", "health", None),
            ("post_crashlog", "crashlog", None),
        ):
            add_event(events, index, phase, command, action)
    return {
        "schema": 1,
        "kind": accept.KIND,
        "mode": "hardware",
        "port": "COM12",
        "baud": 115200,
        "expected_firmware_commit": COMMIT,
        "version": {"ok": True, "cmd": "version", "build_commit": COMMIT},
        "firmware_identity": True,
        "strict_evidence": True,
        "cycles_required": accept.REQUIRED_CYCLES,
        "cycles_completed": accept.REQUIRED_CYCLES,
        "initial_boot_nonce": 99,
        "initial_crashlog": initial_crashlog,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "formats_sd": False,
        "events": events,
        "cycles": cycles,
        "git": {"commit": COMMIT, "dirty": False},
        "ok": True,
    }


def test_strict_dry_run_is_noninteractive_and_port_safe():
    report = accept.dry_run_report(COMMIT, "rrtest", strict_evidence=True)

    assert report["ok"] is True
    assert report["strict_evidence"] is True
    assert report["noninteractive"] is True
    assert report["waits_for_enter"] is False
    assert report["rp2040_usb_used"] is False
    assert report["uf2_volume_used"] is False
    text = json.dumps(report)
    assert "COM8" not in text and "COM11" not in text and "COM29" not in text


def test_firmware_contract_admits_only_explicit_no_card_nvs_and_emits_safety_fields():
    source = open("main/comms/usb_console.c", encoding="utf-8").read()
    body = source.split("static bool storage_retained_history_nvs_no_card_ready", 1)[1].split(
        "static bool retained_canary_backend_generations", 1
    )[0]
    for required in (
        'text_equals(status->manager_state, "NO_CARD")',
        'text_equals(status->sd_state, "no_card")',
        "status->rp2040_bridge_ready",
        "status->rp2040_sd_protocol_supported",
        "!status->sd_present",
        "!status->sd_presence_stale",
        "!status->sd_mounted",
        "!status->bridge_status_stale",
        'text_equals(status->message_store_backend, "nvs")',
        "d1l_retained_blob_store_nvs_markers_complete()",
        "d1l_retained_blob_store_nvs_anchor_ready()",
        "d1l_retained_blob_store_nvs_sentinel_ready()",
    ):
        assert required in body
    canary_body = source.split("static void cmd_storage_retained_canary", 1)[1].split(
        "static void cmd_storage_setup", 1
    )[0]
    assert '\\"backend_mode\\":\\"%s\\"' in canary_body
    assert '\\"dm_rf_tx\\":false' in canary_body
    assert '\\"backend_generations\\"' in canary_body


def test_explicit_no_card_rejects_stale_presence_and_bridge_loss():
    stale = storage_status(1, "nvs_no_card")
    stale["sd"]["presence_stale"] = True
    bridge_loss = storage_status(1, "nvs_no_card")
    bridge_loss["sd"]["rp2040_protocol_supported"] = False

    assert accept.storage_mode_matches(storage_status(1, "nvs_no_card"), "nvs_no_card")
    assert not accept.storage_mode_matches(stale, "nvs_no_card")
    assert not accept.storage_mode_matches(bridge_loss, "nvs_no_card")


def test_absent_persistence_requires_deferred_sd_sync_for_mirrored_stores():
    token = "rrtest-01-nvs"
    results = persistence(token, "nvs_no_card", 2)
    assert accept.persistence_snapshot_clean(results, token, "nvs_no_card")

    mirrored = (
        (f"messages public search {token}", "sd"),
        (f"messages dm {accept.fingerprint_for_token(token)}", "sd"),
        ("routes", "sd_primary"),
    )
    for command, backend in mirrored:
        for field in ("dirty", "reconcile_pending"):
            tampered = copy.deepcopy(results)
            tampered[command]["persistence"][backend][field] = False
            assert not accept.persistence_snapshot_clean(
                tampered, token, "nvs_no_card"
            )


def test_absent_persistence_keeps_packet_and_nvs_state_clean():
    token = "rrtest-01-nvs"
    packet_command = f"packets search {token}"
    public_command = f"messages public search {token}"

    packet_dirty = persistence(token, "nvs_no_card", 2)
    packet_dirty[packet_command]["persistence"]["sd"]["dirty"] = True
    assert not accept.persistence_snapshot_clean(
        packet_dirty, token, "nvs_no_card"
    )

    packet_pending = persistence(token, "nvs_no_card", 2)
    packet_pending[packet_command]["persistence"]["reconcile"]["pending"] = True
    assert not accept.persistence_snapshot_clean(
        packet_pending, token, "nvs_no_card"
    )

    for field, value in (("dirty", True), ("failures", 1)):
        nvs_bad = persistence(token, "nvs_no_card", 2)
        nvs_bad[public_command]["persistence"]["nvs"][field] = value
        assert not accept.persistence_snapshot_clean(
            nvs_bad, token, "nvs_no_card"
        )


def test_ready_sd_persistence_still_rejects_deferred_flags():
    token = "rrtest-01-post"
    public_command = f"messages public search {token}"
    for field in ("dirty", "reconcile_pending"):
        results = persistence(token, "sd", 3)
        results[public_command]["persistence"]["sd"][field] = True
        assert not accept.persistence_snapshot_clean(results, token, "sd")


def test_valid_ten_cycle_report_passes():
    assert accept.validate_report(valid_report())


def test_nine_cycles_fail_closed():
    report = valid_report()
    report["cycles"] = report["cycles"][:-1]
    report["cycles_completed"] = 9
    assert not accept.validate_report(report)


def test_duplicate_or_missing_absent_token_fails_closed():
    report = valid_report()
    report["cycles"][1]["tokens"]["absent"] = report["cycles"][0]["tokens"]["absent"]
    assert not accept.validate_report(report)


def test_bad_generation_delta_fails_closed():
    report = valid_report()
    cycle = report["cycles"][0]
    cycle["prewrite"]["reconcile_generations"] = copy.deepcopy(cycle["absent"]["generations"])
    assert not accept.validate_report(report)


def test_post_write_before_reconcile_fails_closed():
    report = valid_report()
    events = report["events"]
    reconcile = next(i for i, event in enumerate(events) if event.get("cycle") == 1 and event.get("phase") == "prewrite_reconcile")
    post = next(i for i, event in enumerate(events) if event.get("cycle") == 1 and event.get("phase") == "after_reinsert_canary")
    events[reconcile], events[post] = events[post], events[reconcile]
    for sequence, event in enumerate(events, 1):
        event["sequence"] = sequence
    assert not accept.validate_report(report)


def test_boot_nonce_change_and_unsafe_command_fail_closed():
    report = valid_report()
    report["cycles"][3]["health"]["boot_nonce"] = 100
    assert not accept.validate_report(report)

    report = valid_report()
    command_event = next(
        event for event in report["events"]
        if event.get("kind") == "command" and event.get("cycle") == 1
    )
    command_event["command"] = "mesh send public unsafe"
    assert not accept.validate_report(report)


def test_tampered_canary_or_timeout_event_fails_closed():
    report = valid_report()
    report["cycles"][0]["absent"]["canary"]["dm_rf_tx"] = True
    assert not accept.validate_report(report)

    report = valid_report()
    command_event = next(event for event in report["events"] if event.get("kind") == "command")
    command_event["result"] = {"ok": False, "cmd": command_event["command"], "code": "TIMEOUT"}
    assert not accept.validate_report(report)


def test_forged_identity_port_or_top_level_ok_fails_closed():
    report = valid_report()
    report["version"]["build_commit"] = "0" * 40
    assert not accept.validate_report(report)

    report = valid_report()
    report["port"] = "COM8"
    assert not accept.validate_report(report)

    report = valid_report()
    report["ok"] = False
    assert not accept.validate_report(report)


def test_partial_blocker_receipt_cannot_be_canonical_success():
    report = valid_report()
    report["cycles"] = report["cycles"][:3]
    report["cycles_completed"] = 3
    report["ok"] = False
    report["blocker"] = {
        "code": "state_timeout",
        "phase": "no_card_poll",
        "detail": "nvs_no_card",
        "cycle": 4,
        "last_event_sequence": 44,
    }
    assert not accept.validate_report(report)
