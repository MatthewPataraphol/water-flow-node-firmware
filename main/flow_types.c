#include "flow_types.h"

bool flow_state_is_known(flow_state_t state)
{
    return state == FLOW_STATE_NO_FLOW || state == FLOW_STATE_FLOW;
}

const char *flow_state_name(flow_state_t state)
{
    switch (state) {
    case FLOW_STATE_NO_FLOW: return "NO_FLOW";
    case FLOW_STATE_FLOW: return "FLOW";
    default: return "UNKNOWN";
    }
}
