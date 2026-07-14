import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def c_function(source: str, signature: str) -> str:
    start = source.rindex(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_app_snapshot_exposes_bounded_channel_metadata_and_selection():
    header = read("main/app/app_model.h")
    source = read("main/app/app_model.c")

    assert "D1L_APP_SNAPSHOT_CHANNEL_PREVIEW D1L_CHANNEL_STORE_CAPACITY" in header
    assert "d1l_channel_info_t channels[D1L_APP_SNAPSHOT_CHANNEL_PREVIEW]" in header
    assert "uint64_t active_channel_id" in header
    assert "uint32_t channel_store_revision" in header
    assert "d1l_app_model_copy_channels" in header
    assert "d1l_app_model_select_channel" in header
    assert "d1l_app_model_copy_channels(" in source

    copy_channels = c_function(
        source, "esp_err_t d1l_app_model_copy_channels("
    )
    assert "d1l_channel_store_snapshot(" in copy_channels
    assert "d1l_channel_store_copy(" not in copy_channels
    assert "d1l_channel_store_find_default(" not in copy_channels
    assert "d1l_channel_store_stats(" not in copy_channels

    select_channel = c_function(
        source, "esp_err_t d1l_app_model_select_channel("
    )
    assert "channel_id == 0U" in select_channel
    assert "d1l_channel_store_select(" in select_channel
    assert "d1l_channel_store_find(" not in select_channel
    assert "d1l_channel_store_update(" not in select_channel
    assert "d1l_channel_store_copy_protocol_key" not in select_channel
    assert "d1l_channel_store_export_share_uri" not in select_channel


def test_usb_channel_commands_are_redacted_persistent_and_non_rf():
    console = read("main/comms/usb_console.c")
    renderer = c_function(console, "static void print_channel_metadata_json(")
    list_command = c_function(console, "static void cmd_channels(")
    select_command = c_function(console, "static void cmd_channels_select(")

    for forbidden in (
        "history_key",
        "channel_hash",
        "meshcore_uri",
        "export_share_uri",
        "copy_protocol_key",
    ):
        assert forbidden not in renderer
        assert forbidden not in list_command
        assert forbidden not in select_command

    assert "d1l_app_model_copy_channels(" in list_command
    assert "D1L_CHANNEL_STORE_CAPACITY" in list_command
    assert '"secret_material_redacted\\\":true' in list_command
    assert '"public_rf_tx\\\":false' in list_command
    assert '"formats_sd\\\":false' in list_command
    assert "d1l_app_model_select_channel(channel_id, &selected)" in select_command
    assert '"persisted\\\":true' in select_command
    assert 'strcmp(line, "channels") == 0' in console
    assert 'strncmp(line, "channels select ", 16) == 0' in console
    assert "channels select <channel-id-hex>" in console


def test_channel_id_parser_rejects_ambiguous_or_zero_ids(tmp_path):
    console = read("main/comms/usb_console.c")
    parser = c_function(console, "static bool parse_channel_id_hex(")
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for channel-id vectors")

    harness = tmp_path / "channel_id_parser_test.c"
    executable = tmp_path / (
        "channel_id_parser_test.exe" if os.name == "nt" else "channel_id_parser_test"
    )
    harness.write_text(
        "#include <stdbool.h>\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#include <string.h>\n"
        + parser
        + "\nint main(void)\n"
        "{\n"
        "    uint64_t value = 0;\n"
        "    if (!parse_channel_id_hex(\"1\", &value) || value != 1) return 1;\n"
        "    if (!parse_channel_id_hex(\"0123456789aBcDeF\", &value) ||\n"
        "        value != UINT64_C(0x0123456789abcdef)) return 2;\n"
        "    if (parse_channel_id_hex(\"0\", &value)) return 3;\n"
        "    if (parse_channel_id_hex(\"\", &value)) return 4;\n"
        "    if (parse_channel_id_hex(\"10000000000000000\", &value)) return 5;\n"
        "    if (parse_channel_id_hex(\"0x1\", &value)) return 6;\n"
        "    if (parse_channel_id_hex(\"1 \", &value)) return 7;\n"
        "    if (parse_channel_id_hex(\"-1\", &value)) return 8;\n"
        "    if (parse_channel_id_hex(NULL, &value)) return 9;\n"
        "    if (parse_channel_id_hex(\"1\", NULL)) return 10;\n"
        "    return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    subprocess.run(
        [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(executable)],
        check=True,
        cwd=ROOT,
    )
    subprocess.run([str(executable)], check=True, cwd=ROOT)
