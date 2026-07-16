#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/settings_envelope.h"
#include "app/settings_protocol_migration.h"
#include "mock_esp_nvs.h"
#include "platform/time_service_core.h"

#define TEST_LEGACY (D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 17U)
#define TEST_UPPER (TEST_LEGACY + 257U)

typedef struct {
    uint32_t schema_version;
    uint32_t domain_magic;
    uint32_t revision;
    uint32_t phase;
    uint32_t legacy_value;
    uint32_t confirmed_upper_bound;
    uint32_t target_high_water;
    uint32_t reserved;
} migration_payload_t;

typedef struct {
    migration_payload_t prefix;
    uint32_t extension_words[4];
} future_migration_payload_t;

_Static_assert(sizeof(migration_payload_t) == 32U,
               "test payload must mirror the stable migration layout");

#define RECEIPT_BLOB_SIZE \
    (sizeof(d1l_settings_envelope_header_t) + sizeof(migration_payload_t))

static void seed_u32(const char *namespace_name, const char *key,
                     uint32_t value)
{
    assert(mock_nvs_seed_blob(namespace_name, key, &value, sizeof(value)));
}

static bool copy_u32(const char *namespace_name, const char *key,
                     uint32_t *value)
{
    uint32_t copy = 0U;
    const size_t length = mock_nvs_copy_blob(
        namespace_name, key, &copy, sizeof(copy));
    if (length == 0U) {
        return false;
    }
    assert(length == sizeof(copy));
    if (value) {
        *value = copy;
    }
    return true;
}

static migration_payload_t payload(uint32_t phase, uint32_t revision)
{
    return (migration_payload_t) {
        .schema_version = D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION,
        .domain_magic = D1L_TIME_PROTOCOL_MIGRATION_DOMAIN_MAGIC,
        .revision = revision,
        .phase = phase,
        .legacy_value = TEST_LEGACY,
        .confirmed_upper_bound = TEST_UPPER,
        .target_high_water = TEST_UPPER,
        .reserved = 0U,
    };
}

static void seed_receipt(const migration_payload_t *value,
                         uint32_t envelope_revision)
{
    uint8_t blob[RECEIPT_BLOB_SIZE] = {0};
    size_t length = 0U;
    assert(d1l_settings_envelope_build(
        blob, sizeof(blob), value, sizeof(*value), envelope_revision,
        &length));
    assert(length == sizeof(blob));
    assert(mock_nvs_seed_blob(
        D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
        D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY, blob, length));
    memset(blob, 0, sizeof(blob));
}

static void seed_intent(bool legacy_present, bool high_water_present)
{
    migration_payload_t value = payload(
        D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT,
        D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    seed_receipt(&value, D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    memset(&value, 0, sizeof(value));
    if (legacy_present) {
        seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
                 D1L_TIME_PROTOCOL_LEGACY_KEY, TEST_LEGACY);
    }
    if (high_water_present) {
        seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
                 D1L_TIME_PROTOCOL_HIGH_WATER_KEY, TEST_UPPER);
    }
}

static void seed_complete(void)
{
    migration_payload_t value = payload(
        D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE,
        D1L_TIME_PROTOCOL_MIGRATION_COMPLETE_REVISION);
    seed_receipt(&value, D1L_TIME_PROTOCOL_MIGRATION_COMPLETE_REVISION);
    memset(&value, 0, sizeof(value));
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_HIGH_WATER_KEY, TEST_UPPER);
}

static d1l_time_protocol_migration_status_t inspect_state(
    d1l_time_protocol_migration_state_t expected_state,
    esp_err_t expected_result)
{
    d1l_time_protocol_migration_status_t status = {0};
    assert(d1l_time_protocol_migration_inspect(&status) == expected_result);
    assert(status.state == expected_state);
    return status;
}

static void assert_complete(void)
{
    const d1l_time_protocol_migration_status_t status = inspect_state(
        D1L_TIME_PROTOCOL_MIGRATION_COMPLETE, ESP_OK);
    assert(status.receipt_found);
    assert(status.receipt_schema_version ==
           D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION);
    assert(status.receipt_phase ==
           D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE);
    assert(status.revision ==
           D1L_TIME_PROTOCOL_MIGRATION_COMPLETE_REVISION);
    assert(status.completion_committed);
    assert(!status.intent_committed);
    assert(!status.legacy_present);
    assert(status.high_water_present);
    assert(status.observed_high_water == TEST_UPPER);
    assert(status.legacy_value == TEST_LEGACY);
    assert(status.confirmed_upper_bound == TEST_UPPER);
    assert(!status.write_blocked);
    uint32_t high_water = 0U;
    assert(copy_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
                    D1L_TIME_PROTOCOL_HIGH_WATER_KEY, &high_water));
    assert(high_water == TEST_UPPER);
    assert(!copy_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
                     D1L_TIME_PROTOCOL_LEGACY_KEY, NULL));
}

