#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FLOW_STATE_UNKNOWN = 0,
    FLOW_STATE_NO_FLOW,
    FLOW_STATE_FLOW,
} flow_state_t;

// Fixed-width fields are also used in the versioned NVS record format.
typedef struct {
    uint64_t sequence;
    int64_t timestamp_ms;     // Zero until synchronized; never publish zero.
    int64_t monotonic_ms;     // Measurement time since this boot.
    uint64_t boot_id;         // Prevents backdating records from a previous boot.
    uint32_t state;
    uint32_t version;
} flow_event_t;

bool flow_state_is_known(flow_state_t state);
const char *flow_state_name(flow_state_t state);
