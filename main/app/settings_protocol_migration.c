#include "settings_protocol_migration.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "nvs.h"

#include "mesh/store_lock.h"
#include "platform/time_service_core.h"
#include "settings_envelope.h"

typedef struct {
    uint32_t schema_version;
    uint32_t domain_magic;
    uint32_t revision;
    uint32_t phase;
    uint32_t legacy_value;
    uint32_t confirmed_upper_bound;
    uint32_t target_high_water;
    uint32_t reserved;
} d1l_time_protocol_migration_payload_t;

_Static_assert(sizeof(d1l_time_protocol_migration_payload_t) == 32U,
               "protocol migration payload layout must remain stable");
_Static_assert(D1L_TIME_PROTOCOL_MIGRATION_MAX_RECEIPT_SIZE >=
                   sizeof(d1l_settings_envelope_header_t) +
                       sizeof(d1l_time_protocol_migration_payload_t),
               "migration receipt inspection bound must fit current schema");

static d1l_store_lock_t s_migration_lock = D1L_STORE_LOCK_INITIALIZER;

static void clear_status(d1l_time_protocol_migration_status_t *status)
{
    if (status) {
        *status = (d1l_time_protocol_migration_status_t) {
            .state = D1L_TIME_PROTOCOL_MIGRATION_UNINITIALIZED,
            .error = ESP_ERR_INVALID_STATE,
        };
    }
}

static void set_state(d1l_time_protocol_migration_status_t *status,
                      d1l_time_protocol_migration_state_t state,
                      esp_err_t error)
{
    status->state = state;
    status->error = error;
    status->confirmation_required =
        state == D1L_TIME_PROTOCOL_MIGRATION_REQUIRED;
    status->resume_required =
        state == D1L_TIME_PROTOCOL_MIGRATION_PENDING;
    status->write_blocked =
        state != D1L_TIME_PROTOCOL_MIGRATION_ABSENT &&
        state != D1L_TIME_PROTOCOL_MIGRATION_COMPLETE;
    status->intent_committed =
        state == D1L_TIME_PROTOCOL_MIGRATION_PENDING &&
        status->receipt_found &&
        status->receipt_phase == D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT;
    status->completion_committed =
        state == D1L_TIME_PROTOCOL_MIGRATION_COMPLETE &&
        status->receipt_found &&
        status->receipt_phase == D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE;
}

