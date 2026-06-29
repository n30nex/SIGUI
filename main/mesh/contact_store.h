#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "mesh/node_store.h"

#define D1L_CONTACT_STORE_CAPACITY 16U
#define D1L_CONTACT_ALIAS_LEN 24U

typedef struct {
    uint32_t seq;
    uint32_t created_ms;
    uint32_t updated_ms;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char public_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN];
    char alias[D1L_CONTACT_ALIAS_LEN];
    char heard_name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_TYPE_LEN];
    int last_rssi_dbm;
    int last_snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    bool favorite;
    bool muted;
} d1l_contact_entry_t;

typedef struct {
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    size_t count;
    size_t capacity;
} d1l_contact_store_stats_t;

esp_err_t d1l_contact_store_init(void);
esp_err_t d1l_contact_store_clear(void);
esp_err_t d1l_contact_store_upsert_from_node(const char *fingerprint, const char *alias,
                                             const d1l_node_entry_t *heard_node);
d1l_contact_store_stats_t d1l_contact_store_stats(void);
bool d1l_contact_store_find_by_fingerprint(const char *fingerprint, d1l_contact_entry_t *out_entry);
size_t d1l_contact_store_copy_recent(d1l_contact_entry_t *out_entries, size_t max_entries);
