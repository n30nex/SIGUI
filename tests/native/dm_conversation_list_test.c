#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "app/dm_conversation_list.h"

static d1l_dm_entry_t row(uint32_t seq, const char *fingerprint,
                          const char *direction, const char *text)
{
    d1l_dm_entry_t entry = {
        .seq = seq,
        .delivery_state = direction[0] == 't' ?
            D1L_DM_DELIVERY_AWAITING_ACK :
            D1L_DM_DELIVERY_NOT_APPLICABLE,
    };
    snprintf(entry.contact_fingerprint, sizeof(entry.contact_fingerprint),
             "%s", fingerprint);
    snprintf(entry.contact_alias, sizeof(entry.contact_alias), "Node %s",
             fingerprint);
    snprintf(entry.direction, sizeof(entry.direction), "%s", direction);
    snprintf(entry.text, sizeof(entry.text), "%s", text);
    return entry;
}

int main(void)
{
    const d1l_dm_entry_t rows[] = {
        row(1U, "aaaaaaaaaaaaaaaa", "rx", "older unread A"),
        row(2U, "bbbbbbbbbbbbbbbb", "tx", "older B"),
        row(3U, "aaaaaaaaaaaaaaaa", "tx", "latest A"),
        row(4U, "cccccccccccccccc", "tx", "outbound-only C"),
        row(5U, "bbbbbbbbbbbbbbbb", "rx", "latest B"),
    };
    const bool row_unread[] = {true, false, false, false, true};
    d1l_dm_conversation_summary_t summaries[5] = {0};
    size_t total = 99U;
    size_t copied = d1l_dm_conversation_list_project(
        rows, row_unread, sizeof(rows) / sizeof(rows[0]),
        summaries, 5U, &total);
    assert(copied == 3U && total == 3U);
    assert(summaries[0].latest.seq == 5U);
    assert(strcmp(summaries[0].latest.contact_fingerprint,
                  "bbbbbbbbbbbbbbbb") == 0);
    assert(summaries[0].unread_count == 1U && !summaries[0].muted);
    assert(summaries[1].latest.seq == 4U);
    assert(strcmp(summaries[1].latest.contact_fingerprint,
                  "cccccccccccccccc") == 0);
    assert(summaries[1].unread_count == 0U && !summaries[1].muted);
    assert(summaries[2].latest.seq == 3U);
    assert(summaries[2].latest.direction[0] == 't');
    assert(summaries[2].unread_count == 1U && !summaries[2].muted);

    memset(summaries, 0, sizeof(summaries));
    total = 0U;
    copied = d1l_dm_conversation_list_project(
        rows, row_unread, sizeof(rows) / sizeof(rows[0]),
        summaries, 2U, &total);
    assert(copied == 2U && total == 3U);
    assert(summaries[0].latest.seq == 5U);
    assert(summaries[1].latest.seq == 4U);

    total = 99U;
    assert(d1l_dm_conversation_list_project(
               NULL, row_unread, 0U, summaries, 2U, &total) == 0U);
    assert(total == 0U);
    assert(d1l_dm_conversation_list_project(
               rows, NULL, 5U, summaries, 2U, NULL) == 0U);

    d1l_dm_entry_t bounded[D1L_DM_CONVERSATION_SOURCE_CAPACITY + 2U] = {0};
    for (size_t i = 0U; i < sizeof(bounded) / sizeof(bounded[0]); ++i) {
        char fingerprint[D1L_NODE_FINGERPRINT_LEN];
        snprintf(fingerprint, sizeof(fingerprint), "%016u", (unsigned)i);
        bounded[i] = row((uint32_t)i + 1U, fingerprint, "tx", "bounded");
    }
    bool bounded_unread[D1L_DM_CONVERSATION_SOURCE_CAPACITY + 2U] = {0};
    copied = d1l_dm_conversation_list_project(
        bounded, bounded_unread, sizeof(bounded) / sizeof(bounded[0]),
        summaries, 5U, &total);
    assert(copied == 5U);
    assert(total == D1L_DM_CONVERSATION_SOURCE_CAPACITY);
    assert(summaries[0].latest.seq ==
           D1L_DM_CONVERSATION_SOURCE_CAPACITY + 2U);

    puts("native DM conversation list: ok");
    return 0;
}
