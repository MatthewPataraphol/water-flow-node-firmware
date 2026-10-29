#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "flow_types.h"

#define EVENT_STORAGE_CAPACITY 128
#define EVENT_STORAGE_VERSION 1

// Initialize once before starting tasks. Does not erase or repair damaged NVS.
esp_err_t event_storage_init(void);
// Persistent boot counter: unique even before Wi-Fi enables hardware entropy.
esp_err_t event_storage_next_boot_id(uint64_t *boot_id);
flow_state_t event_storage_last_state(void);
esp_err_t event_storage_set_baseline(flow_state_t state);
// Commits before returning ESP_OK. A full store returns ESP_ERR_NO_MEM.
esp_err_t event_storage_append(flow_event_t *event);
// Oldest event with a valid timestamp; unresolved records remain stored.
esp_err_t event_storage_peek(flow_event_t *event);
// Call ONLY after MQTT PUBACK. Checkpoints state before removing the record.
esp_err_t event_storage_ack(uint64_t sequence);
esp_err_t event_storage_resolve_pending(uint64_t boot_id, int64_t monotonic_ms,
                                       int64_t epoch_ms);
size_t event_storage_count(void);
size_t event_storage_unresolved_count(uint64_t current_boot_id);
