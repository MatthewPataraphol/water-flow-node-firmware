// Runs the actual storage, state worker and parser with deterministic fakes.
// The NVS fake models atomic individual key writes, not flash/GC behavior.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event_storage.h"
#include "flow_state.h"
#include "sensor_line_parser.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

typedef struct { char key[16]; unsigned char data[64]; size_t size; } entry_t;
static entry_t flash[EVENT_STORAGE_CAPACITY + 4];
static bool fail_write, fail_erase, synced;
static int64_t clock_ms = 1000;
static void (*worker)(void *);
static jmp_buf idle;

static entry_t *find(const char *key)
{
    for (size_t i = 0; i < sizeof(flash) / sizeof(flash[0]); ++i) {
        if (strcmp(flash[i].key, key) == 0) return &flash[i];
    }
    return NULL;
}
esp_err_t nvs_flash_init_partition(const char *name)
{ assert(strcmp(name, "flow_events") == 0); return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *p, const char *n, int mode, nvs_handle_t *h)
{ (void)p; (void)n; (void)mode; *h = 1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size)
{
    (void)h;
    entry_t *e = find(key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (out) { assert(*size >= e->size); memcpy(out, e->data, e->size); }
    *size = e->size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size)
{
    (void)h;
    if (fail_write) return ESP_FAIL;
    entry_t *e = find(key);
    if (!e) {
        for (size_t i = 0; i < sizeof(flash) / sizeof(flash[0]); ++i) {
            if (!flash[i].key[0]) { e = &flash[i]; break; }
        }
    }
    assert(e && size <= sizeof(e->data));
    strcpy(e->key, key);
    memcpy(e->data, data, size);
    e->size = size;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    (void)h;
    if (fail_erase) return ESP_FAIL;
    entry_t *e = find(key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    memset(e, 0, sizeof(*e));
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "fake error"; }
int64_t esp_timer_get_time(void) { return clock_ms * 1000; }
bool time_sync_epoch_ms_at(int64_t ms, int64_t *epoch)
{ if (synced) *epoch = 1800000000000LL + ms; return synced; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t)
{ (void)s; (void)t; return pdPASS; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdPASS; }
struct host_queue { size_t size; unsigned cap, head, count; unsigned char *data; };
QueueHandle_t xQueueCreate(unsigned cap, size_t size)
{
    QueueHandle_t q = calloc(1, sizeof(*q));
    assert(q);
    q->data = calloc(cap, size); assert(q->data); q->cap = cap; q->size = size; return q;
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
    if (!q->count) longjmp(idle, 1);
    memcpy(data, q->data + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->cap; --q->count; return pdPASS;
}
void vQueueDelete(QueueHandle_t q) { free(q->data); free(q); }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                      void *arg, unsigned priority, void *handle)
{ (void)name; (void)stack; (void)arg; (void)priority; (void)handle; worker = fn; return pdPASS; }
void vTaskDelay(TickType_t ticks) { (void)ticks; assert(!"Unexpected storage retry in worker"); }
static void drain(void) { if (setjmp(idle) == 0) worker(NULL); }
static void reset(void)
{
    memset(flash, 0, sizeof(flash)); fail_write = fail_erase = synced = false;
    assert(event_storage_init() == ESP_OK);
}
static flow_event_t event(flow_state_t state, int64_t timestamp, uint64_t boot)
{
    return (flow_event_t){.state = state, .timestamp_ms = timestamp,
                          .boot_id = boot, .monotonic_ms = clock_ms};
}

static void test_state_changes(void)
{
    reset(); synced = true;
    assert(flow_state_start(1) == ESP_OK);
    assert(flow_state_report(FLOW_STATE_UNKNOWN) == ESP_ERR_INVALID_ARG);
    const flow_state_t states[] = {FLOW_STATE_NO_FLOW, FLOW_STATE_NO_FLOW,
        FLOW_STATE_FLOW, FLOW_STATE_FLOW, FLOW_STATE_NO_FLOW};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        assert(flow_state_report_at(states[i], 100 + (int64_t)i) == ESP_OK);
    }
    drain();
    assert(event_storage_count() == 2);
    flow_event_t out;
    assert(event_storage_peek(&out) == ESP_OK);
    assert(out.state == FLOW_STATE_FLOW && out.timestamp_ms == 1800000000102LL);
    assert(event_storage_ack(out.sequence) == ESP_OK);
    assert(event_storage_peek(&out) == ESP_OK && out.state == FLOW_STATE_NO_FLOW);
    assert(event_storage_ack(out.sequence) == ESP_OK);
    assert(event_storage_init() == ESP_OK); // reboot after an entirely drained queue
    assert(event_storage_last_state() == FLOW_STATE_NO_FLOW);
    assert(flow_state_start(2) == ESP_OK);
    assert(flow_state_report(FLOW_STATE_NO_FLOW) == ESP_OK);
    drain(); assert(event_storage_count() == 0);
    assert(flow_state_report(FLOW_STATE_FLOW) == ESP_OK);
    drain(); assert(event_storage_count() == 1);
    puts("PASS: baseline, deduplication, captured timestamps and state across reboot");
}

static void test_recovery_and_full(void)
{
    reset();
    assert(event_storage_set_baseline(FLOW_STATE_NO_FLOW) == ESP_OK);
    uint64_t boot1, boot2;
    assert(event_storage_next_boot_id(&boot1) == ESP_OK);
    for (unsigned i = 0; i < EVENT_STORAGE_CAPACITY; ++i) {
        flow_event_t e = event(i % 2 ? FLOW_STATE_NO_FLOW : FLOW_STATE_FLOW, 1800000000000LL + i, boot1);
        assert(event_storage_append(&e) == ESP_OK && e.sequence == i + 1);
    }
    flow_event_t e = event(FLOW_STATE_FLOW, 1800000001000LL, boot1);
    assert(event_storage_append(&e) == ESP_ERR_NO_MEM);
    assert(event_storage_init() == ESP_OK);
    assert(event_storage_next_boot_id(&boot2) == ESP_OK && boot2 == boot1 + 1);
    assert(event_storage_count() == EVENT_STORAGE_CAPACITY);
    for (unsigned i = 0; i < EVENT_STORAGE_CAPACITY; ++i) {
        assert(event_storage_peek(&e) == ESP_OK && e.sequence == i + 1);
        assert(event_storage_ack(e.sequence) == ESP_OK);
    }
    assert(event_storage_append(&e) == ESP_OK && e.sequence == EVENT_STORAGE_CAPACITY + 1);
    puts("PASS: persistent FIFO replay, full queue preserves records, boot/sequence counters");
}

static void test_failure_and_power_loss(void)
{
    reset();
    assert(event_storage_set_baseline(FLOW_STATE_NO_FLOW) == ESP_OK);
    flow_event_t e = event(FLOW_STATE_FLOW, 1800000000000LL, 1);
    fail_write = true;
    assert(event_storage_append(&e) == ESP_FAIL && event_storage_count() == 0);
    assert(event_storage_last_state() == FLOW_STATE_NO_FLOW);
    fail_write = false;
    assert(event_storage_append(&e) == ESP_OK);
    fail_write = true;
    assert(event_storage_ack(e.sequence) == ESP_FAIL && event_storage_count() == 1);
    fail_write = false; fail_erase = true;
    assert(event_storage_ack(e.sequence) == ESP_FAIL); // checkpoint committed, event still present
    assert(event_storage_init() == ESP_OK && event_storage_count() == 1);
    assert(event_storage_last_state() == FLOW_STATE_FLOW);
    fail_erase = false;
    assert(event_storage_ack(e.sequence) == ESP_OK);
    assert(event_storage_init() == ESP_OK && event_storage_count() == 0);
    assert(event_storage_last_state() == FLOW_STATE_FLOW);
    puts("PASS: write failure, checkpoint failure and power loss before acknowledged-event deletion");
}

static void test_unsynchronized_events(void)
{
    reset();
    flow_event_t old = event(FLOW_STATE_FLOW, 0, 1);
    flow_event_t current = event(FLOW_STATE_NO_FLOW, 0, 2);
    assert(event_storage_append(&old) == ESP_OK);
    assert(event_storage_append(&current) == ESP_OK);
    flow_event_t out;
    assert(event_storage_peek(&out) == ESP_ERR_NOT_FOUND);
    assert(event_storage_resolve_pending(2, 2000, 1800000002000LL) == ESP_OK);
    assert(event_storage_peek(&out) == ESP_OK && out.sequence == current.sequence);
    assert(out.timestamp_ms == 1800000001000LL);
    assert(event_storage_unresolved_count(2) == 1);
    assert(event_storage_init() == ESP_OK);
    assert(event_storage_peek(&out) == ESP_OK && out.timestamp_ms == 1800000001000LL);
    assert(event_storage_ack(out.sequence) == ESP_OK);
    assert(event_storage_count() == 1 && event_storage_peek(&out) == ESP_ERR_NOT_FOUND);
    puts("PASS: same-boot SNTP backdating and previous-boot unresolved record retention");
}

static void test_invalid_record(void)
{
    reset();
    flow_event_t e = event(FLOW_STATE_FLOW, 1800000000000LL, 1);
    assert(event_storage_append(&e) == ESP_OK);
    find("ev000")->size = 1;
    assert(event_storage_init() == ESP_ERR_INVALID_SIZE);
    assert(find("ev000")); // preserved, not silently erased
    puts("PASS: incompatible/corrupt storage fails closed without erasing records");
}

static void test_backpressure(void)
{
    reset(); assert(flow_state_start(1) == ESP_OK);
    for (unsigned i = 0; i < 32; ++i) assert(flow_state_report(FLOW_STATE_FLOW) == ESP_OK);
    assert(flow_state_report(FLOW_STATE_NO_FLOW) == ESP_ERR_TIMEOUT);
    drain();
    assert(flow_state_report_at(FLOW_STATE_NO_FLOW, 400) == ESP_OK);
    drain();
    assert(event_storage_count() == 1);
    puts("PASS: bounded nonblocking sensor queue exposes backpressure for retry");
}

static void test_parser(void)
{
    sensor_line_parser_t parser = {0};
    flow_state_t state = FLOW_STATE_UNKNOWN;
    const char input[] = "FLOW\r\nNO_FLOW\nflow\ngarbageFLOW\nFLOW\0\n"
                         "012345678901234567890FLOW\nFLOW\n";
    unsigned count = 0;
    for (size_t i = 0; i < sizeof(input) - 1; ++i) {
        if (sensor_line_parser_feed(&parser, input[i], &state)) {
            assert(state == (count == 1 ? FLOW_STATE_NO_FLOW : FLOW_STATE_FLOW));
            ++count;
        }
    }
    assert(count == 3);
    puts("PASS: UART exact lines, CRLF, fragmented input, binary and overlong-line recovery");
}

int main(void)
{
    test_state_changes(); test_recovery_and_full(); test_failure_and_power_loss();
    test_unsynchronized_events(); test_invalid_record(); test_backpressure(); test_parser();
    puts("All 7 host test groups passed.");
    return 0;
}
