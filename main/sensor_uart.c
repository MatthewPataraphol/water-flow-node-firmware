#include "sensor_uart.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flow_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sensor_line_parser.h"

#if CONFIG_FLOW_SENSOR_UART
static const char *TAG = "sensor_uart";
#define SENSOR_UART UART_NUM_1

static bool reserved_pin(int pin)
{
    return (pin >= 6 && pin <= 11) || pin == 16 || pin == 17 || pin == 1 || pin == 3;
}

static void uart_task(void *arg)
{
    (void)arg;
    sensor_line_parser_t parser = {0};
    uint8_t bytes[64];
    while (true) {
        int count = uart_read_bytes(SENSOR_UART, bytes, sizeof(bytes), pdMS_TO_TICKS(1000));
        if (count < 0) {
            ESP_LOGW(TAG, "UART read failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        for (int i = 0; i < count; ++i) {
            flow_state_t state;
            if (sensor_line_parser_feed(&parser, (char)bytes[i], &state)) {
                unsigned retries = 0;
                int64_t measured_ms = esp_timer_get_time() / 1000;
                while (flow_state_report_at(state, measured_ms) != ESP_OK) {
                    if (retries++ % 30 == 0) {
                        ESP_LOGW(TAG, "State queue full; UART input backpressured");
                    }
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
            }
        }
    }
}
#endif

esp_err_t sensor_uart_start(void)
{
#if CONFIG_FLOW_SENSOR_UART
    int rx = CONFIG_FLOW_UART_RX_GPIO;
    int tx = CONFIG_FLOW_UART_TX_GPIO;
    if (!GPIO_IS_VALID_GPIO(rx) || reserved_pin(rx) ||
        (tx != -1 && (!GPIO_IS_VALID_OUTPUT_GPIO(tx) || reserved_pin(tx) || tx == rx))) {
        ESP_LOGE(TAG, "Configure board-safe UART GPIOs; flash/PSRAM/console pins are rejected");
        return ESP_ERR_INVALID_ARG;
    }
    uart_config_t config = {
        .baud_rate = CONFIG_FLOW_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(SENSOR_UART, &config);
    if (err == ESP_OK) {
        err = uart_set_pin(SENSOR_UART, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_driver_install(SENSOR_UART, 1024, 0, 0, NULL, 0);
    }
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(uart_task, "sensor_uart", 3072, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(SENSOR_UART);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UART1 ready: RX=%d TX=%d baud=%d; temporary FLOW/NO_FLOW parser",
             rx, tx, CONFIG_FLOW_UART_BAUD);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
