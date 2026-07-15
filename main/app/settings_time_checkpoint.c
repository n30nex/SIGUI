#include "settings_time_checkpoint.h"

#include <stddef.h>
#include <string.h>

#include "nvs.h"

#include "mesh/store_lock.h"
#include "platform/time_service_core.h"
#include "settings_envelope.h"

#define D1L_TIME_CHECKPOINT_NVS_NAMESPACE "d1l_time"
#define D1L_TIME_CHECKPOINT_NVS_KEY "wall_ckpt_v1"
#define D1L_TIME_CHECKPOINT_PAYLOAD_MAGIC 0x314B4354UL

typedef struct {
    uint32_t schema_version;
    uint32_t domain_magic;
    uint32_t revision;
    uint32_t source;
    int64_t epoch_sec;
    uint32_t protocol_reserved_through;
    uint32_t reserved;
} d1l_settings_time_checkpoint_payload_t;

_Static_assert(sizeof(d1l_settings_time_checkpoint_payload_t) == 32U,
               "time checkpoint payload layout must remain stable");

static d1l_store_lock_t s_checkpoint_lock = D1L_STORE_LOCK_INITIALIZER;

static void clear_outputs(
    d1l_settings_time_checkpoint_t *checkpoint,
    d1l_settings_time_checkpoint_status_t *status)
{
    if (checkpoint) {
        memset(checkpoint, 0, sizeof(*checkpoint));
    }
    if (status) {
        *status = (d1l_settings_time_checkpoint_status_t) {
            .state = D1L_SETTINGS_TIME_CHECKPOINT_UNINITIALIZED,
            .error = ESP_ERR_INVALID_STATE,
        };
    }
}

static bool source_valid(d1l_settings_time_checkpoint_source_t source)
{
    return source == D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP ||
           source ==
               D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED;
}

static bool checkpoint_valid(
    const d1l_settings_time_checkpoint_t *checkpoint)
{
    return checkpoint && source_valid(checkpoint->source) &&
           checkpoint->epoch_sec >= D1L_TIME_WALL_MIN_EPOCH &&
           checkpoint->protocol_reserved_through >=
               D1L_TIME_PROTOCOL_TIMESTAMP_BASE;
}

static void set_status(d1l_settings_time_checkpoint_status_t *status,
                       d1l_settings_time_checkpoint_state_t state,
                       esp_err_t error,
                       uint32_t revision,
                       bool found)
{
    if (!status) {
        return;
    }
    *status = (d1l_settings_time_checkpoint_status_t) {
        .state = state,
        .error = error,
        .revision = revision,
        .found = found,
    };
}

