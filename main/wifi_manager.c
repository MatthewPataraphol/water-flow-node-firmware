#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "time_sync.h"
#include "wifi_auth.h"

#define WIFI_READY BIT0
#define WIFI_CONNECTING BIT1
#define WIFI_HAS_IP BIT2
#define WIFI_RECONNECT BIT3
static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_events;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_events, WIFI_READY);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        xEventGroupClearBits(s_events, WIFI_HAS_IP | WIFI_CONNECTING);
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%u); reconnect scheduled", event->reason);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        xEventGroupClearBits(s_events, WIFI_CONNECTING | WIFI_RECONNECT);
        xEventGroupSetBits(s_events, WIFI_HAS_IP);
        ESP_LOGI(TAG, "Wi-Fi connected; assigned IP " IPSTR, IP2STR(&event->ip_info.ip));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(s_events, WIFI_HAS_IP);
        xEventGroupSetBits(s_events, WIFI_RECONNECT);
        ESP_LOGW(TAG, "IP lease lost; Wi-Fi reconnect scheduled");
    }
}

static void wifi_task(void *arg)
{
    (void)arg;
    bool sntp_started_for_ip = false;
    int64_t attempt_started_ms = 0;
    while (true) {
        EventBits_t bits = xEventGroupGetBits(s_events);
        int64_t now = esp_timer_get_time() / 1000;
        if ((bits & WIFI_RECONNECT) ||
            ((bits & WIFI_CONNECTING) && now - attempt_started_ms >= 30000)) {
            // Also recover a live association that never receives a DHCP lease.
            xEventGroupClearBits(s_events, WIFI_RECONNECT | WIFI_CONNECTING | WIFI_HAS_IP);
            esp_wifi_disconnect();
            sntp_started_for_ip = false;
            ESP_LOGW(TAG, "Restarting Wi-Fi association after IP/connection timeout");
        } else if (bits & WIFI_HAS_IP) {
            if (!sntp_started_for_ip) {
                esp_err_t err = time_sync_start();
                sntp_started_for_ip = err == ESP_OK;
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
                }
            }
        } else {
            sntp_started_for_ip = false;
            if ((bits & WIFI_READY) && !(bits & WIFI_CONNECTING)) {
                xEventGroupSetBits(s_events, WIFI_CONNECTING);
                attempt_started_ms = now;
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    xEventGroupClearBits(s_events, WIFI_CONNECTING);
                    ESP_LOGW(TAG, "Wi-Fi connect failed: %s; will retry", esp_err_to_name(err));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t wifi_manager_start(void)
{
    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }
    wifi_config_t config;
    esp_err_t err = wifi_auth_prepare(&config);
    if (err == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (!esp_netif_create_default_wifi_sta()) {
        return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        return err;
    }
    // Do not make extra persistent copies of build-time credentials.
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL)) != ESP_OK ||
        (err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_set_config(WIFI_IF_STA, &config)) != ESP_OK ||
        (err = wifi_auth_apply()) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK) {
        return err;
    }
    return xTaskCreate(wifi_task, "wifi_reconnect", 4096, NULL, 4, NULL) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}

bool wifi_manager_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & WIFI_HAS_IP) != 0;
}

esp_err_t wifi_manager_get_rssi(int *rssi)
{
    if (!rssi || !wifi_manager_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_ap_record_t ap;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK) {
        *rssi = ap.rssi;
    }
    return err;
}
