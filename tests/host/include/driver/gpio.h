#pragma once

#include <stdbool.h>
#include <stdint.h>

// ESP-IDF 5.5.6 ESP32 soc_caps.h masks. Package wiring is checked separately.
#define SOC_GPIO_VALID_GPIO_MASK (UINT64_C(0xffffffffff) & \
    ~((UINT64_C(1) << 24) | (UINT64_C(0xf) << 28)))
#define SOC_GPIO_VALID_OUTPUT_GPIO_MASK (SOC_GPIO_VALID_GPIO_MASK & \
    ~(UINT64_C(0x3f) << 34))
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) < 40 && \
    ((SOC_GPIO_VALID_GPIO_MASK >> (pin)) & 1U))
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) >= 0 && (pin) < 40 && \
    ((SOC_GPIO_VALID_OUTPUT_GPIO_MASK >> (pin)) & 1U))
