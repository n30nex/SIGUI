#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

#include "app/settings_envelope.h"
#include "app/settings_time_checkpoint.h"
#include "mock_esp_nvs.h"
#include "platform/time_service_core.h"

#define CHECKPOINT_NAMESPACE "d1l_time"
#define CHECKPOINT_KEY "wall_ckpt_v1"
#define CHECKPOINT_DOMAIN_MAGIC 0x314B4354UL
#define TEST_EPOCH \
    (D1L_TIME_WALL_MIN_EPOCH + \
     (D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC * INT64_C(4)))
#define TEST_GUARD \
    (D1L_TIME_PROTOCOL_TIMESTAMP_BASE + \
     (D1L_TIME_PROTOCOL_RESERVATION_SIZE * 4U))

typedef struct {
    uint32_t schema_version;
    uint32_t domain_magic;
    uint32_t revision;
    uint32_t source;
    int64_t epoch_sec;
    uint32_t protocol_reserved_through;
    uint32_t reserved;
} checkpoint_payload_t;

_Static_assert(sizeof(checkpoint_payload_t) == 32U,
               "test payload must mirror the stable checkpoint wire layout");

#define CHECKPOINT_BLOB_SIZE \
    (sizeof(d1l_settings_envelope_header_t) + sizeof(checkpoint_payload_t))

static d1l_settings_time_checkpoint_t checkpoint(
    int64_t epoch_sec,
    uint32_t protocol_reserved_through,
    d1l_settings_time_checkpoint_source_t source)
{
    return (d1l_settings_time_checkpoint_t) {
        .epoch_sec = epoch_sec,
        .protocol_reserved_through = protocol_reserved_through,
        .source = source,
    };
}

static checkpoint_payload_t valid_payload(uint32_t revision)
{
    return (checkpoint_payload_t) {
        .schema_version = D1L_SETTINGS_TIME_CHECKPOINT_SCHEMA_VERSION,
        .domain_magic = CHECKPOINT_DOMAIN_MAGIC,
        .revision = revision,
        .source = D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP,
        .epoch_sec = TEST_EPOCH,
        .protocol_reserved_through = TEST_GUARD,
        .reserved = 0U,
    };
}

static size_t build_blob(const checkpoint_payload_t *payload,
                         uint32_t envelope_revision,
                         uint8_t blob[CHECKPOINT_BLOB_SIZE])
{
    size_t length = 0U;
    assert(d1l_settings_envelope_build(
        blob, CHECKPOINT_BLOB_SIZE, payload, sizeof(*payload),
        envelope_revision, &length));
    assert(length == CHECKPOINT_BLOB_SIZE);
    return length;
}

static size_t copy_committed(uint8_t blob[CHECKPOINT_BLOB_SIZE])
{
    return mock_nvs_copy_blob(CHECKPOINT_NAMESPACE, CHECKPOINT_KEY, blob,
                              CHECKPOINT_BLOB_SIZE);
}

static void assert_committed_equals(const uint8_t *expected,
                                    size_t expected_length)
{
    uint8_t actual[CHECKPOINT_BLOB_SIZE] = {0};
    const size_t actual_length = copy_committed(actual);
    assert(actual_length == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);
}

static void assert_ready_checkpoint(
    const d1l_settings_time_checkpoint_t *expected,
    uint32_t expected_revision)
{
    d1l_settings_time_checkpoint_t loaded = {0};
    d1l_settings_time_checkpoint_status_t status = {0};
    assert(d1l_settings_time_checkpoint_load(&loaded, &status) == ESP_OK);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_READY);
    assert(status.error == ESP_OK);
    assert(status.found);
    assert(status.revision == expected_revision);
    assert(status.epoch_sec == expected->epoch_sec);
    assert(status.protocol_reserved_through ==
           expected->protocol_reserved_through);
    assert(status.source == expected->source);
    assert(loaded.epoch_sec == expected->epoch_sec);
    assert(loaded.protocol_reserved_through ==
           expected->protocol_reserved_through);
    assert(loaded.source == expected->source);
}

