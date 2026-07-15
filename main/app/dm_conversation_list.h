#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh/dm_store.h"

/* One volatile tail can be visible in addition to the durable ring. */
#define D1L_DM_CONVERSATION_SOURCE_CAPACITY (D1L_DM_STORE_CAPACITY + 1U)

typedef struct {
    d1l_dm_entry_t latest;
    uint32_t unread_count;
    bool muted;
} d1l_dm_conversation_summary_t;

/* Projects an oldest-to-newest bounded retained row set into one newest-row
 * summary per exact fingerprint, ordered newest conversation first.  The
 * caller supplies unread truth for every visible row so older unread RX is
 * preserved even when the newest row for that contact is TX. */
size_t d1l_dm_conversation_list_project(
    const d1l_dm_entry_t *rows, const bool *row_unread, size_t row_count,
    d1l_dm_conversation_summary_t *out_summaries, size_t max_summaries,
    size_t *out_total_conversations);
