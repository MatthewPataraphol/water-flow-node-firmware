#include "flow_state.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "event_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "time_sync.h"

static const char *TAG = "flow_state";
static QueueHandle_t s_readings;
static uint64_t s_boot_id;

static void state_task(void *arg)
{
    (void)arg;
    flow_state_t last = event_storage_last_state();
    flow_event_t reading;
    while (true) {
        xQueueReceive(s_readings, &reading, portMAX_DELAY);
        if (reading.state == last) {
            continue;
        }
        bool baseline = last == FLOW_STATE_UNKNOWN;
        ESP_LOGI(TAG, "%s: %s -> %s", baseline ? "Initial baseline" : "Transition",
                 flow_state_name(last), flow_state_name(reading.state));
        unsigned failures = 0;
        while (true) {
            esp_err_t err = baseline ? event_storage_set_baseline(reading.state)
                                     : event_storage_append(&reading);
            if (err == ESP_OK) {
                last = reading.state;
                break;
            }
            if (failures++ % 30 == 0) {
                ESP_LOGE(TAG, "Cannot persist reading (%s); retaining it and applying backpressure",
                         esp_err_to_name(err));
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

esp_err_t flow_state_start(uint64_t boot_id)
{
    s_boot_id = boot_id;
    s_readings = xQueueCreate(32, sizeof(flow_event_t));
    if (!s_readings) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(state_task, "flow_state", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_readings);
        s_readings = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t flow_state_report(flow_state_t state)
{
    return flow_state_report_at(state, esp_timer_get_time() / 1000);
}

esp_err_t flow_state_report_at(flow_state_t state, int64_t monotonic_ms)
{
    if (!flow_state_is_known(state) || monotonic_ms < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_readings) {
        return ESP_ERR_INVALID_STATE;
    }
    flow_event_t reading = {
        .state = state,
        .boot_id = s_boot_id,
        .monotonic_ms = monotonic_ms,
    };
    time_sync_epoch_ms_at(reading.monotonic_ms, &reading.timestamp_ms);
    return xQueueSend(s_readings, &reading, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}