static void test_absent_and_invalid_inputs(void)
{
    mock_nvs_reset();
    d1l_settings_time_checkpoint_t loaded = {
        .epoch_sec = INT64_C(99),
        .protocol_reserved_through = 99U,
        .source = D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP,
    };
    d1l_settings_time_checkpoint_status_t status = {
        .state = D1L_SETTINGS_TIME_CHECKPOINT_READY,
        .error = ESP_FAIL,
        .found = true,
    };
    assert(d1l_settings_time_checkpoint_load(&loaded, &status) == ESP_OK);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_ABSENT);
    assert(status.error == ESP_OK);
    assert(!status.found);
    assert(status.revision == 0U);
    assert(loaded.epoch_sec == 0);
    assert(loaded.protocol_reserved_through == 0U);
    assert(loaded.source == D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_NONE);
    assert(copy_committed(NULL) == 0U);

    assert(d1l_settings_time_checkpoint_load(NULL, &status) ==
           ESP_ERR_INVALID_ARG);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_UNINITIALIZED);
    assert(status.error == ESP_ERR_INVALID_STATE);
    assert(d1l_settings_time_checkpoint_load(&loaded, NULL) ==
           ESP_ERR_INVALID_ARG);
    assert(loaded.epoch_sec == 0);

    bool written = true;
    assert(d1l_settings_time_checkpoint_save(NULL, &written, &status) ==
           ESP_ERR_INVALID_ARG);
    assert(!written);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_UNINITIALIZED);

    d1l_settings_time_checkpoint_t candidate = checkpoint(
        D1L_TIME_WALL_MIN_EPOCH - 1, TEST_GUARD,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, &status) == ESP_ERR_INVALID_ARG);
    assert(!written);
    assert(copy_committed(NULL) == 0U);

    candidate = checkpoint(
        TEST_EPOCH, D1L_TIME_PROTOCOL_TIMESTAMP_BASE - 1U,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, &status) == ESP_ERR_INVALID_ARG);
    candidate = checkpoint(TEST_EPOCH, TEST_GUARD,
                           D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_NONE);
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, &status) == ESP_ERR_INVALID_ARG);
    candidate.source =
        (d1l_settings_time_checkpoint_source_t)UINT32_C(99);
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, &status) == ESP_ERR_INVALID_ARG);

    candidate = checkpoint(TEST_EPOCH, TEST_GUARD,
                           D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &candidate, NULL, &status) == ESP_ERR_INVALID_ARG);
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, NULL) == ESP_ERR_INVALID_ARG);
    assert(!written);
    assert(copy_committed(NULL) == 0U);

    assert(strcmp(d1l_settings_time_checkpoint_state_name(
                      D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_CHECKSUM),
                  "quarantined_checksum") == 0);
    assert(strcmp(d1l_settings_time_checkpoint_source_name(
                      D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED),
                  "companion_authenticated") == 0);
}

static void test_first_save_load_and_bounded_skips(void)
{
    mock_nvs_reset();
    const d1l_settings_time_checkpoint_t initial = checkpoint(
        TEST_EPOCH, TEST_GUARD,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    bool written = false;
    d1l_settings_time_checkpoint_status_t status = {0};
    assert(d1l_settings_time_checkpoint_save(
               &initial, &written, &status) == ESP_OK);
    assert(written);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_READY);
    assert(status.revision == 1U);
    assert(mock_nvs_set_call_count() == 1U);
    assert(mock_nvs_commit_call_count() == 1U);
    assert_ready_checkpoint(&initial, 1U);

    uint8_t first_blob[CHECKPOINT_BLOB_SIZE] = {0};
    const size_t first_length = copy_committed(first_blob);
    assert(first_length == CHECKPOINT_BLOB_SIZE);

    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &initial, &written, &status) == ESP_OK);
    assert(!written);
    assert(status.revision == 1U);
    assert(mock_nvs_set_call_count() == 1U);
    assert(mock_nvs_commit_call_count() == 1U);
    assert_committed_equals(first_blob, first_length);

    d1l_settings_time_checkpoint_t candidate = initial;
    candidate.epoch_sec +=
        D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC - 1;
    candidate.protocol_reserved_through +=
        D1L_TIME_PROTOCOL_RESERVATION_SIZE;
    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, &status) == ESP_OK);
    assert(!written);
    assert(status.revision == 1U);
    assert(status.epoch_sec == initial.epoch_sec);
    assert(status.protocol_reserved_through ==
           initial.protocol_reserved_through);
    assert(mock_nvs_set_call_count() == 1U);
    assert(mock_nvs_commit_call_count() == 1U);
    assert_committed_equals(first_blob, first_length);

    candidate = initial;
    candidate.epoch_sec -=
        D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC - 1;
    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, &status) == ESP_OK);
    assert(!written);
    assert(status.revision == 1U);
    assert(mock_nvs_set_call_count() == 1U);
    assert(mock_nvs_commit_call_count() == 1U);
    assert_committed_equals(first_blob, first_length);
    assert_ready_checkpoint(&initial, 1U);
}

