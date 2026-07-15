import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def body(source: str, start: str, end: str) -> str:
    assert start in source, start
    assert end in source, end
    return source.split(start, 1)[1].split(end, 1)[0]


def test_map_sheets_have_one_bounded_persistent_owner():
    header = read("main/ui/ui_map.h")
    source = read("main/ui/ui_map.c")
    phase1 = read("main/ui/ui_phase1.c")

    assert "D1L_UI_MAP_SHEETS_CONTROLLER_MAX_BYTES 64U" in header
    assert "d1l_ui_map_sheets_controller_t" in header
    assert "_Static_assert(sizeof(d1l_ui_map_sheets_controller_t)" in source
    assert re.search(
        r"static d1l_ui_map_sheets_controller_t\s+\w+\s+EXT_RAM_BSS_ATTR;",
        phase1,
    )

    for former_owner in (
        "static lv_obj_t *s_map_location_sheet;",
        "static lv_obj_t *s_map_options_sheet;",
        "static lv_obj_t *s_map_lat_textarea;",
        "static lv_obj_t *s_map_lon_textarea;",
        "static lv_obj_t *s_map_location_keyboard;",
        "static d1l_ui_map_controls_t s_map_location_controls;",
        "static d1l_ui_map_options_page_t s_map_options_page",
    ):
        assert former_owner not in phase1


def test_map_sheet_create_replaces_both_sheets_and_rolls_back_partial_failure():
    source = read("main/ui/ui_map.c")
    delete_sheet = body(source, "static void map_delete_sheet", "static void map_destroy_sheets")
    destroy = body(source, "static void map_destroy_sheets", "static lv_obj_t *map_create_sheet")
    create = body(source, "bool d1l_ui_map_sheets_create", "static void map_invalidate_options")

    assert "lv_obj_is_valid(*sheet)" in delete_sheet
    assert delete_sheet.index("d1l_ui_modal_hide(*sheet);") < delete_sheet.index(
        "lv_obj_del(*sheet);"
    )
    assert "*sheet = NULL;" in delete_sheet

    assert "map_clear_location_controls(controller);" in destroy
    assert "map_delete_sheet(&controller->location_sheet);" in destroy
    assert "map_delete_sheet(&controller->options_sheet);" in destroy
    assert "memset(controller, 0, sizeof(*controller));" in destroy

    assert create.index("map_destroy_sheets(controller);") < create.index(
        "controller->location_sheet = map_create_sheet(parent);"
    )
    assert "!parent || !lv_obj_is_valid(parent)" in create
    assert create.count("map_destroy_sheets(controller);") >= 3
    assert create.index("controller->location_sheet = map_create_sheet(parent);") < create.index(
        "controller->options_sheet = map_create_sheet(parent);"
    )
    assert "controller->options_page = D1L_UI_MAP_OPTIONS_ROOT;" in create


def test_location_render_and_hide_detach_keyboard_and_fail_closed():
    source = read("main/ui/ui_map.c")
    clear = body(source, "static void map_clear_location_controls", "static void map_delete_sheet")
    invalidate = body(source, "static void map_invalidate_location", "bool d1l_ui_map_sheets_render_options")
    render = body(source, "bool d1l_ui_map_sheets_render_location", "void d1l_ui_map_sheets_hide_location")
    hide = body(source, "void d1l_ui_map_sheets_hide_location", "void d1l_ui_map_sheets_hide_options")

    assert "lv_obj_is_valid(keyboard)" in clear
    assert clear.index("d1l_ui_keyboard_clear_textarea(keyboard);") < clear.index(
        "memset(&controller->location_controls"
    )
    assert "lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);" in clear
    assert invalidate.index("map_clear_location_controls(controller);") < invalidate.index(
        "lv_obj_clean(controller->location_sheet);"
    )

    for control in (
        "latitude_textarea",
        "longitude_textarea",
        "location_keyboard",
    ):
        assert f"!controller->location_controls.{control}" in render
        assert f"!lv_obj_is_valid(controller->location_controls.{control})" in render
    assert render.index("map_invalidate_location(controller);") < render.index(
        "return false;"
    )
    assert "map_clear_location_controls(controller);" in hide
    assert "d1l_ui_modal_hide(controller->location_sheet);" in hide


def test_options_page_validation_and_hide_reset_fail_closed():
    source = read("main/ui/ui_map.c")
    valid = body(source, "static bool map_options_page_is_valid", "static void map_clear_location_controls")
    invalidate = body(source, "static void map_invalidate_options", "static void map_invalidate_location")
    render = body(source, "bool d1l_ui_map_sheets_render_options", "bool d1l_ui_map_sheets_render_location")
    hide = body(source, "void d1l_ui_map_sheets_hide_options", "void d1l_ui_map_sheets_deactivate")

    assert "page == D1L_UI_MAP_OPTIONS_ROOT" in valid
    assert "page == D1L_UI_MAP_OPTIONS_CACHE_STATUS" in valid
    assert "map_options_page_is_valid(page)" in render
    assert render.index("map_invalidate_options(controller);") < render.index(
        "return false;"
    )
    assert "d1l_ui_modal_hide(controller->options_sheet);" in invalidate
    assert "lv_obj_clean(controller->options_sheet);" in invalidate
    assert "controller->options_page = D1L_UI_MAP_OPTIONS_ROOT;" in invalidate
    assert "controller->options_page = page;" in render
    assert "controller->options_page = D1L_UI_MAP_OPTIONS_ROOT;" in hide


def test_map_sheet_getters_reject_invalid_lvgl_objects():
    source = read("main/ui/ui_map.c")
    valid_object = body(source, "static lv_obj_t *map_valid_object", "lv_obj_t *d1l_ui_map_location_sheet")

    assert "object && lv_obj_is_valid(object) ? object : NULL" in valid_object
    for getter in (
        "d1l_ui_map_location_sheet",
        "d1l_ui_map_options_sheet",
        "d1l_ui_map_latitude_textarea",
        "d1l_ui_map_longitude_textarea",
        "d1l_ui_map_location_keyboard",
    ):
        assert getter in source


def test_phase1_routes_map_sheet_lifecycle_through_controller():
    phase1 = read("main/ui/ui_phase1.c")

    for api in (
        "d1l_ui_map_sheets_create",
        "d1l_ui_map_sheets_render_options",
        "d1l_ui_map_sheets_render_location",
        "d1l_ui_map_sheets_hide_location",
        "d1l_ui_map_sheets_hide_options",
        "d1l_ui_map_location_sheet",
        "d1l_ui_map_options_sheet",
        "d1l_ui_map_latitude_textarea",
        "d1l_ui_map_longitude_textarea",
        "d1l_ui_map_location_keyboard",
    ):
        assert api in phase1
