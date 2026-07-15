#include "dm_conversation_list.h"

#include <string.h>

static bool same_fingerprint(const char *left, const char *right)
{
    return left && right && left[0] != '\0' && right[0] != '\0' &&
        strncmp(left, right, D1L_NODE_FINGERPRINT_LEN) == 0;
}

static uint32_t conversation_unread_count(
    const d1l_dm_entry_t *rows, const bool *row_unread,
    size_t first, size_t row_count, const char *fingerprint)
{
    uint32_t unread_count = 0U;
    for (size_t index = first; index < row_count; ++index) {
        if (row_unread[index] &&
            same_fingerprint(rows[index].contact_fingerprint, fingerprint)) {
            unread_count++;
        }
    }
    return unread_count;
}

size_t d1l_dm_conversation_list_project(
    const d1l_dm_entry_t *rows, const bool *row_unread, size_t row_count,
    d1l_dm_conversation_summary_t *out_summaries, size_t max_summaries,
    size_t *out_total_conversations)
{
    if (out_total_conversations) {
        *out_total_conversations = 0U;
    }
    if (!rows || !row_unread || !out_summaries || max_summaries == 0U) {
        return 0U;
    }

    const size_t first = row_count > D1L_DM_CONVERSATION_SOURCE_CAPACITY ?
        row_count - D1L_DM_CONVERSATION_SOURCE_CAPACITY : 0U;
    char seen[D1L_DM_CONVERSATION_SOURCE_CAPACITY]
             [D1L_NODE_FINGERPRINT_LEN] = {{0}};
    size_t seen_count = 0U;
    size_t copied = 0U;
    for (size_t index = row_count; index > first; --index) {
        const d1l_dm_entry_t *entry = &rows[index - 1U];
        if (entry->contact_fingerprint[0] == '\0') {
            continue;
        }
        bool duplicate = false;
        for (size_t seen_index = 0U; seen_index < seen_count; ++seen_index) {
            if (same_fingerprint(seen[seen_index],
                                 entry->contact_fingerprint)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        memcpy(seen[seen_count], entry->contact_fingerprint,
               sizeof(seen[seen_count]));
        seen[seen_count][sizeof(seen[seen_count]) - 1U] = '\0';
        seen_count++;

        if (copied < max_summaries) {
            d1l_dm_conversation_summary_t summary = {
                .latest = *entry,
                .unread_count = conversation_unread_count(
                    rows, row_unread, first, row_count,
                    entry->contact_fingerprint),
                .muted = false,
            };
            out_summaries[copied++] = summary;
        }
    }
    if (out_total_conversations) {
        *out_total_conversations = seen_count;
    }
    return copied;
}