static void test_exact_forward_and_backward_thresholds(void)
{
    mock_nvs_reset();
    const d1l_settings_time_checkpoint_t initial = checkpoint(
        TEST_EPOCH, TEST_GUARD,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    bool written = false;
    d1l_settings_time_checkpoint_status_t status = {0};
    assert(d1l_settings_time_checkpoint_save(
               &initial, &written, &status) == ESP_OK);
    assert(written && status.revision == 1U);

    d1l_settings_time_checkpoint_t forward = initial;
    forward.epoch_sec += D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC;
    forward.protocol_reserved_through +=
        D1L_TIME_PROTOCOL_RESERVATION_SIZE;
    written = false;
    assert(d1l_settings_time_checkpoint_save(
               &forward, &written, &status) == ESP_OK);
    assert(written && status.revision == 2U);
    assert_ready_checkpoint(&forward, 2U);

    d1l_settings_time_checkpoint_t backward = forward;
    backward.epoch_sec -= D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC;
    backward.protocol_reserved_through +=
        D1L_TIME_PROTOCOL_RESERVATION_SIZE;
    written = false;
    assert(d1l_settings_time_checkpoint_save(
               &backward, &written, &status) == ESP_OK);
    assert(written && status.revision == 3U);
    assert_ready_checkpoint(&backward, 3U);
}

static void test_stronger_companion_can_correct_downward_immediately(void)
{
    mock_nvs_reset();
    const d1l_settings_time_checkpoint_t initial = checkpoint(
        TEST_EPOCH, TEST_GUARD,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    bool written = false;
    d1l_settings_time_checkpoint_status_t status = {0};
    assert(d1l_settings_time_checkpoint_save(
               &initial, &written, &status) == ESP_OK);

    const d1l_settings_time_checkpoint_t corrected = checkpoint(
        TEST_EPOCH - 1, TEST_GUARD + D1L_TIME_PROTOCOL_RESERVATION_SIZE,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED);
    written = false;
    assert(d1l_settings_time_checkpoint_save(
               &corrected, &written, &status) == ESP_OK);
    assert(written);
    assert(status.revision == 2U);
    assert_ready_checkpoint(&corrected, 2U);

    d1l_settings_time_checkpoint_t weaker_same_time = corrected;
    weaker_same_time.source = D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP;
    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &weaker_same_time, &written, &status) == ESP_OK);
    assert(!written);
    assert(status.revision == 2U);
    assert_ready_checkpoint(&corrected, 2U);
}

static void assert_bad_blob_preserved(
    const uint8_t *blob,
    size_t blob_length,
    esp_err_t expected_error,
    d1l_settings_time_checkpoint_state_t expected_state,
    uint32_t expected_revision)
{
    mock_nvs_reset();
    assert(mock_nvs_seed_blob(CHECKPOINT_NAMESPACE, CHECKPOINT_KEY,
                              blob, blob_length));
    d1l_settings_time_checkpoint_t loaded = {0};
    d1l_settings_time_checkpoint_status_t status = {0};
    assert(d1l_settings_time_checkpoint_load(&loaded, &status) ==
           expected_error);
    assert(status.state == expected_state);
    assert(status.error == expected_error);
    assert(status.found);
    assert(status.revision == expected_revision);
    assert(loaded.epoch_sec == 0);
    assert_committed_equals(blob, blob_length);

    const d1l_settings_time_checkpoint_t candidate = checkpoint(
        TEST_EPOCH + D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC,
        TEST_GUARD + D1L_TIME_PROTOCOL_RESERVATION_SIZE,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED);
    bool written = true;
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, &status) == expected_error);
    assert(!written);
    assert(status.state == expected_state);
    assert_committed_equals(blob, blob_length);
}

