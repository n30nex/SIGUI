#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mesh/meshcore_trace.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,          \
                    __LINE__, #condition);                                      \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static size_t build_trace_frame(uint32_t tag, uint32_t auth_code,
                                uint8_t flags,
                                const uint8_t *explicit_path,
                                size_t explicit_path_bytes,
                                const int8_t *snrs,
                                size_t outer_hops,
                                uint8_t *raw)
{
    size_t index = 0U;
    raw[index++] = (uint8_t)((D1L_MESHCORE_PAYLOAD_TRACE << 2U) |
                             D1L_MESHCORE_ROUTE_DIRECT);
    raw[index++] = (uint8_t)outer_hops;
    memcpy(&raw[index], snrs, outer_hops);
    index += outer_hops;
    d1l_meshcore_trace_write_le32(&raw[index], tag);
    index += 4U;
    d1l_meshcore_trace_write_le32(&raw[index], auth_code);
    index += 4U;
    raw[index++] = flags;
    memcpy(&raw[index], explicit_path, explicit_path_bytes);
    return index + explicit_path_bytes;
}

static size_t build_terminal(uint32_t tag, uint32_t auth_code,
                             const uint8_t *hashes, const int8_t *snrs,
                             size_t hops, uint8_t *raw)
{
    return build_trace_frame(tag, auth_code, 0U, hashes, hops, snrs, hops,
                             raw);
}

static int test_contact_plan(void)
{
    const uint8_t direct_path[] = {0x11U, 0x22U, 0x33U};
    const uint8_t original_path[] = {0x11U, 0x22U, 0x33U};
    d1l_meshcore_contact_trace_plan_t plan = {0};
    CHECK(d1l_meshcore_trace_plan_contact(
              direct_path, 3U, false, 0x44U, &plan) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_OK);
    const uint8_t chat_expected[] = {
        0x11U, 0x22U, 0x33U, 0x22U, 0x11U,
    };
    CHECK(!plan.includes_contact);
    CHECK(plan.path_hops == sizeof(chat_expected));
    CHECK(memcmp(plan.path_hashes, chat_expected,
                 sizeof(chat_expected)) == 0);

    memset(&plan, 0, sizeof(plan));
    CHECK(d1l_meshcore_trace_plan_contact(
              direct_path, 3U, true, 0x44U, &plan) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_OK);
    const uint8_t server_expected[] = {
        0x11U, 0x22U, 0x33U, 0x44U,
        0x33U, 0x22U, 0x11U,
    };
    CHECK(plan.includes_contact);
    CHECK(plan.path_hops == sizeof(server_expected));
    CHECK(memcmp(plan.path_hashes, server_expected,
                 sizeof(server_expected)) == 0);
    CHECK(memcmp(direct_path, original_path, sizeof(direct_path)) == 0);

    memset(&plan, 0, sizeof(plan));
    CHECK(d1l_meshcore_trace_plan_contact(
              NULL, 0U, true, 0x7AU, &plan) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_OK);
    CHECK(plan.includes_contact && plan.path_hops == 1U &&
          plan.path_hashes[0] == 0x7AU);

    d1l_meshcore_contact_trace_plan_t unchanged;
    memset(&unchanged, 0xA5, sizeof(unchanged));
    const d1l_meshcore_contact_trace_plan_t before = unchanged;
    CHECK(d1l_meshcore_trace_plan_contact(
              NULL, 0U, false, 0U, &unchanged) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_EMPTY);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);
    CHECK(d1l_meshcore_trace_plan_contact(
              direct_path, (uint8_t)(0x40U | 3U), true, 0x44U,
              &unchanged) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_UNSUPPORTED_WIDTH);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);
    CHECK(d1l_meshcore_trace_plan_contact(
              direct_path, 3U, true, 0x44U, NULL) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_INVALID);
    CHECK(d1l_meshcore_trace_plan_contact(
              NULL, 1U, false, 0U, &unchanged) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_INVALID);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);

    uint8_t long_path[32];
    for (size_t i = 0U; i < sizeof(long_path); ++i) {
        long_path[i] = (uint8_t)(i + 1U);
    }
    CHECK(d1l_meshcore_trace_plan_contact(
              long_path, 32U, false, 0U, &plan) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_OK);
    CHECK(plan.path_hops == D1L_MESHCORE_TRACE_MAX_HOPS);
    CHECK(d1l_meshcore_trace_plan_contact(
              long_path, 32U, true, 0x55U, &unchanged) ==
          D1L_MESHCORE_CONTACT_TRACE_PLAN_TOO_LONG);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);
    return 0;
}

