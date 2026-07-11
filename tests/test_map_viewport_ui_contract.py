from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_map_ui_uses_bounded_core_viewport_and_visible_attribution():
    source = read("main/ui/ui_map.c")
    header = read("main/ui/ui_map.h")
    math_header = read("main/map/map_math.h")

    assert '#include "map/map_view_service.h"' in source
    assert "D1L_MAP_VIEW_DEFAULT_ZOOM 10U" in math_header
    assert "D1L_MAP_VIEW_MIN_ZOOM 8U" in math_header
    assert "D1L_MAP_VIEW_MAX_ZOOM 14U" in math_header
    assert "s_viewport_zoom = D1L_MAP_VIEW_DEFAULT_ZOOM" in source
    assert "MAP_VIEWPORT_WIDTH 446U" in source
    assert "MAP_VIEWPORT_HEIGHT 288U" in source
    assert "d1l_map_view_service_acquire_visible" in source
    assert "d1l_map_view_service_acquire_frame" in source
    assert "d1l_map_view_service_release_frame" in source
    assert "d1l_map_view_service_release_visible" in source
    assert "lv_img_cache_invalidate_src(&s_viewport_image)" in source
    assert "lv_obj_invalidate(s_viewport_image_obj)" in source
    assert '"(c) OpenStreetMap contributors"' in source
    assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in source
    ensure_pixels = function_body(
        source, "static bool map_viewport_ensure_pixels", "static void map_viewport_apply_service_status"
    )
    assert ensure_pixels.count("heap_caps_malloc") == 1
    assert "heap_caps_realloc" not in ensure_pixels

    for api in (
        "d1l_ui_map_viewport_release",
        "d1l_ui_map_viewport_set_suppressed",
        "d1l_ui_map_viewport_suppress_next_acquire",
        "d1l_ui_map_viewport_refresh",
        "d1l_ui_map_viewport_lease_active",
        "d1l_ui_map_viewport_generation",
    ):
        assert api in header
        assert api in source


def test_map_viewport_touch_controls_pan_on_release_and_request_one_new_view():
    source = read("main/ui/ui_map.c")
    render = function_body(
        source, "void d1l_ui_map_render(", "static void map_render_options_root"
    )
    drag = function_body(
        source,
        "static void map_viewport_drag_event_cb",
        "static void map_viewport_zoom_in_event_cb",
    )
    pressing = drag.split("if (code == LV_EVENT_PRESSING", 1)[1].split(
        "if (code == LV_EVENT_PRESS_LOST", 1
    )[0]
    released = drag.split("if (code != LV_EVENT_RELEASED", 1)[1]
    zoom_in = function_body(
        source,
        "static void map_viewport_zoom_in_event_cb",
        "static void map_viewport_zoom_out_event_cb",
    )
    zoom_out = function_body(
        source,
        "static void map_viewport_zoom_out_event_cb",
        "static void map_viewport_recenter_event_cb",
    )
    recenter = function_body(
        source,
        "static void map_viewport_recenter_event_cb",
        "bool d1l_ui_map_viewport_refresh",
    )

    assert 'map_button(viewport, "Center", 12, 12, 92, 48' in render
    assert 'viewport, "-", 344, 12, 44, 48' in render
    assert 'viewport, "+", 392, 12, 44, 48' in render
    assert 'map_label(viewport, "Drag to pan"' in render
    assert "s_viewport_drag_hint_label = drag_hint" in render
    assert "lv_obj_add_flag(viewport, LV_OBJ_FLAG_CLICKABLE)" in render
    assert "lv_obj_clear_flag(viewport, LV_OBJ_FLAG_GESTURE_BUBBLE)" in render
    for event in (
        "LV_EVENT_PRESSED",
        "LV_EVENT_PRESSING",
        "LV_EVENT_RELEASED",
        "LV_EVENT_PRESS_LOST",
    ):
        assert f"map_viewport_drag_event_cb, {event}" in render

    assert "lv_indev_get_vect" in pressing
    assert "map_drag_clamp" in pressing
    assert "MAP_DRAG_MAX_X_PIXELS" in pressing
    assert "MAP_DRAG_MAX_Y_PIXELS" in pressing
    assert "portENTER_CRITICAL(&s_viewport_lease_lock)" in pressing
    assert "lv_obj_set_pos(s_viewport_image_obj" in pressing
    assert "map_viewport_request_interactive_view" not in pressing
    assert "MAP_DRAG_THRESHOLD_PIXELS" in released
    assert "d1l_map_math_pan_center" in released
    assert "map_viewport_request_interactive_view()" in released

    assert "D1L_MAP_VIEW_MAX_ZOOM" in zoom_in
    assert "++s_viewport_zoom" in zoom_in
    assert "map_viewport_request_interactive_view()" in zoom_in
    assert "D1L_MAP_VIEW_MIN_ZOOM" in zoom_out
    assert "--s_viewport_zoom" in zoom_out
    assert "map_viewport_request_interactive_view()" in zoom_out
    assert "s_viewport_lat_e7 = s_viewport_saved_lat_e7" in recenter
    assert "s_viewport_lon_e7 = s_viewport_saved_lon_e7" in recenter
    assert "map_viewport_request_interactive_view()" in recenter

    assert (
        "s_viewport_lat_e7, s_viewport_lon_e7, s_viewport_zoom" in render
    )
    status = function_body(
        source,
        "static void map_viewport_apply_service_status",
        "static void map_viewport_update_controls",
    )
    assert '"Downloading" : "Loading"' in status
    assert '"%s %u/%u"' in status
    assert 'lv_label_set_text(s_viewport_drag_hint_label, "Drag to pan")' in status
    refresh = function_body(
        source, "bool d1l_ui_map_viewport_refresh", "static lv_obj_t *map_menu_row"
    )
    assert "s_viewport_drag_active" in refresh


