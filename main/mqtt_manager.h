#pragma once

#include <stdint.h>
#include "esp_err.h"

#define MQTT_HEARTBEAT_MAX_LENGTH 320

esp_err_t mqtt_manager_start(uint64_t boot_id);
// Copies the newest disposable heartbeat into a one-element RAM mailbox.
esp_err_t mqtt_manager_submit_heartbeat(const char *json);