static int test_source_and_terminal(void)
{
    CHECK(strcmp(D1L_MESHCORE_TRACE_RETAINED_TARGET, "trace_last") == 0);
    const char *first_target =
        d1l_meshcore_trace_retained_target_for_tag(0x11111111U);
    const char *second_target =
        d1l_meshcore_trace_retained_target_for_tag(0x22222222U);
    CHECK(strcmp(first_target, "trace_last") == 0);
    CHECK(strcmp(first_target, second_target) == 0);

    uint8_t hashes[] = {0x11U, 0x22U, 0x33U};
    const uint8_t original_hashes[] = {0x11U, 0x22U, 0x33U};
    d1l_meshcore_trace_source_t source = {0};
    CHECK(d1l_meshcore_trace_build_source(
        0x12345678U, 0xA1B2C3D4U, hashes, 3U, &source));
    const uint8_t expected[] = {
        0x26U, 0x00U, 0x78U, 0x56U, 0x34U, 0x12U,
        0xD4U, 0xC3U, 0xB2U, 0xA1U, 0x00U, 0x11U,
        0x22U, 0x33U,
    };
    CHECK(source.raw_len == sizeof(expected));
    CHECK(memcmp(source.raw, expected, sizeof(expected)) == 0);
    hashes[0] = 0xEEU;
    CHECK(memcmp(source.path_hashes, original_hashes,
                 sizeof(original_hashes)) == 0);
    CHECK(source.raw[11] == 0x11U);

    d1l_meshcore_trace_terminal_t unchanged;
    memset(&unchanged, 0xA5, sizeof(unchanged));
    const d1l_meshcore_trace_terminal_t before = unchanged;
    CHECK(d1l_meshcore_trace_classify(source.raw, source.raw_len,
                                      &unchanged) ==
          D1L_MESHCORE_TRACE_FRAME_SOURCE);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);

    CHECK(!d1l_meshcore_trace_build_source(1U, 2U, NULL, 1U, &source));
    CHECK(!d1l_meshcore_trace_build_source(
        1U, 2U, original_hashes, D1L_MESHCORE_TRACE_MAX_HOPS + 1U,
        &source));
    CHECK(!d1l_meshcore_trace_build_source(1U, 2U, original_hashes, 1U,
                                           NULL));

    const int8_t snrs[] = {-4, 8, 127};
    uint8_t raw[D1L_MESHCORE_MAX_RAW_PACKET] = {0};
    size_t raw_len = build_trace_frame(
        0x12345678U, 0xA1B2C3D4U, 0U, original_hashes, 3U, snrs, 1U,
        raw);
    CHECK(d1l_meshcore_trace_classify(raw, raw_len, &unchanged) ==
          D1L_MESHCORE_TRACE_FRAME_IN_FLIGHT);
    CHECK(!d1l_meshcore_trace_parse_terminal(raw, raw_len, &unchanged));
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);

    const uint8_t two_byte_hashes[] = {0x11U, 0x12U, 0x22U, 0x23U};
    raw_len = build_trace_frame(
        1U, 2U, 1U, two_byte_hashes, sizeof(two_byte_hashes), snrs, 1U,
        raw);
    CHECK(d1l_meshcore_trace_classify(raw, raw_len, &unchanged) ==
          D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);

    raw_len = build_trace_frame(
        1U, 2U, 0U, original_hashes, 0U, snrs, 1U, raw);
    CHECK(d1l_meshcore_trace_classify(raw, raw_len, &unchanged) ==
          D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);

    raw_len = build_trace_frame(
        1U, 2U, 0U, original_hashes, 0U, snrs, 0U, raw);
    CHECK(d1l_meshcore_trace_classify(raw, raw_len, &unchanged) ==
          D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);

    const int8_t four_snrs[] = {-4, 8, 12, 16};
    raw_len = build_trace_frame(
        1U, 2U, 0U, original_hashes, 3U, four_snrs, 4U, raw);
    CHECK(d1l_meshcore_trace_classify(raw, raw_len, &unchanged) ==
          D1L_MESHCORE_TRACE_FRAME_MALFORMED);
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);

    raw_len = build_terminal(
        0x12345678U, 0xA1B2C3D4U, original_hashes, snrs, 3U, raw);
    d1l_meshcore_trace_terminal_t terminal = {0};
    CHECK(d1l_meshcore_trace_classify(raw, raw_len, NULL) ==
          D1L_MESHCORE_TRACE_FRAME_TERMINAL);
    CHECK(d1l_meshcore_trace_parse_terminal(raw, raw_len, &terminal));
    CHECK(terminal.tag == 0x12345678U);
    CHECK(terminal.auth_code == 0xA1B2C3D4U);
    CHECK(terminal.path_hops == 3U);
    CHECK(memcmp(terminal.path_hashes, original_hashes, 3U) == 0);
    CHECK(memcmp(terminal.path_snrs_quarter_db, snrs, 3U) == 0);
    raw[2] = 0;
    raw[raw_len - 1U] = 0;
    CHECK(terminal.path_snrs_quarter_db[0] == -4);
    CHECK(terminal.path_hashes[2] == 0x33U);

    CHECK(!d1l_meshcore_trace_parse_terminal(source.raw, source.raw_len,
                                              &unchanged));
    CHECK(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);
    CHECK(!d1l_meshcore_trace_parse_terminal(NULL, raw_len, &unchanged));
    CHECK(!d1l_meshcore_trace_parse_terminal(raw, raw_len, NULL));

    const size_t canonical_len = build_terminal(
        1U, 2U, original_hashes, snrs, 3U, raw);
    raw[0] = (uint8_t)((D1L_MESHCORE_PAYLOAD_TRACE << 2U) |
                       D1L_MESHCORE_ROUTE_FLOOD);
    CHECK(!d1l_meshcore_trace_parse_terminal(raw, canonical_len, &unchanged));
    raw[0] = (uint8_t)((D1L_MESHCORE_PAYLOAD_TRACE << 2U) |
                       D1L_MESHCORE_ROUTE_DIRECT | 0x40U);
    CHECK(!d1l_meshcore_trace_parse_terminal(raw, canonical_len, &unchanged));
    raw[0] = (uint8_t)((D1L_MESHCORE_PAYLOAD_TEXT << 2U) |
                       D1L_MESHCORE_ROUTE_DIRECT);
    CHECK(!d1l_meshcore_trace_parse_terminal(raw, canonical_len, &unchanged));
    (void)build_terminal(1U, 2U, original_hashes, snrs, 3U, raw);
    raw[2U + 3U + 8U] = 1U;
    CHECK(!d1l_meshcore_trace_parse_terminal(raw, canonical_len, &unchanged));
    (void)build_terminal(1U, 2U, original_hashes, snrs, 3U, raw);
    raw[1] = 2U;
    CHECK(!d1l_meshcore_trace_parse_terminal(raw, canonical_len, &unchanged));
    CHECK(!d1l_meshcore_trace_parse_terminal(raw, 10U, &unchanged));
    return 0;
}