def test_ambient_mesh_updates_do_not_tear_down_an_active_map_view():
    source = read("main/ui/ui_phase1.c")
    map_input = function_body(
        source,
        "static d1l_ui_map_render_input_t map_render_input_from_snapshot",
        "static void remember_rendered_map_input",
    )
    refresh_timer = function_body(
        source, "static void refresh_timer_cb", "static void create_top_bar"
    )
    changed = refresh_timer.split(
        "if (content_generation_changed_from_rendered(&s_snapshot))", 1
    )[1]

    for field in (
        "snapshot->map_location_set",
        "snapshot->map_lat_e7",
        "snapshot->map_lon_e7",
        "snapshot->map_tile_zoom",
    ):
        assert field in map_input
    assert "snapshot->map_tile_cache_ready" not in map_input
    assert "snapshot->wifi_connected" not in map_input
    assert "d1l_ui_navigation_active() == D1L_UI_TAB_MAP" in refresh_timer
    assert "map_render_input_changed_from_rendered(&s_snapshot)" in refresh_timer
    assert refresh_timer.index("map_render_input_changed_from_rendered(&s_snapshot)") < (
        refresh_timer.index("content_generation_changed_from_rendered(&s_snapshot)")
    )
    assert "remember_rendered_content_generation(&s_snapshot);" in changed
    assert "request_content_refresh();" in changed
    assert changed.index("remember_rendered_content_generation(&s_snapshot);") < changed.index(
        "request_content_refresh();"
    )


def test_map_ui_releases_before_covers_and_automation_is_fail_closed():
    source = read("main/ui/ui_phase1.c")

    show_modal = function_body(source, "static void show_modal", "static void layout_content_for_active_tab")
    assert show_modal.index("d1l_ui_map_viewport_release();") < show_modal.index(
        "d1l_ui_modal_show(obj);"
    )

    render_active = function_body(source, "static void render_active_tab", "esp_err_t d1l_ui_phase1_request_tab")
    assert render_active.index("d1l_ui_map_viewport_release();") < render_active.index(
        "d1l_ui_screen_render("
    )

    tab_switch = function_body(source, "static void process_pending_tab_switch", "static void process_pending_content_refresh")
    assert "d1l_ui_map_viewport_release();" in tab_switch

    capture_begin = function_body(source, "esp_err_t d1l_ui_capture_begin", "esp_err_t d1l_ui_capture_chunk")
    assert "!s_capture_shadow_ready || s_capture_active" in capture_begin
    assert "d1l_ui_map_viewport_set_suppressed(true);" in capture_begin
    capture_end = function_body(source, "esp_err_t d1l_ui_capture_end", "static void request_tab_event_cb")
    assert "if (!s_capture_active)" in capture_end
    assert "d1l_ui_map_viewport_set_suppressed(false);" in capture_end

    request_tab = function_body(source, "esp_err_t d1l_ui_phase1_request_tab", "esp_err_t d1l_ui_phase1_scroll_probe")
    assert "set_map_interactive_touch_authorized(false);" in request_tab
    assert "d1l_ui_map_viewport_suppress_next_acquire();" in request_tab

    touch_tab = function_body(source, "static void request_tab_event_cb", "static void dock_event_cb")
    assert "set_map_interactive_touch_authorized(*tab == D1L_UI_TAB_MAP);" in touch_tab
    render_map = function_body(source, "static void render_map(", "static const char *packet_filter_direction")
    assert "!map_interactive_touch_authorized()" in render_map
    assert "d1l_ui_map_viewport_suppress_next_acquire();" in render_map

    home_action = function_body(source, "static void handle_home_action", "static void render_home_screen")
    home_map = home_action.split("case D1L_UI_HOME_ACTION_MAP:", 1)[1].split("break;", 1)[0]
    assert home_map.index("set_map_interactive_touch_authorized(true);") < home_map.index(
        "request_tab_switch(D1L_UI_TAB_MAP);"
    )
    close_options = function_body(
        source,
        "static void close_map_options_sheet_event_cb(lv_event_t *event)\n{",
        "static void open_map_cache_status_event_cb(lv_event_t *event)\n{",
    )
    assert close_options.index("set_map_interactive_touch_authorized(true);") < close_options.index(
        "request_tab_switch(D1L_UI_TAB_MAP);"
    )
    save_location = function_body(
        source,
        "static void map_location_save_event_cb(lv_event_t *event)\n{",
        "static void map_location_clear_event_cb(lv_event_t *event)\n{",
    )
    assert save_location.index("hide_map_location_sheet();") < save_location.index(
        "hide_map_options_sheet();"
    ) < save_location.index("set_map_interactive_touch_authorized(true);") < save_location.index(
        "request_tab_switch(D1L_UI_TAB_MAP);"
    )
    assert "request_content_refresh();" not in save_location
    assert "set_map_interactive_touch_authorized(false);" in request_tab


