#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/rp2040_sd_reply.h"
#include "storage/storage_status_policy.h"

static const char READY_STATUS[] =
    "DESKOS_SD_STATUS state=ready present=1 mounted=1 deskos=1 fs=fat32 "
    "needs_fat32=0 capacity_kb=30707712 free_kb=30704768 note=ready "
    "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
    "mount_err=0 mount_data=0 file_ops=1 file_line_max=512 "
    "file_chunk_max=192 path_max=96 atomic_rename=1";

static const char NO_CARD_MOUNT[] =
    "DESKOS_SD_MOUNT state=no_card present=0 mounted=0 deskos=0 fs=none "
    "needs_fat32=0 capacity_kb=0 free_kb=0 note=no_card "
    "probe_power=high probe_mode=dedicated probe_err=254 probe_data=0 "
    "mount_err=254 mount_data=0 file_ops=0 file_line_max=512 "
    "file_chunk_max=192 path_max=96 atomic_rename=0";

static const char MOUNT_ERROR_STATUS[] =
    "DESKOS_SD_STATUS state=error present=0 mounted=0 deskos=0 fs=none "
    "needs_fat32=0 capacity_kb=0 free_kb=0 note=sd_mount_failed "
    "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
    "mount_err=4 mount_data=0 file_ops=0 file_line_max=512 "
    "file_chunk_max=192 path_max=96 atomic_rename=0";

static void test_valid_status_and_mount_replies(void)
{
    d1l_rp2040_sd_reply_t reply = {0};
    assert(d1l_rp2040_sd_reply_parse(
        READY_STATUS, "DESKOS_SD_STATUS", &reply));
    assert(strcmp(reply.state, "ready") == 0);
    assert(reply.card_present);
    assert(reply.filesystem_mounted);
    assert(reply.deskos_root_ready);
    assert(reply.file_ops_supported);
    assert(reply.atomic_rename_supported);
    assert(reply.capacity_kb == 30707712U);

    assert(d1l_rp2040_sd_reply_parse(
        NO_CARD_MOUNT, "DESKOS_SD_MOUNT", &reply));
    assert(strcmp(reply.state, "no_card") == 0);
    assert(!reply.card_present);
    assert(!reply.filesystem_mounted);
    assert(!reply.file_ops_supported);
}

static void test_supported_non_ready_states_are_accepted(void)
{
    static const char *const states[] = {
        "mount_required",
        "mount_pending",
        "creating_deskos_files",
        "not_fat32_or_unmountable",
        "deskos_manifest_invalid",
        "error",
    };
    char line[512];
    d1l_rp2040_sd_reply_t reply = {0};

    for (size_t i = 0U; i < sizeof(states) / sizeof(states[0]); ++i) {
        const int written = snprintf(
            line, sizeof(line),
            "DESKOS_SD_STATUS state=%s present=1 mounted=0 deskos=0 fs=none "
            "needs_fat32=0 capacity_kb=10 free_kb=5 note=pending "
            "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
            "mount_err=0 mount_data=0 file_ops=0 file_line_max=512 "
            "file_chunk_max=192 path_max=96 atomic_rename=0",
            states[i]);
        assert(written > 0 && (size_t)written < sizeof(line));
        assert(d1l_rp2040_sd_reply_parse(
            line, "DESKOS_SD_STATUS", &reply));
        assert(strcmp(reply.state, states[i]) == 0);
    }
}

static void assert_invalid(const char *line, const char *prefix)
{
    d1l_rp2040_sd_reply_t reply;
    memset(&reply, 0xa5, sizeof(reply));
    assert(!d1l_rp2040_sd_reply_parse(line, prefix, &reply));
    for (size_t i = 0U; i < sizeof(reply); ++i) {
        assert(((const uint8_t *)&reply)[i] == 0U);
    }
}

static void test_incomplete_and_malformed_replies_are_rejected(void)
{
    assert_invalid("DESKOS_SD_STATUS", "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=ready mounted=1 deskos=1 fs=fat32 "
        "needs_fat32=0 capacity_kb=10 free_kb=5 note=ready "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=1 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=1",
        "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=ready present=yes mounted=1 deskos=1 fs=fat32 "
        "needs_fat32=0 capacity_kb=10 free_kb=5 note=ready "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=1 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=1",
        "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=ready present=1 mounted=1 deskos=1 fs=fat32 "
        "needs_fat32=0 capacity_kb=10 free_kb=5 note=ready "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=1 file_line_max=512 "
        "file_chunk_max=192 path_max=96",
        "DESKOS_SD_STATUS");
    assert_invalid(READY_STATUS, "DESKOS_SD_MOUNT");
}

