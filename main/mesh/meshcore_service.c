#include "meshcore_service.h"

#include <string.h>

#include "app/settings_model.h"

static d1l_meshcore_service_status_t s_status;

void d1l_meshcore_service_init(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = D1L_MESHCORE_SERVICE_WAITING_FOR_RADIO;
    s_status.path_hash_bytes = settings->path_hash_bytes;
    s_status.identity_ready = false;
    s_status.radio_ready = false;
    s_status.companion_framing_ready = true;
}

d1l_meshcore_service_status_t d1l_meshcore_service_status(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    s_status.path_hash_bytes = settings->path_hash_bytes;
    return s_status;
}

esp_err_t d1l_meshcore_service_request_advert(bool flood)
{
    (void)flood;
    s_status.rejected_commands++;
    return ESP_ERR_INVALID_STATE;
}

esp_err_t d1l_meshcore_service_send_public(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    s_status.rejected_commands++;
    return ESP_ERR_INVALID_STATE;
}

const char *d1l_meshcore_service_state_name(d1l_meshcore_service_state_t state)
{
    switch (state) {
    case D1L_MESHCORE_SERVICE_PHASE1_STUB:
        return "phase1_stub";
    case D1L_MESHCORE_SERVICE_WAITING_FOR_RADIO:
        return "waiting_for_radio";
    default:
        return "unknown";
    }
}
