#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/settings_model.h"
#include "ed_25519.h"
#include "mock_esp_nvs.h"

static void make_keypair(uint32_t counter, uint8_t public_key[32],
                         uint8_t private_key[64])
{
    uint8_t seed[32] = {0};
    seed[0] = (uint8_t)counter;
    seed[1] = (uint8_t)(counter >> 8);
    seed[2] = (uint8_t)(counter >> 16);
    seed[3] = (uint8_t)(counter >> 24);
    for (size_t i = 4; i < sizeof(seed); ++i) {
        seed[i] = (uint8_t)(i * 17U + counter);
    }
    ed25519_create_keypair(public_key, private_key, seed);
}

static void test_absent_and_partial_states(void)
{
    uint8_t public_key[32] = {0};
    uint8_t private_key[64] = {0};
    assert(d1l_identity_state_classify(false, public_key, private_key) ==
           D1L_IDENTITY_STATE_ABSENT);
    assert(d1l_identity_state_classify(true, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);

    public_key[7] = 1U;
    assert(d1l_identity_state_classify(false, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);
    public_key[7] = 0U;
    private_key[39] = 1U;
    assert(d1l_identity_state_classify(false, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);

    assert(d1l_identity_state_classify(false, NULL, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);
    assert(d1l_identity_state_classify(false, public_key, NULL) ==
           D1L_IDENTITY_STATE_INCONSISTENT);
}

static void test_exact_derived_public_match_and_non_mutation(void)
{
    uint8_t public_key[32] = {0};
    uint8_t private_key[64] = {0};
    make_keypair(7U, public_key, private_key);
    uint8_t original_public[32] = {0};
    uint8_t original_private[64] = {0};
    memcpy(original_public, public_key, sizeof(original_public));
    memcpy(original_private, private_key, sizeof(original_private));

    assert(d1l_identity_state_classify(true, public_key, private_key) ==
           D1L_IDENTITY_STATE_CONSISTENT);
    assert(memcmp(public_key, original_public, sizeof(public_key)) == 0);
    assert(memcmp(private_key, original_private, sizeof(private_key)) == 0);
    assert(d1l_identity_state_classify(false, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);

    public_key[31] ^= 0x80U;
    assert(d1l_identity_state_classify(true, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);
    memcpy(public_key, original_public, sizeof(public_key));
    private_key[0] ^= 0x08U;
    assert(d1l_identity_state_classify(true, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);
}

static void test_valid_edge_prefix_is_not_rejected(void)
{
    uint8_t public_key[32] = {0};
    uint8_t private_key[64] = {0};
    bool found_zero_prefix = false;
    bool found_ff_prefix = false;
    for (uint32_t counter = 0; counter < 4096U; ++counter) {
        make_keypair(counter, public_key, private_key);
        if (public_key[0] == 0x00U) {
            found_zero_prefix = true;
            assert(d1l_identity_state_classify(true, public_key, private_key) ==
                   D1L_IDENTITY_STATE_CONSISTENT);
        } else if (public_key[0] == 0xffU) {
            found_ff_prefix = true;
            assert(d1l_identity_state_classify(true, public_key, private_key) ==
                   D1L_IDENTITY_STATE_CONSISTENT);
        }
        if (found_zero_prefix && found_ff_prefix) {
            break;
        }
    }
    assert(found_zero_prefix);
    assert(found_ff_prefix);
}

static void test_unclamped_scalar_is_inconsistent_even_with_matching_public(void)
{
    uint8_t generated_public[32] = {0};
    uint8_t generated_private[64] = {0};
    make_keypair(47U, generated_public, generated_private);
    assert(d1l_identity_state_classify(
               true, generated_public, generated_private) ==
           D1L_IDENTITY_STATE_CONSISTENT);

    uint8_t public_key[32] = {0};
    uint8_t private_key[64] = {0};

    memcpy(private_key, generated_private, sizeof(private_key));
    private_key[0] |= 0x01U;
    ed25519_derive_pub(public_key, private_key);
    assert(d1l_identity_state_classify(true, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);

    memcpy(private_key, generated_private, sizeof(private_key));
    private_key[31] |= 0x80U;
    ed25519_derive_pub(public_key, private_key);
    assert(d1l_identity_state_classify(true, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);

    memcpy(private_key, generated_private, sizeof(private_key));
    private_key[31] &= (uint8_t)~0x40U;
    ed25519_derive_pub(public_key, private_key);
    assert(d1l_identity_state_classify(true, public_key, private_key) ==
           D1L_IDENTITY_STATE_INCONSISTENT);
}

static void assert_uniform_nonce_prefix_rejected(uint8_t value)
{
    d1l_settings_t settings = {0};
    d1l_settings_defaults(&settings);
    make_keypair(53U, settings.identity_public_key,
                 settings.identity_private_key);
    memset(&settings.identity_private_key[32], value, 32U);
    settings.identity_ready = true;
    uint8_t preserved_public[D1L_IDENTITY_PUBLIC_KEY_LEN] = {0};
    uint8_t preserved_private[D1L_IDENTITY_PRIVATE_KEY_LEN] = {0};
    memcpy(preserved_public, settings.identity_public_key,
           sizeof(preserved_public));
    memcpy(preserved_private, settings.identity_private_key,
           sizeof(preserved_private));

    assert(d1l_settings_identity_state(&settings) ==
           D1L_IDENTITY_STATE_INCONSISTENT);
    d1l_settings_sanitize(&settings);
    assert(!settings.identity_ready);
    assert(memcmp(settings.identity_public_key, preserved_public,
                  sizeof(preserved_public)) == 0);
    assert(memcmp(settings.identity_private_key, preserved_private,
                  sizeof(preserved_private)) == 0);
}

static void test_uniform_nonce_prefix_is_inconsistent_with_matching_public(void)
{
    uint8_t public_key[32] = {0};
    uint8_t private_key[64] = {0};
    make_keypair(53U, public_key, private_key);
    assert(d1l_identity_state_classify(true, public_key, private_key) ==
           D1L_IDENTITY_STATE_CONSISTENT);

    assert_uniform_nonce_prefix_rejected(0x00U);
    assert_uniform_nonce_prefix_rejected(0xffU);
    assert_uniform_nonce_prefix_rejected(0x5aU);
}

static void test_settings_sanitizer_fails_closed_and_preserves_evidence(void)
{
    d1l_settings_t settings = {0};
    d1l_settings_defaults(&settings);
    assert(d1l_settings_identity_state(&settings) ==
           D1L_IDENTITY_STATE_ABSENT);

    make_keypair(91U, settings.identity_public_key,
                 settings.identity_private_key);
    settings.identity_ready = true;
    d1l_settings_sanitize(&settings);
    assert(settings.identity_ready);
    assert(d1l_settings_identity_state(&settings) ==
           D1L_IDENTITY_STATE_CONSISTENT);

    settings.identity_public_key[19] ^= 0x20U;
    uint8_t preserved_public[D1L_IDENTITY_PUBLIC_KEY_LEN] = {0};
    uint8_t preserved_private[D1L_IDENTITY_PRIVATE_KEY_LEN] = {0};
    memcpy(preserved_public, settings.identity_public_key,
           sizeof(preserved_public));
    memcpy(preserved_private, settings.identity_private_key,
           sizeof(preserved_private));
    d1l_settings_sanitize(&settings);
    assert(!settings.identity_ready);
    assert(memcmp(settings.identity_public_key, preserved_public,
                  sizeof(preserved_public)) == 0);
    assert(memcmp(settings.identity_private_key, preserved_private,
                  sizeof(preserved_private)) == 0);
    assert(d1l_settings_identity_state(&settings) ==
           D1L_IDENTITY_STATE_INCONSISTENT);
}

static void test_settings_load_status_is_not_forged_by_ram_defaults_or_save(void)
{
    mock_nvs_reset();
    assert(d1l_settings_load_status() == ESP_ERR_INVALID_STATE);

    mock_nvs_fail_next_open(ESP_FAIL);
    assert(d1l_settings_load() == ESP_FAIL);
    assert(d1l_settings_load_status() == ESP_FAIL);

    d1l_settings_t defaults = {0};
    d1l_settings_defaults(&defaults);
    assert(d1l_settings_save(&defaults) == ESP_OK);
    assert(d1l_settings_load_status() == ESP_FAIL);

    assert(d1l_settings_load() == ESP_OK);
    assert(d1l_settings_load_status() == ESP_OK);
}

int main(void)
{
    test_absent_and_partial_states();
    test_exact_derived_public_match_and_non_mutation();
    test_valid_edge_prefix_is_not_rejected();
    test_unclamped_scalar_is_inconsistent_even_with_matching_public();
    test_uniform_nonce_prefix_is_inconsistent_with_matching_public();
    test_settings_sanitizer_fails_closed_and_preserves_evidence();
    test_settings_load_status_is_not_forged_by_ram_defaults_or_save();
    assert(strcmp(d1l_identity_state_name(D1L_IDENTITY_STATE_ABSENT),
                  "absent") == 0);
    assert(strcmp(d1l_identity_state_name(D1L_IDENTITY_STATE_CONSISTENT),
                  "consistent") == 0);
    assert(strcmp(d1l_identity_state_name(D1L_IDENTITY_STATE_INCONSISTENT),
                  "inconsistent") == 0);
    puts("native persisted identity classification: ok");
    return 0;
}
