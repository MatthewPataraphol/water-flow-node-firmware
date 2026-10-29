#include "time_sync.h"

#include <inttypes.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "time_sync";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_valid;
static int64_t s_anchor_epoch_ms;
static int64_t s_anchor_monotonic_ms;

static void sync_callback(struct timeval *tv)
{
    // Accept only an actual SNTP callback, not a power-on/retained wall clock.
    if (tv->tv_sec < 1704067200LL) { // 2024-01-01 UTC
        ESP_LOGW(TAG, "Ignoring implausible SNTP time");
        return;
    }
    int64_t monotonic = esp_timer_get_time() / 1000;
    int64_t epoch = (int64_t)tv->tv_sec * 1000 + tv->tv_usec / 1000;
    portENTER_CRITICAL(&s_lock);
    s_anchor_epoch_ms = epoch;
    s_anchor_monotonic_ms = monotonic;
    s_valid = true;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "Time synchronized; epoch_ms=%" PRId64, epoch);
}

esp_err_t time_sync_init(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_FLOW_NTP_SERVER);
    config.start = false;
    config.wait_for_sync = false;
    config.sync_cb = sync_callback;
    return esp_netif_sntp_init(&config);
}

esp_err_t time_sync_start(void)
{
    return esp_netif_sntp_start();
}

bool time_sync_is_valid(void)
{
    portENTER_CRITICAL(&s_lock);
    bool valid = s_valid;
    portEXIT_CRITICAL(&s_lock);
    return valid;
}

bool time_sync_epoch_ms_at(int64_t monotonic_ms, int64_t *epoch_ms)
{
    if (!epoch_ms || monotonic_ms < 0) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    bool valid = s_valid;
    int64_t epoch = s_anchor_epoch_ms + (monotonic_ms - s_anchor_monotonic_ms);
    portEXIT_CRITICAL(&s_lock);
    if (valid && epoch > 0) {
        *epoch_ms = epoch;
        return true;
    }
    return false;
}