static void assert_semantic_payload_quarantined(checkpoint_payload_t payload,
                                                uint32_t envelope_revision,
                                                esp_err_t expected_error,
                                                d1l_settings_time_checkpoint_state_t expected_state)
{
    uint8_t blob[CHECKPOINT_BLOB_SIZE] = {0};
    const size_t length = build_blob(&payload, envelope_revision, blob);
    assert_bad_blob_preserved(blob, length, expected_error, expected_state,
                              envelope_revision);
}

static void test_future_corrupt_and_malformed_data_are_preserved(void)
{
    checkpoint_payload_t payload = valid_payload(7U);
    uint8_t blob[CHECKPOINT_BLOB_SIZE] = {0};
    size_t length = build_blob(&payload, 7U, blob);

    d1l_settings_envelope_header_t header = {0};
    memcpy(&header, blob, sizeof(header));
    header.schema_version = D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION + 1U;
    memcpy(blob, &header, sizeof(header));
    assert_bad_blob_preserved(
        blob, length, ESP_ERR_NOT_SUPPORTED,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_NEWER_SCHEMA, 7U);

    payload = valid_payload(8U);
    payload.schema_version =
        D1L_SETTINGS_TIME_CHECKPOINT_SCHEMA_VERSION + 1U;
    assert_semantic_payload_quarantined(
        payload, 8U, ESP_ERR_NOT_SUPPORTED,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_NEWER_SCHEMA);

    payload = valid_payload(9U);
    length = build_blob(&payload, 9U, blob);
    blob[sizeof(d1l_settings_envelope_header_t) + 17U] ^= 0x80U;
    assert_bad_blob_preserved(
        blob, length, ESP_FAIL,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_CHECKSUM, 9U);

    payload = valid_payload(10U);
    payload.domain_magic ^= 1U;
    assert_semantic_payload_quarantined(
        payload, 10U, ESP_ERR_INVALID_ARG,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED);

    payload = valid_payload(10U);
    payload.revision = 9U;
    assert_semantic_payload_quarantined(
        payload, 10U, ESP_ERR_INVALID_ARG,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED);

    payload = valid_payload(11U);
    payload.reserved = 1U;
    assert_semantic_payload_quarantined(
        payload, 11U, ESP_ERR_INVALID_ARG,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED);

    payload = valid_payload(12U);
    payload.source = UINT32_C(99);
    assert_semantic_payload_quarantined(
        payload, 12U, ESP_ERR_INVALID_ARG,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED);

    payload = valid_payload(13U);
    payload.protocol_reserved_through =
        D1L_TIME_PROTOCOL_TIMESTAMP_BASE - 1U;
    assert_semantic_payload_quarantined(
        payload, 13U, ESP_ERR_INVALID_ARG,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED);

    payload = valid_payload(14U);
    payload.epoch_sec = D1L_TIME_WALL_MIN_EPOCH - 1;
    assert_semantic_payload_quarantined(
        payload, 14U, ESP_ERR_INVALID_ARG,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED);

    payload = valid_payload(15U);
    length = build_blob(&payload, 15U, blob);
    memcpy(&header, blob, sizeof(header));
    header.reserved = 1U;
    memcpy(blob, &header, sizeof(header));
    assert_bad_blob_preserved(
        blob, length, ESP_ERR_INVALID_SIZE,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED, 15U);

    payload = valid_payload(16U);
    length = build_blob(&payload, 16U, blob);
    assert_bad_blob_preserved(
        blob, length - 1U, ESP_ERR_INVALID_SIZE,
        D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED, 16U);
}

