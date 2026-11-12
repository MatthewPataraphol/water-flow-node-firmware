#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_auth.h"
#include "esp_eap_client.h"

static unsigned calls;
static unsigned fail_at;
static unsigned disable_calls;
static bool enabled;

static esp_err_t step(unsigned expected)
{
    assert(++calls == expected);
    return calls == fail_at ? ESP_FAIL : ESP_OK;
}
const char *esp_err_to_name(esp_err_t err) { (void)err; return "synthetic failure"; }
esp_err_t esp_eap_client_set_eap_methods(esp_eap_method_t methods)
{ assert(methods == ESP_EAP_TYPE_PEAP); return step(1); }
esp_err_t esp_eap_client_set_identity(const unsigned char *identity, int length)
{
    const char *expected = CONFIG_FLOW_WIFI_ENTERPRISE_IDENTITY[0]
                           ? CONFIG_FLOW_WIFI_ENTERPRISE_IDENTITY : CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME;
    assert(length == (int)strlen(expected) && memcmp(identity, expected, (size_t)length) == 0);
    return step(2);
}
esp_err_t esp_eap_client_set_username(const unsigned char *username, int length)
{
    assert(length == (int)strlen(CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME));
    assert(memcmp(username, CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME, (size_t)length) == 0);
    return step(3);
}
esp_err_t esp_eap_client_set_password(const unsigned char *password, int length)
{
    assert(length == (int)strlen(CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD));
    assert(memcmp(password, CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD, (size_t)length) == 0);
    return step(4);
}
esp_err_t esp_eap_client_use_default_cert_bundle(bool use_default)
{ assert(use_default); return step(5); }
esp_err_t esp_eap_client_set_domain_name(const char *domain)
{
    if (CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME[0]) {
        assert(domain && strcmp(domain, CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME) == 0);
    } else {
        assert(domain == NULL);
    }
    return step(6);
}
esp_err_t esp_wifi_sta_enterprise_enable(void)
{
    esp_err_t err = step(7);
    enabled = err == ESP_OK;
    return err;
}
esp_err_t esp_wifi_sta_enterprise_disable(void)
{ ++disable_calls; enabled = false; return ESP_OK; }

int main(void)
{
    wifi_config_t config;
    assert(wifi_auth_prepare(NULL) == ESP_ERR_INVALID_ARG);
#if TEST_WIFI_EMPTY_SSID
    assert(wifi_auth_prepare(&config) == ESP_ERR_NOT_FOUND);
    puts("PASS: empty SSID leaves networking unconfigured");
#elif TEST_WIFI_LONG_SSID || TEST_WIFI_INVALID_USERNAME || TEST_WIFI_EMPTY_PASSWORD
    assert(wifi_auth_prepare(&config) == ESP_ERR_INVALID_ARG);
    assert(calls == 0);
    puts("PASS: invalid settings rejected before any EAP configuration");
#else
    memset(&config, 0xff, sizeof(config));
    assert(wifi_auth_prepare(&config) == ESP_OK);
    assert(memcmp(config.sta.ssid, CONFIG_FLOW_WIFI_SSID, strlen(CONFIG_FLOW_WIFI_SSID)) == 0);
    assert(config.sta.pmf_cfg.capable && !config.sta.pmf_cfg.required);
#if CONFIG_FLOW_WIFI_AUTH_ENTERPRISE
    for (size_t i = 0; i < sizeof(config.sta.password); ++i) assert(config.sta.password[i] == 0);
    assert(config.sta.threshold.authmode == WIFI_AUTH_WPA2_ENTERPRISE);
    assert(strlen(CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD) > sizeof(config.sta.password));
    assert(wifi_auth_apply() == ESP_OK && enabled && calls == 7);
    // Any setup failure, including enabling EAP, must return the error and
    // clear partial credentials without starting a different auth mode.
    for (unsigned failed_step = 1; failed_step <= 7; ++failed_step) {
        calls = disable_calls = 0;
        fail_at = failed_step;
        assert(wifi_auth_apply() == ESP_FAIL);
        assert(calls == failed_step && disable_calls == 1 && !enabled);
    }
    puts("PASS: Enterprise PEAP credentials, identity fallback/override, CA/name configuration and failure cleanup");
#else
    assert(memcmp(config.sta.password, CONFIG_FLOW_WIFI_PASSWORD, strlen(CONFIG_FLOW_WIFI_PASSWORD)) == 0);
    assert(wifi_auth_apply() == ESP_OK && calls == 0 && !enabled);
    puts("PASS: Personal station settings preserved without any Enterprise API calls");
#endif
#endif
    return 0;
}
