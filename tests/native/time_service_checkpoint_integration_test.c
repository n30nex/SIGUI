#ifdef time
#undef time
#endif

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app/settings_envelope.h"
#include "app/settings_time_checkpoint.h"
#include "esp_netif_sntp.h"
#include "mock_esp_nvs.h"
#include "platform/time_service.h"

#define TEST_BUILD_EPOCH INT64_C(1784068276)
#define CHECKPOINT_NAMESPACE "d1l_time"
#define CHECKPOINT_KEY "wall_ckpt_v1"

static esp_err_t s_sntp_init_result = ESP_OK;
static esp_err_t s_sntp_wait_result = ESP_OK;
static bool s_sntp_wait_enabled;

time_t d1l_test_time(time_t *out_time)
{
    const time_t now = (time_t)(TEST_BUILD_EPOCH + 60);
    if (out_time) {
        *out_time = now;
    }
    return now;
}

#ifdef _WIN32
int settimeofday(const struct timeval *value, const void *timezone_value)
#else
int settimeofday(const struct timeval *value,
                 const struct timezone *timezone_value)
#endif
{
    (void)timezone_value;
    if (!value) {
        return -1;
    }
    return 0;
}

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{
    assert(config != NULL);
    s_sntp_wait_enabled = config->wait_for_sync;
    return s_sntp_init_result;
}

esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks_to_wait)
{
    assert(ticks_to_wait > 0U);
    assert(s_sntp_wait_enabled);
    return s_sntp_wait_result;
}

static void assert_checkpoint_blob_unchanged(
    const uint8_t *expected, size_t expected_length)
{
    uint8_t actual[128] = {0};
    const size_t actual_length = mock_nvs_copy_blob(
        CHECKPOINT_NAMESPACE, CHECKPOINT_KEY, actual, sizeof(actual));
    assert(actual_length == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);
}

static void queue_newer_companion_during_save(void)
{
    assert(d1l_time_service_set_companion_time(
               TEST_BUILD_EPOCH +
                   3 * D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC + 61,
               true) == ESP_OK);
}

static void run_happy_and_retry(void)
{
    mock_nvs_reset();
    mock_timer_set_us(0);
    assert(d1l_time_service_init() == ESP_OK);

    assert(d1l_time_service_wait_for_certificate_time(
               1000U, 100U, NULL, NULL) == ESP_OK);
    d1l_time_service_status_t status;
    d1l_time_service_status(&status);
    assert(status.wall_checkpoint_pending);
    assert(status.wall_checkpoint_write_count == 0U);
    assert(status.clock.wall_source == D1L_TIME_SOURCE_SNTP);
    assert(status.clock.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED);

    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_OK);
    d1l_time_service_status(&status);
    assert(!status.wall_checkpoint_pending);
    assert(status.wall_checkpoint_write_count == 1U);
    assert(status.wall_checkpoint.revision == 1U);
    assert(status.wall_checkpoint.protocol_reserved_through ==
           D1L_TIME_PROTOCOL_TIMESTAMP_BASE);

    assert(d1l_time_service_set_companion_time(
               TEST_BUILD_EPOCH + 61, true) == ESP_OK);
    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_OK);
    d1l_time_service_status(&status);
    assert(!status.wall_checkpoint_pending);
    assert(status.wall_checkpoint_write_count == 2U);
    assert(status.wall_checkpoint.revision == 2U);
    assert(status.wall_checkpoint.source ==
           D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED);

    assert(d1l_time_service_set_companion_time(
               TEST_BUILD_EPOCH + 62, true) == ESP_OK);
    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_OK);
    d1l_time_service_status(&status);
    assert(status.wall_checkpoint_skip_count == 1U);
    assert(status.wall_checkpoint.revision == 2U);

    assert(d1l_time_service_set_companion_time(
               TEST_BUILD_EPOCH +
                   D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC + 61,
               true) == ESP_OK);
    mock_nvs_fail_next_set(ESP_FAIL);
    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_FAIL);
    d1l_time_service_status(&status);
    assert(status.wall_checkpoint_pending);
    assert(status.wall_checkpoint_failure_count == 1U);
    assert(status.wall_checkpoint_retry_not_before_us == UINT64_C(30000000));

    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_OK);
    d1l_time_service_status(&status);
    assert(status.wall_checkpoint_pending);
    assert(status.wall_checkpoint_failure_count == 1U);
    mock_timer_set_us(INT64_C(30000000));
    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_OK);
    d1l_time_service_status(&status);
    assert(!status.wall_checkpoint_pending);
    assert(status.wall_checkpoint_write_count == 3U);
    assert(status.wall_checkpoint.revision == 3U);

    assert(d1l_time_service_set_companion_time(
               TEST_BUILD_EPOCH +
                   2 * D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC + 61,
               true) == ESP_OK);
    mock_nvs_run_during_next_set(queue_newer_companion_during_save);
    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_OK);
    d1l_time_service_status(&status);
    assert(status.wall_checkpoint_pending);
    assert(status.wall_checkpoint.revision == 4U);
    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_OK);
    d1l_time_service_status(&status);
    assert(!status.wall_checkpoint_pending);
    assert(status.wall_checkpoint.revision == 5U);
    assert(status.wall_checkpoint.epoch_sec ==
           TEST_BUILD_EPOCH +
               3 * D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC + 61);
}

