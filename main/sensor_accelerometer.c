#include "sensor_accelerometer.h"
#include "esp_log.h"

esp_err_t sensor_accelerometer_start(void)
{
    // TODO: agree on SPI host/pins/power, IIS3DWB driver, sampling and a validated
    // vibration-to-flow classifier. No dummy SPI transactions or fake results.
    ESP_LOGW("sensor_accel", "SPI accelerometer and classifier are not implemented");
    return ESP_ERR_NOT_SUPPORTED;
}
