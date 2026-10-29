#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t time_sync_init(void);
esp_err_t time_sync_start(void);
bool time_sync_is_valid(void);
// Converts a measurement's monotonic time using an SNTP anchor from this boot.
// Returns false before a plausible SNTP response, leaving output untouched.
bool time_sync_epoch_ms_at(int64_t monotonic_ms, int64_t *epoch_ms);