static void run_to_complete(void)
{
    bool written = false;
    d1l_time_protocol_migration_status_t status = {0};
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY, TEST_UPPER,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == ESP_OK);
    assert(written);
    assert(status.state == D1L_TIME_PROTOCOL_MIGRATION_COMPLETE);
    assert_complete();
}

static void test_absent_required_and_bounds(void)
{
    mock_nvs_reset();
    d1l_time_protocol_migration_status_t status = inspect_state(
        D1L_TIME_PROTOCOL_MIGRATION_ABSENT, ESP_OK);
    assert(!status.write_blocked);
    assert(mock_nvs_set_call_count() == 0U);
    assert(mock_nvs_commit_call_count() == 0U);

    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_LEGACY_KEY, TEST_LEGACY);
    status = inspect_state(D1L_TIME_PROTOCOL_MIGRATION_REQUIRED, ESP_OK);
    assert(status.legacy_present);
    assert(status.observed_legacy_value == TEST_LEGACY);
    assert(status.legacy_value == TEST_LEGACY);
    assert(status.confirmation_required);
    assert(status.write_blocked);
    assert(!status.high_water_present);
    assert(mock_nvs_set_call_count() == 0U);
    assert(mock_nvs_commit_call_count() == 0U);

    const size_t set_before = mock_nvs_set_call_count();
    const size_t commit_before = mock_nvs_commit_call_count();
    const size_t erase_before = mock_nvs_erase_call_count();
    bool written = true;
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY, TEST_UPPER, "wrong-confirmation",
               &written, &status) == ESP_ERR_INVALID_ARG);
    assert(!written);
    assert(status.state == D1L_TIME_PROTOCOL_MIGRATION_REQUIRED);
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY + 1U, TEST_UPPER,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == ESP_ERR_INVALID_ARG);
    assert(d1l_time_protocol_migration_run(
               D1L_TIME_PROTOCOL_TIMESTAMP_BASE - 1U, TEST_UPPER,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == ESP_ERR_INVALID_ARG);
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY, TEST_LEGACY - 1U,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == ESP_ERR_INVALID_ARG);
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY,
               UINT32_MAX - D1L_TIME_PROTOCOL_RESERVATION_SIZE + 1U,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == ESP_ERR_INVALID_ARG);
    assert(mock_nvs_set_call_count() == set_before);
    assert(mock_nvs_commit_call_count() == commit_before);
    assert(mock_nvs_erase_call_count() == erase_before);
    uint32_t legacy = 0U;
    assert(copy_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
                    D1L_TIME_PROTOCOL_LEGACY_KEY, &legacy));
    assert(legacy == TEST_LEGACY);
}

static void test_happy_and_idempotent(void)
{
    mock_nvs_reset();
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_LEGACY_KEY, TEST_LEGACY);
    run_to_complete();

    const size_t set_before = mock_nvs_set_call_count();
    const size_t commit_before = mock_nvs_commit_call_count();
    const size_t erase_before = mock_nvs_erase_call_count();
    bool written = true;
    d1l_time_protocol_migration_status_t status = {0};
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY, TEST_UPPER,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == ESP_OK);
    assert(!written);
    assert(status.state == D1L_TIME_PROTOCOL_MIGRATION_COMPLETE);
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY, TEST_UPPER + 1U,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == ESP_ERR_INVALID_ARG);
    assert(mock_nvs_set_call_count() == set_before);
    assert(mock_nvs_commit_call_count() == commit_before);
    assert(mock_nvs_erase_call_count() == erase_before);
}

static void test_each_durable_power_cut_resume(void)
{
    mock_nvs_reset();
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_LEGACY_KEY, TEST_LEGACY);
    inspect_state(D1L_TIME_PROTOCOL_MIGRATION_REQUIRED, ESP_OK);
    run_to_complete();

    mock_nvs_reset();
    seed_intent(true, false);
    d1l_time_protocol_migration_status_t status = inspect_state(
        D1L_TIME_PROTOCOL_MIGRATION_PENDING, ESP_OK);
    assert(status.intent_committed && status.legacy_present);
    assert(!status.high_water_present);
    run_to_complete();

    mock_nvs_reset();
    seed_intent(true, true);
    status = inspect_state(D1L_TIME_PROTOCOL_MIGRATION_PENDING, ESP_OK);
    assert(status.intent_committed && status.legacy_present);
    assert(status.high_water_present);
    run_to_complete();

    mock_nvs_reset();
    seed_intent(false, true);
    status = inspect_state(D1L_TIME_PROTOCOL_MIGRATION_PENDING, ESP_OK);
    assert(status.intent_committed && !status.legacy_present);
    assert(status.high_water_present);
    run_to_complete();

    mock_nvs_reset();
    seed_complete();
    assert_complete();
}

