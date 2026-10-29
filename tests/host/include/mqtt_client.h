#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef const char *esp_event_base_t;
#define ESP_EVENT_ANY_ID -1
enum { MQTT_EVENT_ERROR, MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED,
       MQTT_EVENT_PUBLISHED, MQTT_EVENT_DELETED };
#define MQTT_TRANSPORT_OVER_SSL 2
#define MQTT_PROTOCOL_V_3_1_1 4
typedef struct { int msg_id; } *esp_mqtt_event_handle_t;
typedef void *esp_mqtt_client_handle_t;
typedef struct {
    struct {
        struct { const char *hostname; int port, transport; } address;
        struct { esp_err_t (*crt_bundle_attach)(void *); } verification;
    } broker;
    struct { const char *username; } credentials;
    struct { int protocol_ver, keepalive; } session;
    struct { int reconnect_timeout_ms, timeout_ms; } network;
    struct { uint64_t limit; } outbox;
} esp_mqtt_client_config_t;
typedef void (*host_mqtt_callback_t)(void *, esp_event_base_t, int32_t, void *);
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t, int32_t, host_mqtt_callback_t, void *);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char *, const char *, int, int, int, bool);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t, const char *, const char *, int, int, int);