static void test_inconsistent_replies_are_rejected(void)
{
    assert_invalid(
        "DESKOS_SD_STATUS state=ready present=0 mounted=1 deskos=1 fs=fat32 "
        "needs_fat32=0 capacity_kb=10 free_kb=5 note=ready "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=1 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=1",
        "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=ready present=1 mounted=1 deskos=1 fs=exfat "
        "needs_fat32=0 capacity_kb=10 free_kb=5 note=ready "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=1 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=1",
        "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=no_card present=1 mounted=0 deskos=0 fs=none "
        "needs_fat32=0 capacity_kb=10 free_kb=0 note=no_card "
        "probe_power=high probe_mode=dedicated probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=0 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=0",
        "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=error present=1 mounted=1 deskos=1 fs=fat32 "
        "needs_fat32=0 capacity_kb=5 free_kb=10 note=bad_capacity "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=1 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=1",
        "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=mount_required present=1 mounted=1 deskos=1 fs=fat32 "
        "needs_fat32=0 capacity_kb=10 free_kb=5 note=mount_required "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=1 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=1",
        "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=mount_pending present=1 mounted=1 deskos=1 fs=fat32 "
        "needs_fat32=0 capacity_kb=10 free_kb=5 note=mount_pending "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=0 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=0",
        "DESKOS_SD_STATUS");
    assert_invalid(
        "DESKOS_SD_STATUS state=unknown present=1 mounted=0 deskos=0 fs=none "
        "needs_fat32=0 capacity_kb=10 free_kb=5 note=unknown "
        "probe_power=high probe_mode=mount probe_err=0 probe_data=0 "
        "mount_err=0 mount_data=0 file_ops=0 file_line_max=512 "
        "file_chunk_max=192 path_max=96 atomic_rename=0",
        "DESKOS_SD_STATUS");
}

static void test_bounded_stale_transition_policy(void)
{
    uint32_t failures = 0U;
    assert(!d1l_storage_status_policy_is_stale(failures));
    assert(d1l_storage_status_policy_allows_cached_io(failures));

    failures = d1l_storage_status_policy_note_failure(failures);
    assert(failures == 1U);
    assert(d1l_storage_status_policy_is_stale(failures));
    assert(d1l_storage_status_policy_allows_cached_io(failures));

    failures = d1l_storage_status_policy_note_failure(failures);
    assert(failures == 2U);
    assert(d1l_storage_status_policy_allows_cached_io(failures));

    failures = d1l_storage_status_policy_note_failure(failures);
    assert(failures == D1L_STORAGE_STATUS_STALE_FAILURE_LIMIT);
    assert(!d1l_storage_status_policy_allows_cached_io(failures));
    assert(d1l_storage_status_policy_note_failure(UINT32_MAX) == UINT32_MAX);

    failures = 0U; /* A successfully parsed status is the only recovery input. */
    assert(!d1l_storage_status_policy_is_stale(failures));
    assert(d1l_storage_status_policy_allows_cached_io(failures));
}

static void test_confirmed_presence_changes_only_on_explicit_no_card(void)
{
    d1l_rp2040_sd_reply_t reply = {0};
    bool confirmed_present = false;

    assert(d1l_rp2040_sd_reply_parse(
        READY_STATUS, "DESKOS_SD_STATUS", &reply));
    confirmed_present = d1l_storage_status_policy_effective_present(
        confirmed_present, reply.state, reply.card_present);
    assert(confirmed_present);

    assert(d1l_rp2040_sd_reply_parse(
        MOUNT_ERROR_STATUS, "DESKOS_SD_STATUS", &reply));
    confirmed_present = d1l_storage_status_policy_effective_present(
        confirmed_present, reply.state, reply.card_present);
    assert(confirmed_present);

    assert(d1l_rp2040_sd_reply_parse(
        NO_CARD_MOUNT, "DESKOS_SD_MOUNT", &reply));
    confirmed_present = d1l_storage_status_policy_effective_present(
        confirmed_present, reply.state, reply.card_present);
    assert(!confirmed_present);

    assert(d1l_rp2040_sd_reply_parse(
        READY_STATUS, "DESKOS_SD_STATUS", &reply));
    confirmed_present = d1l_storage_status_policy_effective_present(
        confirmed_present, reply.state, reply.card_present);
    assert(confirmed_present);
}

int main(int argc, char **argv)
{
    if (argc == 3) {
        d1l_rp2040_sd_reply_t reply = {0};
        return d1l_rp2040_sd_reply_parse(argv[2], argv[1], &reply) ? 0 : 2;
    }
    if (argc != 1) {
        return 3;
    }
    test_valid_status_and_mount_replies();
    test_supported_non_ready_states_are_accepted();
    test_incomplete_and_malformed_replies_are_rejected();
    test_inconsistent_replies_are_rejected();
    test_bounded_stale_transition_policy();
    test_confirmed_presence_changes_only_on_explicit_no_card();
    return 0;
}
