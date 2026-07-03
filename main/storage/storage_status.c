#include "storage_status.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/rp2040_bridge.h"
#include "storage/export_store.h"
#include "storage/map_tile_store.h"
#include "storage/retained_blob_store.h"

#define D1L_STORAGE_BOOT_POLL_INTERVAL_MS 150U
#define D1L_STORAGE_BOOT_POLL_TIMEOUT_MS 250U
#define D1L_STORAGE_BOOT_POLL_ATTEMPTS 10U
#define D1L_STORAGE_MANAGER_STACK_BYTES 4096U
#define D1L_STORAGE_MANAGER_IDLE_INTERVAL_MS 2000U
#define D1L_STORAGE_MANAGER_BACKOFF_MS 5000U
#define D1L_STORAGE_MANAGER_RESET_HOLD_MS 500U
#define D1L_STORAGE_MANAGER_RESET_SETTLE_MS 500U

typedef enum {
    D1L_STORAGE_MANAGER_BRIDGE_WAIT,
    D1L_STORAGE_MANAGER_PING,
    D1L_STORAGE_MANAGER_STATUS,
    D1L_STORAGE_MANAGER_MOUNT,
    D1L_STORAGE_MANAGER_READY_SD,
    D1L_STORAGE_MANAGER_READY_NVS,
    D1L_STORAGE_MANAGER_NEEDS_FAT32,
    D1L_STORAGE_MANAGER_NO_CARD,
    D1L_STORAGE_MANAGER_ERROR_BACKOFF,
} d1l_storage_manager_state_t;

static d1l_storage_status_t s_status;
static TaskHandle_t s_storage_manager_task;
static d1l_storage_manager_state_t s_manager_state = D1L_STORAGE_MANAGER_BRIDGE_WAIT;
static volatile bool s_manager_remount_requested;
static volatile bool s_manager_reset_bridge_requested;
static bool s_force_nvs;

static void refresh_retained_sd_health(d1l_storage_status_t *status)
{
    if (!status) {
        return;
    }
    (void)d1l_retained_blob_store_sd_stats(D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES,
                                           &status->retained_sd_stats[
                                               D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES]);
    (void)d1l_retained_blob_store_sd_stats(D1L_RETAINED_BLOB_STORE_DM_MESSAGES,
                                           &status->retained_sd_stats[
                                               D1L_RETAINED_BLOB_STORE_DM_MESSAGES]);
    (void)d1l_retained_blob_store_sd_stats(D1L_RETAINED_BLOB_STORE_ROUTES,
                                           &status->retained_sd_stats[
                                               D1L_RETAINED_BLOB_STORE_ROUTES]);
    (void)d1l_retained_blob_store_sd_stats(D1L_RETAINED_BLOB_STORE_PACKET_LOG,
                                           &status->retained_sd_stats[
                                               D1L_RETAINED_BLOB_STORE_PACKET_LOG]);
    status->retained_sd_degraded = d1l_retained_blob_store_any_sd_degraded();
    if (status->retained_sd_degraded) {
        status->note = D1L_RETAINED_BLOB_STORE_SD_DEGRADED_NOTE;
    }
}

static const char *storage_manager_state_name(d1l_storage_manager_state_t state)
{
    switch (state) {
    case D1L_STORAGE_MANAGER_BRIDGE_WAIT:
        return "BRIDGE_WAIT";
    case D1L_STORAGE_MANAGER_PING:
        return "PING";
    case D1L_STORAGE_MANAGER_STATUS:
        return "STATUS";
    case D1L_STORAGE_MANAGER_MOUNT:
        return "MOUNT";
    case D1L_STORAGE_MANAGER_READY_SD:
        return "READY_SD";
    case D1L_STORAGE_MANAGER_READY_NVS:
        return "READY_NVS";
    case D1L_STORAGE_MANAGER_NEEDS_FAT32:
        return "NEEDS_FAT32";
    case D1L_STORAGE_MANAGER_NO_CARD:
        return "NO_CARD";
    case D1L_STORAGE_MANAGER_ERROR_BACKOFF:
        return "ERROR_BACKOFF";
    default:
        return "ERROR_BACKOFF";
    }
}

static void set_manager_state(d1l_storage_manager_state_t state)
{
    s_manager_state = state;
    s_status.manager_state = storage_manager_state_name(state);
}

