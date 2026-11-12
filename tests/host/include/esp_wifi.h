#pragma once

// Only the station fields exercised by wifi_auth.c. Real layout/API compatibility
// is checked separately by building both modes with the installed ESP-IDF SDK.
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA2_ENTERPRISE = 5,
} wifi_auth_mode_t;
typedef struct {
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
        struct { wifi_auth_mode_t authmode; } threshold;
        struct { bool capable, required; } pmf_cfg;
    } sta;
} wifi_config_t;
