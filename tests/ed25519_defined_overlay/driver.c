#include "ed_25519.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif


enum { DIFFERENTIAL_CASES = 256, MAX_MESSAGE = 256 };

static uint64_t rng_state = UINT64_C(0x6a09e667f3bcc909);

static uint64_t next_random(void) {
    uint64_t value = rng_state;
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    rng_state = value;
    return value * UINT64_C(0x2545f4914f6cdd1d);
}

static void fill_random(unsigned char *output, size_t size) {
    size_t index;
    uint64_t value = 0;
    for (index = 0; index < size; ++index) {
        if ((index & 7U) == 0U) {
            value = next_random();
        }
        output[index] = (unsigned char)(value >> (8U * (index & 7U)));
    }
}

static int hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

static int decode_hex(const char *hex, unsigned char *output, size_t size) {
    size_t index;
    if (strlen(hex) != size * 2U) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        int high = hex_nibble(hex[index * 2U]);
        int low = hex_nibble(hex[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return 0;
        }
        output[index] = (unsigned char)((high << 4) | low);
    }
    return 1;
}

static int emit(const void *data, size_t size) {
    return fwrite(data, 1, size, stdout) == size;
}

static int emit_u32(uint32_t value) {
    unsigned char encoded[4];
    encoded[0] = (unsigned char)value;
    encoded[1] = (unsigned char)(value >> 8);
    encoded[2] = (unsigned char)(value >> 16);
    encoded[3] = (unsigned char)(value >> 24);
    return emit(encoded, sizeof(encoded));
}

static int rfc8032_kat(
    const char *seed_hex,
    const unsigned char *message,
    size_t message_size,
    const char *public_key_hex,
    const char *signature_hex
) {
    unsigned char seed[32];
    unsigned char expected_public_key[32];
    unsigned char expected_signature[64];
    unsigned char public_key[32];
    unsigned char private_key[64];
    unsigned char signature[64];

    if (!decode_hex(seed_hex, seed, sizeof(seed)) ||
        !decode_hex(public_key_hex, expected_public_key, sizeof(expected_public_key)) ||
        !decode_hex(signature_hex, expected_signature, sizeof(expected_signature))) {
        fputs("invalid checked-in RFC 8032 vector\n", stderr);
        return 0;
    }
    ed25519_create_keypair(public_key, private_key, seed);
    ed25519_sign(signature, message, message_size, public_key, private_key);
    if (memcmp(public_key, expected_public_key, sizeof(public_key)) != 0 ||
        memcmp(signature, expected_signature, sizeof(signature)) != 0 ||
        ed25519_verify(signature, message, message_size, public_key) != 1) {
        fputs("RFC 8032 known-answer test failed\n", stderr);
        return 0;
    }
    return emit(public_key, sizeof(public_key)) &&
           emit(private_key, sizeof(private_key)) &&
           emit(signature, sizeof(signature));
}

static int differential_case(uint32_t case_index) {
    unsigned char seed_a[32];
    unsigned char seed_b[32];
    unsigned char scalar[32];
    unsigned char message[MAX_MESSAGE];
    unsigned char public_a[32];
    unsigned char public_b[32];
    unsigned char private_a[64];
    unsigned char private_b[64];
    unsigned char signature[64];
    unsigned char tampered[64];
    unsigned char shared_ab[32];
    unsigned char shared_ba[32];
    unsigned char adjusted_public[32];
    unsigned char adjusted_public_only[32];
    unsigned char adjusted_private[64];
    unsigned char derived_public[32];
    unsigned char adjusted_signature[64];
    unsigned char verify_results[3];
    size_t message_size = (size_t)((case_index * 73U + 19U) % (MAX_MESSAGE + 1U));

    fill_random(seed_a, sizeof(seed_a));
    fill_random(seed_b, sizeof(seed_b));
    fill_random(scalar, sizeof(scalar));
    fill_random(message, message_size);

    ed25519_create_keypair(public_a, private_a, seed_a);
    ed25519_create_keypair(public_b, private_b, seed_b);
    ed25519_sign(signature, message, message_size, public_a, private_a);
    verify_results[0] = (unsigned char)ed25519_verify(
        signature, message, message_size, public_a
    );
    memcpy(tampered, signature, sizeof(tampered));
    tampered[case_index % sizeof(tampered)] ^= (unsigned char)(1U << (case_index & 7U));
    verify_results[1] = (unsigned char)ed25519_verify(
        tampered, message, message_size, public_a
    );

    ed25519_key_exchange(shared_ab, public_b, private_a);
    ed25519_key_exchange(shared_ba, public_a, private_b);

    memcpy(adjusted_public, public_a, sizeof(adjusted_public));
    memcpy(adjusted_public_only, public_a, sizeof(adjusted_public_only));
    memcpy(adjusted_private, private_a, sizeof(adjusted_private));
    ed25519_add_scalar(adjusted_public, adjusted_private, scalar);
    ed25519_add_scalar(adjusted_public_only, NULL, scalar);
    ed25519_derive_pub(derived_public, adjusted_private);
    ed25519_sign(
        adjusted_signature,
        message,
        message_size,
        adjusted_public,
        adjusted_private
    );
    verify_results[2] = (unsigned char)ed25519_verify(
        adjusted_signature, message, message_size, adjusted_public
    );

    if (verify_results[0] != 1U || verify_results[1] != 0U ||
        verify_results[2] != 1U ||
        memcmp(shared_ab, shared_ba, sizeof(shared_ab)) != 0 ||
        memcmp(adjusted_public, adjusted_public_only, sizeof(adjusted_public)) != 0 ||
        memcmp(adjusted_public, derived_public, sizeof(adjusted_public)) != 0) {
        fprintf(stderr, "deterministic differential case %u failed\n", case_index);
        return 0;
    }

    return emit_u32(case_index) && emit_u32((uint32_t)message_size) &&
           emit(message, message_size) && emit(public_a, sizeof(public_a)) &&
           emit(private_a, sizeof(private_a)) && emit(public_b, sizeof(public_b)) &&
           emit(private_b, sizeof(private_b)) && emit(signature, sizeof(signature)) &&
           emit(verify_results, sizeof(verify_results)) &&
           emit(shared_ab, sizeof(shared_ab)) &&
           emit(adjusted_public, sizeof(adjusted_public)) &&
           emit(adjusted_private, sizeof(adjusted_private)) &&
           emit(adjusted_signature, sizeof(adjusted_signature));
}

int main(void) {
    static const unsigned char empty_message[1] = {0};
    static const unsigned char one_byte_message[1] = {0x72};
    uint32_t case_index;

#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        fputs("failed to select binary stdout mode\n", stderr);
        return EXIT_FAILURE;
    }
#endif
    if (!emit("SIGUI-ED25519-DEFINED-V1", 24U)) {
        return EXIT_FAILURE;
    }
    if (!rfc8032_kat(
            "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
            empty_message,
            0,
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
            "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
            "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")) {
        return EXIT_FAILURE;
    }
    if (!rfc8032_kat(
            "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
            one_byte_message,
            sizeof(one_byte_message),
            "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
            "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
            "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")) {
        return EXIT_FAILURE;
    }
    for (case_index = 0; case_index < DIFFERENTIAL_CASES; ++case_index) {
        if (!differential_case(case_index)) {
            return EXIT_FAILURE;
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fputs("failed to flush deterministic output corpus\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
