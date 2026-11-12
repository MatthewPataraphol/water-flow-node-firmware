#include "wifi_auth.h"

#include <limits.h>
#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_FLOW_WIFI_AUTH_ENTERPRISE
#include "esp_eap_client.h"
#endif

static const char *TAG = "wifi_auth";

#if CONFIG_FLOW_WIFI_AUTH_ENTERPRISE
static const char *enterprise_identity(void)
{
    return CONFIG_FLOW_WIFI_ENTERPRISE_IDENTITY[0]
           ? CONFIG_FLOW_WIFI_ENTERPRISE_IDENTITY : CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME;
}
#endif

esp_err_t wifi_auth_prepare(wifi_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(config, 0, sizeof(*config));
#if CONFIG_FLOW_WIFI_AUTH_ENTERPRISE
    ESP_LOGI(TAG, "Wi-Fi authentication mode: WPA2-Enterprise");
#else
    ESP_LOGI(TAG, "Wi-Fi authentication mode: WPA/WPA2 Personal");
#endif
    size_t ssid_length = strlen(CONFIG_FLOW_WIFI_SSID);
    if (ssid_length == 0) {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty; configure Water flow node in menuconfig");
        return ESP_ERR_NOT_FOUND;
    }
    if (ssid_length > sizeof(config->sta.ssid)) {
        ESP_LOGE(TAG, "Wi-Fi SSID exceeds the 32-byte station limit");
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_FLOW_WIFI_AUTH_ENTERPRISE
    size_t username_length = strlen(CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME);
    size_t identity_length = strlen(enterprise_identity());
    size_t password_length = strlen(CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD);
    if (username_length == 0 || username_length > 127 ||
        identity_length == 0 || identity_length > 127 ||
        password_length == 0 || password_length > INT_MAX ||
        strlen(CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME) > 255) {
        ESP_LOGE(TAG, "Enterprise settings invalid: username/identity require 1-127 bytes, "
                 "password must be nonempty, server hostname at most 255 bytes");
        return ESP_ERR_INVALID_ARG;
    }
    // The enterprise account password belongs to EAP, never the PSK field.
    config->sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
#else
    size_t password_length = strlen(CONFIG_FLOW_WIFI_PASSWORD);
    if (password_length > sizeof(config->sta.password)) {
        ESP_LOGE(TAG, "Personal Wi-Fi password exceeds the station field limit");
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config->sta.password, CONFIG_FLOW_WIFI_PASSWORD, password_length);
#endif
    memcpy(config->sta.ssid, CONFIG_FLOW_WIFI_SSID, ssid_length);
    config->sta.pmf_cfg.capable = true;
    config->sta.pmf_cfg.required = false;
    return ESP_OK;
}

esp_err_t wifi_auth_apply(void)
{
#if CONFIG_FLOW_WIFI_AUTH_ENTERPRISE
    const char *identity = enterprise_identity();
    esp_err_t err;
    // IDF 5.5.6 uses MSCHAPv2 as PEAP's default inner method. The TTLS phase-2
    // API is not a PEAP configuration API and is deliberately not called here.
    if ((err = esp_eap_client_set_eap_methods(ESP_EAP_TYPE_PEAP)) != ESP_OK ||
        (err = esp_eap_client_set_identity((const unsigned char *)identity,
                                          (int)strlen(identity))) != ESP_OK ||
        (err = esp_eap_client_set_username(
            (const unsigned char *)CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME,
            (int)strlen(CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME))) != ESP_OK ||
        (err = esp_eap_client_set_password(
            (const unsigned char *)CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD,
            (int)strlen(CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD))) != ESP_OK ||
        (err = esp_eap_client_use_default_cert_bundle(true)) != ESP_OK ||
        (err = esp_eap_client_set_domain_name(CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME[0]
                                             ? CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME
                                             : NULL)) != ESP_OK ||
        (err = esp_wifi_sta_enterprise_enable()) != ESP_OK) {
        // Disable also clears partially configured EAP credentials. Report the
        // original error and never fall back to a different authentication mode.
        esp_wifi_sta_enterprise_disable();
        ESP_LOGE(TAG, "Enterprise authentication setup failed: %s", esp_err_to_name(err));
        return err;
    }
    if (!CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME[0]) {
        ESP_LOGW(TAG, "Enterprise CA chain validation enabled; server hostname is unset");
    }
#endif
    return ESP_OK;
}
