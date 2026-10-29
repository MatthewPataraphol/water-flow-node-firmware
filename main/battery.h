#pragma once

#include <stdbool.h>

typedef struct {
    bool available;
    bool mock;
    int percent;
    float voltage;
} battery_status_t;

// Hardware boundary: returns unavailable until a real monitor is implemented.
battery_status_t battery_read(void);
