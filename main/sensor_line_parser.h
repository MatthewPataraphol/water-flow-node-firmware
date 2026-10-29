#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "flow_types.h"

typedef struct {
    char line[16];
    size_t length;
    bool discard;
} sensor_line_parser_t;

// Feed one byte. Exact case-sensitive FLOW\n or NO_FLOW\n (optional CR).
// Overlong/unknown/binary lines are ignored through their terminating newline.
bool sensor_line_parser_feed(sensor_line_parser_t *parser, char byte, flow_state_t *state);