static int test_tracker(void)
{
    const uint8_t hashes[] = {0x10U, 0x20U};
    uint8_t mutable_hashes[] = {0x10U, 0x20U};
    d1l_meshcore_trace_tracker_t tracker = {0};
    CHECK(d1l_meshcore_trace_tracker_begin(
        &tracker, 7U, 9U, mutable_hashes, 2U, 100U));
    mutable_hashes[0] = 0xFFU;
    CHECK(tracker.pending_path_hashes[0] == 0x10U);
    CHECK(!d1l_meshcore_trace_tracker_begin(
        &tracker, 8U, 10U, hashes, 2U, 101U));

    d1l_meshcore_trace_terminal_t terminal = {
        .tag = 8U,
        .auth_code = 9U,
        .path_hops = 2U,
        .path_hashes = {0x10U, 0x20U},
        .path_snrs_quarter_db = {-1, 2},
    };
    CHECK(d1l_meshcore_trace_tracker_consume(&tracker, &terminal, 110U) ==
          D1L_MESHCORE_TRACE_CORRELATION_UNMATCHED);
    CHECK(tracker.pending);
    terminal.tag = 7U;
    terminal.auth_code = 10U;
    CHECK(d1l_meshcore_trace_tracker_consume(&tracker, &terminal, 111U) ==
          D1L_MESHCORE_TRACE_CORRELATION_AUTH_MISMATCH);
    CHECK(tracker.pending);
    terminal.auth_code = 9U;
    terminal.path_hashes[1] = 0x21U;
    CHECK(d1l_meshcore_trace_tracker_consume(&tracker, &terminal, 112U) ==
          D1L_MESHCORE_TRACE_CORRELATION_PATH_MISMATCH);
    CHECK(tracker.pending);
    terminal.path_hashes[1] = 0x20U;
    CHECK(d1l_meshcore_trace_tracker_consume(&tracker, &terminal, 113U) ==
          D1L_MESHCORE_TRACE_CORRELATION_MATCHED);
    CHECK(!tracker.pending && tracker.completed);
    terminal.path_hashes[0] = 0xEEU;
    terminal.path_snrs_quarter_db[0] = 99;
    CHECK(tracker.last_result.path_hashes[0] == 0x10U);
    CHECK(tracker.last_result.path_snrs_quarter_db[0] == -1);
    terminal.path_hashes[0] = 0x10U;
    CHECK(d1l_meshcore_trace_tracker_consume(&tracker, &terminal, 114U) ==
          D1L_MESHCORE_TRACE_CORRELATION_DUPLICATE);
    CHECK(d1l_meshcore_trace_tracker_consume(
              &tracker, &terminal,
              113U + D1L_MESHCORE_TRACE_DUPLICATE_WINDOW_MS) ==
          D1L_MESHCORE_TRACE_CORRELATION_UNMATCHED);

    memset(&tracker, 0, sizeof(tracker));
    CHECK(d1l_meshcore_trace_tracker_begin(
        &tracker, 7U, 9U, hashes, 2U, 100U));
    CHECK(!d1l_meshcore_trace_tracker_expire_pending(
        &tracker, 100U + D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS - 1U));
    CHECK(d1l_meshcore_trace_tracker_consume(
              &tracker, &terminal,
              100U + D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS) ==
          D1L_MESHCORE_TRACE_CORRELATION_EXPIRED);
    CHECK(!tracker.pending && !tracker.completed);

    memset(&tracker, 0, sizeof(tracker));
    const uint32_t wrap_start = UINT32_MAX - 10U;
    CHECK(d1l_meshcore_trace_tracker_begin(
        &tracker, 1U, 2U, hashes, 2U, wrap_start));
    CHECK(!d1l_meshcore_trace_tracker_expire_pending(&tracker, 8U));
    CHECK(d1l_meshcore_trace_tracker_expire_pending(
        &tracker, wrap_start + D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS));
    CHECK(!tracker.pending);
    CHECK(!d1l_meshcore_trace_tracker_cancel(&tracker, 1U, 2U));

    CHECK(d1l_meshcore_trace_tracker_begin(
        &tracker, 3U, 4U, hashes, 2U, 500U));
    CHECK(!d1l_meshcore_trace_tracker_cancel(&tracker, 3U, 5U));
    CHECK(d1l_meshcore_trace_tracker_cancel(&tracker, 3U, 4U));
    CHECK(!tracker.pending);
    return 0;
}

int main(void)
{
    CHECK(test_contact_plan() == 0);
    CHECK(test_source_and_terminal() == 0);
    CHECK(test_tracker() == 0);
    puts("meshcore_trace_test: ok");
    return 0;
}
