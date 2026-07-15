from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def body(source: str, start: str, end: str) -> str:
    assert start in source, start
    assert end in source, end
    return source.split(start, 1)[1].split(end, 1)[0]


def test_channel_sheets_are_a_bounded_isolated_controller():
    cmake = read("main/CMakeLists.txt")
    header = read("main/ui/ui_channel_sheets.h")
    source = read("main/ui/ui_channel_sheets.c")

    assert '"ui/ui_channel_sheets.c"' in cmake
    assert "D1L_UI_CHANNEL_SHEETS_BINDING_COUNT 19U" in header
    assert "D1L_UI_CHANNEL_SHEETS_CONTROLLER_MAX_BYTES 1024U" in header
    assert "_Static_assert(sizeof(d1l_ui_channel_sheets_controller_t)" in source
    for forbidden in (
        "d1l_app_model_",
        "d1l_channel_store_",
        "d1l_meshcore_",
        "usb_console",
        "printf(",
    ):
        assert forbidden not in source


def test_channel_actions_are_complete_synchronous_and_generation_guarded():
    header = read("main/ui/ui_channel_sheets.h")
    source = read("main/ui/ui_channel_sheets.c")

    for action in (
        "CANCEL_CREATE",
        "SUBMIT_CREATE",
        "CANCEL_IMPORT",
        "SUBMIT_IMPORT",
        "CLOSE_OPTIONS",
        "OPEN_EDIT",
        "CANCEL_EDIT",
        "SUBMIT_EDIT",
        "TOGGLE_ENABLED",
        "MAKE_DEFAULT",
        "OPEN_EXPORT",
        "CLOSE_EXPORT",
        "OPEN_REMOVE",
        "CANCEL_REMOVE",
        "CONFIRM_REMOVE",
    ):
        assert f"D1L_UI_CHANNEL_ACTION_{action}" in header
    assert "const d1l_channel_info_t *channel;" in header
    assert "const char *text;" in header
    assert "binding->generation == binding->controller->generation" in source
    assert "binding->generation != 0U" in source
    assert "controller->generation++" in source
    assert "controller->generation == 0U" in source
    assert "controller->action_handler(&event, controller->action_context);" in source
    assert source.index("controller->action_handler(&event") < source.index(
        "scrub_textarea(controller->import_textarea, true);"
    )


def test_import_and_export_secret_lifetimes_fail_closed():
    header = read("main/ui/ui_channel_sheets.h")
    source = read("main/ui/ui_channel_sheets.c")
    render_import = body(
        source,
        "bool d1l_ui_channel_sheets_render_import",
        "bool d1l_ui_channel_sheets_render_options",
    )
    render_export = body(
        source,
        "bool d1l_ui_channel_sheets_render_export",
        "bool d1l_ui_channel_sheets_render_remove",
    )

    assert "char export_uri[D1L_CHANNEL_SHARE_URI_LEN];" in header
    assert "volatile unsigned char *cursor" in source
    assert "secure_zero(controller->export_uri, sizeof(controller->export_uri));" in source
    assert "lv_textarea_set_password_mode(*out_textarea, password);" in source
    assert "D1L_CHANNEL_SHARE_URI_LEN - 1U, true" in render_import
    assert "lv_qrcode_update(qr, controller->export_uri" in render_export
    assert render_export.index("lv_qrcode_update(") < render_export.index(
        "d1l_ui_channel_sheets_clear_export_uri(controller);"
    )
    assert "create_label(sheet, controller->export_uri" not in source
    assert "lv_label_set_text" in source
    assert "export_uri)" not in body(
        source, "static lv_obj_t *create_label", "static lv_obj_t *create_button"
    )
    assert "d1l_ui_channel_sheets_clear_export_uri(controller);" in body(
        source, "void d1l_ui_channel_sheets_hide_export", "void d1l_ui_channel_sheets_hide_remove"
    ) or "sheet == controller->export_sheet" in source


def test_public_channel_is_protected_in_every_mutating_renderer():
    source = read("main/ui/ui_channel_sheets.c")
    options = body(
        source,
        "bool d1l_ui_channel_sheets_render_options",
        "bool d1l_ui_channel_sheets_render_edit",
    )
    edit = body(
        source,
        "bool d1l_ui_channel_sheets_render_edit",
        "bool d1l_ui_channel_sheets_render_export",
    )
    remove = body(
        source,
        "bool d1l_ui_channel_sheets_render_remove",
        "static void hide_sheet",
    )

    assert "channel->channel_id == D1L_CHANNEL_PUBLIC_ID" in options
    for label in (
        "Rename (protected)",
        "Public is always enabled",
        "Remove (protected)",
        "Public cannot be renamed, disabled, or removed.",
    ):
        assert f'"{label}"' in options
    assert "is_public ? D1L_UI_CHANNEL_ACTION_NONE" in options
    assert "const bool can_make_default = channel->enabled &&" in options
    assert '"Enable before making default"' in options
    assert "can_make_default ? D1L_UI_CHANNEL_ACTION_MAKE_DEFAULT" in options
    assert "controller->selected.channel_id == D1L_CHANNEL_PUBLIC_ID" in edit
    assert "controller->selected.channel_id == D1L_CHANNEL_PUBLIC_ID" in remove
    assert "invalidate_render(controller" in edit
    assert "invalidate_render(controller" in remove


def test_all_sheets_fail_closed_and_controls_enforce_touch_target():
    source = read("main/ui/ui_channel_sheets.c")

    assert "width < 44 || height < 44" in source
    for renderer, next_marker in (
        ("options", "bool d1l_ui_channel_sheets_render_edit"),
        ("export", "bool d1l_ui_channel_sheets_render_remove"),
        ("remove", "static void hide_sheet"),
    ):
        render = body(
            source,
            f"bool d1l_ui_channel_sheets_render_{renderer}",
            next_marker,
        )
        assert "finish_render(controller, sheet, complete)" in render
    assert "return finish_render(controller, sheet, complete);" in body(
        source, "static bool render_input_sheet", "bool d1l_ui_channel_sheets_render_create"
    )
    invalidate = body(source, "static void invalidate_render", "static bool finish_render")
    assert "deactivate_actions(controller);" in invalidate
    assert invalidate.index("d1l_ui_modal_hide(sheet);") < invalidate.index(
        "lv_obj_clean(sheet);"
    )


def test_phase1_integration_surface_has_all_required_sheet_getters():
    header = read("main/ui/ui_channel_sheets.h")
    for renderer in ("create", "import", "options", "edit", "export", "remove"):
        assert f"d1l_ui_channel_sheets_render_{renderer}(" in header
    for hider in ("create", "import", "options", "edit", "export", "remove"):
        assert f"d1l_ui_channel_sheets_hide_{hider}(" in header
    for getter in (
        "create_sheet",
        "create_textarea",
        "create_keyboard",
        "import_sheet",
        "import_textarea",
        "import_keyboard",
        "options",
        "edit",
        "edit_textarea",
        "edit_keyboard",
        "export",
        "remove",
    ):
        assert f"d1l_ui_channel_sheets_{getter}(" in header
