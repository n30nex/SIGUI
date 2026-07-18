import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONSOLE = (ROOT / "main/comms/usb_console.c").read_text(encoding="utf-8")


def _rule_pattern(command: str, access: str) -> re.Pattern[str]:
    return re.compile(
        r"D1L_RELEASE_RULE_(?:EXACT|TOKEN)\(\s*"
        + re.escape(f'"{command}"')
        + rf",\s*D1L_RELEASE_COMMAND_{access},\s*"
        r"D1L_RELEASE_FEATURE_SD_HISTORY\)",
        re.DOTALL,
    )


def test_disabled_sd_mode_rules_cover_mutations_and_keep_probes_read_only():
    table = CONSOLE.split(
        "static const d1l_release_command_rule_t s_release_command_rules[] = {",
        1,
    )[1].split("};", 1)[0]

    read_only = (
        "storage status",
        "storage diag",
        "storage diag raw",
        "storage setup",
        "rp2040 status",
        "rp2040 ping",
        "rp2040 stock-probe",
    )
    mutations = (
        "storage mount",
        "storage remount",
        "storage reset-bridge",
        "storage filecanary",
        "storage export-canary",
        "storage export-diagnostics",
        "storage export-data",
        "storage force-nvs off",
        "rp2040 set-baud",
        "rp2040 baud-probe",
        "rp2040 bootloader",
        "rp2040 reset",
        "rp2040 double-reset",
    )

    for command in read_only:
        match = _rule_pattern(command, "READ_ONLY").search(table)
        assert match is not None, command
    for command in mutations:
        match = _rule_pattern(command, "MUTATION").search(table)
        assert match is not None, command

    assert max(table.index(f'"{command}"') for command in read_only) < min(
        table.index(f'"{command}"') for command in mutations
    )


def test_sd_policy_allows_qualification_unless_mode_is_disabled():
    availability = CONSOLE.split(
        "static bool release_command_feature_available(", 1
    )[1].split("static bool enforce_release_command_policy(", 1)[0]
    enforcement = CONSOLE.split(
        "static bool enforce_release_command_policy(", 1
    )[1].split("static void print_hex_bytes_json(", 1)[0]

    assert "feature == D1L_RELEASE_FEATURE_SD_HISTORY" in availability
    assert "d1l_release_sd_history_mode() !=" in availability
    assert "D1L_SD_HISTORY_MODE_DISABLED" in availability
    assert "D1L_SD_HISTORY_MODE_CONDITIONAL" not in availability
    assert "release_command_feature_available(rule->feature)" in enforcement


def test_sd_mutations_are_admitted_before_dispatch_and_core_help_is_nvs_only():
    handler = CONSOLE.split("static void handle_line(", 1)[1].split(
        "void d1l_usb_console_run(", 1
    )[0]
    assert handler.index("enforce_release_command_policy(command)") < handler.index(
        "const char *line = command->text"
    )

    core_help = CONSOLE.split(
        "if (d1l_release_profile_is_core()) {", 1
    )[1].split("return;", 1)[0]
    assert r"storage force-nvs [on]" in core_help
    assert r"storage force-nvs [on|off]" not in core_help
