#include "mesh/meshcore_advert_admission.h"

#include <stddef.h>
#include <string.h>

#include "mesh/node_store.h"

static bool hex_digit_valid(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static char lower_hex(char value)
{
    if (value >= 'A' && value <= 'F') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static bool verified_identity_valid(const char *fingerprint,
                                    const char *public_key_hex)
{
    if (!fingerprint || !public_key_hex ||
        strlen(fingerprint) != D1L_NODE_FINGERPRINT_LEN - 1U ||
        strlen(public_key_hex) != D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U) {
        return false;
    }
    for (size_t i = 0; i < D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U; ++i) {
        if (!hex_digit_valid(public_key_hex[i])) {
            return false;
        }
        if (i < D1L_NODE_FINGERPRINT_LEN - 1U) {
            if (!hex_digit_valid(fingerprint[i]) ||
                lower_hex(fingerprint[i]) != lower_hex(public_key_hex[i])) {
                return false;
            }
        }
    }
    return true;
}

static void init_receipt(d1l_meshcore_advert_admission_receipt_t *receipt)
{
    memset(receipt, 0, sizeof(*receipt));
    receipt->outcome = D1L_MESHCORE_ADVERT_ADMISSION_INVALID;
    receipt->node_store_error = ESP_OK;
    receipt->contact_store_error = ESP_OK;
    receipt->contact_result = D1L_CONTACT_VERIFIED_ADVERT_NONE;
}

bool d1l_meshcore_advert_admission_receipt_cacheable(
    const d1l_meshcore_advert_admission_receipt_t *receipt)
{
    if (!receipt) {
        return false;
    }
    switch (receipt->outcome) {
        case D1L_MESHCORE_ADVERT_ADMISSION_ACCEPTED:
            return receipt->contact_store_error == ESP_OK;
        case D1L_MESHCORE_ADVERT_ADMISSION_CONTACT_RETRY_SUCCEEDED:
            return receipt->contact_store_error == ESP_OK;
        case D1L_MESHCORE_ADVERT_ADMISSION_REPLAY_REJECTED:
            return true;
        case D1L_MESHCORE_ADVERT_ADMISSION_INVALID:
        case D1L_MESHCORE_ADVERT_ADMISSION_CONTACT_RETRY_FAILED:
        case D1L_MESHCORE_ADVERT_ADMISSION_KEY_COLLISION:
        case D1L_MESHCORE_ADVERT_ADMISSION_NODE_STORE_ERROR:
        default:
            return false;
    }
}

esp_err_t d1l_meshcore_advert_admit_verified(
    const char *fingerprint, const char *public_key_hex, const char *name,
    char type_code, int rssi_dbm, int snr_tenths, uint8_t path_hash_bytes,
    uint8_t path_hops, uint32_t advert_timestamp, bool location_valid,
    int32_t lat_e6, int32_t lon_e6,
    d1l_meshcore_advert_admission_receipt_t *out_receipt)
{
    if (!out_receipt) {
        return ESP_ERR_INVALID_ARG;
    }
    init_receipt(out_receipt);
    if (!verified_identity_valid(fingerprint, public_key_hex)) {
        return ESP_ERR_INVALID_ARG;
    }

    bool stale = false;
    const esp_err_t node_ret = d1l_node_store_upsert_advert(
        fingerprint, public_key_hex, name, type_code, rssi_dbm, snr_tenths,
        path_hash_bytes, path_hops, advert_timestamp, location_valid, lat_e6,
        lon_e6, &stale);
    out_receipt->node_store_error = node_ret;

    if (stale) {
        d1l_node_entry_t retained_node = {0};
        const bool exact_retained_identity =
            d1l_node_store_find_by_fingerprint(fingerprint, &retained_node) &&
            retained_node.advert_timestamp == advert_timestamp &&
            strcmp(retained_node.public_key_hex, public_key_hex) == 0;
        if (exact_retained_identity &&
            !d1l_contact_store_find_by_public_key(public_key_hex, NULL)) {
            out_receipt->contact_store_error =
                d1l_contact_store_upsert_verified_advert(
                    fingerprint, &retained_node, &out_receipt->contact_result,
                    NULL);
            out_receipt->outcome =
                out_receipt->contact_store_error == ESP_OK
                    ? D1L_MESHCORE_ADVERT_ADMISSION_CONTACT_RETRY_SUCCEEDED
                    : D1L_MESHCORE_ADVERT_ADMISSION_CONTACT_RETRY_FAILED;
            return ESP_OK;
        }
        out_receipt->outcome = D1L_MESHCORE_ADVERT_ADMISSION_REPLAY_REJECTED;
        return ESP_OK;
    }

    if (node_ret != ESP_OK) {
        out_receipt->outcome =
            node_ret == ESP_ERR_INVALID_STATE
                ? D1L_MESHCORE_ADVERT_ADMISSION_KEY_COLLISION
                : D1L_MESHCORE_ADVERT_ADMISSION_NODE_STORE_ERROR;
        return ESP_OK;
    }

    d1l_node_entry_t verified_node = {0};
    if (d1l_node_store_find_by_fingerprint(fingerprint, &verified_node) &&
        strcmp(verified_node.public_key_hex, public_key_hex) == 0) {
        out_receipt->contact_store_error =
            d1l_contact_store_upsert_verified_advert(
                fingerprint, &verified_node, &out_receipt->contact_result,
                NULL);
    } else {
        out_receipt->contact_result = D1L_CONTACT_VERIFIED_ADVERT_COLLISION;
        out_receipt->contact_store_error = ESP_ERR_INVALID_STATE;
    }
    out_receipt->outcome = D1L_MESHCORE_ADVERT_ADMISSION_ACCEPTED;
    return ESP_OK;
}