static void test_revision_saturation_preserves_valid_blob(void)
{
    checkpoint_payload_t payload = valid_payload(UINT32_MAX);
    uint8_t blob[CHECKPOINT_BLOB_SIZE] = {0};
    const size_t length = build_blob(&payload, UINT32_MAX, blob);
    mock_nvs_reset();
    assert(mock_nvs_seed_blob(CHECKPOINT_NAMESPACE, CHECKPOINT_KEY,
                              blob, length));

    const d1l_settings_time_checkpoint_t candidate = checkpoint(
        TEST_EPOCH + D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC,
        TEST_GUARD + D1L_TIME_PROTOCOL_RESERVATION_SIZE,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    bool written = true;
    d1l_settings_time_checkpoint_status_t status = {0};
    assert(d1l_settings_time_checkpoint_save(
               &candidate, &written, &status) == ESP_ERR_INVALID_STATE);
    assert(!written);
    assert(status.state ==
           D1L_SETTINGS_TIME_CHECKPOINT_REVISION_SATURATED);
    assert(status.error == ESP_ERR_INVALID_STATE);
    assert(status.found);
    assert(status.revision == UINT32_MAX);
    assert(status.epoch_sec == TEST_EPOCH);
    assert_committed_equals(blob, length);
}

static void test_set_commit_and_power_loss_preserve_old_or_new(void)
{
    mock_nvs_reset();
    const d1l_settings_time_checkpoint_t old_checkpoint = checkpoint(
        TEST_EPOCH, TEST_GUARD,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    const d1l_settings_time_checkpoint_t new_checkpoint = checkpoint(
        TEST_EPOCH + D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC,
        TEST_GUARD + D1L_TIME_PROTOCOL_RESERVATION_SIZE,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    bool written = false;
    d1l_settings_time_checkpoint_status_t status = {0};

    mock_nvs_fail_next_commit(ESP_FAIL);
    assert(d1l_settings_time_checkpoint_save(
               &old_checkpoint, &written, &status) == ESP_FAIL);
    assert(!written);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR);
    assert(copy_committed(NULL) == 0U);
    d1l_settings_time_checkpoint_t missing = {0};
    assert(d1l_settings_time_checkpoint_load(&missing, &status) == ESP_OK);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_ABSENT);
    assert(!status.found);

    mock_nvs_reset();
    assert(d1l_settings_time_checkpoint_save(
               &old_checkpoint, &written, &status) == ESP_OK);

    uint8_t old_blob[CHECKPOINT_BLOB_SIZE] = {0};
    const size_t old_length = copy_committed(old_blob);
    assert(old_length == CHECKPOINT_BLOB_SIZE);

    mock_nvs_fail_next_set(ESP_ERR_NO_MEM);
    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &new_checkpoint, &written, &status) == ESP_ERR_NO_MEM);
    assert(!written);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR);
    assert(status.revision == 1U);
    assert_committed_equals(old_blob, old_length);
    assert_ready_checkpoint(&old_checkpoint, 1U);

    mock_nvs_fail_next_commit(ESP_FAIL);
    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &new_checkpoint, &written, &status) == ESP_FAIL);
    assert(!written);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR);
    assert(status.revision == 1U);
    assert_committed_equals(old_blob, old_length);
    assert_ready_checkpoint(&old_checkpoint, 1U);

    mock_nvs_fail_open_after(1U, ESP_ERR_NO_MEM);
    written = true;
    assert(d1l_settings_time_checkpoint_save(
               &new_checkpoint, &written, &status) == ESP_ERR_NO_MEM);
    assert(!written);
    assert(status.state == D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR);
    assert_committed_equals(old_blob, old_length);

    written = false;
    assert(d1l_settings_time_checkpoint_save(
               &new_checkpoint, &written, &status) == ESP_OK);
    assert(written);
    assert(status.revision == 2U);
    uint8_t new_blob[CHECKPOINT_BLOB_SIZE] = {0};
    const size_t new_length = copy_committed(new_blob);
    assert(new_length == old_length);
    assert(memcmp(new_blob, old_blob, old_length) != 0);
    assert_ready_checkpoint(&new_checkpoint, 2U);
}

typedef struct {
    d1l_settings_time_checkpoint_t candidate;
    esp_err_t result;
    bool written;
    d1l_settings_time_checkpoint_status_t status;
} concurrent_writer_t;

static atomic_int s_concurrent_stage;

static void concurrent_yield(void)
{
#ifdef _WIN32
    (void)SwitchToThread();
#else
    (void)sched_yield();
#endif
}

static void hold_first_candidate_inside_set(void)
{
    atomic_store_explicit(&s_concurrent_stage, 1, memory_order_release);
    while (atomic_load_explicit(&s_concurrent_stage,
                                memory_order_acquire) < 3) {
        concurrent_yield();
    }
}

static void mark_second_candidate_waiting(void)
{
    atomic_store_explicit(&s_concurrent_stage, 2, memory_order_release);
}

