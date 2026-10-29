#include "event_storage.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "event_storage";
static nvs_handle_t s_nvs;
static SemaphoreHandle_t s_lock;
static flow_event_t s_records[EVENT_STORAGE_CAPACITY];
static bool s_used[EVENT_STORAGE_CAPACITY];
static size_t s_count;
static uint64_t s_last_sequence;
static flow_state_t s_last_state;

typedef struct {
    uint64_t sequence;
    uint32_t state;
    uint32_t version;
} checkpoint_t;
static checkpoint_t s_checkpoint;

static void slot_key(size_t slot, char key[8])
{
    snprintf(key, 8, "ev%03u", (unsigned)slot);
}

static esp_err_t read_blob(const char *key, void *value, size_t size)
{
    size_t length = 0;
    esp_err_t err = nvs_get_blob(s_nvs, key, NULL, &length);
    if (err != ESP_OK) {
        return err;
    }
    if (length != size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return nvs_get_blob(s_nvs, key, value, &length);
}

static esp_err_t write_blob(const char *key, const void *value, size_t size)
{
    esp_err_t err = nvs_set_blob(s_nvs, key, value, size);
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

esp_err_t event_storage_init(void)
{
    esp_err_t err = nvs_flash_init_partition("flow_events");
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_open_from_partition("flow_events", "flow_queue", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        return err;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    // Explicit initialization also makes reboot/recovery tests deterministic.
    memset(s_used, 0, sizeof(s_used));
    memset(&s_checkpoint, 0, sizeof(s_checkpoint));
    s_count = 0;
    s_last_sequence = 0;
    s_last_state = FLOW_STATE_UNKNOWN;
    err = read_blob("checkpoint", &s_checkpoint, sizeof(s_checkpoint));
    if (err == ESP_OK) {
        if (s_checkpoint.version != EVENT_STORAGE_VERSION ||
            !flow_state_is_known(s_checkpoint.state)) {
            return ESP_ERR_INVALID_STATE;
        }
        s_last_state = s_checkpoint.state;
        s_last_sequence = s_checkpoint.sequence;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    for (size_t i = 0; i < EVENT_STORAGE_CAPACITY; ++i) {
        char key[8];
        slot_key(i, key);
        flow_event_t *record = &s_records[i];
        err = read_blob(key, record, sizeof(*record));
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (record->version != EVENT_STORAGE_VERSION || record->sequence == 0 ||
            !flow_state_is_known(record->state) || record->timestamp_ms < 0 ||
            record->monotonic_ms < 0) {
            return ESP_ERR_INVALID_STATE;
        }
        for (size_t j = 0; j < i; ++j) {
            if (s_used[j] && s_records[j].sequence == record->sequence) {
                return ESP_ERR_INVALID_STATE;
            }
        }
        s_used[i] = true;
        ++s_count;
        if (record->sequence > s_last_sequence) {
            s_last_sequence = record->sequence;
            s_last_state = record->state;
        }
    }
    ESP_LOGI(TAG, "Recovered %u buffered events; last state %s", (unsigned)s_count,
             flow_state_name(s_last_state));
    return ESP_OK;
}

esp_err_t event_storage_next_boot_id(uint64_t *boot_id)
{
    if (!boot_id) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint64_t counter = 0;
    esp_err_t err = read_blob("boot_counter", &counter, sizeof(counter));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        if (counter == UINT64_MAX) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            ++counter;
            err = write_blob("boot_counter", &counter, sizeof(counter));
            if (err == ESP_OK) {
                *boot_id = counter;
            }
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

flow_state_t event_storage_last_state(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    flow_state_t state = s_last_state;
    xSemaphoreGive(s_lock);
    return state;
}

esp_err_t event_storage_set_baseline(flow_state_t state)
{
    if (!flow_state_is_known(state)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_last_state == FLOW_STATE_UNKNOWN) {
        checkpoint_t checkpoint = {.state = state, .version = EVENT_STORAGE_VERSION};
        err = write_blob("checkpoint", &checkpoint, sizeof(checkpoint));
        if (err == ESP_OK) {
            s_checkpoint = checkpoint;
            s_last_state = state;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t event_storage_append(flow_event_t *event)
{
    if (!event || !flow_state_is_known(event->state) || event->timestamp_ms < 0 ||
        event->monotonic_ms < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (s_last_sequence == UINT64_MAX) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        for (size_t i = 0; i < EVENT_STORAGE_CAPACITY; ++i) {
            if (s_used[i]) {
                continue;
            }
            char key[8];
            slot_key(i, key);
            flow_event_t record = *event;
            record.sequence = s_last_sequence + 1;
            record.version = EVENT_STORAGE_VERSION;
            err = write_blob(key, &record, sizeof(record));
            if (err == ESP_OK) {
                s_records[i] = record;
                s_used[i] = true;
                ++s_count;
                s_last_sequence = record.sequence;
                s_last_state = record.state;
                *event = record;
                ESP_LOGI(TAG, "Stored event %" PRIu64 " (%s), buffered=%u%s",
                         record.sequence, flow_state_name(record.state), (unsigned)s_count,
                         record.timestamp_ms == 0 ? "; awaiting time sync" : "");
            }
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t event_storage_peek(flow_event_t *event)
{
    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int selected = -1;
    for (size_t i = 0; i < EVENT_STORAGE_CAPACITY; ++i) {
        if (s_used[i] && s_records[i].timestamp_ms > 0 &&
            (selected < 0 || s_records[i].sequence < s_records[selected].sequence)) {
            selected = (int)i;
        }
    }
    if (selected >= 0) {
        *event = s_records[selected];
    }
    xSemaphoreGive(s_lock);
    return selected >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t event_storage_ack(uint64_t sequence)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < EVENT_STORAGE_CAPACITY; ++i) {
        if (!s_used[i] || s_records[i].sequence != sequence) {
            continue;
        }
        // Separate commits, in this order: after power loss either the event
        // survives (and may be resent) or its last state survives the deletion.
        if (sequence > s_checkpoint.sequence) {
            checkpoint_t checkpoint = {
                .sequence = sequence, .state = s_records[i].state,
                .version = EVENT_STORAGE_VERSION,
            };
            err = write_blob("checkpoint", &checkpoint, sizeof(checkpoint));
            if (err != ESP_OK) {
                break;
            }
            s_checkpoint = checkpoint;
        }
        char key[8];
        slot_key(i, key);
        err = nvs_erase_key(s_nvs, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK; // A preceding failed commit may already have erased it.
        }
        if (err == ESP_OK) {
            err = nvs_commit(s_nvs);
        }
        if (err == ESP_OK) {
            s_used[i] = false;
            --s_count;
        }
        break;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t event_storage_resolve_pending(uint64_t boot_id, int64_t monotonic_ms,
                                       int64_t epoch_ms)
{
    if (epoch_ms <= 0 || monotonic_ms < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < EVENT_STORAGE_CAPACITY; ++i) {
        flow_event_t record = s_records[i];
        if (!s_used[i] || record.timestamp_ms != 0 || record.boot_id != boot_id) {
            continue;
        }
        if (record.monotonic_ms > monotonic_ms ||
            epoch_ms <= monotonic_ms - record.monotonic_ms) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        record.timestamp_ms = epoch_ms - (monotonic_ms - record.monotonic_ms);
        char key[8];
        slot_key(i, key);
        err = write_blob(key, &record, sizeof(record));
        if (err != ESP_OK) {
            break;
        }
        s_records[i] = record;
        ESP_LOGI(TAG, "Resolved timestamp for event %" PRIu64, record.sequence);
    }
    xSemaphoreGive(s_lock);
    return err;
}

size_t event_storage_count(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = s_count;
    xSemaphoreGive(s_lock);
    return count;
}

size_t event_storage_unresolved_count(uint64_t current_boot_id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = 0;
    for (size_t i = 0; i < EVENT_STORAGE_CAPACITY; ++i) {
        if (s_used[i] && s_records[i].timestamp_ms == 0 &&
            s_records[i].boot_id != current_boot_id) {
            ++count;
        }
    }
    xSemaphoreGive(s_lock);
    return count;
}