static void run_corrupt_isolation(void)
{
    mock_nvs_reset();
    uint8_t blob[sizeof(d1l_settings_envelope_header_t) + 32U] = {0};
    uint8_t payload[32] = {0};
    payload[0] = 1U;
    size_t blob_length = 0U;
    assert(d1l_settings_envelope_build(
        blob, sizeof(blob), payload, sizeof(payload), 1U, &blob_length));
    blob[sizeof(d1l_settings_envelope_header_t)] ^= 0x80U;
    assert(mock_nvs_seed_blob(
        CHECKPOINT_NAMESPACE, CHECKPOINT_KEY, blob, blob_length));
    uint8_t before[sizeof(blob)] = {0};
    const size_t before_length = mock_nvs_copy_blob(
        CHECKPOINT_NAMESPACE, CHECKPOINT_KEY, before, sizeof(before));

    assert(d1l_time_service_init() == ESP_OK);
    d1l_time_service_status_t status;
    d1l_time_service_status(&status);
    assert(status.protocol_persistence_ready);
    assert(status.wall_checkpoint.state ==
           D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_CHECKSUM);
    assert(!status.wall_checkpoint_recovered);
    assert(status.wall_checkpoint_write_blocked);
    assert(!status.clock.wall_valid);

    assert(d1l_time_service_set_companion_time(
               TEST_BUILD_EPOCH + 60, true) == ESP_OK);
    assert(d1l_time_service_wall_checkpoint_flush_if_due() == ESP_OK);
    d1l_time_service_status(&status);
    assert(!status.wall_checkpoint_pending);
    assert(status.clock.certificate_time_valid);
    assert_checkpoint_blob_unchanged(before, before_length);
}

static void run_guard_rollback(void)
{
    mock_nvs_reset();
    const d1l_settings_time_checkpoint_t checkpoint = {
        .epoch_sec = TEST_BUILD_EPOCH + 100,
        .protocol_reserved_through =
            D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 64U,
        .source = D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP,
    };
    d1l_settings_time_checkpoint_status_t checkpoint_status;
    bool written = false;
    assert(d1l_settings_time_checkpoint_save(
               &checkpoint, &written, &checkpoint_status) == ESP_OK);
    assert(written);

    assert(d1l_time_service_init() == ESP_OK);
    d1l_time_service_status_t status;
    d1l_time_service_status(&status);
    assert(status.protocol_persistence_ready);
    assert(status.wall_checkpoint.state ==
           D1L_SETTINGS_TIME_CHECKPOINT_READY);
    assert(status.wall_checkpoint_recovery_error == ESP_ERR_INVALID_ARG);
    assert(!status.wall_checkpoint_recovered);
    assert(status.wall_checkpoint_write_blocked);
    assert(!status.clock.wall_valid);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "happy") == 0) {
        run_happy_and_retry();
    } else if (strcmp(argv[1], "corrupt") == 0) {
        run_corrupt_isolation();
    } else if (strcmp(argv[1], "guard") == 0) {
        run_guard_rollback();
    } else {
        assert(!"unknown scenario");
    }
    puts("native truthful-time checkpoint integration: ok");
    return 0;
}