static esp_err_t read_optional_u32(nvs_handle_t handle,
                                   const char *key,
                                   bool *found,
                                   uint32_t *value)
{
    *found = false;
    *value = 0U;
    const esp_err_t ret = nvs_get_u32(handle, key, value);
    if (ret == ESP_OK) {
        *found = true;
        return ESP_OK;
    }
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

static esp_err_t read_protocol_values(
    d1l_time_protocol_migration_status_t *status)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
                             NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    ret = read_optional_u32(handle, D1L_TIME_PROTOCOL_LEGACY_KEY,
                            &status->legacy_present,
                            &status->observed_legacy_value);
    if (ret == ESP_OK) {
        ret = read_optional_u32(handle, D1L_TIME_PROTOCOL_HIGH_WATER_KEY,
                                &status->high_water_present,
                                &status->observed_high_water);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t read_receipt(
    d1l_time_protocol_migration_payload_t *out_payload,
    d1l_time_protocol_migration_status_t *status)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
                             NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    size_t blob_length = 0U;
    ret = nvs_get_blob(handle, D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY,
                       NULL, &blob_length);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    status->receipt_found = true;
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    uint8_t blob[D1L_TIME_PROTOCOL_MIGRATION_MAX_RECEIPT_SIZE] = {0};
    if (blob_length == 0U || blob_length > sizeof(blob)) {
        nvs_close(handle);
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                  ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t read_length = blob_length;
    ret = nvs_get_blob(handle, D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY,
                       blob, &read_length);
    nvs_close(handle);
    if (ret != ESP_OK || read_length != blob_length) {
        const esp_err_t error = ret == ESP_OK ? ESP_ERR_INVALID_SIZE : ret;
        memset(blob, 0, sizeof(blob));
        return error;
    }

    d1l_settings_envelope_header_t header = {0};
    if (blob_length >= sizeof(header)) {
        memcpy(&header, blob, sizeof(header));
        status->revision = header.revision;
        const bool envelope_shape_valid =
            header.magic == D1L_SETTINGS_ENVELOPE_MAGIC &&
            header.reserved == 0U && header.revision != 0U &&
            (size_t)header.payload_length == blob_length - sizeof(header);
        if (envelope_shape_valid &&
            header.schema_version >
                D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION) {
            memset(blob, 0, sizeof(blob));
            set_state(status,
                      D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
                      ESP_ERR_NOT_SUPPORTED);
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (envelope_shape_valid &&
            header.schema_version ==
                D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION &&
            header.payload_length >
                sizeof(d1l_time_protocol_migration_payload_t)) {
            const uint8_t *extended_payload = blob + sizeof(header);
            if (header.payload_checksum !=
                d1l_settings_envelope_checksum(
                    extended_payload, header.payload_length)) {
                memset(blob, 0, sizeof(blob));
                set_state(
                    status,
                    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_CHECKSUM,
                    ESP_FAIL);
                return ESP_FAIL;
            }
            if (header.payload_length < sizeof(uint32_t) * 2U) {
                memset(blob, 0, sizeof(blob));
                set_state(
                    status,
                    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                    ESP_ERR_INVALID_SIZE);
                return ESP_ERR_INVALID_SIZE;
            }
            uint32_t extended_schema_version = 0U;
            uint32_t extended_domain_magic = 0U;
            memcpy(&extended_schema_version, extended_payload,
                   sizeof(extended_schema_version));
            memcpy(&extended_domain_magic,
                   extended_payload + sizeof(extended_schema_version),
                   sizeof(extended_domain_magic));
            status->receipt_schema_version = extended_schema_version;
            if (extended_domain_magic !=
                D1L_TIME_PROTOCOL_MIGRATION_DOMAIN_MAGIC) {
                memset(blob, 0, sizeof(blob));
                set_state(
                    status,
                    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                    ESP_ERR_INVALID_ARG);
                return ESP_ERR_INVALID_ARG;
            }
            if (extended_schema_version >
                D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION) {
                memset(blob, 0, sizeof(blob));
                set_state(
                    status,
                    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
                    ESP_ERR_NOT_SUPPORTED);
                return ESP_ERR_NOT_SUPPORTED;
            }
            memset(blob, 0, sizeof(blob));
            set_state(
                status,
                extended_schema_version <
                        D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION ?
                    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE :
                    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                extended_schema_version <
                        D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION ?
                    ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_SIZE);
            return status->error;
        }
    }
    const uint8_t *payload_bytes = NULL;
    const d1l_settings_envelope_validation_t validation =
        d1l_settings_envelope_validate(
            blob, blob_length,
            sizeof(d1l_time_protocol_migration_payload_t),
            &header, &payload_bytes);
    status->revision = header.revision;
    if (validation != D1L_SETTINGS_ENVELOPE_VALID) {
        d1l_time_protocol_migration_state_t state =
            D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED;
        esp_err_t error = ESP_ERR_INVALID_SIZE;
        if (validation == D1L_SETTINGS_ENVELOPE_SCHEMA_NEWER) {
            state =
                D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA;
            error = ESP_ERR_NOT_SUPPORTED;
        } else if (validation ==
                   D1L_SETTINGS_ENVELOPE_CHECKSUM_MISMATCH) {
            state = D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_CHECKSUM;
            error = ESP_FAIL;
        }
        memset(blob, 0, sizeof(blob));
        set_state(status, state, error);
        return error;
    }

    memcpy(out_payload, payload_bytes, sizeof(*out_payload));
    memset(blob, 0, sizeof(blob));
    status->receipt_schema_version = out_payload->schema_version;
    status->receipt_phase = out_payload->phase;
    status->revision = out_payload->revision;
    status->legacy_value = out_payload->legacy_value;
    status->confirmed_upper_bound = out_payload->confirmed_upper_bound;
    status->target_high_water = out_payload->target_high_water;
    if (out_payload->schema_version >
        D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION) {
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
                  ESP_ERR_NOT_SUPPORTED);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (out_payload->schema_version <
        D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION) {
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                  ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    const bool phase_valid =
        out_payload->phase == D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT ||
        out_payload->phase == D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE;
    const bool revision_valid =
        (out_payload->phase == D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT &&
         out_payload->revision ==
             D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION) ||
        (out_payload->phase == D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE &&
         out_payload->revision ==
             D1L_TIME_PROTOCOL_MIGRATION_COMPLETE_REVISION);
    if (phase_valid && !revision_valid) {
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                  ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    const bool semantic_valid =
        out_payload->schema_version ==
            D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION &&
        out_payload->domain_magic ==
            D1L_TIME_PROTOCOL_MIGRATION_DOMAIN_MAGIC &&
        out_payload->revision == header.revision && phase_valid &&
        revision_valid &&
        out_payload->legacy_value >= D1L_TIME_PROTOCOL_TIMESTAMP_BASE &&
        out_payload->confirmed_upper_bound >= out_payload->legacy_value &&
        out_payload->confirmed_upper_bound <=
            UINT32_MAX - D1L_TIME_PROTOCOL_RESERVATION_SIZE &&
        out_payload->target_high_water ==
            out_payload->confirmed_upper_bound &&
        out_payload->reserved == 0U;
    if (!semantic_valid) {
        memset(out_payload, 0, sizeof(*out_payload));
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                  ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t inspect_locked(
    d1l_time_protocol_migration_status_t *status)
{
    clear_status(status);
    d1l_time_protocol_migration_payload_t payload = {0};
    esp_err_t ret = read_receipt(&payload, status);
    if (ret != ESP_OK) {
        if (status->state == D1L_TIME_PROTOCOL_MIGRATION_UNINITIALIZED) {
            set_state(status, D1L_TIME_PROTOCOL_MIGRATION_STORAGE_ERROR,
                      ret);
        }
        memset(&payload, 0, sizeof(payload));
        return ret;
    }
    ret = read_protocol_values(status);
    if (ret != ESP_OK) {
        memset(&payload, 0, sizeof(payload));
        set_state(status, D1L_TIME_PROTOCOL_MIGRATION_STORAGE_ERROR, ret);
        return ret;
    }

    if (!status->receipt_found) {
        status->legacy_value = status->observed_legacy_value;
        if (!status->legacy_present) {
            set_state(status, D1L_TIME_PROTOCOL_MIGRATION_ABSENT, ESP_OK);
            return ESP_OK;
        }
        if (status->observed_legacy_value <
            D1L_TIME_PROTOCOL_TIMESTAMP_BASE) {
            set_state(status,
                      D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                      ESP_ERR_INVALID_ARG);
            return ESP_ERR_INVALID_ARG;
        }
        if (status->high_water_present) {
            set_state(status,
                      D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                      ESP_ERR_INVALID_STATE);
            return ESP_ERR_INVALID_STATE;
        }
        status->confirmed_upper_bound = 0U;
        status->target_high_water = 0U;
        set_state(status, D1L_TIME_PROTOCOL_MIGRATION_REQUIRED,
                  ESP_ERR_INVALID_STATE);
        return ESP_OK;
    }

    const bool receipt_complete =
        payload.phase == D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE;
    if (receipt_complete && status->legacy_present) {
        memset(&payload, 0, sizeof(payload));
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                  ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    if (status->legacy_present &&
        status->observed_legacy_value != payload.legacy_value) {
        memset(&payload, 0, sizeof(payload));
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                  ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    if (status->high_water_present &&
        ((receipt_complete && status->observed_high_water <
                                  payload.target_high_water) ||
         (!receipt_complete && status->observed_high_water !=
                                   payload.target_high_water))) {
        memset(&payload, 0, sizeof(payload));
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                  ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    if (receipt_complete) {
        if (!status->high_water_present) {
            memset(&payload, 0, sizeof(payload));
            set_state(status,
                      D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                      ESP_ERR_INVALID_STATE);
            return ESP_ERR_INVALID_STATE;
        }
        memset(&payload, 0, sizeof(payload));
        set_state(status, D1L_TIME_PROTOCOL_MIGRATION_COMPLETE, ESP_OK);
        return ESP_OK;
    }

    if ((!status->legacy_present && !status->high_water_present) ||
        (!status->legacy_present &&
         status->observed_high_water != payload.target_high_water)) {
        memset(&payload, 0, sizeof(payload));
        set_state(status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                  ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&payload, 0, sizeof(payload));
    set_state(status, D1L_TIME_PROTOCOL_MIGRATION_PENDING,
              ESP_ERR_INVALID_STATE);
    return ESP_OK;
}

esp_err_t d1l_time_protocol_migration_inspect(
    d1l_time_protocol_migration_status_t *out_status)
{
    if (!out_status) {
        return ESP_ERR_INVALID_ARG;
    }
    d1l_store_lock_take(&s_migration_lock);
    const esp_err_t ret = inspect_locked(out_status);
    d1l_store_lock_give(&s_migration_lock);
    return ret;
}

static esp_err_t write_receipt(
    uint32_t phase,
    uint32_t legacy_value,
    uint32_t confirmed_upper_bound,
    uint32_t revision)
{
    const d1l_time_protocol_migration_payload_t payload = {
        .schema_version = D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION,
        .domain_magic = D1L_TIME_PROTOCOL_MIGRATION_DOMAIN_MAGIC,
        .revision = revision,
        .phase = (uint32_t)phase,
        .legacy_value = legacy_value,
        .confirmed_upper_bound = confirmed_upper_bound,
        .target_high_water = confirmed_upper_bound,
        .reserved = 0U,
    };
    uint8_t blob[sizeof(d1l_settings_envelope_header_t) +
                 sizeof(payload)] = {0};
    size_t blob_length = 0U;
    if (!d1l_settings_envelope_build(
            blob, sizeof(blob), &payload, sizeof(payload), revision,
            &blob_length)) {
        return ESP_ERR_INVALID_SIZE;
    }
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
                             NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_set_blob(handle, D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY,
                           blob, blob_length);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    memset(blob, 0, sizeof(blob));
    return ret;
}

static esp_err_t write_high_water(uint32_t high_water)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
                             NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_set_u32(handle, D1L_TIME_PROTOCOL_HIGH_WATER_KEY,
                          high_water);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    return ret;
}

static esp_err_t erase_legacy(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
                             NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_erase_key(handle, D1L_TIME_PROTOCOL_LEGACY_KEY);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    return ret;
}

static esp_err_t refresh_after_failure(
    esp_err_t failure,
    d1l_time_protocol_migration_status_t *status)
{
    (void)inspect_locked(status);
    if (status->state == D1L_TIME_PROTOCOL_MIGRATION_UNINITIALIZED) {
        set_state(status, D1L_TIME_PROTOCOL_MIGRATION_STORAGE_ERROR,
                  failure);
    }
    status->error = failure;
    return failure;
}

esp_err_t d1l_time_protocol_migration_run(
    uint32_t expected_legacy_value,
    uint32_t confirmed_upper_bound,
    const char *confirmation,
    bool *out_written,
    d1l_time_protocol_migration_status_t *out_status)
{
    if (out_written) {
        *out_written = false;
    }
    clear_status(out_status);
    if (!out_written || !out_status) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_store_lock_take(&s_migration_lock);
    esp_err_t ret = inspect_locked(out_status);
    const bool request_valid =
        confirmation &&
        strcmp(confirmation,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION) == 0 &&
        expected_legacy_value >= D1L_TIME_PROTOCOL_TIMESTAMP_BASE &&
        confirmed_upper_bound >= expected_legacy_value &&
        confirmed_upper_bound <=
            UINT32_MAX - D1L_TIME_PROTOCOL_RESERVATION_SIZE;
    if (!request_valid) {
        d1l_store_lock_give(&s_migration_lock);
        return ESP_ERR_INVALID_ARG;
    }
    if (ret != ESP_OK ||
        (out_status->state != D1L_TIME_PROTOCOL_MIGRATION_REQUIRED &&
         out_status->state != D1L_TIME_PROTOCOL_MIGRATION_PENDING &&
         out_status->state != D1L_TIME_PROTOCOL_MIGRATION_COMPLETE)) {
        d1l_store_lock_give(&s_migration_lock);
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
    }
    if (out_status->state == D1L_TIME_PROTOCOL_MIGRATION_COMPLETE) {
        const bool same =
            out_status->legacy_value == expected_legacy_value &&
            out_status->confirmed_upper_bound == confirmed_upper_bound;
        d1l_store_lock_give(&s_migration_lock);
        return same ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (out_status->legacy_value != expected_legacy_value ||
        (out_status->state == D1L_TIME_PROTOCOL_MIGRATION_PENDING &&
         out_status->confirmed_upper_bound != confirmed_upper_bound)) {
        d1l_store_lock_give(&s_migration_lock);
        return ESP_ERR_INVALID_ARG;
    }

    if (out_status->state == D1L_TIME_PROTOCOL_MIGRATION_REQUIRED) {
        ret = write_receipt(D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT,
                            expected_legacy_value,
                            confirmed_upper_bound,
                            D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
        if (ret != ESP_OK) {
            ret = refresh_after_failure(ret, out_status);
            d1l_store_lock_give(&s_migration_lock);
            return ret;
        }
        *out_written = true;
        ret = inspect_locked(out_status);
        if (ret != ESP_OK ||
            out_status->state != D1L_TIME_PROTOCOL_MIGRATION_PENDING) {
            ret = ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
            d1l_store_lock_give(&s_migration_lock);
            return ret;
        }
    }

    if (!out_status->high_water_present) {
        ret = write_high_water(confirmed_upper_bound);
        if (ret != ESP_OK) {
            ret = refresh_after_failure(ret, out_status);
            d1l_store_lock_give(&s_migration_lock);
            return ret;
        }
        *out_written = true;
        ret = inspect_locked(out_status);
        if (ret != ESP_OK ||
            out_status->state != D1L_TIME_PROTOCOL_MIGRATION_PENDING) {
            ret = ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
            d1l_store_lock_give(&s_migration_lock);
            return ret;
        }
    }

    if (out_status->legacy_present) {
        ret = erase_legacy();
        if (ret != ESP_OK) {
            ret = refresh_after_failure(ret, out_status);
            d1l_store_lock_give(&s_migration_lock);
            return ret;
        }
        *out_written = true;
        ret = inspect_locked(out_status);
        if (ret != ESP_OK ||
            out_status->state != D1L_TIME_PROTOCOL_MIGRATION_PENDING) {
            ret = ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
            d1l_store_lock_give(&s_migration_lock);
            return ret;
        }
    }

    uint32_t complete_revision = 0U;
    if (!d1l_settings_envelope_next_revision(out_status->revision,
                                             &complete_revision)) {
        set_state(out_status,
                  D1L_TIME_PROTOCOL_MIGRATION_REVISION_SATURATED,
                  ESP_ERR_INVALID_STATE);
        d1l_store_lock_give(&s_migration_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (complete_revision !=
        D1L_TIME_PROTOCOL_MIGRATION_COMPLETE_REVISION) {
        set_state(out_status,
                  D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                  ESP_ERR_INVALID_STATE);
        d1l_store_lock_give(&s_migration_lock);
        return ESP_ERR_INVALID_STATE;
    }
    ret = write_receipt(D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE,
                        expected_legacy_value, confirmed_upper_bound,
                        complete_revision);
    if (ret != ESP_OK) {
        ret = refresh_after_failure(ret, out_status);
        d1l_store_lock_give(&s_migration_lock);
        return ret;
    }
    *out_written = true;
    ret = inspect_locked(out_status);
    if (ret == ESP_OK &&
        out_status->state != D1L_TIME_PROTOCOL_MIGRATION_COMPLETE) {
        ret = ESP_ERR_INVALID_STATE;
    }
    d1l_store_lock_give(&s_migration_lock);
    return ret;
}

const char *d1l_time_protocol_migration_state_name(
    d1l_time_protocol_migration_state_t state)
{
    switch (state) {
    case D1L_TIME_PROTOCOL_MIGRATION_ABSENT:
        return "absent";
    case D1L_TIME_PROTOCOL_MIGRATION_REQUIRED:
        return "confirmation_required";
    case D1L_TIME_PROTOCOL_MIGRATION_PENDING:
        return "pending_resume";
    case D1L_TIME_PROTOCOL_MIGRATION_COMPLETE:
        return "complete";
    case D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA:
        return "quarantined_newer_schema";
    case D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED:
        return "quarantined_malformed";
    case D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_CHECKSUM:
        return "quarantined_checksum";
    case D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE:
        return "quarantined_downgrade";
    case D1L_TIME_PROTOCOL_MIGRATION_STORAGE_ERROR:
        return "storage_error";
    case D1L_TIME_PROTOCOL_MIGRATION_REVISION_SATURATED:
        return "revision_saturated";
    case D1L_TIME_PROTOCOL_MIGRATION_UNINITIALIZED:
    default:
        return "uninitialized";
    }
}
