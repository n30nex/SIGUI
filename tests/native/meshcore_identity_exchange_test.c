#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ed_25519.h"
#include "mesh/meshcore_identity_exchange.h"

#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)

static int s_force_zero_exchange;

void __real_ed25519_key_exchange(unsigned char *shared_secret,
                                 const unsigned char *public_key,
                                 const unsigned char *private_key);

void __wrap_ed25519_key_exchange(unsigned char *shared_secret,
                                 const unsigned char *public_key,
                                 const unsigned char *private_key)
{
    if (s_force_zero_exchange) {
        memset(shared_secret, 0, 32U);
        return;
    }
    __real_ed25519_key_exchange(shared_secret, public_key, private_key);
}

static int zero(const uint8_t *value, size_t size)
{
    uint8_t combined = 0U;
    for (size_t i = 0U; i < size; ++i) combined |= value[i];
    return combined == 0U;
}

int main(void)
{
    uint8_t seed_a[32] = {1U};
    uint8_t seed_b[32] = {2U};
    uint8_t public_a[32], public_b[32], private_a[64], private_b[64];
    uint8_t expected[32], actual[32];
    ed25519_create_keypair(public_a, private_a, seed_a);
    ed25519_create_keypair(public_b, private_b, seed_b);
    ed25519_key_exchange(expected, public_b, private_a);
    CHECK(d1l_meshcore_identity_derive_shared_secret(
        public_b, public_a, private_a, actual));
    CHECK(memcmp(expected, actual, sizeof(actual)) == 0);

    s_force_zero_exchange = 1;
    memset(actual, 0xA5, sizeof(actual));
    CHECK(!d1l_meshcore_identity_derive_shared_secret(
        public_b, public_a, private_a, actual));
    CHECK(zero(actual, sizeof(actual)));
    s_force_zero_exchange = 0;

    uint8_t wrong_local[32];
    memcpy(wrong_local, public_a, sizeof(wrong_local));
    wrong_local[0] ^= 1U;
    memset(actual, 0xA5, sizeof(actual));
    CHECK(!d1l_meshcore_identity_derive_shared_secret(
        public_b, wrong_local, private_a, actual));
    CHECK(zero(actual, sizeof(actual)));

    uint8_t identity[32] = {1U};
    memset(actual, 0xA5, sizeof(actual));
    CHECK(!d1l_meshcore_identity_derive_shared_secret(
        identity, public_a, private_a, actual));
    CHECK(zero(actual, sizeof(actual)));

    uint8_t noncanonical[32];
    memset(noncanonical, 0xFF, sizeof(noncanonical));
    noncanonical[0] = 0xEDU;
    noncanonical[31] = 0x7FU;
    memset(actual, 0xA5, sizeof(actual));
    CHECK(!d1l_meshcore_identity_derive_shared_secret(
        noncanonical, public_a, private_a, actual));
    CHECK(zero(actual, sizeof(actual)));
    puts("meshcore_identity_exchange_test: ok");
    return 0;
}
