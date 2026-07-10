from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_home_and_top_bar_prioritize_retained_sd_degradation_over_ready_state():
    source = read("main/ui/ui_home.c")
    shell = read("main/ui/ui_phase1.c")
    state_body = function_body(
        source,
        "const char *d1l_ui_home_sd_state",
        "d1l_ui_home_box_t d1l_ui_home_destination_box",
    )

    degraded = "if (home_storage_needs_attention(snapshot))"
    ready = "if (snapshot->storage_data_enabled || snapshot->storage_sd_data_root_ready)"
    assert degraded in state_body
    assert state_body.index(degraded) < state_body.index(ready)
    assert 'return "Needs attention";' in state_body

    color_table = source.split("const uint32_t colors[]", 1)[1].split("};", 1)[0]
    assert "home_storage_needs_attention(snapshot) ? 0xF87171" in color_table
    assert color_table.index("home_storage_needs_attention") < color_table.index(
        "snapshot->storage_data_enabled ? 0x5EEAD4"
    )

    top_bar = function_body(shell, "static void update_chrome", "static lv_obj_t *render_metric_card")
    assert 'label_set_fmt(s_identity_label, "Wi-Fi %s  BLE %s  SD %s"' in top_bar
    assert "d1l_ui_home_sd_state(snapshot)" in top_bar


def test_home_and_top_bar_never_render_raw_storage_state_slugs():
    source = read("main/ui/ui_home.c")
    state_body = function_body(
        source,
        "const char *d1l_ui_home_sd_state",
        "d1l_ui_home_box_t d1l_ui_home_destination_box",
    )

    assert "return state;" not in state_body
    assert 'return "internal";' in state_body
    for raw_state in (
        "pending_bridge",
        "rp2040_unavailable",
        "unsupported",
    ):
        assert f'strcmp(state, "{raw_state}") == 0' in state_body


def test_more_sd_card_summary_uses_attention_status_and_warning_style_when_degraded():
    source = read("main/ui/ui_settings.c")
    render_body = source.split("void d1l_ui_settings_render", 1)[1]
    status = render_body.split("const char *storage_status", 1)[1].split(";", 1)[0]

    assert "storage_needs_attention" in status
    assert '"Needs attention"' in status
    assert "storage_ready" in status
    assert status.index("storage_needs_attention") < status.index("storage_ready")

    readiness = render_body.split("const bool storage_ready", 1)[1].split(";", 1)[0]
    assert "snapshot->storage_data_enabled" in readiness
    assert "snapshot->storage_sd_data_root_ready" in readiness

    storage_rows = render_body.split(
        "const settings_menu_item_t storage_maps[]", 1
    )[1].split("};", 1)[0]
    assert "storage_needs_attention ? 0xF87171" in storage_rows
    assert "storage_ready ? 0x5EEAD4" in storage_rows
    assert "D1L_UI_SETTINGS_ACTION_STORAGE, storage_needs_attention" in storage_rows

    category_summary = render_body.split(
        "const char *storage_category_summary", 1
    )[1].split(";", 1)[0]
    assert "storage_needs_attention" in category_summary
    assert '"SD needs attention"' in category_summary

    category_call = render_body.split(
        "render_category(s_menu, SETTINGS_CATEGORY_STORAGE_MAPS", 1
    )[1].split(");", 1)[0]
    assert "storage_category_summary" in category_call
    assert "storage_needs_attention" in category_call


def test_probe_and_reported_sd_errors_use_friendly_attention_summary_before_ready():
    home = read("main/ui/ui_home.c")
    settings = read("main/ui/ui_settings.c")
    raw_faults = (
        '"error"',
        '"bridge_reported"',
        '"inspect_rp2040_sd_cmd0_firmware_path"',
        '"inspect_rp2040_sd_mount_error_firmware_path"',
    )

    home_classifier = function_body(
        home,
        "static bool home_storage_needs_attention",
        "const char *d1l_ui_home_sd_state",
    )
    settings_classifier = function_body(
        settings,
        "static bool settings_storage_needs_attention",
        "static lv_obj_t *settings_create_label",
    )
    for raw_fault in raw_faults:
        assert raw_fault in home_classifier
        assert raw_fault in settings_classifier

    home_summary = function_body(
        home,
        "const char *d1l_ui_home_sd_state",
        "d1l_ui_home_box_t d1l_ui_home_destination_box",
    )
    assert home_summary.index("home_storage_needs_attention(snapshot)") < home_summary.index(
        "snapshot->storage_data_enabled || snapshot->storage_sd_data_root_ready"
    )
    assert 'return "Needs attention";' in home_summary

    settings_render = settings.split("void d1l_ui_settings_render", 1)[1]
    assert settings_render.index("settings_storage_needs_attention(snapshot)") < settings_render.index(
        "const bool storage_ready"
    )
    settings_readiness = settings_render.split("const bool storage_ready", 1)[1].split(";", 1)[0]
    assert "snapshot->storage_data_enabled" in settings_readiness
    assert "snapshot->storage_sd_data_root_ready" in settings_readiness
    assert 'storage_needs_attention\n        ? "Needs attention"' in settings_render

    for raw_fault in raw_faults:
        assert raw_fault not in home_summary
        assert raw_fault not in settings_render
