#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "mesh/contact_store.h"

typedef enum {
    D1L_MESHCORE_ADVERT_ADMISSION_INVALID = 0,
    D1L_MESHCORE_ADVERT_ADMISSION_ACCEPTED,
    D1L_MESHCORE_ADVERT_ADMISSION_REPLAY_REJECTED,
    D1L_MESHCORE_ADVERT_ADMISSION_CONTACT_RETRY_SUCCEEDED,
    D1L_MESHCORE_ADVERT_ADMISSION_CONTACT_RETRY_FAILED,
    D1L_MESHCORE_ADVERT_ADMISSION_KEY_COLLISION,
    D1L_MESHCORE_ADVERT_ADMISSION_NODE_STORE_ERROR,
} d1l_meshcore_advert_admission_outcome_t;

typedef struct {
    d1l_meshcore_advert_admission_outcome_t outcome;
    esp_err_t node_store_error;
    esp_err_t contact_store_error;
    d1l_contact_verified_advert_result_t contact_result;
} d1l_meshcore_advert_admission_receipt_t;

/*
 * True only after durable admission reached a terminal result. A transient
 * contact-store failure intentionally stays retryable by an identical advert.
 */
bool d1l_meshcore_advert_admission_receipt_cacheable(
    const d1l_meshcore_advert_admission_receipt_t *receipt);

/*
 * Applies the durable node/contact admission policy after the caller has
 * verified an advert signature. The receipt distinguishes intentional replay
 * rejection, exact-key contact recovery, key collisions, and storage errors;
 * ESP_ERR_INVALID_ARG is reserved for a malformed caller contract.
 */
esp_err_t d1l_meshcore_advert_admit_verified(
    const char *fingerprint, const char *public_key_hex, const char *name,
    char type_code, int rssi_dbm, int snr_tenths, uint8_t path_hash_bytes,
    uint8_t path_hops, uint32_t advert_timestamp, bool location_valid,
    int32_t lat_e6, int32_t lon_e6,
    d1l_meshcore_advert_admission_receipt_t *out_receipt);
