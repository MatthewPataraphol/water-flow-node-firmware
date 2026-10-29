// Deterministic callback interleavings against the actual MQTT worker.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event_storage.h"
#include "mqtt_manager.h"
#include "mqtt_client.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static bool wifi, synced, auto_ack, fail_enqueue, saw_puback;
static int tick, init_calls, enqueue_calls, ack_calls, ack_failures, publish_calls, stop_calls;
static int current_message_id;
static size_t stored;
static int64_t clock_ms;
static host_mqtt_callback_t callback;
static void (*worker)(void *);
static void (*script)(void);
static jmp_buf done;
static char last_heartbeat[MQTT_HEARTBEAT_MAX_LENGTH];
static const flow_event_t record = {.sequence = 42, .timestamp_ms = 1800000000000LL,
                                    .state = FLOW_STATE_FLOW, .boot_id = 1};

static void notice(int id, int message)
{
    struct { int msg_id; } data = {.msg_id = message};
    assert(callback);
    if (id == MQTT_EVENT_PUBLISHED && message == current_message_id) saw_puback = true;
    callback(NULL, "MQTT", id, &data);
}
int64_t esp_timer_get_time(void) { return clock_ms * 1000; }
bool wifi_manager_is_connected(void) { return wifi; }
bool time_sync_is_valid(void) { return synced; }
bool time_sync_epoch_ms_at(int64_t ms, int64_t *epoch)
{ if (synced) *epoch = 1800000000000LL + ms; return synced; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "fake failure"; }
esp_err_t esp_crt_bundle_attach(void *arg) { (void)arg; return ESP_OK; }
esp_err_t event_storage_resolve_pending(uint64_t boot, int64_t mono, int64_t epoch)
{ assert(synced && boot == 1 && epoch > mono); return ESP_OK; }
size_t event_storage_count(void) { return stored; }
esp_err_t event_storage_peek(flow_event_t *out)
{ if (!stored) return ESP_ERR_NOT_FOUND; *out = record; return ESP_OK; }
esp_err_t event_storage_ack(uint64_t sequence)
{
    assert(sequence == 42 && saw_puback && stored == 1);
    ++ack_calls;
    if (ack_failures) { --ack_failures; return ESP_FAIL; }
    stored = 0;
    return ESP_OK;
}
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    assert(wifi && synced);
    assert(config->broker.address.transport == MQTT_TRANSPORT_OVER_SSL);
    assert(config->broker.address.port == 8883 && config->broker.verification.crt_bundle_attach);
    assert(strcmp(config->credentials.username, "host-test-token") == 0);
    ++init_calls;
    return (void *)1;
}
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t c, int32_t id,
                                        host_mqtt_callback_t fn, void *arg)
{ (void)c; (void)id; (void)arg; callback = fn; return ESP_OK; }
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t c)
{ (void)c; notice(MQTT_EVENT_CONNECTED, 0); return ESP_OK; }
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t c)
{ (void)c; ++stop_calls; notice(MQTT_EVENT_DISCONNECTED, 0); return ESP_OK; }
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t c) { (void)c; return ESP_OK; }
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t c, const char *topic,
                            const char *data, int length, int qos, int retain, bool store)
{
    (void)c;
    assert(stored == 1 && qos == 1 && retain == 0 && store);
    assert(strcmp(topic, "v1/devices/me/telemetry") == 0);
    assert(length == (int)strlen(data));
    assert(strcmp(data, "{\"ts\":1800000000000,\"values\":{\"flow_state\":\"FLOW\"}}") == 0);
    ++enqueue_calls;
    if (fail_enqueue) { fail_enqueue = false; return -2; }
    current_message_id = 100 + enqueue_calls;
    if (auto_ack) notice(MQTT_EVENT_PUBLISHED, current_message_id); // before enqueue returns
    return current_message_id;
}
int esp_mqtt_client_publish(esp_mqtt_client_handle_t c, const char *topic,
                            const char *data, int length, int qos, int retain)
{
    (void)c; (void)topic; (void)length;
    assert(qos == 0 && retain == 0 && wifi);
    ++publish_calls; strcpy(last_heartbeat, data);
    return -1; // Disposable failure must never be retried.
}
struct host_queue { size_t size; unsigned cap, head, count; unsigned char *data; };
QueueHandle_t xQueueCreate(unsigned cap, size_t size)
{
    QueueHandle_t q = calloc(1, sizeof(*q)); assert(q);
    q->cap = cap; q->size = size; q->data = calloc(cap, size); assert(q->data); return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *data, TickType_t ticks)
{
    (void)ticks;
    if (q->count == q->cap) return 0;
    memcpy(q->data + ((q->head + q->count) % q->cap) * q->size, data, q->size);
    ++q->count; return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *data, TickType_t ticks)
{
    (void)ticks;
    if (!q->count) return 0;
    memcpy(data, q->data + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->cap; --q->count; return pdPASS;
}
BaseType_t xQueueReset(QueueHandle_t q) { q->head = q->count = 0; return pdPASS; }
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *data)
{ assert(q->cap == 1); xQueueReset(q); return xQueueSend(q, data, 0); }
EventGroupHandle_t xEventGroupCreate(void) { return calloc(1, sizeof(EventBits_t)); }
EventBits_t xEventGroupGetBits(EventGroupHandle_t e) { return *e; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t e, EventBits_t bits) { return *e |= bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t e, EventBits_t bits)
{ EventBits_t old = *e; *e &= ~bits; return old; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                      void *arg, unsigned priority, void *handle)
{ (void)name; (void)stack; (void)arg; (void)priority; (void)handle; worker = fn; return pdPASS; }
void vTaskDelay(TickType_t ticks) { clock_ms += ticks; ++tick; script(); }

