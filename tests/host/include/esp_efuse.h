#pragma once
#include <stdint.h>

// Host fake for the SDK's read-only package-ID accessor.
uint32_t esp_efuse_get_pkg_ver(void);
