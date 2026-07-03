from scripts import rp2040_direct_sd_file_canary as direct


def test_direct_canary_dry_run_is_non_destructive_and_port_explicit():
    report = direct.dry_run_report(port=None)

    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["hardware_required"] is False
    assert report["explicit_port_required"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["will_format"] is False
    assert report["manual_user_required"] is False
    assert any(
        command.startswith("DESKOS_SD_FILE v=1 id=4 op=write ")
        for command in report["commands"]
    )
    assert not any("format" in command.lower() for command in report["commands"])


def test_direct_canary_command_sequence_covers_file_ops():
    names = [name for name, *_ in direct.canary_commands()]
    requests = [request for _, request, *_ in direct.canary_commands()]

    assert names == [
        "ping",
        "status",
        "delete_root_missing",
        "delete_tmp",
        "delete_final",
        "write_tmp",
        "read_tmp",
        "rename_final",
        "stat_final",
        "read_final",
        "delete_final_after",
        "stat_deleted",
        "final_status",
    ]
    assert requests[0] == "DESKOS_SD_PING"
    assert requests[1] == "DESKOS_SD_STATUS"
    assert requests[-1] == "DESKOS_SD_STATUS"
    assert all(
        request.startswith("DESKOS_SD_FILE ") for request in requests[2:-1]
    )
    assert " crc=" in requests[5]
    assert " replace=1" in requests[7]


def test_direct_canary_step_acceptance_matches_protocol_tokens():
    payload = direct.PAYLOAD
    read_step = {
        "ok": True,
        "tokens": {
            "ok": "1",
            "data": direct.b64url(payload),
            "crc": direct.crc32_hex(payload),
        },
    }
    missing_delete = {"ok": True, "tokens": {"ok": "0", "err": "not_found"}}
    final_stat = {
        "ok": True,
        "tokens": {"ok": "1", "exists": "1", "size": str(len(payload))},
    }

    assert direct.step_accepted("read_tmp", read_step)
    assert direct.step_accepted("delete_tmp", missing_delete)
    assert direct.step_accepted("stat_final", final_stat)
