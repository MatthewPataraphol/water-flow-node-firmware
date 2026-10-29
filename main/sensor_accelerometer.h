#pragma once

#include "esp_err.h"

// Future SPI acquisition/classifier should submit ONLY determined states via
// flow_state_report(). It must retry backpressure before submitting later data.
esp_err_t sensor_accelerometer_start(void);
