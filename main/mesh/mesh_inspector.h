#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mesh/node_store.h"
#include "mesh/route_store.h"

#define D1L_MESH_INSPECTOR_LABEL_LEN 24U
#define D1L_MESH_INSPECTOR_KIND_LEN 16U
#define D1L_MESH_INSPECTOR_SOURCE_LEN 12U

typedef struct {
    uint32_t sample_count;
    uint32_t rx_packet_samples;
    uint32_t route_samples;
    uint32_t node_samples;
    uint32_t room_server_count;
    uint32_t repeater_candidate_count;
    int latest_rssi_dbm;
    int latest_snr_tenths;
    int strongest_rssi_dbm;
    int weakest_rssi_dbm;
    int best_snr_tenths;
    int worst_snr_tenths;
    int avg_rssi_dbm;
    int avg_snr_tenths;
    uint8_t latest_path_hops;
    char latest_label[D1L_MESH_INSPECTOR_LABEL_LEN];
    char latest_kind[D1L_MESH_INSPECTOR_KIND_LEN];
} d1l_mesh_signal_summary_t;

typedef struct {
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_TYPE_LEN];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hops;
    uint32_t heard_count;
    uint32_t last_heard_ms;
} d1l_mesh_room_server_t;

typedef struct {
    char target[D1L_ROUTE_TARGET_LEN];
    char label[D1L_ROUTE_LABEL_LEN];
    char kind[D1L_ROUTE_KIND_LEN];
    char route[D1L_ROUTE_NAME_LEN];
    char source[D1L_MESH_INSPECTOR_SOURCE_LEN];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hops;
    uint8_t confidence;
    uint32_t seen_count;
    uint32_t last_seen_ms;
} d1l_mesh_repeater_candidate_t;

void d1l_mesh_inspector_signal_summary(d1l_mesh_signal_summary_t *out_summary);
size_t d1l_mesh_inspector_copy_room_servers(d1l_mesh_room_server_t *out_entries, size_t max_entries);
size_t d1l_mesh_inspector_copy_repeater_candidates(d1l_mesh_repeater_candidate_t *out_entries,
                                                   size_t max_entries);

