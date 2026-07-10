from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_navigation_module_is_registered_and_lvgl_free():
    cmake = read("main/CMakeLists.txt")
    nav_source = read("main/ui/ui_navigation.c")
    nav_header = read("main/ui/ui_navigation.h")

    assert '"ui/ui_navigation.c"' in cmake
    assert "ui_navigation.h" in nav_source
    assert "lvgl" not in nav_source.lower()
    assert "app_model" not in nav_source
    assert "typedef enum" in nav_header
    for symbol in [
        "d1l_ui_navigation_request",
        "d1l_ui_navigation_begin_pending",
        "d1l_ui_navigation_finish",
        "d1l_ui_navigation_active",
        "d1l_ui_navigation_pending",
        "d1l_ui_navigation_switch_pending",
    ]:
        assert symbol in nav_header
        assert symbol in nav_source


def test_navigation_module_owns_screen_names_and_surface_aliases():
    nav_source = read("main/ui/ui_navigation.c")
    ui_source = read("main/ui/ui_phase1.c")

    for canonical in ["home", "messages", "nodes", "map", "packets", "settings"]:
        assert f'return "{canonical}"' in nav_source

    for alias in [
        '"msg"',
        '"pkts"',
        '"set"',
        '"public_messages"',
        '"dm_thread"',
        '"wi_fi"',
        '"contact_detail"',
        '"contact_options"',
        '"contact_forget"',
        '"contact_route"',
        '"mesh_roles"',
        '"mesh_rooms"',
        '"mesh_repeaters"',
    ]:
        assert alias in nav_source

    surface_mapping = nav_source.split("bool d1l_ui_scroll_surface_from_name", 1)[1].split(
        "void d1l_ui_navigation_request", 1
    )[0]
    for surface in ["mesh_roles", "mesh_rooms", "mesh_repeaters"]:
        mapping = surface_mapping.split(f'strcmp(normalized, "{surface}")', 1)[1].split(
            "} else", 1
        )[0]
        assert f'surface = "{surface}";' in mapping
        assert "screen = D1L_UI_SCREEN_PACKETS;" in mapping

    assert "d1l_ui_screen_from_name(name, out_tab)" in ui_source
    assert "d1l_ui_scroll_surface_from_name(name, out_surface, out_surface_len, out_tab)" in ui_source
    screen_body = ui_source.split("static bool tab_from_name", 1)[1].split(
        "static bool scroll_surface_from_name", 1
    )[0]
    surface_body = ui_source.split("static bool scroll_surface_from_name", 1)[1].split(
        "static bool compose_probe_target_from_name", 1
    )[0]
    assert "strcmp(" not in screen_body
    assert "strcmp(" not in surface_body


def test_phase1_uses_navigation_boundary_for_switch_state():
    ui_source = read("main/ui/ui_phase1.c")

    assert "static portMUX_TYPE s_tab_lock" not in ui_source
    assert "static d1l_ui_tab_t s_active_tab" not in ui_source
    assert "static d1l_ui_tab_t s_pending_tab" not in ui_source
    assert "static bool s_tab_switch_pending" not in ui_source

    for symbol in [
        "d1l_ui_navigation_request(tab)",
        "d1l_ui_navigation_begin_pending(out_tab)",
        "d1l_ui_navigation_finish(rendered_tab)",
        "d1l_ui_navigation_active()",
        "d1l_ui_navigation_pending()",
        "d1l_ui_navigation_switch_pending()",
    ]:
        assert symbol in ui_source
