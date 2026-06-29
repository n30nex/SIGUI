from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_crash_log_is_bounded_and_nvs_backed():
    header = read("main/diagnostics/crash_log.h")
    source = read("main/diagnostics/crash_log.c")
    app_main = read("main/app_main.c")
    cmake = read("main/CMakeLists.txt")
    assert "D1L_CRASH_LOG_CAPACITY 8U" in header
    assert "d1l_crash_log_entry_t" in header
    assert "d1l_crash_log_clear" in header
    assert 'D1L_CRASH_LOG_NAMESPACE "d1l_crash"' in source
    assert 'D1L_CRASH_LOG_KEY "ring"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "append_boot_reset" in source
    assert "ESP_RST_TASK_WDT" in source
    assert "esp_err_t crash_log_ret = d1l_crash_log_init()" in app_main
    assert '"diagnostics/crash_log.c"' in cmake


def test_health_monitor_reports_stack_lvgl_and_largest_blocks():
    header = read("main/diagnostics/health_monitor.h")
    source = read("main/diagnostics/health_monitor.c")
    ui = read("main/ui/ui_phase1.c")
    console = read("main/comms/usb_console.c")
    assert "heap_largest_free" in header
    assert "psram_min_free" in header
    assert "current_task_stack_free_words" in header
    assert "ui_task_stack_free_words" in header
    assert "lvgl_used_pct" in header
    assert "heap_caps_get_largest_free_block" in source
    assert "uxTaskGetStackHighWaterMark(NULL)" in source
    assert "uxTaskGetStackHighWaterMark(s_ui_task)" in source
    assert "lv_mem_monitor" in source
    assert "d1l_health_monitor_register_ui_task" in ui
    assert "d1l_health_monitor_set_lvgl_ready(true)" in ui
    assert '\\"ui_task_stack_free_words\\":%lu' in console
    assert '\\"lvgl_used_pct\\":%u' in console


def test_console_exposes_crashlog_commands():
    console = read("main/comms/usb_console.c")
    assert 'ok_begin("crashlog")' in console
    assert 'ok_begin("crashlog clear")' in console
    assert "d1l_crash_log_copy_recent" in console
    assert "d1l_crash_log_clear()" in console
    assert 'strcmp(line, "crashlog")' in console
    assert 'strcmp(line, "crashlog clear")' in console
    assert "crashlog" in SMOKE_COMMANDS
