#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include "sensor_uart.h"
#include "soc/efuse_defs.h"

static uint32_t package_id;
uint32_t esp_efuse_get_pkg_ver(void) { return package_id; }

int main(void)
{
    package_id = EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302;
    assert(sensor_uart_validate_pins(7, -1) == ESP_OK);
    assert(sensor_uart_validate_pins(7, 8) == ESP_OK);
    assert(sensor_uart_validate_pins(8, -1) == ESP_OK);
    assert(sensor_uart_validate_pins(8, 7) == ESP_OK);
    puts("PASS: PICO-V3-02 GPIO7/8 accepted, including Feather V2 RX7 with TX disabled");

    const int reserved[] = {1, 3, 6, 9, 10, 11, 16, 17, 18, 23};
    for (unsigned i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        assert(sensor_uart_validate_pins(reserved[i], -1) == ESP_ERR_INVALID_ARG);
        assert(sensor_uart_validate_pins(7, reserved[i]) == ESP_ERR_INVALID_ARG);
    }
    puts("PASS: console, flash, PSRAM and unavailable PICO package GPIOs rejected in both directions");

    const int invalid[] = {-1, -2, INT_MIN, 24, 28, 29, 30, 31, 40, 99, INT_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(sensor_uart_validate_pins(invalid[i], -1) == ESP_ERR_INVALID_ARG);
        if (invalid[i] != -1) {
            assert(sensor_uart_validate_pins(7, invalid[i]) == ESP_ERR_INVALID_ARG);
        }
    }
    for (int input_only = 34; input_only <= 39; ++input_only) {
        assert(sensor_uart_validate_pins(input_only, -1) == ESP_OK);
        assert(sensor_uart_validate_pins(7, input_only) == ESP_ERR_INVALID_ARG);
    }
    assert(sensor_uart_validate_pins(7, 7) == ESP_ERR_INVALID_ARG);
    assert(sensor_uart_validate_pins(8, 8) == ESP_ERR_INVALID_ARG);
    assert(sensor_uart_validate_pins(21, 21) == ESP_ERR_INVALID_ARG);
    assert(sensor_uart_validate_pins(21, -1) == ESP_OK);
    puts("PASS: GPIO masks, input-only RX, output-only TX constraints, overlap and disabled-TX sentinel");

    // Unknown and other known ESP32 package IDs must never inherit the exception.
    const uint32_t other_packages[] = {0, 1, 2, 4, 5, 7, 8, 15};
    for (unsigned i = 0; i < sizeof(other_packages) / sizeof(other_packages[0]); ++i) {
        package_id = other_packages[i];
        for (int pin = 6; pin <= 11; ++pin) {
            assert(sensor_uart_validate_pins(pin, -1) == ESP_ERR_INVALID_ARG);
            assert(sensor_uart_validate_pins(21, pin) == ESP_ERR_INVALID_ARG);
        }
        assert(sensor_uart_validate_pins(1, -1) == ESP_ERR_INVALID_ARG);
        assert(sensor_uart_validate_pins(3, -1) == ESP_ERR_INVALID_ARG);
        assert(sensor_uart_validate_pins(16, -1) == ESP_ERR_INVALID_ARG);
        assert(sensor_uart_validate_pins(17, -1) == ESP_ERR_INVALID_ARG);
        assert(sensor_uart_validate_pins(21, -1) == ESP_OK);
    }
    puts("PASS: other/unknown ESP32 packages retain GPIO6-11 and console/GPIO16-17 protection");
    puts("All 4 UART pin-validation groups passed.");
    return 0;
}
