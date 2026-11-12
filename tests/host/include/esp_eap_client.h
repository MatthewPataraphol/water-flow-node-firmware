#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum { ESP_EAP_TYPE_PEAP = (1 << 2) } esp_eap_method_t;
esp_err_t esp_eap_client_set_eap_methods(esp_eap_method_t methods);
esp_err_t esp_eap_client_set_identity(const unsigned char *identity, int length);
esp_err_t esp_eap_client_set_username(const unsigned char *username, int length);
esp_err_t esp_eap_client_set_password(const unsigned char *password, int length);
esp_err_t esp_eap_client_use_default_cert_bundle(bool use_default);
esp_err_t esp_eap_client_set_domain_name(const char *domain);
esp_err_t esp_wifi_sta_enterprise_enable(void);
esp_err_t esp_wifi_sta_enterprise_disable(void);
