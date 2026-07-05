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


def test_phase1_routes_sheet_visibility_through_modal_boundary():
    ui_source = read("main/ui/ui_phase1.c")

    assert '#include "ui_modal.h"' in ui_source
    assert "d1l_ui_modal_configure_scroll(sheet);" in ui_source
    assert "return d1l_ui_modal_visible(obj);" in ui_source

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