static esp_err_t load_locked(
    d1l_settings_time_checkpoint_t *out_checkpoint,
    d1l_settings_time_checkpoint_status_t *out_status)
{
    clear_outputs(out_checkpoint, out_status);
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_TIME_CHECKPOINT_NVS_NAMESPACE,
                             NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_ABSENT,
                   ESP_OK, 0U, false);
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR,
                   ret, 0U, false);
        return ret;
    }

    size_t blob_length = 0U;
    ret = nvs_get_blob(handle, D1L_TIME_CHECKPOINT_NVS_KEY, NULL,
                       &blob_length);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_ABSENT,
                   ESP_OK, 0U, false);
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        nvs_close(handle);
        set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR,
                   ret, 0U, false);
        return ret;
    }

    uint8_t blob[sizeof(d1l_settings_envelope_header_t) +
                 sizeof(d1l_settings_time_checkpoint_payload_t)] = {0};
    if (blob_length == 0U || blob_length > sizeof(blob)) {
        nvs_close(handle);
        set_status(out_status,
                   D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED,
                   ESP_ERR_INVALID_SIZE, 0U, true);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t read_length = blob_length;
    ret = nvs_get_blob(handle, D1L_TIME_CHECKPOINT_NVS_KEY, blob,
                       &read_length);
    nvs_close(handle);
    if (ret != ESP_OK || read_length != blob_length) {
        const esp_err_t error = ret == ESP_OK ? ESP_ERR_INVALID_SIZE : ret;
        memset(blob, 0, sizeof(blob));
        set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR,
                   error, 0U, true);
        return error;
    }

    d1l_settings_envelope_header_t header = {0};
    const uint8_t *payload_bytes = NULL;
    const d1l_settings_envelope_validation_t validation =
        d1l_settings_envelope_validate(
            blob, blob_length,
            sizeof(d1l_settings_time_checkpoint_payload_t),
            &header, &payload_bytes);
    if (validation != D1L_SETTINGS_ENVELOPE_VALID) {
        d1l_settings_time_checkpoint_state_t state =
            D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED;
        esp_err_t error = ESP_ERR_INVALID_SIZE;
        if (validation == D1L_SETTINGS_ENVELOPE_SCHEMA_NEWER) {
            state = D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_NEWER_SCHEMA;
            error = ESP_ERR_NOT_SUPPORTED;
        } else if (validation ==
                   D1L_SETTINGS_ENVELOPE_CHECKSUM_MISMATCH) {
            state = D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_CHECKSUM;
            error = ESP_FAIL;
        }
        memset(blob, 0, sizeof(blob));
        set_status(out_status, state, error, header.revision, true);
        return error;
    }

    d1l_settings_time_checkpoint_payload_t payload = {0};
    memcpy(&payload, payload_bytes, sizeof(payload));
    memset(blob, 0, sizeof(blob));
    if (payload.schema_version >
        D1L_SETTINGS_TIME_CHECKPOINT_SCHEMA_VERSION) {
        memset(&payload, 0, sizeof(payload));
        set_status(out_status,
                   D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_NEWER_SCHEMA,
                   ESP_ERR_NOT_SUPPORTED, header.revision, true);
        return ESP_ERR_NOT_SUPPORTED;
    }
    const d1l_settings_time_checkpoint_t checkpoint = {
        .epoch_sec = payload.epoch_sec,
        .protocol_reserved_through = payload.protocol_reserved_through,
        .source = (d1l_settings_time_checkpoint_source_t)payload.source,
    };
    const bool reserved_clear = payload.reserved == 0U;
    const bool valid = payload.schema_version ==
                           D1L_SETTINGS_TIME_CHECKPOINT_SCHEMA_VERSION &&
                       payload.domain_magic ==
                           D1L_TIME_CHECKPOINT_PAYLOAD_MAGIC &&
                       payload.revision == header.revision &&
                       reserved_clear && checkpoint_valid(&checkpoint);
    memset(&payload, 0, sizeof(payload));
    if (!valid) {
        set_status(out_status,
                   D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED,
                   ESP_ERR_INVALID_ARG, header.revision, true);
        return ESP_ERR_INVALID_ARG;
    }
    if (out_checkpoint) {
        *out_checkpoint = checkpoint;
    }
    set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_READY,
               ESP_OK, header.revision, true);
    out_status->epoch_sec = checkpoint.epoch_sec;
    out_status->protocol_reserved_through =
        checkpoint.protocol_reserved_through;
    out_status->source = checkpoint.source;
    return ESP_OK;
}

esp_err_t d1l_settings_time_checkpoint_load(
    d1l_settings_time_checkpoint_t *out_checkpoint,
    d1l_settings_time_checkpoint_status_t *out_status)
{
    if (!out_checkpoint || !out_status) {
        clear_outputs(out_checkpoint, out_status);
        return ESP_ERR_INVALID_ARG;
    }
    d1l_store_lock_take(&s_checkpoint_lock);
    const esp_err_t ret = load_locked(out_checkpoint, out_status);
    d1l_store_lock_give(&s_checkpoint_lock);
    return ret;
}

static bool source_is_stronger(
    d1l_settings_time_checkpoint_source_t candidate,
    d1l_settings_time_checkpoint_source_t current)
{
    return candidate ==
               D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED &&
           current == D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP;
}

static bool checkpoint_write_required(
    const d1l_settings_time_checkpoint_t *candidate,
    const d1l_settings_time_checkpoint_t *current,
    bool current_found)
{
    if (!current_found) {
        return true;
    }
    if (candidate->epoch_sec == current->epoch_sec) {
        return source_is_stronger(candidate->source, current->source);
    }
    if (source_is_stronger(candidate->source, current->source)) {
        return true;
    }
    /* The checkpoint is a wall-clock estimate, not the monotonic protocol
     * guard. Persist material backward corrections as well as forward
     * progress so a bad-but-valid old wall cannot strand offline display time
     * forever. Small clock corrections remain coalesced. */
    const uint64_t change = candidate->epoch_sec > current->epoch_sec ?
        (uint64_t)(candidate->epoch_sec - current->epoch_sec) :
        (uint64_t)(current->epoch_sec - candidate->epoch_sec);
    return change >=
           (uint64_t)D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC;
}

