import json

from scripts import sd_reboot_remount_acceptance_d1l as remount_accept


class FakeSerial:
    def __init__(self, responses):
        self.responses = [line.encode("utf-8") for line in responses]
        self.writes = []
        self.reset_count = 0

    def write(self, data):
        self.writes.append(data.decode("utf-8"))

    def readline(self):
        return self.responses.pop(0) if self.responses else b""

    def reset_input_buffer(self):
        self.reset_count += 1

    def open(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


def ready_storage_line(manager_state: str = "READY_SD") -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        f'"manager":{{"running":true,"state":"{manager_state}"}},'
        '"sd":{"state":"ready","filesystem":"fat32","present":true,"mounted":true,'
        '"data_root_ready":true,"file_ops":true,"atomic_rename":true}}\n'
    )


def bridge_wait_storage_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"manager":{"running":true,"state":"BRIDGE_WAIT"},'
        '"sd":{"state":"rp2040_unavailable","present":false,"mounted":false,'
        '"data_root_ready":false,"file_ops":false,"atomic_rename":false}}\n'
    )


def mount_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage remount",'
        '"sd":{"state":"ready","filesystem":"fat32","present":true,"mounted":true},'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def busy_remount_line(manager_state: str = "PING") -> str:
    return (
        '{"schema":1,"ok":false,"cmd":"storage remount","code":"ESP_ERR_INVALID_STATE",'
        f'"manager":{{"running":true,"state":"{manager_state}"}},'
        '"sd":{"state":"ready","present":true,"mounted":true,"data_root_ready":true},'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def filecanary_line() -> str:
    return '{"schema":1,"ok":true,"cmd":"storage filecanary","public_rf_tx":false,"formats_sd":false}\n'


def retained_canary_line(token: str) -> str:
    fp = remount_accept.fingerprint_for_token(token)
    return f'{{"schema":1,"ok":true,"cmd":"storage retained-canary","token":"{token}","fingerprint":"{fp}"}}\n'


def map_tile_canary_line(token: str) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage map-tile-canary",'
        f'"token":"{token}","rename_replace":true,"read_final":true,'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def map_tile_check_line(token: str) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage map-tile-check",'
        f'"token":"{token}","stat_final":true,"read_final":true,'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def readback_lines(token: str) -> list[str]:
    fp = remount_accept.fingerprint_for_token(token)
    return [
        f'{{"schema":1,"ok":true,"cmd":"messages public","entries":[{{"text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages dm","entries":[{{"fingerprint":"{fp}","text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"routes trace","fingerprint":"{fp}","entries":[{{"target":"{fp}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"packets search","entries":[{{"note":"sd-canary {token}"}}]}}\n',
    ]


def health_line() -> str:
    return '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n'


def test_storage_ready_requires_fat32_filesystem():
    ready = json.loads(ready_storage_line())
    assert remount_accept.storage_ready(ready) is True
    ready["sd"]["filesystem"] = "exfat"
    assert remount_accept.storage_ready(ready) is False


def test_storage_ready_requires_manager_ready_sd_not_cached_sd_fields():
    cached_ready = json.loads(ready_storage_line("PING"))
    assert remount_accept.storage_ready(cached_ready) is False

    cached_ready["manager"]["state"] = "READY_SD"
    assert remount_accept.storage_ready(cached_ready) is True


def install_fake_serial(monkeypatch, serials):
    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())


def test_dry_run_is_serial_only_and_read_only_after_reboot():
    report = remount_accept.dry_run_report("remount1")

    assert report["ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert "storage map-tile-canary remount1" in report["commands"]
    assert "storage map-tile-check remount1" in report["commands"]
    assert "reboot" in report["commands"]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("setup confirm" in command for command in report["commands"])


def test_reboot_remount_requires_retained_readbacks_and_read_only_map_check(monkeypatch):
    token = "remount1"
    pre = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        filecanary_line(),
        retained_canary_line(token),
        map_tile_canary_line(token),
        *readback_lines(token),
    ]
    reboot = ['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true}\n']
    post = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        *readback_lines(token),
        map_tile_check_line(token),
        health_line(),
    ]
    pre_serial = FakeSerial(pre)
    reboot_serial = FakeSerial(reboot)
    post_serial = FakeSerial(post)
    install_fake_serial(monkeypatch, [pre_serial, reboot_serial, post_serial])
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["pre_remount_ready"] is True
    assert report["post_remount_ready"] is True
    assert report["retained_canary_passed"] is True
    assert report["pre_reboot_readbacks_ok"] is True
    assert report["post_reboot_readbacks_ok"] is True
    assert report["pre_map_tile_canary_passed"] is True
    assert report["post_map_tile_canary_passed"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert "storage remount\n" in pre_serial.writes
    assert "storage remount\n" in post_serial.writes
    assert "storage map-tile-check remount1\n" in post_serial.writes
    assert "storage map-tile-check remount1" in report["commands"]


def test_reboot_remount_polls_transient_bridge_wait_until_ready(monkeypatch):
    token = "remount1"
    pre = [
        bridge_wait_storage_line(),
        mount_line(),
        bridge_wait_storage_line(),
        ready_storage_line(),
        filecanary_line(),
        retained_canary_line(token),
        map_tile_canary_line(token),
        *readback_lines(token),
    ]
    reboot = ['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true}\n']
    post = [
        bridge_wait_storage_line(),
        mount_line(),
        bridge_wait_storage_line(),
        ready_storage_line(),
        *readback_lines(token),
        map_tile_check_line(token),
        health_line(),
    ]
    pre_serial = FakeSerial(pre)
    reboot_serial = FakeSerial(reboot)
    post_serial = FakeSerial(post)
    install_fake_serial(monkeypatch, [pre_serial, reboot_serial, post_serial])
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=2,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["pre_remount_ready"] is True
    assert report["post_remount_ready"] is True
    assert pre_serial.writes[:4] == [
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage status\n",
    ]
    assert post_serial.writes[:4] == [
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage status\n",
    ]


def test_mount_sequence_waits_after_busy_remount_even_when_cached_sd_is_ready(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_line("PING"),
            busy_remount_line("PING"),
            ready_storage_line("READY_SD"),
            ready_storage_line("READY_SD"),
        ]
    )
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    commands, results, storage_after = remount_accept.run_mount_sequence(
        ser,
        timeout=1.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert remount_accept.remount_manager_busy(results[1]) is True
    assert remount_accept.storage_ready(storage_after) is True
    assert commands == ["storage status", "storage remount", "storage status", "storage status"]
    assert ser.writes == [f"{command}\n" for command in commands]