static void execute_concurrent_writer(concurrent_writer_t *writer)
{
    writer->result = d1l_settings_time_checkpoint_save(
        &writer->candidate, &writer->written, &writer->status);
}

#ifdef _WIN32
typedef HANDLE concurrent_thread_t;

static DWORD WINAPI run_concurrent_writer(LPVOID context)
{
    execute_concurrent_writer((concurrent_writer_t *)context);
    return 0U;
}

static bool start_concurrent_thread(concurrent_thread_t *thread,
                                    concurrent_writer_t *writer)
{
    *thread = CreateThread(NULL, 0U, run_concurrent_writer, writer, 0U, NULL);
    return *thread != NULL;
}

static bool join_concurrent_thread(concurrent_thread_t thread)
{
    const DWORD result = WaitForSingleObject(thread, INFINITE);
    const bool closed = CloseHandle(thread) != 0;
    return result == WAIT_OBJECT_0 && closed;
}
#else
typedef pthread_t concurrent_thread_t;

static void *run_concurrent_writer(void *context)
{
    execute_concurrent_writer((concurrent_writer_t *)context);
    return NULL;
}

static bool start_concurrent_thread(concurrent_thread_t *thread,
                                    concurrent_writer_t *writer)
{
    return pthread_create(thread, NULL, run_concurrent_writer, writer) == 0;
}

static bool join_concurrent_thread(concurrent_thread_t thread)
{
    return pthread_join(thread, NULL) == 0;
}
#endif

static void test_concurrent_candidates_are_serialized(void)
{
    mock_nvs_reset();
    const d1l_settings_time_checkpoint_t initial = checkpoint(
        TEST_EPOCH, TEST_GUARD,
        D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP);
    bool written = false;
    d1l_settings_time_checkpoint_status_t status = {0};
    assert(d1l_settings_time_checkpoint_save(
               &initial, &written, &status) == ESP_OK);

    concurrent_writer_t forward = {
        .candidate = {
            .epoch_sec = TEST_EPOCH +
                D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC,
            .protocol_reserved_through =
                TEST_GUARD + D1L_TIME_PROTOCOL_RESERVATION_SIZE,
            .source = D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP,
        },
        .result = ESP_ERR_INVALID_STATE,
    };
    concurrent_writer_t stronger_correction = {
        .candidate = {
            .epoch_sec = TEST_EPOCH - 1,
            .protocol_reserved_through =
                TEST_GUARD + (D1L_TIME_PROTOCOL_RESERVATION_SIZE * 2U),
            .source =
                D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED,
        },
        .result = ESP_ERR_INVALID_STATE,
    };

    atomic_store_explicit(&s_concurrent_stage, 0, memory_order_release);
    mock_nvs_run_during_next_set(hold_first_candidate_inside_set);
    concurrent_thread_t first_thread;
    concurrent_thread_t second_thread;
    assert(start_concurrent_thread(&first_thread, &forward));
    while (atomic_load_explicit(&s_concurrent_stage,
                                memory_order_acquire) < 1) {
        concurrent_yield();
    }
    mock_semaphore_run_after_takes(1U, mark_second_candidate_waiting);
    assert(start_concurrent_thread(&second_thread, &stronger_correction));
    while (atomic_load_explicit(&s_concurrent_stage,
                                memory_order_acquire) < 2) {
        concurrent_yield();
    }
    atomic_store_explicit(&s_concurrent_stage, 3, memory_order_release);
    assert(join_concurrent_thread(first_thread));
    assert(join_concurrent_thread(second_thread));

    assert(forward.result == ESP_OK);
    assert(forward.written);
    assert(forward.status.revision == 2U);
    assert(stronger_correction.result == ESP_OK);
    assert(stronger_correction.written);
    assert(stronger_correction.status.revision == 3U);
    assert_ready_checkpoint(&stronger_correction.candidate, 3U);
}

int main(void)
{
    test_absent_and_invalid_inputs();
    test_first_save_load_and_bounded_skips();
    test_exact_forward_and_backward_thresholds();
    test_stronger_companion_can_correct_downward_immediately();
    test_future_corrupt_and_malformed_data_are_preserved();
    test_revision_saturation_preserves_valid_blob();
    test_set_commit_and_power_loss_preserve_old_or_new();
    test_concurrent_candidates_are_serialized();
    puts("native retained time checkpoint: ok");
    return 0;
}
