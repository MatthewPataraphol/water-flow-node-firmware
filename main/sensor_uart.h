#pragma once

#include "esp_err.h"

// Read-only package-aware GPIO validation; TX=-1 is disabled. PICO-V3-02
// supports Feather V2 RX=7/TX=8. No driver or pin routing is changed here.
esp_err_t sensor_uart_validate_pins(int rx, int tx);

// UART1, receive-only unless TX is explicitly configured. No production
// MSP430 framing, electrical interface or classifier is assumed here.
esp_err_t sensor_uart_start(void);
