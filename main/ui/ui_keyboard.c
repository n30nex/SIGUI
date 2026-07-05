#include "ui_keyboard.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool d1l_ui_keyboard_normalize_probe_target(const char *name, char *out_target,
                                            size_t out_target_len)
{
    char normalized[16] = {0};
    if (!name || !out_target || out_target_len == 0) {
        return false;
    }
    size_t len = 0;
    while (*name && len + 1U < sizeof(normalized)) {
        char ch = (char)tolower((unsigned char)*name++);
        normalized[len++] = (ch == '-' || ch == ' ') ? '_' : ch;
    }
    normalized[len] = '\0';
    if (*name || len == 0) {
        return false;
    }

    const char *target = NULL;
    if (strcmp(normalized, "public") == 0 ||
        strcmp(normalized, "public_short") == 0) {
        target = "public";
    } else if (strcmp(normalized, "public_long") == 0) {
        target = "public_long";
    } else if (strcmp(normalized, "dm") == 0 ||
               strcmp(normalized, "direct") == 0 ||
               strcmp(normalized, "dm_short") == 0 ||
               strcmp(normalized, "direct_short") == 0) {
        target = "dm";
    } else if (strcmp(normalized, "dm_long") == 0 ||
               strcmp(normalized, "direct_long") == 0) {
        target = "dm_long";
    } else if (strcmp(normalized, "public_search") == 0) {
        target = "public_search";
    } else if (strcmp(normalized, "packet_search") == 0) {
        target = "packet_search";
    } else if (strcmp(normalized, "contact_edit") == 0) {
        target = "contact_edit";
    } else if (strcmp(normalized, "onboarding") == 0 ||
               strcmp(normalized, "onboarding_name") == 0) {
        target = "onboarding";
    } else if (strcmp(normalized, "map_location") == 0) {
        target = "map_location";
    } else if (strcmp(normalized, "map_provider") == 0 ||
               strcmp(normalized, "map_tiles") == 0) {
        target = "map_provider";
    } else if (strcmp(normalized, "wifi") == 0 ||
               strcmp(normalized, "wifi_ssid") == 0) {
        target = "wifi_ssid";
    } else if (strcmp(normalized, "wifi_password") == 0 ||
               strcmp(normalized, "wifi_pass") == 0) {
        target = "wifi_password";
    } else {
        return false;
    }

    snprintf(out_target, out_target_len, "%s", target);
    return true;
}

bool d1l_ui_keyboard_probe_target_is_dm(const char *target)
{
    return target && strncmp(target, "dm", 2) == 0;
}

bool d1l_ui_keyboard_probe_target_is_compose(const char *target)
{
    return target &&
        (strcmp(target, "public") == 0 ||
         strcmp(target, "public_long") == 0 ||
         strcmp(target, "dm") == 0 ||
         strcmp(target, "dm_long") == 0);
}

bool d1l_ui_keyboard_probe_target_is_onboarding(const char *target)
{
    return target && strcmp(target, "onboarding") == 0;
}

bool d1l_ui_keyboard_probe_requires_hidden_dock(const char *target)
{
    return d1l_ui_keyboard_probe_target_is_compose(target) ||
        d1l_ui_keyboard_probe_target_is_onboarding(target) ||
        strcmp(target, "public_search") == 0 ||
        strcmp(target, "packet_search") == 0 ||
        strcmp(target, "contact_edit") == 0 ||
        strcmp(target, "map_location") == 0 ||
        strcmp(target, "map_provider") == 0 ||
        strcmp(target, "wifi_ssid") == 0 ||
        strcmp(target, "wifi_password") == 0;
}

int32_t d1l_ui_keyboard_probe_min_width(const char *target)
{
    if (strcmp(target, "public_search") == 0 ||
        strcmp(target, "packet_search") == 0 ||
        strcmp(target, "contact_edit") == 0 ||
        d1l_ui_keyboard_probe_target_is_onboarding(target)) {
        return 400;
    }
    return 440;
}

int32_t d1l_ui_keyboard_probe_min_height(const char *target)
{
    if (d1l_ui_keyboard_probe_target_is_compose(target)) {
        return 250;
    }
    if (strcmp(target, "public_search") == 0 ||
        strcmp(target, "packet_search") == 0) {
        return 180;
    }
    if (strcmp(target, "contact_edit") == 0) {
        return 170;
    }
    if (d1l_ui_keyboard_probe_target_is_onboarding(target) ||
        strcmp(target, "map_location") == 0) {
        return 150;
    }
    return 80;
}