static void set_store_backends(d1l_storage_status_t *status)
{
    const bool public_messages_on_sd =
        d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES);
    const bool dm_messages_on_sd =
        d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_DM_MESSAGES);
    const bool routes_on_sd =
        d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_ROUTES);
    const bool packet_log_on_sd =
        d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG);
    const bool any_retained_sd = public_messages_on_sd ||
                                 dm_messages_on_sd ||
                                 routes_on_sd ||
                                 packet_log_on_sd;
    status->data_backend = any_retained_sd ? "mixed" : "nvs";
    status->message_store_backend =
        d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES);
    status->dm_store_backend =
        d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_DM_MESSAGES);
    status->packet_log_backend =
        d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PACKET_LOG);
    status->route_store_backend =
        d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_ROUTES);
    status->map_tile_backend = d1l_map_tile_store_sd_ready(status) ?
        "sd_map_tiles_ready" : "unavailable";
    status->export_backend = d1l_export_store_sd_ready(status) ?
        "sd_diagnostic_exports_ready" : "serial";
    status->data_enabled = any_retained_sd;
    refresh_retained_sd_health(status);
}

static void set_default_actions(d1l_storage_status_t *status)
{
    status->setup_action = "not_available";
    status->sd_filesystem = "unknown";
    status->file_ops_supported = false;
    status->atomic_rename_supported = false;
    status->file_line_max = 0;
    status->file_chunk_max = 0;
    status->path_max = 0;
}

static bool storage_sd_ready_for_files(void)
{
    return s_status.rp2040_sd_protocol_supported &&
           s_status.sd_present &&
           s_status.sd_mounted &&
           s_status.sd_data_root_ready &&
           !s_status.sd_needs_fat32 &&
           s_status.file_ops_supported &&
           s_status.atomic_rename_supported;
}

static void apply_force_nvs_status(void)
{
    d1l_retained_blob_store_note_sd_backend(false, false, false, 0, 0, 0);
    set_store_backends(&s_status);
    s_status.force_nvs = true;
    s_status.data_enabled = false;
    s_status.setup_action = "forced_nvs";
    s_status.note = "SD data storage is forced to onboard NVS until storage force-nvs off or storage remount is requested";
    set_manager_state(D1L_STORAGE_MANAGER_READY_NVS);
}

static void clear_sd_runtime_fields(d1l_storage_status_t *status)
{
    status->rp2040_sd_protocol_supported = false;
    status->sd_present = false;
    status->sd_mounted = false;
    status->sd_data_root_ready = false;
    status->sd_needs_fat32 = false;
    status->setup_required = false;
    status->setup_supported = false;
    status->file_ops_supported = false;
    status->atomic_rename_supported = false;
    status->response_truncated = false;
    status->capacity_kb = 0;
    status->free_kb = 0;
    status->file_line_max = 0;
    status->file_chunk_max = 0;
    status->path_max = 0;
    status->sd_probe_error = 0;
    status->sd_probe_data = 0;
    status->sd_mount_error = 0;
    status->sd_mount_data = 0;
    status->sd_filesystem = "unknown";
    status->sd_probe_power[0] = '\0';
    status->sd_probe_mode[0] = '\0';
}

static const char *stable_sd_state(const char *state)
{
    if (!state || state[0] == '\0') {
        return "unknown";
    }
    if (strcmp(state, "ready") == 0) {
        return "ready";
    }
    if (strcmp(state, "no_card") == 0) {
        return "no_card";
    }
    if (strcmp(state, "setup_required") == 0) {
        return "not_fat32_or_unmountable";
    }
    if (strcmp(state, "unformatted") == 0) {
        return "not_fat32_or_unmountable";
    }
    if (strcmp(state, "not_fat32_or_unmountable") == 0) {
        return "not_fat32_or_unmountable";
    }
    if (strcmp(state, "deskos_manifest_invalid") == 0) {
        return "deskos_manifest_invalid";
    }
    if (strcmp(state, "creating_deskos_files") == 0) {
        return "creating_deskos_files";
    }
    if (strcmp(state, "fat32_ready") == 0) {
        return "fat32_ready";
    }
    if (strcmp(state, "checking") == 0) {
        return "checking";
    }
    if (strcmp(state, "error") == 0) {
        return "error";
    }
    if (strcmp(state, "mount_required") == 0) {
        return "mount_required";
    }
    if (strcmp(state, "mount_pending") == 0) {
        return "mount_pending";
    }
    if (strcmp(state, "protocol_pending") == 0) {
        return "protocol_pending";
    }
    if (strcmp(state, "bridge_unavailable") == 0) {
        return "bridge_unavailable";
    }
    return "bridge_reported";
}

