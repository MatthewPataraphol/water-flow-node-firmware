#include <inttypes.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "event_storage.h"
#include "flow_state.h"
#include "heartbeat.h"
#include "mqtt_manager.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sensor_accelerometer.h"
#include "sensor_mock.h"
#include "sensor_uart.h"
#include "time_sync.h"
#include "wifi_manager.h"

static const char *TAG = "app";

static bool initialized(const char *name, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s initialization failed: %s", name, esp_err_to_name(err));
        return false;
    }
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Water flow node boot; ESP-IDF %s; target %s",
             esp_get_idf_version(), CONFIG_IDF_TARGET);
    // A failed/corrupt event store must never trigger an automatic erase.
    if (!initialized("persistent event store", event_storage_init())) {
        ESP_LOGE(TAG, "Startup stopped to preserve historical data; inspect NVS/partition layout");
        return;
    }
    uint64_t boot_id;
    if (!initialized("persistent boot counter", event_storage_next_boot_id(&boot_id))) {
        return;
    }
    size_t unresolved = event_storage_unresolved_count(boot_id);
    if (unresolved) {
        ESP_LOGE(TAG, "%u records from earlier boots have no epoch timestamp; retained for recovery",
                 (unsigned)unresolved);
    }
    if (!initialized("network stack", esp_netif_init()) ||
        !initialized("event loop", esp_event_loop_create_default()) ||
        !initialized("SNTP", time_sync_init()) ||
        !initialized("state task", flow_state_start(boot_id))) {
        return;
    }
    // Wi-Fi NVS is separate from the historical event partition. Keep sensing
    // available if networking cannot initialize; do not erase either partition.
    if (initialized("network NVS", nvs_flash_init())) {
        initialized("Wi-Fi", wifi_manager_start());
    }
    initialized("MQTT worker", mqtt_manager_start(boot_id));
    initialized("heartbeat", heartbeat_start());
#if CONFIG_FLOW_BATTERY_MOCK
    ESP_LOGW(TAG, "Battery telemetry uses configured MOCK values");
#else
    ESP_LOGW(TAG, "Battery hardware is undefined; reporting null battery readings");
#endif
    esp_err_t sensor_err;
#if CONFIG_FLOW_SENSOR_MOCK
    sensor_err = sensor_mock_start();
#elif CONFIG_FLOW_SENSOR_UART
    sensor_err = sensor_uart_start();
#else
    sensor_err = sensor_accelerometer_start();
#endif
    if (!initialized("sensor source", sensor_err)) {
        ESP_LOGW(TAG, "Sensor unavailable; networking and queued-event replay remain active");
    }
}