esp_err_t d1l_settings_time_checkpoint_save(
    const d1l_settings_time_checkpoint_t *checkpoint,
    bool *out_written,
    d1l_settings_time_checkpoint_status_t *out_status)
{
    if (out_written) {
        *out_written = false;
    }
    if (!checkpoint_valid(checkpoint) || !out_written || !out_status) {
        clear_outputs(NULL, out_status);
        return ESP_ERR_INVALID_ARG;
    }

    d1l_store_lock_take(&s_checkpoint_lock);
    d1l_settings_time_checkpoint_t current = {0};
    d1l_settings_time_checkpoint_status_t current_status = {0};
    esp_err_t ret = load_locked(&current, &current_status);
    if (ret != ESP_OK) {
        *out_status = current_status;
        d1l_store_lock_give(&s_checkpoint_lock);
        return ret;
    }
    if (!checkpoint_write_required(checkpoint, &current,
                                   current_status.found)) {
        *out_status = current_status;
        d1l_store_lock_give(&s_checkpoint_lock);
        return ESP_OK;
    }

    uint32_t next_revision = 0U;
    if (!d1l_settings_envelope_next_revision(current_status.revision,
                                             &next_revision)) {
        set_status(out_status,
                   D1L_SETTINGS_TIME_CHECKPOINT_REVISION_SATURATED,
                   ESP_ERR_INVALID_STATE, current_status.revision,
                   current_status.found);
        out_status->epoch_sec = current.epoch_sec;
        out_status->protocol_reserved_through =
            current.protocol_reserved_through;
        out_status->source = current.source;
        d1l_store_lock_give(&s_checkpoint_lock);
        return ESP_ERR_INVALID_STATE;
    }

    const d1l_settings_time_checkpoint_payload_t payload = {
        .schema_version = D1L_SETTINGS_TIME_CHECKPOINT_SCHEMA_VERSION,
        .domain_magic = D1L_TIME_CHECKPOINT_PAYLOAD_MAGIC,
        .revision = next_revision,
        .source = (uint32_t)checkpoint->source,
        .epoch_sec = checkpoint->epoch_sec,
        .protocol_reserved_through =
            checkpoint->protocol_reserved_through,
        .reserved = 0U,
    };
    uint8_t blob[sizeof(d1l_settings_envelope_header_t) +
                 sizeof(payload)] = {0};
    size_t blob_length = 0U;
    if (!d1l_settings_envelope_build(
            blob, sizeof(blob), &payload, sizeof(payload), next_revision,
            &blob_length)) {
        memset(blob, 0, sizeof(blob));
        set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR,
                   ESP_ERR_INVALID_SIZE, current_status.revision,
                   current_status.found);
        out_status->epoch_sec = current.epoch_sec;
        out_status->protocol_reserved_through =
            current.protocol_reserved_through;
        out_status->source = current.source;
        d1l_store_lock_give(&s_checkpoint_lock);
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t handle;
    ret = nvs_open(D1L_TIME_CHECKPOINT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_set_blob(handle, D1L_TIME_CHECKPOINT_NVS_KEY,
                           blob, blob_length);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    memset(blob, 0, sizeof(blob));
    if (ret != ESP_OK) {
        set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR,
                   ret, current_status.revision, current_status.found);
        out_status->epoch_sec = current.epoch_sec;
        out_status->protocol_reserved_through =
            current.protocol_reserved_through;
        out_status->source = current.source;
        d1l_store_lock_give(&s_checkpoint_lock);
        return ret;
    }

    *out_written = true;
    set_status(out_status, D1L_SETTINGS_TIME_CHECKPOINT_READY,
               ESP_OK, next_revision, true);
    out_status->epoch_sec = checkpoint->epoch_sec;
    out_status->protocol_reserved_through =
        checkpoint->protocol_reserved_through;
    out_status->source = checkpoint->source;
    d1l_store_lock_give(&s_checkpoint_lock);
    return ESP_OK;
}

const char *d1l_settings_time_checkpoint_state_name(
    d1l_settings_time_checkpoint_state_t state)
{
    switch (state) {
    case D1L_SETTINGS_TIME_CHECKPOINT_ABSENT:
        return "absent";
    case D1L_SETTINGS_TIME_CHECKPOINT_READY:
        return "ready";
    case D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_NEWER_SCHEMA:
        return "quarantined_newer_schema";
    case D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED:
        return "quarantined_malformed";
    case D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_CHECKSUM:
        return "quarantined_checksum";
    case D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR:
        return "storage_error";
    case D1L_SETTINGS_TIME_CHECKPOINT_REVISION_SATURATED:
        return "revision_saturated";
    case D1L_SETTINGS_TIME_CHECKPOINT_UNINITIALIZED:
    default:
        return "uninitialized";
    }
}

const char *d1l_settings_time_checkpoint_source_name(
    d1l_settings_time_checkpoint_source_t source)
{
    switch (source) {
    case D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP:
        return "sntp";
    case D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED:
        return "companion_authenticated";
    case D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_NONE:
    default:
        return "none";
    }
}