static const char *stable_filesystem(const char *filesystem)
{
    if (!filesystem || filesystem[0] == '\0') {
        return "unknown";
    }
    if (strcmp(filesystem, "fat32") == 0) {
        return "fat32";
    }
    if (strcmp(filesystem, "exfat") == 0) {
        return "exfat";
    }
    if (strcmp(filesystem, "fatfs") == 0) {
        return "fatfs";
    }
    return "reported";
}

static void apply_rp2040_sd_status(const d1l_rp2040_sd_status_t *sd)
{
    const bool mount_failed_with_diag = sd->card_present &&
                                        !sd->filesystem_mounted &&
                                        (sd->mount_error != 0U || sd->mount_data != 0U);
    const bool probe_rejected_card =
        strcmp(sd->note, "sd_probe_rejected_card") == 0 ||
        ((strcmp(sd->state, "error") == 0 || !sd->card_present) &&
         (sd->probe_error == 3U || sd->probe_error == 4U));

    s_status.rp2040_bridge_ready = sd->bridge_ready;
    s_status.rp2040_sd_protocol_supported = sd->protocol_supported;
    s_status.sd_present = sd->card_present;
    s_status.sd_mounted = sd->filesystem_mounted;
    s_status.sd_data_root_ready = sd->deskos_root_ready;
    s_status.sd_needs_fat32 = !probe_rejected_card &&
                              (sd->needs_fat32 ||
                               (sd->card_present && !sd->filesystem_mounted));
    s_status.setup_required = sd->card_present && (!sd->filesystem_mounted ||
                                                    !sd->deskos_root_ready ||
                                                    s_status.sd_needs_fat32);
    s_status.setup_supported = sd->protocol_supported && sd->card_present;
    s_status.file_ops_supported = sd->file_ops_supported;
    s_status.atomic_rename_supported = sd->atomic_rename_supported;
    s_status.response_truncated = sd->response_truncated;
    s_status.capacity_kb = sd->capacity_kb;
    s_status.free_kb = sd->free_kb;
    s_status.file_line_max = sd->file_line_max;
    s_status.file_chunk_max = sd->file_chunk_max;
    s_status.path_max = sd->path_max;
    s_status.sd_probe_error = sd->probe_error;
    s_status.sd_probe_data = sd->probe_data;
    s_status.sd_mount_error = sd->mount_error;
    s_status.sd_mount_data = sd->mount_data;
    snprintf(s_status.sd_probe_power, sizeof(s_status.sd_probe_power), "%s",
             sd->probe_power[0] ? sd->probe_power : "unknown");
    snprintf(s_status.sd_probe_mode, sizeof(s_status.sd_probe_mode), "%s",
             sd->probe_mode[0] ? sd->probe_mode : "unknown");
    s_status.last_error = sd->last_error;
    s_status.sd_state = stable_sd_state(sd->state);
    s_status.sd_filesystem = stable_filesystem(sd->filesystem);
    d1l_retained_blob_store_note_sd_backend(sd->data_ready,
                                            sd->file_ops_supported,
                                            sd->atomic_rename_supported,
                                            sd->file_line_max,
                                            sd->file_chunk_max,
                                            sd->path_max);
    set_store_backends(&s_status);

    if (!sd->bridge_ready) {
        s_status.setup_action = "bridge_unavailable";
        s_status.note = "RP2040 UART bridge is unavailable; onboard NVS remains the default data store";
    } else if (!sd->protocol_supported) {
        s_status.setup_action = "bridge_protocol_pending";
        s_status.note = "RP2040 UART is ready, but the DeskOS SD status protocol is not implemented on the bridge yet";
    } else if (strcmp(s_status.sd_state, "mount_required") == 0) {
        s_status.setup_action = "run_storage_mount";
        s_status.note = "RP2040 SD bridge is ready; run storage mount to check the inserted card before enabling SD data storage";
    } else if (strcmp(s_status.sd_state, "mount_pending") == 0) {
        s_status.setup_action = "wait_for_storage_mount";
        s_status.note = "RP2040 SD bridge is checking the inserted card; onboard NVS remains the default data store until the mount completes";
    } else if (probe_rejected_card) {
        s_status.setup_action = "inspect_rp2040_sd_cmd0_firmware_path";
        s_status.note =
            "RP2040 SD probe rejected the card init response; inspect firmware CMD0/CMD8 diagnostics before changing the card";
    } else if (!sd->card_present) {
        s_status.setup_action = "insert_card";
        s_status.note = "No SD card reported by the RP2040 bridge; onboard NVS remains the default data store";
    } else if (s_status.sd_needs_fat32) {
        if (mount_failed_with_diag) {
            s_status.setup_action = "inspect_rp2040_sd_mount_error_firmware_path";
            s_status.note =
                "SD card is present but the RP2040 SdFat mount failed; inspect firmware mount diagnostics before changing the card";
        } else {
            s_status.setup_action = "prepare_fat32_on_computer";
            s_status.note =
                "SD card is present but not usable; prepare a FAT32 card on a computer and reinsert it";
        }
    } else if (!sd->deskos_root_ready) {
        s_status.setup_action =
            strcmp(s_status.sd_state, "deskos_manifest_invalid") == 0 ?
            "backup_reformat_fat32_on_computer" : "retry_storage_mount";
        s_status.note =
            strcmp(s_status.sd_state, "deskos_manifest_invalid") == 0 ?
            "DeskOS files are invalid; back up the card and prepare FAT32 on a computer" :
            "DeskOS files are not ready; retry storage mount to create the required folders";
    } else if (s_status.setup_required) {
        s_status.setup_action = "use_nvs_fallback";
        s_status.note = "SD card is present but not ready; onboard NVS remains active";
    } else {
        s_status.setup_action = s_status.data_enabled ? "retained_history_sd_enabled" :
                                "store_migration_pending";
        s_status.note = s_status.data_enabled ?
            "SD card is valid; retained Public/DM message, route, packet history, diagnostic exports, and map tile cache can use SD with onboard NVS mirrors" :
            "SD card is valid, but retained stores remain on onboard NVS until SD-backed store migration is enabled";
        s_status.map_tile_backend = d1l_map_tile_store_sd_ready(&s_status) ?
            "sd_map_tiles_ready" : "sd_pending_store_migration";
    }
}

