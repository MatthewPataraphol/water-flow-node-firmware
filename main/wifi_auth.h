#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

// Build RAM-only station configuration for the selected authentication mode.
// ESP_ERR_NOT_FOUND means SSID is empty (networking intentionally unconfigured).
esp_err_t wifi_auth_prepare(wifi_config_t *config);
// Call after Wi-Fi initialization, station mode and station configuration,
// before esp_wifi_start(). Reconnection reuses this authentication setup.
esp_err_t wifi_auth_apply(void);
