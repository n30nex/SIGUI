from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_settings_model_defaults_and_nvs_contract():
    header = read("main/app/settings_model.h")
    source = read("main/app/settings_model.c")
    assert "D1L_SETTINGS_SCHEMA_VERSION 1U" in header
    assert 'D1L_SETTINGS_NAMESPACE "d1l_settings"' in source
    assert 'D1L_SETTINGS_KEY "settings"' in source
    assert 'snprintf(settings->node_name, sizeof(settings->node_name), "D1L Desk")' in source
    assert "settings->wifi_enabled = false" in source
    assert "settings->ble_companion_enabled = false" in source
    assert "settings->observer_enabled = false" in source
    assert "settings->path_hash_bytes = 1" in source
    assert "settings->frequency_hz = D1L_RADIO_FREQ_HZ" in source
    assert "settings->tcxo_mode = D1L_TCXO_NONE" in source


def test_console_exposes_phase2_foundation_commands():
    console = read("main/comms/usb_console.c")
    for command in [
        "settings get",
        "settings reset",
        "settings set name",
        "settings set pathhash",
        "identity status",
        "radio set freq",
        "radio set bw",
        "radio set sf",
        "radio set cr",
        "mesh advert flood",
        'strcmp(line, "health")',
    ]:
        assert command in console

    assert "arg_len >= D1L_NODE_NAME_LEN" in console
    assert "memcpy(settings.node_name, arg, arg_len + 1U)" in console
    assert "bool prompt_pending = true" in console
    assert "clearerr(stdin)" in console
    assert "vTaskDelay(pdMS_TO_TICKS(20))" in console
    assert 'printf("\\n");\n        handle_line(line);' in console
    assert '\\"expected\\":[\\"0x20\\",\\"0x48\\"]' in console


def test_smoke_includes_settings_identity_and_mesh_status():
    assert "settings get" in SMOKE_COMMANDS
    assert "identity status" in SMOKE_COMMANDS
    assert "mesh status" in SMOKE_COMMANDS