esp_err_t d1l_storage_status_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.initialized = true;
    s_status.mount_point = D1L_STORAGE_SD_MOUNT_POINT;
    s_status.data_root = D1L_STORAGE_SD_DATA_ROOT;
    s_status.sd_filesystem = "unknown";
    s_status.manager_state = storage_manager_state_name(s_manager_state);
    s_status.manager_running = s_storage_manager_task != NULL;
    s_status.force_nvs = s_force_nvs;
    s_status.manager_attempt = 0;
    s_status.manager_backoff_ms = 0;
    s_status.last_error = ESP_ERR_NOT_SUPPORTED;

#if CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1L
    s_status.direct_supported = false;
    s_status.rp2040_bridge_required = true;
    s_status.sd_interface = "rp2040";
    s_status.sd_state = "pending_bridge";
    s_status.note =
        "D1L microSD is not exposed through ESP32 SDMMC/SDSPI; using onboard fallback until RP2040 SD bridge is implemented";
#else
    s_status.direct_supported = false;
    s_status.rp2040_bridge_required = false;
    s_status.sd_interface = "unknown";
    s_status.sd_state = "unsupported";
    s_status.note = "SD data storage is not enabled for this board profile";
#endif

    d1l_retained_blob_store_note_sd_backend(false, false, false, 0, 0, 0);
    set_store_backends(&s_status);
    set_default_actions(&s_status);
    return ESP_OK;
}