static void reset(void (*fn)(void))
{
    wifi = synced = auto_ack = fail_enqueue = saw_puback = false;
    tick = init_calls = enqueue_calls = ack_calls = ack_failures = publish_calls = stop_calls = 0;
    clock_ms = 1000; stored = 1; callback = NULL; script = fn;
    last_heartbeat[0] = 0;
    assert(mqtt_manager_start(1) == ESP_OK);
}
static void run(void) { if (setjmp(done) == 0) worker(NULL); }

static void reconnect_script(void)
{
    switch (tick) {
    case 1: assert(init_calls == 0); wifi = true; break;
    case 2: assert(init_calls == 0); synced = true; break;
    case 3:
        assert(enqueue_calls == 1 && stored == 1 && ack_calls == 0);
        wifi = false; notice(MQTT_EVENT_DISCONNECTED, 0); break;
    case 4: assert(stored == 1); wifi = true; notice(MQTT_EVENT_CONNECTED, 0); break;
    case 5: assert(enqueue_calls == 1); notice(MQTT_EVENT_PUBLISHED, 999); break;
    case 6: assert(ack_calls == 0); notice(MQTT_EVENT_PUBLISHED, current_message_id); break;
    case 7: assert(ack_calls == 1 && stored == 0 && enqueue_calls == 1); longjmp(done, 1);
    default: assert(tick < 8);
    }
}
static void expiry_script(void)
{
    switch (tick) {
    case 1: assert(enqueue_calls == 1 && stored == 1); clock_ms += 5000; break;
    case 2: assert(enqueue_calls == 2); notice(MQTT_EVENT_DELETED, current_message_id); break;
    case 3: assert(ack_calls == 0 && stored == 1); clock_ms += 5000; auto_ack = true; break;
    case 4: assert(enqueue_calls == 3 && stored == 1); ack_failures = 2; break;
    case 5: assert(ack_calls == 1 && stored == 1 && enqueue_calls == 3); break;
    case 6: assert(ack_calls == 2 && stored == 1 && enqueue_calls == 3); break;
    case 7: assert(ack_calls == 3 && stored == 0 && enqueue_calls == 3); longjmp(done, 1);
    default: assert(tick < 8);
    }
}
static void heartbeat_script(void)
{
    switch (tick) {
    case 1: assert(publish_calls == 0); wifi = synced = true; break;
    case 2:
        assert(publish_calls == 0);
        assert(mqtt_manager_submit_heartbeat("{\"sample\":2}") == ESP_OK);
        assert(mqtt_manager_submit_heartbeat("{\"sample\":3}") == ESP_OK); break;
    case 3: assert(publish_calls == 1 && strcmp(last_heartbeat, "{\"sample\":3}") == 0); break;
    case 4: assert(publish_calls == 1 && enqueue_calls == 0 && ack_calls == 0); longjmp(done, 1);
    default: assert(tick < 5);
    }
}
static void overflow_script(void)
{
    switch (tick) {
    case 1:
        assert(enqueue_calls == 1);
        for (int i = 0; i < 20; ++i) notice(MQTT_EVENT_DISCONNECTED, 0);
        break;
    case 2:
        assert(stop_calls == 1 && stored == 1 && ack_calls == 0);
        clock_ms += 5000; auto_ack = true; break;
    case 3: assert(init_calls == 2 && enqueue_calls == 2 && stored == 1); break;
    case 4: assert(stored == 0 && ack_calls == 1); longjmp(done, 1);
    default: assert(tick < 5);
    }
}

int main(void)
{
    reset(reconnect_script); run();
    puts("PASS: Wi-Fi/SNTP gates, reconnect retains in-flight message, only matching PUBACK removes");
    reset(expiry_script); wifi = synced = true; fail_enqueue = true; run();
    puts("PASS: enqueue failure, outbox expiration, fast PUBACK and failed NVS removal retry");
    reset(heartbeat_script); stored = 0;
    assert(mqtt_manager_submit_heartbeat("{\"sample\":1}") == ESP_OK); run();
    puts("PASS: offline heartbeat discard, newest-only mailbox and disposable QoS0 failure");
    reset(overflow_script); wifi = synced = true; run();
    puts("PASS: callback overflow rebuilds client while preserving unsent history");
    puts("All 4 MQTT host test groups passed.");
    return 0;
}
