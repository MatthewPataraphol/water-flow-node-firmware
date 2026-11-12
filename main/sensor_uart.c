#include "sensor_uart.h"

#include "driver/gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
#include "esp_efuse.h"
#include "soc/efuse_defs.h"
#endif

static bool reserved_pin(int pin, bool pico_v3_02)
{
    // Preserve the UART0 console and the unavailable/reserved GPIO16/17.
    if (pin == 1 || pin == 3 || pin == 16 || pin == 17) {
        return true;
    }
    if (pico_v3_02) {
        // PICO-V3-02: flash CLK/CS = GPIO6/11, PSRAM CS/CLK = GPIO9/10.
        // GPIO18/23 are also not exposed on this SiP (internal memory wiring).
        // GPIO7/8 ARE available: Feather V2's labeled RX/TX respectively.
        // These restrictions apply even with CONFIG_SPIRAM disabled.
        return pin == 6 || pin == 9 || pin == 10 || pin == 11 ||
               pin == 18 || pin == 23;
    }
    // Do not release GPIO7/8 on a generic ESP32 or another PICO package.
    return pin >= 6 && pin <= 11;
}

esp_err_t sensor_uart_validate_pins(int rx, int tx)
{
    if (!GPIO_IS_VALID_GPIO(rx) ||
        (tx != -1 && (!GPIO_IS_VALID_OUTPUT_GPIO(tx) || tx == rx))) {
        return ESP_ERR_INVALID_ARG;
    }
    bool pico_v3_02 = false;
#if CONFIG_IDF_TARGET_ESP32
    // Read-only identification, using the same SDK package constant as chip_info.
    pico_v3_02 = esp_efuse_get_pkg_ver() == EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302;
#endif
    return reserved_pin(rx, pico_v3_02) || (tx != -1 && reserved_pin(tx, pico_v3_02))
           ? ESP_ERR_INVALID_ARG : ESP_OK;
}

#if CONFIG_FLOW_SENSOR_UART
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flow_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_line_parser.h"

static const char *TAG = "sensor_uart";
#define SENSOR_UART UART_NUM_1

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
    if (sensor_uart_validate_pins(rx, tx) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid UART pins: check GPIO direction/overlap and console/memory pins. "
                 "Feather V2 RX=7/TX=8 requires the detected ESP32-PICO-V3-02 package");
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