void d1l_storage_status_note_rp2040(esp_err_t rp2040_init_result)
{
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }
    s_status.rp2040_bridge_ready = (rp2040_init_result == ESP_OK);
    if (s_status.rp2040_bridge_required && !s_status.rp2040_bridge_ready) {
        clear_sd_runtime_fields(&s_status);
        d1l_retained_blob_store_note_sd_backend(false, false, false, 0, 0, 0);
        set_store_backends(&s_status);
        s_status.sd_state = "rp2040_unavailable";
        s_status.last_error = rp2040_init_result;
        s_status.setup_action = "bridge_unavailable";
        s_status.note = "RP2040 bridge is not ready; SD data storage remains on onboard fallback";
    } else if (s_status.rp2040_bridge_required) {
        clear_sd_runtime_fields(&s_status);
        d1l_retained_blob_store_note_sd_backend(false, false, false, 0, 0, 0);
        set_store_backends(&s_status);
        s_status.sd_state = "protocol_pending";
        s_status.last_error = ESP_ERR_NOT_SUPPORTED;
        s_status.setup_action = "bridge_protocol_pending";
        s_status.note = "RP2040 UART is ready; DeskOS SD status protocol is pending on the bridge";
    }
}

esp_err_t d1l_storage_status_refresh(uint32_t timeout_ms)
{
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }

    if (!s_status.rp2040_bridge_required) {
        s_status.last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    d1l_rp2040_sd_status_t sd = {0};
    esp_err_t ret = d1l_rp2040_bridge_probe_sd(&sd, timeout_ms);
    apply_rp2040_sd_status(&sd);
    if (s_force_nvs) {
        apply_force_nvs_status();
    }
    return ret;
}

static esp_err_t poll_mount_pending(void)
{
    esp_err_t last_ret = ESP_OK;
    for (uint32_t attempt = 0; attempt < D1L_STORAGE_BOOT_POLL_ATTEMPTS; ++attempt) {
        if (strcmp(s_status.sd_state, "mount_pending") != 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(D1L_STORAGE_BOOT_POLL_INTERVAL_MS));
        esp_err_t poll_ret =
            d1l_storage_status_refresh(D1L_STORAGE_BOOT_POLL_TIMEOUT_MS);
        if (poll_ret != ESP_OK && poll_ret != ESP_ERR_TIMEOUT) {
            return poll_ret;
        }
        last_ret = poll_ret;
    }

    if (last_ret == ESP_ERR_TIMEOUT && strcmp(s_status.sd_state, "mount_pending") != 0) {
        return ESP_OK;
    }
    return last_ret;
}

static void classify_storage_manager_state(esp_err_t ret)
{
    s_status.force_nvs = s_force_nvs;
    if (s_force_nvs) {
        apply_force_nvs_status();
        s_status.manager_backoff_ms = 0;
        return;
    }

    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        set_manager_state(D1L_STORAGE_MANAGER_ERROR_BACKOFF);
        s_status.manager_backoff_ms = D1L_STORAGE_MANAGER_BACKOFF_MS;
        return;
    }

    if (storage_sd_ready_for_files()) {
        set_manager_state(D1L_STORAGE_MANAGER_READY_SD);
        s_status.manager_backoff_ms = 0;
    } else if (!s_status.rp2040_bridge_ready || !s_status.rp2040_sd_protocol_supported) {
        set_manager_state(D1L_STORAGE_MANAGER_BRIDGE_WAIT);
        s_status.manager_backoff_ms = D1L_STORAGE_MANAGER_BACKOFF_MS;
    } else if (!s_status.sd_present) {
        set_manager_state(D1L_STORAGE_MANAGER_NO_CARD);
        s_status.manager_backoff_ms = 0;
    } else if (s_status.sd_needs_fat32 ||
               strcmp(s_status.sd_state, "not_fat32_or_unmountable") == 0) {
        set_manager_state(D1L_STORAGE_MANAGER_NEEDS_FAT32);
        s_status.manager_backoff_ms = 0;
    } else {
        set_manager_state(D1L_STORAGE_MANAGER_READY_NVS);
        s_status.manager_backoff_ms = 0;
    }
}

