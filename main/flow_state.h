#pragma once

#include "esp_err.h"
#include "flow_types.h"

esp_err_t flow_state_start(uint64_t boot_id);
// Task-context, nonblocking. On ESP_ERR_TIMEOUT the producer must retain and
// retry this reading before submitting later readings. UNKNOWN is not a state.
esp_err_t flow_state_report(flow_state_t state);
// Keep the original monotonic measurement time when retrying backpressure.
esp_err_t flow_state_report_at(flow_state_t state, int64_t monotonic_ms);