static void assert_failure_then_retry(esp_err_t expected_error,
                                      bool expect_legacy,
                                      bool expect_high_water,
                                      bool expect_receipt)
{
    bool written = false;
    d1l_time_protocol_migration_status_t status = {0};
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY, TEST_UPPER,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == expected_error);
    assert(status.legacy_present == expect_legacy);
    assert(status.high_water_present == expect_high_water);
    assert(status.receipt_found == expect_receipt);
    assert(status.write_blocked);
    run_to_complete();
}

static void test_commit_failures_model_every_power_cut_window(void)
{
    mock_nvs_reset();
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_LEGACY_KEY, TEST_LEGACY);
    mock_nvs_fail_next_commit(ESP_FAIL);
    assert_failure_then_retry(ESP_FAIL, true, false, false);

    mock_nvs_reset();
    seed_intent(true, false);
    mock_nvs_fail_next_commit(ESP_FAIL);
    assert_failure_then_retry(ESP_FAIL, true, false, true);

    mock_nvs_reset();
    seed_intent(true, true);
    mock_nvs_fail_next_commit(ESP_FAIL);
    assert_failure_then_retry(ESP_FAIL, true, true, true);

    mock_nvs_reset();
    seed_intent(false, true);
    mock_nvs_fail_next_commit(ESP_FAIL);
    assert_failure_then_retry(ESP_FAIL, false, true, true);
}

static void test_nvs_operation_failures_are_retry_safe(void)
{
    mock_nvs_reset();
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_LEGACY_KEY, TEST_LEGACY);
    mock_nvs_fail_next_set(ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    assert_failure_then_retry(
        ESP_ERR_NVS_NOT_ENOUGH_SPACE, true, false, false);

    mock_nvs_reset();
    seed_intent(true, false);
    mock_nvs_fail_next_set(ESP_FAIL);
    assert_failure_then_retry(ESP_FAIL, true, false, true);

    mock_nvs_reset();
    seed_intent(true, true);
    mock_nvs_fail_next_erase(ESP_FAIL);
    assert_failure_then_retry(ESP_FAIL, true, true, true);

    mock_nvs_reset();
    seed_intent(false, true);
    mock_nvs_fail_next_set(ESP_FAIL);
    assert_failure_then_retry(ESP_FAIL, false, true, true);

    mock_nvs_reset();
    mock_nvs_fail_next_open(ESP_FAIL);
    d1l_time_protocol_migration_status_t status = inspect_state(
        D1L_TIME_PROTOCOL_MIGRATION_STORAGE_ERROR, ESP_FAIL);
    assert(status.write_blocked);
    assert(mock_nvs_set_call_count() == 0U);

    mock_nvs_reset();
    mock_nvs_fail_next_get(ESP_FAIL);
    status = inspect_state(D1L_TIME_PROTOCOL_MIGRATION_STORAGE_ERROR,
                           ESP_FAIL);
    assert(status.write_blocked);
    assert(mock_nvs_set_call_count() == 0U);
}

static void assert_quarantine(
    d1l_time_protocol_migration_state_t expected_state,
    esp_err_t expected_error)
{
    d1l_time_protocol_migration_status_t status = inspect_state(
        expected_state, expected_error);
    assert(status.write_blocked);
    const size_t set_before = mock_nvs_set_call_count();
    const size_t commit_before = mock_nvs_commit_call_count();
    const size_t erase_before = mock_nvs_erase_call_count();
    bool written = true;
    assert(d1l_time_protocol_migration_run(
               TEST_LEGACY, TEST_UPPER,
               D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION,
               &written, &status) == expected_error);
    assert(!written);
    assert(status.state == expected_state);
    assert(mock_nvs_set_call_count() == set_before);
    assert(mock_nvs_commit_call_count() == commit_before);
    assert(mock_nvs_erase_call_count() == erase_before);
}

