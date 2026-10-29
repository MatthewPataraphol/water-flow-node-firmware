#pragma once

#include "esp_err.h"

// UART1, receive-only unless TX is explicitly configured. No production
// MSP430 framing, electrical interface or classifier is assumed here.
esp_err_t sensor_uart_start(void);