static void storage_manager_run_once(void)
{
    s_status.manager_running = true;
    s_status.manager_attempt++;
    s_status.force_nvs = s_force_nvs;
    if (s_force_nvs) {
        apply_force_nvs_status();
        return;
    }

    if (s_manager_reset_bridge_requested) {
        s_manager_reset_bridge_requested = false;
        set_manager_state(D1L_STORAGE_MANAGER_BRIDGE_WAIT);
        esp_err_t reset_ret =
            d1l_rp2040_bridge_reset(D1L_STORAGE_MANAGER_RESET_HOLD_MS,
                                    D1L_STORAGE_MANAGER_RESET_SETTLE_MS);
        d1l_storage_status_note_rp2040(reset_ret);
        if (reset_ret != ESP_OK) {
            classify_storage_manager_state(reset_ret);
            return;
        }
    }

    set_manager_state(D1L_STORAGE_MANAGER_PING);
    d1l_rp2040_ping_t ping = {0};
    esp_err_t ret =
        d1l_rp2040_bridge_ping(&ping, D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS);
    if (ret != ESP_OK) {
        d1l_storage_status_note_rp2040(ret);
        classify_storage_manager_state(ret);
        return;
    }
    d1l_storage_status_note_rp2040(ESP_OK);

    set_manager_state(D1L_STORAGE_MANAGER_STATUS);
    ret = d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS);

    const bool remount_requested = s_manager_remount_requested;
    s_manager_remount_requested = false;
    if ((ret == ESP_OK || ret == ESP_ERR_TIMEOUT) &&
        (remount_requested ||
         strcmp(s_status.sd_state, "mount_required") == 0 ||
         strcmp(s_status.sd_state, "mount_pending") == 0)) {
        set_manager_state(D1L_STORAGE_MANAGER_MOUNT);
        ret = d1l_storage_status_mount(remount_requested ?
                                       D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS :
                                       D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS);
        if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
            esp_err_t poll_ret = poll_mount_pending();
            if (poll_ret != ESP_OK && ret == ESP_OK) {
                ret = poll_ret;
            }
        }
    }

    classify_storage_manager_state(ret);
}

static void storage_manager_task(void *arg)
{
    (void)arg;
    for (;;) {
        storage_manager_run_once();
        const uint32_t delay_ms = s_status.manager_backoff_ms > 0 ?
            s_status.manager_backoff_ms : D1L_STORAGE_MANAGER_IDLE_INTERVAL_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t d1l_storage_boot_prepare(uint32_t timeout_ms)
{
    esp_err_t ret = d1l_storage_status_refresh(timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (strcmp(s_status.sd_state, "mount_required") != 0) {
        return ret;
    }

    ret = d1l_storage_status_mount(timeout_ms);
    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        return ret;
    }

    return poll_mount_pending();
}

esp_err_t d1l_storage_manager_start(void)
{
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }
    s_status.manager_running = s_storage_manager_task != NULL;
    if (s_storage_manager_task) {
        return ESP_OK;
    }
    BaseType_t created = xTaskCreate(storage_manager_task,
                                     "storage_manager",
                                     D1L_STORAGE_MANAGER_STACK_BYTES,
                                     NULL,
                                     3,
                                     &s_storage_manager_task);
    s_status.manager_running = created == pdPASS;
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t d1l_storage_manager_request_remount(void)
{
    s_force_nvs = false;
    s_status.force_nvs = false;
    s_manager_remount_requested = true;
    return d1l_storage_manager_start();
}

esp_err_t d1l_storage_manager_reset_bridge(void)
{
    s_force_nvs = false;
    s_status.force_nvs = false;
    s_manager_reset_bridge_requested = true;
    s_manager_remount_requested = true;
    return d1l_storage_manager_start();
}

void d1l_storage_manager_force_nvs(bool force_nvs)
{
    s_force_nvs = force_nvs;
    s_status.force_nvs = force_nvs;
    if (force_nvs) {
        apply_force_nvs_status();
    } else {
        s_manager_remount_requested = true;
    }
}

esp_err_t d1l_storage_status_mount(uint32_t timeout_ms)
{
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }

    if (!s_status.rp2040_bridge_required) {
        s_status.last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    d1l_rp2040_sd_status_t sd = {0};
    esp_err_t ret = d1l_rp2040_bridge_mount_sd(&sd, timeout_ms);
    apply_rp2040_sd_status(&sd);
    if (s_force_nvs) {
        apply_force_nvs_status();
    }
    return ret;
}

void d1l_storage_status(d1l_storage_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }
    refresh_retained_sd_health(&s_status);
    *out_status = s_status;
}
