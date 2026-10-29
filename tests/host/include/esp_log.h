#pragma once
// Keep log arguments type-checked and considered used, without noisy output.
#include <stdio.h>
#define ESP_LOGI(tag, format, ...) do { (void)(tag); if (0) printf(format, ##__VA_ARGS__); } while (0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
