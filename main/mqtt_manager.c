#include "mqtt_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "time_sync.h"
#include "wifi_manager.h"

#if !CONFIG_MQTT_REPORT_DELETED_MESSAGES
#error "Enable CONFIG_MQTT_REPORT_DELETED_MESSAGES so expired events can be retried"
#endif
#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#error "Enable CONFIG_MBEDTLS_CERTIFICATE_BUNDLE for verified MQTT TLS"
#endif

#define TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define CALLBACK_OVERFLOW BIT0
static const char *TAG = "mqtt_manager";
typedef struct {
    int32_t id;
    int message_id;
} mqtt_notice_t;
typedef struct {
    char json[MQTT_HEARTBEAT_MAX_LENGTH];
} heartbeat_message_t;

static QueueHandle_t s_notices;
static QueueHandle_t s_heartbeat;
static EventGroupHandle_t s_faults;
static uint64_t s_boot_id;

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t event = data;
    if (id == MQTT_EVENT_ERROR) {
        // Avoid printing connect packets, usernames, or configuration secrets.
        ESP_LOGW(TAG, "MQTT transport/broker error; automatic retry enabled");
        return;
    }
    if (id != MQTT_EVENT_CONNECTED && id != MQTT_EVENT_DISCONNECTED &&
        id != MQTT_EVENT_PUBLISHED && id != MQTT_EVENT_DELETED) {
        return;
    }
    mqtt_notice_t notice = {.id = id, .message_id = event->msg_id};
    // Never take a manager mutex in an MQTT callback: enqueue holds an MQTT
    // internal lock. One worker owns all connection/in-flight state instead.
    if (xQueueSend(s_notices, &notice, 0) != pdPASS) {
        xEventGroupSetBits(s_faults, CALLBACK_OVERFLOW);
    }
}

static esp_mqtt_client_handle_t create_client(void)
{
    const esp_mqtt_client_config_t config = {
        .broker.address.hostname = CONFIG_FLOW_TB_HOST,
        .broker.address.port = CONFIG_FLOW_TB_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = CONFIG_FLOW_TB_TOKEN,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 5000,
        .network.timeout_ms = 5000,
        .outbox.limit = 2048,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
    if (!client) {
        return NULL;
    }
    esp_err_t err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(client);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s; will retry", esp_err_to_name(err));
        esp_mqtt_client_destroy(client);
        return NULL;
    }
    return client;
}

