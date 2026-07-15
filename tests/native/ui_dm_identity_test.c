#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_dm_identity.h"

bool d1l_contact_store_is_canonical(const d1l_contact_entry_t *entry)
{
    return entry &&
        entry->verification_source == D1L_CONTACT_VERIFICATION_SIGNED_ADVERT &&
        entry->fingerprint[0] != '\0' && entry->public_key_hex[0] != '\0';
}

bool d1l_contact_store_can_dm(const d1l_contact_entry_t *entry)
{
    return d1l_contact_store_is_canonical(entry) &&
        strcmp(entry->type, "chat") == 0;
}

static d1l_contact_entry_t ready_contact(void)
{
    d1l_contact_entry_t contact = {0};
    snprintf(contact.fingerprint, sizeof(contact.fingerprint),
             "%s", "0bf0a701d5ae2db6");
    snprintf(contact.public_key_hex, sizeof(contact.public_key_hex), "%s",
             "0bf0a701d5ae2db660b6aba17831f883"
             "937d290883817cbd1122334455667788");
    snprintf(contact.type, sizeof(contact.type), "%s", "chat");
    contact.verification_source = D1L_CONTACT_VERIFICATION_SIGNED_ADVERT;
    return contact;
}

static d1l_ui_dm_identity_input_t ready_input(
    d1l_contact_entry_t *contact)
{
    return (d1l_ui_dm_identity_input_t) {
        .source = D1L_UI_DM_IDENTITY_SOURCE_HEARD_NODE,
        .fingerprint = "0BF0A701D5AE2DB6",
        .public_key_hex =
            "0BF0A701D5AE2DB660B6ABA17831F883"
            "937D290883817CBD1122334455667788",
        .resolved_contact = contact,
        .contact_found_by_full_key = true,
    };
}

static void expect_reason(const d1l_ui_dm_identity_input_t *input,
                          d1l_ui_dm_identity_reason_t reason,
                          bool can_open)
{
    const d1l_ui_dm_identity_eligibility_t eligibility =
        d1l_ui_dm_identity_eligibility(input);
    if (eligibility.reason != reason) {
        fprintf(stderr, "expected reason %d, got %d\n",
                (int)reason, (int)eligibility.reason);
    }
    assert(eligibility.reason == reason);
    assert(eligibility.can_open_compose == can_open);
    assert(d1l_ui_dm_identity_reason_code(reason)[0] != '\0');
    assert(d1l_ui_dm_identity_reason_text(reason)[0] != '\0');
}

int main(void)
{
    d1l_contact_entry_t contact = ready_contact();
    d1l_ui_dm_identity_input_t input = ready_input(&contact);

    expect_reason(NULL, D1L_UI_DM_IDENTITY_INCOMPLETE, false);

    input.source = D1L_UI_DM_IDENTITY_SOURCE_PUBLIC_SENDER_LABEL;
    expect_reason(&input, D1L_UI_DM_IDENTITY_SENDER_NAME_UNVERIFIED, false);

    input = ready_input(&contact);
    input.public_key_hex = "";
    expect_reason(&input, D1L_UI_DM_IDENTITY_INCOMPLETE, false);

    input.source = D1L_UI_DM_IDENTITY_SOURCE_CONTACT;
    expect_reason(&input, D1L_UI_DM_IDENTITY_INCOMPLETE, false);

    input = ready_input(&contact);
    input.contact_found_by_full_key = false;
    input.resolved_contact = NULL;
    expect_reason(&input, D1L_UI_DM_IDENTITY_HEARD_ONLY, false);

    input.source = D1L_UI_DM_IDENTITY_SOURCE_CONTACT;
    expect_reason(&input, D1L_UI_DM_IDENTITY_CONTACT_MISSING, false);

    input = ready_input(&contact);
    input.fingerprint = "1BF0A701D5AE2DB6";
    expect_reason(&input, D1L_UI_DM_IDENTITY_MISMATCH, false);

    input = ready_input(&contact);
    contact.fingerprint[0] = '1';
    expect_reason(&input, D1L_UI_DM_IDENTITY_MISMATCH, false);

    contact = ready_contact();
    contact.verification_source = D1L_CONTACT_VERIFICATION_NONE;
    input = ready_input(&contact);
    expect_reason(&input, D1L_UI_DM_IDENTITY_CONTACT_NOT_CANONICAL, false);

    contact = ready_contact();
    snprintf(contact.type, sizeof(contact.type), "%s", "repeater");
    input = ready_input(&contact);
    expect_reason(&input, D1L_UI_DM_IDENTITY_ROLE_UNSUPPORTED, false);

    contact = ready_contact();
    input = ready_input(&contact);
    expect_reason(&input, D1L_UI_DM_IDENTITY_READY, true);
    assert(strcmp(d1l_ui_dm_identity_reason_code(input.source ==
        D1L_UI_DM_IDENTITY_SOURCE_HEARD_NODE ? D1L_UI_DM_IDENTITY_READY :
        D1L_UI_DM_IDENTITY_INCOMPLETE), "ready") == 0);

    puts("native UI DM identity: ok");
    return 0;
}