def test_removed_provider_and_offline_map_ui_cannot_reappear():
    ui_source = "\n".join(
        read(path)
        for path in (
            "main/ui/ui_map.c",
            "main/ui/ui_map.h",
            "main/ui/ui_phase1.c",
            "main/ui/ui_navigation.c",
            "main/ui/ui_settings.c",
            "main/ui/ui_keyboard.c",
            "main/ui/ui_home.c",
        )
    ).lower()
    for removed in (
        "offline maps",
        "tile source",
        "map_provider",
        "map_tiles_sheet",
        "map_tile_source",
        "map_remove_confirm",
        "save_map_tile_provider",
        "clear_map_tile_provider",
    ):
        assert removed not in ui_source

    for required in (
        "map options",
        "set location",
        "cache status",
        "wi-fi needed",
        "sd card needed",
        "loading map",
        "map unavailable",
    ):
        assert required in ui_source


def test_map_options_is_a_quiet_two_row_menu():
    source = read("main/ui/ui_map.c")
    options = function_body(
        source, "static void map_render_options_root", "static void map_render_cache_status"
    )

    assert 'map_menu_row(parent, 68, "Set location"' in options
    assert 'map_menu_row(parent, 140, "Cache status"' in options
    assert 'cache_ready ? "SD ready" : "Needs SD"' in options
    assert options.count("map_menu_row(") == 2
    assert 'map_panel(parent, 16, 280, 448, 80)' in options
    assert (
        '"Tiles download only while Map is open. Reopening the same area uses the saved copy."'
        in options
    )
    assert '"Built-in map"' not in options
    assert "Choose a location and review" not in options


def test_map_cache_status_is_read_only_and_reports_saved_work_honestly():
    source = read("main/ui/ui_map.c")
    saved_status = function_body(
        source, "static void map_saved_area_status", "void d1l_ui_map_render("
    )
    cache_page = function_body(
        source, "static void map_render_cache_status", "void d1l_ui_map_render_options"
    )

    assert "status->planned_tiles == 0U" in saved_status
    assert "status->worker_running" in saved_status
    assert '"Saving %u/%u"' in saved_status
    assert "!status->worker_running" in saved_status
    assert "status->rendered_tiles == status->planned_tiles" in saved_status
    assert "status->failed_tiles == 0U" in saved_status
    assert '"Local area saved"' in saved_status
    assert "status->rendered_tiles > 0U" in saved_status
    assert '"Partly saved"' in saved_status
    assert '"Open Map to save"' in saved_status

    assert "d1l_map_view_service_status(&view_status);" in cache_page
    assert "d1l_map_view_service_acquire_visible" not in cache_page
    assert "d1l_map_view_service_acquire_frame" not in cache_page
    assert '"OpenStreetMap is built in."' in cache_page
    assert '"(c) OpenStreetMap contributors"' in cache_page
    assert 'map_render_header(parent, "Cache status", "Read-only readiness", "Back"' in cache_page


def test_unsaved_map_location_uses_empty_fields_and_example_placeholders():
    map_source = read("main/ui/ui_map.c")
    phase_source = read("main/ui/ui_phase1.c")
    editor = function_body(
        map_source, "void d1l_ui_map_render_location", "void d1l_ui_map_viewport_release"
    )
    snapshot_copy = function_body(
        phase_source, "static void map_location_from_snapshot", "static bool parse_coord_text_e7"
    )

    assert 'char latitude[20] = "";' in editor
    assert 'char longitude[20] = "";' in editor
    assert "if (snapshot->map_location_set)" in editor
    assert 'map_configure_textarea(controls->latitude_textarea, "e.g. 43.6532000", latitude' in editor
    assert 'map_configure_textarea(controls->longitude_textarea, "e.g. -79.3832000", longitude' in editor
    assert "s_map_location_lat_e7 = 0;" in snapshot_copy
    assert "s_map_location_lon_e7 = 0;" in snapshot_copy
    assert "436532000L" not in phase_source
    assert "-793832000L" not in phase_source


def test_home_map_status_reports_setup_requirements_not_sd_as_saved_content():
    source = read("main/ui/ui_home.c")

    assert 'const char *map_status = "Ready to open";' in source
    assert 'map_status = "Set a location";' in source
    assert 'map_status = "Needs SD";' in source
    assert 'map_status = "Needs Wi-Fi";' in source
    assert '"Map cache ready"' not in source
    assert '"Map setup needed"' not in source
    assert source.index("if (!snapshot->map_location_set)") < source.index(
        "else if (!snapshot->map_tile_cache_ready)"
    ) < source.index("else if (!snapshot->wifi_connected)")