static void mqtt_task(void *arg)
{
    (void)arg;
    esp_mqtt_client_handle_t client = NULL;
    bool connected = false;
    bool ack_pending = false;
    int inflight_id = -1;
    flow_event_t inflight = {0};
    int64_t next_start_ms = 0;
    int64_t next_send_ms = 0;
    int64_t next_storage_log_ms = 0;
    while (true) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t epoch;
        if (time_sync_epoch_ms_at(now, &epoch)) {
            esp_err_t err = event_storage_resolve_pending(s_boot_id, now, epoch);
            if (err != ESP_OK && now >= next_storage_log_ms) {
                ESP_LOGE(TAG, "Timestamp persistence failed: %s", esp_err_to_name(err));
                next_storage_log_ms = now + 30000;
            }
        }
        if (xEventGroupGetBits(s_faults) & CALLBACK_OVERFLOW) {
            ESP_LOGE(TAG, "MQTT callback queue overflow; rebuilding client, retaining NVS events");
            if (client) {
                esp_mqtt_client_stop(client);
                esp_mqtt_client_destroy(client);
                client = NULL;
            }
            xQueueReset(s_notices);
            xEventGroupClearBits(s_faults, CALLBACK_OVERFLOW);
            connected = false;
            // An acknowledged record still needs its NVS removal retried.
            if (!ack_pending) {
                inflight_id = -1;
            }
        }
        if (!client && wifi_manager_is_connected() && time_sync_is_valid() &&
            CONFIG_FLOW_TB_TOKEN[0] && now >= next_start_ms) {
            client = create_client();
            next_start_ms = now + 5000;
        }
        mqtt_notice_t notice;
        while (xQueueReceive(s_notices, &notice, 0) == pdPASS) {
            switch (notice.id) {
            case MQTT_EVENT_CONNECTED:
                connected = true;
                ESP_LOGI(TAG, "MQTT connected; buffered=%u", (unsigned)event_storage_count());
                break;
            case MQTT_EVENT_DISCONNECTED:
                connected = false;
                // ESP-MQTT retains and retransmits this QoS1 message across
                // reconnects. Do not enqueue a second copy of it here.
                ESP_LOGW(TAG, "MQTT disconnected; buffered events retained");
                break;
            case MQTT_EVENT_PUBLISHED:
                if (notice.message_id == inflight_id) {
                    ack_pending = true;
                    ESP_LOGI(TAG, "Event %" PRIu64 " published (broker PUBACK)", inflight.sequence);
                }
                break;
            case MQTT_EVENT_DELETED:
                if (notice.message_id == inflight_id && !ack_pending) {
                    ESP_LOGW(TAG, "MQTT outbox expired; event %" PRIu64 " will be retried",
                             inflight.sequence);
                    inflight_id = -1;
                    next_send_ms = now + 5000;
                }
                break;
            default: break;
            }
        }
        if (ack_pending) {
            esp_err_t err = event_storage_ack(inflight.sequence);
            if (err == ESP_OK) {
                ack_pending = false;
                inflight_id = -1;
            } else if (now >= next_storage_log_ms) {
                ESP_LOGE(TAG, "PUBACK received, but NVS removal failed: %s; retaining event",
                         esp_err_to_name(err));
                next_storage_log_ms = now + 30000;
            }
        }
        bool online = connected && wifi_manager_is_connected();
        if (online && inflight_id < 0 && !ack_pending && now >= next_send_ms &&
            event_storage_peek(&inflight) == ESP_OK) {
            char payload[128];
            int length = snprintf(payload, sizeof(payload),
                                  "{\"ts\":%" PRId64 ",\"values\":{\"flow_state\":\"%s\"}}",
                                  inflight.timestamp_ms, flow_state_name(inflight.state));
            if (length > 0 && (size_t)length < sizeof(payload)) {
                inflight_id = esp_mqtt_client_enqueue(client, TELEMETRY_TOPIC, payload,
                                                      length, 1, 0, true);
                if (inflight_id >= 0) {
                    ESP_LOGI(TAG, "Sending buffered event %" PRIu64 " (%s)",
                             inflight.sequence, flow_state_name(inflight.state));
                } else {
                    ESP_LOGW(TAG, "MQTT enqueue failed; event retained in NVS");
                    next_send_ms = now + 5000;
                }
            }
        }
        heartbeat_message_t heartbeat;
        if (xQueueReceive(s_heartbeat, &heartbeat, 0) == pdPASS && online) {
            // QoS0, immediate send from this worker. It is never put in NVS
            // or the MQTT outbox, and is discarded if this attempt fails.
            if (esp_mqtt_client_publish(client, TELEMETRY_TOPIC, heartbeat.json, 0, 0, 0) >= 0) {
                ESP_LOGI(TAG, "Heartbeat sent");
            } else {
                ESP_LOGW(TAG, "Heartbeat discarded after publish failure");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t mqtt_manager_start(uint64_t boot_id)
{
    s_boot_id = boot_id;
    s_notices = xQueueCreate(16, sizeof(mqtt_notice_t));
    s_heartbeat = xQueueCreate(1, sizeof(heartbeat_message_t));
    s_faults = xEventGroupCreate();
    if (!s_notices || !s_heartbeat || !s_faults) {
        return ESP_ERR_NO_MEM;
    }
    if (!CONFIG_FLOW_TB_TOKEN[0]) {
        ESP_LOGW(TAG, "ThingsBoard token is empty; cloud publishing disabled until configured");
    }
    return xTaskCreate(mqtt_task, "telemetry", 6144, NULL, 4, NULL) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t mqtt_manager_submit_heartbeat(const char *json)
{
    if (!s_heartbeat) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!json || strlen(json) >= MQTT_HEARTBEAT_MAX_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    heartbeat_message_t message = {0};
    memcpy(message.json, json, strlen(json) + 1);
    return xQueueOverwrite(s_heartbeat, &message) == pdPASS ? ESP_OK : ESP_FAIL;
}
