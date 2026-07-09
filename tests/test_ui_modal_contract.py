import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_modal_module_is_registered_and_owns_visibility_helpers():
    cmake = read("main/CMakeLists.txt")
    modal_source = read("main/ui/ui_modal.c")
    modal_header = read("main/ui/ui_modal.h")

    assert '"ui/ui_modal.c"' in cmake
    assert '#include "ui_modal.h"' in modal_source
    for symbol in [
        "d1l_ui_modal_reset",
        "d1l_ui_modal_hide",
        "d1l_ui_modal_show",
        "d1l_ui_modal_visible",
        "d1l_ui_modal_configure_scroll",
    ]:
        assert symbol in modal_header
        assert symbol in modal_source

    assert "lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)" in modal_source
    assert "lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN)" in modal_source
    assert "lv_obj_move_foreground(obj)" in modal_source
    assert "static lv_obj_t *s_active_modal" in modal_source
    assert "lv_obj_is_valid(s_active_modal)" in modal_source

    show_body = modal_source.split("void d1l_ui_modal_show", 1)[1].split(
        "bool d1l_ui_modal_visible", 1
    )[0]
    assert "s_active_modal && s_active_modal != obj" in show_body
    assert show_body.index("lv_obj_add_flag(s_active_modal, LV_OBJ_FLAG_HIDDEN)") < show_body.index(
        "lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN)"
    )
    assert show_body.index("lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN)") < show_body.index(
        "s_active_modal = obj"
    )

    hide_body = modal_source.split("void d1l_ui_modal_hide", 1)[1].split(
        "void d1l_ui_modal_show", 1
    )[0]
    assert "if (!obj)" in hide_body
    assert "if (s_active_modal == obj)" in hide_body
    assert "s_active_modal = NULL" in hide_body

    reset_body = modal_source.split("void d1l_ui_modal_reset", 1)[1].split(
        "void d1l_ui_modal_hide", 1
    )[0]
    assert "s_active_modal = NULL" in reset_body
    assert "Semaphore" not in modal_source
    assert "Mutex" not in modal_source


def test_phase1_routes_sheet_visibility_through_modal_boundary():
    ui_source = read("main/ui/ui_phase1.c")

    assert '#include "ui_modal.h"' in ui_source
    assert "d1l_ui_modal_configure_scroll(sheet);" in ui_source
    assert "return d1l_ui_modal_visible(obj);" in ui_source
    assert ui_source.index("d1l_ui_modal_reset();") < ui_source.index(
        's_screen = create_screen_object("root screen")'
    )

    assert not re.search(
        r"lv_obj_add_flag\(s_(?:[a-z0-9_]+_)?sheet, LV_OBJ_FLAG_HIDDEN\)",
        ui_source,
    )
    assert not re.search(
        r"lv_obj_clear_flag\(s_(?:[a-z0-9_]+_)?sheet, LV_OBJ_FLAG_HIDDEN\)",
        ui_source,
    )
    assert not re.search(
        r"lv_obj_move_foreground\(s_(?:[a-z0-9_]+_)?sheet\)",
        ui_source,
    )

    hide_calls = re.findall(r"d1l_ui_modal_hide\(s_[a-z0-9_]+_sheet\)", ui_source)
    show_calls = re.findall(r"d1l_ui_modal_show\(s_[a-z0-9_]+_sheet\)", ui_source)
    assert len(hide_calls) >= 20
    assert len(show_calls) >= 20


def test_exclusive_modal_parent_returns_and_onboarding_suppression_are_explicit():
    ui_source = read("main/ui/ui_phase1.c")

    public_close = ui_source.split("static void close_public_search_event_cb", 1)[1].split(
        "static void public_history_load_older_event_cb", 1
    )[0]
    assert "hide_public_search_sheet();" in public_close
    assert "show_public_history_sheet();" in public_close

    public_keyboard = ui_source.split("static void public_search_keyboard_event_cb", 1)[1].split(
        "static void open_public_search_event_cb", 1
    )[0]
    assert "close_public_search_event_cb(event);" in public_keyboard

    contact_close = ui_source.split("static void close_contact_export_event_cb", 1)[1].split(
        "static void render_contact_export_sheet", 1
    )[0]
    assert "hide_contact_export_sheet();" in contact_close
    assert "show_contact_detail_sheet();" in contact_close

    onboarding = ui_source.split("static void update_onboarding_visibility", 1)[1].split(
        "static void set_object_hidden", 1
    )[0]
    assert "if (s_onboarding_probe_suppressed)" in onboarding
    assert "s_onboarding_probe_suppressed && object_is_visible" not in onboarding

    compose_probe = ui_source.split("static void open_compose_probe_on_ui_task", 1)[1].split(
        "static void open_keyboard_probe_on_ui_task", 1
    )[0]
    compose_after_switch = compose_probe.split("process_pending_tab_switch();", 1)[1]
    assert "s_onboarding_probe_suppressed = true;" in compose_after_switch

    keyboard_probe = ui_source.split("static void open_keyboard_probe_on_ui_task", 1)[1].split(
        "static void run_compose_probe_on_ui_task", 1
    )[0]
    keyboard_after_switch = keyboard_probe.split("process_pending_tab_switch();", 1)[1]
    assert (
        "s_onboarding_probe_suppressed = !d1l_ui_keyboard_probe_target_is_onboarding(target);"
        in keyboard_after_switch
    )

    assert "lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN)" in ui_source
    assert "lv_obj_clear_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN)" in ui_source
