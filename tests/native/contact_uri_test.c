#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "mesh/contact_uri.h"

static const char KEY[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static void assert_rejected(const char *uri)
{
    d1l_contact_uri_t parsed;
    memset(&parsed, 0xa5, sizeof(parsed));
    assert(!d1l_contact_uri_parse(uri, strlen(uri), &parsed));
    d1l_contact_uri_t empty = {0};
    assert(memcmp(&parsed, &empty, sizeof(parsed)) == 0);
}

static void test_parse_and_format_round_trip(void)
{
    static const char uri[] =
        "meshcore://contact/add?name=North+Room%21&public_key="
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
        "&type=2";
    d1l_contact_uri_t parsed = {0};
    assert(d1l_contact_uri_parse(uri, sizeof(uri) - 1U, &parsed));
    assert(strcmp(parsed.name, "North Room!") == 0);
    assert(strcmp(parsed.public_key_hex, KEY) == 0);
    assert(parsed.type_id == 2U);

    char formatted[D1L_CONTACT_URI_MAX_LEN + 1U] = {0};
    assert(d1l_contact_uri_format(&parsed, formatted, sizeof(formatted)));
    assert(strcmp(
               formatted,
               "meshcore://contact/add?name=North+Room%21&public_key="
               "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
               "&type=2") == 0);

    d1l_contact_uri_t reparsed = {0};
    assert(d1l_contact_uri_parse(formatted, strlen(formatted), &reparsed));
    assert(memcmp(&reparsed, &parsed, sizeof(parsed)) == 0);
}

static void test_field_order_is_flexible_but_set_is_exact(void)
{
    char uri[D1L_CONTACT_URI_MAX_LEN + 1U] = {0};
    const int written = snprintf(
        uri, sizeof(uri),
        "meshcore://contact/add?type=4&public_key=%s&name=Weather_1", KEY);
    assert(written > 0 && (size_t)written < sizeof(uri));
    d1l_contact_uri_t parsed = {0};
    assert(d1l_contact_uri_parse(uri, (size_t)written, &parsed));
    assert(strcmp(parsed.name, "Weather_1") == 0);
    assert(parsed.type_id == 4U);

    assert_rejected(
        "meshcore://contact/add?name=A&name=B&public_key="
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "&type=1");
    assert_rejected(
        "meshcore://contact/add?name=A&public_key="
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "&type=1&extra=x");
}

static void test_official_name_capacity_and_utf8_round_trip(void)
{
    static const char max_name[] = "1234567890123456789012345678901";
    static const char max_uri[] =
        "meshcore://contact/add?name=1234567890123456789012345678901&public_key="
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "&type=1";
    d1l_contact_uri_t parsed = {0};
    assert(strlen(max_name) == D1L_CONTACT_URI_NAME_LEN - 1U);
    assert(d1l_contact_uri_parse(max_uri, sizeof(max_uri) - 1U, &parsed));
    assert(strcmp(parsed.name, max_name) == 0);

    static const char utf8_uri[] =
        "meshcore://contact/add?name=Caf%C3%A9+%E6%9D%B1%E4%BA%AC&public_key="
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "&type=3";
    memset(&parsed, 0, sizeof(parsed));
    assert(d1l_contact_uri_parse(utf8_uri, sizeof(utf8_uri) - 1U, &parsed));
    assert(strcmp(parsed.name, "Caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac") == 0);

    char formatted[D1L_CONTACT_URI_MAX_LEN + 1U] = {0};
    assert(d1l_contact_uri_format(&parsed, formatted, sizeof(formatted)));
    assert(strcmp(formatted, utf8_uri) == 0);
}

static void test_fail_closed_matrix(void)
{
    static const char *invalid[] = {
        "",
        "MESHCORE://contact/add?name=A&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?",
        "meshcore://contact/add?name=A&type=1",
        "meshcore://contact/add?name=A&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde&type=1",
        "meshcore://contact/add?name=A&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefg&type=1",
        "meshcore://contact/add?name=A&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=0",
        "meshcore://contact/add?name=A&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=5",
        "meshcore://contact/add?name=A&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=10",
        "meshcore://contact/add?name=&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=Bad%&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=Bad%2X&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=Bad%00&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=12345678901234567890123456789012&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%80&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%C0%AF&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%E0%80%AF&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%F0%80%80%AF&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%ED%A0%80&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%F4%90%80%80&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%C3&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%C3%28&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%1F&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%7F&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=%C2%85&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=A&&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
        "meshcore://contact/add?name=A&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1&",
        "meshcore://contact/add?name=A=B&public_key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&type=1",
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert_rejected(invalid[i]);
    }

    const char embedded_nul[] =
        "meshcore://contact/add?name=A\0&public_key="
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "&type=1";
    d1l_contact_uri_t parsed = {0};
    assert(!d1l_contact_uri_parse(embedded_nul, sizeof(embedded_nul) - 1U,
                                  &parsed));

    char overlong[D1L_CONTACT_URI_MAX_LEN + 1U];
    memset(overlong, 'x', sizeof(overlong));
    assert(!d1l_contact_uri_parse(overlong, sizeof(overlong), &parsed));
    assert(!d1l_contact_uri_parse(NULL, 1U, &parsed));
    assert(!d1l_contact_uri_parse("x", 1U, NULL));
}

static void test_format_rejects_invalid_or_truncated_output(void)
{
    d1l_contact_uri_t contact = {0};
    snprintf(contact.name, sizeof(contact.name), "%s", "Alice");
    snprintf(contact.public_key_hex, sizeof(contact.public_key_hex), "%s", KEY);
    contact.type_id = 1U;

    char tiny[8] = "dirty";
    assert(!d1l_contact_uri_format(&contact, tiny, sizeof(tiny)));
    assert(tiny[0] == '\0');

    contact.type_id = 0U;
    assert(!d1l_contact_uri_format(&contact, tiny, sizeof(tiny)));
    contact.type_id = 1U;
    contact.public_key_hex[63] = 'z';
    assert(!d1l_contact_uri_format(&contact, tiny, sizeof(tiny)));
    contact.public_key_hex[63] = 'f';
    contact.name[0] = '\0';
    assert(!d1l_contact_uri_format(&contact, tiny, sizeof(tiny)));

    memset(contact.name, 0, sizeof(contact.name));
    contact.name[0] = (char)0xc0U;
    contact.name[1] = (char)0xafU;
    char output[D1L_CONTACT_URI_MAX_LEN + 1U] = "dirty";
    assert(!d1l_contact_uri_format(&contact, output, sizeof(output)));
    assert(output[0] == '\0');
}

int main(void)
{
    test_parse_and_format_round_trip();
    test_field_order_is_flexible_but_set_is_exact();
    test_official_name_capacity_and_utf8_round_trip();
    test_fail_closed_matrix();
    test_format_rejects_invalid_or_truncated_output();
    puts("contact URI vectors: ok");
    return 0;
}