static void test_malformed_newer_checksum_and_domain_quarantine(void)
{
    mock_nvs_reset();
    const uint8_t malformed[] = {0x01U, 0x02U, 0x03U};
    assert(mock_nvs_seed_blob(
        D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
        D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY,
        malformed, sizeof(malformed)));
    assert_quarantine(D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                      ESP_ERR_INVALID_SIZE);

    mock_nvs_reset();
    migration_payload_t value = payload(
        D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT,
        D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    uint8_t blob[RECEIPT_BLOB_SIZE] = {0};
    size_t length = 0U;
    assert(d1l_settings_envelope_build(
        blob, sizeof(blob), &value, sizeof(value),
        D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION, &length));
    d1l_settings_envelope_header_t *header =
        (d1l_settings_envelope_header_t *)blob;
    header->schema_version = D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION + 1U;
    assert(mock_nvs_seed_blob(
        D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
        D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY, blob, length));
    assert_quarantine(
        D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
        ESP_ERR_NOT_SUPPORTED);

    mock_nvs_reset();
    header->schema_version = D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION;
    blob[sizeof(*header)] ^= 0x80U;
    assert(mock_nvs_seed_blob(
        D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
        D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY, blob, length));
    assert_quarantine(D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_CHECKSUM,
                      ESP_FAIL);

    mock_nvs_reset();
    value = payload(D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT,
                    D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    value.schema_version = D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION + 1U;
    seed_receipt(&value, D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    assert_quarantine(
        D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
        ESP_ERR_NOT_SUPPORTED);

    mock_nvs_reset();
    value = payload(D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT,
                    D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    value.domain_magic ^= 0x01000000U;
    seed_receipt(&value, D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    assert_quarantine(D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
                      ESP_ERR_INVALID_ARG);
    memset(blob, 0, sizeof(blob));
    memset(&value, 0, sizeof(value));
}

static void test_extended_future_receipts_are_preserved_as_newer(void)
{
    mock_nvs_reset();
    future_migration_payload_t future = {
        .prefix = payload(D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT, 3U),
        .extension_words = {
            0x46555431U, 0x46555432U, 0x46555433U, 0x46555434U,
        },
    };
    future.prefix.schema_version =
        D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION + 1U;
    uint8_t blob[sizeof(d1l_settings_envelope_header_t) +
                 sizeof(future)] = {0};
    size_t length = 0U;
    assert(d1l_settings_envelope_build(
        blob, sizeof(blob), &future, sizeof(future), 3U, &length));
    assert(length == sizeof(blob));
    assert(mock_nvs_seed_blob(
        D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
        D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY, blob, length));
    d1l_time_protocol_migration_status_t status = inspect_state(
        D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
        ESP_ERR_NOT_SUPPORTED);
    assert(status.receipt_found);
    assert(status.receipt_schema_version ==
           D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION + 1U);
    assert(status.revision == 3U);
    assert_quarantine(
        D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
        ESP_ERR_NOT_SUPPORTED);
    uint8_t preserved[sizeof(blob)] = {0};
    assert(mock_nvs_copy_blob(
               D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
               D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY,
               preserved, sizeof(preserved)) == sizeof(blob));
    assert(memcmp(preserved, blob, sizeof(blob)) == 0);

    mock_nvs_reset();
    d1l_settings_envelope_header_t *header =
        (d1l_settings_envelope_header_t *)blob;
    header->schema_version = D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION + 1U;
    assert(mock_nvs_seed_blob(
        D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE,
        D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY, blob, length));
    status = inspect_state(
        D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
        ESP_ERR_NOT_SUPPORTED);
    assert(status.receipt_found);
    assert(status.revision == 3U);
    assert_quarantine(
        D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
        ESP_ERR_NOT_SUPPORTED);
    memset(preserved, 0, sizeof(preserved));
    memset(blob, 0, sizeof(blob));
    memset(&future, 0, sizeof(future));
}

static void test_downgrade_receipts_and_state_are_quarantined(void)
{
    mock_nvs_reset();
    migration_payload_t value = payload(
        D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT,
        D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    value.schema_version = D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION - 1U;
    seed_receipt(&value, D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    assert_quarantine(D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                      ESP_ERR_INVALID_STATE);

    mock_nvs_reset();
    value = payload(D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE,
                    D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    seed_receipt(&value, D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION);
    assert_quarantine(D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                      ESP_ERR_INVALID_STATE);

    mock_nvs_reset();
    seed_complete();
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_LEGACY_KEY, TEST_LEGACY);
    assert_quarantine(D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                      ESP_ERR_INVALID_STATE);

    mock_nvs_reset();
    seed_intent(true, false);
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_HIGH_WATER_KEY, TEST_UPPER + 1U);
    assert_quarantine(D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                      ESP_ERR_INVALID_STATE);

    mock_nvs_reset();
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_LEGACY_KEY, TEST_LEGACY);
    seed_u32(D1L_TIME_PROTOCOL_NVS_NAMESPACE,
             D1L_TIME_PROTOCOL_HIGH_WATER_KEY, TEST_UPPER);
    assert_quarantine(D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
                      ESP_ERR_INVALID_STATE);
    memset(&value, 0, sizeof(value));
}

int main(void)
{
    test_absent_required_and_bounds();
    test_happy_and_idempotent();
    test_each_durable_power_cut_resume();
    test_commit_failures_model_every_power_cut_window();
    test_nvs_operation_failures_are_retry_safe();
    test_malformed_newer_checksum_and_domain_quarantine();
    test_extended_future_receipts_are_preserved_as_newer();
    test_downgrade_receipts_and_state_are_quarantined();
    puts("native legacy protocol migration: ok");
    return 0;
}
