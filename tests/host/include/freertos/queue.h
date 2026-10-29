#pragma once
#include <stddef.h>
#include "FreeRTOS.h"
typedef struct host_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, size_t);
BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
void vQueueDelete(QueueHandle_t);
BaseType_t xQueueReset(QueueHandle_t);
BaseType_t xQueueOverwrite(QueueHandle_t, const void *);
