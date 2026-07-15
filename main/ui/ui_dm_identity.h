#pragma once

#include <stdbool.h>

#include "mesh/contact_store.h"

typedef enum {
    D1L_UI_DM_IDENTITY_SOURCE_PUBLIC_SENDER_LABEL = 0,
    D1L_UI_DM_IDENTITY_SOURCE_HEARD_NODE,
    D1L_UI_DM_IDENTITY_SOURCE_CONTACT,
} d1l_ui_dm_identity_source_t;

typedef enum {
    D1L_UI_DM_IDENTITY_READY = 0,
    D1L_UI_DM_IDENTITY_SENDER_NAME_UNVERIFIED,
    D1L_UI_DM_IDENTITY_INCOMPLETE,
    D1L_UI_DM_IDENTITY_HEARD_ONLY,
    D1L_UI_DM_IDENTITY_CONTACT_MISSING,
    D1L_UI_DM_IDENTITY_CONTACT_NOT_CANONICAL,
    D1L_UI_DM_IDENTITY_MISMATCH,
    D1L_UI_DM_IDENTITY_ROLE_UNSUPPORTED,
} d1l_ui_dm_identity_reason_t;

typedef struct {
    d1l_ui_dm_identity_source_t source;
    const char *fingerprint;
    const char *public_key_hex;
    const d1l_contact_entry_t *resolved_contact;
    bool contact_found_by_full_key;
} d1l_ui_dm_identity_input_t;

typedef struct {
    d1l_ui_dm_identity_reason_t reason;
    bool can_open_compose;
} d1l_ui_dm_identity_eligibility_t;

d1l_ui_dm_identity_eligibility_t d1l_ui_dm_identity_eligibility(
    const d1l_ui_dm_identity_input_t *input);
const char *d1l_ui_dm_identity_reason_code(d1l_ui_dm_identity_reason_t reason);
const char *d1l_ui_dm_identity_reason_text(d1l_ui_dm_identity_reason_t reason);
