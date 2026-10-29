#include "sensor_mock.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "flow_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_FLOW_SENSOR_MOCK
static const char *TAG = "sensor_mock";

static void mock_task(void *arg)
{
    (void)arg;
    flow_state_t state = FLOW_STATE_NO_FLOW;
    while (true) {
        int64_t measured_ms = esp_timer_get_time() / 1000;
        unsigned retries = 0;
        while (flow_state_report_at(state, measured_ms) != ESP_OK) {
            if (retries++ % 30 == 0) {
                ESP_LOGW(TAG, "State queue busy; retaining mock state for retry");
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        vTaskDelay((TickType_t)CONFIG_FLOW_MOCK_INTERVAL_S * configTICK_RATE_HZ);
        state = state == FLOW_STATE_NO_FLOW ? FLOW_STATE_FLOW : FLOW_STATE_NO_FLOW;
    }
}
#endif

esp_err_t sensor_mock_start(void)
{
#if CONFIG_FLOW_SENSOR_MOCK
    ESP_LOGW(TAG, "MOCK SENSOR ENABLED: generated test transitions every %d seconds",
             CONFIG_FLOW_MOCK_INTERVAL_S);
    return xTaskCreate(mock_task, "sensor_mock", 3072, NULL, 5, NULL) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
