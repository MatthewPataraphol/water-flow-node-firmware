#include "heartbeat.h"

#include <inttypes.h>
#include <stdio.h>
#include "battery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_manager.h"
#include "sdkconfig.h"
#include "wifi_manager.h"

static const char *TAG = "heartbeat";

static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    // Seconds * tick rate avoids 32-bit pdMS_TO_TICKS overflow at long intervals.
    const TickType_t interval = (TickType_t)CONFIG_FLOW_HEARTBEAT_INTERVAL_S * configTICK_RATE_HZ;
    while (true) {
        vTaskDelayUntil(&last_wake, interval);
        battery_status_t battery = battery_read();
        char percent[16] = "null";
        char voltage[16] = "null";
        char rssi_text[16] = "null";
        if (battery.available) {
            snprintf(percent, sizeof(percent), "%d", battery.percent);
            snprintf(voltage, sizeof(voltage), "%.3f", (double)battery.voltage);
        }
        int rssi;
        if (wifi_manager_get_rssi(&rssi) == ESP_OK) {
            snprintf(rssi_text, sizeof(rssi_text), "%d", rssi);
        }
        char payload[MQTT_HEARTBEAT_MAX_LENGTH];
        int length = snprintf(payload, sizeof(payload),
                              "{\"battery_percent\":%s,\"battery_voltage\":%s,"
                              "\"wifi_rssi\":%s,\"uptime_s\":%" PRId64 ","
                              "\"buffered_events\":%u,\"battery_mock\":%s}",
                              percent, voltage, rssi_text, esp_timer_get_time() / 1000000,
                              (unsigned)event_storage_count(), battery.mock ? "true" : "false");
        if (length <= 0 || (size_t)length >= sizeof(payload) ||
            mqtt_manager_submit_heartbeat(payload) != ESP_OK) {
            ESP_LOGW(TAG, "Heartbeat could not be submitted; discarded");
        }
    }
}

esp_err_t heartbeat_start(void)
{
    return xTaskCreate(heartbeat_task, "heartbeat", 3072, NULL, 3, NULL) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}
